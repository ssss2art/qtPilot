"""Unit tests for the download module."""

from __future__ import annotations

import hashlib
import io
import sys
import tarfile
import urllib.error
import zipfile
from pathlib import Path
from unittest import mock

import pytest

from qtpilot.download import (
    AVAILABLE_VERSIONS,
    DEFAULT_ARCH,
    LINUX_ARCHITECTURES,
    WINDOWS_ARCHITECTURES,
    ChecksumError,
    DownloadError,
    UnsupportedPlatformError,
    VersionNotFoundError,
    _default_release_tag,
    build_archive_url,
    build_checksums_url,
    detect_platform,
    download_and_extract,
    extract_archive,
    get_archive_filename,
    get_launcher_filename,
    get_probe_filename,
    normalize_version,
    parse_checksums,
    verify_checksum,
)


class TestPlatformDetection:
    """Tests for platform detection logic."""

    def test_linux_platform_detection(self) -> None:
        """Linux platforms should return 'linux'."""
        with mock.patch("qtpilot.download.sys.platform", "linux"):
            assert detect_platform() == "linux"

    def test_linux2_platform_detection(self) -> None:
        """linux2 (older Python) should also work."""
        with mock.patch("qtpilot.download.sys.platform", "linux2"):
            assert detect_platform() == "linux"

    def test_win32_platform_detection(self) -> None:
        """Windows platforms should return 'windows'."""
        with mock.patch("qtpilot.download.sys.platform", "win32"):
            assert detect_platform() == "windows"

    def test_darwin_platform_detection(self) -> None:
        """darwin should return 'macos'."""
        with mock.patch("qtpilot.download.sys.platform", "darwin"):
            assert detect_platform() == "macos"

    def test_unsupported_platform_raises(self) -> None:
        """Unsupported platforms should raise UnsupportedPlatformError."""
        with mock.patch("qtpilot.download.sys.platform", "freebsd"):
            with pytest.raises(UnsupportedPlatformError) as exc_info:
                detect_platform()
            assert "freebsd" in str(exc_info.value)
            assert "Supported platforms" in str(exc_info.value)


class TestDefaultReleaseTag:
    """Tests for default release tag derivation."""

    def test_clean_version(self) -> None:
        """Clean version produces v-prefixed tag."""
        with mock.patch("qtpilot.download._pkg_version", return_value="0.2.0"):
            assert _default_release_tag() == "v0.2.0"

    def test_dev_suffix_stripped(self) -> None:
        """Dev suffix is stripped."""
        with mock.patch("qtpilot.download._pkg_version", return_value="0.3.0.dev5+gabcdef"):
            assert _default_release_tag() == "v0.3.0"

    def test_post_suffix_stripped(self) -> None:
        """Post suffix is stripped."""
        with mock.patch("qtpilot.download._pkg_version", return_value="0.2.0.post1"):
            assert _default_release_tag() == "v0.2.0"


class TestVersionNormalization:
    """Tests for Qt version normalization."""

    def test_short_version(self) -> None:
        """Short versions pass through unchanged."""
        assert normalize_version("6.8") == "6.8"

    def test_long_version_trimmed(self) -> None:
        """Patch versions are trimmed to major.minor."""
        assert normalize_version("6.8.0") == "6.8"
        assert normalize_version("5.15.2") == "5.15"

    def test_patched_suffix_preserved(self) -> None:
        """Patched suffix is preserved after normalization."""
        assert normalize_version("5.15-patched") == "5.15-patched"
        assert normalize_version("5.15.1-patched") == "5.15-patched"


class TestFilenames:
    """Tests for simplified filename generation."""

    def test_probe_filename_windows(self) -> None:
        """Windows probe filename."""
        assert get_probe_filename("windows") == "qtPilot-probe.dll"

    def test_probe_filename_linux(self) -> None:
        """Linux probe filename."""
        assert get_probe_filename("linux") == "qtPilot-probe.so"

    def test_probe_filename_auto_detect(self) -> None:
        """Probe filename auto-detects platform."""
        with mock.patch("qtpilot.download.sys.platform", "win32"):
            assert get_probe_filename() == "qtPilot-probe.dll"
        with mock.patch("qtpilot.download.sys.platform", "linux"):
            assert get_probe_filename() == "qtPilot-probe.so"

    def test_launcher_filename_windows(self) -> None:
        """Windows launcher filename."""
        assert get_launcher_filename("windows") == "qtPilot-launcher.exe"

    def test_launcher_filename_linux(self) -> None:
        """Linux launcher filename."""
        assert get_launcher_filename("linux") == "qtPilot-launcher"

    def test_launcher_filename_auto_detect(self) -> None:
        """Launcher filename auto-detects platform."""
        with mock.patch("qtpilot.download.sys.platform", "win32"):
            assert get_launcher_filename() == "qtPilot-launcher.exe"
        with mock.patch("qtpilot.download.sys.platform", "linux"):
            assert get_launcher_filename() == "qtPilot-launcher"


