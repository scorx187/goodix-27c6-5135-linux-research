> **Milestone note:** This CFG70 proof remains valid, but its old “next phase” is now completed through FDT and first TLS image transport. See `docs/CURRENT_STATUS_2026-08-27.md`.

# Linux CFG70 Build + Command 0x90 Upload Proof

**Date:** 2026-08-27  
**Target:** Goodix USB `27c6:5135`  
**Firmware tested:** `GF_HC460SEC_APP_12508`  
**Logical chip ID:** `0x2504` (`a2 04 25 00`)  
**Sensor family:** ChicagoHS / sensor type 12 / 80x64

## Status

The original config-selection blocker is closed, and the Linux implementation has now crossed two additional proof milestones:

1. Linux can reconstruct the exact 224-byte Windows runtime CFG70 payload from the live sensor OTP.
2. Linux can send that exact payload using `upload_config_mcu()` / protocol command `0x90`, receive success, and verify the expected calibration state afterward.

This does **not** yet prove `config missing -> Linux restores config`, because the device already had a valid runtime config loaded immediately before the controlled Linux upload test.

## Public-safe result summary

The tested reconstruction path is:

```text
local static CFG70 template
        +
live 64-byte OTP
        |
        +-> OTP CRC validation
        +-> DAC mirror validation
        +-> tcode derivation
        +-> FDT delta derivation
        +-> FDT offset derivation
        +-> four DAC values
        |
        v
six proven CFG70 calibration substitutions
        +
Goodix 16-bit config checksum
        |
        v
exact 224-byte Windows-compatible runtime CFG70
        |
        v
COMMAND_UPLOAD_CONFIG_MCU (0x90)
```

The generated Linux payload matched the private Windows runtime reference byte-for-byte.

No per-device runtime payload, OTP, PSK, fingerprint image/template, Windows process dump, or per-device runtime SHA is published in this document.

## Static CFG70 extraction

A local `gfusb.dll` structural scan found:

- 41 CFG70 structural occurrences
- exactly 1 unique 224-byte structural blob
- all 41 occurrences are byte-identical

The static template tail bytes are **not** treated as the authoritative runtime checksum. Windows recomputes bytes `0xde/0xdf` after applying OTP-derived calibration.

The public repository should not redistribute `gfusb.dll`. The extraction helper operates only on a locally supplied Windows driver binary.

## OTP derivation proof

The live sensor OTP is 64 bytes.

The Linux parser validates:

- CP CRC
- FT CRC
- MT CRC
- FT DAC CRC
- MT DAC CRC
- FT/MT DAC mirror equality

For the tested unit, Linux derived the same calibration metadata printed independently by the Windows Goodix debug log, including:

- `tcode`
- `fdt_delta`
- `fdt_offset`
- all four DAC values

The full OTP is private and must not be logged or committed.

## Proven CFG70 mutation fields

The 224-byte CFG70 template uses the following logical runtime calibration fields:

| Register | Purpose / source |
|---|---|
| `0x005c` | OTP-derived tcode |
| `0x0220` | OTP-derived primary DAC |
| `0x0236` | OTP-derived DAC |
| `0x0238` | OTP-derived DAC |
| `0x023a` | OTP-derived DAC |
| `0x0082` | OTP-derived FDT delta mapping |

The final two bytes are the recomputed Goodix 16-bit little-endian checksum.

## Checksum rule

```c
sum = 0xa5a5;
for (offset = 0; offset < 222; offset += 2)
    sum += config[offset] | (config[offset + 1] << 8);

checksum = (uint16_t)(0 - sum);
config[222] = checksum & 0xff;
config[223] = checksum >> 8;
```

The Linux-generated 224-byte runtime buffer passed this rule.

## Controlled Linux dry-run proof

The dry-run builder performed the following without config upload:

- initialized USB transport
- read live OTP
- validated all OTP CRCs and DAC mirror
- loaded the private local CFG70 static template
- applied the six calibration substitutions
- recomputed the config checksum
- enforced exact length 224
- compared the generated output against a private Windows runtime reference

Result:

```text
OTP validation        : PASS
DAC mirror            : PASS
Template family       : CFG70
Generated length      : 224
Generated checksum    : PASS
Windows reference     : EXACT BYTE-FOR-BYTE MATCH
Config upload called  : NO
```

## Controlled Linux `0x90` proof

Before the upload:

```text
Firmware       : GF_HC460SEC_APP_12508
Chip ID        : a2042500
0x0220         : calibrated value already present
MCU state      : stable configured state
```

The guarded uploader then:

