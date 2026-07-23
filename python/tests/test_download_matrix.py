"""Tests for download availability: version/platform/arch matrix + input validation.

These encode two review findings:
  * `latest_version()` used a lexicographic `max`, wrongly ranking "6.9" > "6.10".
  * `download.py` advertised (version, platform, arch) combinations that the CI
    release matrix never builds, so `download-tools` 404'd instead of failing
    fast with a clear error.
  * `arch` / `release_tag` were interpolated into the URL/filename unvalidated.

Run stdlib-only (no fastmcp/conftest needed):
    pytest python/tests/test_download_matrix.py --noconftest
"""

from __future__ import annotations

import pytest

from qtpilot.download import (
    AVAILABLE_VERSIONS,
    BUILD_MATRIX,
    UnsupportedPlatformError,
    VersionNotFoundError,
    build_archive_url,
    is_build_available,
    latest_version,
)

# What the CI release matrix (.github/workflows/ci.yml) actually builds today.
# Kept here as an independent copy so a drift between the shipped matrix and CI
# is caught by test review.
EXPECTED_BUILDS = {
    ("5.15", "linux", "x64"),
    ("6.5", "linux", "x64"),
    ("6.8", "linux", "x64"),
    ("6.9", "linux", "x64"),
    ("6.10", "linux", "x64"),
    ("5.15", "windows", "x64"),
    ("6.5", "windows", "x64"),
    ("6.8", "windows", "x64"),
    ("6.9", "windows", "x64"),
    ("6.10", "windows", "x64"),
    ("5.15", "windows", "x86"),
    ("6.10", "macos", "arm64"),
}


class TestLatestVersion:
    def test_latest_version_is_numeric_max_not_lexicographic(self) -> None:
        # Lexicographically "6.9" > "6.10"; numerically 6.10 is newest.
        assert latest_version() == "6.10"


class TestBuildMatrix:
    def test_matrix_matches_ci(self) -> None:
        assert set(BUILD_MATRIX) == EXPECTED_BUILDS

    def test_available_versions_derived_from_matrix(self) -> None:
        assert set(AVAILABLE_VERSIONS) == {v for (v, _p, _a) in EXPECTED_BUILDS}
        # 5.15-patched was advertised but never built.
        assert "5.15-patched" not in AVAILABLE_VERSIONS

    @pytest.mark.parametrize("combo", sorted(EXPECTED_BUILDS))
    def test_built_combos_are_available(self, combo: tuple[str, str, str]) -> None:
        version, platform_name, arch = combo
        assert is_build_available(version, platform_name, arch)
        # And a URL can be built without raising.
        url = build_archive_url(version, "v9.9.9", platform_name=platform_name, arch=arch)
        assert url.endswith((".zip", ".tar.gz"))


class TestUnbuiltCombosFailFast:
    """Combos the CI never builds must raise, not silently 404 at download time."""

    def test_macos_x86_64_not_built(self) -> None:
        with pytest.raises((VersionNotFoundError, UnsupportedPlatformError)):
            build_archive_url("6.10", "v1.0.0", platform_name="macos", arch="x86_64")

    def test_macos_non_610_not_built(self) -> None:
        with pytest.raises((VersionNotFoundError, UnsupportedPlatformError)):
            build_archive_url("6.8", "v1.0.0", platform_name="macos", arch="arm64")

    def test_windows_x86_only_for_515(self) -> None:
        # 5.15 x86 builds; 6.x x86 does not.
        build_archive_url("5.15", "v1.0.0", platform_name="windows", arch="x86")
        with pytest.raises((VersionNotFoundError, UnsupportedPlatformError)):
            build_archive_url("6.8", "v1.0.0", platform_name="windows", arch="x86")

    def test_linux_x86_not_built(self) -> None:
        with pytest.raises((VersionNotFoundError, UnsupportedPlatformError)):
            build_archive_url("6.10", "v1.0.0", platform_name="linux", arch="x86")

    def test_patched_not_built(self) -> None:
        with pytest.raises(VersionNotFoundError):
            build_archive_url("5.15-patched", "v1.0.0", platform_name="linux", arch="x64")


class TestInputValidation:
    def test_arch_path_traversal_rejected(self) -> None:
        with pytest.raises((ValueError, UnsupportedPlatformError, VersionNotFoundError)):
            build_archive_url("6.10", "v1.0.0", platform_name="linux", arch="../../evil")

    def test_release_tag_path_separator_rejected(self) -> None:
        with pytest.raises(ValueError):
            build_archive_url("6.10", "../../evil", platform_name="linux", arch="x64")

    def test_release_tag_absolute_rejected(self) -> None:
        with pytest.raises(ValueError):
            build_archive_url("6.10", "/etc/passwd", platform_name="linux", arch="x64")