class TestArchiveFilename:
    """Tests for archive filename generation."""

    def test_windows_zip_default_x64(self) -> None:
        """Windows archives default to x64 and include arch suffix."""
        assert get_archive_filename("6.8", "windows") == "qtpilot-qt6.8-windows-x64.zip"

    def test_windows_zip_explicit_x64(self) -> None:
        """Windows archives with explicit x64."""
        assert get_archive_filename("6.8", "windows", arch="x64") == "qtpilot-qt6.8-windows-x64.zip"

    def test_windows_zip_x86(self) -> None:
        """Windows x86 archives include -x86 suffix."""
        assert get_archive_filename("6.8", "windows", arch="x86") == "qtpilot-qt6.8-windows-x86.zip"

    def test_linux_tar_gz_default_x64(self) -> None:
        """Linux x64 archives have no arch suffix (backward compat)."""
        assert get_archive_filename("6.8", "linux") == "qtpilot-qt6.8-linux.tar.gz"

    def test_linux_tar_gz_explicit_x64(self) -> None:
        """Linux with explicit x64 has no arch suffix."""
        assert get_archive_filename("6.8", "linux", arch="x64") == "qtpilot-qt6.8-linux.tar.gz"

    def test_linux_tar_gz_x86(self) -> None:
        """Linux x86 archives include -x86 suffix."""
        assert get_archive_filename("6.8", "linux", arch="x86") == "qtpilot-qt6.8-linux-x86.tar.gz"

    def test_patched_version(self) -> None:
        """Patched versions should be preserved in filename."""
        assert get_archive_filename("5.15-patched", "linux") == "qtpilot-qt5.15-patched-linux.tar.gz"

    def test_patched_version_x86(self) -> None:
        """Patched version with x86 arch."""
        assert get_archive_filename("5.15-patched", "linux", arch="x86") == "qtpilot-qt5.15-patched-linux-x86.tar.gz"

    def test_version_normalization(self) -> None:
        """Full version strings should be normalized."""
        assert get_archive_filename("6.8.0", "windows") == "qtpilot-qt6.8-windows-x64.zip"

    def test_auto_detect_platform(self) -> None:
        """Platform should be auto-detected when not specified."""
        with mock.patch("qtpilot.download.sys.platform", "win32"):
            assert get_archive_filename("6.8") == "qtpilot-qt6.8-windows-x64.zip"

    def test_arch_none_defaults_to_x64(self) -> None:
        """arch=None should default to x64 behavior."""
        assert get_archive_filename("6.8", "windows", arch=None) == "qtpilot-qt6.8-windows-x64.zip"
        assert get_archive_filename("6.8", "linux", arch=None) == "qtpilot-qt6.8-linux.tar.gz"


