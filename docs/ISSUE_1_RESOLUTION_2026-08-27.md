# Issue #1 resolution

## Resolved: Windows runtime configuration recovered structurally and reconstructed exactly

The original blocker was identifying the exact 224-byte ChicagoHS configuration uploaded by the Windows Goodix driver with command `0x90`.

That template-selection question is now resolved for the tested `27c6:5135` / `GF_HC460SEC_APP_12508` device.

### Confirmed

- chip ID: `0x2504`
- Windows selects ChicagoHS, sensor type 12, 80×64
- Windows upload command is `0x90`
- upload payload length is exactly `0xe0` / 224 bytes
- the static source template is **CFG70**, not CFG90
- Windows applies OTP-derived calibration to six logical sensor-register fields:
  - `0x005c`
  - `0x0220`
  - `0x0236`
  - `0x0238`
  - `0x023a`
  - `0x0082`
- the final two bytes are a recomputed 16-bit little-endian checksum
- the rebuilt config matched the Windows runtime buffer **byte-for-byte across all 224 bytes**

The public checksum algorithm is:

```c
uint16_t sum = 0xa5a5;

for (size_t offset = 0; offset < 222; offset += 2)
    sum = (uint16_t)(sum +
        (config[offset] |
        ((uint16_t)config[offset + 1] << 8)));

uint16_t checksum = (uint16_t)(0 - sum);
```

### Static/runtime diff proof

Before checksum regeneration, the only static-CFG70/runtime differences were at:

```text
0x73, 0x77, 0x78, 0x7b, 0x7f, 0x83, 0xb0, 0xde, 0xdf
```

The first seven changed bytes correspond to the six Windows-observed calibration fields; `0xde` and `0xdf` are the checksum.

After applying those calibration substitutions and recomputing the checksum:

```text
remaining diffs: 0
full 224-byte match: true
```

### Important clarification

`0x90` is the **upload-config command ID**. It is not evidence that the selected static template is “CFG90”.

For this device, the actual path is:

```text
CFG70
  -> OTP-derived ChicagoHS calibration
  -> checksum
  -> 224-byte payload
  -> command 0x90
```

### Privacy

No plaintext PSK, full OTP, process-memory dump, fingerprint image/template, or per-device reconstructed 224-byte payload should be committed.

### Next issue / next phase

The next engineering task is to implement a Linux dry-run ChicagoHS config builder:

1. read OTP
2. derive the six calibration fields
3. apply them to CFG70
4. recompute checksum
5. validate exactly 224 bytes
6. compare locally against the known private reference
7. only then permit a controlled volatile `upload_config_mcu()` test

The original “identify the Windows 224-byte config” blocker can therefore be considered completed.
