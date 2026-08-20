#!/usr/bin/env python3
"""Invoke hacBrewPack on an audited staging tree and user-owned keyset."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import shutil
import struct
import subprocess


STAGE_FILES = {
    "exefs/main",
    "exefs/main.npdm",
    "romfs/AppAssets.zip",
    "romfs/AppAssets.metadata",
    "control/control.nacp",
    "control/icon_AmericanEnglish.dat",
}


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def verify_stage(stage: pathlib.Path) -> str:
    manifest_path = stage / "package-manifest.json"
    if not manifest_path.is_file() or manifest_path.is_symlink():
        raise SystemExit("package stage has no regular manifest")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("format") != 1:
        raise SystemExit("unsupported package stage manifest format")
    try:
        title_id = int(manifest.get("title_id", ""), 0)
    except (TypeError, ValueError) as error:
        raise SystemExit("package stage has an invalid title ID") from error
    if title_id <= 0 or title_id > 0xFFFFFFFFFFFFFFFF:
        raise SystemExit("package stage title ID is outside the u64 range")
    declared: dict[str, dict[str, object]] = {}
    for record in manifest.get("files", []):
        if not isinstance(record, dict) or not isinstance(record.get("path"), str):
            raise SystemExit("package stage manifest contains an invalid record")
        relative = pathlib.PurePosixPath(record["path"])
        if relative.is_absolute() or ".." in relative.parts or not relative.parts:
            raise SystemExit("package stage manifest contains an unsafe path")
        if record["path"] in declared:
            raise SystemExit("package stage manifest contains a duplicate path")
        if not isinstance(record.get("size"), int) or record["size"] < 0:
            raise SystemExit("package stage manifest contains an invalid size")
        if (not isinstance(record.get("sha256"), str) or
                len(record["sha256"]) != 64):
            raise SystemExit("package stage manifest contains an invalid digest")
        declared[record["path"]] = record

    for path in stage.rglob("*"):
        if path.is_symlink():
            raise SystemExit(f"package stage contains a symbolic link: {path}")
    actual = {
        path.relative_to(stage).as_posix()
        for path in stage.rglob("*")
        if path.is_file() and path != manifest_path
    }
    if actual != STAGE_FILES:
        raise SystemExit(
            f"package stage has an invalid production file set: "
            f"missing={sorted(STAGE_FILES - actual)}, "
            f"unexpected={sorted(actual - STAGE_FILES)}"
        )
    if set(declared) != actual:
        raise SystemExit(
            f"package stage file set differs from manifest: "
            f"missing={sorted(set(declared) - actual)}, "
            f"unexpected={sorted(actual - set(declared))}"
        )
    for relative, record in declared.items():
        path = stage / relative
        if path.stat().st_size != record["size"] or sha256(path) != record["sha256"]:
            raise SystemExit(f"package stage input differs from manifest: {relative}")
    return f"{title_id:016x}.nsp"


def verify_pfs0_package(path: pathlib.Path) -> None:
    with path.open("rb") as package:
        header = package.read(16)
        if len(header) != 16:
            raise SystemExit("hacBrewPack produced a truncated NSP header")
        magic, count, string_size, reserved = struct.unpack("<4sIII", header)
        if magic != b"PFS0" or reserved != 0 or count != 3 or string_size > 4096:
            raise SystemExit("hacBrewPack produced an invalid NSP PFS0 header")
        raw_entries = package.read(count * 24)
        strings = package.read(string_size)
        if len(raw_entries) != count * 24 or len(strings) != string_size:
            raise SystemExit("hacBrewPack produced a truncated NSP directory")
        data_offset = 16 + count * 24 + string_size
        package_size = path.stat().st_size
        names: list[str] = []
        extents: list[tuple[int, int]] = []
        for index in range(count):
            offset, size, name_offset, entry_reserved = struct.unpack_from(
                "<QQII", raw_entries, index * 24
            )
            if entry_reserved != 0 or name_offset >= len(strings):
                raise SystemExit("hacBrewPack produced an invalid NSP entry")
            terminator = strings.find(b"\0", name_offset)
            if terminator < 0:
                raise SystemExit("hacBrewPack produced an unterminated NSP name")
            try:
                name = strings[name_offset:terminator].decode("ascii")
            except UnicodeDecodeError as error:
                raise SystemExit("hacBrewPack produced a non-ASCII NSP name") from error
            names.append(name)
            begin = data_offset + offset
            end = begin + size
            if begin < data_offset or end < begin or end > package_size:
                raise SystemExit("hacBrewPack produced an out-of-range NSP entry")
            extents.append((begin, end))
        if len(set(names)) != 3 or sum(name.endswith(".cnmt.nca") for name in names) != 1:
            raise SystemExit("hacBrewPack NSP does not contain one metadata NCA")
        if any(
            len(name.removesuffix(".cnmt.nca").removesuffix(".nca")) != 32
            or any(character not in "0123456789abcdef" for character in
                   name.removesuffix(".cnmt.nca").removesuffix(".nca"))
            or not name.endswith(".nca")
            for name in names
        ):
            raise SystemExit("hacBrewPack NSP contains an invalid NCA filename")
        ordered = sorted(extents)
        if ordered[0][0] != data_offset or any(
            ordered[index][1] != ordered[index + 1][0]
            for index in range(len(ordered) - 1)
        ) or ordered[-1][1] != package_size:
            raise SystemExit("hacBrewPack NSP payload extents are not contiguous")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--stage", type=pathlib.Path, required=True)
    parser.add_argument("--keys", type=pathlib.Path, required=True)
    parser.add_argument("--hacbrewpack", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    stage = args.stage.resolve()
    for relative in (*sorted(STAGE_FILES), "package-manifest.json"):
        if not (stage / relative).is_file():
            raise SystemExit(f"incomplete package stage: missing {relative}")
    expected_package_name = verify_stage(stage)
    if not args.keys.is_file():
        raise SystemExit("--keys must name a user-owned keyset file")
    if not args.hacbrewpack.is_file():
        raise SystemExit("--hacbrewpack must name the packer executable")
    if not os.access(args.hacbrewpack, os.X_OK):
        raise SystemExit("--hacbrewpack must be executable")
    output = args.output.resolve()
    work = output / ".pack-work"
    if work.exists():
        shutil.rmtree(work)
    output.mkdir(parents=True, exist_ok=True)
    work.mkdir()
    package_output = work / "output"
    package_output.mkdir()
    package_input = work / "input"
    for directory in ("exefs", "romfs", "control"):
        shutil.copytree(stage / directory, package_input / directory)
    command = [
        str(args.hacbrewpack), "--keyset", str(args.keys.resolve()),
        "--exefsdir", str(package_input / "exefs"),
        "--romfsdir", str(package_input / "romfs"),
        "--controldir", str(package_input / "control"), "--nologo",
        "--nspdir", str(package_output), "--tempdir", str(work / "temp"),
        "--ncadir", str(work / "nca"),
    ]
    # Older hacBrewPack releases echo the full value of every unrecognized
    # keyset entry.  Capture their output so a normal CI/terminal transcript
    # can never disclose user-owned key material.
    packed = subprocess.run(
        command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT
    )
    if packed.returncode != 0:
        raise SystemExit(
            f"hacBrewPack failed with exit status {packed.returncode}; "
            "its output was suppressed because some releases print key values"
        )
    # The packer edits control.nacp's logo-handling byte. It received only the
    # disposable input copy, so the audited stage must still verify exactly.
    verify_stage(stage)
    packages = list(package_output.glob("*.nsp"))
    if len(packages) != 1 or packages[0].name.lower() != expected_package_name:
        raise SystemExit("hacBrewPack did not produce exactly one PFS0 NSP")
    verify_pfs0_package(packages[0])
    destination = output / packages[0].name
    temporary = destination.with_suffix(destination.suffix + ".tmp")
    shutil.copyfile(packages[0], temporary)
    os.replace(temporary, destination)
    for stale in output.glob("*.nsp"):
        if stale != destination:
            stale.unlink()
    shutil.rmtree(work)
    print(destination)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
