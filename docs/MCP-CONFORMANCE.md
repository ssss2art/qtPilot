# MCP Protocol Conformance

This document states, explicitly, which revision of the **Model Context Protocol
(MCP)** the qtPilot MCP server targets and ships, so the moving spec doesn't
leave the project's conformance ambiguous.

> **TL;DR** — qtPilot supports **two** MCP revisions from one codebase.
> A default install advertises **`2025-11-25`**. Installing the `mcp-next`
> extra advertises **`2026-07-28`** (the stateless revision). Both negotiate
> down to clients as old as **`2024-11-05`**. The revision is provided by the
> official MCP Python SDK that FastMCP depends on; qtPilot does not implement
> the wire protocol itself.

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
fastmcp>=2.9,<5              # spans both revisions
qtpilot[mcp-stable]          # -> fastmcp<4       -> MCP 2025-11-25
qtpilot[mcp-next]            # -> fastmcp>=4.0.1  -> MCP 2026-07-28
```

**A plain `pip install qtpilot` now resolves FastMCP 4 and therefore
`2026-07-28`.** That changed when FastMCP 4 left pre-release: the base range
spans both majors, so the newest satisfying release wins. Pin `mcp-stable` to
stay on `2025-11-25`.

The base range cannot simply be capped at `<4` to keep the old default —
extras are *additive*, so `fastmcp<4` from the base plus `fastmcp>=4.0.1` from
`mcp-next` is unsatisfiable and the extra would stop resolving at all.

Verified empirically on **2026-08-01** by installing each set into a clean venv
and reading `mcp.types.LATEST_PROTOCOL_VERSION`:

| Package set | `LATEST_PROTOCOL_VERSION` | Extra |
|---|---|---|
| `fastmcp` 2.14.7 / `mcp` 1.28.1 | `2025-11-25` | `mcp-stable` |
| `fastmcp` 3.4.7 / `mcp` 1.29.1 | `2025-11-25` | `mcp-stable` |
| `fastmcp` 4.0.3 / `mcp` 2.1.1 | **`2026-07-28`** | `mcp-next` (default resolve) |

Note that **FastMCP 3.x does not move the protocol** — it is an architectural
release (providers/transforms). Only FastMCP 4 reaches `2026-07-28`.

`DEFAULT_NEGOTIATED_VERSION` (the fallback when a client sends no version) is
`2025-03-26` on every set above.

Spec references:
[`2025-11-25`](https://modelcontextprotocol.io/specification/2025-11-25) ·
[`2026-07-28`](https://modelcontextprotocol.io/specification/2026-07-28)

## Status of `2026-07-28` support

**FastMCP 4 shipped stable (4.0.1, 2026-09).** No pre-release flags are needed,
and the `mcp-next` extra floors at `4.0.1` rather than a beta:

```bash
pip install 'qtpilot[mcp-next]'          # explicit 2026-07-28
uvx --from 'qtpilot[mcp-next]' qtpilot serve