class TestArchiveUrlBuilding:
    """Tests for archive URL construction."""

    def test_build_archive_url_windows_default_x64(self) -> None:
        """Build correct URL for Windows archive (default x64)."""
        url = build_archive_url("6.8", release_tag="v0.3.0", platform_name="windows")
        assert url == (
            "https://github.com/ssss2art/qtPilot/releases/download/"
            "v0.3.0/qtpilot-qt6.8-windows-x64.zip"
        )

    def test_build_archive_url_windows_x86(self) -> None:
        """Build correct URL for Windows x86 archive."""
        url = build_archive_url("6.8", release_tag="v0.3.0", platform_name="windows", arch="x86")
        assert url == (
            "https://github.com/ssss2art/qtPilot/releases/download/"
            "v0.3.0/qtpilot-qt6.8-windows-x86.zip"
        )

    def test_build_archive_url_linux(self) -> None:
        """Build correct URL for Linux archive (no arch suffix for x64)."""
        url = build_archive_url("6.8", release_tag="v0.3.0", platform_name="linux")
        assert url == (
            "https://github.com/ssss2art/qtPilot/releases/download/"
            "v0.3.0/qtpilot-qt6.8-linux.tar.gz"
        )

    def test_build_archive_url_linux_x86(self) -> None:
        """Build correct URL for Linux x86 archive."""
        url = build_archive_url("6.8", release_tag="v0.3.0", platform_name="linux", arch="x86")
        assert url == (
            "https://github.com/ssss2art/qtPilot/releases/download/"
            "v0.3.0/qtpilot-qt6.8-linux-x86.tar.gz"
        )

    def test_build_archive_url_patched(self) -> None:
        """Build correct URL for patched Qt version."""
        url = build_archive_url("5.15-patched", release_tag="v0.3.0", platform_name="linux")
        assert url == (
            "https://github.com/ssss2art/qtPilot/releases/download/"
            "v0.3.0/qtpilot-qt5.15-patched-linux.tar.gz"
        )

    def test_build_archive_url_invalid_version(self) -> None:
        """Invalid Qt version should raise VersionNotFoundError."""
        with pytest.raises(VersionNotFoundError) as exc_info:
            build_archive_url("6.0", release_tag="v0.3.0")
        assert "6.0" in str(exc_info.value)
        assert "Available versions" in str(exc_info.value)

    def test_build_archive_url_latest_uses_package_version(self) -> None:
        """'latest' release tag should resolve from package version."""
        with mock.patch("qtpilot.download._pkg_version", return_value="0.3.0"):
            url = build_archive_url("6.8", release_tag="latest", platform_name="linux")
            assert "/v0.3.0/" in url

    def test_build_archive_url_latest_strips_dev_suffix(self) -> None:
        """'latest' should strip dev suffixes from package version."""
        with mock.patch("qtpilot.download._pkg_version", return_value="0.3.0.dev5+gabcdef"):
            url = build_archive_url("6.8", release_tag="latest", platform_name="linux")
            assert "/v0.3.0/" in url

    def test_build_checksums_url(self) -> None:
        """Build correct URL for SHA256SUMS file."""
        url = build_checksums_url("v1.2.3")
        assert url == "https://github.com/ssss2art/qtPilot/releases/download/v1.2.3/SHA256SUMS"

    def test_build_checksums_url_latest_uses_package_version(self) -> None:
        """'latest' should resolve from package version for checksums."""
        with mock.patch("qtpilot.download._pkg_version", return_value="0.2.0"):
            url = build_checksums_url("latest")
            assert "/v0.2.0/" in url


class TestChecksumParsing:
    """Tests for SHA256SUMS file parsing."""

    def test_parse_standard_format(self) -> None:
        """Parse standard sha256sum output format."""
        content = """\
abc123def456  qtpilot-qt6.8-linux.tar.gz
789xyz012abc  qtpilot-qt6.8-windows-x64.zip
"""
        checksums = parse_checksums(content)
        assert checksums["qtpilot-qt6.8-linux.tar.gz"] == "abc123def456"
        assert checksums["qtpilot-qt6.8-windows-x64.zip"] == "789xyz012abc"

    def test_parse_binary_mode_format(self) -> None:
        """Parse sha256sum binary mode format (asterisk prefix)."""
        content = "abc123def456 *qtpilot-qt6.8-linux.tar.gz\n"
        checksums = parse_checksums(content)
        assert checksums["qtpilot-qt6.8-linux.tar.gz"] == "abc123def456"

    def test_parse_empty_lines_ignored(self) -> None:
        """Empty lines should be ignored."""
        content = "\nabc123def456  file.zip\n\n"
        checksums = parse_checksums(content)
        assert len(checksums) == 1


class TestChecksumVerification:
    """Tests for checksum verification."""

    def test_checksum_match(self, tmp_path: Path) -> None:
        """Checksum should match for correct file."""
        content = b"test file content"
        expected_hash = hashlib.sha256(content).hexdigest()

        test_file = tmp_path / "test.bin"
        test_file.write_bytes(content)

        assert verify_checksum(test_file, expected_hash) is True

    def test_checksum_mismatch(self, tmp_path: Path) -> None:
        """Checksum should not match for incorrect file."""
        test_file = tmp_path / "test.bin"
        test_file.write_bytes(b"test content")

        wrong_hash = "0" * 64
        assert verify_checksum(test_file, wrong_hash) is False

    def test_checksum_case_insensitive(self, tmp_path: Path) -> None:
        """Checksum comparison should be case-insensitive."""
        content = b"test"
        expected_hash = hashlib.sha256(content).hexdigest().upper()

        test_file = tmp_path / "test.bin"
        test_file.write_bytes(content)

        assert verify_checksum(test_file, expected_hash) is True


