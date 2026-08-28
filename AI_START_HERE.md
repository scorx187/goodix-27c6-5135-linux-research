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
2. `docs/CHICAGO_MATCHER_CORRESPONDENCE_5BAF0_2026-08-28.md`
3. `docs/CHICAGO_FEATURE_EXTRACTION_AND_PRUNING_2026-08-28.md`
4. `docs/CHICAGO_435A0_LOCAL_CONTRAST_NORMALIZATION_2026-08-28.md`
5. `docs/CHICAGO_PREPROCESS_CORE_2026-08-28.md`
6. `docs/CHICAGO_POST_MASK_STAGE_4AEA0_2026-08-28.md`
7. `docs/CHICAGO_MODE9_GAUSSIAN_EXECUTOR_4FFF0_2026-08-28.md`
8. `docs/CHICAGO_MODE9_STREAMING_EXECUTOR_E380_2026-08-28.md`
9. `docs/CHICAGO_MODE9_HORIZONTAL_GAUSSIAN_F5F0_2026-08-28.md`
10. `docs/CHICAGO_MODE9_VERTICAL_GAUSSIAN_FD20_2026-08-28.md`
11. `docs/RELEASE_READINESS_AND_SAFETY_GATES.md`
12. `docs/DEVELOPER_ROADMAP.md`
13. `docs/SAFETY.md`

Older handoff/status text may describe blockers already solved. `docs/CURRENT_STATUS_2026-08-28.md` is the current high-level truth.

## Current position

Do **not** restart from USB, TLS, image decode, preprocessing, Gaussian analysis, feature detection, or post-extraction pruning.

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
correspondence consolidation 0x5a350                   CURRENT
final scoring details                                  NEXT
enrollment update/template storage                      AFTER MATCHER
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

Exports/wrappers:

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

`0x180056780` writes the four Pass-A sign/projection DWORDs at `+0x10..+0x1f`.

`0x180056670` writes the Pass-B sign/projection DWORD at `+0x28..+0x2b`.

`0x180056520` writes the lower-median-threshold bitsets, using the lower median returned by `0x180056b10`.

## Post-extraction stages already closed enough

### `0x1800371c0` / `0x18003b820`

Post-extraction spatial pruning. `0x3b820` is an exact local zero-count map via summed-area/integral image; `0x371c0` compacts the `0x3c` list in-place. Do not descend further unless newly required.

### `0x180016930`

Computes one local image/map-derived score per retained feature. One caller stores the parallel score array at `object+0x164`.

### `0x180016bd0`

Computes pairwise neighborhood/spatial-support evidence between retained features. Writes a clamped support-like score to `feature+0x39` and feeds final feature-status refinement.

### `0x18000ede0`

Whole-list reorder/finalizer. Reorders `0x3c` records by low status bits while swapping the parallel `object+0x164` entries in lockstep. Writes a partition/count-like value at `output+0x108`.

## Identify matcher ABI — proven

`0x18000d6c0` builds the probe then loops candidate templates.

`0x180028c90` is the per-candidate matcher wrapper/orchestrator.

Effective high-level ABI:

```text
first argument  = match-result / score output pointer
second argument = probe fingerprint object
third argument  = candidate/template container
return EAX      = execution/status
```

Do not confuse `EAX` with the match score. Identify checks `EAX` for execution failure, then checks the value written through the first argument for the match result.

`0x28c90` prepares local context and delegates substantial work to `0x1800293c0`.

## Matcher structure now established

The current matcher path is:

```text
identify 0x18000d6c0
  -> 0x180028c90 matcher wrapper
  -> 0x1800293c0 matcher orchestrator / score fusion
       -> 0x180028de0 early multi-channel matcher
            -> 0x180059580 primary correspondence builder
                 -> 0x18005baf0 pairwise descriptor-cost primitive (twice)
                 -> 0x18005a350 correspondence consolidation   <-- CURRENT
            -> 0x18001f840 correspondence/count reduction
            -> 0x180050c80 spatial/image similarity channel
            -> 0x180058420 alternate/refined correspondence state
            -> 0x18001f840 refined reduction
            -> 0x180051980 / 0x18005a6b0 additional relation metrics
       -> type/policy gates including 0x1800258c0 / 0x18001c920
       -> final score fusion / output
```

