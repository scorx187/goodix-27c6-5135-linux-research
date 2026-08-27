#!/usr/bin/env python3
"""Minimal read-only probe for Goodix 27c6:5135.

Requires the protocol/goodix Python modules from goodix-fp-dump in PYTHONPATH.
No reset, config upload, firmware write, PSK write, or register write is performed.
"""

import goodix
import protocol

PID = 0x5135

device = None
try:
    print("=== Goodix 27c6:5135 read-only state probe ===")
    device = goodix.Device(PID, protocol.USBProtocol)

    fw = device.firmware_version()
    print("Firmware:", fw)

    reg = device.read_sensor_register(0x0220, 2)
    print("0x0220:", reg.hex())

    state = device.query_mcu_state(b"\x55", True)
    print("MCU state:", state.hex())
    print("MCU state length:", len(state))
finally:
    if device is not None:
        try:
            device.disconnect()
        except Exception:
            pass
