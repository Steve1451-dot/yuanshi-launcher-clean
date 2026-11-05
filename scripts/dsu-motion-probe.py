"""Local, synthetic Cemuhook input for an isolated emulator diagnostic.

No real device, login or cloud connection. Protocol reference:
https://v1993.github.io/cemuhook-protocol/
"""

import argparse
import math
import socket
import struct
import time
import zlib


def message(kind, payload):
    packet = bytearray(struct.pack("<4sHHIII", b"DSUS", 1001, len(payload) + 4, 0, 0x4D4F544E, kind) + payload)
    struct.pack_into("<I", packet, 8, zlib.crc32(packet))
    return bytes(packet)


def valid_request(packet):
    if len(packet) < 20 or packet[:4] != b"DSUC":
        return False
    if struct.unpack_from("<H", packet, 6)[0] != len(packet) - 16:
        return False
    expected = struct.unpack_from("<I", packet, 8)[0]
    check = bytearray(packet)
    struct.pack_into("<I", check, 8, 0)
    return zlib.crc32(check) == expected


def common(slot=0):
    return bytes((slot, 2 if slot == 0 else 0, 2, 1, 0x02, 0, 0, 0, 0, 1, 5))


def motion_packet(counter, elapsed):
    payload = bytearray(80)
    payload[:11] = common()
    payload[11] = 1
    struct.pack_into("<I", payload, 12, counter & 0xFFFFFFFF)
    payload[20:24] = bytes((128,) * 4)
    struct.pack_into("<Q", payload, 48, int(elapsed * 1_000_000))
    # Rest, then independent oscillation on all axes. Acceleration is in g;
    # angular velocity is degrees/s here, NOT the native cloud wire's rad/s.
    phase = elapsed % 16
    moving = 3 <= phase < 13
    pitch = 30 * math.sin(elapsed * 1.3) if moving else 0
    yaw = 40 * math.cos(elapsed * 0.9) if moving else 0
    roll = 20 * math.sin(elapsed * 0.7) if moving else 0
    struct.pack_into("<6f", payload, 56, 0, 0, 1, pitch, yaw, roll)
    return message(0x100002, payload)


def self_test():
    packet = motion_packet(123, 4.5)
    assert len(packet) == 100
    assert struct.unpack_from("<H", packet, 6)[0] == 84
    request = bytearray(message(0x100002, bytes(8)))
    request[:4] = b"DSUC"
    struct.pack_into("<I", request, 8, 0)
    struct.pack_into("<I", request, 8, zlib.crc32(request))
    assert valid_request(request)
    request[-1] ^= 1
    assert not valid_request(request)
    assert struct.unpack_from("<I", packet, 32)[0] == 123
    assert struct.unpack_from("<f", packet, 84)[0] == 1
    print("DSU_SELF_TEST_OK length=100 crc=checked offsets=checked", flush=True)


def serve(port, duration):
    clients = {}
    requests = sent = invalid = 0
    subscribed = None
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", port))
    sock.settimeout(0.004)
    started = time.monotonic()
    next_packet = started
    print(f"DSU_READY loopback=1 port={port} synthetic=1", flush=True)
    while time.monotonic() - started < duration:
        try:
            packet, client = sock.recvfrom(4096)
            if not valid_request(packet):
                invalid += 1
                continue
            requests += 1
            kind = struct.unpack_from("<I", packet, 16)[0]
            if kind == 0x100000:
                sock.sendto(message(kind, struct.pack("<H", 1001)), client)
            elif kind == 0x100001 and len(packet) >= 24:
                count = struct.unpack_from("<I", packet, 20)[0]
                if count <= 4 and len(packet) >= 24 + count:
                    for slot in packet[24:24 + count]:
                        sock.sendto(message(kind, common(slot) + b"\0"), client)
            elif kind == 0x100002 and len(packet) >= 28:
                flags, slot = packet[20:22]
                if flags == 0 or (flags & 1 and slot == 0):
                    clients[client] = time.monotonic()
                    if subscribed is None:
                        subscribed = time.monotonic()
                        print("DSU_SUBSCRIBED slot=0", flush=True)
        except socket.timeout:
            pass
        now = time.monotonic()
        if now >= next_packet:
            clients = {client: seen for client, seen in clients.items() if now - seen < 5}
            data = motion_packet(sent + 1, now - (subscribed or started))
            for client in clients:
                sock.sendto(data, client)
            if clients:
                sent += 1
            next_packet = now + 1 / 120
    sock.close()
    print(f"DSU_DONE requests={requests} sent_ticks={sent} invalid={invalid}", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=26761)
    parser.add_argument("--duration", type=float, default=90)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
    else:
        if not 1024 <= args.port <= 65535 or not 1 <= args.duration <= 300:
            parser.error("port or bounded duration out of range")
        serve(args.port, args.duration)
