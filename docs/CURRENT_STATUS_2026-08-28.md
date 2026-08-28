# Current status — 2026-08-28

Target: Goodix USB fingerprint sensor `27c6:5135`, firmware `GF_HC460SEC_APP_12508`, logical chip `0x2504`, ChicagoHS / ChicagoHU, sensor family/type `0x0c`.

This is the canonical high-level checkpoint for the Linux enablement effort. Static Windows reverse engineering is used only to reproduce compatible behavior. No proprietary Goodix binaries, device secrets, biometric samples/templates, unit-specific runtime configuration, or Windows biometric database material are committed.

## Plain-language position

The tested device/transport path is solved: USB transport, volatile runtime configuration, factory-compatible TLS, activation, finger detection, image request/decrypt/framing/CRC, RAW12 decode, ChicagoHU regroup, and Windows-compatible `80x64` downstream layout are established.

The preprocessing pipeline is substantially closed through correction, normalization, texture/mask generation, quality/segmentation policy, final output-mask processing, and outer copy-out.

Feature extraction is substantially mapped: retained minutiae are `0x3c` bytes each, coordinates/direction are known, local binary descriptors are structurally mapped, post-extraction spatial pruning is proven, and later per-feature quality/support refinement is substantially understood.

The active work is now inside the matcher. The direct pairwise binary-descriptor comparator `0x18005baf0` is proven. The next two layers are also now structurally closed:

- `0x18005a350` filters descriptor candidates using a best-vs-second-best ambiguity test, suppresses spatially duplicate correspondences using X/Y, keeps a bounded best-K set, and emits flat signed `int32` index pairs padded with `-1`.
- `0x18001f840` expands those pair indices back into X/Y/direction geometry, applies geometric/directional filtering, and returns the **count of surviving correspondence inliers**.

The immediate next target is the exact inlier policy delegated by `0x1f840` to `0x1800586c0` and the additional per-pair validity filter `0x180024880`.

Detailed matcher checkpoint: `docs/CHICAGO_MATCHER_CORRESPONDENCE_REDUCTION_2026-08-28.md`.

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
Pass-A sign descriptor +0x10..+0x1f                    PROVEN structurally
Pass-A median descriptor +0x20..+0x27                  PROVEN
Pass-B sign descriptor +0x28..+0x2b                    PROVEN structurally
Pass-B median descriptor +0x2c..+0x2f                  PROVEN
post-extraction spatial pruning 0x1800371c0             PROVEN
local zero-count map builder 0x18003b820                PROVEN
per-feature local score 0x180016930                     PROVEN role
per-feature neighborhood support 0x180016bd0            PROVEN role
feature reorder/finalizer 0x18000ede0                   PROVEN role
identify per-candidate wrapper 0x180028c90              PROVEN role
matcher orchestrator / score fusion 0x1800293c0         PROVEN role
early multi-channel matcher 0x180028de0                 PROVEN role
spatial similarity channel 0x180050c80                  PROVEN role
primary correspondence builder 0x180059580              PROVEN role
pairwise descriptor-cost primitive 0x18005baf0          PROVEN
correspondence consolidation 0x18005a350                PROVEN structurally
correspondence inlier reducer 0x18001f840               PROVEN structurally
inlier geometry helper 0x1800586c0                      CURRENT
additional pair filter 0x180024880                      CURRENT
exact final matcher scoring/decision                    NOT YET CLOSED
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

Never regress to `source-state`.

`state+0x9924` remains an AlgoChicago internal preprocessing/calibration-state plane. It is not yet independently proven identical to gfusb persisted ImageBase.

## Retained feature record — current proven structure

```text
+0x00        local flag / quality-like field; exact name unclaimed
+0x02        X Q8
+0x04        Y Q8
+0x06        signed Q12-radian direction
+0x08        response/strength-like field; exact name unclaimed
+0x0c        Y-border classification after extraction orchestration
+0x10..+0x1f 128-bit Pass-A sign/projection descriptor
+0x20..+0x27 64-bit Pass-A lower-median-threshold descriptor
+0x28..+0x2b 32-bit Pass-B sign/projection descriptor
+0x2c..+0x2f 32-bit Pass-B lower-median-threshold descriptor
+0x30..+0x37 cleared/reserved on normal type-0x0c extraction path before later consumers
+0x38        status/validity-like byte
+0x39        neighborhood/spatial-support score
```

## Matcher path — current reconstruction

