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
2. `docs/CHICAGO_FEATURE_EXTRACTION_AND_PRUNING_2026-08-28.md`
3. `docs/CHICAGO_435A0_LOCAL_CONTRAST_NORMALIZATION_2026-08-28.md`
4. `docs/CHICAGO_PREPROCESS_CORE_2026-08-28.md`
5. `docs/CHICAGO_POST_MASK_STAGE_4AEA0_2026-08-28.md`
6. `docs/CHICAGO_MODE9_GAUSSIAN_EXECUTOR_4FFF0_2026-08-28.md`
7. `docs/CHICAGO_MODE9_STREAMING_EXECUTOR_E380_2026-08-28.md`
8. `docs/CHICAGO_MODE9_HORIZONTAL_GAUSSIAN_F5F0_2026-08-28.md`
9. `docs/CHICAGO_MODE9_VERTICAL_GAUSSIAN_FD20_2026-08-28.md`
10. `docs/RELEASE_READINESS_AND_SAFETY_GATES.md`
11. `docs/DEVELOPER_ROADMAP.md`
12. `docs/SAFETY.md`

Older handoff/status text may describe blockers already solved. `docs/CURRENT_STATUS_2026-08-28.md` is the current high-level truth.

## Current position

Do **not** restart from USB, TLS, image decode, preprocessing, or Gaussian analysis.

The active work is now inside the matcher/enrollment side of `AlgoChicago.dll`.

Current reconstructed path:

```text
USB/TLS/image acquisition                       DONE
RAW12 + ChicagoHU regroup                       DONE
Windows-compatible 80x64 image                  DONE
preprocessing/correction/normalization          SUBSTANTIALLY CLOSED
quality/segmentation/output-mask gates          PROVEN
accepted fingerprint-feature extraction         PROVEN at orchestrator level
retained 0x3c feature records                   PROVEN
local orientation + compact descriptors         PROVEN substantially
post-extraction feature pruning                 PROVEN
first matcher-ready representation after prune  CURRENT
matcher/enrollment semantics                    NEXT
libfprint/fprintd                               LATER
release/lifecycle safety matrix                 FINAL
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

`0x180016920` is a thunk to `0x1800139e0`.

Direct callers prove `0x1800139e0` is a shared representation builder used by both identify and enrollment.

Known output-object fields:

```text
output+0xf0 = retained feature count
output+0xf8 = retained feature-array pointer
feature stride = 0x3c bytes
```

## Fingerprint feature extraction — proven checkpoint

`0x180011080` is the feature-extraction orchestrator.

`0x1800153b0` is the first fingerprint-specific candidate scanner/detector stage.

`0x180014560` is the accepted-feature emitter and increments `feature_count`.

Proven retained-record fields:

```text
feature+0x02 = X Q8
feature+0x04 = Y Q8
feature+0x06 = signed Q12-radian direction
```

`feature+0x00` and `feature+0x08` have quality/response-like roles, but exact semantic names remain unclaimed.

`0x180011a60` builds a 36-bin circular local orientation histogram. It uses a spatial neighborhood controlled by candidate `+0x14`, applies opposite-orientation symmetry on the normal type-`0x0c` path, smooths circularly with exact binomial kernel:

```text
[1,4,6,4,1] / 16
```

and feeds peak/sub-bin direction estimation in `0x180014560`.

Important correction:

```text
candidate+0x14 is a local scale/neighborhood parameter,
not the final direction angle.
```

## Compact local descriptor — proven checkpoint

`0x1800157d0` calls `0x180017730` twice:

```text
Pass A = 128 components
Pass B =  32 components
```

`0x180056b10` returns the lower median:

```text
N=128 -> descriptor rank 63
N=32  -> descriptor rank 15
```

`0x180056520` packs one bit per selected component:

```text
bit = 1 iff component > lower_median
```

For type `0x0c`:

```text
Pass A:
  output bits = 64
  source stride = 2
  source indices = 1,3,5,...,127
  feature+0x20..+0x27 = 8-byte median-threshold mask

Pass B:
  output bits = 32
  source stride = 1
  source indices = 0..31
  feature+0x2c..+0x2f = 4-byte median-threshold mask
```

`0x180056780` builds four 32-bit Pass-A sign/projection masks at `feature+0x10..+0x1f`.

`0x180056670` builds a 32-bit Pass-B sign/projection mask at `feature+0x28..+0x2b`. On the normal type-`0x0c` path, `+0x30..+0x37` are cleared and not populated by this writer.

## `0x1800371c0` — post-extraction feature pruning

This is the first proven whole-feature-list consumer after extraction.

It receives both:

```text
feature_array
&feature_count
```

It is **not** the matcher/template builder.

It rounds feature coordinates:

```text
x = (x_q8 + 0x80) >> 8
y = (y_q8 + 0x80) >> 8
```

and indexes a temporary WORD map.

When the map value is greater than `1`, it deletes that feature by moving the last `0x3c` record into the current slot, zeroing the old last slot, decrementing the count, and rechecking the moved record.

It writes the final count back through `feature_count`.

## `0x18003b820` — CLOSED; do not descend further

This function builds the WORD map used by `0x1800371c0`.

Caller-proven ABI:

```text
RCX  = input byte mask/buffer
RDX  = destination WORD map
R8D  = width
R9D  = height
arg5 = local radius/policy
arg6 = &total_zero_count
```

It allocates a temporary `(width+1)x(height+1)` WORD summed-area table over:

```text
z(y,x) = 1 if input_byte(y,x) == 0
         0 otherwise
```

and returns:

```text
total_zero_count = count(input_byte == 0)
```

Then for every pixel it computes by four integral-image corner reads:

```text
local_zero_count[y,x]
```

inside the clipped square neighborhood:

```text
radius = arg5
nominal window = (2*radius+1) x (2*radius+1)
```

The second direct caller independently corroborates this by comparing map values with approximately half the nominal square area.

`0x1800371c0` prunes only if:

```text
0x18003b820 succeeded
and total_zero_count >= 50
```

and removes a retained feature when:

```text
local_zero_count[round(y),round(x)] > 1
```

Do not rename zero-valued pixels as background/invalid until the polarity/producer of this exact input buffer is independently proven.

## Immediate next task

Continue **inside `0x1800139e0` immediately after the call to `0x1800371c0`**.

Goal: identify the first persistent matcher-ready probe/template-side representation built from the pruned `0x3c` feature list.

Then connect that representation to:

```text
enrollment core   0x18000cd70
identify core     0x18000d6c0
matcher           0x180028c90
```

`0x180028c90` is already strongly proven as a per-candidate compare/score orchestrator, but exact scoring/decision semantics are not yet closed.

Reverse only decisive children required for compatibility. Do not recursively reverse every reachable helper.

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
- outer preprocessing stages;
- Q13 correction stages;
- mode-9 Gaussian;
- local normalization/quality/output-mask stages;
- `0x180011080` / `0x1800153b0` / `0x180014560` roles;
- `0x180011a60` histogram role;
- `0x180056b10` lower median;
- `0x180056520` bitset packing;
- `0x1800371c0` list compaction;
- `0x18003b820` local zero-count integral-image map.

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
