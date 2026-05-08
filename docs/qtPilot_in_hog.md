# qtPilot in Hog — How-To Guide

Automate and introspect the Hog desk UI using [qtPilot](https://github.com/ssss2art/qtPilot) MCP tools.

## Prerequisites

### 1. Install qtPilot

```bash
pip install qtpilot
```

### 2. Download the probe (one-time)

The golden Hog build is **Win32 (x86)**. Download the matching probe:

```bash
qtpilot download-tools --qt-version 5.15 --arch x86 --release v0.1.4 --output "%USERPROFILE%/.qtpilot-x86"
```

> The CLI crashes after extraction with a harmless `TypeError`. The probe and launcher extract successfully — ignore the traceback.

### 3. Launch Hog with the probe

```bash
cmd /c "set PATH=E:\git\_hog4\gut.Win32.golden.exports\bin;%PATH% && C:\Users\stjohnson\.qtpilot-x86\qtPilot-launcher.exe E:\git\_hog4\gut.Win32.golden.exports\bin\launcher-Win32-golden.exe --probe C:\Users\stjohnson\.qtpilot-x86\qtPilot-probe.dll --port 9222 --qt-version 5.15 --inject-children --run-as-admin --detach"
```

Key flags:
- `--run-as-admin` — Hog's launcher requires elevation (UAC prompt)
- `--inject-children` — propagates probe into child processes (desktop, server, etc.)
- `--detach` — launcher exits after injection; Hog keeps running

### 4. Verify

```bash
# Check port is listening
netstat -an | grep :9222

# Ping the probe
python -c "import asyncio,json,websockets; asyncio.run((lambda: websockets.connect('ws://localhost:9222').__aenter__())())"
```

### 5. Register MCP server in Claude Code

Add `.mcp.json` to the project root:

```json
{
  "mcpServers": {
    "qtpilot": {
      "command": "qtpilot",
      "args": ["serve", "--mode", "native", "--ws-url", "ws://localhost:9222"]
    }
  }
}
```

## Starting the Application

When the probe-injected Hog launcher starts, it opens the **ShowConnectWindow** — the show selection / startup screen. The initial MCP connection is to the launcher probe on port 9222.

### Launcher buttons

These are standard `QPushButton` widgets, so both `qt_ui_click` and `qt_methods_invoke` work.

| Button | objectId | Description |
|--------|----------|-------------|
| Launch Show | `ShowConnectWindow/text_Launch__Show` | Start a new show (spawns desk + server) |
| Connect to Show | `ShowConnectWindow/text_Connect__to_Show` | Join an existing show on the network |
| New Show | `ShowConnectWindow/text_New__Show` | Create a new show file |
| Browse | `ShowConnectWindow/text_Browse` | Browse for a show file |
| File Browser | `ShowConnectWindow/text_File__Browser` | Open the file browser |
| Start Processor | `ShowConnectWindow/text_Start__Processor` | Launch in processor-only mode |
| Control Panel | `ShowConnectWindow/text_Control__Panel` | Open the system control panel |
| Help | `ShowConnectWindow/text_Help` | Open help |
| Quit | `ShowConnectWindow/text_Quit` | Exit the launcher |

> **Note:** Double underscores in objectIds represent spaces in the button text (e.g., `text_Launch__Show` = "Launch Show").

### Launch a show

**Step 1** — Click "Launch Show" on the launcher:

```
qt_ui_click(objectId="ShowConnectWindow/text_Launch__Show")
```

Or equivalently:

```
qt_methods_invoke(objectId="ShowConnectWindow/text_Launch__Show", method="click")
```

**Step 2** — Wait a few seconds for the child processes to start, then discover them:

```
qtpilot_list_probes()
```

You should see 4-6 probes: the launcher plus child processes (desktop, server, critical, dp8k).

**Step 3** — Connect to the desktop probe to interact with the desk UI:

```
qtpilot_connect_probe(ws_url="ws://<ip>:<desktop-port>")
```

Use the `ws_url` from `qtpilot_list_probes()` for the `desktop-Win32-golden` entry. The port is auto-assigned (not 9222 — that's the launcher).

### Launcher log window

The launcher also has a **LogWindow** with these buttons:

| Button | objectId |
|--------|----------|
| Pause | `LogWindow/QWidget/text_Pause` |
| Clear | `LogWindow/QWidget/text_Clear` |
| Apply | `LogWindow/QWidget/text_Apply` |

## Connecting to the Desktop

After launching a show, the desk spawns multiple processes. Use `qtpilot_list_probes()` to discover them:

| Process | Description |
|---------|-------------|
| `launcher-Win32-golden` | Launcher window (port 9222) |
| `desktop-Win32-golden` | Main desk UI (auto-assigned port) |
| `server-Win32-golden` | Show server(s) |
| `critical-Win32-golden` | Critical process |
| `dp8k-Win32-golden` | DP8000 service |

Switch to the desktop probe:

```
qtpilot_connect_probe(ws_url="ws://<ip>:<port>")
```

Use the `ws_url` from `qtpilot_list_probes()` for the desktop entry.

## UI Architecture

Hog's desk is a hybrid:
- **Toolbar/menu system** — QWidget-based (`FIDToolButton`, `ToolButton`, `HogButtonsPopup`)
- **Main content areas** — QML/Qt Quick (rendered off-screen via `QQuickOffScreenWindow`)

### Clicking QWidget buttons

Use `qt_methods_invoke` with the `click` method:

```
qt_methods_invoke(objectId="...", method="click")
```

> `qt_ui_click` only works on QWidget objects. It fails on QML items with "Object is not a widget". For QML, use coordinate-based `qt_ui_click` on a parent QWidget, or invoke methods directly.

### Key widget paths

| Widget | objectId | Description |
|--------|----------|-------------|
| Main screen | `ScreenWindow` | Top-level desk window |
| Directory bar | `ScreenWindow/DirectoryBar` | Toolbar at top of screen |
| Hog menu button | `ScreenWindow/DirectoryBar/FIDToolButton#1` | Hog icon (leftmost button) |
| Views button | `ScreenWindow/DirectoryBar/Views` | View selection |
| Workspace | `ScreenWindow/WorkspaceArea` | Content area below toolbar |

## Opening the Hog Menu (Console Popup)

The hog icon button at the left of the DirectoryBar opens a popup menu (`HogButtonsPopup`). The popup is **created fresh on each click** and auto-closes.

### Two-step sequence

**Step 1** — Click the hog icon to create the popup:

```
qt_methods_invoke(objectId="ScreenWindow/DirectoryBar/FIDToolButton#1", method="click")
```

**Step 2** — Click the desired menu item inside the popup:

```
qt_methods_invoke(objectId="ScreenWindow/DirectoryBar/FIDToolButton#1/HogButtonsPopup/text_<ButtonId>", method="click")
```

## Hog Menu Items Reference

All menu items live under `ScreenWindow/DirectoryBar/FIDToolButton#1/HogButtonsPopup/`.

| Menu Item | Button objectId suffix | What it opens | Internal action |
|-----------|----------------------|---------------|-----------------|
| Control Panel | `text_Control_Panel` | System control panel | `Gui::CreateControlPanel()` |
| Preferences | `text_Preferences` | User preferences editor | `Gui::CreatePreferencesWindow()` |
| Network | `text_Network` | Network configuration | `Gui::CreateNetworkWindow()` |
| Shows | `text_Shows` | Show session manager | `FID_SHELL_SESSION_MANAGER_WINDOW` |
| Fixtures | `text_Fixtures` | Fixture schedule window | `FID_OPEN_FIXTURE` |
| DMX Output | `text_DMX_Output` | DMX output monitor | `FID_OPEN_DMXMONITOR` |
| Virtual Panels | `text_Virtual_Panels` | Screen layout popup (secondary menu) | `ScreenLayoutPopup` |
| Patterns | `text_Patterns` | Pattern editor window | `Gui::CreatePatternWindow()` |
| Lock | `text_Lock` | Lock the console | `FID_SHELL_LOCK` |
| Log Off | `text_Log_Off` | Log off current user | `LOGOFF_REQUEST` |
| Log Off & Clean Up | `text_Log_Off____Clean_Up` | Log off and clean show data | `LOGOFF_AND_CLEAN_REQUEST` |

### Examples

**Open the Fixture window:**

```
qt_methods_invoke(objectId="ScreenWindow/DirectoryBar/FIDToolButton#1", method="click")
qt_methods_invoke(objectId="ScreenWindow/DirectoryBar/FIDToolButton#1/HogButtonsPopup/text_Fixtures", method="click")
```

After opening, the fixture window appears at:
```
ScreenWindow/WorkspaceArea/FixtureWin   (class: FixtureWindow)
```

**Open the Control Panel:**

```
qt_methods_invoke(objectId="ScreenWindow/DirectoryBar/FIDToolButton#1", method="click")
qt_methods_invoke(objectId="ScreenWindow/DirectoryBar/FIDToolButton#1/HogButtonsPopup/text_Control_Panel", method="click")
```

**Open DMX Output:**

```
qt_methods_invoke(objectId="ScreenWindow/DirectoryBar/FIDToolButton#1", method="click")
qt_methods_invoke(objectId="ScreenWindow/DirectoryBar/FIDToolButton#1/HogButtonsPopup/text_DMX_Output", method="click")
```

**Open Preferences:**

```
qt_methods_invoke(objectId="ScreenWindow/DirectoryBar/FIDToolButton#1", method="click")
qt_methods_invoke(objectId="ScreenWindow/DirectoryBar/FIDToolButton#1/HogButtonsPopup/text_Preferences", method="click")
```

## DirectoryBar Toolbar Buttons

Beyond the hog menu, the DirectoryBar contains view and window management buttons:

### View buttons (screen tabs)

| Button | objectId |
|--------|----------|
| Palettes | `ScreenWindow/DirectoryBar/text_Palettes~1` |
| Master | `ScreenWindow/DirectoryBar/text_Master~1` |
| Programmer | `ScreenWindow/DirectoryBar/text_Programmer~1` |
| Output | `ScreenWindow/DirectoryBar/text_Output~1` |

### Window management buttons

| Button | objectId |
|--------|----------|
| Views | `ScreenWindow/DirectoryBar/Views` |
| Help | `ScreenWindow/DirectoryBar/Help` |
| Copy | `ScreenWindow/DirectoryBar/Copy` |
| Size | `ScreenWindow/DirectoryBar/Size` |
| Move | `ScreenWindow/DirectoryBar/Move` |
| Max | `ScreenWindow/DirectoryBar/Max` |
| Focus | `ScreenWindow/DirectoryBar/Focus` |
| Close | `ScreenWindow/DirectoryBar/Close` |

Click any of these with:

```
qt_methods_invoke(objectId="ScreenWindow/DirectoryBar/<objectId>", method="click")
```

## Virtual Front Panel (Command Line Keys)

Hog's command line is driven by front panel key presses. There are two front panel widgets:

- **SimplifiedPanel** — QWidget-based, always present (one per screen). Keys are QWidget children found by objectName.
- **Full Panel** — QML-based (`QQuickWidget` with `windowTitle: "Full panel"`). Keys are QML `Text` elements found by objectName. Must be opened explicitly.

The SimplifiedPanel is the easiest to automate since its keys are real QWidgets and `qt_ui_click` works directly.

### Key base path

All SimplifiedPanel keys live under:

```
frontPanel/QWidget/QWidget/QWidget#6/<keyName>
```

For secondary screens, use `frontPanel~N/...` instead of `frontPanel/...`.

### Key name reference

| Key | objectName | Notes |
|-----|-----------|-------|
| `0` - `9` | `0` - `9` | Number keys |
| `.` | `.` | Decimal point |
| `Thru` | `Thru` | Range operator |
| `At` | `At` | The `@` key — set level |
| `Full` | `Full` | Set to 100% |
| `Enter` | `Enter` | Execute command |
| `Record` | `Record` | Record cue/palette |
| `Go` | `Go` | Go on cuelist |
| `Pause` | `Pause` | Pause cuelist |
| `Clear` | `Clear` | Clear command line / programmer |
| `Delete` | `Delete` | Delete objects |
| `Copy` | `Copy` | Copy objects |
| `Move` | `Move` | Move objects |
| `Merge` | `Merge` | Merge into existing |
| `Update` | `Update` | Update cue/palette |
| `Open` | `Open` | Open window |
| `Set` | `Set` | Set parameter |
| `Next` | `Next` | Next fixture |
| `Previous` | `Previous` | Previous fixture |
| `Blind` | `Blind` | Blind mode toggle |
| `Group` | `Group` | Group kind key |
| `Fixture` | `Fixture` | Fixture kind key |
| `Scene` | `Scene` | Scene/cuelist kind key |
| `Cue` | `Cue` | Cue kind key |
| `List` | `List` | List kind key |
| `Effect` | `Effect` | Effect kind key |
| `Macro` | `Macro` | Macro kind key |
| `Page` | `Page` | Page key |
| `NextPage` | `NextPage` | Next page |
| `Intens` | `Intens` | Intensity kind |
| `Positn` | `Positn` | Position kind |
| `Colour` | `Colour` | Colour kind |
| `Beam` | `Beam` | Beam kind |
| `Goto` | `Goto` | Goto cue |
| `Release` | `Release` | Release master |
| `Pig` | `Pig` | Pig (modifier) key |
| `HighLight` | `HighLight` | Highlight mode |
| `Minus` | `-` | Minus / remove from selection |
| `Slash` | `/` | Slash key |
| `Back` | `Back` | Backspace |

### Typing a command: fpType pattern

The Squish test helper `fpType()` (in `SquishTesting/GlobalScripts/FrontPanelHelper.js`) uses a semicolon-delimited string to type front panel key sequences. The qtPilot equivalent:

**Squish:**
```javascript
fpTypeAndEnter("1;Thru;1;0;At;Full");
```

**qtPilot equivalent — click each key in sequence:**

```
qt_ui_click(objectId="frontPanel/QWidget/QWidget/QWidget#6/1")
qt_ui_click(objectId="frontPanel/QWidget/QWidget/QWidget#6/Thru")
qt_ui_click(objectId="frontPanel/QWidget/QWidget/QWidget#6/1")
qt_ui_click(objectId="frontPanel/QWidget/QWidget/QWidget#6/0")
qt_ui_click(objectId="frontPanel/QWidget/QWidget/QWidget#6/At")
qt_ui_click(objectId="frontPanel/QWidget/QWidget/QWidget#6/Full")
qt_ui_click(objectId="frontPanel/QWidget/QWidget/QWidget#6/Enter")
```

This executes `1 Thru 10 @ Full Enter` — select fixtures 1 through 10, set to full intensity.

### Common command examples

**Select fixture 1 at 50%:**
```
Keys: 1 ; At ; 5 ; 0 ; Enter
```

**Record cue 1:**
```
Keys: Record ; 1 ; Enter
```

**Go on cuelist:**
```
Keys: Go
```

**Clear the command line:**
```
Keys: Clear
```

**Open fixture window (via front panel):**
```
Keys: Open ; Fixture ; Enter
```

### Name mapping (fpType names to objectNames)

When translating from Squish `fpType` strings, note these aliases:

| fpType input | objectName on SimplifiedPanel |
|-------------|------------------------------|
| `@` or `At` | `At` |
| `>` or `Thru` | `Thru` |
| `Dot` or `Decimal` or `.` | `.` |
| `Minus` or `-` | `-` |
| `Slash` or `/` | `/` |
| `ArrwLf` or `Back` | `Back` |
| Numbers `0`-`9` | `0`-`9` (unchanged) |
| All others | Capitalized objectName (e.g., `Full`, `Enter`, `Record`) |

## Tips

- **Multiple screens:** Hog may have multiple ScreenWindows. The primary screen's DirectoryBar is at `ScreenWindow/DirectoryBar`. Secondary screens use `ScreenWindow~N/DirectoryBar`.
- **HogButtonsPopup numbering:** Each click on the hog icon creates a new popup instance (`HogButtonsPopup`, `HogButtonsPopup#2`, etc.). Always use the base name `HogButtonsPopup` — it refers to the most recently created instance.
- **QML content:** The main desk content (playback bars, encoders, command line) is QML. Use `qt_objects_query(properties={"text": "..."})` to find QML elements, and coordinate-based `qt_ui_click` on the parent `ScreenWindow` for interaction.
- **Probe discovery:** After launching with `--inject-children`, use `qtpilot_list_probes()` to find all instrumented processes and their WebSocket URLs.
- **Front panel key clicks:** Use `qt_ui_click` on SimplifiedPanel keys (QWidget-based). The `qt_methods_invoke` with `click` also works but `qt_ui_click` is simpler since these are real QWidgets.
- **Text input fields:** For search boxes and text fields, prefer `qt_properties_set(name="text", value="...")` over `qt_ui_sendKeys(text="...")` — sendKeys delivers individual key events asynchronously and characters can arrive out of order.

## Source References

- Hog menu items and actions: `components/gui_core/datacache/directory_bar.cpp` (lines 430-550)
- HogButtonsPopup widget: `components/gui_windows/hog_buttons_popup.h`
- Front panel helper (fpType): `SquishTesting/GlobalScripts/FrontPanelHelper.js`
- Front panel key names: `SquishTesting/GlobalScripts/gnames.js`
- Squish test object names: `SquishTesting/suite_PreRelease_Software/shared/scripts/names.js`
