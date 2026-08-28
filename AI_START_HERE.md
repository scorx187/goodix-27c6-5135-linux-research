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

## Read these first

1. `docs/CURRENT_STATUS_2026-08-28.md`
2. `docs/CHICAGO_MATCHER_CORRESPONDENCE_REDUCTION_2026-08-28.md`
3. `docs/CHICAGO_MATCHER_CORRESPONDENCE_5BAF0_2026-08-28.md`
4. `docs/CHICAGO_FEATURE_EXTRACTION_AND_PRUNING_2026-08-28.md`
5. `docs/CHICAGO_435A0_LOCAL_CONTRAST_NORMALIZATION_2026-08-28.md`
6. `docs/CHICAGO_PREPROCESS_CORE_2026-08-28.md`
7. `docs/CHICAGO_POST_MASK_STAGE_4AEA0_2026-08-28.md`
8. `docs/CHICAGO_MODE9_GAUSSIAN_EXECUTOR_4FFF0_2026-08-28.md`
9. `docs/CHICAGO_MODE9_STREAMING_EXECUTOR_E380_2026-08-28.md`
10. `docs/CHICAGO_MODE9_HORIZONTAL_GAUSSIAN_F5F0_2026-08-28.md`
11. `docs/CHICAGO_MODE9_VERTICAL_GAUSSIAN_FD20_2026-08-28.md`
12. `docs/RELEASE_READINESS_AND_SAFETY_GATES.md`
13. `docs/DEVELOPER_ROADMAP.md`
14. `docs/SAFETY.md`

Older handoff/status text may describe blockers already solved. `docs/CURRENT_STATUS_2026-08-28.md` is the current high-level truth.

## Current position

Do **not** restart from USB, TLS, image decode, preprocessing, Gaussian analysis, feature detection, descriptor extraction, or post-extraction pruning.

The active work is inside the matcher correspondence/scoring path of `AlgoChicago.dll`.

Current reconstructed path:

```text
USB/TLS/image acquisition                              DONE
RAW12 + ChicagoHU regroup                              DONE
Windows-compatible 80x64 image                         DONE
preprocessing/correction/normalization                 SUBSTANTIALLY CLOSED
quality/segmentation/output-mask gates                 PROVEN
accepted fingerprint-feature extraction                PROVEN
retained 0x3c feature records                          PROVEN
orientation + local binary descriptors                 PROVEN substantially
post-extraction pruning / per-feature refinement       PROVEN substantially
identify matcher wrapper/orchestrator                   PROVEN
multi-channel matcher / score fusion                    PROVEN role
pairwise binary-descriptor correspondence 0x5baf0      PROVEN
correspondence consolidation 0x5a350                   PROVEN structurally
correspondence inlier reduction 0x1f840               PROVEN structurally
exact inlier geometry helper 0x586c0                  CURRENT
additional pair filter 0x24880                        CURRENT
final scoring details                                  AFTER THESE
enrollment update/template storage                     AFTER MATCHER
libfprint/fprintd                                      LATER
release/lifecycle safety matrix                        FINAL
```

## Critical facts — do not regress

### Preprocessing subtraction direction

For selector/type `0x0c`:

```text
diff16[i] = state_plus_0x9924_u16[i] - source_u16[i]
```

It is state minus source, wrapping as U16 and later sign-extended.

Selector `4` has:

```text
state_plane - source + 0x0fff
```

Never change this back to source-minus-state.

Do **not** equate AlgoChicago `state+0x9924` with gfusb persisted ImageBase until its producer is independently proven.

## Preprocessing is no longer the active blocker

Proven later stages include:

```text
state+0x13244 corrected U16
  -> 0x1800435a0 local contrast normalization
  -> state+0x1cb64 normalized U8
  -> 0x180050550 directional edge/texture mask
  -> 0x180046ff0 quality/segmentation gate
  -> 0x180046930 multi-policy orchestrator
  -> 0x180045bf0 mask consistency/coverage evaluator
  -> 0x1800547c0 final U8 output-mask/validity stage
  -> 0x18000e780 outer copy-out
```

Mode 9 is already closed as a separable 5x5 Gaussian with Q16 kernel:

```text
[7869, 15328, 19142, 15328, 7869]
```

including the fixed-point/border path needed for the current reconstruction.

## Matcher/enrollment entry points

