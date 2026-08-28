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
6. `docs/CHICAGO_497C0_GATE_TO_B460_2026-08-28.md`
7. `docs/CHICAGO_GATED_FACTOR_UPDATE_B460_2026-08-28.md`
8. `docs/CHICAGO_E110_WRAPPER_TO_FFF0_2026-08-28.md`
9. `docs/CHICAGO_MODE9_GAUSSIAN_EXECUTOR_4FFF0_2026-08-28.md`
10. `docs/CHICAGO_MODE9_STREAMING_EXECUTOR_E380_2026-08-28.md`
11. `docs/CHICAGO_MODE9_HORIZONTAL_GAUSSIAN_F5F0_2026-08-28.md`
12. `docs/CHICAGO_MODE9_VERTICAL_GAUSSIAN_FD20_2026-08-28.md`
13. `docs/RELEASE_READINESS_AND_SAFETY_GATES.md`
14. `docs/DEVELOPER_ROADMAP.md`
15. `docs/WINDOWS_IMAGE_LAYOUT_5135_PROOF_2026-08-27.md`
16. `docs/IMAGE_TRANSPORT_5135_PROOF_2026-08-27.md`
17. `docs/FDT_5135_PROOF_2026-08-27.md`
18. `docs/LINUX_CFG70_UPLOAD_PROOF_2026-08-27.md`
19. `docs/FAILURES_AND_RECOVERIES_2026-08-27.md`
20. `docs/SAFETY.md`

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
state-source signed subtraction              PROVEN 0x180044970
mask geometry/source-range cleanup           PROVEN 0x180043c40
post-mask parent correction                  PROVEN 0x18004aea0
persistent corrected plane                   PROVEN state+0x13244
Q13 primary/secondary surfaces               PROVEN
late type-0x0c Q13 composition               PROVEN
base scratch_A adaptive update               PROVEN 0x18004d6f0
spatial ratio median filter                  PROVEN 0x18004c3b0
0x1800497c0 caller role                      PROVEN boolean gate
gated late-factor updater                    PROVEN 0x18004b460
0x18004e110 role                             PROVEN generic wrapper/orchestrator
mode-9 static selector 0x18004fbf0           PROVEN
mode-9 kernel                                PROVEN 5-tap Gaussian sigma=1.5 Q16
0x18004fff0 role                             PROVEN geometry/dispatch wrapper
0x18004e380 role                             PROVEN streaming/ring-buffer orchestrator
mode-9 horizontal pass                       PROVEN 0x18004f5f0
mode-9 vertical pass                         PROVEN 0x18004fd20
mode-9 2D structure                          PROVEN separable 5x5 Gaussian
vertical bias/shift derivation                CURRENT via 0x18004f480/context builder
matcher/enrollment                           LATER
libfprint/fprintd integration                LATER
release/safety matrix                        FINAL
```

## Preprocessing proof summary

### Signed difference — IMPORTANT CORRECT DIRECTION

For selector `0x0c`, `0x180044970` performs:

```text
diff16[i] = state_plus_0x9924_u16[i] - source_u16[i]
```

This direction is proven independently by both SIMD and scalar code:

```asm
xmm1 = state_plane
xmm0 = source
psubw xmm1,xmm0
```

and the scalar tail loads state then subtracts source. Negative values are intentionally retained as signed 16-bit.

For selector `4` only:

```text
state_plane - source + 0x0fff
```

Older text claiming `source - state_plane` is stale and must not be reused.

Do **not** equate AlgoChicago `state+0x9924` with gfusb persisted ImageBase until its producer is independently proven.

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

It later writes the persistent corrected image-like plane at `state+0x13244`, using shift 14:

```text
if gate_word[i] != 0:
    if scratch_A[i] == 0:
        processed[i] = scratch_C[i] << 14
    else:
        processed[i] = round((scratch_C[i] << 14) / scratch_A[i])
else:
    processed[i] = scratch_C[i]
```

### Q13 composition

Inside `0x180049ba0`, `scratch_A` starts at Q13 unity:

```text
0x2000 == 8192
```

with exact rounded multiplication:

```text
Q13_mul(a,b) = (a*b + 0x1000) >> 13
```

Late type-`0x0c` composition of `scratch_A` and `scratch_B` is proven; see `docs/CHICAGO_POST_MASK_STAGE_4AEA0_2026-08-28.md`.

## Base adaptive update `0x18004d6f0` — PROVEN

It forms:

```text
if scratch_A[i] == 0:
    ratio_raw[i] = reference[i] << 13
else:
    ratio_raw[i] = round((reference[i] << 13) / scratch_A[i])
```

then applies `0x18004c3b0`.

For type `0x0c`:

```text
threshold = 1800
```

and:

```text
q = ratio_filtered[i]
x = work_object.data[i]

if q != 0 and x != 0 and abs(q-x) > 1800:
    scratch_A[i] = min(
        round((scratch_A[i] * q) / x),
        0x7fff
    )
```

otherwise `scratch_A[i]` is unchanged.

## Spatial median `0x18004c3b0` — PROVEN

For interior pixels:

```text
H(y,x) = median(src[y][x-1], src[y][x], src[y][x+1])
out(y,x) = median(H(y-1,x), H(y,x), H(y+1,x))
```

Borders are copied unchanged. This is separable median-of-three horizontal then vertical, not the true median of all nine 3x3 samples.

## `0x1800497c0` caller role — PROVEN

On the tested path:

```text
call 0x1800497c0
test eax,eax
je   skip_B460_branch
```

`scratch_A` is not passed directly to this gate. If nonzero, `0x18004b460` is called twice with `R8 = scratch_A`.

Do not descend into `0x1800497c0` unless the exact predicate becomes necessary later.

## Gated late-factor updater `0x18004b460` — PROVEN at parent level

`0x18004b460` reads `scratch_A` but does not write it directly. It adaptively updates late per-pixel Q13 factor surfaces that are composed into `scratch_A/scratch_B` later in the same `0x180049ba0` invocation.

Initial Q13 ratio:

```text
if scratch_A[i] == 0:
    ratio32[i] = reference[i] << 13
