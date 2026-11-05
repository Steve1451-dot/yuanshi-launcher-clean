#!/usr/bin/env python3
"""Fill every NACP language slot from the primary language entry.

devkitPro's nacptool only initializes the first 12 language entries. The
remaining slots (Russian, Korean, Traditional Chinese and Simplified Chinese)
stay empty, which makes the application title blank when the Switch is set to
one of those languages. This script patches either a standalone control.nacp
or the NACP embedded in an NRO, copying the primary title and author into any
empty slot. It is deliberately idempotent so it can run after every build.
"""

from __future__ import annotations

import argparse
import os
import struct
import sys
import tempfile
from pathlib import Path

NACP_SIZE = 0x4000
LANGUAGE_COUNT = 16
ENTRY_SIZE = 0x300
TITLE_SIZE = 0x200
AUTHOR_SIZE = 0x100


def embedded_nacp_offset(data: bytes) -> int:
    if len(data) < 0x20 or data[0x10:0x14] != b"NRO0":
        if len(data) == NACP_SIZE:
            return 0
        raise ValueError("file is neither an NRO nor a 0x4000-byte NACP")

    asset_offset = struct.unpack_from("<I", data, 0x18)[0]
    if asset_offset + 0x38 > len(data):
        raise ValueError("NRO asset header is outside the file")
    if data[asset_offset:asset_offset + 4] != b"ASET":
        raise ValueError("NRO is missing the ASET asset header")

    nacp_relative, nacp_size = struct.unpack_from("<QQ", data, asset_offset + 0x18)
    if nacp_size != NACP_SIZE:
        raise ValueError(f"unexpected embedded NACP size: {nacp_size:#x}")
    nacp_offset = asset_offset + nacp_relative
    if nacp_offset + NACP_SIZE > len(data):
        raise ValueError("embedded NACP is outside the file")
    return nacp_offset


def c_string(entry: bytes, offset: int, size: int) -> bytes:
    value = entry[offset:offset + size]
    return value.split(b"\0", 1)[0]


def fill_languages(data: bytearray, base: int) -> list[int]:
    primary = bytes(data[base:base + ENTRY_SIZE])
    title = c_string(primary, 0, TITLE_SIZE)
    author = c_string(primary, TITLE_SIZE, AUTHOR_SIZE)
    if not title:
        raise ValueError("primary NACP language entry has no title")

    changed: list[int] = []
    for language in range(LANGUAGE_COUNT):
        entry_offset = base + language * ENTRY_SIZE
        entry = bytes(data[entry_offset:entry_offset + ENTRY_SIZE])
        if c_string(entry, 0, TITLE_SIZE):
            continue
        data[entry_offset:entry_offset + TITLE_SIZE] = title.ljust(TITLE_SIZE, b"\0")
        data[entry_offset + TITLE_SIZE:entry_offset + ENTRY_SIZE] = author.ljust(
            AUTHOR_SIZE, b"\0"
        )
        changed.append(language)
    return changed


def write_atomically(path: Path, data: bytes) -> None:
    path = path.resolve()
    descriptor, temporary = tempfile.mkstemp(prefix=path.name + ".", dir=path.parent)
    try:
        with os.fdopen(descriptor, "wb") as handle:
            handle.write(data)
        os.replace(temporary, path)
    except BaseException:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path, help="control.nacp or Switch NRO")
    args = parser.parse_args()

    path = args.path
    data = bytearray(path.read_bytes())
    base = embedded_nacp_offset(data)
    changed = fill_languages(data, base)
    if changed:
        write_atomically(path, data)
        slots = ", ".join(str(language) for language in changed)
        print(f"Filled NACP language slots {slots} in {path}")
    else:
        print(f"NACP language slots already complete in {path}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as error:
        print(f"fill-nacp-languages: {error}", file=sys.stderr)
        sys.exit(1)
