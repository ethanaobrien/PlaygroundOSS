#!/usr/bin/env python3
"""Fail closed when the Switch executable outgrows its process descriptor."""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import subprocess


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--elf", type=pathlib.Path, required=True)
    parser.add_argument("--json", type=pathlib.Path, required=True)
    parser.add_argument("--npdm", type=pathlib.Path, required=True)
    parser.add_argument("--nm", type=pathlib.Path, required=True)
    args = parser.parse_args()

    descriptor = json.loads(args.json.read_text(encoding="utf-8"))
    services = descriptor["service_access"]
    if not services or any("*" in service for service in services):
        raise SystemExit("NPDM service access must be a non-wildcard allowlist")
    if descriptor.get("service_host"):
        raise SystemExit("the application must not register Horizon services")
    if "nvdrv:a" in services:
        raise SystemExit(
            "application titles must use nvdrv; nvdrv:a is reserved for applets"
        )
    permissions = int(descriptor["filesystem_access"]["permissions"], 0)
    # 0x1 is the ordinary application-content permission.  0x20 is the
    # narrow creation permission needed to provision this title's NACP-sized
    # SaveData/CacheStorage when Horizon has not done so before first launch.
    if permissions != 0x21:
        raise SystemExit(
            "installed title may only request ApplicationInfo and SaveData "
            "creation FS access"
        )
    if descriptor["title_id_range_min"] != descriptor["title_id"] or \
       descriptor["title_id_range_max"] != descriptor["title_id"]:
        raise SystemExit("NPDM title range must be exactly the configured title")

    capability = next(item for item in descriptor["kernel_capabilities"]
                      if item["type"] == "syscalls")
    permitted = set(capability["value"])
    symbols = subprocess.run(
        [str(args.nm), "-C", str(args.elf)], check=True, text=True,
        capture_output=True).stdout
    # Service ACLs are independent of kernel SVC capabilities. Tie the
    # installed-title descriptor to the linked libnx clients which are
    # otherwise easy for an emulator to accept despite a hardware-invalid
    # service name.
    required_services = {}
    service_symbols = {
        "accountInitialize": "acc:u0",
        "apmInitialize": "apm",
        "appletInitialize": "appletOE",
        "audoutInitialize": "audout:u",
        "socketInitialize": "bsd:u",
        "capssuInitialize": "caps:su",
        "fatalThrow": "fatal:u",
        # Current libnx exports nvInitialize; older revisions used the
        # internal spelling _nvInitialize. Accept either symbol as evidence
        # that the executable opens the NV service.
        "nvInitialize": "nvdrv",
        "_nvInitialize": "nvdrv",
        "csrngInitialize": "csrng",
        "fsInitialize": "fsp-srv",
        "hidInitialize": "hid",
        "nifmInitialize": "nifm:u",
        "notifInitialize": "notif:a",
        "setInitialize": "set",
        "setsysInitialize": "set:sys",
        # getaddrinfo is backed by Horizon's resolver service. Socket and NIFM
        # initialization can both succeed without this ACL, leaving libcurl to
        # fail every hostname before it sends an HTTP request.
        "sfdnsresGetAddrInfoRequest": "sfdnsres",
        "sslInitialize": "ssl",
        "timeInitialize": "time:u",
        "viInitialize": "vi:u",
    }
    symbol_names = {
        match.group(1)
        for match in re.finditer(r"^[0-9a-fA-F]+\s+[A-Za-z]\s+(\S+)$",
                                 symbols, re.MULTILINE)
    }
    for symbol, service in service_symbols.items():
        if symbol in symbol_names:
            required_services[symbol] = service
    missing_services = sorted(set(required_services.values()) - set(services))
    stale_services = sorted(set(services) - set(required_services.values()))
    if missing_services:
        raise SystemExit(
            "linked libnx clients absent from NPDM service access: " +
            ", ".join(missing_services)
        )
    if stale_services:
        raise SystemExit(
            "NPDM contains services with no linked libnx client: " +
            ", ".join(stale_services)
        )
    linked = {
        line.split()[-1]
        for line in symbols.splitlines()
        if len(line.split()) >= 3 and line.split()[1] in {"T", "t", "W", "w"}
        and line.split()[-1].startswith("svc")
    }
    missing = sorted(linked - permitted)
    stale = sorted(permitted - linked)
    if missing:
        raise SystemExit("linked SVCs absent from NPDM: " + ", ".join(missing))
    if stale:
        raise SystemExit("NPDM contains unlinked SVCs: " + ", ".join(stale))
    if not args.npdm.is_file() or args.npdm.stat().st_size < 0x400:
        raise SystemExit("npdmtool did not produce a valid-sized descriptor")
    print(f"Switch NPDM audit passed: {len(services)} services, "
          f"{len(linked)} SVCs, FS mask 0x{permissions:x}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
