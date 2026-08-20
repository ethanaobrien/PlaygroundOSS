#!/usr/bin/env python3
"""Generate deterministic integrity metadata for an unchanged AppAssets.zip."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path, PurePosixPath
import stat
import zipfile


def safe_name(raw: str) -> PurePosixPath:
    name = raw.replace("\\", "/")
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
    parser.add_argument("--required", action="append", default=["start.lua"])
    args = parser.parse_args()

    digest = hashlib.sha256()
    size = 0
    with args.archive.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            size += len(chunk)
            digest.update(chunk)

    names: set[str] = set()
    portable_names: set[str] = set()
    expanded = 0
    count = 0
    with zipfile.ZipFile(args.archive) as archive:
        for entry in archive.infolist():
            path = safe_name(entry.filename.rstrip("/"))
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
            expanded += entry.file_size
            count += 1
        required = [safe_name(value).as_posix() for value in args.required]
        missing = [value for value in required if value not in names]
        if missing:
            raise ValueError("required AppAssets entries missing: " + ", ".join(missing))

    text = (
        "format=2\n"
        f"sha256={digest.hexdigest()}\n"
        f"size={size}\n"
        f"expanded_size={expanded}\n"
        f"entry_count={count}\n"
        + "".join(f"required={value}\n" for value in required)
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix(args.output.suffix + ".tmp")
    temporary.write_text(text, encoding="utf-8", newline="\n")
    temporary.replace(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
