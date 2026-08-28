# Current status — 2026-08-28

Target: Goodix USB fingerprint sensor `27c6:5135`, firmware `GF_HC460SEC_APP_12508`, logical chip `0x2504`, ChicagoHS / ChicagoHU, sensor family/type `0x0c`.

This is the canonical high-level checkpoint for the Linux enablement effort. Static Windows reverse engineering is used only to reproduce compatible behavior. No proprietary Goodix binaries, device secrets, biometric samples/templates, unit-specific runtime configuration, or Windows biometric database material are committed.

## Plain-language position

The tested device/transport path is established: USB transport, volatile runtime configuration, factory-compatible TLS, activation, finger detection, image request/decrypt/framing/CRC, RAW12 decode, ChicagoHU regroup, and Windows-compatible `80x64` downstream layout.

The preprocessing pipeline is substantially closed through correction, normalization, Gaussian/local processing, texture/mask generation, quality/segmentation policy, final output-mask processing, and outer copy-out.

The retained feature path is substantially mapped: `0x3c`-byte records, Q8 X/Y, signed Q12 direction, binary descriptor fields, post-extraction pruning, and later per-feature support/quality state.

The minimum decisive type-`0x0c` matcher architecture is now closed enough to implement: descriptor correspondence, ambiguity/spatial consolidation, geometric consensus, direction filtering, direct score normalization, and fallback/general positive score normalization are understood. Positive type-`0x0c` scores are fundamentally normalized surviving-correspondence/inlier counts; spatial/overlap metrics act as candidate-selection/acceptance gates rather than continuous score multipliers.

Enrollment architecture is also substantially closed. Enrollment retains multiple capture fingerprint objects plus a pairwise geometric-relation graph; it does not collapse all accepted captures into one fused minutia list. Completion performs graph closure and builds reusable per-capture derived support/coverage state.

**Active work now moves to a clean Linux implementation baseline and portable reference implementation.** Do not restart Windows reverse engineering unless implementation tests prove a specific missing bit-exact dependency.

## Decisive current milestones

```text
Device identity / profile                               PASS
Factory compatibility constraints                       PASS
USB transport / CFG70 / volatile 0x90                  PASS
Factory-compatible TLS / activation / FDT              PASS on tested unit
Image command 0x20 / decrypt / framing / CRC           PASS
RAW12 -> 5120 / ChicagoHU regroup                      PROVEN
Windows-compatible 80x64 downstream plane              PROVEN
Preprocessing/correction/normalization                  SUBSTANTIALLY CLOSED
Quality/segmentation/output-mask stages                 PROVEN
Retained 0x3c feature representation                   PROVEN substantially
Descriptor extraction                                  PROVEN substantially
Post-extraction pruning/support                         PROVEN substantially
Pairwise descriptor comparator 0x5baf0                 PROVEN
Correspondence consolidation 0x5a350                   PROVEN substantially
Geometric survivor reducer 0x1f840                     PROVEN
Geometric consensus 0x586c0                            PROVEN substantially
3-point transform solver / transform gate              PROVEN sufficiently for matcher path
Direction-consistency filtering                        PROVEN sufficiently for matcher path
Type-0x0c direct score                                 CLOSED
Type-0x0c fallback/general score                       CLOSED sufficiently
Enrollment entry/orchestrator                          PROVEN
Persistent multi-capture representation                PROVEN
Direct pairwise relation graph                         PROVEN
Graph closure / derived relation marker=2              PROVEN
Type-0x0c post-finalizer 0x31d90                       PROVEN role
Linux portable reference implementation                NOT YET IMPLEMENTED
libfprint/fprintd integration                           NOT YET IMPLEMENTED
Lifecycle/stress safety matrix                         NOT YET COMPLETE
```

## Critical preprocessing arithmetic — do not regress

For selector/type `0x0c`:

```text
diff16[i] = state_plus_0x9924_u16[i] - source_u16[i]
```

This is **state minus source**, wrapping as 16-bit and later interpreted as signed.

Selector `4` has:

```text
state_plane - source + 0x0fff
```

Do not change this back to source-minus-state.

## Retained feature record

```text
+0x00        local flag / quality-like field; exact name unclaimed
+0x02        X Q8
+0x04        Y Q8
+0x06        signed Q12-radian direction
+0x08        response/strength-like field; exact name unclaimed
+0x0c        Y-border classification
+0x10..+0x1f 128-bit Pass-A sign/projection descriptor
+0x20..+0x27 64-bit lower-median threshold descriptor
+0x28..+0x2b 32-bit Pass-B sign/projection descriptor
+0x2c..+0x2f 32-bit lower-median threshold descriptor
+0x30..+0x37 reserved/cleared on normal type-0x0c extraction path
+0x38        status/validity-like byte
+0x39        neighborhood/spatial-support score
```

## Matcher — implementation-level facts

Normal correspondence path:

