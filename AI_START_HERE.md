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
3. `docs/CHICAGO_POST_MASK_STAGE_4AEA0_2026-08-28.md`
4. `docs/RELEASE_READINESS_AND_SAFETY_GATES.md`
5. `docs/DEVELOPER_ROADMAP.md`
6. `docs/WINDOWS_IMAGE_LAYOUT_5135_PROOF_2026-08-27.md`
7. `docs/IMAGE_TRANSPORT_5135_PROOF_2026-08-27.md`
8. `docs/FDT_5135_PROOF_2026-08-27.md`
9. `docs/LINUX_CFG70_UPLOAD_PROOF_2026-08-27.md`
10. `docs/FAILURES_AND_RECOVERIES_2026-08-27.md`
11. `docs/SAFETY.md`

Older handoff/current-status documents are historical evidence and may describe blockers already solved.

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
outer semantic preprocessor routine          PROVEN: 0x18000e780..0x18000e947
core preprocessing orchestrator              PROVEN: 0x1800484e0
first source/state subtraction               PROVEN: 0x180044970
mask geometry/source-range cleanup            PROVEN: 0x180043c40
post-mask correction stage                   PROVEN: 0x18004aea0
persistent corrected plane                   PROVEN: state+0x13244
primary Q13 correction surface               PROVEN: scratch_A starts at 0x2000 unity
type-0x0c Q13 composition path               CURRENT TASK inside 0x180049ba0
matcher/enrollment                           LATER
libfprint/fprintd integration                 LATER
release/safety matrix                        FINAL STAGE
```

## Preprocessing proof so far

### Stage 1 — signed source/state difference

`0x1800484e0` copies the source u16 image and a second AlgoChicago internal preprocessing-state plane from `state+0x9924`, then calls `0x180044970`.

For tested type `0x0c`:

```text
diff16[i] = source_u16[i] - state_plane_u16[i]
```

No clamp/saturation/gain is performed in that first subtraction. Later consumers sign-extend the difference, proving negative values are intentional.

Do **not** yet equate AlgoChicago `state+0x9924` with gfusb persisted ImageBase; their population/data-flow identity is still unproven.

### Stage 2 — mask cleanup and coverage

`0x180043c40` consumes the mask/statistics structure, computes row/column occupancy, fills small holes/geometry gaps, applies source-value validity filtering, and recomputes coverage.

For tested type `0x0c`, a mask pixel is rejected when the corresponding original source u16 value is outside the accepted interior range:

```text
source <= 100
or
source >= 3800
```

This routine is mask/coverage cleanup, not image normalization.

### Stage 3 — post-mask full-plane correction

After the common join in `0x1800484e0`, `0x18004aea0` runs.

It allocates three full-size u16 scratch planes and, on the tested type-`0x0c` branch, pre-scales:

```text
scratch_C[i] = 3 * source_side_word[i]
global_work[i] = 3 * state_plus_0x9924[i]
```

It then runs:

```text
0x18004d3b0
optional 0x18004b290
0x180049ba0
final fixed-point per-WORD output loop
```

The parent finally writes a persistent corrected image-like plane to:

```text
state + 0x13244
```

For tested type `0x0c`, the parent uses fixed-point shift `14` and the primary relation is:

```text
if gate_word[i] != 0:
    if scratch_A[i] == 0:
        processed[i] = scratch_C[i] << 14
    else:
        processed[i] = round((scratch_C[i] << 14) / scratch_A[i])
else:
    processed[i] = scratch_C[i]
```

with `processed = state+0x13244`.

The three scratch buffers are freed only after this persistent plane is written.

### Stage 4 — `0x180049ba0` primary Q13 surface

`0x180049ba0` is a substantial multi-pass correction-surface builder, not a simple denominator helper.

For selector `0x0c`:

- it takes the non-`0x0b` branch;
- it performs non-negative WORD difference/threshold transforms;
- it initializes the descriptor plane mapped as `scratch_A` to `0x2000` for every pixel;
- `0x2000 == 8192` is Q13 unity;
- subsequent scalar and SIMD paths compose per-pixel factors using Q13 multiplication.

Scalar pattern:

```text
result = (a * b + 0x1000) >> 13
```

SIMD equivalent uses integer operations such as:

```text
pmulld
paddd
psrad
```

Therefore the safe semantic label is:

```text
scratch_A = composed Q13 per-pixel correction/gain surface
```

`scratch_B` is a secondary related Q13 surface; exact type-0x0c copy/divergence conditions remain open.

## Immediate next task

**Do not return to USB/TLS/CRC/regroup/gfusb callbacks, `0x180044970`, or `0x180043c40` unless a newly discovered dependency requires it.**

Do not reverse all 25 child calls in `0x180049ba0`.

Isolate only the tested selector `0x0c` path inside:

```text
AlgoChicago.dll 0x180049ba0
```

The key control-flow region is approximately:

```text
0x18004a0c0 .. 0x18004aa94
```

Important selector checkpoints already known:

```text
0x18004a0d1: cmp r8d,0x0c
```

Later dispatch:

- selector `0x0c` is not in bit mask `0x00412030`;
- it reaches `0x18004a405`;
- `0x0c - 0x0b == 1`, so it takes the branch to `0x18004a727`;
- that path eventually rejoins at `0x18004aa94`.

Need to prove next:

1. exactly which Q13 factor planes modify `scratch_A` on the type-`0x0c` branch;
2. when and how `scratch_B` copies or diverges from `scratch_A`;
3. exact roles of `0x18004d6f0`, `0x1800497c0`, `0x18004b460`, and the final `0x180049530` call **only where their outputs are proven to feed the tested Q13 surface**;
4. then trace `state+0x13244` into its next downstream consumer and matcher-facing input;
5. independently, prove the producer of `state+0x9924` before equating it to gfusb ImageBase.

Do not assign semantic names to unexplained result/status codes until proven.

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

A second independent private capture is desirable before release.

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
