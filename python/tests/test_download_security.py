"""Security-hardening tests for the downloader (red/green for review findings).

Covers:
  * zip traversal check missing backslash / drive-letter members
  * no size cap on download or uncompressed extraction (zip/tar bomb)
  * checksum entries not validated as 64-hex
  * tar extraction crashing on Python 3.11.0-3.11.3 (no `filter=` kwarg)

stdlib-only:  pytest python/tests/test_download_security.py --noconftest
"""

from __future__ import annotations

import io
import tarfile
import zipfile
from pathlib import Path
from unittest import mock

import pytest

from qtpilot.download import (
    DownloadError,
    download_file,
    extract_archive,
    parse_checksums,
)


def _make_zip(path: Path, members: list[tuple[str, bytes]]) -> None:
    with zipfile.ZipFile(path, "w") as zf:
        for name, data in members:
            zf.writestr(name, data)


def _make_targz(path: Path, members: list[tuple[str, bytes]]) -> None:
    with tarfile.open(path, "w:gz") as tf:
        for name, data in members:
            info = tarfile.TarInfo(name)
            info.size = len(data)
            tf.addfile(info, io.BytesIO(data))


class TestZipTraversalHardening:
    def test_backslash_traversal_rejected(self, tmp_path: Path) -> None:
        archive = tmp_path / "evil.zip"
        _make_zip(archive, [("..\\..\\evil.txt", b"x")])
        with pytest.raises(DownloadError):
            extract_archive(archive, tmp_path / "out")

    def test_drive_absolute_rejected(self, tmp_path: Path) -> None:
        archive = tmp_path / "evil2.zip"
        _make_zip(archive, [("C:\\Windows\\evil.txt", b"x")])
        with pytest.raises(DownloadError):
            extract_archive(archive, tmp_path / "out")


class TestExtractionSizeCap:
    def test_zip_uncompressed_cap(self, tmp_path: Path) -> None:
        archive = tmp_path / "big.zip"
        _make_zip(archive, [("probe.so", b"\0" * 5000)])
        with pytest.raises(DownloadError):
            extract_archive(archive, tmp_path / "out", max_uncompressed=1000)

    def test_tar_uncompressed_cap(self, tmp_path: Path) -> None:
        archive = tmp_path / "big.tar.gz"
        _make_targz(archive, [("probe.so", b"\0" * 5000)])
        with pytest.raises(DownloadError):
            extract_archive(archive, tmp_path / "out", max_uncompressed=1000)

    def test_within_cap_ok(self, tmp_path: Path) -> None:
        archive = tmp_path / "ok.tar.gz"
        _make_targz(archive, [("probe.so", b"\0" * 500)])
        extract_archive(archive, tmp_path / "out", max_uncompressed=1000)
        assert (tmp_path / "out" / "probe.so").exists()


class TestDownloadSizeCap:
    def test_download_aborts_over_cap(self, tmp_path: Path) -> None:
        class FakeResp:
            def __init__(self) -> None:
                self._chunks = [b"x" * 4096, b"x" * 4096, b"x" * 4096]

            def read(self, n: int = -1) -> bytes:
                return self._chunks.pop(0) if self._chunks else b""

            def __enter__(self):  # noqa: ANN001
                return self

            def __exit__(self, *a):  # noqa: ANN001
                return False

        with mock.patch("qtpilot.download.urllib.request.urlopen", return_value=FakeResp()):
            with pytest.raises(DownloadError):
                download_file("https://example/x", tmp_path / "out.bin", max_bytes=5000)


class TestChecksumHexValidation:
    def test_non_hex_entry_skipped(self) -> None:
        good = "a" * 64
        content = f"not-a-valid-hash  bad.zip\n{good}  good.zip\n"
        result = parse_checksums(content)
        assert result.get("good.zip") == good
        assert "bad.zip" not in result

    def test_wrong_length_skipped(self) -> None:
        content = f"{'a' * 63}  short.zip\n"
        assert "short.zip" not in parse_checksums(content)


class TestTarFilterFallback:
    """Python 3.11.0-3.11.3 lack the `filter=` kwarg; extraction must not crash."""

    def test_extract_without_filter_kwarg(self, tmp_path: Path, monkeypatch) -> None:  # noqa: ANN001
        archive = tmp_path / "a.tar.gz"
        _make_targz(archive, [("probe.so", b"payload")])

        real_extract = tarfile.TarFile.extract

        def legacy_extract(self, member, path="", set_attrs=True, *, numeric_owner=False):  # noqa: ANN001
            # Mimic the pre-3.11.4 signature: passing filter= raises TypeError.
            return real_extract(
                self, member, path, set_attrs=set_attrs, numeric_owner=numeric_owner
            )

        monkeypatch.setattr(tarfile.TarFile, "extract", legacy_extract)
        out = tmp_path / "out"
        extract_archive(archive, out)
        assert (out / "probe.so").read_bytes() == b"payload"
