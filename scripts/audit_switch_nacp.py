#!/usr/bin/env python3
"""Fail closed when installed-title NACP storage/account policy drifts."""

from __future__ import annotations

import argparse
import ctypes
import pathlib

from configure_switch_nacp import NacpStruct


def integer(value: str) -> int:
    return int(value, 0)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--nacp", required=True, type=pathlib.Path)
    parser.add_argument("--title-id", required=True, type=integer)
    parser.add_argument("--save-size", required=True, type=integer)
    parser.add_argument("--save-journal", required=True, type=integer)
    parser.add_argument("--cache-size", required=True, type=integer)
    parser.add_argument("--cache-journal", required=True, type=integer)
    parser.add_argument("--cache-max", required=True, type=integer)
    args = parser.parse_args()

    if args.title_id >> 48 != 0x0100 or args.title_id & 0xFFF:
        raise ValueError(
            "Switch application title ID must be 0100xxxxxxxxx000"
        )

    data = args.nacp.read_bytes()
    if len(data) != ctypes.sizeof(NacpStruct):
        raise ValueError(f"NACP size is {len(data):#x}, expected 0x4000")
    nacp = NacpStruct.from_buffer_copy(data)
    expected = {
        "startup_user_account": 1,
        "user_account_switch_lock": 1,
        "presence_group_id": args.title_id,
        "add_on_content_base_id": args.title_id + 0x1000,
        "save_data_owner_id": args.title_id,
        "user_account_save_data_size": args.save_size,
        "user_account_save_data_journal_size": args.save_journal,
        "user_account_save_data_size_max": args.save_size,
        "user_account_save_data_journal_size_max": args.save_journal,
        "device_save_data_size": 0,
        "device_save_data_journal_size": 0,
        "cache_storage_size": args.cache_size,
        "cache_storage_journal_size": args.cache_journal,
        "cache_storage_data_and_journal_size_max": args.cache_max,
        "cache_storage_index_max": 1,
    }
    wrong = {
        name: (getattr(nacp, name), value)
        for name, value in expected.items()
        if getattr(nacp, name) != value
    }
    if wrong:
        raise ValueError(f"NACP policy mismatch: {wrong}")
    if nacp.screenshot != 0:
        raise ValueError("NACP disables application screenshots")
    print(
        "Switch NACP audit passed: required account, locked user, "
        f"SaveData={args.save_size:#x}, CacheStorage={args.cache_size:#x}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
