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
2. `docs/RELEASE_READINESS_AND_SAFETY_GATES.md`
3. `docs/DEVELOPER_ROADMAP.md`
4. `docs/WINDOWS_IMAGE_LAYOUT_5135_PROOF_2026-08-27.md`
5. `docs/IMAGE_TRANSPORT_5135_PROOF_2026-08-27.md`
6. `docs/FDT_5135_PROOF_2026-08-27.md`
7. `docs/LINUX_CFG70_UPLOAD_PROOF_2026-08-27.md`
8. `docs/FAILURES_AND_RECOVERIES_2026-08-27.md`
9. `docs/SAFETY.md`

Older current-status/handoff documents are historical evidence and may describe blockers that have already been solved.

## Current state — do not repeat solved work

The project is **past config, TLS, FDT, image transport, image CRC, RAW12 decode, ChicagoHU regroup, ImageBase/live layout proof, and gfusb.dll result packaging/handoff**.

The Windows biometric layer above `gfusb.dll` has now also been identified.

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
full semantic preprocessor CFG               CURRENT TASK
exact preprocessing arithmetic               NOT YET PROVEN
matcher/enrollment                           LATER
libfprint/fprintd integration                 LATER
release/safety matrix                        FINAL STAGE
```

## Immediate next task

**Do not return to transport, ImageBase orientation, the detector, callback `0x180013280`, or request helper `0x18001393c` unless a new dependency requires it.**

The current reverse-engineering target is the complete reachable control flow beginning at:

```text
AlgoChicago.dll 0x18000e780
```

`preprocessor_wrapper` at RVA `0x0000b560` is only a seven-argument forwarding shim to this implementation.

Important correction: the first x64 `RUNTIME_FUNCTION` record covering the entry ends at `0x18000e880`, but code in that region directly branches to at least `0x18000e91a` and `0x18000e92e`. Therefore one unwind record is **not** the complete semantic routine boundary. Follow the control-flow graph across all reachable fragments/funclets until actual returns are reached.

Need exact proof, if present, for:

1. identities of the seven preprocessor arguments;
2. ImageBase input;
3. live-image input;
4. processed-image output;
5. calibration state/data;
6. subtraction direction and signedness;
7. clamp/saturation;
8. normalization/gain;
9. per-pixel correction/noise handling;
10. crop/grow/output dimensions;
11. quality/coverage outputs;
12. exact buffer passed into identification/enrollment.

Do not name unexplained status values such as EngineAdapter's observed special `0x84` result until the producer/meaning is proven from the algorithm code.

## Windows biometric architecture checkpoint

The installed package configures Windows Biometric Framework with the Windows sensor/storage adapters and Goodix `EngineAdapter.dll` as the vendor engine adapter.

Static EngineAdapter analysis proves family selection includes:

```text
family 0x03 -> Milan algorithm path
family 0x0c -> AlgoChicago.dll
family 0x0e -> AlgoChicagoT.dll
```

For this device, logical chip `0x2504` maps to family/type `0x0c`, therefore the active algorithm DLL is `AlgoChicago.dll`.

EngineAdapter resolves algorithm functions dynamically through `LoadLibraryW` / `GetProcAddress`, including preprocessing, calibration, sensor check, identify, enroll, and template operations.

This establishes that final preprocessing/matching is above `gfusb.dll`; do not search the USB driver for final matcher arithmetic.

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

This is a base/live detector/classifier, **not** the final matcher preprocessing output routine.

## gfusb.dll result handoff — solved

The concrete post-detection callback registered in `0x18059cb60` is `0x180013280`.

It receives the effective shape:

```text
(context, persisted ImageBase, regrouped live image, flags)
```

and packages the two image planes with metadata. No final per-pixel matcher preprocessing occurs there.

`0x18001393c` then completes the pending Windows request and hands the result to the higher biometric layer. This boundary is solved; do not spend more time there.

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

A second independent private capture is desirable before release as cross-capture confirmation.

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

- Windows upload length: exactly 224 bytes (`0xe0`).
- Correct static family: CFG70.
- Linux rebuild from live OTP matched the private Windows runtime reference byte-for-byte.
- Config checksum rule is proven.
- One controlled Linux `0x90` upload was accepted and post-upload calibration verified.
- Runtime `0x90` is a volatile MCU write; describe it accurately.
- Do not publish the full unit-specific runtime config or its private hash.

## FDT

- private `goodix.dat` layout: `OTP64 + FDT12 + NAV3200 + IMAGE10240 + CRC4`;
- FDT12 is six duplicated manual seed bytes;
- manual `0x36`: `0d01 + seed12`;
- down thresholds: `floor(raw/2)`, encoded as six `80 xx` pairs;
- down `0x32`: `0801 + regs12 + timestampLE`;
- up `0x34`: `0a02 + regs12`;
- current FDT-up success uses a private Windows-traced per-unit threshold set; generic derivation remains future work.

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

For completion criteria, use `docs/RELEASE_READINESS_AND_SAFETY_GATES.md`. Do not claim universal mathematical `100% safety`; the practical engineering target is **all defined safety gates passed and no known unsafe behavior**.

## Working style

The user prefers one terminal block at a time and pastes output. Avoid unnecessary questions. Preserve decisive checkpoints in this repository so a chat/session limit cannot destroy progress.
