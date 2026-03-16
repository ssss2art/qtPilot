# Spec Compatibility Report: Build Simplification vs. Claude Code Plugin

## Executive Summary

The two specs have **5 breaking conflicts** that must be resolved before either can be implemented. The root cause is that the build-simplification spec was written without awareness of the plugin spec's binary distribution requirements. The good news: the two specs operate on largely **separate concerns** (CMake install layout vs. Python/plugin download system), so a unified approach is achievable with targeted amendments to both specs.

---

## Conflict #1: Binary Naming — Qt Version Tag

| | Build Spec | Plugin Spec |
|---|---|---|
| Probe name | `qtPilot-probe` (no version) | `qtPilot-probe-qt6.8-windows-msvc.dll` (versioned + platform) |
| Launcher name | `qtPilot-launcher` | `qtpilot-launch` |

**Why it matters:** The plugin's download cache stores multiple Qt-version-specific probe binaries side-by-side. Stripping the Qt version from the filename makes this impossible.

**Resolution:** Use two naming layers:

1. **CMake build output** — keep Qt version tag in probe name: `qtPilot-probe-qt6.8.dll`. This is the status quo and costs nothing to keep.
2. **GitHub Release artifacts** — add platform suffix: `qtPilot-probe-qt6.8-windows-msvc.dll`. This is already how `release.yml` and `download.py` work.
3. **Launcher** — standardize on `qtpilot-launch` (current name), not `qtPilot-launcher` (build spec's proposed rename). The launcher is NOT Qt-version-specific, so drop the Qt tag from it.

**Changes needed:**
- Build spec: remove the line about stripping Qt version from probe filename
- Build spec: keep launcher name as `qtpilot-launch`, not `qtPilot-launcher`
- Plugin spec: no changes needed (its naming is already correct)

---

## Conflict #2: Deleting .github/ Kills Binary Distribution

The build spec says: *"Delete `.github/` (entire directory)"*

The plugin spec depends on:
- `release.yml` — creates GitHub Releases with probe binaries (the plugin's download source)
- `publish-pypi.yml` — publishes the Python package (required for `uvx qtpilot` to work)
- `ci.yml` — builds the 8 probe binaries that `release.yml` packages
- `ci-patched-qt.yml` + `actions/build-qt/` — builds 2 patched-Qt binaries

**If .github/ is deleted:** `download.py` can't download anything, `uvx qtpilot` can't install anything, the plugin is dead on arrival.

**Resolution:** Change "delete `.github/`" to "simplify `.github/`":

| Keep | Simplify | Remove |
|---|---|---|
| `release.yml` | `ci.yml` — strip CodeQL, clang-format, clang-tidy | Nothing else is safe to delete |
| `publish-pypi.yml` | `ci.yml` — update preset names after CMakePresets.json rewrite | |
| `ci-patched-qt.yml` | | |
| `actions/build-qt/` | | |

**Changes needed:**
- Build spec: replace "Delete `.github/`" with "Simplify CI workflows — remove linting/CodeQL, update preset references"
- Build spec: add launcher binary to `release.yml` artifact list (plugin needs it, currently missing)

---

## Conflict #3: Removing CI Presets Breaks Workflows

The build spec reduces CMakePresets.json from 6 to 2 presets (`debug` and `release`), eliminating `ci-linux` and `ci-windows`.

The CI workflows (`ci.yml`) reference `--preset ci-linux` and `--preset ci-windows` throughout.

**Resolution:** When rewriting CMakePresets.json, either:
- **(A)** Add `ci-linux` / `ci-windows` presets that inherit from `release` with platform-specific settings (keeps workflows working with minimal changes), or
- **(B)** Update the CI workflow YAML files to use the new preset names (`release` + platform flags)

Option (A) is simpler — the presets are just 10 lines each.

**Changes needed:**
- Build spec: add CI presets as thin wrappers over the simplified base presets

---

## Conflict #4: Removing vcpkg Breaks CI Builds

The build spec removes `vcpkg.json` and all vcpkg integration. The CI workflows use `lukka/run-vcpkg@v11` for dependency resolution.

**Current reality:** The project only uses Qt (already available via `aqtinstall` in CI) and has already removed nlohmann_json and spdlog dependencies. vcpkg was providing these optional deps.

**Resolution:** Since the optional deps are being removed (build spec, section 3), vcpkg is genuinely unnecessary. The CI workflows just need to stop referencing it.

**Changes needed:**
- Build spec: explicitly note that CI workflows must be updated to remove vcpkg steps
- CI workflows: replace `lukka/run-vcpkg` with direct Qt installation (already partially done via `jurplel/install-qt-action`)

---

## Conflict #5: Deleting .ci/ Breaks Patched Qt Builds

The build spec says: *"Delete `.ci/` (entire directory)"*

`ci-patched-qt.yml` uses `.ci/patches/` to build patched Qt 5.15.1 from source.

**Resolution:** Keep `.ci/` — it's only ~10 files of Qt patches, not build infrastructure bloat. Or move the patches into `.github/patches/` to consolidate.

**Changes needed:**
- Build spec: remove `.ci/` from the deletion list, or move its contents under `.github/`

---

## Non-Conflicts (Things That Work Fine)

| Concern | Why it's fine |
|---|---|
| **Install path simplification** (`lib/qtpilot/qt6.8/` → `lib/`) | Plugin/Python side has ZERO dependency on CMake install paths. It uses its own cache dir. |
| **qtPilotConfig.cmake.in simplification** | Only affects `find_package(qtPilot)` consumers. Plugin uses `uvx`, not CMake. |
| **qtPilot_inject_probe.cmake** | Uses CMake generator expressions only — immune to path changes. |
| **tests/CMakeLists.txt simplification** | Purely internal build concern, no plugin impact. |
| **Removing nlohmann_json/spdlog** | Project already uses QJsonDocument and QDebug. |
| **Root CMakeLists.txt cleanup** | Internal build concern. |

---

## Recommended Implementation Order

### Phase 1: Amend the Build Spec
Update `build-simplification-spec.md` to resolve all 5 conflicts:
1. Keep Qt version tag in probe binary name
2. Change "delete `.github/`" to "simplify `.github/`"
3. Add CI presets to CMakePresets.json
4. Note vcpkg removal requires CI workflow updates
5. Keep `.ci/` directory (or relocate patches)

### Phase 2: Implement Build Simplification (amended)
Execute the build spec with amendments. This must go first because:
- The plugin spec's `.mcp.json` references `uvx qtpilot` — the Python package must exist on PyPI
- The plugin spec's download system depends on GitHub Releases — the release workflow must work
- Binary naming must be stable before the plugin hardcodes expectations

### Phase 3: Implement Plugin Packaging
Create the `plugin/` directory with all plugin artifacts. By this point:
- Binary naming is settled and matches what `download.py` expects
- GitHub Releases are working and publishing correct artifacts
- PyPI publishing works for `uvx`

### Phase 4: Add Missing Pieces
- Add launcher binary to `release.yml` (currently only probes are released)
- Implement `ensure_probe()`, `ensure_launcher()`, `get_cache_dir()` in `download.py`
- Add Qt version detection to `connection.py`

---

## One-Line Summary

> The build spec must stop short of deleting `.github/` and stripping Qt version tags — the plugin spec needs both. Everything else is compatible.
