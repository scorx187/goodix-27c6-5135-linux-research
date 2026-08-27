#!/usr/bin/env python3
"""Inspect/decode a PRIVATE decrypted Goodix 27c6:5135 image capture.

The input file is expected to be TLS plaintext saved locally from a successful
command-0x20 capture. The script never prints image bytes, pixel values, or CRC
values.

For the 5135 image path, the Windows driver checks a 7684-byte block containing
7680 packed RAW12 bytes followed by a 4-byte CRC field. The CRC algorithm is
CRC-32/MPEG-2 (poly 0x04C11DB7, init 0xFFFFFFFF, non-reflected, xorout 0), and
the four stored bytes use the 16-bit-half-swapped ordering reconstructed from
the Windows checker.

A private PGM can be written only after the Windows-compatible CRC check passes.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path

WIDTH = 80
HEIGHT = 64
PIXELS = WIDTH * HEIGHT
PACKED_LEN = PIXELS * 12 // 8
CRC_LEN = 4
IMAGE_BLOCK_LEN = PACKED_LEN + CRC_LEN
TOTAL_LEN = 7693
PROTO_PAYLOAD_LEN = 7689


def crc32_mpeg(data: bytes) -> int:
    """CRC-32/MPEG-2: poly 0x04C11DB7, init FFFFFFFF, no reflection/xorout."""
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


def windows_image_crc_value(stored: bytes) -> int:
    """Reconstruct the 32-bit chip CRC exactly as the Windows checker does.

    For stored bytes [a, b, c, d], the Windows checker forms:
        c<<24 | d<<16 | a<<8 | b

    Equivalently, the stored field is the CRC's two 16-bit halves swapped.
    """
    if len(stored) != CRC_LEN:
        raise ValueError("image CRC field must be exactly 4 bytes")
    a, b, c, d = stored
    return (c << 24) | (d << 16) | (a << 8) | b


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
    image_block = payload[5:]
    if len(image_block) != IMAGE_BLOCK_LEN:
        raise SystemExit("unexpected Windows image-checker block length")

    packed = image_block[:-CRC_LEN]
    stored_crc = image_block[-CRC_LEN:]

    print("Command                : 0x20")
    print("Protocol trailer       : 0x88")
    print("Image metadata length  :", len(meta))
    print("Windows checker block  :", len(image_block))
    print("Packed image length    :", len(packed))
    print("Image CRC length       :", len(stored_crc))
    print("Biometric bytes printed: NO")
    print("CRC values printed     : NO")

    host_crc = crc32_mpeg(packed)
    chip_crc = windows_image_crc_value(stored_crc)
    crc_ok = host_crc == chip_crc

    print("CRC algorithm           : CRC-32/MPEG-2")
    print("CRC domain              : packed RAW12 only (7680 bytes)")
    print("CRC field ordering      : 16-bit-half swapped")
    print("Windows image CRC check :", "PASS" if crc_ok else "FAIL")

    pixels = decode_12bit(packed)
    print("Decoded pixels         :", len(pixels))
    print("Geometry               : 80x64")
    print("12-bit range check     : PASS")

    if args.write_pgm:
        if not crc_ok:
            raise SystemExit("refusing PGM write because Windows image CRC verification failed")
        private_write_pgm(args.write_pgm, pixels)
        print("Private PGM written    :", args.write_pgm)
        print("PGM mode               : 0600")
    else:
        print("PGM written            : NO")

    return 0 if crc_ok else 2


if __name__ == "__main__":
    raise SystemExit(main())
