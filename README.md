# Goodix 27c6:5135 Linux research

Reverse-engineering notes and Linux enablement research for the **Goodix USB fingerprint sensor `27c6:5135`** using firmware **`GF_HC460SEC_APP_12508`**.

> Status: research / bring-up. **Not yet a production libfprint driver.**

This repository documents a device-specific investigation intended to help Linux users with the same hardware and to preserve enough context that development can resume without repeating destructive experiments.

## Confirmed hardware

| Item | Value |
|---|---|
| USB VID:PID | `27c6:5135` |
| Vendor | Shenzhen Goodix Technology Co., Ltd. |
| Firmware | `GF_HC460SEC_APP_12508` |
| Chip ID | `0x2504` |
| Windows sensor path | `ChicagoHS` / functions named `ChicagoHU*` |
| Sensor type | `12` |
| Geometry | `80 x 64` |
| Pixel count | `5120` |
| USB transport | CDC-like, bulk IN `0x81`, bulk OUT `0x01` |
| Windows driver | Goodix FP `1.1.125.12`, `gfusb.inf` |

## What works / is proven

- USB communication with the device.
- Firmware version read.
- OTP read.
- Sensor register reads.
- MCU state query.
- Factory TLS 1.2 PSK handshake using `PSK-AES128-GCM-SHA256`.
- A post-handshake encrypted-session NOP.
- Windows profile selection: chip `0x2504`, ChicagoHS, type `12`, `80 x 64`.
- MCU configuration upload command `0x90`.
- Exact MCU configuration payload length: `0xe0` / 224 bytes.
- Static template selection: **CFG70**.
- OTP-derived modification of six logical sensor-register fields.
- Goodix 16-bit little-endian config checksum reconstruction.
- Clean reconstruction of the Windows runtime config with **zero byte differences across all 224 bytes**.

## Major resolved blocker

For the tested `27c6:5135` / HC460 / ChicagoHS unit, the Windows runtime path is now proven:

```text
CFG70 static template
  -> OTP-derived ChicagoHS calibration
  -> checksum recalculation
  -> exact 224-byte runtime configuration
  -> upload with MCU command 0x90
```

The `0x90` command ID and the CFG70 template name are different concepts. The command number is not evidence for a template named CFG90.

Full public evidence:

- [`docs/CFG70_RUNTIME_PROOF_2026-08-27.md`](docs/CFG70_RUNTIME_PROOF_2026-08-27.md)
- [`docs/ISSUE_1_RESOLUTION_2026-08-27.md`](docs/ISSUE_1_RESOLUTION_2026-08-27.md)

## Current blocker

The remaining blocker is implementation on Linux, not identification of the Windows payload.

The next step is a **dry-run ChicagoHS config builder** that:

1. reads OTP,
2. derives the six calibration fields,
3. applies them to CFG70,
4. recomputes the checksum,
5. requires exactly 224 bytes,
6. compares locally against the private Windows-derived reference,
7. performs no USB write until exact parity is proven.

Only after byte-for-byte parity should the project perform a single controlled **volatile** `upload_config_mcu()` and verify MCU state before continuing to TLS/image capture.

See [`docs/NEXT_STEPS.md`](docs/NEXT_STEPS.md).

## Safety boundary

**Do not blindly run 5117/5125 provisioning scripts on this device.**

This project intentionally avoids:

- firmware erase,
- firmware replacement,
- PSK write/re-provisioning,
- arbitrary persistent register writes,
- speculative/unverified MCU config upload,
- publishing biometric frames/templates,
- publishing the factory PSK,
- publishing full OTP/process-memory dumps,
- committing reconstructed per-device 224-byte configs.

The goal is to preserve Windows Hello functionality and the factory firmware/key material throughout the Linux bring-up.

See [`docs/SAFETY.md`](docs/SAFETY.md).

## Repository map

- [`AI_START_HERE.md`](AI_START_HERE.md) — canonical handoff/current state.
- [`docs/CFG70_RUNTIME_PROOF_2026-08-27.md`](docs/CFG70_RUNTIME_PROOF_2026-08-27.md) — byte-for-byte CFG70 runtime reconstruction proof.
- [`docs/ISSUE_1_RESOLUTION_2026-08-27.md`](docs/ISSUE_1_RESOLUTION_2026-08-27.md) — Issue #1 resolution summary.
- [`docs/HARDWARE.md`](docs/HARDWARE.md) — USB, firmware, chip, geometry and cache layout.
- [`docs/WINDOWS_DRIVER.md`](docs/WINDOWS_DRIVER.md) — Windows package identity and reverse-engineered runtime behavior.
- [`docs/PROTOCOL.md`](docs/PROTOCOL.md) — known Goodix USB commands and TLS behavior.
- [`docs/CONFIG_UPLOAD_TRACE.md`](docs/CONFIG_UPLOAD_TRACE.md) — Windows cold-init/config upload evidence.
- [`docs/CONFIG_TEMPLATES.md`](docs/CONFIG_TEMPLATES.md) — CFG30/CFG70/CFG90 analysis and resolved CFG70 selection.
- [`docs/RESEARCH_LOG.md`](docs/RESEARCH_LOG.md) — chronological findings and corrections.
- [`docs/NEXT_STEPS.md`](docs/NEXT_STEPS.md) — evidence-driven next tasks.
- [`docs/SAFETY.md`](docs/SAFETY.md) — destructive operations to avoid.
- [`scripts/`](scripts/) — offline parsers and read-only probes only.

## Image geometry and packing

Windows reports `80 x 64 = 5120` pixels. The encrypted image response ultimately contains 7680 packed image bytes, matching 12-bit packing:

```text
5120 pixels * 12 bits = 7680 bytes
```

The Windows regroup stage outputs `10240` bytes (`5120 * 2`) as 16-bit host samples.

## Contribution policy

Evidence is preferred over guesses. If submitting findings, include:

- exact VID:PID,
- firmware string,
- chip ID if known,
- operating system/driver version,
- command lengths/statuses,
- whether the observation occurred before or after power loss/re-enumeration.

Do **not** post plaintext PSKs, biometric images/templates, private Windows account data, proprietary driver binaries, full OTP, process-memory dumps, or reconstructed per-device runtime configs.

See [`CONTRIBUTING.md`](CONTRIBUTING.md).
