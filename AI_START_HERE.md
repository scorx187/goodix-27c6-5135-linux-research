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

The device is currently responsive after USB re-enumeration, but the runtime MCU configuration is absent.

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

which is little-endian `0x0bf8`, matching the Windows OTP-calibrated DAC.

## Major confirmed results

### Factory TLS works from Linux

A local TLS 1.2 PSK bridge using:

```text
PSK-AES128-GCM-SHA256
identity: Client_identity
```

was accepted by the sensor. The factory 32-byte host PSK was recovered locally from Windows DPAPI data and **must never be committed or pasted into public logs**.

The Linux sequence successfully completed:

```text
request_tls_connection
TLS record bridge
TLS successfully established
secured NOP
```

### Windows driver

```text
Provider: Goodix FP
Driver:   1.1.125.12
Date:     2021-05-25
INF:      gfusb.inf (published as oem173.inf)
```

Package files include:

```text
gfusb.dll
EngineAdapter.dll
AlgoMilan.dll
AlgoChicago.dll
AlgoChicagoT.dll
GoodixEventLog.dll
SessionService.exe
```

The INF supports both:

```text
USB\VID_27C6&PID_5125
USB\VID_27C6&PID_5135
```

### Windows profile selection is proven

Windows event logs show:

```text
Get Chip ID: 0x2504
device_enable_init_by_chip ... to init device by chipid 0x2504
!!!! to Open ChicagoHS, sensor type: 12
sensor info ready, chipid:0x2504, sensorType:12, col:80, row:64
```

Functions used in the driver include `ChicagoHUSetMode`, `ChicagoHU_check_and_parse_otp`, `ChicagoHUsetDac`, and `chicagoHUget_*`.

### OTP-derived config changes

Windows logs show exactly:

```text
0x0220: 0x0808 -> 0x0bf8
0x0236: 0x0080 -> 0x00c0
0x0238: 0x0080 -> 0x00bf
0x023a: 0x0080 -> 0x00bf
0x005c: 0x0180 -> 0x0120
0x0082: 0x1580 -> 0x1d80
```

and:

```text
dac=0xbf8, dac1=0xc0, dac2=0xbf, dac3=0xbf
tcode=288
fdt_delta=29
```

### IMPORTANT: MCU config length is 224 bytes, not 256

The Windows trace around `gf_download_config` proves:

```text
cmd0-cmd1-Len-ackt-ec:0x9-0-0xe0-1000-0
get ack for cmd 0x90
recvd data cmd-len: 0x90-3
OTP data valid, download config 1
have config 1
```

So:

```text
COMMAND_UPLOAD_CONFIG_MCU = 0x90
payload length             = 0xe0 = 224 bytes
```

This explains an earlier reverse-engineering observation: candidate template differences began exactly at offset `+0xe0`; bytes after that are neighboring data, not part of the uploaded config.

## Candidate config templates found in gfusb.dll

Repeated templates were found beginning with:

```text
CFG30: 30 11 64 75 ...
CFG70: 70 11 74 85 ...
CFG90: 90 11 74 85 ...
```

CFG70 and CFG90 both contain the six default register/value tuples above at the same offsets.

**Do not yet assume CFG90 is the HC460 config.** Direct xrefs were absent because the binary appears to access these through an indirect table/descriptor mechanism or copies them dynamically.

The current next task is to determine whether the 224-byte upload begins with `70...`, `90...`, or another template by extracting the outgoing Windows payload or reversing the descriptor selection.

## Windows `goodix.dat`

Exact size and validated layout:

```text
13520 total
  64 OTP
  12 FDT base
3200 NAV base
10240 IMAGE base
   4 CRC
```

The CRC calculation matched the stored little-endian value.

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

Do not request a frame until MCU config is verified and loaded.

## Safety rules — mandatory

Never:

- run `run_5117.py` on this device,
- erase MCU application firmware,
- flash ST411/5117 firmware,
- write/re-provision PSK,
- publish the factory PSK,
- upload an unverified config,
- publish fingerprint images/templates,
- assume 5125 == 5135 merely because the Windows INF lists both.

Volatile/read operations and device-specific, evidence-backed runtime configuration work are acceptable.

## Next task

1. Recover the exact **224-byte** payload sent by Windows command `0x90`, or prove which 224-byte template the ChicagoHS path selects.
2. Compare it against the DLL template.
3. Reproduce the six OTP substitutions locally.
4. Determine/check the config checksum over exactly 224 bytes.
5. Print the final diff and checksum **without USB writes**.
6. Only after exact parity is proven, perform one volatile MCU config upload.
7. Read back state/registers and verify `have config=1` and `0x0220=f80b`.
8. Then restore factory TLS and test one image frame.
9. Move the working sequence into a libfprint driver.
