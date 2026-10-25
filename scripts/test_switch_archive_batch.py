#!/usr/bin/env python3
"""Run real engine extraction against the host Horizon-commit model."""
import pathlib
import subprocess
import sys
import struct
import tempfile
import zipfile

with tempfile.TemporaryDirectory(prefix="pg-archive-batch-") as directory:
    root = pathlib.Path(directory)
    archive = root / "fixture.zip"
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as output:
        for index in range(256):
            output.writestr(f"fixture-{index}", b"x" * 8193)
    damaged = bytearray(archive.read_bytes())
    central = damaged.index(b"PK\x01\x02")
    crc = struct.unpack_from("<I", damaged, central + 16)[0]
    struct.pack_into("<I", damaged, central + 16, crc ^ 1)
    corrupt = root / "corrupt.zip"
    corrupt.write_bytes(damaged)
    subprocess.run([sys.argv[1], str(root / "scratch"), str(archive), str(corrupt)], check=True)
