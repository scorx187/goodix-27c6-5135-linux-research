# AI / developer handoff — start here

## Goal

Make Goodix fingerprint sensor `27c6:5135` work under Linux/libfprint **without breaking Windows Hello, factory firmware, PSK provisioning, or stored Windows fingerprints**.

## Exact device

```text
USB VID:PID: 27c6:5135
Firmware:    GF_HC460SEC_APP_12508
Chip ID:     0x2504
Profile:     ChicagoHS / ChicagoHU
Sensor type: 12
Geometry:    80 x 64
```

## Current Linux state

The device is responsive after USB re-enumeration, but the runtime MCU configuration is absent.

Last read-only probe:

```text
Firmware: GF_HC460SEC_APP_12508
register 0x0220: 0000
MCU state: 00023000000800002000000001000000
```

Do **not** attempt image capture in this state.

Previously, while Windows-loaded runtime configuration was still present, Linux read:

```text
register 0x0220: f80b
```

## Major confirmed results

### Factory TLS works from Linux

A local TLS 1.2 PSK bridge using:

```text
PSK-AES128-GCM-SHA256
identity: Client_identity
```

was accepted by the sensor. The factory host PSK was recovered locally from Windows DPAPI data and **must never be committed or pasted into public logs**.

The Linux sequence successfully completed:

```text
request_tls_connection
TLS record bridge
TLS successfully established
secured NOP
```

### Windows driver and profile

```text
Provider: Goodix FP
Driver:   1.1.125.12
Date:     2021-05-25
INF:      gfusb.inf
```

Windows event logs prove:

```text
Get Chip ID: 0x2504
!!!! to Open ChicagoHS, sensor type: 12
sensor info ready, chipid:0x2504, sensorType:12, col:80, row:64
```

### Resolved: exact Windows runtime config construction

The former blocker is closed for the tested unit.

Windows uses this path:

```text
CFG70 static template
  -> OTP-derived ChicagoHS calibration
  -> Goodix 16-bit config checksum
  -> exact 224-byte runtime config
  -> MCU upload-config command 0x90
```

Important terminology:

- **CFG70** is the selected static template family.
- **`0x90`** is the MCU upload-config command ID.
- The command number does **not** imply a template named CFG90.

The six logical calibration fields are:

```text
0x005c
0x0220
0x0236
0x0238
0x023a
0x0082
```

The final two bytes (`0xde`, `0xdf`) are the recomputed 16-bit little-endian checksum.

Verified checksum algorithm:

```c
uint16_t sum = 0xa5a5;

for (size_t offset = 0; offset < 222; offset += 2) {
    uint16_t word =
        config[offset] |
        ((uint16_t)config[offset + 1] << 8);

    sum = (uint16_t)(sum + word);
}

uint16_t checksum = (uint16_t)(0 - sum);

config[222] = checksum & 0xff;
config[223] = checksum >> 8;
```

The clean reconstruction test was:

1. copy static CFG70,
2. apply only the six Windows-observed OTP-derived substitutions,
3. recompute checksum,
4. compare all 224 bytes with the Windows runtime buffer.

Result:

```text
remaining differences: 0
full 224-byte match: true
checksum verification: true
```

Public proof:

- [`docs/CFG70_RUNTIME_PROOF_2026-08-27.md`](docs/CFG70_RUNTIME_PROOF_2026-08-27.md)
- [`docs/ISSUE_1_RESOLUTION_2026-08-27.md`](docs/ISSUE_1_RESOLUTION_2026-08-27.md)

Per-device OTP values, process-memory dumps, and reconstructed per-device 224-byte payloads remain private.

### MCU config length is 224 bytes, not 256

The Windows trace around `gf_download_config` proves:

```text
COMMAND_UPLOAD_CONFIG_MCU = 0x90
payload length             = 0xe0 = 224 bytes
```

Bytes after offset `0xdf` are not part of this configuration upload.

## Windows `goodix.dat`

Validated layout:

```text
13520 total
  64 OTP
  12 FDT base
3200 NAV base
10240 IMAGE base
   4 CRC
```

Do not publish the full device-specific file.

## Capture facts

Windows image mode uses command `0x20` and returns a TLS-protected payload.

Observed image facts:

```text
geometry:       80 x 64
pixels:         5120
packed payload: 7680 bytes (12 bits/pixel)
regrouped data: 10240 bytes (5120 x 16-bit)
```

Do not request a frame until MCU config is verified and loaded on Linux.

## Safety rules — mandatory

Never:

- run destructive 51x7/5117 flows on this device,
- erase MCU application firmware,
- flash ST411/5117 firmware,
- write or re-provision PSK,
- publish the factory PSK,
- upload a speculative/unverified config,
- publish fingerprint images/templates,
- commit full OTP, process-memory dumps, or reconstructed per-device runtime payloads,
- assume 5125 == 5135 merely because the Windows INF lists both.

Volatile/read operations and device-specific, evidence-backed runtime configuration work are acceptable.

## Next task

The next blocker is **implementation**, not template identification.

### Phase A — Linux dry-run ChicagoHS config builder

1. Require USB PID `27c6:5135`.
2. Read and verify firmware; require the expected HC460 family before mutation logic.
3. Read the 64-byte OTP without printing it by default.
4. Derive the six ChicagoHS calibration fields.
5. Start from the proven CFG70 template.
6. Apply the six logical substitutions.
7. Recompute the 16-bit checksum.
8. Require exactly 224 bytes.
9. Validate only; **do not upload**.

### Phase B — local parity proof

On the original test machine, compare the Linux-built config against the private Windows-derived reference.

Public tests should validate structure and checksum without embedding private per-device material.

### Phase C — controlled volatile upload

Only after exact local parity:

1. perform the understood volatile initialization/reset sequence,
2. upload the validated 224-byte config with command `0x90`,
3. query MCU state and verify `have config`,
4. read back expected calibrated state/registers,
5. restore the already-proven factory TLS path.

No firmware or PSK writes are required.

### Phase D — capture pipeline

After config + TLS are stable:

- reproduce FDT down/up,
- request image mode,
- decode 12-bit wire data,
- regroup to 80×64 samples,
- keep fingerprint captures private,
- integrate into libfprint only after the userspace protocol path is reliable.
