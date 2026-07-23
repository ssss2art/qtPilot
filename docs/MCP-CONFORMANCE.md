# MCP Protocol Conformance

This document states, explicitly, which revision of the **Model Context Protocol
(MCP)** the qtPilot MCP server targets and ships, so the moving spec doesn't
leave the project's conformance ambiguous.

> **TL;DR** — qtPilot's MCP server advertises MCP protocol revision
> **`2025-11-25`** and negotiates down to any client back to **`2024-11-05`**.
> The revision is provided by the official MCP Python SDK that FastMCP depends
> on; qtPilot does not implement the wire protocol itself.

## Two different "protocols" — don't conflate them

qtPilot has **two** independent protocols. This document is about the first.

| # | Protocol | Between | Transport | Versioning |
|---|----------|---------|-----------|------------|
| 1 | **MCP** (this doc) | AI client (e.g. Claude) ↔ `qtpilot` MCP server | stdio, JSON-RPC 2.0 | Dated spec revisions (`YYYY-MM-DD`), see below |
| 2 | **qtPilot probe protocol** | `qtpilot` MCP server ↔ injected C++ probe | WebSocket, JSON-RPC 2.0 | Integer, currently **`1`** — see [`qtPilot-specification.md`](../qtPilot-specification.md) |

> The `protocol_version` field returned by `qtpilot_status` / discovery refers
> to protocol **2** (the probe protocol), **not** the MCP spec revision.

## What qtPilot targets and ships

The server is built on **[FastMCP](https://github.com/jlowin/fastmcp)**, which
runs on the **[official MCP Python SDK](https://github.com/modelcontextprotocol/python-sdk)**
(`mcp`). qtPilot itself contains no MCP wire-protocol code — the supported
revisions come entirely from the resolved `mcp` SDK.

Declared dependency (`python/pyproject.toml`):

```
fastmcp>=2.9,<3      # -> pulls mcp>=1.24,<2
```

The MCP revision therefore tracks whatever `mcp` version resolves inside that
range. As verified on **2026-07-22** (`fastmcp 2.14.7`, `mcp 1.28.1`):

| Field (`mcp.shared.version`) | Value |
|---|---|
| `LATEST_PROTOCOL_VERSION` (advertised by the server) | **`2025-11-25`** |
| `SUPPORTED_PROTOCOL_VERSIONS` (negotiable) | `2024-11-05`, `2025-03-26`, `2025-06-18`, `2025-11-25` |
| `DEFAULT_NEGOTIATED_VERSION` (fallback if client sends none) | `2025-03-26` |

**Behavior:** on `initialize`, the server offers `2025-11-25`; if the client
requests an older supported revision, the SDK negotiates down to it. A client
older than `2024-11-05` is not supported.

Spec reference for the primary target: <https://modelcontextprotocol.io/specification/2025-11-25>

## Transport & capabilities

- **Transport:** **stdio** only. The MCP client launches `qtpilot serve` as a
  subprocess (see `.mcp.json`). qtPilot does not expose an HTTP/SSE endpoint
  (SSE is deprecated in the MCP spec).
- **Server capabilities:** **tools** only (the `qt_*` / `chr_*` / `cu_*` tool
  families). qtPilot does not implement MCP `resources` or `prompts`.

## Verifying the shipped revision

Because the revision is derived from the installed SDK, confirm it in your
environment:

```bash
python -c "import mcp.shared.version as v; \
  print('latest:', v.LATEST_PROTOCOL_VERSION); \
  print('supported:', v.SUPPORTED_PROTOCOL_VERSIONS)"
```

## Pinning to a single revision (optional)

The `fastmcp>=2.9,<3` range lets the MCP revision advance as the `mcp` SDK
updates. This is intentional (MCP is designed for latest-offered + negotiate-
down), but it means "the revision qtPilot ships" is a range, not a fixed point.

If a deployment must guarantee one exact revision, **pin the SDK** — e.g. add a
constraint such as `mcp==<version-that-tops-out-at-the-desired-revision>` (and
optionally a matching `fastmcp==` pin), then re-verify with the command above.
qtPilot's runtime does not otherwise depend on a specific revision.

---

*Verified against fastmcp 2.14.7 / mcp 1.28.1 on 2026-07-22. Re-run the
verification command after a dependency bump and update the table above.*
