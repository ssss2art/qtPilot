"""Download qtPilot tool archives from GitHub Releases.

This module provides utilities to download the correct probe + launcher
archive for your Qt version and platform.
"""

from __future__ import annotations

import hashlib
import sys
import tarfile
import urllib.error
import urllib.request
import zipfile
from importlib.metadata import version as _pkg_version
from pathlib import Path

# GitHub repository for qtPilot releases
GITHUB_REPO = "ssss2art/qtPilot"
RELEASES_URL = f"https://github.com/{GITHUB_REPO}/releases/download"


def _default_release_tag() -> str:
    """Derive the default release tag from the installed package version."""
    v = _pkg_version("qtpilot")
    # Strip dev/post/local suffixes for a clean release tag
    base = v.split(".dev")[0].split("+")[0].split(".post")[0]
    return f"v{base}"

# Available Qt versions (release workflow builds these)
AVAILABLE_VERSIONS = frozenset([
    "5.15",
    "5.15-patched",
    "6.5",
    "6.8",
    "6.9",
])

# Supported architectures per platform
WINDOWS_ARCHITECTURES = frozenset(["x64", "x86"])
LINUX_ARCHITECTURES = frozenset(["x64", "x86"])
DEFAULT_ARCH = "x64"

# Platform mapping: sys.platform -> (platform_name, archive_ext, lib_ext)
PLATFORM_MAP: dict[str, tuple[str, str, str]] = {
    "linux": ("linux", "tar.gz", "so"),
    "win32": ("windows", "zip", "dll"),
}


class DownloadError(Exception):
    """Raised when download fails."""


class ChecksumError(DownloadError):
    """Raised when checksum verification fails."""


class UnsupportedPlatformError(DownloadError):
    """Raised when the current platform is not supported."""


class VersionNotFoundError(DownloadError):
    """Raised when the requested Qt version is not available."""


def detect_platform() -> str:
    """Detect the current platform name.

    Returns:
        Platform name string: "linux" or "windows"

    Raises:
        UnsupportedPlatformError: If the current platform is not supported.
    """
    platform = sys.platform
    if platform.startswith("linux"):
        platform = "linux"

    if platform not in PLATFORM_MAP:
        supported = ", ".join(PLATFORM_MAP.keys())
        raise UnsupportedPlatformError(
            f"Unsupported platform: {platform}. Supported platforms: {supported}"
        )

    return PLATFORM_MAP[platform][0]


def get_probe_filename(platform_name: str | None = None) -> str:
    """Get the simplified probe filename for the current platform.

    Returns:
        Filename like "qtPilot-probe.dll" or "qtPilot-probe.so"
    """
    if platform_name is None:
        platform_name = detect_platform()
    ext = "dll" if platform_name == "windows" else "so"
    return f"qtPilot-probe.{ext}"


def get_launcher_filename(platform_name: str | None = None) -> str:
    """Get the simplified launcher filename for the current platform.

    Returns:
        Filename like "qtPilot-launcher.exe" or "qtPilot-launcher"
    """
    if platform_name is None:
        platform_name = detect_platform()
    if platform_name == "windows":
        return "qtPilot-launcher.exe"
    return "qtPilot-launcher"


def normalize_version(qt_version: str) -> str:
    """Normalize Qt version string.

    Args:
        qt_version: Version like "6.8", "6.8.0", "5.15-patched"

    Returns:
        Normalized version like "6.8" or "5.15-patched"
    """
    # Handle patched suffix
    patched = qt_version.endswith("-patched")
    if patched:
        qt_version = qt_version[:-8]  # Remove "-patched"

    # Split and normalize (e.g., "6.8.0" -> "6.8")
    parts = qt_version.split(".")
    if len(parts) >= 2:
        normalized = f"{parts[0]}.{parts[1]}"
    else:
        normalized = qt_version

    # Re-add patched suffix if present
    if patched:
        normalized = f"{normalized}-patched"

    return normalized


def get_archive_filename(
    qt_version: str, platform_name: str | None = None, arch: str | None = None,
) -> str:
    """Get the archive filename for a Qt version, platform, and architecture.

    Args:
        qt_version: Qt version (e.g., "6.8", "5.15-patched")
        platform_name: Platform name (auto-detected if None)
        arch: Target architecture ("x64" or "x86"). Defaults to "x64".

    Returns:
        Archive filename like "qtpilot-qt6.8-windows-x64.zip" or
        "qtpilot-qt5.15-linux.tar.gz"

    Windows archives always include the arch suffix (-x64 or -x86).
    Linux x64 archives have no arch suffix (backward compatibility).
    Linux x86 archives include the -x86 suffix.
    """
    version = normalize_version(qt_version)
    if platform_name is None:
        platform_name = detect_platform()
    if arch is None:
        arch = DEFAULT_ARCH
    ext = "zip" if platform_name == "windows" else "tar.gz"

    # Windows: always include arch suffix
    if platform_name == "windows":
        return f"qtpilot-qt{version}-{platform_name}-{arch}.{ext}"

    # Linux: only include arch suffix for x86 (backward compat for x64)
    if arch == "x86":
        return f"qtpilot-qt{version}-{platform_name}-x86.{ext}"
    return f"qtpilot-qt{version}-{platform_name}.{ext}"


