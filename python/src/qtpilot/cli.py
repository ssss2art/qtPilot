"""CLI entry point for the qtPilot MCP server."""

from __future__ import annotations

import argparse
import json
import logging
import os
import sys


def cmd_serve(args: argparse.Namespace) -> int:
    """Run the MCP server."""
    # Handle --demo flag
    if getattr(args, "demo", False):
        if args.target:
            print("Error: Cannot use --demo with --target", file=sys.stderr)
            return 1
        from qtpilot.download import get_testapp_path
        testapp = get_testapp_path()
        if testapp is None:
            print(
                "Error: Test app not found. Run: qtpilot download-tools --qt-version <version>",
                file=sys.stderr,
            )
            return 1
        args.target = str(testapp)
        # Use the self-contained launcher bundled inside testapp/
        from qtpilot.download import get_launcher_filename
        launcher = testapp.parent / get_launcher_filename()
        if launcher.exists() and not args.launcher_path:
            args.launcher_path = str(launcher)

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


def cmd_demo(args: argparse.Namespace) -> int:
    """One-command demo: download tools if needed, then launch."""
    from qtpilot.download import (
        ChecksumError,
        DownloadError,
        UnsupportedPlatformError,
        VersionNotFoundError,
        download_and_extract,
        get_testapp_path,
        latest_version,
    )

    output_dir = args.output
    qt_version = args.qt_version or latest_version()

    # Download if testapp not already present
    testapp = get_testapp_path(output_dir=output_dir)
    if testapp is None:
        print(f"Downloading qtPilot tools for Qt {qt_version}...")
        try:
            download_and_extract(
                qt_version=qt_version,
                output_dir=output_dir,
                verify=not args.no_verify,
                arch=args.arch,
            )
        except (VersionNotFoundError, UnsupportedPlatformError, ChecksumError, DownloadError) as e:
            print(f"Error: {e}", file=sys.stderr)
            return 1
        testapp = get_testapp_path(output_dir=output_dir)
        if testapp is None:
            print("Error: Test app not found after download.", file=sys.stderr)
            return 1
        print(f"Downloaded to: {testapp.parent}")

    # Build a serve-compatible namespace and delegate
    serve_args = argparse.Namespace(
        mode="native",
        ws_url=None,
        target=None,
        port=args.port,
        launcher_path=None,
        discovery_port=9221,
        no_discovery=False,
        qt_version=None,
        qt_dir=None,
        arch=args.arch,
        demo=True,
    )
    return cmd_serve(serve_args)


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
        from qtpilot.download import get_testapp_path
        testapp = get_testapp_path(output_dir=args.output)
        if testapp:
            print(f"Test app:           {testapp}")
            print(f"\nRun 'qtpilot demo' to try it out!")
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


# Exit codes, so a CI step can tell a behaviour change from a broken fixture.
REPLAY_EXIT_OK = 0
REPLAY_EXIT_DIVERGED = 1
REPLAY_EXIT_USAGE = 2
# A replay that could not complete: an action errored partway, so the run proves nothing either
# way. Kept distinct from DIVERGED because "the application changed" and "the replay fell over"
# call for different responses, and conflating them means CI cannot tell them apart.
REPLAY_EXIT_ABORTED = 3


def _print_report(result, as_json: bool) -> None:
    """Write a replay report to stdout, for a person or for a machine."""
    if as_json:
        print(json.dumps({
            "source": result.scenario.source,
            "passed": result.passed,
            "summary": result.summary(),
            "aborted_at": result.aborted_at,
            "abort_reason": result.abort_reason,
            "divergences": [
                {
                    "step": d.step,
                    "kind": d.kind,
                    "method": d.method,
                    "expected": d.expected,
                    "actual": d.actual,
                }
                for d in result.divergences
            ],
        }, indent=2))
        return

    print(result.summary())
    for divergence in result.divergences:
        print(f"  {divergence}")