class TestExtractArchive:
    """Tests for archive extraction."""

    def test_extract_zip(self, tmp_path: Path) -> None:
        """Extract a zip archive with probe + launcher."""
        archive_path = tmp_path / "qtpilot-qt6.8-windows.zip"
        output_dir = tmp_path / "output"

        with zipfile.ZipFile(archive_path, "w") as zf:
            zf.writestr("qtPilot-probe.dll", b"probe binary")
            zf.writestr("qtPilot-launcher.exe", b"launcher binary")

        extracted = extract_archive(archive_path, output_dir)

        assert len(extracted) == 2
        assert (output_dir / "qtPilot-probe.dll").exists()
        assert (output_dir / "qtPilot-launcher.exe").exists()
        assert (output_dir / "qtPilot-probe.dll").read_bytes() == b"probe binary"
        assert (output_dir / "qtPilot-launcher.exe").read_bytes() == b"launcher binary"

    def test_extract_tar_gz(self, tmp_path: Path) -> None:
        """Extract a tar.gz archive with probe + launcher."""
        archive_path = tmp_path / "qtpilot-qt6.8-linux.tar.gz"
        output_dir = tmp_path / "output"

        with tarfile.open(archive_path, "w:gz") as tf:
            probe_data = b"probe binary"
            info = tarfile.TarInfo(name="qtPilot-probe.so")
            info.size = len(probe_data)
            tf.addfile(info, io.BytesIO(probe_data))

            launcher_data = b"launcher binary"
            info = tarfile.TarInfo(name="qtPilot-launcher")
            info.size = len(launcher_data)
            tf.addfile(info, io.BytesIO(launcher_data))

        extracted = extract_archive(archive_path, output_dir)

        assert len(extracted) == 2
        assert (output_dir / "qtPilot-probe.so").exists()
        assert (output_dir / "qtPilot-launcher").exists()

    def test_extract_zip_rejects_path_traversal(self, tmp_path: Path) -> None:
        """Zip archives with path traversal should be rejected."""
        archive_path = tmp_path / "evil.zip"
        output_dir = tmp_path / "output"

        with zipfile.ZipFile(archive_path, "w") as zf:
            zf.writestr("../../../etc/passwd", b"evil content")

        with pytest.raises(DownloadError, match="unsafe path"):
            extract_archive(archive_path, output_dir)

    def test_extract_tar_rejects_path_traversal(self, tmp_path: Path) -> None:
        """Tar archives with path traversal should be rejected."""
        archive_path = tmp_path / "evil.tar.gz"
        output_dir = tmp_path / "output"

        with tarfile.open(archive_path, "w:gz") as tf:
            data = b"evil content"
            info = tarfile.TarInfo(name="../../../etc/passwd")
            info.size = len(data)
            tf.addfile(info, io.BytesIO(data))

        with pytest.raises(DownloadError, match="unsafe path"):
            extract_archive(archive_path, output_dir)

    def test_extract_tar_rejects_symlinks(self, tmp_path: Path) -> None:
        """Tar archives with symlinks should be rejected."""
        archive_path = tmp_path / "evil.tar.gz"
        output_dir = tmp_path / "output"

        with tarfile.open(archive_path, "w:gz") as tf:
            info = tarfile.TarInfo(name="link")
            info.type = tarfile.SYMTYPE
            info.linkname = "/etc/passwd"
            tf.addfile(info)

        with pytest.raises(DownloadError, match="unsafe path"):
            extract_archive(archive_path, output_dir)

    def test_extract_unsupported_format(self, tmp_path: Path) -> None:
        """Unsupported archive formats should raise DownloadError."""
        archive_path = tmp_path / "file.rar"
        archive_path.write_bytes(b"not a real archive")
        output_dir = tmp_path / "output"

        with pytest.raises(DownloadError, match="Unsupported archive format"):
            extract_archive(archive_path, output_dir)

    def test_extract_bad_zip(self, tmp_path: Path) -> None:
        """Corrupt zip should raise DownloadError."""
        archive_path = tmp_path / "bad.zip"
        archive_path.write_bytes(b"not a zip file")
        output_dir = tmp_path / "output"

        with pytest.raises(DownloadError, match="Failed to extract"):
            extract_archive(archive_path, output_dir)


