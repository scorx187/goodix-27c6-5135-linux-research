# Current status — 2026-08-28

Target: Goodix USB fingerprint sensor `27c6:5135`, firmware `GF_HC460SEC_APP_12508`, logical chip `0x2504`, ChicagoHS / ChicagoHU, sensor family/type `0x0c`.

This is the canonical high-level checkpoint for the Linux enablement effort. Static Windows reverse engineering is used only to reproduce compatible behavior. No proprietary Goodix binaries, device secrets, biometric samples/templates, unit-specific runtime configuration, or Windows biometric database material are committed.

## Plain-language position

The device/transport side is solved on the tested unit: USB transport, volatile runtime configuration, factory-compatible TLS, activation, finger detection, image request/decrypt/framing/CRC, RAW12 decode, ChicagoHU regroup, and Windows-compatible 80x64 downstream layout are established.

The preprocessing pipeline is also substantially closed through correction, normalization, texture/mask generation, quality/segmentation policy, final output-mask processing, and outer copy-out.

Feature extraction is substantially mapped: retained minutiae are `0x3c` bytes each, their coordinates/direction are known, the local binary descriptors are structurally mapped, post-extraction spatial pruning is proven, and later per-feature quality/support refinement is substantially understood.

The active work is now inside the matcher. A major new checkpoint is closed: `0x18005baf0` directly performs pairwise binary-descriptor comparison across the retained `0x3c` feature lists. It traverses both lists with nested loops, XORs descriptor DWORDs, converts XOR bytes through a fixed lookup table, produces pair costs/variant flags, and tracks best/second-best candidate indices per feature. This logic is implemented directly in `0x5baf0`; it is not delegated to another matcher child.

The immediate next target is `0x18005a350`, which consumes the two directional `0x5baf0` result sets and is expected to consolidate them into the correspondence state reduced later by `0x18001f840`.

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
correspondence consolidation 0x18005a350                CURRENT
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

The final retained feature stride is exactly `0x3c` bytes.

Current structure:

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

`0x180056780` produces the four Pass-A sign/projection DWORDs at `+0x10..+0x1f`; `0x180056670` produces the Pass-B sign/projection DWORD at `+0x28..+0x2b`.

`0x180056520` produces the median-threshold bitsets, using the lower median returned by `0x180056b10`.

## Post-extraction list and quality finalization

### `0x1800371c0` / `0x18003b820`

`0x371c0` filters the retained list in-place using a local zero-count WORD map produced exactly by `0x3b820` via a summed-area/integral image. This stage is sufficiently closed; do not descend further unless a new dependency requires it.

### `0x180016930`

Computes one local image/map-derived score per retained feature. One caller writes these scores directly to the parallel `object+0x164` array.

### `0x180016bd0`

Performs pairwise local-neighborhood analysis between retained features and writes a clamped support-like score to `feature+0x39`. It also contributes to the final status refinement using local and neighbor-smoothed scores.

### `0x18000ede0`

Whole-feature-list reorder/finalizer. It partitions/reorders `0x3c` records based on low status bits and swaps the parallel `object+0x164` entries in lockstep. It writes a partition/count-like value at `output+0x108`.

## Identify / matcher ABI checkpoint

`identifyImageWrapper` enters `0x18000d6c0`.

The identify path builds the probe through `0x180016920` and then loops over candidate templates.

`0x180028c90` is the per-candidate matcher wrapper. Proven high-level ABI:

```text
first argument  = match-result/score output pointer
second argument = probe fingerprint object
third argument  = candidate/template container
return EAX      = execution/status
```

The actual match decision/result is written through the first argument, while nonzero `EAX` indicates execution failure/status.

`0x28c90` prepares matcher context and delegates the substantial work to `0x1800293c0`.

## Matcher evidence channels

The matcher combines multiple evidence channels rather than using one descriptor distance alone.

Current reconstruction:

```text
binary minutia correspondence evidence
  -> 0x59580 / 0x5baf0 / 0x5a350

spatial/image representation similarity
  -> 0x50c80 and helpers

additional relation/geometry metrics
  -> 0x51980 / 0x5a6b0 and surrounding orchestration

quality / coverage / policy gates
  -> 0x258c0 / 0x1c920 and other thresholds

final score fusion
  -> 0x293c0
```

`0x258c0` and `0x1c920` are threshold/policy predicates over already-computed metrics; they are not raw minutia descriptor comparators.

`0x50c80` is primarily a spatial/image-representation similarity channel and does not directly traverse `output+0xf8` as `0x3c` minutiae.

## `0x18005baf0` — pairwise binary descriptor correspondence primitive

This is the newest major closed checkpoint.

A previous run incorrectly stopped at the first PE `RUNTIME_FUNCTION` boundary. Full CFG recovery proves the logical function crosses three unwind entries and spans:

```text
0x18005baf0 .. 0x18005bf35
273 reachable instructions
```

The function contains nested loops with explicit `0x3c` stride arithmetic over both retained-feature lists.

For each feature pair, the primary descriptor comparison starts at `feature+0x10` and covers the 16-byte Pass-A sign/projection descriptor. It XORs corresponding DWORDs, extracts the four bytes of each XOR result, and accumulates values from the fixed lookup table at:

```text
0x180079730
```

This is operationally a Hamming-like byte-lookup binary-descriptor distance. Do not yet label the table itself as an exact popcount table until its entries are independently verified.

The function also performs additional XOR + lookup cost terms using selected later binary fields in the `0x3c` record.

It writes pairwise byte costs/variant flags and maintains the best two observed pair costs plus associated opposite-list indices for each outer feature.

There is no substantive child below this comparison: the only direct call is the teardown/security-cookie helper `0x18000c170`.

Therefore the actual pairwise binary-descriptor comparison primitive has now been reached.

Detailed checkpoint: `docs/CHICAGO_MATCHER_CORRESPONDENCE_5BAF0_2026-08-28.md`.

## Immediate task

Reverse `0x18005a350` next.

It is called immediately after the two directional `0x18005baf0` passes from `0x180059580`.

Determine:

1. whether it enforces reciprocal/mutual nearest-neighbor correspondences;
2. how best/second-best costs and indices are filtered;
3. the exact correspondence list/record it emits;
4. how that state is reduced by `0x18001f840`;
5. where geometry/direction consistency joins the descriptor-distance evidence.

After that, continue only the minimum decisive path needed to close final matcher scoring and then enrollment update/template storage.

Do not return to USB/TLS/preprocessing/Gaussian or previously closed feature-pruning helpers unless a newly proven dependency requires it.

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