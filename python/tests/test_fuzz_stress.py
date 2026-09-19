"""Fuzz and stress tests for parameter tolerance, coordinate resolution, and object storms."""

from __future__ import annotations

import random
from unittest.mock import AsyncMock, MagicMock, patch
import pytest
from fastmcp import Client

from qtpilot.server import create_server
from qtpilot.tools.cu import _resolve_coords


class TestFuzzStress:
    @pytest.fixture
    def mock_probe(self):
        probe = MagicMock()
        probe.call = AsyncMock(return_value={"ok": True})
        probe.is_connected = True
        probe.ws_url = "ws://localhost:9222"
        return probe

    def test_fuzz_resolve_coords_valid_shapes(self):
        """Fuzz test _resolve_coords with various valid parameter representations."""
        rng = random.Random(42)
        for _ in range(100):
            x = rng.randint(-5000, 5000)
            y = rng.randint(-5000, 5000)

            # Direct x, y
            res_direct = _resolve_coords(x=x, y=y)
            assert res_direct == (x, y)

            # List coordinate [x, y]
            res_list = _resolve_coords(coordinate=[x, y])
            assert res_list == (x, y)

            # Tuple coordinate (x, y)
            res_tuple = _resolve_coords(coordinate=(x, y))
            assert res_tuple == (x, y)

            # Dict coordinate {"x": x, "y": y}
            res_dict = _resolve_coords(point={"x": x, "y": y})
            assert res_dict == (x, y)

    def test_fuzz_resolve_coords_invalid_shapes(self):
        """Fuzz test _resolve_coords with malformed or incomplete shapes raising ValueError."""
        invalid_cases = [
            {},
            {"x": 10},
            {"y": 20},
            {"coordinate": []},
            {"coordinate": [10]},
            {"point": {}},
            {"point": {"x": 10}},
            {"point": {"y": 20}},
        ]
        for kwargs in invalid_cases:
            with pytest.raises(ValueError):
                _resolve_coords(**kwargs)

    @pytest.mark.asyncio
    async def test_objects_tree_defaults_max_depth_to_protect_channel(self, mock_probe):
        """Verify qt_objects_tree defaults maxDepth=3 to prevent full-graph dumps on startup object storms."""
        with patch("qtpilot.server.require_probe", return_value=mock_probe):
            mcp = create_server(mode="native")
            async with Client(mcp) as client:
                # Default call: should send maxDepth=3
                await client.call_tool("qt_objects_tree", {})
                mock_probe.call.assert_awaited_with("qt.objects.tree", {"maxDepth": 3})

                # Explicit maxDepth call
                await client.call_tool("qt_objects_tree", {"maxDepth": 5})
                mock_probe.call.assert_awaited_with("qt.objects.tree", {"maxDepth": 5})

                # Explicit unbound maxDepth (-1)
                await client.call_tool("qt_objects_tree", {"maxDepth": -1})
                mock_probe.call.assert_awaited_with("qt.objects.tree", {"maxDepth": -1})

    @pytest.mark.asyncio
    async def test_rapid_fuzz_tool_invocations(self, mock_probe):
        """Simulate rapid client requests with permutations of parameter names and formats."""
        rng = random.Random(1337)
        with patch("qtpilot.server.require_probe", return_value=mock_probe):
            mcp = create_server(mode="all")
            async with Client(mcp) as client:
                for i in range(50):
                    # Search with permutations of aliases
                    search_params = {}
                    if rng.choice([True, False]):
                        search_params[rng.choice(["objectName", "name"])] = f"obj_{i}"
                    if rng.choice([True, False]):
                        search_params[rng.choice(["className", "class_name"])] = rng.choice(
                            ["QPushButton", "QLabel", "QWidget"]
                        )
                    if rng.choice([True, False]):
                        search_params[rng.choice(["root", "rootId"])] = f"root_{i}"
                    await client.call_tool("qt_objects_search", search_params)

                    # Inspect with permutations of parts
                    inspect_params = {"objectId": f"obj_{i}"}
                    part_choice = rng.choice(["all", "info", "info, properties", ["info", "geometry"], None])
                    if part_choice is not None:
                        param_name = rng.choice(["parts", "part"])
                        inspect_params[param_name] = part_choice
                    await client.call_tool("qt_objects_inspect", inspect_params)

                    # Native click with position shapes
                    click_params = {"objectId": f"btn_{i}"}
                    coord_choice = rng.choice(["list", "dict", "tuple", "none"])
                    if coord_choice == "list":
                        click_params["position"] = [rng.randint(0, 500), rng.randint(0, 500)]
                    elif coord_choice == "tuple":
                        click_params["position"] = (rng.randint(0, 500), rng.randint(0, 500))
                    elif coord_choice == "dict":
                        click_params["position"] = {"x": rng.randint(0, 500), "y": rng.randint(0, 500)}
                    await client.call_tool("qt_ui_click", click_params)

                    # Computer use click with parameter aliases
                    cu_params = {}
                    cu_choice = rng.choice(["flat", "coord", "point"])
                    if cu_choice == "flat":
                        cu_params["x"] = rng.randint(0, 500)
                        cu_params["y"] = rng.randint(0, 500)
                    elif cu_choice == "coord":
                        cu_params["coordinate"] = [rng.randint(0, 500), rng.randint(0, 500)]
                    elif cu_choice == "point":
                        cu_params["point"] = {"x": rng.randint(0, 500), "y": rng.randint(0, 500)}
                    await client.call_tool("cu_leftClick", cu_params)

            assert mock_probe.call.await_count == 200
