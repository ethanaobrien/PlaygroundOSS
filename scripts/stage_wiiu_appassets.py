#!/usr/bin/env python3
"""Safely expand AppAssets.zip into Wii U read-only title content."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path, PurePosixPath
import shutil
import stat
import zipfile


def safe_name(raw: str) -> PurePosixPath:
    name = raw.replace("\\", "/").rstrip("/")
    path = PurePosixPath(name)
    if not name or name.startswith("/") or ":" in name or any(
        part in ("", ".", "..") or part.endswith((" ", ".")) or
        any(ord(character) < 0x20 or ord(character) == 0x7F
            for character in part)
        for part in path.parts
    ):
        raise ValueError(f"unsafe archive path: {raw!r}")
    return path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("archive", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("stamp", type=Path)
    args = parser.parse_args()

    temporary = args.output.with_name(args.output.name + ".staging")
    # Older Wii U builds staged the original ZIP beside the runtime files.
    # Remove it explicitly so an incremental build cannot package both forms.
    (args.output.parent / "AppAssets.zip").unlink(missing_ok=True)
    shutil.rmtree(temporary, ignore_errors=True)
    temporary.mkdir(parents=True)
    names: set[str] = set()
    portable_names: set[str] = set()
    try:
        with zipfile.ZipFile(args.archive) as archive:
            for entry in archive.infolist():
                path = safe_name(entry.filename)
                key = path.as_posix()
                if key in names:
                    raise ValueError(f"duplicate archive path: {entry.filename!r}")
                names.add(key)
                portable_key = key.casefold()
                if portable_key in portable_names:
                    raise ValueError(
                        f"case-colliding archive path: {entry.filename!r}")
                portable_names.add(portable_key)
                if stat.S_ISLNK(entry.external_attr >> 16):
                    raise ValueError(f"symbolic link in archive: {entry.filename!r}")
                destination = temporary.joinpath(*path.parts)
                if entry.is_dir():
                    destination.mkdir(parents=True, exist_ok=True)
                    continue
                destination.parent.mkdir(parents=True, exist_ok=True)
                with archive.open(entry) as source, destination.open("wb") as output:
                    shutil.copyfileobj(source, output, 1024 * 1024)
        if "start.lua" not in names:
            raise ValueError("required AppAssets entry missing: start.lua")
        shutil.rmtree(args.output, ignore_errors=True)
        temporary.replace(args.output)
    except BaseException:
        shutil.rmtree(temporary, ignore_errors=True)
        raise

    digest = hashlib.sha256(args.archive.read_bytes()).hexdigest()
    args.stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp_temporary = args.stamp.with_suffix(args.stamp.suffix + ".tmp")
    stamp_temporary.write_text(
        f"sha256={digest}\nentry_count={len(names)}\n", encoding="utf-8")
    stamp_temporary.replace(args.stamp)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
