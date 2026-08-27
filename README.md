# Goodix 27c6:5135 Linux research

Reverse-engineering and Linux enablement research for the **Goodix USB fingerprint sensor `27c6:5135`** with firmware **`GF_HC460SEC_APP_12508`**.

> Status: end-to-end transport bring-up is far along; **not yet a production libfprint driver**.

## Current milestone

As of 2026-08-27 Linux has successfully reproduced:

- exact device identification (`0x2504`, ChicagoHS, sensor type 12, 80x64);
- factory TLS 1.2 PSK session without changing the factory key;
- exact 224-byte CFG70 runtime reconstruction from live OTP;
- one controlled, volatile `0x90` config upload;
- FDT manual/down/up finger detection;
- command `0x20` image request and ACK;
- TLS-protected image response transport and decryption;
- Goodix image-message framing to a private 7693-byte TLS plaintext frame.

The immediate next task is **image CRC validation and 12-bit unpacking**, not config/TLS/FDT discovery.

See [`AI_START_HERE.md`](AI_START_HERE.md) and [`docs/CURRENT_STATUS_2026-08-27.md`](docs/CURRENT_STATUS_2026-08-27.md).

## Confirmed hardware

| Item | Value |
|---|---|
| USB VID:PID | `27c6:5135` |
| Firmware | `GF_HC460SEC_APP_12508` |
| Raw chip response | `a2042500` |
| Logical chip ID | `0x2504` |
| Sensor path | ChicagoHS / ChicagoHU |
| Sensor type | `12` |
| Geometry | `80 x 64` |
| Pixels | `5120` |
| USB Bulk IN / OUT | `0x81` / `0x01` |

## Major protocol results

### Runtime config

Windows sends command `0x90` with exactly `224` bytes. The tested device uses the CFG70 family. Linux can rebuild the exact private Windows runtime payload from live OTP and the proven checksum rule. The full per-device payload is intentionally not published. Public evidence is preserved in [`docs/CFG70_RUNTIME_PROOF_2026-08-27.md`](docs/CFG70_RUNTIME_PROOF_2026-08-27.md), [`docs/ISSUE_1_RESOLUTION_2026-08-27.md`](docs/ISSUE_1_RESOLUTION_2026-08-27.md), and [`docs/LINUX_CFG70_UPLOAD_PROOF_2026-08-27.md`](docs/LINUX_CFG70_UPLOAD_PROOF_2026-08-27.md).

### TLS

```text
TLS 1.2
PSK-AES128-GCM-SHA256
identity Client_identity
```

The factory key remains private/local and is not reprovisioned.

### FDT

Linux reproduces:

```text
0x36 manual baseline
0x32 finger-down ACK/event
0x34 finger-up ACK/event
```

### Image transport

```text
0x20 payload 01 00
normal ACK
second Goodix pack flags 0xb0, length 7722
TLS plaintext 7693 bytes
inner command 0x20
inner trailer 0x88 (no-checksum mode)
inner payload 7689 bytes
```

The frame shape is consistent with 5 bytes image metadata + 7680 packed 12-bit pixels + 4 bytes image CRC.

## Safety and privacy

This project intentionally avoids firmware erase/flash and PSK provisioning. Never commit factory secrets or real biometric material.

See [`docs/SAFETY.md`](docs/SAFETY.md).

## Repository map

- `AI_START_HERE.md` — canonical next-session handoff.
- `docs/CURRENT_STATUS_2026-08-27.md` — current proof matrix.
- `docs/CFG70_RUNTIME_PROOF_2026-08-27.md` — exact Windows CFG70 runtime reconstruction proof.
- `docs/ISSUE_1_RESOLUTION_2026-08-27.md` — original Issue #1/config blocker resolution.
- `docs/FDT_5135_PROOF_2026-08-27.md` — FDT manual/down/up proof.
- `docs/IMAGE_TRANSPORT_5135_PROOF_2026-08-27.md` — first image transport proof.
- `docs/LINUX_CFG70_UPLOAD_PROOF_2026-08-27.md` — config reconstruction/upload proof.
- `docs/FAILURES_AND_RECOVERIES_2026-08-27.md` — negative results and recoveries.
- `docs/DEVELOPER_ROADMAP.md` — path to libfprint.
- `docs/LEARNING_GUIDE.md` — conceptual guide for future developers.
- `scripts/inspect_private_image_capture_5135.py` — private local framing/CRC/decode inspector.
- `scripts/build_cfg70_dry_run.py` — public-safe local CFG70 builder.
- `scripts/upload_cfg70_5135.py` — guarded public-safe config uploader.
- `scripts/recover_transport_5135.sh` — sysfs USB transport recovery.

## Related work

- `goodix-fp-linux-dev/goodix-fp-dump` provides shared Goodix protocol primitives and the known 12-bit unpacker.
- `goodix-fp-linux-dev/sigfm` is relevant when evaluating matcher integration.
- ChicagoHS/chip `0x2504` SPI projects can provide sensor-layer comparison, but they are **not** proof for USB `27c6:5135` transport/firmware behavior.

This repository does not claim priority or "world first" status without an external literature/repository survey; it records reproducible evidence for this exact tested USB device.