```text
preprocessor_wrapper      RVA 0x0000b560 -> 0x18000e780
identifyImageWrapper      RVA 0x0000b790 -> 0x18000d6c0
enrolAddImageWrapper      RVA 0x0000b6b0 -> 0x18000cd70
getQuality                RVA 0x0000d440
```

`0x180016920` is a thunk to the shared representation builder `0x1800139e0`, used by both identify and enrollment.

Known output-object fields:

```text
output+0xf0 = retained feature count
output+0xf8 = retained feature-array pointer
feature stride = 0x3c bytes
```

## Retained feature record — current map

```text
+0x00        local flag / quality-like field; exact name unclaimed
+0x02        X Q8
+0x04        Y Q8
+0x06        signed Q12-radian direction
+0x08        response/strength-like field; exact name unclaimed
+0x0c        Y-border classification
+0x10..+0x1f 128-bit Pass-A sign/projection descriptor
+0x20..+0x27 64-bit Pass-A lower-median-threshold descriptor
+0x28..+0x2b 32-bit Pass-B sign/projection descriptor
+0x2c..+0x2f 32-bit Pass-B lower-median-threshold descriptor
+0x30..+0x37 cleared/reserved on the normal type-0x0c extraction path before later consumers
+0x38        status/validity-like byte
+0x39        neighborhood/spatial-support score
```

## Matcher ABI — proven

`0x18000d6c0` builds the probe then loops candidate templates.

`0x180028c90` is the per-candidate matcher wrapper/orchestrator.

Effective high-level ABI:

```text
first argument  = match-result / score output pointer
second argument = probe fingerprint object
third argument  = candidate/template container
return EAX      = execution/status
```

Do not confuse `EAX` from `0x28c90` with the match score. Identify checks it for execution failure/status; the match result is written through the first argument.

## Matcher structure now established

```text
identify 0x18000d6c0
  -> 0x180028c90 matcher wrapper
  -> 0x1800293c0 matcher orchestrator / score fusion
       -> 0x180028de0 early multi-channel matcher
            -> 0x180059580 primary correspondence builder
                 -> 0x18005baf0 pairwise descriptor-cost primitive [twice]
                 -> 0x18005a350 ambiguity/spatial/top-K consolidation
            -> 0x18001f840 geometric/directional correspondence reduction
                 -> 0x1800586c0 inlier-mask helper             <-- CURRENT
                 -> 0x180024880 additional pair filter         <-- CURRENT
                 -> optional 0x180058f90 refinement
            -> 0x180050c80 spatial/image similarity channel
            -> 0x180058420 alternate/refined correspondence state
            -> 0x18001f840 refined reduction
            -> 0x180051980 / 0x18005a6b0 additional relation metrics
       -> type/policy gates including 0x1800258c0 / 0x18001c920
       -> final score fusion / output
```

`0x1800258c0` and `0x18001c920` are policy/threshold predicates over precomputed metrics, not raw descriptor comparators.

`0x180050c80` is primarily a spatial/image-representation similarity channel; it does not directly iterate the `+0xf8` retained minutia list.

## `0x18005baf0` — direct pairwise descriptor matcher

Important unwind correction: do not truncate this function at its first PE runtime entry.

Full CFG:

```text
0x18005baf0 .. 0x18005bf35
273 reachable instructions
3 runtime/unwind entries
```

It directly traverses both `0x3c` feature lists with nested loops. The primary comparison consumes `feature+0x10..+0x1f`, XORs descriptor DWORDs, and sums per-byte lookup values from `0x180079730`. This is a Hamming-like byte-lookup binary-descriptor distance; do not call the table exact popcount until its contents are independently verified.

It also consumes selected later binary fields and maintains best/second-best costs and opposite-list indices.

## `0x18005a350` — correspondence consolidation

This function is **not** a direct reciprocal-NN test.

It applies an integer best-vs-second-best ambiguity relation:

```text
best_cost * cfg_A < second_cost * cfg_B
```

Then it suppresses spatially competing selected features using Q8 X/Y from `+0x02/+0x04`. The exact machine-domain duplicate condition is:

```text
(dx_q8 * dx_q8) + (dy_q8 * dy_q8) < 0x10000
```

For a conflict it retains the lower-cost candidate.

It maintains a bounded best-K selected set. Once full, it replaces the current worst retained cost only when a lower-cost new pair arrives.

It emits flat signed `int32` correspondence pairs:

```text
[index_A0,index_B0,index_A1,index_B1,...]
```

and pads unused pair slots with `-1`.

Direction `+0x06` is not consumed by `0x5a350` itself.

## `0x18001f840` — correspondence inlier survivor count

Another important unwind split:

```text
0x18001f840 .. 0x18001fbb0
197 reachable instructions
3 runtime/unwind entries
```

The function consumes the flat pair array, resolves candidate/probe feature arrays at `+0xf8`, and for every nonnegative pair copies from both `0x3c` records:

```text
+0x02 X Q8
+0x04 Y Q8
+0x06 signed Q12-radian direction
```

Thus direction consistency enters the matcher here.

It counts valid pairs and returns zero unless:

```text
valid_pair_count > arg5_minimum
```

It then calls `0x1800586c0`, which structurally produces a byte inlier mask plus an auxiliary geometry/fixed-point metric. Mask-zero pairs are invalidated.

Next it calls `0x180024880`, which itself traverses referenced `0x3c` records and reads direction `+0x06`. Pairs invalidated by it clear the corresponding inlier-mask byte.

Finally:

```text
survivor_count = count(mask[i] != 0)
EAX = survivor_count
```

So the integer stored on the early path at `match_record+0x00/+0x04` is a surviving correspondence/inlier count, not a probabilistic similarity score.

If the survivor count is at least four and the geometry metric exceeds `0x4000`, `0x1f840` invokes `0x180058f90`. It returns the already-counted `EBX` afterward without recounting, so `0x58f90` is not required to define this returned integer.

## Immediate next task — do not branch away

Reverse these two functions:

```text
0x1800586c0
0x180024880
```

Answer only the decisive questions:

1. What exact transform/geometry consistency model does `0x586c0` use?
2. How exactly is its byte inlier mask generated?
3. What is the scale/meaning of the auxiliary metric later compared with `0x4000`?
4. What exact per-pair direction/feature rule does `0x24880` use to set correspondence indices to `-1`?

Only after those are closed should work return to minimum remaining final score/policy fusion.

Do **not** descend into `0x58f90` unless a downstream consumer proves its side effect is required.

Then move to enrollment update/template storage, Linux implementation, libfprint/fprintd integration, and lifecycle/safety tests.

## What not to repeat

Do not restart or re-prove unless a new dependency demands it:

- USB transport / CFG70 / command `0x90`;
- factory TLS / activation / FDT;
- image `0x20`, framing, CRC, RAW12, ChicagoHU regroup;
- gfusb ImageBase packaging;
- preprocessing / mode-9 Gaussian / mask-quality stages;
- feature detector/emitter / local orientation histogram;
- descriptor packing;
- `0x371c0` / `0x3b820` pruning;
- `0x16930` / `0x16bd0` feature quality/support;
- `0xede0` reorder/finalizer;
- `0x28c90` wrapper ABI;
- `0x258c0` / `0x1c920` policy roles;
- `0x50c80` spatial similarity role;
- whether `0x5baf0` directly compares descriptors: it does;
- whether `0x5a350` is reciprocal-NN: no evidence; recovered logic is ambiguity + spatial + top-K selection;
- what `0x1f840` returns: surviving correspondence/inlier count.

## Release readiness definition

Do not call the project complete until all defined safety gates pass with no known unsafe behavior:

- `fprintd-enroll` works;
- `fprintd-verify` works;
- cold boot/reboot works;
- suspend/resume works;
- cancellation/timeouts recover;
- no secrets/biometric payloads in logs;
- Windows Hello still works;
- existing Windows fingerprints still work;
- no firmware erase/flash;
- no factory PSK rewrite/reprovision.

## Safety / privacy — strict

Never ask for, print, commit, publish, upload, or publicly hash:

- plaintext factory PSK;
- PSK files/hashes;
- full OTP;
- fingerprint images/raw/templates;
- `goodix.dat`;
- `goodix_calib.dat`;
- `Goodix_Cache.bin`;
- proprietary Goodix DLL/EXE/CAT files;
- Windows biometric DB material;
- full process/memory dumps;
- full unit-specific 224-byte runtime config;
- unit-specific runtime-config hash.

Never firmware erase/flash, rewrite/reprovision the PSK, run destructive 5117 tooling, perform arbitrary persistent register writes, or remove/re-enroll Windows fingerprints as a shortcut.

The Windows partition must remain read-only during analysis.