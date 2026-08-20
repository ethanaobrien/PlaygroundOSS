#!/usr/bin/env python3
"""Configure documented libnx NacpStruct storage/account fields by name."""

from __future__ import annotations

import argparse
import ctypes
from pathlib import Path


class LanguageEntry(ctypes.LittleEndianStructure):
    _fields_ = [("name", ctypes.c_char * 0x200), ("author", ctypes.c_char * 0x100)]


class NeighborGroup(ctypes.LittleEndianStructure):
    _fields_ = [("group_id", ctypes.c_uint64), ("key", ctypes.c_uint8 * 0x10)]


class NeighborConfig(ctypes.LittleEndianStructure):
    _fields_ = [("send", NeighborGroup), ("receive", NeighborGroup * 0x10)]


class JitConfig(ctypes.LittleEndianStructure):
    _fields_ = [("flags", ctypes.c_uint64), ("memory_size", ctypes.c_uint64)]


class NacpStruct(ctypes.LittleEndianStructure):
    _fields_ = [
        ("lang", LanguageEntry * 16),
        ("isbn", ctypes.c_uint8 * 0x25),
        ("startup_user_account", ctypes.c_uint8),
        ("user_account_switch_lock", ctypes.c_uint8),
        ("add_on_content_registration_type", ctypes.c_uint8),
        ("attribute_flag", ctypes.c_uint32),
        ("supported_language_flag", ctypes.c_uint32),
        ("parental_control_flag", ctypes.c_uint32),
        ("screenshot", ctypes.c_uint8),
        ("video_capture", ctypes.c_uint8),
        ("data_loss_confirmation", ctypes.c_uint8),
        ("play_log_policy", ctypes.c_uint8),
        ("presence_group_id", ctypes.c_uint64),
        ("rating_age", ctypes.c_int8 * 0x20),
        ("display_version", ctypes.c_char * 0x10),
        ("add_on_content_base_id", ctypes.c_uint64),
        ("save_data_owner_id", ctypes.c_uint64),
        ("user_account_save_data_size", ctypes.c_uint64),
        ("user_account_save_data_journal_size", ctypes.c_uint64),
        ("device_save_data_size", ctypes.c_uint64),
        ("device_save_data_journal_size", ctypes.c_uint64),
        ("bcat_delivery_cache_storage_size", ctypes.c_uint64),
        ("application_error_code_category", ctypes.c_uint64),
        ("local_communication_id", ctypes.c_uint64 * 8),
        ("logo_type", ctypes.c_uint8),
        ("logo_handling", ctypes.c_uint8),
        ("runtime_add_on_content_install", ctypes.c_uint8),
        ("runtime_parameter_delivery", ctypes.c_uint8),
        ("reserved_x30f4", ctypes.c_uint8 * 2),
        ("crash_report", ctypes.c_uint8),
        ("hdcp", ctypes.c_uint8),
        ("pseudo_device_id_seed", ctypes.c_uint64),
        ("bcat_passphrase", ctypes.c_char * 0x41),
        ("startup_user_account_option", ctypes.c_uint8),
        ("reserved_for_user_account_save_data_operation", ctypes.c_uint8 * 6),
        ("user_account_save_data_size_max", ctypes.c_uint64),
        ("user_account_save_data_journal_size_max", ctypes.c_uint64),
        ("device_save_data_size_max", ctypes.c_uint64),
        ("device_save_data_journal_size_max", ctypes.c_uint64),
        ("temporary_storage_size", ctypes.c_uint64),
        ("cache_storage_size", ctypes.c_uint64),
        ("cache_storage_journal_size", ctypes.c_uint64),
        ("cache_storage_data_and_journal_size_max", ctypes.c_uint64),
        ("cache_storage_index_max", ctypes.c_uint16),
        ("reserved_x318a", ctypes.c_uint8 * 6),
        ("play_log_queryable_application_id", ctypes.c_uint64 * 0x10),
        ("play_log_query_capability", ctypes.c_uint8),
        ("repair_flag", ctypes.c_uint8),
        ("program_index", ctypes.c_uint8),
        ("required_network_service_license_on_launch", ctypes.c_uint8),
        ("reserved_x3214", ctypes.c_uint32),
        ("neighbor_detection_client_configuration", NeighborConfig),
        ("jit_configuration", JitConfig),
        ("reserved_x33c0", ctypes.c_uint8 * 0xC40),
    ]


def integer(value: str) -> int:
    return int(value, 0)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
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

    assert ctypes.sizeof(NacpStruct) == 0x4000
    assert NacpStruct.startup_user_account.offset == 0x3025
    assert NacpStruct.save_data_owner_id.offset == 0x3078
    assert NacpStruct.cache_storage_size.offset == 0x3170
    data = args.input.read_bytes()
    if len(data) != ctypes.sizeof(NacpStruct):
        raise ValueError(f"expected a 0x4000-byte NACP, got {len(data):#x}")
    nacp = NacpStruct.from_buffer_copy(data)
    nacp.startup_user_account = 1  # Required: qlaunch selects a user at launch.
    nacp.user_account_switch_lock = 1
    nacp.save_data_owner_id = args.title_id
    nacp.user_account_save_data_size = args.save_size
    nacp.user_account_save_data_journal_size = args.save_journal
    nacp.user_account_save_data_size_max = args.save_size
    nacp.user_account_save_data_journal_size_max = args.save_journal
    nacp.cache_storage_size = args.cache_size
    nacp.cache_storage_journal_size = args.cache_journal
    nacp.cache_storage_data_and_journal_size_max = args.cache_max
    nacp.cache_storage_index_max = 1
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(bytes(nacp))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
