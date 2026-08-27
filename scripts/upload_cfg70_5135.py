#!/usr/bin/env python3
"""Controlled public-safe Goodix 27c6:5135 CFG70 uploader.

Default mode validates only. Upload requires an exact confirmation string and
a private local Windows runtime reference file. The full config is never
printed by this wrapper.
"""

from __future__ import annotations
import argparse, contextlib, importlib.util, io, sys
from pathlib import Path

PID = 0x5135
EXPECTED_FW = "GF_HC460SEC_APP_12508"
EXPECTED_CHIP = bytes.fromhex("a2042500")
CONFIRM = "EXACT_PRIVATE_REFERENCE_MATCH"

def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    try:
        spec.loader.exec_module(mod)
    except Exception:
        sys.modules.pop(name, None)
        raise
    return mod

def safe_disconnect(d):
    try:
        d.disconnect()
    except Exception as exc:
        print(f"Disconnect warning: {type(exc).__name__}: {exc}", file=sys.stderr)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--template", type=Path, required=True)
    ap.add_argument("--reference", type=Path, required=True, help="private 224-byte Windows runtime reference")
    ap.add_argument("--builder", type=Path, required=True)
    ap.add_argument("--goodix-fp-dump", type=Path, required=True)
    ap.add_argument("--confirm-upload")
    args = ap.parse_args()

    do_upload = args.confirm_upload == CONFIRM
    if args.confirm_upload is not None and not do_upload:
        raise SystemExit("invalid confirmation text; no upload")

    sys.path.insert(0, str(args.goodix_fp_dump.resolve()))
    import goodix, protocol
    builder = load_module("cfg70_builder", args.builder)

    template = builder.read_blob(args.template, 224)
    reference = builder.read_blob(args.reference, 224)
    builder.validate_template(template)

    d = goodix.Device(PID, protocol.USBProtocol)
    try:
        d.nop(); d.tls_successfully_established(); d.nop(); d.enable_chip(True); d.nop()
        fw = d.firmware_version()
        if fw != EXPECTED_FW:
            raise RuntimeError(f"firmware guard failed: {fw}")
        reset_result = d.reset(True, False, 20)
        if reset_result != (True, 2048):
            raise RuntimeError(f"reset guard failed: {reset_result}")
        chip = d.read_sensor_register(0x0000, 4)
        if chip != EXPECTED_CHIP:
            raise RuntimeError(f"chip guard failed: {chip.hex()}")

        otp = bytes(d.read_otp())
        cal = builder.parse_otp(otp)
        runtime = builder.build_runtime_config(template, cal)
        if runtime != reference:
            raise RuntimeError("private Windows reference mismatch")
        if builder.u16le(runtime, 222) != builder.checksum(runtime):
            raise RuntimeError("checksum mismatch")

        print("Firmware guard         : PASS")
        print("Chip guard             : PASS")
        print("OTP validation         : PASS")
        print("Generated length       : 224")
        print("Generated checksum     : PASS")
        print("Private reference      : EXACT MATCH")
        before = d.read_sensor_register(0x0220, 2)
        print("0x0220 before          :", before.hex())

        if not do_upload:
            print("VALIDATION ONLY — NO CONFIG UPLOAD")
            print(f"Upload requires --confirm-upload {CONFIRM}")
            return 0

        sink = io.StringIO()
        with contextlib.redirect_stdout(sink):
            ok = d.upload_config_mcu(runtime)
        if ok is not True:
            raise RuntimeError("device rejected command 0x90")

        after = d.read_sensor_register(0x0220, 2)
        expected = cal.dac0.to_bytes(2, "little")
        print("upload_config_mcu      : SUCCESS")
        print("0x0220 after           :", after.hex())
        print("Expected 0x0220        :", expected.hex())
        if after != expected:
            raise RuntimeError("post-upload calibration mismatch")
        print("CONTROLLED 0x90 UPLOAD ACCEPTED")
        return 0
    finally:
        safe_disconnect(d)

if __name__ == "__main__":
    raise SystemExit(main())
