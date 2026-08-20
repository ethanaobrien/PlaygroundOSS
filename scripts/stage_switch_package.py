#!/usr/bin/env python3
"""Create deterministic hacBrewPack inputs without handling console keys."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import shutil
import struct
import subprocess


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def jpeg_dimensions(path: pathlib.Path) -> tuple[int, int]:
    data = path.read_bytes()
    if not data.startswith(b"\xff\xd8") or not data.endswith(b"\xff\xd9"):
        raise ValueError("Switch icon must be a complete JPEG")
    offset = 2
    while offset + 4 <= len(data):
        if data[offset] != 0xFF:
            raise ValueError("invalid JPEG marker stream")
        marker = data[offset + 1]
        offset += 2
        if marker in {0xD8, 0xD9}:
            continue
        length = struct.unpack_from(">H", data, offset)[0]
        if length < 2 or offset + length > len(data):
            raise ValueError("invalid JPEG segment size")
        if marker in {0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7,
                      0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF}:
            height, width = struct.unpack_from(">HH", data, offset + 3)
            return width, height
        offset += length
    raise ValueError("JPEG has no image dimensions")


def copy(source: pathlib.Path, destination: pathlib.Path) -> None:
    if not source.is_file():
        raise ValueError(f"required package input is missing: {source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--elf", type=pathlib.Path, required=True)
    parser.add_argument("--npdm", type=pathlib.Path, required=True)
    parser.add_argument("--nacp", type=pathlib.Path, required=True)
    parser.add_argument("--archive", type=pathlib.Path, required=True)
    parser.add_argument("--metadata", type=pathlib.Path, required=True)
    parser.add_argument("--icon", type=pathlib.Path, required=True)
    parser.add_argument("--elf2nso", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--title-id", required=True)
    args = parser.parse_args()

    if jpeg_dimensions(args.icon) != (256, 256):
        raise ValueError("Switch icon must be exactly 256x256 pixels")
    output = args.output.resolve()
    if output.exists():
        shutil.rmtree(output)
    (output / "exefs").mkdir(parents=True)
    (output / "romfs").mkdir()
    (output / "control").mkdir()
    subprocess.run([str(args.elf2nso), str(args.elf),
                    str(output / "exefs/main")], check=True)
    copy(args.npdm, output / "exefs/main.npdm")
    copy(args.nacp, output / "control/control.nacp")
    copy(args.icon, output / "control/icon_AmericanEnglish.dat")
    copy(args.archive, output / "romfs/AppAssets.zip")
    copy(args.metadata, output / "romfs/AppAssets.metadata")

    files = sorted(path for path in output.rglob("*") if path.is_file())
    manifest = {
        "format": 1,
        "title_id": args.title_id,
        "files": [
            {"path": path.relative_to(output).as_posix(),
             "size": path.stat().st_size, "sha256": sha256(path)}
            for path in files
        ],
    }
    (output / "package-manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
