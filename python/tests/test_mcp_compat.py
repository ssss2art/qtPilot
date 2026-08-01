"""Unit tests for the FastMCP/MCP SDK compatibility layer.

These assert behaviour that must hold on *every* supported SDK generation, so
they run unchanged against fastmcp 2.x, 3.x, and 4.x.
"""

from __future__ import annotations

import pytest

from fastmcp import FastMCP

from qtpilot import _mcp_compat as mcp_compat


class TestVersionDetection:
    def test_fastmcp_version_is_non_empty(self):
        assert mcp_compat.fastmcp_version()
        assert mcp_compat.fastmcp_version() != "0"

    def test_fastmcp_major_is_supported(self):
        assert mcp_compat.fastmcp_major() in (2, 3, 4)

    def test_protocol_revision_is_dated(self):
        revision = mcp_compat.protocol_revision()
        assert revision != "unknown"
        # YYYY-MM-DD
        assert len(revision) == 10
        assert revision[4] == revision[7] == "-"

    def test_stateless_flag_matches_revision(self):
        expected = mcp_compat.protocol_revision() >= mcp_compat.REVISION_STATELESS
        assert mcp_compat.is_stateless_protocol() is expected

    def test_major_4_implies_stateless(self):
        """FastMCP 4 is the generation that moved to 2026-07-28."""
        if mcp_compat.fastmcp_major() >= 4:
            assert mcp_compat.is_stateless_protocol()
        else:
            assert mcp_compat.protocol_revision() == mcp_compat.REVISION_LEGACY


class TestDescribe:
    def test_describe_keys(self):
        info = mcp_compat.describe()
        assert set(info) == {
            "fastmcp_version",
            "fastmcp_major",
            "mcp_protocol_revision",
            "stateless_protocol",
        }

    def test_describe_is_json_safe(self):
        import json

        json.dumps(mcp_compat.describe())  # must not raise


class TestToolEnumeration:
    @pytest.mark.asyncio
    async def test_lists_registered_tools(self):
        mcp = FastMCP("enumeration-test")

        @mcp.tool
        def qt_alpha() -> str:
            return "a"

        @mcp.tool
        def chr_beta() -> str:
            return "b"

        names = await mcp_compat.list_tool_names(mcp)
        assert set(names) == {"qt_alpha", "chr_beta"}

    @pytest.mark.asyncio
    async def test_empty_server_lists_nothing(self):
        assert await mcp_compat.list_tool_names(FastMCP("empty")) == []

    @pytest.mark.asyncio
    async def test_find_tool_returns_callable(self):
        mcp = FastMCP("find-test")

        @mcp.tool
        def qt_alpha() -> str:
            return "a"

        tool = await mcp_compat.find_tool(mcp, "qt_alpha")
        assert tool is not None
        assert tool.fn() == "a"

    @pytest.mark.asyncio
    async def test_find_tool_missing_returns_none(self):
        assert await mcp_compat.find_tool(FastMCP("find-test"), "nope") is None


class TestToolRemoval:
    def test_removal_support_matches_generation(self):
        """FastMCP 4 dropped remove_tool; earlier generations have it."""
        mcp = FastMCP("removal-test")
        expected = mcp_compat.fastmcp_major() < 4
        assert mcp_compat.supports_tool_removal(mcp) is expected

    @pytest.mark.asyncio
    async def test_remove_tools_reports_what_it_removed(self):
        mcp = FastMCP("removal-test")

        @mcp.tool
        def qt_alpha() -> str:
            return "a"

        @mcp.tool
        def chr_beta() -> str:
            return "b"

        removed = mcp_compat.remove_tools(mcp, ["qt_alpha"])
        remaining = set(await mcp_compat.list_tool_names(mcp))

        if mcp_compat.supports_tool_removal(mcp):
            assert removed == ["qt_alpha"]
            assert remaining == {"chr_beta"}
        else:
            # No removal API: report nothing removed and leave the surface alone
            # rather than claiming a narrowing that did not happen.
            assert removed == []
            assert remaining == {"qt_alpha", "chr_beta"}

    def test_transform_support_matches_generation(self):
        """Transforms arrived in FastMCP 3."""
        mcp = FastMCP("transform-test")
        expected = mcp_compat.fastmcp_major() >= 3
        assert mcp_compat.supports_transforms(mcp) is expected


PREFIXES = {"native": ["qt_"], "cu": ["cu_"], "chrome": ["chr_"]}

requires_transforms = pytest.mark.skipif(
    not mcp_compat.supports_transforms(FastMCP("transform-probe")),
    reason=f"FastMCP {mcp_compat.fastmcp_version()} has no transform support",
)


