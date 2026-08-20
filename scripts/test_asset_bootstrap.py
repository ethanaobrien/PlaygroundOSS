#!/usr/bin/env python3
"""End-to-end tests for the platform-neutral AppAssets bootstrap."""

from __future__ import annotations

import hashlib
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile


def metadata_for(archive: pathlib.Path, entries: dict[str, bytes]) -> str:
    payload = archive.read_bytes()
    return "".join(
        [
            "format=2\n",
            f"sha256={hashlib.sha256(payload).hexdigest()}\n",
            f"size={len(payload)}\n",
            f"expanded_size={sum(map(len, entries.values()))}\n",
            f"entry_count={len(entries)}\n",
            "required=start.lua\n",
            "required=m_boot/start.lua\n",
        ]
    )


def write_zip(path: pathlib.Path, entries: dict[str, bytes]) -> None:
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as output:
        for name, value in entries.items():
            output.writestr(name, value)


def corrupt_first_payload(path: pathlib.Path) -> None:
    data = bytearray(path.read_bytes())
    signature = data.find(b"PK\x03\x04")
    if signature < 0:
        raise AssertionError("ZIP local header is missing")
    name_length, extra_length = struct.unpack_from("<HH", data, signature + 26)
    payload = signature + 30 + name_length + extra_length
    data[payload] ^= 0x40
    path.write_bytes(data)


