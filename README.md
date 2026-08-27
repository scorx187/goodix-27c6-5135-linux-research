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

Windows logs explicitly show:

```text
Get Chip ID: 0x2504
!!!! to Open ChicagoHS, sensor type: 12
sensor info ready, chipid:0x2504, sensorType:12, col:80, row:64
```

## What works on Linux already

- USB communication with the device.
- Firmware version read.
- OTP read.
- Sensor register reads.
- MCU state query.
- Factory TLS 1.2 PSK handshake using `PSK-AES128-GCM-SHA256`.
- A post-handshake encrypted-session NOP.
- Identification of the Windows runtime initialization path.
- Identification of the Windows MCU configuration upload command and **confirmed payload length of `0xe0` (224 bytes)**.

## Current blocker

After a USB re-enumeration/reset, the device loses its **runtime MCU configuration**. The firmware remains intact and responsive, but register `0x0220` becomes `0000` and Windows-equivalent image capture must not be attempted until the exact `ChicagoHS` configuration is reconstructed and uploaded.

The Windows trace proves that configuration upload is command `0x90` with a payload length of `0xe0`:

```text
UsbSendDataToDevice ... cmd0-cmd1-Len-ackt-ec:0x9-0-0xe0-1000-0
...
get ack for cmd 0x90
...
recvd data cmd-len: 0x90-3
...
OTP data valid, download config 1
... have config 1
```

This is an important correction to earlier 256-byte assumptions.

## Safety boundary

**Do not blindly run 5117/5125 provisioning scripts on this device.**

In particular, this project intentionally avoids:

- firmware erase,
- firmware replacement,
- PSK write/re-provisioning,
- arbitrary persistent register writes,
- unverified MCU config upload,
- publishing biometric frames/templates,
- publishing the factory PSK.

The goal is to preserve Windows Hello functionality and the factory firmware/key material throughout the Linux bring-up.

See [`docs/SAFETY.md`](docs/SAFETY.md).

## Repository map

- [`AI_START_HERE.md`](AI_START_HERE.md) — exact handoff/current state for future AI-assisted sessions.
- [`docs/HARDWARE.md`](docs/HARDWARE.md) — USB, firmware, chip, geometry and cache layout.
- [`docs/WINDOWS_DRIVER.md`](docs/WINDOWS_DRIVER.md) — Windows package identity and reverse-engineered runtime behavior.
- [`docs/PROTOCOL.md`](docs/PROTOCOL.md) — known Goodix USB commands and TLS behavior.
- [`docs/CONFIG_UPLOAD_TRACE.md`](docs/CONFIG_UPLOAD_TRACE.md) — Windows cold-init/config upload evidence.
- [`docs/CONFIG_TEMPLATES.md`](docs/CONFIG_TEMPLATES.md) — CFG30/CFG70/CFG90 investigation.
- [`docs/RESEARCH_LOG.md`](docs/RESEARCH_LOG.md) — chronological findings and corrections.
- [`docs/NEXT_STEPS.md`](docs/NEXT_STEPS.md) — evidence-driven next tasks.
- [`docs/SAFETY.md`](docs/SAFETY.md) — destructive operations to avoid.
- [`scripts/`](scripts/) — offline parsers and read-only probes only.

## Important findings so far

### Factory TLS works

The original factory host PSK recovered locally from the Windows Goodix cache has been accepted by the sensor under Linux. TLS 1.2 with `PSK-AES128-GCM-SHA256` completes and a secured-session command succeeds.

The PSK itself is intentionally **not included** in this repository.

### Windows cold initialization requires config upload

A cold/re-enumerated sensor reports no configuration. Windows then:

1. identifies chip `0x2504`,
2. opens `ChicagoHS` sensor type `12`,
3. reads/validates OTP,
4. applies OTP-specific register values,
5. resets the sensor and sets idle,
6. writes DAC registers,
7. uploads the MCU config using command `0x90`, length `0xe0`,
8. reports `have config 1`,
9. establishes TLS and rebuilds image/FDT bases.

### OTP-derived calibration values observed on this unit

The Windows logs showed these default-to-calibrated transformations:

```text
0x0220: 0x0808 -> 0x0bf8
0x0236: 0x0080 -> 0x00c0
0x0238: 0x0080 -> 0x00bf
0x023a: 0x0080 -> 0x00bf
0x005c: 0x0180 -> 0x0120
0x0082: 0x1580 -> 0x1d80
```

These values are device-specific calibration evidence, not a recommendation to write them blindly to another sensor.

### Image geometry and packing

Windows reports `80 x 64 = 5120` pixels. The encrypted image response ultimately contains 7680 packed image bytes, matching 12-bit packing:

```text
5120 pixels * 12 bits = 7680 bytes
```

The Windows regroup stage outputs `10240` bytes (`5120 * 2`) as 16-bit host samples.

## Related work

Other Goodix Linux projects are useful protocol references, including work on 5125/51x7 and newer ChicagoHS-family reversing. **Do not assume firmware/config compatibility between models solely because command IDs are similar.**

The `27c6:5135 / GF_HC460SEC_APP_12508` target documented here has device-specific firmware, OTP and runtime behavior that must be preserved.

## Contribution policy

Evidence is preferred over guesses. If submitting findings, include:

- exact VID:PID,
- firmware string,
- chip ID if known,
- operating system/driver version,
- command lengths/statuses,
- whether the observation occurred before or after power loss/re-enumeration.

Do **not** post plaintext PSKs, biometric images/templates, private Windows account data, or proprietary driver binaries.

See [`CONTRIBUTING.md`](CONTRIBUTING.md).
