"""Read-only ELF64/AArch64 string/call cross-reference aid for public APK code.

No disassembler dependency and no APK execution. ADRP/ADD matches are candidates,
not a control-flow or register-liveness proof: verify each in objdump before
drawing conclusions. Never run against account files or captured traffic.
"""
import argparse
import hashlib
import re
import struct
from pathlib import Path


def signed(value, bits):
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


class Elf:
    def __init__(self, data):
        if len(data) < 64 or data[:6] != b"\x7fELF\x02\x01":
            raise ValueError("expected little-endian ELF64")
        if struct.unpack_from("<H", data, 18)[0] != 183:
            raise ValueError("expected AArch64")
        table = struct.unpack_from("<Q", data, 40)[0]
        size, count, names = struct.unpack_from("<HHH", data, 58)
        if size != 64 or not count or names >= count or table + size * count > len(data):
            raise ValueError("unsupported ELF section table")
        self.data = data
        entries = [struct.unpack_from("<IIQQQQIIQQ", data, table + i * size)
                   for i in range(count)]
        for entry in entries:
            if entry[1] != 8 and entry[4] + entry[5] > len(data):
                raise ValueError("section outside ELF file")
        names_entry = entries[names]
        names_data = data[names_entry[4]:names_entry[4] + names_entry[5]]
        self.sections = {}
        for entry in entries:
            end = names_data.find(b"\0", entry[0])
            if end < 0:
                raise ValueError("invalid section name")
            name = names_data[entry[0]:end].decode("ascii")
            self.sections[name] = entry

    def section(self, name):
        entry = self.sections[name]
        return entry[3], self.data[entry[4]:entry[4] + entry[5]]

    def words(self):
        address, data = self.section(".text")
        if len(data) % 4:
            raise ValueError("unaligned AArch64 text section")
        return address, [w[0] for w in struct.iter_unpack("<I", data)]

    def strings(self, pattern):
        address, data = self.section(".rodata")
        offset = 0
        for value in data.split(b"\0"):
            if pattern.search(value.decode("ascii", errors="replace")):
                yield address + offset, value.decode("ascii", errors="replace")
            offset += len(value) + 1

    def xrefs(self, targets):
        """Return candidate instruction refs to exact string starts."""
        base, words = self.words()
        matches = {target: [] for target in targets}
        for index, word in enumerate(words):
            address = base + index * 4
            kind = word & 0x9f000000
            if kind not in (0x10000000, 0x90000000):
                continue
            imm = signed(((word >> 5) & 0x7ffff) << 2 | (word >> 29) & 3, 21)
            if kind == 0x10000000:
                target = address + imm
                if target in matches:
                    matches[target].append((address, address, "ADR"))
                continue
            page = (address & ~0xfff) + (imm << 12)
            register = word & 31
            for delta in range(1, min(25, len(words) - index)):
                later = words[index + delta]
                if later & 0xff800000 == 0x91000000:
                    if (later >> 5) & 31 == register:
                        value = (later >> 10) & 0xfff
                        target = page + (value << (12 if later & (1 << 22) else 0))
                        if target in matches:
                            matches[target].append((address, address + delta * 4, "ADRP+ADD"))
                # Do not carry an address candidate through a call or return.
                if later & 0xfc000000 == 0x94000000 or later == 0xd65f03c0:
                    break
                # A second ADR(P) into the same register replaces this page.
                if later & 0x1f000000 == 0x10000000 and later & 31 == register:
                    break
        return matches

    def calls(self, target):
        base, words = self.words()
        for index, word in enumerate(words):
            if word & 0xfc000000 != 0x94000000:
                continue
            address = base + index * 4
            if address + (signed(word & 0x3ffffff, 26) << 2) == target:
                yield address


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--strings", help="case-insensitive regex over .rodata strings")
    group.add_argument("--calls-to", type=lambda value: int(value, 0))
    args = parser.parse_args()
    data = Path(args.library).read_bytes()
    elf = Elf(data)
    print("sha256", hashlib.sha256(data).hexdigest())
    if args.calls_to is not None:
        for address in elf.calls(args.calls_to):
            print(f"BL 0x{address:x} -> 0x{args.calls_to:x}")
        return
    values = dict(elf.strings(re.compile(args.strings, re.IGNORECASE)))
    for target, refs in elf.xrefs(values).items():
        print(f"STRING 0x{target:x} {values[target]!r}")
        for first, second, kind in refs:
            print(f"  CANDIDATE {kind} 0x{first:x} / 0x{second:x}")


if __name__ == "__main__":
    main()
