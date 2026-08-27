# Next steps

## Immediate blocker: recover exact 224-byte ChicagoHS MCU config

We know Windows sends:

```text
command 0x90
length  0xe0 (224 bytes)
```

The remaining question is the exact payload/template.

## Preferred proof paths

### Path A — recover outgoing Windows packet

Use an existing detailed Windows driver trace, ETW/EVTX logging, or USBPcap/Wireshark during a normal cold driver initialization. Capture the host-to-device command `0x90` and extract only its 224-byte payload.

This is the strongest evidence because it avoids guessing the DLL selection logic.

### Path B — reverse the config descriptor/table in `gfusb.dll`

The DLL contains repeated 224+-byte templates but no direct x64 references. Locate pointers/descriptors that reference the template copies, then trace the descriptor chosen by `device_enable_init_by_chip(0x2504)` / ChicagoHS.

### Path C — dynamic Windows debugging

If needed, instrument the Windows driver/user-mode component immediately before `UsbSendDataToDevice` for command `0x90` and dump the 224-byte application payload locally.

## After payload recovery

1. Save an **offline redacted reference** (do not include secrets/biometrics).
2. Verify the six default tuples exist at expected offsets.
3. Apply device OTP substitutions in memory.
4. Recompute the Goodix MCU config checksum over the confirmed 224-byte shape if required by the exact Windows payload.
5. Compare final bytes against the captured Windows upload.
6. Only if byte parity is achieved, perform one volatile Linux `upload_config_mcu`.
7. Read MCU state and `0x0220`.
8. Expected verification:

```text
have config = 1
0x0220 = f80b
```

9. Restore TLS using the existing factory PSK.
10. Replicate Windows image-mode sequence and capture one local frame.
11. Do not publish the frame.
12. Move the working transport/state machine into libfprint/fprintd integration.
