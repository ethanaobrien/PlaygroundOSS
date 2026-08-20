#!/usr/bin/env python3
"""Fail-closed tests for the keyless Switch NSP packaging boundary."""

from __future__ import annotations

import hashlib
import json
import os
import pathlib
import struct
import subprocess
import sys
import tempfile


STAGE_FILES = (
    "exefs/main",
    "exefs/main.npdm",
    "romfs/AppAssets.zip",
    "romfs/AppAssets.metadata",
    "control/control.nacp",
    "control/icon_AmericanEnglish.dat",
)


def digest(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def make_stage(root: pathlib.Path) -> pathlib.Path:
    stage = root / "stage"
    for index, relative in enumerate(STAGE_FILES):
        path = stage / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(f"fixture-{index}-{relative}".encode())
    records = [
        {"path": relative, "size": (stage / relative).stat().st_size,
         "sha256": digest(stage / relative)}
        for relative in sorted(STAGE_FILES)
    ]
    (stage / "package-manifest.json").write_text(
        json.dumps({"format": 1, "title_id": "0x0000000000000001", "files": records},
                   indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return stage


def invoke(tool: pathlib.Path, stage: pathlib.Path, keyset: pathlib.Path,
           packer: pathlib.Path, output: pathlib.Path,
           expect_success: bool) -> None:
    result = subprocess.run(
        [sys.executable, str(tool), "--stage", str(stage), "--keys",
         str(keyset), "--hacbrewpack", str(packer), "--output", str(output)],
        text=True, capture_output=True,
    )
    if (result.returncode == 0) != expect_success:
        raise AssertionError(
            f"unexpected packaging result {result.returncode}:\n"
            f"stdout={result.stdout}\nstderr={result.stderr}"
        )


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_switch_package_tools.py package-script")
    tool = pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="playground-switch-package-") as raw:
        root = pathlib.Path(raw)
        keyset = root / "prod.keys"
        keyset.write_text("fixture only\n", encoding="utf-8")
        packer = root / "fake-hacbrewpack.py"
        packer.write_text(
            "#!/usr/bin/env python3\n"
            "import pathlib, struct, sys\n"
            "args = sys.argv[1:]\n"
            "valued = {'--keyset', '--exefsdir', '--romfsdir', "
            "'--controldir', '--nspdir', '--tempdir', '--ncadir'}\n"
            "index = 0\n"
            "while index < len(args):\n"
            "    if args[index] == '--nologo':\n"
            "        index += 1\n"
            "    elif args[index] in valued and index + 1 < len(args):\n"
            "        index += 2\n"
            "    else:\n"
            "        raise SystemExit('unsupported fake packer option: ' + "
            "args[index])\n"
            "out = pathlib.Path(args[args.index('--nspdir') + 1])\n"
            "out.mkdir(parents=True, exist_ok=True)\n"
            "control = pathlib.Path(args[args.index('--controldir') + 1]) / "
            "'control.nacp'\n"
            "control.write_bytes(b'packer-mutated-control')\n"
            "names = [b'0' * 32 + b'.nca\\0', b'1' * 32 + b'.cnmt.nca\\0', "
            "b'2' * 32 + b'.nca\\0']\n"
            "strings = b''.join(names)\n"
            "strings += b'\\0' * ((32 - len(strings) % 32) % 32)\n"
            "payloads = [b'A', b'BC', b'DEF']\n"
            "entries = bytearray(); offset = 0; name_offset = 0\n"
            "for name, payload in zip(names, payloads):\n"
            "    entries += struct.pack('<QQII', offset, len(payload), "
            "name_offset, 0)\n"
            "    offset += len(payload); name_offset += len(name)\n"
            "package = struct.pack('<4sIII', b'PFS0', 3, len(strings), 0) + "
            "entries + strings + b''.join(payloads)\n"
            "(out / '0000000000000001.nsp').write_bytes(package)\n",
            encoding="utf-8",
        )
        packer.chmod(0o700)

        stage = make_stage(root / "valid")
        output = root / "output"
        output.mkdir()
        (output / "stale.nsp").write_bytes(b"PFS0stale")
        invoke(tool, stage, keyset, packer, output, True)
        if (stage / "control/control.nacp").read_bytes() != \
                b"fixture-4-control/control.nacp":
            raise AssertionError("packer mutated the audited package stage")
        if not (output / "0000000000000001.nsp").is_file():
            raise AssertionError("valid package was not atomically published")
        if any(path.name == ".pack-work" for path in output.iterdir()):
            raise AssertionError("successful package left temporary work")
        if sorted(path.name for path in output.glob("*.nsp")) != [
                "0000000000000001.nsp"]:
            raise AssertionError("successful package retained a stale NSP")

        stage = make_stage(root / "tampered")
        (stage / "romfs/AppAssets.zip").write_bytes(b"tampered")
        invoke(tool, stage, keyset, packer, root / "tampered-output", False)

        stage = make_stage(root / "extra")
        (stage / "romfs/secret.keys").write_text("must not package\n")
        records = json.loads((stage / "package-manifest.json").read_text())
        secret = stage / "romfs/secret.keys"
        records["files"].append(
            {"path": "romfs/secret.keys", "size": secret.stat().st_size,
             "sha256": digest(secret)}
        )
        (stage / "package-manifest.json").write_text(
            json.dumps(records, indent=2, sort_keys=True) + "\n"
        )
        invoke(tool, stage, keyset, packer, root / "extra-output", False)

        if hasattr(os, "symlink"):
            stage = make_stage(root / "symlink")
            (stage / "exefs/main").unlink()
            try:
                os.symlink(keyset, stage / "exefs/main")
            except OSError:
                pass  # Windows runners may not grant symlink privileges.
            else:
                invoke(tool, stage, keyset, packer,
                       root / "symlink-output", False)

    print("switch-package-tools test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