1. re-read firmware and required an exact firmware match
2. re-read and required exact chip ID
3. re-read live OTP without printing it
4. rebuilt the 224-byte runtime CFG70 in RAM
5. revalidated length and checksum
6. compared it to the private Windows reference
7. sent exactly one `upload_config_mcu(runtime)` call
8. verified post-upload calibration state

Result:

```text
upload_config_mcu      : True
Windows reference      : EXACT MATCH
Post-upload calibration: VERIFIED
MCU state              : stable
```

Therefore Linux command `0x90` interoperability is proven for the tested unit.

### Important scientific limitation

The device already had a correct runtime config loaded immediately before this upload. Therefore the test proves:

> Linux can construct the exact Windows-compatible config and the sensor accepts the Linux `0x90` upload.

It does **not yet** prove:

> Linux can recover from a genuinely missing runtime config state.

Do not overstate this in README/docs.

## Reset / transport experiments — do not repeat blindly

These experiments were performed while trying to produce an unconfigured state.

### Protocol reset without initialization

Calling:

```python
device.reset(True, False, 20)
```

directly could time out waiting for the ACK and leave the transport temporarily non-responsive.

Do not repeat this direct sequence.

### Proven protocol reset sequence

A later state-dependent failure showed that the more robust, device-proven activation sequence is now documented in `CURRENT_STATUS_2026-08-27.md`: `NOP -> 0xd4 -> NOP -> enable_chip -> NOP -> firmware -> reset -> chip read`.

returned:

```text
success = True
number  = 2048
```

However, after USB transport recovery and reinitialization, the calibrated runtime register value was still present.

Conclusion:

> A successful Goodix `0xA2` sensor reset does not demonstrate runtime-config loss on this tested state.

Do not use it as a `config gone` proof.

### sysfs authorized toggle

Linux USB deauthorize/authorize:

```text
/sys/bus/usb/devices/.../authorized = 0
authorized = 1
```

successfully recovered the USB transport after timeouts, but preserved the runtime config.

Use it as a transport-recovery technique, not as a config reset.

### PyUSB / libusb device reset

`usb.core.Device.reset()` returned successfully but also preserved the runtime config.

Do not treat a successful libusb reset return as proof that CFG70 was cleared.

### `usbreset` command

The local `usbreset` utility was unreliable for this device/session and returned errors such as `No such device`.

Do not repeatedly invoke it.

## USB cleanup timeout

`device.disconnect()` can raise a timeout such as:

```text
TimeoutError: Device is still connected
```

after otherwise successful operations.

A cleanup timeout must not overwrite a successfully completed preceding read/build/upload result.

Public scripts should catch this as a warning after a successful operation.

## Logging privacy bug discovered

The current local `goodix.py` implementation prints the entire config argument when calling:

```python
upload_config_mcu(config)
```

This caused the private 224-byte per-device runtime configuration to appear in terminal output during the successful test.

Public tooling must suppress this verbose call output or change the library logging behavior.

Do not commit terminal logs containing the full `upload_config_mcu(b'...')` payload.

## Safety / privacy

Never commit or publish:

- plaintext factory PSK
- PSK files or per-device PSK hashes
- full OTP
- full 224-byte per-device runtime config
- per-device runtime config hashes if treating the config as private
- fingerprint images or templates
- `goodix.dat`
- `goodix_calib.dat`
- `Goodix_Cache.bin`
- Windows process-memory dumps
- proprietary Goodix `.dll`, `.exe`, or `.cat` files

The static CFG70 template should be extracted locally from a legally obtained local driver binary rather than redistributed from this repository.

## Current engineering state

Completed:

- identify ChicagoHS / chip `0x2504`
- factory TLS PSK recovery path proven privately
- TLS 1.2 PSK handshake proven on Linux
- exact 224-byte config length proven
- CFG70 static source proven
- OTP mutation field mapping proven
- checksum algorithm proven
- exact Windows runtime CFG70 reconstruction proven
- Linux live-OTP dry-run reconstruction proven
- Linux command `0x90` acceptance proven

## Next phase

Do **not** spend more time trying to force config loss.

Proceed from the currently configured sensor:

1. rerun the existing private/local `tls_probe_5135.py`
2. verify TLS handshake after the successful Linux CFG70 upload
3. implement / validate FDT finger-down
4. implement / validate FDT finger-up
5. request one TLS-protected image
6. decode the 12-bit wire image to local 16-bit samples
7. keep all fingerprint capture data local/private
8. only after the end-to-end capture path works, revisit a controlled `config missing -> Linux restore` experiment if still useful

The current blocker is **FDT + TLS image capture**, not CFG70.
