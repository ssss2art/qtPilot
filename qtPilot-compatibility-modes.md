# qtPilot API Modes

qtPilot exposes three focused MCP tool families and an `all` mode. The
computer-use and Chrome-style families borrow familiar interaction patterns,
but their names and schemas are qtPilot APIs; they are not drop-in copies of a
vendor-specific tool version.

## Mode Summary

| Mode | Prefix | Mode tools | Total with shared tools | Best for |
|------|--------|------------|-------------------------|----------|
| `native` | `qt_*` | 27 | 37 | Qt introspection, models, signals, and deterministic automation |
| `cu` | `cu_*` | 13 | 23 | Screenshots and coordinate-based input |
| `chrome` | `chr_*` | 8 | 18 | Accessibility trees and semantic element references |
| `all` | all prefixes | 48 | 58 | Exploration and workflows that combine APIs |

Start in the narrowest mode that suits the task. This gives an MCP client fewer
tools to choose from. Use `all` when investigating the surface or when a task
genuinely needs more than one interaction style.

Ten `qtpilot_*` tools are always visible for probe connections, runtime mode
selection, message logging, and event recording. See [MCP Tooling](docs/MCP-TOOLS.md)
for those tools and a schema-inspection workflow.

## Native Mode

Native mode works with Qt objects directly. Object IDs remain meaningful across
calls and allow a client to inspect or manipulate state without inferring it
from pixels.

| Area | Tools |
|------|-------|
| Connection | `qt_ping`, `qt_version` |
| Objects | `qt_objects_tree`, `qt_objects_inspect`, `qt_objects_search` |
| Properties and methods | `qt_properties_get`, `qt_properties_set`, `qt_methods_invoke` |
| Signals and events | `qt_signals_subscribe`, `qt_signals_unsubscribe`, `qt_signals_setLifecycle`, `qt_events_start`, `qt_events_stop` |
| UI interaction | `qt_ui_click`, `qt_ui_sendKeys`, `qt_ui_screenshot`, `qt_ui_geometry`, `qt_ui_hitTest`, `qt_ui_clickItem` |
| Stable names | `qt_names_register`, `qt_names_unregister`, `qt_names_list`, `qt_names_validate`, `qt_names_load` |
| Models | `qt_models_list`, `qt_models_data`, `qt_models_search` |

Use native mode for test automation, debugging, property inspection, method
invocation, signal observation, and structured model access.

## Computer-Use Mode

Computer-use mode operates visually using screenshots and screen coordinates.
It is useful for custom-painted controls, canvas-like content, and visual
verification where semantic Qt metadata is incomplete.

The 13 tools are:

- Capture and position: `cu_screenshot`, `cu_cursorPosition`
- Clicks: `cu_leftClick`, `cu_rightClick`, `cu_middleClick`, `cu_doubleClick`
- Pointer movement: `cu_mouseMove`, `cu_mouseDrag`, `cu_mouseDown`, `cu_mouseUp`
- Keyboard and scrolling: `cu_type`, `cu_key`, `cu_scroll`

Coordinates are less stable than object IDs or accessibility references. Prefer
native or Chrome mode when the UI exposes enough structure for the task.

## Chrome Mode

Chrome mode presents Qt accessibility information as a semantic page-like tree.
`chr_readPage` assigns references that the other tools can use without relying
on exact screen coordinates.

The 8 tools are:

- Read and find: `chr_readPage`, `chr_getPageText`, `chr_find`
- Interact: `chr_click`, `chr_formInput`
- Context: `chr_navigate`, `chr_tabsContext`, `chr_readConsoleMessages`

Use this mode for forms, standard controls, text extraction, and other workflows
where Qt accessibility data accurately describes the UI.

## Shared Runtime Tools

The active mode can be selected at launch:

```bash
qtpilot serve --mode native --target /path/to/app
qtpilot serve --mode cu --target /path/to/app
qtpilot serve --mode chrome --target /path/to/app
qtpilot serve --mode all --target /path/to/app
```

It can also change during a session. Call `qtpilot_status` to read the active
mode and connection state, then call `qtpilot_set_mode` with `native`, `cu`,
`chrome`, or `all`. MCP clients that support tool-list change notifications can
refresh the visible tools after a switch.

Probe discovery and connection are independent of mode. Use
`qtpilot_connect_probe` and `qtpilot_disconnect_probe` when the server was
started without `--target` or `--ws-url`.

## Choosing a Mode

Choose `native` when you can identify Qt objects and want the richest,
least-ambiguous API. Choose `chrome` when accessibility semantics are more
useful than implementation details. Choose `cu` when visual coordinates are the
only reliable representation. Switch modes or use `all` when a workflow needs
both semantic interaction and screenshot verification.

For exact parameters and return schemas, inspect the running server rather than
using a static copy. See [MCP Tooling](docs/MCP-TOOLS.md).
