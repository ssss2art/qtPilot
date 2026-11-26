# Screenshots Always Go to a File

## Intent

`qt_ui_screenshot` and `cu_screenshot` must hand the caller a usable image of any size, from a local or a remote probe. Today they return nothing: both probe methods reply with the envelope `{meta, result: {image, width, height}}`, and the server's `process_screenshot_response` reads `image` from the top level, so the inline image is empty and `save_to` writes 0 bytes. Separately, large images fail twice over: inlined image data exceeds what an MCP client accepts, and anything over 1 MiB never reaches the server, because the probe connection uses the `websockets` default `max_size` and closes on a bigger message.

## Goals

- Both tools always write the PNG to a file on the MCP server's machine and return its path, never image data.
- Images from a remote probe (LAN, Android, iOS) work the same as from a local one: the image crosses the probe connection and the server saves it.
- The probe connection accepts screenshot-sized messages.

## Non-goals

- Chunked image transfer (a probe/server protocol change; only matters above the new cap).
- MCP resource links for the image.
- The WebAssembly page transport (`page_connection.py`), which does not go through `websockets`.
- Cleaning up old temp files (left to the OS temp cleanup).

## Anti-goals

- Reporting success with an empty or 0-byte image.
- Any base64 image data in a tool result.

## Approaches

- **A. Return a file path (chosen).** Unwrap the envelope, always save, return `{saved_to, width, height, bytes}`; raise the connection cap. Chosen: the MCP server and the AI client share a machine (stdio), so a path is enough, and raising the cap covers remote probes.
- **B. A plus an MCP resource link.** Would let clients without file access fetch the image, but client support varies and it adds a resource endpoint to build and test.
- **C. A plus chunked transfer.** Removes any size cap, at the cost of a protocol change on both ends; no realistic screenshot needs it.

## Design (decided)

- **Helper** (`python/src/qtpilot/tools/screenshot_helper.py`): accept the probe envelope (`result.image`), and still the flat shape. Decode, take width/height from the reply or the PNG header, and write the PNG. No image data in the return value.
- **Where it goes:** `save_to` when given; otherwise a per-run folder under the system temp directory.
- **Return value:** `{saved_to, width, height, bytes}` for both tools. `saved_to` keeps the key the existing `save_to` path already returns.
- **`as_image`** is removed from both tools rather than accepted and ignored.
- **No image in the reply** (missing or empty `image`): raise an error; never write a file.
- **Probe connection** (`python/src/qtpilot/connection.py`): pass `max_size` to `websockets` `connect`, set to 256 MiB as a named module constant.
- **Callers and docs:** update the `qt_ui_screenshot` / `cu_screenshot` docstrings, the `prompts.py` guidance (tell the model to open the returned file to see the image), every doc that shows inline screenshot output, and the `test-mcp-modes` skill.
- **Tests:** replace the flat-reply mocks in `test_screenshot_efficiency.py` with the real envelope shape; each test seen failing first. Add a connection test proving a message over 1 MiB from a probe arrives.

## Left to the builder (my calls, open to change)

- Temp folder: `<system temp>/qtpilot/screenshots/<server start time>-<pid>/`, created on first use.
- File name: `<sequence>-<tool name>.png` (e.g. `0003-qt_ui_screenshot.png`), so files sort in capture order and never collide within a run.
- The 256 MiB cap is a constant, not a CLI option.
