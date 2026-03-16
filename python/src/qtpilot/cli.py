"""CLI entry point for the qtPilot MCP server."""

from __future__ import annotations

import argparse
import logging
import os
import sys


def cmd_serve(args: argparse.Namespace) -> int:
    """Run the MCP server."""
    # ws_url is None unless explicitly provided or a target is specified
    ws_url = None
    if args.target:
        ws_url = f"ws://localhost:{args.port}"
    elif args.ws_url:
        ws_url = args.ws_url

    from qtpilot.server import create_server

    server = create_server(
        mode=args.mode,
        ws_url=ws_url,
        target=args.target,
        port=args.port,
        launcher_path=args.launcher_path,
        discovery_port=args.discovery_port,
        discovery_enabled=not args.no_discovery,
        qt_version=args.qt_version,
        qt_dir=args.qt_dir,
    )
    server.run()
    return 0


def cmd_download_tools(args: argparse.Namespace) -> int:
    """Download probe + launcher archive from GitHub Releases."""
    from qtpilot.download import (
        AVAILABLE_VERSIONS,
        ChecksumError,
        DownloadError,
        UnsupportedPlatformError,
        VersionNotFoundError,
        download_and_extract,
    )

    try:
        probe_path, launcher_path = download_and_extract(
            qt_version=args.qt_version,
            output_dir=args.output,
            verify=not args.no_verify,
            release_tag=args.release,
            arch=args.arch,
        )
        print(f"Extracted probe:    {probe_path}")
        print(f"Extracted launcher: {launcher_path}")
    except VersionNotFoundError as e:
        print(f"Error: {e}", file=sys.stderr)
        print(f"Available versions: {', '.join(sorted(AVAILABLE_VERSIONS))}", file=sys.stderr)
        return 1
    except UnsupportedPlatformError as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1
    except ChecksumError as e:
        print(f"Error: {e}", file=sys.stderr)
        print("Try --no-verify to skip checksum verification (not recommended).", file=sys.stderr)
        return 1
    except DownloadError as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1

    return 0


def create_parser() -> argparse.ArgumentParser:
    """Create the argument parser with subcommands."""
    parser = argparse.ArgumentParser(
        prog="qtpilot",
        description="qtPilot - MCP server for controlling Qt applications",
    )

    subparsers = parser.add_subparsers(
        title="commands",
        dest="command",
        required=True,
        metavar="COMMAND",
    )

    # --- serve subcommand ---
    serve_parser = subparsers.add_parser(
        "serve",
        help="Run the MCP server",
        description="Start the MCP server to control Qt applications via the qtPilot probe.",
    )
    serve_parser.add_argument(
        "--mode",
        default="native",
        choices=["native", "cu", "chrome", "all"],
        help="API mode to expose (default: native). Use 'all' for every tool set.",
    )
    serve_parser.add_argument(
        "--ws-url",
        default=os.environ.get("QTPILOT_WS_URL"),
        help="WebSocket URL of the qtPilot probe (auto-connect on startup)",
    )
    serve_parser.add_argument(
        "--target",
        default=None,
        help="Path to Qt application exe to auto-launch",
    )
    serve_parser.add_argument(
        "--port",
        type=int,
        default=int(os.environ.get("QTPILOT_PORT", "9222")),
        help="Port for auto-launched probe (default: 9222)",
    )
    serve_parser.add_argument(
        "--launcher-path",
        default=os.environ.get("QTPILOT_LAUNCHER"),
        help="Path to qtpilot-launch executable",
    )
    serve_parser.add_argument(
        "--discovery-port",
        type=int,
        default=int(os.environ.get("QTPILOT_DISCOVERY_PORT", "9221")),
        help="UDP port for probe discovery (default: 9221)",
    )
    serve_parser.add_argument(
        "--qt-version",
        default=None,
        metavar="VERSION",
        help="Qt version for probe auto-detection (e.g., 5.15, 6.8)",
    )
    serve_parser.add_argument(
        "--qt-dir",
        default=os.environ.get("QTPILOT_QT_DIR"),
        metavar="PATH",
        help="Path to Qt installation prefix (e.g., C:/Qt/6.8.0/msvc2022_64). "
             "Auto-sets QT_PLUGIN_PATH and PATH for the target application.",
    )
    serve_parser.add_argument(
        "--no-discovery",
        action="store_true",
        help="Disable UDP probe discovery",
    )
    serve_parser.add_argument(
        "--arch",
        default=None,
        choices=["x64", "x86"],
        help="Target architecture (default: x64). Must match target app bitness.",
    )
    serve_parser.set_defaults(func=cmd_serve)

    # --- download-tools subcommand ---
    download_parser = subparsers.add_parser(
        "download-tools",
        help="Download probe + launcher from GitHub Releases",
        description=(
            "Download the qtPilot probe and launcher for your Qt version from GitHub Releases.\n\n"
            "Available Qt versions: 5.15, 5.15-patched, 6.5, 6.8, 6.9\n\n"
            "Downloads a platform-specific archive (zip on Windows, tar.gz on Linux)\n"
            "containing qtPilot-probe and qtPilot-launcher, then extracts them.\n\n"
            "Example:\n"
            "  qtpilot download-tools --qt-version 6.8\n"
            "  qtpilot download-tools --qt-version 5.15 --output ./tools\n"
            "  qtpilot download-tools --qt-version 5.15-patched --release v0.3.0"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    download_parser.add_argument(
        "--qt-version",
        required=True,
        metavar="VERSION",
        help="Qt version to download tools for (e.g., 6.8, 5.15, 5.15-patched)",
    )
    download_parser.add_argument(
        "--output",
        "-o",
        default=None,
        metavar="DIR",
        help="Directory to extract tools into (default: current directory)",
    )
    download_parser.add_argument(
        "--no-verify",
        action="store_true",
        help="Skip SHA256 checksum verification (not recommended)",
    )
    download_parser.add_argument(
        "--release",
        default="latest",
        metavar="TAG",
        help="Release tag to download from (default: latest)",
    )
    download_parser.add_argument(
        "--arch",
        default=None,
        choices=["x64", "x86"],
        help="Target architecture (default: x64). Must match target app bitness.",
    )
    download_parser.set_defaults(func=cmd_download_tools)

    return parser


def main() -> None:
    """Parse arguments and run the appropriate command."""
    logging.basicConfig(level=logging.DEBUG, stream=sys.stderr)

    parser = create_parser()
    args = parser.parse_args()

    # Run the command and exit with its return code
    sys.exit(args.func(args))