else:
    ratio32[i] = round((reference[i] << 13) / scratch_A[i])
```

If an optional factor is present:

```text
combined = Q13_mul(scratch_A[i], optional_factor[i])
```

and the temporary ratio uses `reference/combined`.

Then:

```text
0x1800501a0(...)
0x18004e110(temp_object, work_object, packed_dims, 9, -1, -1)
```

After that operation:

```text
if work[i] != 0:
    local_ratio = round((temp_filtered[i] << 13) / work[i])
else:
    local_ratio = 0x2000
```

Update only when:

```text
abs(local_ratio - 0x2000) < 0x148
```

where `0x148 = 328`.

With `n = observation_count`:

```text
factor[i] = round((factor[i]*n + local_ratio) / (n+1))
observation_count = min(observation_count + 1, 30)
```

## `0x18004e110` — wrapper, not pixel filter

Exact PE region `0x18004e110..0x18004e378`; real return `0x18004e377`.

It validates/reconciles image objects, temporarily rewrites format metadata, builds context using `0x18004e820`, dispatches through `0x18004fff0`, restores metadata and frees context allocations.

Mode `9` sets `ctx+0x88 = 0`. Modes `6` or `8` set it to `1`. Therefore the constant `9` is a mode selector, not a 9x9-window size.

## Mode-9 static Gaussian kernel — PROVEN

`0x18004fbf0` uses `0x40`-byte mode records. For mode `9`, class `4`:

```text
record = 0x1800932b0 + 9*0x40
       = 0x1800934f0

count = 5
coefficients = [7869, 15328, 19142, 15328, 7869]
sum = 65536
```

These are exactly the normalized discrete Gaussian samples at `[-2,-1,0,1,2]` for `sigma=1.5`, rounded to Q16.

Therefore:

```text
mode 9 = 1D 5-tap Gaussian, sigma=1.5, Q16 coefficients
```

## Mode-9 separable Gaussian application — PROVEN

`0x18004f5f0` is the horizontal pixel helper. It reads the five Q16 coefficients through `ctx+0x90` and computes:

```text
H[y,x] = (
    7869  * src[y,x-2]
  + 15328 * src[y,x-1]
  + 19142 * src[y,x]
  + 15328 * src[y,x+1]
  + 7869  * src[y,x+2]
) >> 16
```

There is no `+0x8000` before the shift in this pass; Q16 conversion is truncation. The intermediate output is stored as DWORD values.

`0x18004fd20` is the second-axis helper. It reads an array of pointers to those horizontally filtered DWORD rows and uses the descriptor at `ctx+0x98`. For five taps, it centers on the middle row and computes symmetrically:

```text
V[y,x] = (
    7869  * H[y-2,x]
  + 15328 * H[y-1,x]
  + 19142 * H[y,x]
  + 15328 * H[y+1,x]
  + 7869  * H[y+2,x]
  + vertical_bias
) >> vertical_shift
```

then stores a U16 output word.

Thus mode 9 is **PROVEN separable 5x5 Gaussian**: horizontal five-tap pass followed by vertical five-tap pass, rather than a monolithic 25-coefficient convolution.

The only unresolved numeric detail in this Gaussian stage is how the context builder derives:

```text
ctx+0x98 descriptor +0x28 = vertical_bias
ctx+0x98 descriptor +0x2c = vertical_shift
```

Do not assume `vertical_bias=0x8000` or `vertical_shift=16` until statically proven.

## `0x18004fff0` — geometry/dispatch wrapper

No coefficient loop. It computes a start index via `0x18004ddf0`, then calls:

```text
0x18004e380(
    ctx,
    input.data + input[0x08] * start_index,
    input[0x08],
    ctx[0x74] - ctx[0x6c],
    output.data,
    output[0x08]
)
```

## `0x18004e380` — PROVEN streaming/ring-buffer orchestrator

The logical routine crosses unwind regions and returns at `0x18004e817`.

It manages context geometry and rolling row/span state, copies incoming data into context-owned working/ring buffers, constructs stored-row pointers and dispatches actual per-row processing.

For mode `9`, `ctx+0x88 == 0`, proving the selected helpers are:

```text
0x18004f5f0  horizontal Gaussian helper
0x18004fd20  vertical Gaussian/output helper
```

and these alternate helpers are **not** selected:

```text
0x18004f790
0x18004f940
```

## Immediate next task

Reverse exact routine:

```text
AlgoChicago.dll 0x18004f480
```

Focus only on the construction of the descriptors later stored at `ctx+0x90` and `ctx+0x98`, especially the second descriptor fields consumed by `0x18004fd20`:

```text
+0x28 additive bias
+0x2c right-shift count
```

Need to prove the exact mode-9 values and close the Gaussian output equation. If `0x18004f480` delegates those calculations, descend only into the decisive child helper.

After closing this gated factor update, return to final Q13 composition and trace `state+0x13244` toward matcher/enrollment. Independently prove the producer of `state+0x9924` before equating it with gfusb ImageBase.

Do not return to USB/TLS/CRC/regroup/gfusb callbacks, `0x180044970`, `0x180043c40`, `0x18004d6f0`, `0x18004c3b0`, `0x1800497c0`, `0x18004b460`, `0x18004e110`, `0x18004fff0`, `0x18004e380`, `0x18004f5f0`, or `0x18004fd20` unless a newly discovered dependency requires it.

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
