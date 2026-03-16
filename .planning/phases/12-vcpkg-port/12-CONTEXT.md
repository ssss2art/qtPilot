# Phase 12: vcpkg Port - Context

**Gathered:** 2026-02-03
**Status:** Ready for planning

<domain>
## Phase Boundary

Users can install the qtPilot probe via vcpkg overlay port. Two port variants: source build (compiles against user's Qt) and binary download (fetches prebuilt from GitHub Releases). Source port is the recommended primary path. Overlay ports live in the repo; public registry submission is deferred.

</domain>

<decisions>
## Implementation Decisions

### Port scope
- Both source and binary ports provided
- Source port is the primary/recommended install path; binary port is a convenience alternative
- Overlay ports directory at `ports/` in repo root
- Registry submission deferred to future work

### Port naming
- Claude's Discretion: separate ports (qtpilot / qtpilot-bin) vs single port with feature flag — pick the idiomatic vcpkg approach

### Qt version detection
- Source port: try vcpkg-provided Qt first, fall back to system Qt via CMAKE_PREFIX_PATH
- Source port auto-detects Qt major.minor version at configure time for artifact naming
- Binary port: detects installed Qt (vcpkg or system), maps to matching GitHub Release artifact
- Binary port fails with FATAL_ERROR if detected Qt version has no matching prebuilt binary — message lists available versions and suggests source port instead

### Install experience
- After install, user does `find_package(qtPilot)` + `target_link_libraries` — standard CMake workflow
- No injection helper function — user handles LD_PRELOAD/DLL injection themselves
- Include a vcpkg usage file with quick-start instructions shown after install
- Both x64-windows and x64-linux triplets supported from day one

### Binary download
- Portfile pins a specific release tag with SHA512 hash per artifact — reproducible builds
- Hashes embedded directly in portfile.cmake (vcpkg standard), not fetched from SHA256SUMS
- Downloads only the single probe matching the detected Qt version — minimal disk usage
- Binary port installs probe binary AND generates qtPilotConfig.cmake so find_package works identically to source port

### Claude's Discretion
- Exact portfile.cmake structure and vcpkg helper usage
- How to structure the CMake config generation in the binary port
- vcpkg.json dependency declarations and feature flags
- Whether to use vcpkg_from_github or vcpkg_download_distfile for source port

</decisions>

<specifics>
## Specific Ideas

- Source port should work seamlessly with the Phase 8 CMake build system (qtPilotConfig.cmake already exists)
- Binary port should produce an identical find_package experience to the source port — user code shouldn't know which port variant was used
- The existing GitHub Release artifacts from Phase 11 (qtPilot-probe-qt{M}.{m}.dll/.so) are the download targets for the binary port

</specifics>

<deferred>
## Deferred Ideas

- vcpkg public registry submission (microsoft/vcpkg PR) — future phase or task
- macOS triplet support — blocked on macOS platform support (v1.2?)

</deferred>

---

*Phase: 12-vcpkg-port*
*Context gathered: 2026-02-03*
