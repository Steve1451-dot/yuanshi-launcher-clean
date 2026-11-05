"""Synthetic ELF tests. No APK, network, credentials or installed decoder needed."""
import importlib.util
import re
import struct
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    "inspect_apk_native", Path(__file__).resolve().parents[1] / "scripts/inspect-apk-native.py")
native = importlib.util.module_from_spec(spec)
spec.loader.exec_module(native)


def fixture(words):
    data = bytearray(0x300 + len(words) * 4)
    data[:6] = b"\x7fELF\x02\x01"
    struct.pack_into("<H", data, 18, 183)
    struct.pack_into("<Q", data, 40, 0x80)
    struct.pack_into("<HHH", data, 58, 64, 4, 1)
    names = b"\0.shstrtab\0.rodata\0.text\0"
    rodata = b"target\0other\0"
    data[0x180:0x180 + len(names)] = names
    data[0x200:0x200 + len(rodata)] = rodata
    for index, (name, kind, flags, address, offset, size) in enumerate([
            (0, 0, 0, 0, 0, 0),
            (1, 3, 0, 0, 0x180, len(names)),
            (11, 1, 2, 0x1100, 0x200, len(rodata)),
            (19, 1, 6, 0x5000, 0x300, len(words) * 4)]):
        struct.pack_into("<IIQQQQIIQQ", data, 0x80 + index * 64,
                         name, kind, flags, address, offset, size, 0, 0, 4, 0)
    for index, word in enumerate(words):
        struct.pack_into("<I", data, 0x300 + index * 4, word)
    return data


def adr(delta, register=1, page=False):
    value = delta & 0x1fffff
    return ((0x90000000 if page else 0x10000000) |
            (value & 3) << 29 | (value >> 2) << 5 | register)


def add(value, base=1, destination=1, shift=False):
    return 0x91000000 | int(shift) << 22 | value << 10 | base << 5 | destination


NOP = 0xd503201f
RET = 0xd65f03c0


class NativeInspectionTests(unittest.TestCase):
    def test_section_address_is_not_file_offset(self):
        elf = native.Elf(fixture([RET]))
        self.assertEqual(elf.section(".text")[0], 0x5000)
        self.assertEqual(list(elf.strings(re.compile("^target$"))), [(0x1100, "target")])

    def test_signed_adr(self):
        elf = native.Elf(fixture([adr(0x1100 - 0x5000), RET]))
        self.assertEqual(elf.xrefs([0x1100])[0x1100], [(0x5000, 0x5000, "ADR")])

    def test_negative_adrp_and_add(self):
        elf = native.Elf(fixture([adr(-4, page=True), NOP, add(0x100), RET]))
        self.assertEqual(elf.xrefs([0x1100])[0x1100], [(0x5000, 0x5008, "ADRP+ADD")])

    def test_add_with_shift(self):
        elf = native.Elf(fixture([adr(0, page=True), add(2, shift=True), RET]))
        self.assertEqual(elf.xrefs([0x7000])[0x7000], [(0x5000, 0x5004, "ADRP+ADD")])

    def test_unrelated_register_is_not_a_match(self):
        elf = native.Elf(fixture([adr(-4, page=True), add(0x100, base=2), RET]))
        self.assertEqual(elf.xrefs([0x1100])[0x1100], [])

    def test_call_and_return_end_candidate_window(self):
        for barrier in (RET, 0x94000008):
            elf = native.Elf(fixture([adr(-4, page=True), barrier, add(0x100)]))
            self.assertEqual(elf.xrefs([0x1100])[0x1100], [])

    def test_replaced_page_is_not_reused(self):
        elf = native.Elf(fixture([adr(-4, page=True), adr(-3, page=True), add(0x100)]))
        self.assertEqual(elf.xrefs([0x1100])[0x1100], [])

    def test_bounded_candidate_window(self):
        elf = native.Elf(fixture([adr(-4, page=True)] + [NOP] * 24 + [add(0x100)]))
        self.assertEqual(elf.xrefs([0x1100])[0x1100], [])

    def test_direct_calls_in_both_directions(self):
        elf = native.Elf(fixture([0x94000002, NOP, 0x97fffffe, RET]))
        self.assertEqual(list(elf.calls(0x5008)), [0x5000])
        self.assertEqual(list(elf.calls(0x5000)), [0x5008])

    def test_rejects_unsupported_or_truncated_file(self):
        with self.assertRaises(ValueError):
            native.Elf(b"\x7fELF\x02\x01")
        invalid = fixture([RET])
        invalid[18] = 62
        with self.assertRaises(ValueError):
            native.Elf(invalid)
        with self.assertRaises(ValueError):
            native.Elf(fixture([RET])[:0x100])
        with self.assertRaises(ValueError):
            native.Elf(fixture([RET])[:-1])


if __name__ == "__main__":
    unittest.main()