def _server_with_modes(get_mode):
    mcp = FastMCP("mode-visibility-test")

    @mcp.tool
    def qt_ping() -> str:
        return "qt"

    @mcp.tool
    def cu_screenshot() -> str:
        return "cu"

    @mcp.tool
    def chr_find() -> str:
        return "chr"

    @mcp.tool
    def qtpilot_status() -> str:
        return "session"

    assert mcp_compat.install_mode_visibility(mcp, get_mode, PREFIXES)
    return mcp


class TestModeVisibility:
    """Mode filtering via transform — the FastMCP 3+ narrowing path."""

    @requires_transforms
    @pytest.mark.asyncio
    async def test_all_mode_shows_everything(self):
        mcp = _server_with_modes(lambda: "all")
        assert set(await mcp_compat.list_tool_names(mcp)) == {
            "qt_ping",
            "cu_screenshot",
            "chr_find",
            "qtpilot_status",
        }

    @requires_transforms
    @pytest.mark.asyncio
    @pytest.mark.parametrize(
        "mode,expected",
        [
            ("native", "qt_ping"),
            ("cu", "cu_screenshot"),
            ("chrome", "chr_find"),
        ],
    )
    async def test_single_mode_shows_only_its_own_tools(self, mode, expected):
        mcp = _server_with_modes(lambda: mode)
        names = set(await mcp_compat.list_tool_names(mcp))
        assert names == {expected, "qtpilot_status"}

    @requires_transforms
    @pytest.mark.asyncio
    async def test_mode_agnostic_tools_survive_every_mode(self):
        """Session tools belong to no mode and must never be filtered out."""
        for mode in ("all", "native", "cu", "chrome"):
            mcp = _server_with_modes(lambda m=mode: m)
            assert "qtpilot_status" in await mcp_compat.list_tool_names(mcp)

    @requires_transforms
    @pytest.mark.asyncio
    async def test_mode_is_read_per_request_not_captured(self):
        """Switching modes takes effect without re-registering anything."""
        mode = {"value": "all"}
        mcp = _server_with_modes(lambda: mode["value"])

        assert len(await mcp_compat.list_tool_names(mcp)) == 4
        mode["value"] = "native"
        assert set(await mcp_compat.list_tool_names(mcp)) == {
            "qt_ping",
            "qtpilot_status",
        }
        mode["value"] = "all"
        assert len(await mcp_compat.list_tool_names(mcp)) == 4

    @requires_transforms
    @pytest.mark.asyncio
    async def test_hidden_tool_is_not_resolvable(self):
        """A filtered tool must also be uncallable, not merely unlisted."""
        mcp = _server_with_modes(lambda: "native")
        assert await mcp_compat.find_tool(mcp, "qt_ping") is not None
        assert await mcp_compat.find_tool(mcp, "cu_screenshot") is None

    @requires_transforms
    @pytest.mark.asyncio
    async def test_hidden_tool_cannot_be_called_by_a_stale_client(self):
        """End-to-end: a client holding an old tool list gets refused."""
        from fastmcp import Client

        mcp = _server_with_modes(lambda: "native")
        async with Client(mcp) as client:
            assert (await client.call_tool("qt_ping", {})) is not None
            with pytest.raises(Exception):
                await client.call_tool("cu_screenshot", {})

    @requires_transforms
    @pytest.mark.asyncio
    async def test_registration_order_is_stable_across_modes(self):
        """Filtering preserves order, so tools/list stays cache-friendly."""
        full = await mcp_compat.list_tool_names(_server_with_modes(lambda: "all"))
        native = await mcp_compat.list_tool_names(_server_with_modes(lambda: "native"))
        assert native == [n for n in full if n in set(native)]

    def test_install_is_a_noop_without_transform_support(self, monkeypatch):
        """Callers must be able to detect the fallback, not silently lose filtering."""
        monkeypatch.setattr(mcp_compat, "supports_transforms", lambda _mcp: False)
        assert (
            mcp_compat.install_mode_visibility(
                FastMCP("no-transforms"), lambda: "native", PREFIXES
            )
            is False
        )


class TestToolRemovalFallback:
    def test_remove_unknown_tool_is_not_fatal(self):
        """One bad name must not wedge a batch mid-mode-switch."""
        mcp = FastMCP("removal-test")

        @mcp.tool
        def qt_alpha() -> str:
            return "a"

        removed = mcp_compat.remove_tools(mcp, ["does_not_exist", "qt_alpha"])
        if mcp_compat.supports_tool_removal(mcp):
            assert removed == ["qt_alpha"]
        else:
            assert removed == []
