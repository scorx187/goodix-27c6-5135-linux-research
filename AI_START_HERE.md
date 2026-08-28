# AI / developer handoff — START HERE

## Goal

Make Goodix USB fingerprint sensor `27c6:5135` work natively under Linux/libfprint **without breaking Windows Hello, factory firmware, factory PSK provisioning, or existing Windows fingerprints**.

## Exact tested device

```text
USB VID:PID: 27c6:5135
Firmware:    GF_HC460SEC_APP_12508
Chip ID:     raw a2042500 / logical 0x2504
Profile:     ChicagoHS / ChicagoHU
Sensor type: 12 / family 0x0c
Packed axis: 64 fast x 80 slow
Output plane:80 columns x 64 rows
Pixels:      5120
USB bulk IN: 0x81
USB bulk OUT:0x01
```

## Canonical current checkpoint

Read first:

1. `docs/CURRENT_STATUS_2026-08-28.md`
2. `docs/CHICAGO_PREPROCESS_CORE_2026-08-28.md`
3. `docs/RELEASE_READINESS_AND_SAFETY_GATES.md`
4. `docs/DEVELOPER_ROADMAP.md`
5. `docs/WINDOWS_IMAGE_LAYOUT_5135_PROOF_2026-08-27.md`
6. `docs/IMAGE_TRANSPORT_5135_PROOF_2026-08-27.md`
7. `docs/FDT_5135_PROOF_2026-08-27.md`
8. `docs/LINUX_CFG70_UPLOAD_PROOF_2026-08-27.md`
9. `docs/FAILURES_AND_RECOVERIES_2026-08-27.md`
10. `docs/SAFETY.md`

Older current-status/handoff documents are historical evidence and may describe blockers already solved.

## Current state — do not repeat solved work

```text
CFG70 reconstruction                         DONE
command 0x90 upload                          DONE
factory TLS                                  DONE
verified activation sequence                 DONE
FDT manual/down/up                           DONE on tested unit
image transport/decrypt                      DONE
Windows-compatible image CRC                 DONE on one private capture
RAW12 -> 5120 samples                        DONE
ChicagoHU regroup                            PROVEN
ImageBase/live relative layout               PROVEN
Candidate A                                  PROVEN
Candidate B double-regroup                   REJECTED
gfusb.dll post-detection packaging           PROVEN
gfusb.dll Windows request handoff             PROVEN
Windows engine component                     IDENTIFIED: EngineAdapter.dll
0x2504 / family 0x0c algorithm selection     PROVEN: AlgoChicago.dll
AlgoChicago preprocessor export              PROVEN: RVA 0x0000b560
real preprocessor entry                      PROVEN: 0x18000e780
outer semantic preprocessor routine          PROVEN: 0x18000e780..0x18000e947
core preprocessing orchestrator              PROVEN: 0x1800484e0
first image/state-plane combiner             PROVEN: 0x180044970
first type-0x0c pixel subtraction            PROVEN
next preprocessing stage                     CURRENT TASK: 0x180043c40
matcher/enrollment                           LATER
libfprint/fprintd integration                 LATER
release/safety matrix                        FINAL STAGE
```

## Most important new proof

`0x1800484e0` copies:

1. the source image into a temporary u16 plane;
2. a second u16 plane from AlgoChicago internal preprocessing state at `state+0x9924`.

It then calls:

```text
0x180044970(source_temp, state_plane_temp, result, selector)
```

For the tested type/family `0x0c`, `0x180044970` takes the selector-`!=4` path and performs the exact first pixel-wise operation:

```text
diff16[i] = source_u16[i] - state_plane_u16[i]
```

The vector loop uses `psubw`; the scalar tail uses WORD subtraction. There is no clamp, saturation or gain in this subtraction step.

Later code reads the difference plane with `movsx`, proving it is intentionally interpreted as **signed 16-bit**. Negative differences survive as two's-complement values.

A separate selector-4-only path applies `source - state_plane + 0x0fff`; that is **not** the tested 5135/type-0x0c path.

Important terminology boundary:

- gfusb persisted `ImageBase` layout/order is already proven;
- AlgoChicago `state+0x9924` is proven to be an internal preprocessing/calibration-state plane;
- **do not claim these two planes are identical until population/data flow into `state+0x9924` is proven.**

## What `0x180044970` does after subtraction

It also computes block/window statistics over the signed difference plane, derives dynamic threshold-like values, creates/updates a byte mask, contains explicit type-`0x0c` branches, and stores a percentage-like field computed as:

```text
count * 100 / pixel_count
```

at result offset `+0x0e`.

Exact semantic names of all threshold/mask fields are still open.

## Immediate next task

**Do not return to transport, ImageBase orientation, gfusb detector, callback `0x180013280`, request helper `0x18001393c`, outer preprocessor boundary, or the first subtraction loop unless a new dependency requires it.**

