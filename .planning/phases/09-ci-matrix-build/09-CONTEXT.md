# Phase 9: CI Matrix Build - Context

**Gathered:** 2026-02-02
**Status:** Ready for planning

<domain>
## Phase Boundary

GitHub Actions workflow that builds the probe for 4 Qt versions (5.15, 6.2, 6.8, 6.9) on 2 platforms (Windows MSVC, Linux GCC) — 8 matrix cells. All cells must pass. Artifacts uploaded for downstream use. Tag-triggered releases and patched Qt 5.15.1 builds are separate phases.

</domain>

<decisions>
## Implementation Decisions

### Trigger policy
- Trigger on push to main AND pull requests targeting main
- Include workflow_dispatch for manual triggering
- No tag triggers (Phase 11 handles release workflow)
- Path filters: only trigger on changes to src/**, CMakeLists.txt, cmake/**, .github/workflows/** (skip docs-only changes)

### Matrix shape
- 8 cells: 4 Qt versions x 2 platforms
- Linux: Ubuntu 22.04 for Qt 5.15, Ubuntu 24.04 for Qt 6.x
- Windows: windows-latest for all Qt versions
- Release builds only (no Debug in CI)
- fail-fast disabled — let all cells finish even if one fails, then report overall failure
- Install all optional deps (nlohmann_json, spdlog, X11/XTest on Linux)

### Artifact output
- Upload per cell: probe binary + launcher executable + import library (.lib/.a)
- Include PDB debug symbols on Windows
- Artifact name encodes platform: `qtpilot-qt6.8-windows-msvc` / `qtpilot-qt6.8-linux-gcc`
- Retention: 7 days

### CMake invocation
- Build with tests enabled (QTPILOT_BUILD_TESTS=ON), run ctest — failures block the check
- Run cmake --install and verify the install layout (versioned paths correct)
- Optional deps installed via vcpkg (nlohmann_json, spdlog on both platforms)
- Qt installed via jurplel/install-qt-action@v4

### Claude's Discretion
- Whether to use QTPILOT_QT_DIR or CMAKE_PREFIX_PATH for Qt path in CI (pick whichever is more robust)
- vcpkg caching strategy
- Exact cmake configure/build/test/install command flags
- Workflow file structure (single file vs reusable workflows)

</decisions>

<specifics>
## Specific Ideas

- Artifact naming should work well for Phase 11's release workflow (collecting all binaries into a GitHub Release)
- Install layout verification ensures Phase 8's versioned paths work in a clean CI environment

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope

</deferred>

---

*Phase: 09-ci-matrix-build*
*Context gathered: 2026-02-02*
