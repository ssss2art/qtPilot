"""Tests for the ``qtpilot://status`` MCP resource.

Beyond checking the payload, these lock in the resource's **cache policy**.
MCP `2026-07-28` made `resources/read` results cacheable via `ttlMs` and
`cacheScope`, and `qtpilot://status` is the one thing qtPilot serves that must
never be cached: it reports live probe connection state that changes underneath
the client. The MCP SDK currently defaults to `ttlMs=0` / `cacheScope="private"`,
which is exactly right — these tests fail loudly if a future SDK bump changes
that default and starts letting clients serve stale connection state.
"""

from __future__ import annotations

import json

import pytest

from fastmcp import Client

from qtpilot import _mcp_compat as mcp_compat
from qtpilot.server import create_server

STATUS_URI = "qtpilot://status"

# ttlMs / cacheScope only exist on SDKs implementing MCP 2026-07-28.
requires_cacheable_results = pytest.mark.skipif(
    not mcp_compat.is_stateless_protocol(),
    reason=(
        f"MCP {mcp_compat.protocol_revision()} has no CacheableResult fields "
        "(added in 2026-07-28)"
    ),
)


class TestStatusResourceRegistration:
    @pytest.mark.asyncio
    async def test_resource_is_registered(self):
        mcp = create_server(mode="native")
        async with Client(mcp) as client:
            uris = {str(r.uri) for r in await client.list_resources()}
        assert STATUS_URI in uris

    @pytest.mark.asyncio
    async def test_resource_payload_shape(self):
        mcp = create_server(mode="native")
        async with Client(mcp) as client:
            contents = await client.read_resource(STATUS_URI)

        payload = json.loads(contents[0].text)
        assert payload["connected"] is False  # no probe wired in test
        assert payload["ws_url"] is None
        assert payload["mode"] == "native"
        assert "discovery_active" in payload
        assert "discovered_probes" in payload


class TestStatusResourceCachePolicy:
    """`qtpilot://status` must be served as non-cacheable."""

    @requires_cacheable_results
    @pytest.mark.asyncio
    async def test_status_read_is_not_cacheable(self):
        mcp = create_server(mode="native")
        async with Client(mcp) as client:
            result = await client.session.read_resource(STATUS_URI)

        # 0 == "do not cache". Anything else lets a client serve stale probe
        # connection state, which is the whole point of this resource.
        assert result.ttl_ms == 0, (
            f"qtpilot://status served with ttlMs={result.ttl_ms}; live connection "
            "state must not be cached"
        )
        # "private" keeps shared intermediaries from caching it at all.
        assert result.cache_scope == "private"

    @requires_cacheable_results
    @pytest.mark.asyncio
    async def test_repeated_reads_reflect_mode_change(self):
        """A second read must observe state changed between reads."""
        mcp = create_server(mode="native")
        async with Client(mcp) as client:
            first = json.loads((await client.read_resource(STATUS_URI))[0].text)

            from qtpilot.server import get_state

            await get_state().set_mode("all")

            second = json.loads((await client.read_resource(STATUS_URI))[0].text)

        assert first["mode"] == "native"
        assert second["mode"] == "all"


class TestListResultsCachePolicy:
    """The list endpoints qtPilot serves are cacheable; record what they say."""

    @requires_cacheable_results
    @pytest.mark.asyncio
    async def test_list_tools_cache_scope_is_private(self):
        """qtPilot's tool list is per-user, never shared-cacheable.

        The surface depends on the launch mode of this specific server process,
        so a shared intermediary must not serve one user's list to another.
        """
        mcp = create_server(mode="native")
        async with Client(mcp) as client:
            result = await client.session.list_tools()

        assert result.cache_scope == "private"

    @requires_cacheable_results
    @pytest.mark.asyncio
    async def test_tool_list_order_is_deterministic(self):
        """`2026-07-28` asks for a stable `tools/list` order.

        Two servers built with the same mode must list tools in the same order,
        or client-side caching and prompt-cache hit rates suffer.
        """
        async with Client(create_server(mode="all")) as client:
            first = [t.name for t in (await client.session.list_tools()).tools]
        async with Client(create_server(mode="all")) as client:
            second = [t.name for t in (await client.session.list_tools()).tools]

        assert first == second