def cmd_replay(args: argparse.Namespace) -> int:
    """Replay a recorded session against a running application.

    Quietens the transport loggers first. main() turns on DEBUG for everything, which is useful
    when serving but buries a replay report under a line per websocket frame -- and this command
    exists to be read by CI.

    Returns REPLAY_EXIT_USAGE for a fixture that cannot be replayed at all and
    REPLAY_EXIT_DIVERGED for one that ran and disagreed, so a CI step can tell a real behaviour
    change from a broken recording rather than seeing one red for both.
    """
    import asyncio

    for noisy in ("websockets", "websockets.client", "qtpilot.connection", "asyncio"):
        logging.getLogger(noisy).setLevel(logging.WARNING)

    from qtpilot.replay import load_scenario, load_watch_list, run_scenario

    try:
        scenario = load_scenario(args.path)
        watch = load_watch_list(args.watch) if args.watch else None
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return REPLAY_EXIT_USAGE

    if scenario.unsupported:
        listed = ", ".join(f"{n} x{c}" for n, c in sorted(scenario.unsupported.items()))
        print(
            f"warning: {scenario.source}: {sum(scenario.unsupported.values())} recorded call(s) "
            f"will not be re-driven ({listed}). Differences they would have caused are reported "
            "as application divergences.",
            file=sys.stderr,
        )

    if args.record and not args.watch:
        print(
            "error: --record without --watch would re-record exactly what the log already "
            "observes. Give it a watch list, or record a fresh session with qtpilot_log_start.",
            file=sys.stderr,
        )
        return REPLAY_EXIT_USAGE

    if args.record and not args.output:
        # --output used to default to the input path, so --record wrote its baseline over the
        # recording it had just read. Destructive-by-default is close to impossible to walk back
        # once scripts depend on it, so the destination is now explicit.
        print(
            "error: --record requires --output. Writing the baseline over the input log would "
            "destroy the recording it was produced from.",
            file=sys.stderr,
        )
        return REPLAY_EXIT_USAGE

    if not scenario.is_replayable:
        # Distinguish "the log has no wire traffic" from "the log is full of calls replay
        # cannot reproduce". Telling someone to re-record at level 2 when they already did --
        # because every call was a cu.* one -- sends them round the same loop again.
        if scenario.unsupported:
            listed = ", ".join(
                f"{name} x{count}" for name, count in sorted(scenario.unsupported.items())
            )
            print(
                f"error: {scenario.source}: nothing replayable -- every recorded call is one "
                f"replay cannot reproduce ({listed}).",
                file=sys.stderr,
            )
        else:
            print(
                f"error: {scenario.source}: nothing to replay -- no mutating calls found. "
                "Record at level 2 or above (qtpilot_log_start(level=2)).",
                file=sys.stderr,
            )
        return REPLAY_EXIT_USAGE

    if args.inspect:
        actions = [s.action.method for s in scenario.steps if s.action]
        observations = sum(len(s.observations) for s in scenario.steps)
        notifications = sum(len(s.notifications) for s in scenario.steps)
        if args.json:
            print(json.dumps({
                "source": scenario.source,
                "replayable": True,
                "steps": len(scenario.steps),
                "actions": actions,
                "observations": observations,
                "notifications": notifications,
            }, indent=2))
        else:
            print(f"{scenario.source}: {len(scenario.steps)} step(s), {observations} observation(s), {notifications} notification(s)")
            for action in actions:
                print(f"  drives {action}")
        return REPLAY_EXIT_OK

    from qtpilot.connection import ProbeConnection, ProbeError

    async def go() -> int:
        probe = ProbeConnection(args.ws_url)

        # Reaching the application is a precondition, not part of the comparison. An app that is
        # not running has not behaved differently, and reporting it as a divergence would send
        # someone hunting a regression that does not exist.
        try:
            await probe.connect()
        except (OSError, ProbeError) as exc:
            print(
                f"error: cannot connect to a probe at {args.ws_url}: {exc}\n"
                "Start the application with the qtPilot probe before replaying.",
                file=sys.stderr,
            )
            return REPLAY_EXIT_USAGE

        try:
            await probe.handshake()
            result = await run_scenario(
                scenario, probe, settle=args.settle, watch=watch, record=args.record
            )
        finally:
            await probe.disconnect()

        if args.record:
            # Never write a partial scenario. An aborted run stopped partway, so its steps are a
            # truncation of the recording rather than a baseline -- and since --output used to
            # default to the input path, writing here replaced the user's only copy of a 40-step
            # recording with the 6 steps that ran, printed a success line, and exited 0.
            if result.aborted_at is not None:
                print(
                    f"error: replay aborted at step {result.aborted_at} "
                    f"({result.abort_reason}); refusing to write a partial baseline.",
                    file=sys.stderr,
                )
                return REPLAY_EXIT_ABORTED
            result.write_log(args.output)
            observations = sum(len(step.observations) for step in result.steps)
            print(
                f"recorded {len(result.steps)} step(s), {observations} observation(s) "
                f"-> {args.output}"
            )
            return REPLAY_EXIT_OK

        _print_report(result, args.json)
        if result.aborted_at is not None:
            return REPLAY_EXIT_ABORTED
        return REPLAY_EXIT_OK if result.passed else REPLAY_EXIT_DIVERGED

    return asyncio.run(go())


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
    serve_parser.add_argument(
        "--demo",
        action="store_true",
        help="Launch the bundled test app (requires download-tools first)",
    )
    serve_parser.set_defaults(func=cmd_serve)

    # --- demo subcommand ---
    demo_parser = subparsers.add_parser(
        "demo",
        help="Download tools (if needed) and launch the bundled test app",
        description=(
            "One-command demo: downloads the qtPilot probe, launcher, and test app\n"
            "if not already present, then starts the MCP server with the test app.\n\n"
            "Example:\n"
            "  qtpilot demo\n"
            "  qtpilot demo --qt-version 6.5\n"
            "  qtpilot demo --port 9333"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    demo_parser.add_argument(
        "--qt-version",
        default=None,
        metavar="VERSION",
        help="Qt version to download (default: latest available)",
    )
    demo_parser.add_argument(
        "--port",
        type=int,
        default=int(os.environ.get("QTPILOT_PORT", "9222")),
        help="Port for the probe WebSocket (default: 9222)",
    )
    demo_parser.add_argument(
        "--output",
        "-o",
        default=None,
        metavar="DIR",
        help="Directory for downloaded tools (default: current directory)",
    )
    demo_parser.add_argument(
        "--no-verify",
        action="store_true",
        help="Skip SHA256 checksum verification",
    )
    demo_parser.add_argument(
        "--arch",
        default=None,
        choices=["x64", "x86"],
        help="Target architecture (default: x64)",
    )
    demo_parser.set_defaults(func=cmd_demo)

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

    # --- replay subcommand ---
    replay_parser = subparsers.add_parser(
        "replay",
        help="Re-drive a recorded session and report what changed",
        description=(
            "Replay a .jsonl message log against a running application.\n\n"
            "Re-issues the recorded actions in order, re-issues the recorded observations after\n"
            "each one, and compares. Timings, request ids and generated object handles are\n"
            "ignored, so a difference means the application behaved differently.\n\n"
            "The application must already be in the state the recording started from -- replay\n"
            "drives input, it does not reset anything.\n\n"
            "Exit codes: 0 no divergence, 1 diverged or aborted, 2 the log cannot be replayed.\n\n"
            "Example:\n"
            "  qtpilot replay scenarios/submit-form.jsonl\n"
            "  qtpilot replay scenarios/submit-form.jsonl --inspect\n"
            "  qtpilot replay scenarios/submit-form.jsonl --settle 0.25 --json"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    replay_parser.add_argument(
        "path",
        metavar="LOG",
        help="Path to a .jsonl message log recorded at level 2 or above",
    )
    replay_parser.add_argument(
        "--ws-url",
        default=os.environ.get("QTPILOT_WS_URL", "ws://localhost:9222"),
        help="WebSocket URL of the qtPilot probe (default: ws://localhost:9222)",
    )
    replay_parser.add_argument(
        "--settle",
        type=float,
        default=0.1,
        help=(
            "Seconds to wait after each action for signals to arrive (default: 0.1). "
            "Raise it for an application that updates asynchronously; too short reports a race "
            "as a divergence."
        ),
    )
    replay_parser.add_argument(
        "--inspect",
        action="store_true",
        help="Summarise the log without connecting to anything. Safe against a live application.",
    )
    replay_parser.add_argument(
        "--watch",
        metavar="FILE",
        default=None,
        help=(
            "JSON watch list queried after every action. Use with --record to give a scenario "
            "assertions it did not happen to record: a session of nothing but clicks otherwise "
            "replays as a sequence of clicks that cannot fail."
        ),
    )
    replay_parser.add_argument(
        "--record",
        action="store_true",
        help=(
            "Capture a new baseline instead of comparing against one. Requires --watch "
            "and --output."
        ),
    )
    replay_parser.add_argument(
        "--output",
        "-o",
        metavar="FILE",
        default=None,
        help=(
            "Where --record writes the baseline. Required with --record: it used to default "
            "to the input log, so a --record run replaced the recording it was reading."
        ),
    )
    replay_parser.add_argument(
        "--json",
        action="store_true",
        help="Emit the report as JSON",
    )
    replay_parser.set_defaults(func=cmd_replay)

    return parser


def main() -> None:
    """Parse arguments and run the appropriate command."""
    logging.basicConfig(level=logging.DEBUG, stream=sys.stderr)

    parser = create_parser()
    args = parser.parse_args()

    # Run the command and exit with its return code
    sys.exit(args.func(args))
