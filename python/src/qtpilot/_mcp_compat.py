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
API                     2.14.7   3.4.5    4.0.0b1
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
