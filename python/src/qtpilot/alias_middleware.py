"""FastMCP middleware that normalizes tool arguments and alias parameters."""

from __future__ import annotations

from typing import Any
from fastmcp.server.middleware import Middleware

# Mapping of tool_name -> dict of {alias_param: canonical_param}
ALIAS_MAP: dict[str, dict[str, str]] = {
    "qt_objects_search": {
        "name": "objectName",
        "class_name": "className",
        "root_id": "root",
        "rootId": "root",
    },
    "qt_objects_inspect": {
        "part": "parts",
        "declaredOnly": "declared_only",
        "propertyName": "property_name",
    },
    "qt_ui_sendKeys": {
        "key": "sequence",
        "keys": "sequence",
    },
    "qt_models_search": {
        "maxHits": "max_hits",
    },
    "qt_ui_clickItem": {
        "target": "itemPath",
    },
    "cu_leftClick": {
        "screenAbsolute": "screen_absolute",
        "delayMs": "delay_ms",
    },
    "cu_rightClick": {
        "screenAbsolute": "screen_absolute",
        "delayMs": "delay_ms",
    },
    "cu_middleClick": {
        "screenAbsolute": "screen_absolute",
        "delayMs": "delay_ms",
    },
    "cu_doubleClick": {
        "screenAbsolute": "screen_absolute",
        "delayMs": "delay_ms",
    },
    "cu_tripleClick": {
        "screenAbsolute": "screen_absolute",
        "delayMs": "delay_ms",
    },
    "cu_mouseMove": {
        "screenAbsolute": "screen_absolute",
        "delayMs": "delay_ms",
    },
    "cu_mouseDrag": {
        "screenAbsolute": "screen_absolute",
        "delayMs": "delay_ms",
    },
    "cu_mouseDown": {
        "screenAbsolute": "screen_absolute",
    },
    "cu_mouseUp": {
        "screenAbsolute": "screen_absolute",
    },
    "cu_scroll": {
        "screenAbsolute": "screen_absolute",
    },
    "cu_key": {
        "text": "key",
    },
    "chr_formInput": {
        "text": "value",
    },
}


class AliasMiddleware(Middleware):
    """Normalize parameter aliases and casing differences before FastMCP schema validation."""

    async def on_call_tool(self, context: Any, call_next: Any) -> Any:
        tool_name = getattr(context.message, "name", None)
        raw_args = getattr(context.message, "arguments", None)
        if tool_name and isinstance(raw_args, dict):
            args = dict(raw_args)
            mappings = ALIAS_MAP.get(tool_name)
            if mappings:
                for alias, canonical in mappings.items():
                    if alias in args:
                        val = args.pop(alias)
                        if canonical not in args:
                            args[canonical] = val

            # Normalize int ref to str for chr_* tools
            if tool_name.startswith("chr_") and "ref" in args and isinstance(args["ref"], int):
                args["ref"] = str(args["ref"])

            context.message.arguments = args

        return await call_next(context)
