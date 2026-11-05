"""Read public APK FileDescriptorProto metadata without third-party packages.

Never use against captured traffic or account files. No payload values/addresses
are needed: the output is only schema names, field numbers and enum constants.
"""
import argparse
from pathlib import Path


def varint(data, offset):
    result = 0
    for shift in range(0, 70, 7):
        if offset >= len(data):
            raise ValueError("truncated varint")
        byte = data[offset]
        offset += 1
        if shift == 63 and byte > 1:
            raise ValueError("oversized varint")
        result |= (byte & 127) << shift
        if byte < 128:
            return result, offset
    raise ValueError("unterminated varint")


def fields(data):
    result, offset = {}, 0
    while offset < len(data):
        tag, offset = varint(data, offset)
        number, wire = tag >> 3, tag & 7
        if not number:
            raise ValueError("invalid tag")
        if wire == 0:
            value, offset = varint(data, offset)
        else:
            if wire == 2:
                length, offset = varint(data, offset)
            elif wire in (1, 5):
                length = 8 if wire == 1 else 4
            else:
                raise ValueError("unsupported wire type")
            if length > len(data) - offset:
                raise ValueError("truncated field")
            value, offset = data[offset:offset + length], offset + length
        result.setdefault(number, []).append(value)
    return result


def first(mapping, key, default=b""):
    return mapping.get(key, [default])[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library")
    parser.add_argument("proto_name")
    parser.add_argument("--message", default="")
    args = parser.parse_args()
    data = Path(args.library).read_bytes()
    name = args.proto_name.encode("ascii")
    if len(name) >= 128:
        raise SystemExit("schema name too long")
    prefix, offset = bytes([10, len(name)]) + name, 0
    while True:
        start = data.find(prefix, offset)
        if start < 0:
            raise SystemExit("descriptor not found")
        offset = start + len(prefix)
        end = data.find(b"\x62\x06proto3", offset)
        if end < 0 or end - start > 262144:
            continue
        try:
            descriptor = fields(data[start:end + 8])
        except ValueError:
            continue
        if first(descriptor, 1) != name:
            continue
        for message in descriptor.get(4, []):
            message = fields(message)
            message_name = first(message, 1).decode("ascii")
            if args.message not in message_name:
                continue
            print("message", message_name)
            for field in message.get(2, []):
                field = fields(field)
                print(first(field, 3, 0), first(field, 1).decode("ascii"),
                      "type", first(field, 5, 0), first(field, 6).decode("ascii"))
        return


if __name__ == "__main__":
    main()
