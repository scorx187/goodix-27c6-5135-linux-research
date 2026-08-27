# Goodix 27c6:5135 — ChicagoHS CFG70 runtime-config proof

**Date:** 2026-08-27  
**Device:** Goodix USB fingerprint reader `27c6:5135`  
**Firmware:** `GF_HC460SEC_APP_12508`  
**Chip ID:** `0x2504`  
**Windows driver:** Goodix FP `1.1.125.12`  
**Sensor family selected by the Windows driver:** ChicagoHS / sensor type 12 / 80×64

## Executive summary

The major configuration blocker for the Goodix `27c6:5135` has been resolved.

Windows does **not** upload the static template informally named `CFG90` merely because the upload command is `0x90`.

The observed Windows path is:

```text
CFG70 static template
    -> apply OTP-derived ChicagoHS calibration substitutions
    -> recompute Goodix 16-bit config checksum
    -> resulting 224-byte runtime configuration
    -> send with MCU upload-config command 0x90
```

This reconstruction was verified **byte-for-byte** against the runtime buffer present in the Goodix Windows driver process after successful device initialization.

The locally rebuilt 224-byte buffer and the runtime buffer had:

```text
remaining byte differences: 0
full 224-byte match: true
checksum verification: true
```

Per-device OTP values and the resulting per-device configuration are intentionally not published.

---

## Why this matters

Before this work, three 224/256-byte-looking static candidates had been identified in the Windows driver and were referred to as `CFG30`, `CFG70`, and `CFG90`.

Separately, Windows logs proved that the device upload command is command `0x90`, with payload length `0xe0` (224 bytes).

Those two facts could easily lead to a wrong conclusion:

> command `0x90` means template `CFG90`

That conclusion is false for this tested `27c6:5135` / HC460 / ChicagoHS device.

The actual runtime payload is derived from **CFG70**.

---

## Evidence chain

### 1. Windows selects ChicagoHS

During a successful cold initialization, the Goodix driver reported:

```text
chip id: 0x2504
Open ChicagoHS
sensor type: 12
columns: 80
rows: 64
```

This ties the tested hardware to the ChicagoHS path rather than guessing from USB PID alone.

### 2. Windows parses the sensor OTP

The driver reads the 64-byte OTP and runs the ChicagoHU/ChicagoHS OTP parser.

The full OTP is per-device data and is intentionally omitted from this repository.

The logs prove that OTP-derived values are used to modify the sensor configuration before download.

### 3. Six sensor-register configuration fields are modified

The Windows log shows six logical sensor-register substitutions before configuration upload:

```text
register 0x005c
register 0x0220
register 0x0236
register 0x0238
register 0x023a
register 0x0082
```

The concrete per-device replacement values are intentionally omitted here.

### 4. Windows uploads exactly 224 bytes

The successful Windows sequence includes:

```text
gf_download_config
UsbSendDataToDevice ... cmd0=0x9 cmd1=0 length=0xe0
ACK for command 0x90
OTP data valid, download config 1
have config 1
```

Therefore:

- upload command: `0x90`
- payload length: `0xe0`
- payload length decimal: `224`

Anything beyond byte `0xdf` is not part of this configuration upload.

### 5. Runtime memory was inspected locally

The Goodix UMDF driver was identified inside:

```text
WUDFHost.exe
    gfusb.dll
```

A local full process memory dump was made only for offline analysis.

The dump itself is private and must not be committed because process memory may contain sensitive material such as TLS/PSK data.

A memory scan looked for the known structure of the modified ChicagoHS configuration.

Result:

```text
two matching runtime copies found
both copies byte-identical
template family detected: CFG70
```

The two copies had the same 224-byte content. The local per-device SHA-256 is intentionally omitted from the public documentation.

### 6. Static CFG70 vs runtime config

The runtime 224-byte buffer was compared against matching static CFG70 instances in `gfusb.dll`.

The best static/runtime comparison had exactly **9 byte differences**:

```text
0x73
0x77
0x78
0x7b
0x7f
0x83
0xb0
0xde
0xdf
```

Interpretation:

```text
0x73       register 0x005c calibration byte
0x77-0x78 register 0x0220 calibration bytes
0x7b       register 0x0236 calibration byte
0x7f       register 0x0238 calibration byte
0x83       register 0x023a calibration byte
0xb0       register 0x0082 calibration byte
0xde-0xdf checksum
```

No unrelated runtime changes were observed.

This is strong evidence that Windows starts from CFG70, applies the six calibration substitutions, then updates the final checksum.

### 7. Checksum reconstruction

The 224-byte config uses a 16-bit little-endian checksum in the final two bytes.

The verified calculation is:

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

For the private test unit, the locally computed checksum matched the checksum present in the runtime buffer.

The concrete checksum value is omitted because it belongs to the per-device reconstructed payload.

### 8. Full clean reconstruction test

The final verification was deliberately performed from the static template rather than simply re-reading the runtime bytes:

1. Copy the static 224-byte CFG70 template.
2. Apply only the six Windows-observed OTP-derived substitutions.
3. Recompute the checksum using the algorithm above.
4. Compare all 224 bytes against the runtime buffer extracted from memory.

Result:

```text
remaining differences: 0
full 224-byte match: true
```

This closes the template-selection question for the tested HC460 / ChicagoHS device.

---

## Confirmed conclusion

For the tested Goodix `27c6:5135`:

```text
GF_HC460SEC_APP_12508
chip 0x2504
ChicagoHS / type 12 / 80x64

CFG70
  + OTP-derived calibration
  + checksum recalculation
  = exact 224-byte Windows runtime config

runtime config
  -> command 0x90
  -> successful MCU config upload
```

Important terminology:

- **CFG70** = the static template family selected by Windows.
- **0x90** = the MCU upload-config command.
- These are different concepts.

---

## Failed / nonproductive approaches worth preserving

### USBPcap cold-init capture timing

Several USBPcap captures were attempted.

A successful Windows cold initialization was observed in the Goodix event log, but an early USBPcap capture stopped just before the `0x90` upload.

Later captures used:

```text
--capture-from-new-devices
--inject-descriptors
```

but ordinary disable/enable cycles retained MCU configuration, so the upload was not repeated.

A full shutdown produced a true configuration upload once, but that upload occurred in a window not successfully captured by USBPcap.

Do not repeat endless disable/enable cycles expecting a configuration download; `have config 1` means there is nothing useful to capture.

### WBDI.log

The Windows driver logged a configured path:

```text
C:\ProgramData\Goodix\WBDI.log
```

but that file was not present on the tested system.

The useful diagnostics came from:

```text
Goodix-FingerprintProvider/Debug
```

in Windows Event Log.

### Static template naming

Do not infer static template selection from the protocol command number.

`COMMAND_UPLOAD_CONFIG_MCU == 0x90` does **not** imply `CFG90`.

---

## Privacy and safety rules

The following must remain private / local:

- plaintext factory TLS PSK
- PSK-containing process memory dumps
- full per-device OTP
- fingerprint images
- fingerprint templates
- `goodix.dat`
- `goodix_calib.dat`
- `Goodix_Cache.bin`
- full proprietary Windows binaries
- reconstructed per-device 224-byte payloads

Do not use destructive 51x7 procedures on this device.

In particular, do not:

- erase firmware
- flash 5117 firmware
- overwrite or provision PSK
- run destructive `run_5117.py` flows
- upload speculative configs

Windows functionality must be preserved.

---

## Recommended Linux implementation path

The next implementation should be intentionally split into a read-only/dry-run phase and an explicit runtime-upload phase.

### Phase A — dry-run builder

Create a Linux-side ChicagoHS configuration builder that:

1. Requires USB PID `27c6:5135`.
2. Reads and verifies firmware.
3. Requires the expected HC460 firmware family before allowing mutation logic.
4. Reads the 64-byte OTP.
5. Derives the ChicagoHS calibration fields from OTP.
6. Starts from the proven CFG70 template.
7. Applies the six logical substitutions.
8. Recomputes the 16-bit checksum.
9. Requires exactly 224 bytes.
10. Performs validation only and **does not upload**.

The builder should not print the full OTP or resulting per-device config by default.

### Phase B — local proof

On the original test machine, compare the Linux-built result against the private Windows-derived reference kept locally.

Public tests should validate structural properties without embedding private per-device data.

### Phase C — volatile upload

Only after the dry-run result is proven exact:

1. reset/initialize only using already-understood volatile protocol operations
2. upload the exact validated 224-byte config with command `0x90`
3. query MCU state
4. verify `have config`
5. continue to the already-proven factory TLS path

No firmware or PSK writes are required for this path.

### Phase D — capture pipeline

After config + TLS are stable:

- reproduce FDT down/up
- request image mode
- decode 12-bit wire data
- regroup to 80×64 samples
- keep fingerprint captures private during development
- integrate into libfprint only after the userspace protocol implementation is reliable

---

## Clean-room / redistribution note

The full proprietary `gfusb.dll` must not be committed.

If the final open-source implementation needs static configuration bytes, document their provenance carefully and prefer a clean-room, protocol-oriented representation or an independently redistributable source where possible.

The important public result from this investigation is the **selection rule, mutation structure, offsets, checksum algorithm, command framing, and validation method**, not redistribution of the Windows binary.

---

## Handoff

A future developer or AI should treat the following as proven for the tested unit:

```text
USB:       27c6:5135
firmware:  GF_HC460SEC_APP_12508
chip:      0x2504
sensor:    ChicagoHS type 12
geometry:  80×64
config:    CFG70-derived
length:    224 bytes
upload:    command 0x90
checksum:  16-bit little-endian, seed 0xa5a5
runtime reconstruction: exact byte-for-byte match
```

The next blocker is no longer “which Windows config is uploaded?”

The next blocker is:

> implement and validate the ChicagoHS OTP-to-CFG70 builder on Linux without exposing private per-device material, then perform a controlled volatile config upload.