`0x1800258c0` and `0x18001c920` are policy/threshold predicates over precomputed metrics, not raw descriptor comparators.

`0x180050c80` is primarily a spatial/image-representation similarity channel; it does not directly iterate the `+0xf8` retained minutia list.

## Major new checkpoint: `0x18005baf0`

### Important unwind correction

Do not truncate `0x5baf0` at its first PE `RUNTIME_FUNCTION` entry.

Full CFG recovery proves one logical function across three unwind entries:

```text
0x18005baf0 .. 0x18005bf35
273 reachable instructions
```

The earlier partial 129-byte decode was incomplete and all conclusions based on that truncation are superseded.

### Direct pairwise feature traversal

The full function contains nested loops over two retained feature lists with explicit `0x3c` stride arithmetic.

This is the actual pairwise binary-descriptor correspondence primitive for the normal type-`0x0c` path.

### Primary descriptor cost

For each feature pair, `0x5baf0` starts at descriptor offset `+0x10` and compares the complete 16-byte Pass-A sign/projection descriptor.

It repeatedly performs:

```text
xor_word = word_A XOR word_B
```

then splits the XOR result into four bytes and sums a fixed DWORD lookup value for each byte from:

```text
0x180079730
```

Call this a **Hamming-like byte-lookup binary-descriptor distance** for now.

Do not claim the lookup table is an exact popcount table until its contents are independently dumped/verified.

### More descriptor terms

The function performs additional XOR + lookup cost terms on selected later binary fields in the `0x3c` feature record. Exact grouping/weighting of all secondary descriptor terms is not yet closed.

### Pairwise outputs

For evaluated feature pairs, `0x5baf0`:

- writes a byte pair-cost into a work matrix/array;
- writes a byte flag selecting between two cost variants;
- maintains the best two pair costs for each outer feature;
- maintains associated opposite-list indices.

Thus it produces both dense-ish pair evidence and nearest-candidate correspondence/ranking state.

### No deeper matcher child

The only direct call inside the complete logical function is the teardown/security-cookie helper `0x18000c170`.

The descriptor XOR/lookup and best-match tracking logic is implemented directly in `0x5baf0`.

## Immediate next task

Reverse **`0x18005a350` only**.

It consumes the two directional result sets produced by the two `0x5baf0` calls in `0x180059580`.

Determine:

1. whether it enforces reciprocal/mutual nearest-neighbor correspondences;
2. how best/second-best costs and corresponding indices are filtered;
3. the exact correspondence record/list it emits;
4. how its result is consumed by `0x18001f840`;
5. where geometry/direction consistency enters relative to the binary descriptor cost.

After `0x5a350`, close only the minimum decisive path needed to reproduce final matcher scoring, then move to enrollment update/template storage.

Do not recursively reverse every reachable helper.

## What not to repeat

Do not restart or re-prove unless a new dependency demands it:

- USB transport;
- CFG70 reconstruction;
- command `0x90` upload;
- factory TLS;
- activation;
- FDT;
- image `0x20` transport/framing/CRC;
- RAW12 decode;
- ChicagoHU regroup;
- gfusb ImageBase packaging;
- preprocessing/correction/normalization;
- mode-9 Gaussian;
- texture/mask/quality/output-mask stages;
- feature detector/emitter roles;
- 36-bin local orientation histogram;
- descriptor lower-median packing;
- `0x371c0` / `0x3b820` pruning;
- `0x16930` / `0x16bd0` feature quality/support roles;
- `0xede0` reorder/finalizer role;
- `0x28c90` wrapper ABI;
- `0x258c0` / `0x1c920` as policy gates;
- `0x50c80` as spatial similarity channel;
- whether `0x5baf0` performs direct pairwise descriptor comparison: it does.

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