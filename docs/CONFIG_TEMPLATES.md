# `gfusb.dll` config-template analysis

Offline static analysis of the Windows `gfusb.dll` found many repeated template-like byte arrays. No proprietary binary is included in this repository.

## Repeated prefixes

Three recurring template families were observed:

```text
CFG30 starts: 30 11 64 75 ...
CFG70 starts: 70 11 74 85 ...
CFG90 starts: 90 11 74 85 ...
```

The analyzed DLL contained repeated instances of each family.

## Default register tuples

CFG70 and CFG90 both contain the six default sensor-register tuples later modified by the ChicagoHS OTP path:

```text
+0x71: 0x005c
+0x75: 0x0220
+0x79: 0x0236
+0x7d: 0x0238
+0x81: 0x023a
+0xad: 0x0082
```

Static inspection alone did not originally prove which family was selected.

## Resolved selection for 27c6:5135 / HC460 / ChicagoHS

Runtime analysis has now resolved the question for the tested device:

```text
GF_HC460SEC_APP_12508
chip 0x2504
ChicagoHS / sensor type 12 / 80x64

CFG70
  -> OTP-derived calibration
  -> checksum recalculation
  -> exact 224-byte runtime config
  -> command 0x90
```

The important distinction is:

- **CFG70** is the static source template family.
- **`0x90`** is the MCU upload-config command.
- `COMMAND_UPLOAD_CONFIG_MCU == 0x90` does **not** imply CFG90.

## Runtime diff proof

Before checksum regeneration, the static CFG70/runtime differences were only at:

```text
0x73, 0x77, 0x78, 0x7b, 0x7f, 0x83, 0xb0, 0xde, 0xdf
```

The first seven changed bytes correspond to the six logical OTP-derived calibration fields. The final two bytes are the checksum.

After applying those substitutions and recomputing the checksum:

```text
remaining differences: 0
full 224-byte match: true
```

See [`CFG70_RUNTIME_PROOF_2026-08-27.md`](CFG70_RUNTIME_PROOF_2026-08-27.md) for the evidence chain and checksum algorithm.

## Corrected 224-byte boundary

Windows proved that the actual uploaded config length is exactly `0xe0` bytes.

Therefore any earlier differences at `+0xe0` or later were adjacent data accidentally included by the old 256-byte analysis window, not MCU configuration bytes.

## Privacy

The repository should document the selection rule, mutation structure, offsets, checksum algorithm, command framing, and validation method.

Do not commit:

- full proprietary Windows binaries,
- full per-device OTP,
- process-memory dumps,
- plaintext PSK material,
- reconstructed per-device 224-byte payloads,
- fingerprint images/templates.
