"""Compatibility shim across FastMCP/MCP SDK generations.

qtPilot targets two MCP protocol revisions at once:

======================  ==================  ===================================
SDK generation          MCP revision        Notes
======================  ==================  ===================================
``fastmcp>=2.9,<3``     ``2025-11-25``      legacy; the default resolve today
``fastmcp>=3,<4``       ``2025-11-25``      3.x does not move the protocol
``fastmcp>=4``          ``2026-07-28``      stateless core; beta as of 2026-08
======================  ==================  ===================================

The APIs qtPilot depends on shifted underneath it across those generations:

======================  =======  =======  =========
API                     2.14.7   3.4.5    4.0.3
======================  =======  =======  =========
``_tool_manager``       present  gone     gone
``remove_tool()``       present  present  gone
``list_tools()``        absent   async    async
``add_tool()``          present  present  present
======================  =======  =======  =========

Every version-specific access lives here so the rest of the package can stay
generation-agnostic. Nothing outside this module may touch a FastMCP private
attribute.
"""

from __future__ import annotations

import logging
from collections.abc import Callable, Mapping, Sequence
from typing import TYPE_CHECKING, Any

if TYPE_CHECKING:  # pragma: no cover - typing only
    from fastmcp import FastMCP

logger = logging.getLogger(__name__)

#: Revision advertised by SDK generations that predate the stateless rewrite.
REVISION_LEGACY = "2025-11-25"
#: Revision introduced by the stateless rewrite (fastmcp 4 / mcp 2).
REVISION_STATELESS = "2026-07-28"


def fastmcp_version() -> str:
    """Return the installed FastMCP version string, or ``"0"`` if unknown."""
    try:
        import fastmcp
    except ImportError:  # pragma: no cover - fastmcp is a hard dependency
        return "0"
    return getattr(fastmcp, "__version__", "0")


def fastmcp_major() -> int:
    """Return the major version of the installed FastMCP, or ``0`` if unknown."""
    head = fastmcp_version().split(".", 1)[0]
    try:
        return int(head)
    except ValueError:
        return 0


def protocol_revision() -> str:
    """Return the MCP revision advertised by the resolved ``mcp`` SDK.

    Read from the SDK rather than inferred from the FastMCP version, so the
    answer stays correct if the two are upgraded independently.
    """
    for module, attr in (
        ("mcp.types", "LATEST_PROTOCOL_VERSION"),
        ("mcp.shared.version", "LATEST_PROTOCOL_VERSION"),
    ):
        try:
            mod = __import__(module, fromlist=["_"])
        except ImportError:
            continue
        value = getattr(mod, attr, None)
        if value:
            return str(value)
    logger.debug("Could not determine MCP protocol revision from the mcp SDK")
    return "unknown"


def is_stateless_protocol() -> bool:
    """True when the resolved SDK speaks the stateless (2026-07-28+) protocol."""
    return protocol_revision() >= REVISION_STATELESS


async def notify_tool_list_changed(ctx: Any) -> bool:
    """Tell the client its cached tools/list is stale, across SDK generations.

    FastMCP 2.x/3.x expose ``Context.send_tool_list_changed()``. FastMCP 4
    removed it in favour of the generic ``Context.send_notification()`` taking
    an ``mcp.types`` notification instance. Calling the old name on 4 raises
    AttributeError, which is what made every SUCCESSFUL mode switch fail there
    -- the server state had already changed, so the client saw an error for an
    operation that had in fact happened.

    Best-effort by design: the notification is a cache-invalidation hint, not
    part of the mode switch. A client that never receives it re-lists on its own
    schedule, which is strictly better than failing a completed operation.

    :returns: True if a notification was delivered, False if this SDK offers no
        way to send one. Never raises.
    """
    legacy = getattr(ctx, "send_tool_list_changed", None)
    if callable(legacy):
        try:
            await legacy()
            return True
        except Exception:  # noqa: BLE001 - hint only; never fail the caller
            logger.debug("send_tool_list_changed() failed", exc_info=True)
            return False

    send = getattr(ctx, "send_notification", None)
    if callable(send):
        try:
            import mcp.types as mcp_types

            await send(mcp_types.ToolListChangedNotification())
            return True
        except Exception:  # noqa: BLE001 - as above
            logger.debug("send_notification(ToolListChanged) failed", exc_info=True)
            return False

    logger.debug("No tools/list_changed notification API on this SDK")
    return False


async def list_tool_names(mcp: FastMCP) -> list[str]:
    """Return the names of every tool currently registered on ``mcp``.

    Tries each generation's public accessor before falling back to the private
    manager that only FastMCP 2.x has. The fallback is the sole remaining
    private-attribute access in the package and disappears when the 2.x floor
    is raised.
    """
    lister = getattr(mcp, "list_tools", None)
    if callable(lister):  # fastmcp 3.x / 4.x -> list[Tool]
        return [tool.name for tool in await lister()]

    getter = getattr(mcp, "get_tools", None)
    if callable(getter):  # fastmcp 2.x -> dict[str, Tool]
        return list(await getter())

    manager = getattr(mcp, "_tool_manager", None)
    tools = getattr(manager, "_tools", None)
    if tools is not None:  # pragma: no cover - very old fastmcp only
        logger.debug("Falling back to _tool_manager for tool enumeration")
        return list(tools)

    logger.warning(
        "No known tool-enumeration API on FastMCP %s; treating server as empty",
        fastmcp_version(),
    )
    return []


