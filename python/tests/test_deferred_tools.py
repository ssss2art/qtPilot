"""The deferred switch on tools whose call runs application code."""

from __future__ import annotations

from unittest.mock import AsyncMock, MagicMock, patch

import pytest
from fastmcp import Client

from qtpilot.server import create_server


@pytest.fixture
def mock_probe() -> MagicMock:
    probe = MagicMock()
    probe.call = AsyncMock(return_value={"ok": True})
    probe.is_connected = True
    probe.ws_url = "ws://localhost:9222"
    return probe


async def _call(mock_probe: MagicMock, tool: str, arguments: dict[str, object]) -> None:
    with patch("qtpilot.server.require_probe", return_value=mock_probe):
        async with Client(create_server(mode="native")) as client:
            await client.call_tool(tool, arguments)


@pytest.mark.asyncio
async def test_methods_invoke_forwards_deferred(mock_probe: MagicMock) -> None:
    await _call(
        mock_probe,
        "qt_methods_invoke",
        {"objectId": "dialog", "method": "exec", "deferred": True},
    )

    mock_probe.call.assert_awaited_once_with(
        "qt.methods.invoke", {"objectId": "dialog", "method": "exec", "deferred": True}
    )


@pytest.mark.asyncio
async def test_methods_invoke_stays_synchronous_by_default(mock_probe: MagicMock) -> None:
    # A synchronous call is the only one that can return the method's result.
    await _call(mock_probe, "qt_methods_invoke", {"objectId": "dialog", "method": "isVisible"})

    mock_probe.call.assert_awaited_once_with(
        "qt.methods.invoke", {"objectId": "dialog", "method": "isVisible"}
    )


@pytest.mark.asyncio
async def test_activate_menu_item_defers_by_default(mock_probe: MagicMock) -> None:
    await _call(mock_probe, "qt_ui_activateMenuItem", {"text": "Delete"})

    mock_probe.call.assert_awaited_once_with(
        "qt.ui.activateMenuItem", {"text": "Delete", "deferred": True}
    )


@pytest.mark.asyncio
async def test_activate_menu_item_can_run_inline(mock_probe: MagicMock) -> None:
    await _call(mock_probe, "qt_ui_activateMenuItem", {"text": "Delete", "deferred": False})

    mock_probe.call.assert_awaited_once_with(
        "qt.ui.activateMenuItem", {"text": "Delete", "deferred": False}
    )


@pytest.mark.asyncio
async def test_activate_menu_item_forwards_a_label_path(mock_probe: MagicMock) -> None:
    await _call(mock_probe, "qt_ui_activateMenuItem", {"path": ["Export", "As PDF"]})

    mock_probe.call.assert_awaited_once_with(
        "qt.ui.activateMenuItem", {"path": ["Export", "As PDF"], "deferred": True}
    )


@pytest.mark.asyncio
@pytest.mark.parametrize(
    "arguments",
    [{}, {"text": "Delete", "path": ["Delete"]}, {"path": []}],
    ids=["neither", "both", "empty-path"],
)
async def test_activate_menu_item_needs_exactly_one_of_text_or_path(
    mock_probe: MagicMock, arguments: dict[str, object]
) -> None:
    with patch("qtpilot.server.require_probe", return_value=mock_probe):
        async with Client(create_server(mode="native")) as client:
            result = await client.call_tool(
                "qt_ui_activateMenuItem", arguments, raise_on_error=False
            )

    assert result.is_error
    mock_probe.call.assert_not_awaited()