def build_archive_url(
    qt_version: str,
    release_tag: str = "latest",
    platform_name: str | None = None,
    arch: str | None = None,
) -> str:
    """Build the URL for a release archive.

    Args:
        qt_version: Qt version (e.g., "6.8", "5.15-patched")
        release_tag: Release tag (e.g., "v0.3.0") or "latest"
        platform_name: Platform name (auto-detected if None)
        arch: Target architecture ("x64" or "x86"). Defaults to "x64".

    Returns:
        URL to the archive file

    Raises:
        UnsupportedPlatformError: If platform detection fails
        VersionNotFoundError: If the Qt version is not available
    """
    version = normalize_version(qt_version)

    if version not in AVAILABLE_VERSIONS:
        available = ", ".join(sorted(AVAILABLE_VERSIONS))
        raise VersionNotFoundError(
            f"Qt version '{version}' not available. Available versions: {available}"
        )

    filename = get_archive_filename(version, platform_name, arch)

    if release_tag == "latest":
        release_tag = _default_release_tag()

    return f"{RELEASES_URL}/{release_tag}/{filename}"


def build_checksums_url(release_tag: str = "latest") -> str:
    """Build the URL for the SHA256SUMS file.

    Args:
        release_tag: Release tag (e.g., "v1.0.0") or "latest"

    Returns:
        URL to the SHA256SUMS file
    """
    if release_tag == "latest":
        release_tag = _default_release_tag()
    return f"{RELEASES_URL}/{release_tag}/SHA256SUMS"


def parse_checksums(content: str) -> dict[str, str]:
    """Parse SHA256SUMS file content.

    Args:
        content: Content of SHA256SUMS file

    Returns:
        Dict mapping filename to SHA256 hash
    """
    checksums = {}
    for line in content.strip().split("\n"):
        line = line.strip()
        if not line:
            continue
        # Format: "hash  filename" or "hash *filename" (binary mode)
        parts = line.split()
        if len(parts) >= 2:
            hash_value = parts[0]
            filename = parts[-1].lstrip("*")
            checksums[filename] = hash_value
    return checksums


def verify_checksum(filepath: Path, expected_hash: str) -> bool:
    """Verify file SHA256 checksum.

    Args:
        filepath: Path to the file
        expected_hash: Expected SHA256 hash (hex string)

    Returns:
        True if checksum matches
    """
    sha256 = hashlib.sha256()
    with open(filepath, "rb") as f:
        for chunk in iter(lambda: f.read(8192), b""):
            sha256.update(chunk)
    return sha256.hexdigest().lower() == expected_hash.lower()


def download_file(url: str, output_path: Path) -> None:
    """Download a file from URL.

    Args:
        url: URL to download
        output_path: Local path to save file

    Raises:
        DownloadError: If download fails
    """
    try:
        # Create parent directories if needed
        output_path.parent.mkdir(parents=True, exist_ok=True)

        # Download with progress indication
        with urllib.request.urlopen(url, timeout=60) as response:
            with open(output_path, "wb") as f:
                while True:
                    chunk = response.read(8192)
                    if not chunk:
                        break
                    f.write(chunk)
    except urllib.error.HTTPError as e:
        if e.code == 404:
            raise DownloadError(f"File not found: {url}") from e
        raise DownloadError(f"HTTP error {e.code}: {url}") from e
    except urllib.error.URLError as e:
        raise DownloadError(f"Network error downloading {url}: {e.reason}") from e


def _validate_tar_member(member: tarfile.TarInfo) -> bool:
    """Validate a tar member name to prevent path traversal attacks.

    Returns:
        True if the member name is safe.
    """
    # Reject absolute paths
    if member.name.startswith("/") or member.name.startswith("\\"):
        return False
    # Reject path traversal
    if ".." in member.name.split("/"):
        return False
    if ".." in member.name.split("\\"):
        return False
    # Reject symlinks
    if member.issym() or member.islnk():
        return False
    return True