async def find_tool(mcp: FastMCP, name: str) -> Any | None:
    """Return the registered tool object called ``name``, or ``None``.

    Used by tests to invoke a tool's underlying callable without reaching into
    the server's internals.
    """
    lister = getattr(mcp, "list_tools", None)
    if callable(lister):  # fastmcp 3.x / 4.x
        for tool in await lister():
            if tool.name == name:
                return tool
        return None

    getter = getattr(mcp, "get_tools", None)
    if callable(getter):  # fastmcp 2.x
        return (await getter()).get(name)

    manager = getattr(mcp, "_tool_manager", None)
    tools = getattr(manager, "_tools", None)
    return tools.get(name) if tools is not None else None


def supports_tool_removal(mcp: FastMCP) -> bool:
    """True when tools can be removed from a live server.

    FastMCP 4 dropped ``remove_tool``: a tool surface that mutates per-connection
    conflicts with the stateless protocol's cacheable, deterministically-ordered
    ``tools/list``. Callers must fall back to visibility filtering.
    """
    return callable(getattr(mcp, "remove_tool", None))


def remove_tools(mcp: FastMCP, names: list[str]) -> list[str]:
    """Remove the named tools from ``mcp``; return the names actually removed.

    Returns an empty list when the SDK does not support removal, leaving the
    caller to decide whether that is fatal. Individual failures are logged and
    skipped rather than aborting the batch, so a partially-renamed tool set
    cannot wedge a mode switch.
    """
    remover = getattr(mcp, "remove_tool", None)
    if not callable(remover):
        logger.debug(
            "FastMCP %s does not support remove_tool; %d tool(s) left registered",
            fastmcp_version(),
            len(names),
        )
        return []

    removed: list[str] = []
    for name in names:
        try:
            remover(name)
        except Exception as exc:  # noqa: BLE001 - never let one tool wedge the batch
            logger.debug("Could not remove tool %r: %s", name, exc)
            continue
        removed.append(name)
    return removed


def supports_transforms(mcp: FastMCP) -> bool:
    """True when the SDK can filter the tool surface with a transform.

    FastMCP 3 introduced the provider/transform architecture. It is the
    sanctioned replacement for mutating the registered tool set: a transform
    filters each ``tools/list`` at request time, leaving registration itself
    static and deterministically ordered.
    """
    if not callable(getattr(mcp, "add_transform", None)):
        return False
    try:
        import fastmcp.server.transforms  # noqa: F401
    except ImportError:
        return False
    return True


def _build_mode_visibility_transform(
    get_mode: Callable[[], str],
    mode_prefixes: Mapping[str, Sequence[str]],
) -> Any:
    """Build a transform hiding tools that don't belong to the active mode.

    Defined inside the function because ``fastmcp.server.transforms`` does not
    exist before FastMCP 3.

    ``get_mode`` is read on every request rather than captured, so switching
    modes takes effect immediately without re-registering anything.
    """
    from fastmcp.server.transforms import Transform

    class ModeVisibility(Transform):
        """Filters mode-owned tools down to the active mode."""

        def __repr__(self) -> str:
            return f"ModeVisibility(mode={get_mode()!r})"

        @staticmethod
        def _owner(name: str) -> str | None:
            """Return the mode owning ``name``, or None if it is mode-agnostic."""
            for mode, prefixes in mode_prefixes.items():
                if any(name.startswith(p) for p in prefixes):
                    return mode
            return None

        def _visible(self, name: str) -> bool:
            owner = self._owner(name)
            if owner is None:
                # Session tools (qtpilot_*) and recording tools belong to no
                # mode and must stay reachable in every mode.
                return True
            mode = get_mode()
            return mode == "all" or mode == owner

        async def list_tools(self, tools):
            return [t for t in tools if self._visible(t.name)]

        async def get_tool(self, name, call_next, *, version=None):
            tool = await call_next(name, version=version)
            if tool is None or not self._visible(name):
                # Hide from resolution too, so a stale client cannot call a
                # tool that no longer appears in tools/list.
                return None
            return tool

    return ModeVisibility()


def install_mode_visibility(
    mcp: FastMCP,
    get_mode: Callable[[], str],
    mode_prefixes: Mapping[str, Sequence[str]],
) -> bool:
    """Install mode-based tool filtering on ``mcp``. True if it took effect.

    Returns False on SDK generations without transforms, leaving the caller to
    fall back to registering and unregistering tools.
    """
    if not supports_transforms(mcp):
        return False
    try:
        mcp.add_transform(_build_mode_visibility_transform(get_mode, mode_prefixes))
    except Exception as exc:  # noqa: BLE001 - never block startup over this
        logger.warning("Could not install mode visibility transform: %s", exc)
        return False
    return True


def describe() -> dict[str, Any]:
    """Return a diagnostic snapshot of the resolved MCP stack.

    Surfaced through ``qtpilot_status`` so a bug report states which SDK
    generation and protocol revision produced it.
    """
    revision = protocol_revision()
    return {
        "fastmcp_version": fastmcp_version(),
        "fastmcp_major": fastmcp_major(),
        "mcp_protocol_revision": revision,
        "stateless_protocol": revision >= REVISION_STATELESS,
    }
