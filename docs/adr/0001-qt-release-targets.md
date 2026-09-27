# ADR 0001: Qt Version Support Tiers and CI Matrix Policy

## Status
Accepted

## Context
qtPilot introspects and automates Qt applications across Linux, macOS, Windows, Android, and iOS. Qt issues frequent releases, including short-term feature releases, Long Term Support (LTS) releases (e.g. 5.15, 6.8), and active development branches.

Previously, CI tested an uncoordinated mix of versions (5.15.2, 6.5.3, 6.8.0, 6.9.0, 6.10.0), resulting in:
1. Combinatorial matrix bloat and long CI cycle times.
2. Android pinned to legacy 6.5.3 while iOS was on 6.10.0.
3. Frequent PR discussions regarding which versions to test and package.
4. Redundant intermediate versions in the prebuilt artifact catalog (`download-tools`).

## Decision

We adopt a strict **Three-Tier Policy** for desktop platforms and a **Latest-Only Policy** for mobile platforms using official, stable tooling and SHA-pinned actions:

### 1. Desktop Platforms (Linux, Windows, macOS)
- **Tier 1: Oldest Supported Baseline (`Oldest`)**
  - **Linux & Windows:** Qt 5.15.2 (Qt 5 LTS baseline, including Windows 32-bit x86).
  - **macOS:** Qt 5 is unsupported due to C++23 compiler and Apple Silicon requirements (requires Qt 6.5+; see `docs/MACOS.md`).
- **Tier 2: Latest Active LTS (`LTS`)**
  - **All Desktop Platforms:** Qt 6.8.x (current Qt 6 LTS).
  - Advances when the Qt Project designates a new LTS release and it matures in CI tooling.
- **Tier 3: Active Qt Latest (`Latest`)**
  - **All Desktop Platforms:** Qt 6.10.x (current stable release).
  - Advances when new minor/major Qt releases become available in stable, official `aqtinstall` releases on PyPI.
  - Includes a Linux non-QML compile variant (`-DQTPILOT_ENABLE_QML=OFF`) to ensure pure widget apps compile cleanly without QML dependencies.

### 2. Mobile Platforms (Android, iOS)
- **Mobile Latest:** Both Android and iOS strictly target **Qt Latest** (currently Qt 6.10.x).
- **Rationale:** Mobile OS toolchains (Android NDK, iOS SDKs) enforce rapid modernization. Furthermore, sharing host Qt (Ubuntu 6.10.0) with desktop Linux eliminates duplicate Qt downloads and optimizes runner cache reuse.

### 3. Supply Chain Security & SHA Pinning
- All external GitHub Actions in workflows (`.github/workflows/`) MUST be referenced by full 40-character commit SHA with inline version comment.
- Official package sources only: avoiding unreleased or ad-hoc git dependencies in production CI runners.

### 4. Shipped Tool Distribution (`download-tools`)
- The prebuilt distribution catalog in `python/src/qtpilot/download.py` (`BUILD_MATRIX`) must match the CI release matrix exactly.
- Intermediate non-LTS versions (e.g. 6.5, 6.9) are not built by CI and will fail fast at request time (`VersionNotFoundError`) rather than 404ing at download time.

### 5. Progression Criteria to Qt 6.11
- Qt 6.11.2 packages exist in official Qt repositories. However, Qt upstream altered the Windows directory layout in 6.11, which causes the current PyPI release of `aqtinstall` (`3.3.0`) to fail on Windows.
- Tier 3 will advance from Qt 6.10.0 to Qt 6.11.x as soon as `aqtinstall 3.4.0` (which fixes this layout issue) is officially released on PyPI.

## Consequences
- Total desktop build legs reduced from 13 to 10, cutting CI runtime and cache usage.
- Mobile platforms are unified on modern toolchains.
- The policy is self-governing: when Qt 6.11 tooling matures or a new LTS ships, the tiers advance by rule without ad-hoc debates.
