# Research log

## 1. Linux/libfprint baseline

`fprintd` initially reported no devices. The stock libfprint build did not list `27c6:5135`.

## 2. Safe protocol probe

A read-oriented probe established:

```text
Firmware: GF_HC460SEC_APP_12508
Chip ID response compatible with 0x2504 family
OTP length: 64
MCU state readable
PSK metadata readable
```

This showed that much of the shared Goodix protocol framing was compatible, while firmware/profile differences made destructive 5117 procedures unsafe.

## 3. Windows PSK recovery

Goodix Windows data was located under `C:\ProgramData\Goodix`.

`Goodix_Cache.bin` contained a Windows DPAPI blob plus an 8-byte Goodix entropy seed. Using the reversed Goodix entropy derivation and Windows DPAPI, the factory host TLS PSK was recovered locally.

The plaintext PSK is intentionally not recorded here.

## 4. Factory TLS from Linux

Using the recovered local PSK and an OpenSSL PSK server, the sensor completed TLS 1.2 using `PSK-AES128-GCM-SHA256`, followed by a secured NOP.

This proved:

- USB transport works,
- the factory PSK is accepted,
- the Goodix TLS record bridge is compatible,
- no firmware/PSK re-provisioning is required for Linux interoperability.

## 5. Windows driver identification

DriverStore package:

```text
Goodix FP 1.1.125.12
gfusb.inf
```

The package contains both Chicago and Milan algorithm/transport code paths.

## 6. ChicagoHS identified

Windows debug EVTX logs definitively selected:

```text
chipid 0x2504
ChicagoHS
sensor type 12
80 x 64
```

This replaced earlier uncertainty between ChicagoT/ChicagoHU/Milan paths.

## 7. `goodix.dat` layout proven

The file is exactly 13520 bytes:

```text
64 + 12 + 3200 + 10240 + 4 = 13520
```

The stored CRC matched the calculated CRC.

This established the OTP/FDT/NAV/IMAGE-base layout for this Windows installation.

## 8. Image geometry corrected

An early inference interpreted `10240` as a possible `80 x 128` 8-bit image. Windows logs later proved the actual sensor is `80 x 64`.

Correct interpretation:

```text
5120 pixels * 2 bytes = 10240 bytes
```

The wire image is 12-bit packed (`7680` bytes).

## 9. Config requirement corrected

An early hypothesis suggested image capture might work without downloading config if the firmware retained a prior runtime configuration.

A cold Windows trace disproved this:

```text
have config 0
MCU has no config
...
OTP parsed
config modified
config downloaded
...
have config 1
```

A Linux USB re-enumeration later reproduced the no-config state:

```text
firmware still responds
0x0220 = 0000
```

## 10. DLL template analysis

Repeated apparent config templates were found in `gfusb.dll`:

```text
CFG30: 30 11 64 75 ...
CFG70: 70 11 74 85 ...
CFG90: 90 11 74 85 ...
```

Both CFG70 and CFG90 contain the six register defaults later changed by OTP.

Direct xrefs from x64 `.text` were absent, indicating indirect selection/copying.

A 256-byte scanner initially produced false positives and included adjacent data.

## 11. Critical correction: config length is `0xe0`

Windows `gf_download_config` logs show:

```text
cmd0-cmd1-Len-ackt-ec:0x9-0-0xe0-1000-0
```

`0x9/0` maps to command `0x90`; `0xe0` is **224 bytes**.

This explains why one mutable DLL candidate only differed beginning exactly at offset `+0xe0`: those changes are outside the actual config payload.

### Current state

Exact template identity (`CFG70`, `CFG90`, or another 224-byte source) still needs to be proven before any Linux upload.

## 12. CFG70 selection and exact runtime parity — resolved

Offline extraction found one unique 224-byte CFG70 structural family. The live-OTP Linux builder applied the six proven calibration mutations, recomputed the config checksum, and matched a private Windows runtime reference byte-for-byte. One controlled Linux `0x90` upload was accepted and post-upload calibration state was verified.

The full runtime bytes and unit-specific hash remain private.

## 13. Factory TLS reconfirmed after config work

TLS 1.2 PSK with `PSK-AES128-GCM-SHA256` and identity `Client_identity` succeeded, followed by an encrypted-session NOP.

## 14. FDT manual seed interpretation corrected

The 12-byte `goodix.dat` FDT block is six duplicated manual threshold bytes. It is not six raw `u16` values and not six already encoded down-threshold pairs.

Manual `0x36` returns six live zone measurements. Down thresholds derived as `floor(raw/2)` were accepted by `0x32` and produced real finger-down events.

## 15. Activation-state bug resolved

After earlier FDT experiments, direct reads returned `06000000` and `enable_chip(True)` could timeout. A read-only pending-frame probe showed no stale queued packet.

The working activation sequence was proven as:

```text
NOP -> 0xd4 -> NOP -> 0x96 -> NOP -> firmware -> 0xa2 reset -> chip read
```

Three reads then returned `a2042500`.

## 16. FDT down/up complete

A corrected probe completed:

```text
manual 0x36 PASS
down 0x32 ACK/event PASS
up 0x34 ACK/event PASS
```

Finger-up returned IRQ `0x0200`, touchflag `0x0000`, and 0/6 active zones.

## 17. First TLS-protected image transport complete

With a finger held after FDT-down, command `0x20` payload `01 00` received a normal ACK then a Goodix pack with flags `0xb0`, length `7722`.

The TLS record decrypted to exactly `7693` bytes. Safe metadata parsing proved:

```text
command 0x20
declared length 7690
trailer 0x88
no-checksum protocol mode
payload 7689 bytes
```

Upstream `[8:-5]` image slicing leaves exactly `7680` packed bytes, consistent with 5120 12-bit pixels at 80x64.

The private capture was saved locally only and must never be committed.