class TestDownloadAndExtract:
    """Tests for the main download_and_extract function."""

    def _make_zip(self, probe_data: bytes, launcher_data: bytes) -> bytes:
        """Create an in-memory zip archive."""
        buf = io.BytesIO()
        with zipfile.ZipFile(buf, "w") as zf:
            zf.writestr("qtPilot-probe.dll", probe_data)
            zf.writestr("qtPilot-launcher.exe", launcher_data)
        return buf.getvalue()

    def test_download_success_without_checksum(self, tmp_path: Path) -> None:
        """Download succeeds without checksum verification."""
        archive_data = self._make_zip(b"probe", b"launcher")

        def mock_urlopen(url: str, timeout: int | None = None) -> io.BytesIO:
            return io.BytesIO(archive_data)

        with mock.patch("qtpilot.download.sys.platform", "win32"):
            with mock.patch("qtpilot.download.urllib.request.urlopen", mock_urlopen):
                probe, launcher = download_and_extract(
                    "6.8",
                    output_dir=tmp_path,
                    verify=False,
                    release_tag="v0.3.0",
                )

        assert probe.exists()
        assert launcher.exists()
        assert probe.name == "qtPilot-probe.dll"
        assert launcher.name == "qtPilot-launcher.exe"
        assert probe.read_bytes() == b"probe"
        assert launcher.read_bytes() == b"launcher"

    def test_download_with_checksum_verification(self, tmp_path: Path) -> None:
        """Download verifies checksum when enabled."""
        archive_data = self._make_zip(b"probe", b"launcher")
        expected_hash = hashlib.sha256(archive_data).hexdigest()
        checksums_content = f"{expected_hash}  qtpilot-qt6.8-windows-x64.zip\n"

        call_count = {"count": 0}

        def mock_urlopen(url: str, timeout: int | None = None) -> io.BytesIO:
            call_count["count"] += 1
            if "SHA256SUMS" in url:
                return io.BytesIO(checksums_content.encode())
            return io.BytesIO(archive_data)

        with mock.patch("qtpilot.download.sys.platform", "win32"):
            with mock.patch("qtpilot.download.urllib.request.urlopen", mock_urlopen):
                probe, launcher = download_and_extract(
                    "6.8",
                    output_dir=tmp_path,
                    verify=True,
                    release_tag="v0.3.0",
                )

        assert probe.exists()
        assert launcher.exists()
        assert call_count["count"] == 2  # SHA256SUMS + archive

    def test_download_checksum_mismatch_raises(self, tmp_path: Path) -> None:
        """Checksum mismatch should raise ChecksumError."""
        archive_data = self._make_zip(b"probe", b"launcher")
        wrong_hash = "0" * 64
        checksums_content = f"{wrong_hash}  qtpilot-qt6.8-windows-x64.zip\n"

        def mock_urlopen(url: str, timeout: int | None = None) -> io.BytesIO:
            if "SHA256SUMS" in url:
                return io.BytesIO(checksums_content.encode())
            return io.BytesIO(archive_data)

        with mock.patch("qtpilot.download.sys.platform", "win32"):
            with mock.patch("qtpilot.download.urllib.request.urlopen", mock_urlopen):
                with pytest.raises(ChecksumError) as exc_info:
                    download_and_extract(
                        "6.8",
                        output_dir=tmp_path,
                        verify=True,
                        release_tag="v0.3.0",
                    )

        assert "verification failed" in str(exc_info.value)
        # Archive should be cleaned up
        assert not (tmp_path / "qtpilot-qt6.8-windows-x64.zip").exists()

    def test_archive_cleaned_up_after_extraction(self, tmp_path: Path) -> None:
        """Archive file should be deleted after successful extraction."""
        archive_data = self._make_zip(b"probe", b"launcher")

        def mock_urlopen(url: str, timeout: int | None = None) -> io.BytesIO:
            return io.BytesIO(archive_data)

        with mock.patch("qtpilot.download.sys.platform", "win32"):
            with mock.patch("qtpilot.download.urllib.request.urlopen", mock_urlopen):
                download_and_extract(
                    "6.8",
                    output_dir=tmp_path,
                    verify=False,
                    release_tag="v0.3.0",
                )

        # Archive should be cleaned up
        assert not (tmp_path / "qtpilot-qt6.8-windows-x64.zip").exists()
        # But extracted files should remain
        assert (tmp_path / "qtPilot-probe.dll").exists()
        assert (tmp_path / "qtPilot-launcher.exe").exists()


