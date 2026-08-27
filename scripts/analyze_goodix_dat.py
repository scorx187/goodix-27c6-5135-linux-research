#!/usr/bin/env python3
"""Offline layout check for a Goodix goodix.dat file.

Does not access the fingerprint sensor. Keep the input file private.
"""

from pathlib import Path
import argparse


def crc32_mpeg2(data: bytes) -> int:
    crc = 0xFFFFFFFF
    poly = 0x04C11DB7
    for byte in data:
        crc ^= byte << 24
        for _ in range(8):
            crc = ((crc << 1) ^ poly) & 0xFFFFFFFF if crc & 0x80000000 else (crc << 1) & 0xFFFFFFFF
    return crc


p = argparse.ArgumentParser()
p.add_argument("file", type=Path)
a = p.parse_args()
d = a.file.read_bytes()

OTP, FDT, NAV, IMAGE, CRC = 64, 12, 3200, 10240, 4
expected = OTP + FDT + NAV + IMAGE + CRC

print("size:", len(d))
print("expected ChicagoHS layout:", expected)
if len(d) != expected:
    raise SystemExit("unexpected size")

stored = int.from_bytes(d[-4:], "little")
calc = crc32_mpeg2(d[:-4])

print("OTP:", OTP)
print("FDT:", FDT)
print("NAV:", NAV)
print("IMAGE:", IMAGE)
print("CRC:", CRC)
print("stored CRC:", hex(stored))
print("calculated:", hex(calc))
print("match:", stored == calc)
