# Current status — 2026-08-28

Target: Goodix USB fingerprint sensor `27c6:5135`, firmware `GF_HC460SEC_APP_12508`, logical chip `0x2504`, ChicagoHS / ChicagoHU, sensor family/type `0x0c`.

This is the canonical high-level checkpoint for the Linux enablement effort. Static Windows reverse engineering is used only to reproduce compatible behavior. No proprietary Goodix binaries, device secrets, biometric samples/templates, unit-specific runtime configuration, or Windows biometric database material are committed.

## Plain-language position

The device/transport side is solved on the tested unit: USB transport, volatile runtime configuration, factory-compatible TLS, activation, finger detection, image request/decrypt/framing/CRC, RAW12 decode, ChicagoHU regroup, and Windows-compatible 80x64 downstream layout are all established.

The preprocessing pipeline is also substantially closed. We have traced the correction/normalization path through the persistent corrected U16 plane, local contrast normalization to U8, directional/texture mask generation, quality/segmentation policy gates, and final U8 output-mask postprocessing. The outer preprocessor copy-out boundary is known.

The active work is now Phase 8: fingerprint feature extraction, post-extraction filtering, and the representation consumed by enrollment/identify/matching.

The current exact checkpoint is immediately after the post-extraction spatial-pruning stage `0x1800371c0` / `0x18003b820`. The next task is to continue through `0x1800139e0` and identify the first matcher-ready probe/template-side representation.

## Milestones

```text
Device identity / 27c6:5135 profile                     PASS
Factory compatibility constraints                       PASS
USB transport                                           PASS
CFG70 reconstruction                                    PASS
Volatile command 0x90 runtime config                    PASS
Factory TLS 1.2 PSK session                             PASS
Activation sequence                                     PASS
FDT manual/down/up                                       PASS on tested unit
Image command 0x20                                      PASS
TLS image transport/decrypt                             PASS
Goodix framing / CRC                                    PASS on one private capture
RAW12 -> 5120 samples                                   PASS
ChicagoHU regroup                                       PROVEN
ImageBase/live same-index layout                        PROVEN
Candidate A                                             PROVEN
Candidate B double-regroup                              REJECTED
gfusb post-detection packaging                          PROVEN
WBDI / EngineAdapter layer                              PROVEN
family 0x0c -> AlgoChicago.dll                          PROVEN
outer preprocessor 0x18000e780                          PROVEN
core preprocessing orchestrator 0x1800484e0             PROVEN
state-source signed subtraction 0x180044970             PROVEN
mask cleanup/source validity 0x180043c40                PROVEN
Q13 correction parent 0x18004aea0                      PROVEN
persistent corrected U16 plane state+0x13244            PROVEN
mode-9 Gaussian path                                    PROVEN
mode-9 border policy / numerics                         PROVEN
U16 -> normalized U8 0x1800435a0                       PROVEN
persistent normalized byte plane state+0x1cb64          PROVEN
post-normalization texture/mask stage 0x180050550       PROVEN
quality/segmentation gate 0x180046ff0                   PROVEN
quality/preprocess policy orchestrator 0x180046930      PROVEN
mask consistency evaluator 0x180045bf0                  PROVEN
final U8 output-mask postprocessor 0x1800547c0          PROVEN
outer preprocessor copy-out boundary                    PROVEN
identify wrapper -> 0x18000d6c0                         PROVEN
 enrollment wrapper -> 0x18000cd70                       PROVEN target
shared identify/enrollment builder 0x1800139e0          PROVEN role
feature extraction orchestrator 0x180011080             PROVEN
feature candidate scanner 0x1800153b0                   PROVEN role
accepted-feature emitter 0x180014560                    PROVEN
36-bin local orientation histogram 0x180011a60          PROVEN role
retained feature stride 0x3c                            PROVEN
local descriptor median threshold 0x180056b10           PROVEN
binary descriptor packer 0x180056520                    PROVEN
Pass-A / Pass-B descriptor dimensions                   PROVEN
post-extraction spatial pruning 0x1800371c0             PROVEN
local zero-count map builder 0x18003b820                PROVEN
first matcher-ready representation after pruning        CURRENT
per-candidate matcher 0x180028c90                       PROVEN role only
exact matcher scoring/decision                          NOT YET CLOSED
enrollment template update/storage representation       NOT YET CLOSED
Linux feature extraction/matcher implementation         NOT YET IMPLEMENTED
libfprint/fprintd integration                           NOT YET IMPLEMENTED
lifecycle/stress safety matrix                          NOT YET COMPLETE
```

## Critical preprocessing arithmetic

For selector/type `0x0c`, `0x180044970` computes:

```text
diff16[i] = state_plus_0x9924_u16[i] - source_u16[i]
```

with wrapping 16-bit subtraction later interpreted as signed `s16`.

Selector `4` has the separate relation:

```text
state_plane - source + 0x0fff
```

Do not regress to `source-state`.

Also, `state+0x9924` remains an AlgoChicago internal preprocessing/calibration-state plane. It is not yet independently proven identical to gfusb persisted ImageBase.

