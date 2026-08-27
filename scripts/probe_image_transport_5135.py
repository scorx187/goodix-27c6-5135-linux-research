#!/usr/bin/env python3
"""Public-safe first image-transport probe for Goodix USB 27c6:5135.

Captures one TLS-protected command-0x20 frame after FDT-down, decrypts it through
an existing factory PSK, prints metadata only, and optionally saves TLS plaintext
to a private mode-0600 file. It never prints image bytes/pixels.

No firmware erase/write, no PSK write, no config upload, no register writes.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import pwd
import re
import select
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


def invoking_home() -> Path:
    user = os.environ.get("SUDO_USER")
    if user and user != "root":
        try:
            return Path(pwd.getpwnam(user).pw_dir)
        except KeyError:
            pass
    return Path.home()


def tls_record_offset(data: bytes) -> int:
    for off in range(0, min(32, max(0, len(data) - 5)) + 1):
        if data[off:off+3] != b"\x17\x03\x03":
            continue
        n = int.from_bytes(data[off+3:off+5], "big")
        if off + 5 + n <= len(data):
            return off
    raise ValueError("complete TLS 1.2 application-data record not found")


def read_tls_plaintext(proc: subprocess.Popen, timeout=5.0) -> bytes:
    if proc.stdout is None:
        raise RuntimeError("OpenSSL stdout unavailable")
    fd = proc.stdout.fileno()
    out = bytearray()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        ready, _, _ = select.select([fd], [], [], min(0.5, deadline - time.monotonic()))
        if not ready:
            if out:
                break
            continue
        chunk = os.read(fd, 65536)
        if not chunk:
            break
        out += chunk
        if len(out) >= 7693:
            break
    if not out:
        raise TimeoutError("no TLS plaintext received")
    return bytes(out)


def secure_save(path: Path, data: bytes):
    path.parent.mkdir(parents=True, exist_ok=True)
    os.chmod(path.parent, 0o700)
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "wb") as f:
        f.write(data)
        f.flush()
        os.fsync(f.fileno())


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--goodix-fp-dump", type=Path, required=True)
    ap.add_argument("--psk-file", type=Path, required=True)
    ap.add_argument("--goodix-dat", type=Path, required=True)
    ap.add_argument("--fdt-up-hex", required=True,
                    help="private local 12-byte FDT-up thresholds; not printed")
    ap.add_argument("--capture-dir", type=Path,
                    default=invoking_home() / "goodix-private" / "captures" / "5135")
    ap.add_argument("--no-save", action="store_true",
                    help="decrypt/validate metadata but do not persist plaintext")
    args = ap.parse_args()

    psk = args.psk_file.read_text().strip().lower()
    if not re.fullmatch(r"[0-9a-f]{64}", psk):
        raise SystemExit("invalid private PSK file format")
    up_regs = bytes.fromhex(re.sub(r"[^0-9a-fA-F]", "", args.fdt_up_hex))
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
        print("Image bytes printed    : NO")
        print("Private FDT data print : NO")
        device = goodix.Device(PID, protocol.USBProtocol)
        device.nop(); device.tls_successfully_established(); device.nop()
        device.enable_chip(True); device.nop()
        fw = device.firmware_version()
        if fw != EXPECTED_FW:
            raise RuntimeError(f"firmware guard failed: {fw}")
        if device.reset(True, False, 20) != (True, 2048):
            raise RuntimeError("reset guard failed")
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
        device.tls_successfully_established(); device.nop()
        print("TLS                   : PASS")

        print("Keep finger OFF sensor.")
        manual = command_event(device, goodix.COMMAND_MCU_SWITCH_TO_FDT_MODE, b"\x0d\x01" + seed, EVENT_TIMEOUT)
        _, touch, zones = parse_fdt(manual)
        if touch != 0:
            raise RuntimeError("touch detected during manual baseline")
        down_regs = derive_down(zones)
        print("Manual FDT 0x36       : PASS")

        input("Keep finger OFF; press Enter to arm FDT-down...")
        stamp = (time.monotonic_ns() // 1000) & 0xFFFF
        print("PLACE FINGER ON SENSOR NOW.")
        down = command_event(device, goodix.COMMAND_MCU_SWITCH_TO_FDT_DOWN,
                             b"\x08\x01" + down_regs + struct.pack("<H", stamp), EVENT_TIMEOUT)
        _, touch, _ = parse_fdt(down)
        zones = (touch & 0x3F).bit_count()
        if zones == 0:
            raise RuntimeError("finger-down event had zero touched zones")
        print(f"FDT-down              : PASS ({zones}/6 zones)")

        input("KEEP finger on sensor; press Enter to capture one frame...")
        # Command 0x20, payload 01 00. Consume normal ACK first.
        device.protocol.write(goodix.encode_message_pack(
            goodix.encode_message_protocol(b"\x01\x00", goodix.COMMAND_MCU_GET_IMAGE)))
        ack = goodix.check_message_pack(device.protocol.read(), goodix.FLAGS_MESSAGE_PROTOCOL)
        goodix.check_ack(goodix.check_message_protocol(ack, goodix.COMMAND_ACK),
                         goodix.COMMAND_MCU_GET_IMAGE)
        print("Image 0x20 ACK        : PASS")

        raw2 = device.protocol.read(timeout=10)
        tls_payload, flags, declared = goodix.decode_message_pack(raw2)
        if len(tls_payload) < declared:
            raise RuntimeError("short second image frame")
        tls_payload = tls_payload[:declared]
        print(f"Second-frame flags    : 0x{flags:02x}")
        print("Second-frame length   :", declared)
        if flags not in (goodix.FLAGS_TRANSPORT_LAYER_SECURITY,
                         goodix.FLAGS_TRANSPORT_LAYER_SECURITY_DATA):
            raise RuntimeError("unexpected non-TLS transport flags")
        off = tls_record_offset(tls_payload)
        print("TLS record offset     :", off)
        bridge.sendall(tls_payload[off:])
        plain = read_tls_plaintext(server)

        print("TLS plaintext length  :", len(plain))
        if len(plain) >= 4:
            cmd = plain[0]
            declared_inner = int.from_bytes(plain[1:3], "little")
            trailer = plain[-1]
            print(f"Inner command         : 0x{cmd:02x}")
            print("Inner declared length :", declared_inner)
            print(f"Inner trailer         : 0x{trailer:02x}")
            if cmd == 0x20 and declared_inner == 7690 and trailer == 0x88 and len(plain) == 7693:
                print("5135 image framing    : PASS")
            else:
                print("5135 image framing    : UNEXPECTED")

        if not args.no_save:
            stamp_s = time.strftime("%Y%m%d-%H%M%S")
            out = args.capture_dir / f"capture-{stamp_s}.tls-plain.bin"
            secure_save(out, plain)
            print("Private capture saved :", out)
            print("Capture mode          : 0600")
        else:
            print("Private capture saved : NO")

        input("KEEP finger on sensor; press Enter, then lift it...")
        print("LIFT FINGER NOW.")
        up = command_event(device, goodix.COMMAND_MCU_SWITCH_TO_FDT_UP,
                           b"\x0a\x02" + up_regs, EVENT_TIMEOUT)
        _, touch, _ = parse_fdt(up)
        if touch != 0:
            raise RuntimeError("FDT-up touchflag did not clear")
        print("FDT-up               : PASS")
        print("FIRST IMAGE TRANSPORT: SUCCESS")
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
