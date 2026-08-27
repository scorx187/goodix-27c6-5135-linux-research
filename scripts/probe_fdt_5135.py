#!/usr/bin/env python3
"""Public-safe FDT manual/down/up probe for Goodix USB 27c6:5135.

Requires a local checkout of goodix-fp-dump, a private factory PSK file, a
private local goodix.dat, and the 12-byte per-unit FDT-up threshold set from a
local Windows trace. Private seed/threshold values are never printed.

No firmware erase/write, no PSK write, no config upload, no image capture.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import socket
import struct
import subprocess
import sys
import time

PID = 0x5135
EXPECTED_FW = "GF_HC460SEC_APP_12508"
EXPECTED_CHIP = bytes.fromhex("a2042500")
GOODIX_DAT_SIZE = 13520
EVENT_TIMEOUT = 45


def crc32_mpeg(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= b << 24
        for _ in range(8):
            crc = (((crc << 1) ^ 0x04C11DB7) if crc & 0x80000000 else (crc << 1)) & 0xFFFFFFFF
    return crc


def load_seed(path: Path) -> bytes:
    data = path.read_bytes()
    if len(data) != GOODIX_DAT_SIZE:
        raise ValueError("unexpected goodix.dat size")
    if int.from_bytes(data[-4:], "little") != crc32_mpeg(data[:-4]):
        raise ValueError("goodix.dat CRC32/MPEG mismatch")
    seed = data[64:76]
    if len(seed) != 12 or not all(seed[i] == seed[i + 1] for i in range(0, 12, 2)):
        raise ValueError("unexpected ChicagoHS FDT manual-seed structure")
    return seed


def parse_fdt(payload: bytes):
    if len(payload) < 16:
        raise ValueError("short FDT event")
    irq = int.from_bytes(payload[0:2], "little")
    touch = int.from_bytes(payload[2:4], "little")
    zones = [int.from_bytes(payload[4+i*2:6+i*2], "little") for i in range(6)]
    return irq, touch, zones


def derive_down(zones) -> bytes:
    out = bytearray()
    for raw in zones:
        t = raw // 2
        if not 0 <= t <= 0xFF:
            raise ValueError("derived FDT-down threshold out of range")
        out += bytes((0x80, t))
    return bytes(out)


def free_port() -> int:
    s = socket.socket()
    try:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]
    finally:
        s.close()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--goodix-fp-dump", type=Path, required=True)
    ap.add_argument("--psk-file", type=Path, required=True)
    ap.add_argument("--goodix-dat", type=Path, required=True)
    ap.add_argument("--fdt-up-hex", required=True,
                    help="private local 12-byte FDT-up thresholds; not printed")
    args = ap.parse_args()

    psk = args.psk_file.read_text().strip().lower()
    if not re.fullmatch(r"[0-9a-f]{64}", psk):
        raise SystemExit("invalid private PSK file format")
    cleaned = re.sub(r"[^0-9a-fA-F]", "", args.fdt_up_hex)
    up_regs = bytes.fromhex(cleaned)
    if len(up_regs) != 12:
        raise SystemExit("--fdt-up-hex must decode to exactly 12 bytes")
    seed = load_seed(args.goodix_dat)

    sys.path.insert(0, str(args.goodix_fp_dump.resolve()))
    import goodix  # type: ignore
    import protocol  # type: ignore
    import tool  # type: ignore

    def command_event(device, command: int, payload: bytes, timeout: float):
        device.protocol.write(goodix.encode_message_pack(goodix.encode_message_protocol(payload, command)))
        goodix.check_ack(
            goodix.check_message_protocol(goodix.check_message_pack(device.protocol.read()), goodix.COMMAND_ACK),
            command,
        )
        return goodix.check_message_protocol(
            goodix.check_message_pack(device.protocol.read(timeout=timeout)), command
        )

    device = server = bridge = None
    try:
        print("Private FDT seed printed: NO")
        print("Private FDT-up printed : NO")
        device = goodix.Device(PID, protocol.USBProtocol)

        # Verified 5135 activation-state ordering.
        device.nop()
        device.tls_successfully_established()
        device.nop()
        device.enable_chip(True)
        device.nop()
        fw = device.firmware_version()
        if fw != EXPECTED_FW:
            raise RuntimeError(f"firmware guard failed: {fw}")
        reset = device.reset(True, False, 20)
        if reset != (True, 2048):
            raise RuntimeError(f"unexpected reset result: {reset}")
        time.sleep(0.3)
        chips = [device.read_sensor_register(0x0000, 4) for _ in range(3)]
        if any(c != EXPECTED_CHIP for c in chips):
            raise RuntimeError("chip guard failed")
        print("Activation/chip guard : PASS")

        port = free_port()
        server = subprocess.Popen([
            "openssl", "s_server", "-accept", str(port), "-nocert", "-tls1_2",
            "-cipher", "PSK-AES128-GCM-SHA256", "-psk_identity", "Client_identity",
            "-psk", psk, "-quiet",
        ], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        time.sleep(0.25)
        if server.poll() is not None:
            raise RuntimeError("OpenSSL TLS server exited before handshake")
        bridge = socket.create_connection(("127.0.0.1", port), timeout=8)
        tool.connect_device(device, bridge)
        device.tls_successfully_established()
        device.nop()
        print("TLS                   : PASS")

        print("Keep finger OFF sensor.")
        manual = command_event(device, goodix.COMMAND_MCU_SWITCH_TO_FDT_MODE, b"\x0d\x01" + seed, EVENT_TIMEOUT)
        irq, touch, zones = parse_fdt(manual)
        if touch != 0:
            raise RuntimeError("touch detected during manual baseline")
        down_regs = derive_down(zones)
        print(f"Manual FDT 0x36       : PASS (IRQ 0x{irq:04x})")

        input("Keep finger OFF; press Enter to arm finger-down...")
        stamp = (time.monotonic_ns() // 1000) & 0xFFFF
        print("PLACE FINGER ON SENSOR NOW.")
        down = command_event(
            device, goodix.COMMAND_MCU_SWITCH_TO_FDT_DOWN,
            b"\x08\x01" + down_regs + struct.pack("<H", stamp), EVENT_TIMEOUT,
        )
        irq, touch, _ = parse_fdt(down)
        count = (touch & 0x3F).bit_count()
        if count == 0:
            raise RuntimeError("finger-down event had zero touched zones")
        print(f"FDT-down 0x32         : PASS (IRQ 0x{irq:04x}, zones {count}/6)")

        input("KEEP finger on sensor; press Enter, then lift it...")
        print("LIFT FINGER NOW.")
        up = command_event(device, goodix.COMMAND_MCU_SWITCH_TO_FDT_UP, b"\x0a\x02" + up_regs, EVENT_TIMEOUT)
        irq, touch, _ = parse_fdt(up)
        if touch != 0:
            raise RuntimeError(f"finger-up touchflag not zero: 0x{touch:04x}")
        print(f"FDT-up 0x34           : PASS (IRQ 0x{irq:04x}, zones 0/6)")
        print("FDT DOWN/UP PROBE     : SUCCESS")
        return 0
    finally:
        if bridge is not None:
            try: bridge.close()
            except Exception: pass
        if server is not None:
            try:
                server.terminate(); server.wait(timeout=2)
            except Exception:
                try: server.kill()
                except Exception: pass
        if device is not None:
            try: device.disconnect()
            except Exception as exc: print(f"Disconnect warning: {type(exc).__name__}: {exc}")


if __name__ == "__main__":
    raise SystemExit(main())
