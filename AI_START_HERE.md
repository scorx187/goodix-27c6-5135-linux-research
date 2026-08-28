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

1. `docs/CURRENT_STATUS_2026-08-28.md` — canonical current truth.
2. `docs/CHICAGO_MATCHER_TYPE0C_DIRECT_SCORE_2026-08-28.md`.
3. `docs/CHICAGO_MATCHER_FALLBACK_SCORE_CLOSURE_2026-08-28.md`.
4. `docs/CHICAGO_MATCHER_GEOMETRIC_CONSENSUS_586C0_2026-08-28.md`.
5. `docs/CHICAGO_ENROLLMENT_MULTI_CAPTURE_GRAPH_2026-08-28.md`.
6. `docs/CHICAGO_ENROLLMENT_DERIVED_RELATION_5D620_2026-08-28.md`.
7. `docs/CHICAGO_ENROLLMENT_POST_FINALIZER_31D90_2026-08-28.md`.
8. `docs/CHICAGO_FEATURE_EXTRACTION_AND_PRUNING_2026-08-28.md`.
9. `docs/RELEASE_READINESS_AND_SAFETY_GATES.md`.
10. `docs/SAFETY.md`.

Older documents are evidence/checkpoints, not the active task. If they say matcher or enrollment reverse engineering is still the immediate blocker, prefer `CURRENT_STATUS_2026-08-28.md`.

## Current position — do not regress

Do **not** restart USB, TLS, image decode, RAW12, ChicagoHU regroup, preprocessing, Gaussian analysis, feature extraction, descriptor correspondence, matcher score fusion, or enrollment graph analysis unless a concrete implementation test proves a missing exact dependency.

The minimum decisive static architecture is closed enough to start implementation:

```text
USB/TLS/image acquisition                     ESTABLISHED
80x64 downstream image                        PROVEN
preprocessing/normalization                    SUBSTANTIALLY CLOSED
quality/segmentation/output mask               PROVEN
0x3c feature representation                    PROVEN substantially
descriptor correspondence                      PROVEN
geometric/directional inlier path              PROVEN substantially
type-0x0c direct score                         CLOSED
type-0x0c fallback positive score              CLOSED sufficiently
enrollment multi-capture representation        PROVEN
enrollment relation graph / graph closure      PROVEN
type-0x0c derived post-finalizer state         PROVEN role
portable Linux reference implementation        CURRENT
libfprint/fprintd integration                   AFTER REFERENCE CODE
lifecycle/safety matrix                        FINAL
```

## Immediate task

**Establish the local Linux/libfprint build baseline first.** Determine installed distro/kernel/libfprint/fprintd versions, build tools, whether a libfprint checkout already exists, and confirm `27c6:5135` is visible.

Then create a clean portable reference implementation and unit tests from the reconstructed behavior. Do not put proprietary Windows binaries or private biometric/device material into the repository.

Only return to a specific DLL helper if a failing reference/integration test proves that its exact behavior is required.

## Critical arithmetic — never regress

For selector/type `0x0c`, preprocessing subtraction is:

```text
diff16[i] = state_plus_0x9924_u16[i] - source_u16[i]
```

It is state minus source, wrapping as U16 and later sign-extended.

Selector `4`:

```text
state_plane - source + 0x0fff
```

Never change this back to source-minus-state.

Do not claim AlgoChicago `state+0x9924` is identical to gfusb persisted ImageBase unless independently proven.

## Retained feature record

```text
+0x00        local flag / quality-like field; exact label unclaimed
+0x02        X Q8
+0x04        Y Q8
+0x06        signed Q12-radian direction
+0x08        response/strength-like field
+0x0c        Y-border classification
+0x10..+0x1f 128-bit Pass-A sign/projection descriptor
+0x20..+0x27 64-bit lower-median threshold descriptor
+0x28..+0x2b 32-bit Pass-B sign/projection descriptor
+0x2c..+0x2f 32-bit lower-median threshold descriptor
+0x30..+0x37 reserved/cleared on normal type-0x0c path
+0x38        status/validity-like byte
+0x39        neighborhood/spatial-support score
```

## Matcher facts needed for implementation

Correspondence path:

```text
0x5baf0 descriptor costs
 -> 0x5a350 ambiguity + spatial duplicate suppression + top-K
 -> flat int32 index pairs
 -> 0x1f840 geometry/direction reduction
 -> surviving inlier count
```

`0x586c0` is a deterministic three-correspondence geometric-consensus estimator. It fits a six-DWORD Q8 transform, constrains it toward a similarity/scaled-rotation-like model, uses a 2.5-pixel reprojection radius for inliers, and chooses maximum inliers with lower MSE as tie-breaker.

### Type-0x0c direct score

```text
q8_i = ((match_record[+0x04] << 8) + 21) / 42
accumulator += q8_i
score = ((accumulator * 100) / accepted_count) >> 8
```

`0x24c70` is only a ranking/index reorder side effect, not a score multiplier.

### Type-0x0c fallback positive score

Fallback candidate selection uses geometry/spatial/binary-overlap gates, but accepted positive score remains normalized from the selected inlier/count field:

```text
q8 = ((field_04 << 8) + 21) / 42
score = (q8 * 100) >> 8
```

One-bit Jaccard/IoU is an acceptance gate, not a score multiplier.

## Enrollment facts needed for implementation

Enrollment does **not** fuse all captures into one minutia list.

Persistent model:

```text
multiple capture fingerprint objects
+ pairwise 0x1c geometric-relation graph
+ per-capture derived support state
```

Relation record:

```text
+0x00        direct relation strength/inlier count,
             -1 unusable,
             2 graph-closure-derived relation
+0x04..+0x1b 24-byte transform
```

`0x5d620` stores one canonical record per unordered pair and reverses direction by transform inversion.

After graph closure, type `0x0c` runs `0x31d90`, a reusable derived-state/cache builder. It ensures a bit-packed support mask at capture `+0x130` and computes support/coverage metadata at `+0x118` and `+0x138`. It does not rerun minutiae matching or mutate the relation graph.

## What not to repeat

Do not restart/re-prove unless a test demands it:

- USB transport / CFG70 / volatile command `0x90`;
- factory-compatible TLS / activation / FDT;
- image `0x20`, framing, CRC, RAW12, ChicagoHU regroup;
- gfusb packaging/ImageBase layout work;
- preprocessing / mode-9 Gaussian / mask-quality stages;
- detector/orientation/descriptor packing;
- post-extraction pruning/support;
- matcher wrapper ABI;
- descriptor correspondence and geometric consensus;
- direct/fallback type-0x0c positive score architecture;
- enrollment multi-capture graph and derived relations;
- `0x31d90` post-finalizer role.

## Release readiness definition

Do not call the project complete until **all defined safety gates passed with no known unsafe behavior**:

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

- plaintext factory PSK or PSK files/hashes;
- full OTP;
- fingerprint images/raw/templates;
- `goodix.dat`, `goodix_calib.dat`, `Goodix_Cache.bin`;
- proprietary Goodix DLL/EXE/CAT files;
- Windows biometric DB material;
- full process/memory dumps;
- full unit-specific 224-byte runtime config or its hash.

Never firmware erase/flash, rewrite/reprovision the PSK, run destructive 5117 tooling, perform arbitrary persistent register writes, or remove/re-enroll Windows fingerprints as a shortcut.

The Windows partition must remain read-only during analysis/testing.
