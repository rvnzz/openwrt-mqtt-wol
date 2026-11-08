#!/usr/bin/env python3
"""Listener for the `make test` smoke: expects a 102-byte magic packet
for C8:FF:BF:0D:86:CB on 127.0.0.1:19009 within the timeout."""
import socket
import sys

EXPECT_MAC = bytes.fromhex("c8ffbf0d86cb")

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(("127.0.0.1", 19009))
s.settimeout(float(sys.argv[1]) if len(sys.argv) > 1 else 5.0)
try:
    data, addr = s.recvfrom(2048)
except socket.timeout:
    print("FAIL: no packet received", file=sys.stderr)
    sys.exit(1)

if len(data) != 102:
    print(f"FAIL: expected 102 bytes, got {len(data)}", file=sys.stderr)
    sys.exit(1)
if data[:6] != b"\xff" * 6:
    print("FAIL: header is not 6x FF", file=sys.stderr)
    sys.exit(1)
if data[6:] != EXPECT_MAC * 16:
    print("FAIL: magic payload incorrect", file=sys.stderr)
    sys.exit(1)

print(f"OK: magic packet for C8:FF:BF:0D:86:CB received from {addr}, {len(data)} bytes")