def run(probe: str, archive: pathlib.Path, manifest: pathlib.Path,
        cache: pathlib.Path, digest: str, cancel_after: int | None = None,
        crash_at: str | None = None,
        copy_publish: bool = False,
        succeeds: bool = True) -> subprocess.CompletedProcess[str]:
    command = [probe, str(archive), str(manifest), str(cache), digest]
    if cancel_after is not None:
        command.append(str(cancel_after))
    elif crash_at is not None:
        command.append(f"crash:{crash_at}")
    elif copy_publish:
        command.append("copy-publish")
    result = subprocess.run(command, text=True, capture_output=True)
    if crash_at is not None:
        if result.returncode != 70:
            raise AssertionError(
                f"checkpoint crash returned {result.returncode}: "
                f"{result.stdout}{result.stderr}"
            )
        return result
    if (result.returncode == 0) != succeeds:
        raise AssertionError(
            f"unexpected result {result.returncode}: {result.stdout}{result.stderr}"
        )
    return result


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_asset_bootstrap.py probe")
    probe = sys.argv[1]
    with tempfile.TemporaryDirectory(prefix="playground-bootstrap-") as temp:
        root = pathlib.Path(temp)
        archive = root / "AppAssets.zip"
        manifest = root / "AppAssets.version"
        cache = root / "cache"
        entries = {
            "start.lua": b"print('start')\n",
            "m_boot/start.lua": b"print('boot')\n",
            "assets/data.bin": bytes(range(256)) * 4096,
            "unicode/\u30c6\u30b9\u30c8.txt": b"valid utf-8 name\n",
        }
        write_zip(archive, entries)
        text = metadata_for(archive, entries)
        manifest.write_text(text, encoding="utf-8")
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()

        run(probe, archive, manifest, cache, digest, 1, succeeds=False)
        assert not list(cache.glob("install-*"))
        assert not list(cache.glob(".staging-*"))

        first = run(probe, archive, manifest, cache, digest)
        assert "installed=1" in first.stdout
        install = pathlib.Path(first.stdout.splitlines()[0])
        for name, value in entries.items():
            assert (install / name).read_bytes() == value

        hidden_archive = root / "AppAssets.hidden"
        archive.rename(hidden_archive)
        second = run(probe, archive, manifest, cache, digest)
        assert "installed=0" in second.stdout
        hidden_archive.rename(archive)

        # Old yuzu releases do not implement RenameDirectory. The optional
        # copy publisher consumes only a fully validated staging tree and must
        # produce the same content-addressed generation with no staging leak.
        copy_cache = root / "copy-publish-cache"
        copied = run(probe, archive, manifest, copy_cache, digest,
                     copy_publish=True)
        copied_install = pathlib.Path(copied.stdout.splitlines()[0])
        assert "installed=1" in copied.stdout
        assert (copied_install / "start.lua").read_bytes() == \
            entries["start.lua"]
        assert not list(copy_cache.glob(".staging-*"))

        # A process death after staging is durably committed must preserve the
        # previous generation. The next normal launch discards the abandoned
        # staging tree and can install the replacement.
        crash_entries = dict(entries)
        crash_entries["start.lua"] = b"print('checkpoint-a')\n"
        crash_archive = root / "AppAssets-checkpoint-a.zip"
        crash_manifest = root / "AppAssets-checkpoint-a.version"
        write_zip(crash_archive, crash_entries)
        crash_manifest.write_text(metadata_for(crash_archive, crash_entries),
                                  encoding="utf-8")
        crash_digest = hashlib.sha256(crash_archive.read_bytes()).hexdigest()
        run(probe, crash_archive, crash_manifest, cache, crash_digest,
            crash_at="staging-committed", succeeds=False)
        assert install.is_dir()
        assert (install / "start.lua").read_bytes() == entries["start.lua"]
        assert list(cache.glob(".staging-*"))
        checkpoint_a = run(probe, crash_archive, crash_manifest, cache,
                           crash_digest)
        install_a = pathlib.Path(checkpoint_a.stdout.splitlines()[0])
        assert (install_a / "start.lua").read_bytes() == \
            crash_entries["start.lua"]

        # Death after rename can leave either the new committed-looking name
        # or (on real media before commit) only the old generation. Exercise
        # the former here; reinstall/old-generation preservation is already
        # covered by the staging case above.
        rename_entries = dict(entries)
        rename_entries["start.lua"] = b"print('checkpoint-b')\n"
        rename_archive = root / "AppAssets-checkpoint-b.zip"
        rename_manifest = root / "AppAssets-checkpoint-b.version"
        write_zip(rename_archive, rename_entries)
        rename_manifest.write_text(metadata_for(rename_archive, rename_entries),
                                   encoding="utf-8")
        rename_digest = hashlib.sha256(rename_archive.read_bytes()).hexdigest()
        run(probe, rename_archive, rename_manifest, cache, rename_digest,
            crash_at="generation-renamed", succeeds=False)
        assert install_a.is_dir()
        renamed = cache / f"install-{rename_digest}"
        assert renamed.is_dir()
        recovered_rename = run(probe, rename_archive, rename_manifest, cache,
                               rename_digest)
        assert pathlib.Path(recovered_rename.stdout.splitlines()[0]) == renamed
        assert not install_a.exists()

        committed_entries = dict(entries)
        committed_entries["start.lua"] = b"print('checkpoint-c')\n"
        committed_archive = root / "AppAssets-checkpoint-c.zip"
        committed_manifest = root / "AppAssets-checkpoint-c.version"
        write_zip(committed_archive, committed_entries)
        committed_manifest.write_text(
            metadata_for(committed_archive, committed_entries),
            encoding="utf-8")
        committed_digest = hashlib.sha256(
            committed_archive.read_bytes()).hexdigest()
        run(probe, committed_archive, committed_manifest, cache,
            committed_digest, crash_at="generation-committed", succeeds=False)
        committed = cache / f"install-{committed_digest}"
        assert committed.is_dir() and renamed.is_dir()
        recovered_commit = run(probe, committed_archive, committed_manifest,
                               cache, committed_digest)
        assert pathlib.Path(recovered_commit.stdout.splitlines()[0]) == committed
        assert not renamed.exists()

        # Restore the original generation for the corruption and upgrade
        # cases below.
        first = run(probe, archive, manifest, cache, digest)
        install = pathlib.Path(first.stdout.splitlines()[0])

        (install / ".appassets-version").write_text("damaged", encoding="utf-8")
        repaired = run(probe, archive, manifest, cache, digest)
        assert "installed=1" in repaired.stdout

        upgrade_entries = dict(entries)
        upgrade_entries["start.lua"] = b"print('upgraded')\n"
        upgrade_entries["assets/upgrade.bin"] = b"upgrade"
        upgrade = root / "AppAssets-upgrade.zip"
        upgrade_manifest = root / "AppAssets-upgrade.version"
        write_zip(upgrade, upgrade_entries)
        upgrade_manifest.write_text(metadata_for(upgrade, upgrade_entries),
                                    encoding="utf-8")
        upgrade_digest = hashlib.sha256(upgrade.read_bytes()).hexdigest()
        interrupted = run(probe, upgrade, upgrade_manifest, cache,
                          upgrade_digest, 1, succeeds=False)
        assert interrupted.returncode != 0
        assert install.is_dir()
        assert (install / "start.lua").read_bytes() == entries["start.lua"]
        assert not list(cache.glob(".staging-*"))
        upgraded = run(probe, upgrade, upgrade_manifest, cache, upgrade_digest)
        upgraded_install = pathlib.Path(upgraded.stdout.splitlines()[0])
        assert upgraded_install != install
        assert (upgraded_install / "start.lua").read_bytes() == \
            upgrade_entries["start.lua"]
        assert not install.exists()

        shutil.rmtree(cache)
        evicted = run(probe, archive, manifest, cache, digest)
        assert "installed=1" in evicted.stdout

        run(probe, archive, manifest, root / "wrong-hash-cache", "0" * 64,
            succeeds=False)

        truncated = root / "truncated.zip"
        truncated.write_bytes(archive.read_bytes()[:-11])
        truncated_manifest = root / "truncated.version"
        truncated_manifest.write_text(metadata_for(truncated, entries),
                                      encoding="utf-8")
        run(probe, truncated, truncated_manifest, root / "truncated-cache",
            hashlib.sha256(truncated.read_bytes()).hexdigest(), succeeds=False)

        bad_crc = root / "bad-crc.zip"
        write_zip(bad_crc, entries)
        corrupt_first_payload(bad_crc)
        bad_crc_manifest = root / "bad-crc.version"
        bad_crc_manifest.write_text(metadata_for(bad_crc, entries),
                                    encoding="utf-8")
        run(probe, bad_crc, bad_crc_manifest, root / "bad-crc-cache",
            hashlib.sha256(bad_crc.read_bytes()).hexdigest(), succeeds=False)

        overflow_manifest = root / "overflow.version"
        overflow_manifest.write_text(
            metadata_for(archive, entries).replace(
                f"expanded_size={sum(map(len, entries.values()))}",
                "expanded_size=18446744073709551616"),
            encoding="utf-8")
        run(probe, archive, overflow_manifest, root / "overflow-cache", digest,
            succeeds=False)

        unsafe = root / "unsafe.zip"
        unsafe_entries = {"../escape": b"no", "start.lua": b"x",
                          "m_boot/start.lua": b"y"}
        write_zip(unsafe, unsafe_entries)
        unsafe_text = metadata_for(unsafe, unsafe_entries)
        unsafe_manifest = root / "unsafe.version"
        unsafe_manifest.write_text(unsafe_text, encoding="utf-8")
        unsafe_digest = hashlib.sha256(unsafe.read_bytes()).hexdigest()
        unsafe_cache = root / "unsafe-cache"
        run(probe, unsafe, unsafe_manifest, unsafe_cache, unsafe_digest,
            succeeds=False)
        assert not (root / "escape").exists()

        duplicate = root / "duplicate.zip"
        with zipfile.ZipFile(duplicate, "w", zipfile.ZIP_STORED) as output:
            output.writestr("start.lua", b"one")
            output.writestr("start.lua", b"two")
            output.writestr("m_boot/start.lua", b"boot")
        duplicate_entries = {
            "first": b"one", "second": b"two", "boot": b"boot"
        }
        duplicate_text = metadata_for(duplicate, duplicate_entries)
        duplicate_manifest = root / "duplicate.version"
        duplicate_manifest.write_text(duplicate_text, encoding="utf-8")
        run(probe, duplicate, duplicate_manifest, root / "duplicate-cache",
            hashlib.sha256(duplicate.read_bytes()).hexdigest(), succeeds=False)

        case_collision = root / "case-collision.zip"
        with zipfile.ZipFile(case_collision, "w") as output:
            output.writestr("start.lua", b"start")
            output.writestr("m_boot/start.lua", b"boot")
            output.writestr("assets/Image.texb", b"one")
            output.writestr("assets/image.texb", b"two")
        collision_entries = {
            "start.lua": b"start", "m_boot/start.lua": b"boot",
            "assets/Image.texb": b"one", "assets/image.texb": b"two",
        }
        collision_manifest = root / "case-collision.version"
        collision_manifest.write_text(
            metadata_for(case_collision, collision_entries), encoding="utf-8")
        run(probe, case_collision, collision_manifest,
            root / "case-collision-cache",
            hashlib.sha256(case_collision.read_bytes()).hexdigest(),
            succeeds=False)

        portable_collision = root / "portable-collision.zip"
        portable_entries = {
            "start.lua": b"start", "m_boot/start.lua": b"boot",
            "assets/name. ": b"ambiguous",
        }
        write_zip(portable_collision, portable_entries)
        portable_manifest = root / "portable-collision.version"
        portable_manifest.write_text(
            metadata_for(portable_collision, portable_entries),
            encoding="utf-8")
        run(probe, portable_collision, portable_manifest,
            root / "portable-collision-cache",
            hashlib.sha256(portable_collision.read_bytes()).hexdigest(),
            succeeds=False)

        symlink = root / "symlink.zip"
        with zipfile.ZipFile(symlink, "w") as output:
            link = zipfile.ZipInfo("link")
            link.create_system = 3
            link.external_attr = 0o120777 << 16
            output.writestr(link, "start.lua")
            output.writestr("start.lua", b"start")
            output.writestr("m_boot/start.lua", b"boot")
        symlink_entries = {"link": b"start.lua", "start.lua": b"start",
                           "m_boot/start.lua": b"boot"}
        symlink_manifest = root / "symlink.version"
        symlink_manifest.write_text(metadata_for(symlink, symlink_entries),
                                    encoding="utf-8")
        run(probe, symlink, symlink_manifest, root / "symlink-cache",
            hashlib.sha256(symlink.read_bytes()).hexdigest(), succeeds=False)

        shutil.rmtree(cache)
    print("asset bootstrap tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