## Preprocessor output path now closed far past the old checkpoint

The mode-9 transform is a separable 5x5 Gaussian using the five Q16 coefficients:

```text
[7869, 15328, 19142, 15328, 7869]
```

with the border behavior and fixed-point application already closed.

The later preprocessing chain is also mapped:

```text
state+0x13244 corrected U16
  -> 0x1800435a0 local contrast normalization
  -> state+0x1cb64 normalized U8
  -> 0x180050550 directional edge/texture mask
  -> 0x180046ff0 quality/segmentation acceptance
  -> 0x180046930 multi-policy quality/preprocessing
  -> 0x180045bf0 mask consistency/coverage evaluation
  -> 0x1800547c0 final U8 validity/output-mask postprocessing
  -> outer wrapper copy-out
```

Preprocessing is therefore no longer the immediate blocker.

## Feature extraction checkpoint

`0x180016920` thunks to shared builder `0x1800139e0`, which is called by both enrollment and identify paths.

Its output object contains:

```text
output+0xf0 = retained feature count
output+0xf8 = retained feature array
```

with each feature exactly `0x3c` bytes.

### Accepted feature fields

`0x180014560` is proven to append accepted features and increment the count.

Known fields:

```text
+0x02 = X Q8
+0x04 = Y Q8
+0x06 = signed Q12-radian direction
```

The final direction comes from a 36-bin circular local orientation histogram built by `0x180011a60`, followed by sub-bin interpolation in `0x180014560`.

### Compact local descriptors

`0x180017730` enriches each retained feature using two passes.

`0x180056b10` returns the lower median of the descriptor components:

```text
N=128 -> rank 63
N=32  -> rank 15
```

`0x180056520` produces bitsets using the rule:

```text
bit = 1 iff selected_component > lower_median
```

For type `0x0c`:

```text
Pass A: 64 bits, source stride 2, 8 bytes at feature+0x20..+0x27
Pass B: 32 bits, source stride 1, 4 bytes at feature+0x2c..+0x2f
```

The associated sign/projection masks are produced by `0x180056780` and `0x180056670`.

See `docs/CHICAGO_FEATURE_EXTRACTION_AND_PRUNING_2026-08-28.md` for the current detailed record map.

## `0x1800371c0` — proven post-extraction pruning

This function receives both the complete retained feature array and `&feature_count`.

It does not build the matcher template. Instead it filters the list in-place.

For each feature:

```text
x = (x_q8 + 0x80) >> 8
y = (y_q8 + 0x80) >> 8
```

It then checks a temporary WORD map at `map[y*width+x]`.

When the map value is greater than `1`, the feature is removed by swapping the final retained `0x3c` record into the current slot, zeroing the old last record, decrementing the count, and rechecking the moved record. The final feature count is written back.

Therefore `0x1800371c0` is a real post-extraction feature-list pruning/compaction stage.

## `0x18003b820` — exact local zero-count map

This helper is now algorithmically closed.

It receives an input byte mask/buffer, output WORD map, width, height, local radius, and total-zero-count output pointer.

It builds a `(width+1)x(height+1)` summed-area/integral image of:

```text
z(y,x) = 1 when input_byte(y,x) == 0
         0 otherwise
```

and simultaneously returns:

```text
total_zero_count = number of zero-valued input pixels
```

It then uses the four-corner summed-area formula to write, for each image location:

```text
local_zero_count[y,x]
```

inside a clipped square neighborhood whose nominal size is:

```text
(2*radius+1) x (2*radius+1)
```

`0x1800371c0` only applies the pruning path when the global zero count is at least 50, and removes a feature when its local zero count is greater than 1.

Do not yet assign semantic polarity such as "background" or "invalid" to zero values until the exact producer of this particular byte mask is independently closed.

## Immediate task

Return to `0x1800139e0` immediately after its `0x1800371c0` call.

Do not spend more time on `0x18003b820`; it is sufficiently closed.

Trace the next representation-building operations until the first object that is demonstrably consumed by:

```text
enrollment core 0x18000cd70
identify core   0x18000d6c0
matcher         0x180028c90
```

Prioritize the minimum decisive path needed to reproduce:

1. probe/template representation;
2. enrollment accumulation/update;
3. per-candidate comparison/scoring;
4. final identify/verify decision.

Do not reverse every helper merely because it is reachable.

## Completion / safety gates

Release is not complete until native Linux integration satisfies all defined safety gates with no known unsafe behavior:

- `fprintd-enroll` works;
- `fprintd-verify` works;
- reboot/cold boot works;
- suspend/resume works;
- cancellation/timeouts recover cleanly;
- no secret or biometric payload logging;
- Windows Hello and the existing Windows fingerprints still work afterward;
- no firmware erase/flash or factory PSK rewrite/reprovision.

Never publish or request plaintext PSK/hashes, full OTP, unit-specific full runtime config/hash, fingerprint images/raw/templates, `goodix.dat`, `goodix_calib.dat`, `Goodix_Cache.bin`, proprietary Goodix binaries, Windows biometric DB material, or process/memory dumps.
