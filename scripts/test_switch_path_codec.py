#!/usr/bin/env python3
"""Contract tests for the lossless Horizon component codec."""

from __future__ import annotations

import pathlib
import subprocess
import sys
import tempfile


def invoke(probe: str, operation: str, path: pathlib.Path | str,
           succeeds: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run([probe, operation, str(path)], text=True,
                            capture_output=True)
    if (result.returncode == 0) != succeeds:
        raise AssertionError(
            f"{operation} returned {result.returncode}: "
            f"{result.stdout}{result.stderr}")
    return result


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_switch_path_codec.py probe")
    probe = sys.argv[1]
    long_name = "long-" + "x" * 60 + ".imag"
    assert len(long_name.encode()) == 70
    reserved = "~pg-user-visible"
    unicode_name = "長い名前" * 9 + ".imag"
    assert len(unicode_name.encode("utf-8")) > 64

    with tempfile.TemporaryDirectory(prefix="playground-switch-path-") as temp:
        root = pathlib.Path(temp)
        logical_parent = root / "assets/image/gui/idol-skill"
        logical_long = logical_parent / long_name
        physical_long = pathlib.Path(
            invoke(probe, "encode", logical_long).stdout.strip())
        assert physical_long != logical_long
        assert all(len(part.encode()) <= 64 for part in physical_long.parts)
        physical_long.parent.mkdir(parents=True)
        physical_long.write_bytes(b"long")

        logical_short = logical_parent / "short.imag"
        physical_short = pathlib.Path(
            invoke(probe, "encode", logical_short).stdout.strip())
        assert physical_short == logical_short
        physical_short.write_bytes(b"short")

        logical_reserved = logical_parent / reserved
        physical_reserved = pathlib.Path(
            invoke(probe, "encode", logical_reserved).stdout.strip())
        assert physical_reserved != logical_reserved
        physical_reserved.parent.mkdir(parents=True, exist_ok=True)
        physical_reserved.write_bytes(b"reserved")

        logical_unicode = logical_parent / unicode_name
        physical_unicode = pathlib.Path(
            invoke(probe, "encode", logical_unicode).stdout.strip())
        assert physical_unicode != logical_unicode
        assert all(len(part.encode()) <= 64 for part in physical_unicode.parts)
        physical_unicode.parent.mkdir(parents=True, exist_ok=True)
        physical_unicode.write_bytes(b"unicode")

        physical_parent = pathlib.Path(
            invoke(probe, "encode", logical_parent).stdout.strip())
        listed = invoke(probe, "list", physical_parent).stdout.splitlines()
        assert listed == sorted(
            [long_name, reserved, "short.imag", unicode_name])

        # Different names with a 45-byte common prefix share their first
        # continuation directory without colliding.
        prefix = "x" * 60
        siblings = [prefix + "-alpha", prefix + "-beta"]
        for name in siblings:
            physical = pathlib.Path(
                invoke(probe, "encode", logical_parent / name).stdout.strip())
            physical.parent.mkdir(parents=True, exist_ok=True)
            physical.write_bytes(name.encode())
        listed = invoke(probe, "list", physical_parent).stdout.splitlines()
        assert listed == sorted(
            [long_name, reserved, "short.imag", unicode_name, *siblings])

        # Reserved physical names that are not canonical codec output are
        # corruption, not application-visible filenames.
        (physical_parent / "~pg-unknown").write_bytes(b"bad")
        invoke(probe, "list", physical_parent, succeeds=False)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
