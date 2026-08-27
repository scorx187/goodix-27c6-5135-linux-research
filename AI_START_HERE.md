# AI / developer handoff — START HERE

## Goal

Make Goodix USB fingerprint sensor `27c6:5135` work natively under Linux/libfprint **without breaking Windows Hello, factory firmware, factory PSK provisioning, or existing Windows fingerprints**.

## Exact tested device

```text
USB VID:PID: 27c6:5135
Firmware:    GF_HC460SEC_APP_12508
Chip ID:     raw a2042500 / logical 0x2504
Profile:     ChicagoHS / ChicagoHU
Sensor type: 12
Packed axis: 64 fast x 80 slow
Output plane:80 columns x 64 rows
Pixels:      5120
USB bulk IN: 0x81
USB bulk OUT:0x01
```

## Current state — do not repeat solved work

As of 2026-08-27 the project is **past config, TLS, FDT, image transport, image CRC, RAW12 decode, ChicagoHU regroup, and ImageBase/live spatial-order proof**.

```text
CFG70 reconstruction              DONE
command 0x90 upload               DONE
factory TLS                       DONE
verified activation sequence      DONE
FDT manual 0x36                   DONE
FDT-down 0x32                     DONE
FDT-up 0x34                       DONE
image command 0x20 ACK            DONE
TLS image transport               DONE
TLS image decrypt                 DONE
Goodix image framing              DONE
Windows-compatible image CRC      DONE (one private capture)
RAW12 -> 5120 samples             DONE
ChicagoHU regroup                 PROVEN
ImageBase/live relative layout    PROVEN
Candidate A                       PROVEN
Candidate B double-regroup        REJECTED
exact matcher preprocessing       NEXT
```

Read first:

1. `docs/CURRENT_STATUS_2026-08-27.md`
2. `docs/WINDOWS_IMAGE_LAYOUT_5135_PROOF_2026-08-27.md`
3. `docs/IMAGE_TRANSPORT_5135_PROOF_2026-08-27.md`
4. `docs/FDT_5135_PROOF_2026-08-27.md`
5. `docs/LINUX_CFG70_UPLOAD_PROOF_2026-08-27.md`
6. `docs/FAILURES_AND_RECOVERIES_2026-08-27.md`
7. `docs/SAFETY.md`
8. `docs/DEVELOPER_ROADMAP.md`

## Immediate next task

**Stop investigating transport and stop re-proving ImageBase orientation.**

The current target is the Windows post-detection image-preparation path before the matcher/feature extractor.

The capture path calls runtime callback slot `0x18059cb60` with the runtime object, persisted ImageBase, captured live image, and a mode/flag byte. Resolve who registers `0x18059cb60`, inspect its concrete target, and prove the actual image processing it performs.

Need exact proof, if present, for:

1. baseline subtraction direction;
2. clamp/saturation rules;
3. normalization/gain scaling;
4. per-pixel calibration/noise correction;
5. crop/output dimensions;
6. any role of `goodix_calib.dat`;
7. exact buffer handed to matcher/feature extraction.

## Windows live-image layout proof — critical checkpoint

For logical chip `0x2504`, the family path selects type `0x0c` and initializes the Chicago object at `0x180588dc0`.

The initializer at `0x1800266a4` installs full-image callback:

```text
object + 0x13d48 -> 0x1800289f8
```

Inside that callback, after image CRC success:

```text
packed length 0x1e00
    -> 0x180023e38 decode/regroup
    -> temporary 16-bit plane
    -> copy into pointer at object + 0x13cc0
```

The key identity is:

```text
0x180588dc0 + 0x13cc0 = 0x18059ca80
```

`0x18059ca80` is the common runtime live-image buffer pointer. Thus the Chicago callback writes regrouped output into the exact buffer later copied into the capture routine.

The capture routine compares that live plane with persisted ImageBase from `0x18059ca88` using identical indices. ImageBase load/save does not apply another regroup.

Therefore:

```text
Candidate A: ImageBase already stored in downstream regrouped order  PROVEN
Candidate B: regroup ImageBase again                                 WRONG
```

## ChicagoHU geometry