def extract_archive(archive_path: Path, output_dir: Path) -> list[Path]:
    """Extract a zip or tar.gz archive to a directory.

    Args:
        archive_path: Path to the archive file
        output_dir: Directory to extract into

    Returns:
        List of extracted file paths

    Raises:
        DownloadError: If extraction fails or archive contains unsafe paths
    """
    output_dir.mkdir(parents=True, exist_ok=True)
    extracted = []

    name = archive_path.name
    try:
        if name.endswith(".zip"):
            with zipfile.ZipFile(archive_path, "r") as zf:
                for info in zf.infolist():
                    # Skip directories
                    if info.is_dir():
                        continue
                    # Validate path safety
                    if ".." in info.filename.split("/") or info.filename.startswith("/"):
                        raise DownloadError(
                            f"Archive contains unsafe path: {info.filename}"
                        )
                    zf.extract(info, output_dir)
                    extracted.append(output_dir / info.filename)
        elif name.endswith(".tar.gz") or name.endswith(".tgz"):
            with tarfile.open(archive_path, "r:gz") as tf:
                for member in tf.getmembers():
                    if member.isdir():
                        continue
                    if not _validate_tar_member(member):
                        raise DownloadError(
                            f"Archive contains unsafe path: {member.name}"
                        )
                    tf.extract(member, output_dir, filter="data")
                    extracted.append(output_dir / member.name)
        else:
            raise DownloadError(f"Unsupported archive format: {name}")
    except (zipfile.BadZipFile, tarfile.TarError) as e:
        raise DownloadError(f"Failed to extract archive: {e}") from e

    return extracted


def download_and_extract(
    qt_version: str,
    output_dir: Path | str | None = None,
    verify: bool = True,
    release_tag: str = "latest",
    platform_name: str | None = None,
    arch: str | None = None,
) -> tuple[Path, Path]:
    """Download and extract the qtPilot tools archive for a Qt version.

    Downloads the archive (zip on Windows, tar.gz on Linux) containing
    the probe and launcher, verifies the checksum, and extracts them.

    Args:
        qt_version: Qt version (e.g., "6.8", "5.15", "5.15-patched")
        output_dir: Directory to extract into (default: current directory)
        verify: Whether to verify SHA256 checksum (default: True)
        release_tag: Release tag to download from (default: "latest")
        platform_name: Platform name (auto-detected if None)
        arch: Target architecture ("x64" or "x86"). Defaults to "x64".

    Returns:
        Tuple of (probe_path, launcher_path) pointing to extracted files

    Raises:
        DownloadError: If download or extraction fails
        ChecksumError: If checksum verification fails
        UnsupportedPlatformError: If current platform is not supported
        VersionNotFoundError: If Qt version is not available
    """
    version = normalize_version(qt_version)

    if platform_name is None:
        platform_name = detect_platform()

    # Build URL
    archive_url = build_archive_url(version, release_tag, platform_name, arch)
    archive_filename = get_archive_filename(version, platform_name, arch)

    # Determine output path
    if output_dir is None:
        output_dir = Path.cwd()
    else:
        output_dir = Path(output_dir)

    output_dir.mkdir(parents=True, exist_ok=True)
    archive_path = output_dir / archive_filename

    # Download checksums first if verification enabled
    expected_hash: str | None = None
    if verify:
        checksums_url = build_checksums_url(release_tag)
        try:
            with urllib.request.urlopen(checksums_url, timeout=30) as response:
                checksums_content = response.read().decode("utf-8")
            checksums = parse_checksums(checksums_content)
            expected_hash = checksums.get(archive_filename)
            if expected_hash is None:
                raise DownloadError(
                    f"Checksum not found for {archive_filename} in SHA256SUMS"
                )
        except urllib.error.HTTPError as e:
            if e.code == 404:
                raise DownloadError(
                    f"SHA256SUMS not found for release {release_tag}"
                ) from e
            raise DownloadError(
                f"Error downloading checksums: HTTP {e.code}"
            ) from e
        except urllib.error.URLError as e:
            raise DownloadError(
                f"Network error downloading checksums: {e.reason}"
            ) from e

    # Download archive
    download_file(archive_url, archive_path)

    # Verify checksum if enabled
    if verify and expected_hash:
        if not verify_checksum(archive_path, expected_hash):
            archive_path.unlink(missing_ok=True)
            raise ChecksumError(
                f"Checksum verification failed for {archive_filename}. "
                "File may be corrupted or tampered with."
            )

    # Extract archive
    try:
        extract_archive(archive_path, output_dir)
    finally:
        # Clean up the archive file after extraction
        archive_path.unlink(missing_ok=True)

    # Set executable permissions on Linux
    if platform_name == "linux":
        import stat

        for name in [get_probe_filename(platform_name), get_launcher_filename(platform_name)]:
            path = output_dir / name
            if path.exists():
                path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

    probe_path = output_dir / get_probe_filename(platform_name)
    launcher_path = output_dir / get_launcher_filename(platform_name)

    return probe_path, launcher_path