The next reverse-engineering target is:

```text
AlgoChicago.dll 0x180043c40
```

Call site from `0x1800484e0`:

```text
RCX = copied source u16 image
RDX = copied internal state+0x9924 u16 plane
R8D = flag/mode decoded from packed preprocessing config
R9D = another flag decoded from packed preprocessing config
stack arg5 = type/selector field (0x0c for tested device)
stack arg6 = copied result structure produced from 0x180044970 path
```

Need to prove:

1. whether `0x180043c40` consumes or recomputes the signed difference plane;
2. how it uses the mask/statistics produced by `0x180044970`;
3. exact normalization/gain/clamp behavior, if any;
4. crop/grow/geometry effects;
5. exact output buffer it produces;
6. how that output reaches the temporary result returned by `0x1800484e0`;
7. exact matcher/enrollment input;
8. independently, how `state+0x9924` is populated and whether it derives from gfusb ImageBase.

Do not give semantic names to unexplained result codes such as `0x7531`, `0x7532`, `0xc351`, or EngineAdapter's special `0x84` until proven.

## Windows biometric architecture checkpoint

The installed package uses Windows built-in sensor/storage adapters and Goodix `EngineAdapter.dll` as the vendor engine adapter.

Static EngineAdapter selection:

```text
family 0x03 -> Milan algorithm path
family 0x0c -> AlgoChicago.dll
family 0x0e -> AlgoChicagoT.dll
```

For logical chip `0x2504`, active family/type is `0x0c`, therefore the tested algorithm DLL is `AlgoChicago.dll`.

EngineAdapter resolves preprocessing, calibration, sensor check, identify, enroll and template functions dynamically with `LoadLibraryW` / `GetProcAddress`.

Final matcher preprocessing therefore lives above `gfusb.dll`.

## Windows live-image layout proof — solved

For logical chip `0x2504`, family path selects type `0x0c` and initializes the Chicago object at `0x180588dc0`.

Full-image callback:

```text
object + 0x13d48 -> 0x1800289f8
```

After CRC success:

```text
packed 0x1e00
 -> 0x180023e38 decode/regroup
 -> temporary 16-bit plane
 -> object + 0x13cc0
```

Key alias:

```text
0x180588dc0 + 0x13cc0 = 0x18059ca80
```

Thus Chicago callback writes regrouped live image directly into the runtime live-image buffer later used by capture.

Persisted ImageBase is at `0x18059ca88`, and load/save do not apply another regroup.

Therefore:

```text
Candidate A: persisted ImageBase already downstream-regrouped  PROVEN
Candidate B: regroup ImageBase again                           WRONG
```

## Geometry

```text
packed stream: 64 fast x 80 slow
samples:       5120
packed bytes:  7680
output plane:  80 columns x 64 rows
u16 bytes:     10240
```

Regroup:

```text
dst = (n % 64) * 80 + (n / 64)
```

## Image CRC

Windows-compatible image integrity rule, proven on one private capture:

```text
CRC-32/MPEG-2
poly    0x04C11DB7
init    0xFFFFFFFF
refin   false
refout  false
xorout  0
```

Stored field transform and exact domain are documented in the current status / image transport proof. A second independent private capture is desirable before release.

## Verified activation sequence

When `enable_chip(True)` times out or register 0 reads `06000000`, use the tested sequence:

```text
NOP
0xd4 TLS_SUCCESSFULLY_ESTABLISHED
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

Factory host PSK was recovered privately from the user's own Windows DPAPI-protected Goodix cache. **Never request, print, commit, hash publicly, or re-provision it.**

## FDT caveat

Current FDT-up success uses a private Windows-traced per-unit threshold set; generic derivation remains open and should be solved before claiming a broadly reusable upstream driver.

## Mandatory privacy/safety

Never publish or ask the user to upload:

- plaintext factory PSK;
- PSK files or hashes;
- full OTP;
- fingerprint images/raw frames/templates;
- `goodix.dat`, `goodix_calib.dat`, `Goodix_Cache.bin`;
- proprietary Goodix DLL/EXE/CAT binaries;
- Windows biometric database material;
- process memory dumps;
- full unit-specific 224-byte runtime config or its private hash.

Never erase/flash firmware or write/re-provision PSK.

Preserving Windows Hello is a release requirement.

For completion criteria, use `docs/RELEASE_READINESS_AND_SAFETY_GATES.md`. Do not claim universal mathematical `100% safety`; practical completion means all defined safety gates pass with no known unsafe behavior.

## Working style

The user prefers one terminal block at a time and pastes output. Avoid unnecessary questions. Preserve decisive checkpoints in this repository so a session/chat limit cannot destroy progress.