Do not conflate packed transport orientation with downstream plane orientation.

```text
packed stream: 64 fast x 80 slow
samples:       5120
packed bytes:  7680
output plane:  80 columns x 64 rows
u16 bytes:     10240
```

Proven regroup mapping:

```text
dst = (n % 64) * 80 + (n / 64)
```

## Detector/classifier — solved enough

Wrapper `0x180023acc` reaches classifier `0x180022178`.

It compares ImageBase and live image at the same pixel index, computes tile statistics and directional differences, and Windows logs label its numeric returns:

```text
0 = temperature
1 = finger down
2 = void
3 = bad
```

This is a base/live detector/classifier, **not** the final matcher preprocessing output routine. Do not spend more time reverse engineering it unless required by a later dependency.

## Image CRC

Windows-compatible image integrity rule is proven on one private capture:

```text
CRC-32/MPEG-2
poly    0x04C11DB7
init    0xFFFFFFFF
refin   false
refout  false
xorout  0
```

For stored bytes `[a,b,c,d]`, Windows reconstructs:

```text
(c << 24) | (d << 16) | (a << 8) | b
```

CRC domain is the 7680 packed RAW12 bytes; checker input block is packed RAW12 + 4-byte stored CRC (`7684` bytes total).

A second independent private capture is desirable as cross-capture confirmation, but it is not the current blocker.

## Verified 5135 activation sequence

When `enable_chip(True)` times out or register 0 reads `06000000`, use the tested sequence rather than assuming hardware damage:

```text
NOP
0xd4 TLS_SUCCESSFULLY_ESTABLISHED (transient activation-state command)
NOP
0x96 ENABLE_CHIP true
NOP
firmware_version
0xa2 reset(True, False, 20)
read register 0x0000
```

This restored three consecutive `a2042500` reads.

## Factory TLS

```text
TLS version: TLS 1.2
cipher:      PSK-AES128-GCM-SHA256
identity:    Client_identity
```

The factory host PSK was recovered privately from the user's own Windows DPAPI-protected Goodix cache. **Never request, print, commit, or re-provision it.**

## CFG70 / command 0x90

The original config blocker is solved.

- Windows upload length: exactly 224 bytes (`0xe0`).
- Correct static family: CFG70.
- Linux rebuild from live OTP matched the private Windows runtime reference byte-for-byte.
- Config checksum rule is proven.
- One controlled Linux `0x90` upload was accepted and post-upload calibration verified.
- Do not publish the full unit-specific runtime config or its private hash.

## FDT

- private `goodix.dat` layout: `OTP64 + FDT12 + NAV3200 + IMAGE10240 + CRC4`;
- FDT12 is six duplicated manual seed bytes;
- manual `0x36`: `0d01 + seed12`;
- down thresholds: `floor(raw/2)`, encoded as six `80 xx` pairs;
- down `0x32`: `0801 + regs12 + timestampLE`;
- up `0x34`: `0a02 + regs12`;
- current FDT-up success uses a private Windows-traced per-unit threshold set; generic derivation remains future work.

## Image transport facts

Image request:

```text
command 0x20
payload 01 00
```

Reply order:

```text
normal ACK for 0x20
then Goodix transport frame flags 0xb0, len 7722
TLS 1.2 application-data at offset 0
TLS plaintext len 7693
```

The decrypted Goodix message uses `checksum=False` / trailer `0x88`.

Do not repeat the old `0xb2` assumption.

## Mandatory privacy/safety

Never publish or ask the user to upload:

- plaintext factory PSK;
- PSK files or hashes;
- full OTP;
- fingerprint images/raw frames/templates;
- `goodix.dat`, `goodix_calib.dat`, `Goodix_Cache.bin`;
- proprietary Windows DLL/EXE/CAT;
- process memory dumps;
- full unit-specific 224-byte runtime config or its private hash.

Never erase/flash firmware or write/re-provision PSK.

A runtime config upload (`0x90`) is a volatile MCU write. Describe it accurately.

## Working style

The user prefers one terminal block at a time and pastes output. Avoid unnecessary questions. Preserve decisive checkpoints in this repository so a chat/session limit cannot destroy progress.
