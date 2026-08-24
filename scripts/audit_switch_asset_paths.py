#!/usr/bin/env python3
"""Prove a bundled archive is injectively representable on Horizon storage."""

from __future__ import annotations

import argparse
import base64
from pathlib import Path
import zipfile


COMPONENT_LIMIT = 64
PATH_LIMIT = 0x300
PAYLOAD_LIMIT = 60
RESERVED = "~pg"


def encode_component(component: str) -> list[str]:
    raw = component.encode("utf-8")
    if len(raw) <= COMPONENT_LIMIT and not component.startswith(RESERVED):
        return [component]
    payload = base64.urlsafe_b64encode(raw).decode("ascii").rstrip("=")
    if len(payload) <= PAYLOAD_LIMIT:
        return ["~pgs" + payload]
    result = ["~pgb" + payload[:PAYLOAD_LIMIT]]
    offset = PAYLOAD_LIMIT
    while len(payload) - offset > PAYLOAD_LIMIT:
        result.append("~pgc" + payload[offset:offset + PAYLOAD_LIMIT])
        offset += PAYLOAD_LIMIT
    result.append("~pge" + payload[offset:])
    return result


def encode_path(path: str) -> str:
    physical: list[str] = []
    for component in path.split("/"):
        physical.extend(encode_component(component))
    result = "/".join(physical)
    if len(result.encode()) > PATH_LIMIT:
        raise ValueError(f"encoded path exceeds {PATH_LIMIT} bytes: {path}")
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--archive", required=True, type=Path)
    args = parser.parse_args()

    logical: list[str] = []
    with zipfile.ZipFile(args.archive) as archive:
        for entry in archive.infolist():
            name = entry.filename.rstrip("/")
            if name:
                logical.append("pgcache:/application/.staging/" + name)
    physical = [encode_path(path) for path in logical]
    if len(set(logical)) != len(logical):
        raise SystemExit("AppAssets contains duplicate logical paths")
    if len(set(physical)) != len(physical):
        raise SystemExit("Switch component codec produced a physical collision")
    components = [component for path in physical for component in path.split("/")]
    maximum_component = max((len(value.encode()) for value in components),
                            default=0)
    if maximum_component > COMPONENT_LIMIT:
        raise SystemExit("Switch component codec exceeded its physical limit")
    maximum_path = max((len(value.encode()) for value in physical), default=0)
    print(
        "Switch AppAssets path audit passed: "
        f"{len(logical)} unique entries, max component={maximum_component}, "
        f"max path={maximum_path}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