class TestErrorHandling:
    """Tests for error handling."""

    def test_404_error_handling(self, tmp_path: Path) -> None:
        """HTTP 404 should raise DownloadError."""
        def mock_urlopen(url: str, timeout: int | None = None) -> None:
            raise urllib.error.HTTPError(url, 404, "Not Found", {}, None)

        with mock.patch("qtpilot.download.sys.platform", "win32"):
            with mock.patch("qtpilot.download.urllib.request.urlopen", mock_urlopen):
                with pytest.raises(DownloadError) as exc_info:
                    download_and_extract(
                        "6.8",
                        output_dir=tmp_path,
                        verify=True,
                        release_tag="v0.3.0",
                    )

        assert "not found" in str(exc_info.value).lower()

    def test_network_error_handling(self, tmp_path: Path) -> None:
        """Network errors should raise DownloadError."""
        def mock_urlopen(url: str, timeout: int | None = None) -> None:
            raise urllib.error.URLError("Connection refused")

        with mock.patch("qtpilot.download.sys.platform", "win32"):
            with mock.patch("qtpilot.download.urllib.request.urlopen", mock_urlopen):
                with pytest.raises(DownloadError) as exc_info:
                    download_and_extract(
                        "6.8",
                        output_dir=tmp_path,
                        verify=True,
                        release_tag="v0.3.0",
                    )

        assert "network error" in str(exc_info.value).lower()


class TestAvailableVersions:
    """Tests for available versions constant."""

    def test_expected_versions_available(self) -> None:
        """All expected Qt versions should be available."""
        assert "5.15" in AVAILABLE_VERSIONS
        assert "5.15-patched" in AVAILABLE_VERSIONS
        assert "6.5" in AVAILABLE_VERSIONS
        assert "6.8" in AVAILABLE_VERSIONS
        assert "6.9" in AVAILABLE_VERSIONS

    def test_versions_is_frozen(self) -> None:
        """AVAILABLE_VERSIONS should be immutable."""
        assert isinstance(AVAILABLE_VERSIONS, frozenset)


class TestGetTestappPath:
    def test_returns_none_when_not_found(self, tmp_path):
        """Returns None when test app doesn't exist."""
        from qtpilot.download import get_testapp_path
        assert get_testapp_path(output_dir=tmp_path, platform_name="windows") is None

    def test_finds_windows_testapp(self, tmp_path):
        """Finds qtPilot-test-app.exe in testapp/ subdirectory."""
        from qtpilot.download import get_testapp_path
        testapp_dir = tmp_path / "testapp"
        testapp_dir.mkdir()
        exe = testapp_dir / "qtPilot-test-app.exe"
        exe.touch()
        result = get_testapp_path(output_dir=tmp_path, platform_name="windows")
        assert result == exe

    def test_finds_linux_testapp(self, tmp_path):
        """Finds qtPilot-test-app.sh wrapper in testapp/ subdirectory."""
        from qtpilot.download import get_testapp_path
        testapp_dir = tmp_path / "testapp"
        testapp_dir.mkdir()
        wrapper = testapp_dir / "qtPilot-test-app.sh"
        wrapper.touch()
        result = get_testapp_path(output_dir=tmp_path, platform_name="linux")
        assert result == wrapper


class TestArchitectureConstants:
    """Tests for architecture-related constants."""

    def test_default_arch_is_x64(self) -> None:
        """Default architecture should be x64."""
        assert DEFAULT_ARCH == "x64"

    def test_windows_architectures(self) -> None:
        """Windows should support x64 and x86."""
        assert "x64" in WINDOWS_ARCHITECTURES
        assert "x86" in WINDOWS_ARCHITECTURES

    def test_linux_architectures(self) -> None:
        """Linux should support x64 and x86."""
        assert "x64" in LINUX_ARCHITECTURES
        assert "x86" in LINUX_ARCHITECTURES

    def test_architecture_sets_are_frozen(self) -> None:
        """Architecture sets should be immutable."""
        assert isinstance(WINDOWS_ARCHITECTURES, frozenset)
        assert isinstance(LINUX_ARCHITECTURES, frozenset)
