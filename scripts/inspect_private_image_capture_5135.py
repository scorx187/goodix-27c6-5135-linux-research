#!/usr/bin/env python3
"""Inspect/decode a PRIVATE decrypted Goodix 27c6:5135 image capture.

The input file is expected to be TLS plaintext saved locally from a successful
command-0x20 capture. The script never prints image bytes or pixel values.

It reports framing, tests several CRC32/MPEG candidate domains/byte orders,
and can decode the 7680-byte packed plane to a local PGM only when explicitly
requested.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path

WIDTH = 80
HEIGHT = 64
PIXELS = WIDTH * HEIGHT
PACKED_LEN = PIXELS * 12 // 8
TOTAL_LEN = 7693
PROTO_PAYLOAD_LEN = 7689


def crc32_mpeg(data: bytes) -> int:
    crc = 0xFFFFFFFF
    poly = 0x04C11DB7
    for byte in data:
        crc ^= byte << 24
        for _ in range(8):
            if crc & 0x80000000:
                crc = ((crc << 1) ^ poly) & 0xFFFFFFFF
            else:
                crc = (crc << 1) & 0xFFFFFFFF
    return crc


def decode_12bit(data: bytes) -> list[int]:
    if len(data) != PACKED_LEN or len(data) % 6:
        raise ValueError("packed image must be exactly 7680 bytes")
    out: list[int] = []
    for i in range(0, len(data), 6):
        c = data[i:i+6]
        out.append(((c[0] & 0x0F) << 8) | c[1])
        out.append((c[3] << 4) | (c[0] >> 4))
        out.append(((c[5] & 0x0F) << 8) | c[2])
        out.append((c[4] << 4) | (c[5] >> 4))
    if len(out) != PIXELS:
        raise AssertionError("decoded pixel count mismatch")
    if any(not 0 <= v <= 4095 for v in out):
        raise AssertionError("decoded pixel range mismatch")
    return out


def private_write_pgm(path: Path, pixels: list[int]):
    path.parent.mkdir(parents=True, exist_ok=True)
    os.chmod(path.parent, 0o700)
    # P5, 16-bit big-endian sample storage per Netpbm for maxval > 255.
    header = f"P5\n{WIDTH} {HEIGHT}\n4095\n".encode("ascii")
    body = bytearray()
    for value in pixels:
        body += int(value).to_bytes(2, "big")
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "wb") as f:
        f.write(header)
        f.write(body)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("capture", type=Path, help="private *.tls-plain.bin")
    ap.add_argument("--write-pgm", type=Path,
                    help="optional PRIVATE output; file is created mode 0600")
    args = ap.parse_args()

    data = args.capture.read_bytes()
    print("Total plaintext length :", len(data))
    if len(data) != TOTAL_LEN:
        raise SystemExit("unexpected total length")

    cmd = data[0]
    declared = int.from_bytes(data[1:3], "little")
    trailer = data[-1]
    if cmd != 0x20 or declared != 7690 or trailer != 0x88:
        raise SystemExit("unexpected Goodix image framing")

    payload = data[3:-1]
    if len(payload) != PROTO_PAYLOAD_LEN:
        raise SystemExit("unexpected protocol payload length")

    meta = payload[:5]
    packed = payload[5:-4]
    stored_crc = payload[-4:]

    print("Command                : 0x20")
    print("Protocol trailer       : 0x88")
    print("Image metadata length  :", len(meta))
    print("Packed image length    :", len(packed))
    print("Image CRC length       :", len(stored_crc))
    print("Biometric bytes printed: NO")

    candidates = {
        "packed-only": packed,
        "metadata+packed": meta + packed,
        "protocol-payload-without-crc": payload[:-4],
    }
    matches = []
    for name, blob in candidates.items():
        calc = crc32_mpeg(blob)
        if stored_crc == calc.to_bytes(4, "little"):
            matches.append(name + " / little-endian")
        if stored_crc == calc.to_bytes(4, "big"):
            matches.append(name + " / big-endian")

    if matches:
        print("CRC32/MPEG candidate   : PASS")
        for match in matches:
            print("CRC match domain       :", match)
    else:
        print("CRC32/MPEG candidate   : NO MATCH")
        print("CRC domain remains     : unresolved")

    pixels = decode_12bit(packed)
    print("Decoded pixels         :", len(pixels))
    print("Geometry               : 80x64")
    print("12-bit range check     : PASS")

    if args.write_pgm:
        private_write_pgm(args.write_pgm, pixels)
        print("Private PGM written    :", args.write_pgm)
        print("PGM mode               : 0600")
    else:
        print("PGM written            : NO")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
