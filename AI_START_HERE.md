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

## Canonical checkpoints

Read first:

1. `docs/CURRENT_STATUS_2026-08-28.md`
2. `docs/CHICAGO_PREPROCESS_CORE_2026-08-28.md`
3. `docs/CHICAGO_POST_MASK_STAGE_4AEA0_2026-08-28.md`
4. `docs/CHICAGO_Q13_BASE_UPDATE_D6F0_2026-08-28.md`
5. `docs/CHICAGO_SPATIAL_MEDIAN_FILTER_C3B0_2026-08-28.md`
6. `docs/RELEASE_READINESS_AND_SAFETY_GATES.md`
7. `docs/DEVELOPER_ROADMAP.md`
8. `docs/WINDOWS_IMAGE_LAYOUT_5135_PROOF_2026-08-27.md`
9. `docs/IMAGE_TRANSPORT_5135_PROOF_2026-08-27.md`
10. `docs/FDT_5135_PROOF_2026-08-27.md`
11. `docs/LINUX_CFG70_UPLOAD_PROOF_2026-08-27.md`
12. `docs/FAILURES_AND_RECOVERIES_2026-08-27.md`
13. `docs/SAFETY.md`

Older handoff/current-status documents are historical evidence and may describe blockers already solved.

## Do not repeat solved work

```text
CFG70 reconstruction                         DONE
command 0x90 upload                          DONE
factory TLS                                  DONE
activation/reset recovery                    DONE
FDT manual/down/up                           DONE on tested unit
image transport/decrypt                      DONE
Windows-compatible image CRC                 DONE on one private capture
RAW12 -> 5120 samples                        DONE
ChicagoHU regroup                            PROVEN
ImageBase downstream layout                  PROVEN
Candidate A                                  PROVEN
Candidate B double-regroup                   REJECTED
gfusb post-detection packaging               PROVEN
WBDI EngineAdapter architecture              PROVEN
family 0x0c -> AlgoChicago.dll               PROVEN
preprocessor wrapper / real entry            PROVEN
outer semantic preprocessor                  PROVEN 0x18000e780..0x18000e947
core orchestrator                            PROVEN 0x1800484e0
source/state signed subtraction              PROVEN 0x180044970
mask geometry/source-range cleanup            PROVEN 0x180043c40
post-mask parent correction                  PROVEN 0x18004aea0
persistent corrected plane                   PROVEN state+0x13244
Q13 primary/secondary surfaces               PROVEN
late type-0x0c Q13 composition               PROVEN
base scratch_A adaptive update               PROVEN 0x18004d6f0
spatial ratio filter                         PROVEN 0x18004c3b0
current task                                 caller region after 0x1800497c0
matcher/enrollment                           LATER
libfprint/fprintd integration                 LATER
release/safety matrix                        FINAL
```

## Preprocessing proof summary

### Signed difference

For selector `0x0c`, `0x180044970` performs:

```text
diff16[i] = source_u16[i] - state_plus_0x9924_u16[i]
```

Negative values are intentionally retained as signed 16-bit. Do **not** equate AlgoChicago `state+0x9924` with gfusb persisted ImageBase until its producer is proven.

### Mask cleanup

`0x180043c40` cleans/fills mask geometry and recomputes coverage. For type `0x0c`, a mask pixel is rejected when:

```text
source <= 100
or
source >= 3800
```

### Post-mask parent correction

`0x18004aea0` allocates `scratch_A/B/C`. For type `0x0c`:

```text
scratch_C[i] = 3 * source_side_word[i]
global_work[i] = 3 * state_plus_0x9924[i]
```

It later writes the persistent corrected image-like plane:

```text
state + 0x13244
```

using shift 14 and the primary relation:

```text
if gate_word[i] != 0:
    if scratch_A[i] == 0:
        processed[i] = scratch_C[i] << 14
    else:
        processed[i] = round((scratch_C[i] << 14) / scratch_A[i])
else:
    processed[i] = scratch_C[i]
```

### Q13 surface composition

Inside `0x180049ba0`, `scratch_A` starts at Q13 unity:

```text
0x2000 == 8192
```

