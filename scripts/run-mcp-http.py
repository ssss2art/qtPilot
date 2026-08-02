#!/usr/bin/env python3
"""Expose qtPilot over localhost HTTP for MCP development tools."""

from __future__ import annotations

import argparse

from qtpilot.server import create_server


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run qtPilot over streamable HTTP for local inspection."
    )
    parser.add_argument(
        "--mode",
        choices=("native", "cu", "chrome", "all"),
        default="all",
        help="API mode to expose (default: all)",
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8000)
    parser.add_argument("--path", default="/mcp")
    parser.add_argument("--ws-url", help="Optional WebSocket URL of a running probe")
    parser.add_argument(
        "--enable-discovery",
        action="store_true",
        help="Enable UDP probe discovery (disabled by default)",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    server = create_server(
        mode=args.mode,
        ws_url=args.ws_url,
        discovery_enabled=args.enable_discovery,
    )
    server.run(
        transport="streamable-http",
        host=args.host,
        port=args.port,
        path=args.path,
        show_banner=False,
    )


if __name__ == "__main__":
    main()
