#!/usr/bin/env python3
"""Goodix 27c6:5135 ChicagoHS CFG70 dry-run builder.

No config upload is implemented in this file.
Private OTP/config contents are not printed by default.
"""

from __future__ import annotations
import argparse, os, stat
from dataclasses import dataclass
from pathlib import Path

CFG_LEN = 224
PREFIX = bytes.fromhex("70 11 74 85")
REG_FIELDS = {
    0x005C: (0x71, 0x73),
    0x0220: (0x75, 0x77),
    0x0236: (0x79, 0x7B),
    0x0238: (0x7D, 0x7F),
    0x023A: (0x81, 0x83),
    0x0082: (0xAD, 0xAF),
}
STATIC_EXPECTED = {
    0x005C: 0x0180,
    0x0220: 0x0808,
    0x0236: 0x0080,
    0x0238: 0x0080,
    0x023A: 0x0080,
    0x0082: 0x1580,
}

@dataclass(frozen=True)
class OtpCalibration:
    tcode: int
    fdt_delta: int
    fdt_offset: int
    dac0: int
    dac1: int
    dac2: int
    dac3: int

def crc8(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (((crc << 1) ^ 0x07) if (crc & 0x80) else (crc << 1)) & 0xff
    return crc

def inv_crc8(data: bytes) -> int:
    return (~crc8(data)) & 0xff

def validate_otp(otp: bytes):
    if len(otp) != 64:
        raise ValueError("OTP must be exactly 64 bytes")
    checks = {
        "CP": (inv_crc8(otp[0:11] + otp[36:40]), otp[60]),
        "MT": (inv_crc8(otp[20:28] + otp[29:36] + otp[40:50] + otp[54:56]), otp[63]),
        "FT": (inv_crc8(otp[11:20] + otp[28:29] + otp[50:54] + otp[56:60] + otp[62:63]), otp[61]),
        "FT_DAC": (inv_crc8(otp[50:54]), otp[62]),
        "MT_DAC": (inv_crc8(otp[46:50]), otp[22]),
    }
    failed = [k for k, (calc, stored) in checks.items() if calc != stored]
    if failed:
        raise ValueError("OTP CRC validation failed: " + ", ".join(failed))
    if otp[46:50] != otp[50:54]:
        raise ValueError("OTP DAC mirror mismatch")

def majority_fdt_offset(encoded: int) -> int:
    if encoded == 0:
        return 0
    a, b, c = encoded & 3, (encoded >> 2) & 3, (encoded >> 4) & 3
    if a == c or a == b:
        return a
    if c == b:
        return c
    return 0

def parse_otp(otp: bytes) -> OtpCalibration:
    validate_otp(otp)
    b42 = otp[42]
    tcode = ((b42 >> 4) + 1) * 16 + 64
    fdt_delta = (int((((b42 & 0x0f) + 2) * 25600) / tcode / 3) >> 4) & 0xff
    return OtpCalibration(
        tcode=tcode,
        fdt_delta=fdt_delta,
        fdt_offset=majority_fdt_offset(otp[27]),
        dac0=(otp[46] << 4) | 0x08,
        dac1=otp[47],
        dac2=otp[48],
        dac3=otp[49],
    )

def u16le(data, off):
    return int(data[off]) | (int(data[off+1]) << 8)

def put_u16le(data, off, value):
    data[off] = value & 0xff
    data[off+1] = (value >> 8) & 0xff

def checksum(data):
    if len(data) != CFG_LEN:
        raise ValueError("config length must be 224")
    total = 0xa5a5
    for off in range(0, 222, 2):
        total = (total + int(data[off]) + (int(data[off+1]) << 8)) & 0xffff
    return (-total) & 0xffff

def validate_template(template: bytes):
    if len(template) != CFG_LEN or not template.startswith(PREFIX):
        raise ValueError("invalid CFG70 template")
    for reg, (addr_off, value_off) in REG_FIELDS.items():
        if u16le(template, addr_off) != reg:
            raise ValueError(f"register layout mismatch for 0x{reg:04x}")
        if u16le(template, value_off) != STATIC_EXPECTED[reg]:
            raise ValueError(f"static value mismatch for 0x{reg:04x}")

def build_runtime_config(template: bytes, cal: OtpCalibration) -> bytes:
    validate_template(template)
    cfg = bytearray(template)
    put_u16le(cfg, REG_FIELDS[0x005C][1], cal.tcode)
    put_u16le(cfg, REG_FIELDS[0x0220][1], cal.dac0)
    put_u16le(cfg, REG_FIELDS[0x0236][1], cal.dac1)
    put_u16le(cfg, REG_FIELDS[0x0238][1], cal.dac2)
    put_u16le(cfg, REG_FIELDS[0x023A][1], cal.dac3)
    put_u16le(cfg, REG_FIELDS[0x0082][1], (cal.fdt_delta << 8) | 0x80)
    put_u16le(cfg, 222, checksum(cfg))
    if u16le(cfg, 222) != checksum(cfg):
        raise AssertionError("checksum verification failed")
    return bytes(cfg)

def read_blob(path: Path, expected: int) -> bytes:
    raw = path.read_bytes()
    if len(raw) == expected:
        return raw
    text = raw.decode("ascii")
    cleaned = "".join(text.replace("0x", "").replace("0X", "").split()).replace(":", "")
    data = bytes.fromhex(cleaned)
    if len(data) != expected:
        raise ValueError(f"{path}: expected {expected} bytes")
    return data

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--template", type=Path, required=True)
    ap.add_argument("--otp-file", type=Path, required=True,
                    help="private local 64-byte OTP raw/hex file")
    ap.add_argument("--reference", type=Path,
                    help="optional private Windows 224-byte runtime reference")
    args = ap.parse_args()

    template = read_blob(args.template, 224)
    otp = read_blob(args.otp_file, 64)
    cal = parse_otp(otp)
    runtime = build_runtime_config(template, cal)

    print("OTP CRC validation    : PASS")
    print("DAC mirror validation : PASS")
    print("Template family       : CFG70")
    print("Generated length      : 224")
    print("Generated checksum    : PASS")
    print("Config upload called  : NO")

    if args.reference:
        reference = read_blob(args.reference, 224)
        print("Windows reference     :", "MATCH" if runtime == reference else "MISMATCH")
        return 0 if runtime == reference else 2
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