```text
identify 0x18000d6c0
  -> 0x180028c90 per-candidate matcher wrapper
  -> 0x1800293c0 score/policy orchestrator
       -> 0x180028de0 early matcher
            -> 0x180059580 primary correspondence builder
                 -> 0x18005baf0 descriptor comparator (twice)
                 -> 0x18005a350 ambiguity/spatial/top-K consolidation
            -> 0x18001f840 geometric/directional survivor reduction
                 -> 0x1800586c0 inlier-mask helper          [CURRENT]
                 -> 0x180024880 additional pair filter      [CURRENT]
                 -> optional 0x180058f90 refinement
            -> 0x180050c80 spatial/image similarity channel
            -> 0x180058420 alternate/refined correspondence state
            -> 0x18001f840 refined survivor reduction
            -> 0x180051980 / 0x18005a6b0 additional relation metrics
       -> policy gates including 0x1800258c0 / 0x18001c920
       -> final score fusion / result write
```

`0x1800258c0` and `0x18001c920` are threshold/policy predicates over already-computed metrics; they are not raw minutia descriptor comparators.

## `0x18005baf0` — descriptor correspondence primitive

Full CFG recovery proves one logical function across three unwind entries:

```text
0x18005baf0 .. 0x18005bf35
273 reachable instructions
```

It directly traverses both `0x3c` feature lists. The primary descriptor comparison covers `feature+0x10..+0x1f`, XORs corresponding DWORDs, splits the XOR results into bytes, and sums a fixed lookup value from `0x180079730`. Treat this as a Hamming-like byte-lookup binary-descriptor distance until the lookup table is independently verified.

It also consumes selected later binary fields, emits pair costs/variant flags, and maintains best/second-best candidate indices. No deeper substantive matcher child performs this comparison.

## `0x18005a350` — ambiguity, spatial duplicate suppression, top-K selection

The recovered function is not a reciprocal-nearest-neighbor test.

For each candidate pair it applies the integer ambiguity relation:

```text
best_cost * cfg_A < second_cost * cfg_B
```

Then it loads X/Y from the referenced `0x3c` feature and suppresses spatial conflicts when:

```text
(dx_q8 * dx_q8) + (dy_q8 * dy_q8) < 0x10000
```

When candidates conflict, it retains the lower-cost one.

The selected correspondence set is bounded. If full, the function finds the currently worst retained cost and replaces it only when the new pair is better.

Final output is a flat signed 32-bit pair array:

```text
[index_A0,index_B0,index_A1,index_B1,...]
```

Unused slots are set to `-1`.

`0x5a350` does not consume minutia direction `+0x06`; direction enters in `0x1f840`.

## `0x18001f840` — surviving inlier count

The first 0x39-byte PE runtime entry is only a split-unwind fragment. Full CFG recovery proves:

```text
0x18001f840 .. 0x18001fbb0
197 reachable instructions
3 runtime/unwind entries
```

The function resolves candidate and probe feature arrays at `+0xf8`, reads each nonnegative correspondence pair, and copies for both referenced minutiae:

```text
+0x02 X Q8
+0x04 Y Q8
+0x06 signed Q12-radian direction
```

It counts valid input pairs. If that count is not greater than the caller-supplied minimum threshold, it returns zero.

It then calls `0x1800586c0`, which structurally produces an inlier-like byte mask and an auxiliary fixed-point/geometry metric. Pairs whose mask byte is zero are invalidated.

Next it calls `0x180024880`, which contains its own `0x3c` traversal and reads direction `+0x06` from both sides. Pairs invalidated by this helper clear the corresponding mask byte.

Finally `0x1f840` counts nonzero mask entries and returns exactly:

```text
EAX = surviving_correspondence_count
```

On the early matcher path this value is written to `match_record+0x00` and `match_record+0x04`.

If the survivor count is at least 4 and the metric returned through the `0x586c0` work state exceeds `0x4000`, `0x1f840` calls `0x180058f90`. It does not recount before returning, so `0x58f90` is not required to define the returned integer; it likely refines side-state/context used later.

## Immediate task

Reverse only:

```text
0x1800586c0
0x180024880
```

Determine:

1. the exact geometric transform/consistency model used by `0x586c0`;
2. how its byte inlier mask is produced;
3. the meaning/scaling of the metric checked against `0x4000`;
4. the exact direction/feature-consistency rule by which `0x24880` invalidates pairs.

Do not descend into `0x58f90` unless a later consumer proves that side effect is needed for final score reproduction.

After these are closed, continue only the minimum decisive path needed for final matcher score/policy fusion, then enrollment update/template storage.

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