pip install 'qtpilot[mcp-stable]'        # stay on 2025-11-25
```

Both revisions are exercised by blocking CI legs (`mcp-stable`, `mcp-next`,
`mcp-next-py3.14`). They were `continue-on-error` while FastMCP 4 was a beta;
that allowance is gone.

qtPilot's own test suite passes against all three package sets, with **no
behavioural differences** in the tool surface. `qtpilot_set_mode` narrows
`tools/list` on every generation — see below for how, since the mechanism
differs.

Design notes and the migration record are in
[`docs/plans/2026-08-01-mcp-2026-07-28-migration.md`](plans/2026-08-01-mcp-2026-07-28-migration.md).

### How mode switching narrows the tool surface

FastMCP 4 removed `remove_tool`, because a tool set that mutates per-connection
conflicts with the stateless protocol's cacheable, deterministically-ordered
`tools/list`. qtPilot therefore uses two mechanisms:

| SDK | Mechanism | Registration |
|---|---|---|
| FastMCP 2.x | register/unregister tools on switch | only the active mode |
| FastMCP 3.x, 4.x | **visibility transform** filters `tools/list` per request | all modes, once at startup |

On the transform path, `qtpilot_set_mode` changes a single field — no
registration is touched. Tools hidden by the active mode are also unresolvable,
so a client holding a stale tool list gets an error rather than reaching a tool
that is no longer exposed. Session tools (`qtpilot_*`) and recording tools
belong to no mode and stay visible in all of them.

This is the spec-aligned shape: registration is static and deterministically
ordered, and the exposed surface is a pure function of the active mode.

## Transport & capabilities

- **Transport:** **stdio** only. The MCP client launches `qtpilot serve` as a
  subprocess (see `.mcp.json`). qtPilot does not expose an HTTP/SSE endpoint
  (HTTP+SSE is deprecated as of `2026-07-28`).
- **Server capabilities:** **tools** and one **resource**. The tool families are
  `qt_*` / `chr_*` / `cu_*` plus the `qtpilot_*` session tools; the resource is
  `qtpilot://status` (live probe connection state, registered by
  `qtpilot/status.py`). qtPilot does not implement MCP `prompts`.
- **Unused MCP features:** Roots, Sampling, Logging, and MCP `ping` are not
  implemented. All four are deprecated or removed as of `2026-07-28`, so this
  costs qtPilot nothing. In particular, qtPilot's `qtpilot_log_*` tools and
  `qt_ping` are qtPilot's own — unrelated to MCP Logging and MCP `ping`.

### Cache policy (`2026-07-28` only)

`2026-07-28` made list and read results cacheable via `ttlMs` / `cacheScope`.
qtPilot serves everything as **non-cacheable and private**, verified against
fastmcp 4.0.3:

| Result | `ttlMs` | `cacheScope` |
|---|---|---|
| `resources/read` (`qtpilot://status`) | `0` | `private` |
| `resources/list` | `0` | `private` |
| `tools/list` | `0` | `private` |

This matters most for `qtpilot://status`, which reports live probe connection
state — caching it would let a client serve a connection status that is no
longer true. `ttlMs: 0` means "do not cache"; `cacheScope: "private"` keeps
shared intermediaries out. These are the MCP SDK's own defaults rather than
qtPilot overrides (`FastMCP.resource()` exposes no ttl parameter), so
`python/tests/test_status_resource.py` asserts them directly — a future SDK bump
that introduces a non-zero default TTL fails the suite instead of silently
going stale.

qtPilot's `tools/list` order is deterministic, as `2026-07-28` asks: tools are
registered in a fixed sequence, so two servers launched in the same mode list
them identically.

## Verifying the shipped revision

Because the revision is derived from the installed SDK, confirm it in your
environment:

```bash
python -c "import qtpilot._mcp_compat as c; print(c.describe())"
```

```
{'fastmcp_version': '4.0.3', 'fastmcp_major': 4,
 'mcp_protocol_revision': '2026-07-28', 'stateless_protocol': True}
```

The underlying SDK constants can also be read directly:

```bash
python -c "import mcp.types as t; print(t.LATEST_PROTOCOL_VERSION)"
```

## Pinning to a single revision

Use the extras — `qtpilot[mcp-stable]` or `qtpilot[mcp-next]` — rather than
hand-pinning `fastmcp`. They are declared mutually exclusive in
`[tool.uv].conflicts`, and `python/uv.lock` locks a resolution for each.

If a deployment must guarantee one exact revision beyond the extra's range,
pin the SDK directly (e.g. `mcp==<version>`) and re-verify with the command
above. qtPilot's runtime does not otherwise depend on a specific revision.

---

*Verified against fastmcp 3.4.7 / 4.0.3 on 2026-09-06 (and 2.14.7 / 3.4.5 / 4.0.0b1 on 2026-08-01). Re-run the
verification command after a dependency bump and update the tables above.*