and surfaces compose with exact rounded Q13 multiplication:

```text
Q13_mul(a,b) = (a*b + 0x1000) >> 13
```

Late type-`0x0c` composition is proven for `scratch_A` and `scratch_B`; see `docs/CHICAGO_POST_MASK_STAGE_4AEA0_2026-08-28.md`.

## Base adaptive update `0x18004d6f0` — PROVEN

Caller mapping on the tested `0x0c` path:

```text
RCX = reference/global plane side
RDX = work object; data pointer at +0x18
R8  = scratch_A (in/out primary Q13 surface)
R9D = one geometry dimension
arg5 = other geometry dimension
arg6 = selector 0x0c
```

It builds a temporary ratio plane:

```text
if scratch_A[i] == 0:
    ratio_raw[i] = reference[i] << 13
else:
    ratio_raw[i] = round((reference[i] << 13) / scratch_A[i])
```

and calls:

```text
0x18004c3b0(ratio_raw_object, work_object)
```

For type `0x0c`, the post-filter threshold is:

```text
0x708 = 1800
```

and the update is:

```text
q = ratio_filtered[i]
x = work_object.data[i]

if q != 0 and x != 0 and abs(q-x) > 1800:
    scratch_A[i] = min(
        round((scratch_A[i] * q) / x),
        0x7fff
    )
else:
    scratch_A[i] unchanged
```

## Spatial filter `0x18004c3b0` — PROVEN

`0x18004c3b0` is a deterministic integer separable median-of-three filter.

For interior pixels define:

```text
H(y,x) = median(src[y][x-1], src[y][x], src[y][x+1])
```

then:

```text
out[y][x] = median(H(y-1,x), H(y,x), H(y+1,x))
```

for:

```text
1 <= x < width-1
1 <= y < height-1
```

The border is copied unchanged. The routine uses two temporary u16 rows and ends via tail-call to the allocation-release helper `0x180064d60`.

This is a separable median-style 3x3 filter; it is not generally equal to the true median of all nine 3x3 samples.

Combined with `0x18004d6f0`:

```text
ratio_raw
 -> separable median-of-3 horizontal
 -> separable median-of-3 vertical
 -> ratio_filtered
 -> compare against work plane
 -> thresholded scratch_A rescale
```

So the base type-`0x0c` Q13 update is now closed at this level.

## Immediate next task

Return to the tested selector-`0x0c` caller path in `0x180049ba0`.

The next call in execution order is:

```text
0x1800497c0
```

but the already-recovered call arguments do **not** visibly pass `scratch_A` directly. Therefore do not descend into `0x1800497c0` yet.

First inspect the caller region:

```text
0x18004a1d0 .. 0x18004a2f0
```

Need to prove:

1. complete control flow after `call 0x1800497c0`;
2. what its return value controls;
3. whether any path before `0x18004a2f0` modifies/copies `scratch_A` or `scratch_B`;
4. descend into `0x1800497c0` only if its output/side effects are proven to feed the Q13 correction surfaces;
5. otherwise skip it and continue to the next proven denominator-modifying stage;
6. after the correction surface is fully closed, trace `state+0x13244` into matcher/enrollment-facing processing;
7. independently prove the producer of `state+0x9924` before equating it with gfusb ImageBase.

Do not return to USB/TLS/CRC/regroup/gfusb callbacks, `0x180044970`, `0x180043c40`, `0x18004d6f0`, or `0x18004c3b0` unless a newly discovered dependency requires it.

## Safety / privacy

Never publish or ask the user to upload:

- plaintext factory PSK or hashes;
- full OTP;
- fingerprint images/raw frames/templates;
- `goodix.dat`, `goodix_calib.dat`, `Goodix_Cache.bin`;
- proprietary Goodix DLL/EXE/CAT binaries;
- Windows biometric database material;
- process memory dumps;
- full unit-specific runtime configuration or private hashes.

Never erase/flash firmware or write/re-provision PSK. Preserving Windows Hello is a release requirement.

## Working style

One terminal block at a time. Do not repeat solved steps. Use proof-driven labels (`PROVEN` vs open hypothesis) and preserve decisive checkpoints in this repository.
