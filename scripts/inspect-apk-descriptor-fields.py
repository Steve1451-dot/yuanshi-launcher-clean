"""Read public APK protobuf descriptor metadata without third-party modules.

Only accepts native .so files. Does not execute the library, read runtime data,
or print default values. A descriptor/name search is not proof that no private
API exists, nor that an observed message is accepted by a live service.
"""
import argparse
import re
from pathlib import Path


def varint(data, offset):
    value = 0
    for shift in range(0, 70, 7):
        if offset >= len(data):
            raise ValueError("truncated varint")
        byte = data[offset]
        offset += 1
        if shift == 63 and byte > 1:
            raise ValueError("overflowing varint")
        value |= (byte & 127) << shift
        if byte < 128:
            return value, offset
    raise ValueError("overflowing varint")


def fields(data):
    result = {}
    offset = 0
    count = 0
    while offset < len(data):
        tag, offset = varint(data, offset)
        number, wire = tag >> 3, tag & 7
        count += 1
        if not number or number > 0x1fffffff or count > 100000:
            raise ValueError("invalid field")
        if wire == 0:
            value, offset = varint(data, offset)
        elif wire in (1, 2, 5):
            if wire == 2:
                size, offset = varint(data, offset)
            else:
                size = 8 if wire == 1 else 4
            end = offset + size
            if end > len(data):
                raise ValueError("truncated field")
            value, offset = data[offset:end], end
        else:
            raise ValueError("unsupported wire type")
        result.setdefault(number, []).append(value)
    return result


def text(record, number):
    value = record.get(number, [b""])[0]
    if not isinstance(value, bytes):
        raise ValueError("expected descriptor string")
    return value.decode("utf-8")


def descriptors(data, name):
    encoded = name.encode("ascii")
    if not 1 <= len(encoded) < 128:
        raise ValueError("unsupported descriptor name")
    prefix = bytes([10, len(encoded)]) + encoded
    position = 0
    while True:
        start = data.find(prefix, position)
        if start < 0:
            return
        position = start + len(prefix)
        end = data.find(b"\x62\x06proto3", position, start + 1024 * 1024)
        if end < 0:
            continue
        try:
            record = fields(data[start:end + 8])
            if text(record, 1) == name and text(record, 12) == "proto3":
                yield record
        except (ValueError, UnicodeError):
            continue


def messages(raw, parent, match, depth=0):
    if depth > 16:
        raise ValueError("descriptor nesting limit")
    record = fields(raw)
    name = parent + "." + text(record, 1)
    if match.search(name):
        print("message", name)
        for raw_field in record.get(2, []):
            field = fields(raw_field)
            print(" ", field.get(3, [0])[0], text(field, 1),
                  "type", field.get(5, [0])[0],
                  "label", field.get(4, [0])[0], text(field, 6))
    for nested in record.get(3, []):
        messages(nested, name, match, depth + 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library", type=Path)
    parser.add_argument("proto_name")
    parser.add_argument("--message", default=".", help="message-name regex")
    args = parser.parse_args()
    if args.library.suffix != ".so":
        parser.error("only public native APK .so files are accepted")
    data = args.library.read_bytes()
    if data[:6] != b"\x7fELF\x02\x01":
        parser.error("expected a little-endian ELF64 library")
    match = re.compile(args.message, re.IGNORECASE)
    descriptor = next(descriptors(data, args.proto_name), None)
    if descriptor is None:
        parser.error("descriptor not found or unsupported; not an API absence proof")
    package = text(descriptor, 2)
    print("file:", args.proto_name, "package:", package)
    for raw in descriptor.get(4, []):
        messages(raw, package, match)


if __name__ == "__main__":
    main()
