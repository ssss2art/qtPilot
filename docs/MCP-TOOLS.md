# MCP Tooling

This page describes the MCP surface shipped by the current `main` branch. Tool
definitions are generated from the Python server, so use MCP discovery when you
need exact argument schemas rather than copying schemas from older design docs.

## Modes and Counts

Every mode includes 10 shared `qtpilot_*` tools for connection management,
mode switching, message logging, and event recording.

| Mode | Mode-specific tools | Shared tools | Total | Primary use |
|------|---------------------|--------------|-------|-------------|
| `native` | 27 `qt_*` | 10 | 37 | Qt object, property, method, signal, model, and UI access |
| `cu` | 13 `cu_*` | 10 | 23 | Screenshot and coordinate-based interaction |
| `chrome` | 8 `chr_*` | 10 | 18 | Accessibility-tree and element-reference interaction |
| `all` | 48 across all families | 10 | 58 | Exploration and mixed workflows |

The shared tools are:

- Session: `qtpilot_status`, `qtpilot_connect_probe`,
  `qtpilot_disconnect_probe`, `qtpilot_set_mode`
- Logging: `qtpilot_log_start`, `qtpilot_log_stop`, `qtpilot_log_status`
- Recording: `qtpilot_recording_start`, `qtpilot_recording_stop`,
  `qtpilot_recording_status`

The server also exposes one resource, `qtpilot://status`, and currently exposes
no MCP prompts. Calling `qtpilot_set_mode` changes the visible mode-specific
tools while retaining the shared tools.

## Inspect with mcp-explorer

The normal `qtpilot serve` command uses stdio because that is what MCP clients
launch. [`mcp-explorer`](https://github.com/simonw/mcp-explorer) accepts an HTTP
endpoint, so the repository includes a development helper that runs the same
server factory over streamable HTTP on localhost.

Install the project and explorer in a virtual environment, then start the
helper from the repository root:

```bash
python -m pip install -e ./python
python -m pip install mcp-explorer
python scripts/run-mcp-http.py --mode all --port 8000
```

In another terminal, inspect the current stateful server with the explorer's
legacy initialization handshake:

```bash
mcp-explorer info --legacy http://127.0.0.1:8000/mcp
mcp-explorer list --legacy http://127.0.0.1:8000/mcp
mcp-explorer list --legacy --json http://127.0.0.1:8000/mcp
mcp-explorer resources --legacy http://127.0.0.1:8000/mcp
mcp-explorer prompts --legacy http://127.0.0.1:8000/mcp
```

Use `inspect` for a single tool and `call` for a live request. For example:

```bash
mcp-explorer inspect --legacy http://127.0.0.1:8000/mcp qtpilot_status
mcp-explorer call --legacy http://127.0.0.1:8000/mcp qtpilot_status
```

The helper disables UDP discovery by default and binds only to `127.0.0.1`.
Pass `--ws-url` if you want it to connect to a running probe. It is a developer
inspection endpoint, not the recommended MCP client configuration.

## Checking a Changeset

For protocol or tooling changes, create a separate virtual environment for each
worktree, start each helper on a different port, and save `info --json`,
`list --json`, `resources --json`, and `prompts --json`. Compare tool names,
input schemas, negotiated protocol metadata, capabilities, resources, and
prompts. This keeps dependency versions isolated and makes unintended surface
changes visible before merge.