```text
0x59580
  -> 0x5baf0 descriptor cost, twice
  -> 0x5a350 ambiguity + spatial duplicate suppression + top-K
  -> flat int32 correspondence pairs
  -> 0x1f840 X/Y/direction reduction
       -> 0x586c0 deterministic 3-correspondence geometric consensus
       -> direction/pair filtering
       -> surviving inlier count
```

`0x586c0` fits three-point Q8 transforms, constrains them toward a scaled-rotation/similarity-like form, and accepts correspondence reprojection residuals below 2.5 pixels. It selects larger inlier count, tie-breaking on lower Q16 mean squared reprojection error. The later `0x4000` threshold is `0.25 px^2` / `0.5 px RMS`.

### Type-0x0c direct positive score

Per accepted candidate:

```text
q8_i = ((match_record[+0x04] << 8) + 21) / 42
accumulator += q8_i
```

Final direct score:

```text
score = ((accumulator * 100) / accepted_count) >> 8
```

`0x24c70` is only a candidate-subobject index reorder helper; its return value is overwritten and is not a score factor.

### Type-0x0c fallback/general positive score

The fallback finalizer uses geometry/spatial/binary-overlap metrics to select/gate a winner. For type `0x0c`, the positive final score is still normalized from the surviving inlier/count field using divisor `42`:

```text
q8   = ((field_04 << 8) + 21) / 42
score = (q8 * 100) >> 8
```

The binary-overlap path includes Q8 agreement and Jaccard/IoU values; `+0x20` is the one-bit Jaccard/IoU gate. These metrics gate acceptance; they are not multiplied into the positive score.

## Enrollment representation

Entry path:

```text
enrolAddImageWrapper
  -> 0x00cd70
  -> shared representation builder 0x16920/0x139e0
  -> 0x194f0 enrollment orchestrator
```

Persistent representation:

```text
multiple capture fingerprint objects
+ pairwise 0x1c relation graph
+ per-capture derived support state
```

Relevant context fields:

```text
+0x24  accepted capture count
+0x28  capacity
+0x30  capture pointer array
+0x1c0 relation record store
+0x87f0 ranking/index array
```

Each relation record:

```text
+0x00        direct relation strength/inlier count, -1 if unusable,
             or 2 for graph-closure-derived relation
+0x04..+0x1b 24-byte transform
```

`0x5d620` is the canonical relation accessor/materializer. There is one record per unordered pair; reverse direction is obtained by transform inversion.

## Type-0x0c post-finalizer `0x31d90`

After graph closure, type `0x0c` runs `0x31d90`. It is also callable later as a derived-state/cache recomputation routine.

It:

- ensures each capture has a bit-packed binary support mask at `capture+0x130`;
- reads/composes finalized graph transforms;
- computes a support/bit-count-like metric at `capture+0x118` (values below 20 become zero);
- computes `capture+0x138`, the number of active peers satisfying a transformed-coverage criterion with 40-percent scaling.

It does **not** traverse `+0xf0/+0xf8` minutiae arrays, does not rerun descriptor matching, does not reorder captures, and does not mutate the relation graph.

Detailed checkpoints:

- `docs/CHICAGO_MATCHER_GEOMETRIC_CONSENSUS_586C0_2026-08-28.md`
- `docs/CHICAGO_MATCHER_TYPE0C_DIRECT_SCORE_2026-08-28.md`
- `docs/CHICAGO_MATCHER_FALLBACK_SCORE_CLOSURE_2026-08-28.md`
- `docs/CHICAGO_ENROLLMENT_ENTRY_CD70_194F0_2026-08-28.md`
- `docs/CHICAGO_ENROLLMENT_MULTI_CAPTURE_GRAPH_2026-08-28.md`
- `docs/CHICAGO_ENROLLMENT_DERIVED_RELATION_5D620_2026-08-28.md`
- `docs/CHICAGO_ENROLLMENT_POST_FINALIZER_31D90_2026-08-28.md`

## Immediate task

Stop broad Windows static-analysis expansion. Establish the local Linux/libfprint build baseline, then create portable reference code and tests for the already-reconstructed device/image/preprocess/feature/matcher/enrollment behavior. Re-open DLL analysis only for a concrete failing differential/unit test that requires a missing exact helper detail.

After a reference implementation is testable, integrate the Goodix `27c6:5135` path into libfprint/fprintd with strict lifecycle and compatibility guards.

## Completion / safety gates

Do not call the project complete until all defined safety gates pass with no known unsafe behavior:

- `fprintd-enroll` works;
- `fprintd-verify` works;
- cold boot/reboot works;
- suspend/resume works;
- cancellation/timeouts recover cleanly;
- no secrets or biometric payloads in logs;
- Windows Hello still works afterward;
- existing Windows fingerprints still work afterward;
- no firmware erase/flash;
- no factory PSK rewrite/reprovision.

Never publish/request plaintext PSK or PSK hashes, full OTP, fingerprint images/raw/templates, full unit-specific runtime configuration/hash, `goodix.dat`, `goodix_calib.dat`, `Goodix_Cache.bin`, proprietary Goodix binaries, Windows biometric DB material, or process/memory dumps. Keep the Windows partition read-only during analysis and testing.
