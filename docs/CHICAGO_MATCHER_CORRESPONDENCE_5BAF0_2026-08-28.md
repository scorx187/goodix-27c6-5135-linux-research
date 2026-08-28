# Chicago matcher correspondence primitive `0x18005baf0` — 2026-08-28

Target: Goodix USB fingerprint sensor `27c6:5135`, ChicagoHS/ChicagoHU, sensor family/type `0x0c`.

This document records a static reverse-engineering checkpoint inside `AlgoChicago.dll` for Linux compatibility research. It contains no proprietary binary, factory secret, biometric image/template, unit-specific calibration/runtime payload, or Windows biometric database material.

## Why this checkpoint matters

The matcher path is no longer only mapped at the wrapper/orchestrator level. For the normal type-`0x0c` path, `0x18005baf0` is now proven to directly traverse pairs of retained `0x3c` fingerprint-feature records and compute binary-descriptor distance/correspondence costs.

This is the first closed matcher-side primitive in the current reconstruction that directly consumes the same `0x3c` retained records produced by feature extraction.

## Current matcher path

The current high-level path is:

```text
identifyImageWrapper
  -> 0x18000d6c0 identify core
  -> 0x180016920 / 0x1800139e0 shared probe builder
  -> per-candidate loop
  -> 0x180028c90 matcher wrapper/orchestrator
  -> 0x1800293c0 matcher orchestrator / score fusion
       -> early correspondence/evidence stage 0x180028de0
            -> 0x180059580 primary correspondence-state builder
                 -> 0x18005baf0 pairwise descriptor-cost primitive  [twice]
                 -> 0x18005a350 correspondence consolidation       [next target]
            -> 0x18001f840 reduction/count metric
            -> 0x180050c80 spatial/image-representation similarity
            -> 0x180058420 alternate/refined correspondence state
            -> 0x18001f840 refined reduction/count metric
            -> 0x180051980 / 0x18005a6b0 additional relation metrics
       -> type-specific policy gates including 0x1800258c0 / 0x18001c920
       -> final score fusion / result write
```

`0x180028c90` returns execution/status in `EAX`; the actual match result is written through its first output argument. The decisive comparison logic is delegated beneath it.

## Important correction: PE unwind split at `0x5baf0`

An earlier disassembly stopped at the first `RUNTIME_FUNCTION` boundary:

```text
0x18005baf0 .. 0x18005bb71
```

That was incomplete. The visible first chunk already branched to `0x18005bf1c`, proving that the logical function crossed multiple unwind entries.

Full CFG recovery proves one logical function spanning:

```text
entry: 0x18005baf0
last instruction: 0x18005bf35
reachable instructions: 273
```

and three PE runtime/unwind entries:

```text
RVA 0x0005baf0 .. 0x0005bb71
RVA 0x0005bb71 .. 0x0005bf1c
RVA 0x0005bf1c .. 0x0005bf36
```

Therefore the earlier tentative claims that `0x5baf0` had no loops, no descriptor operations, and no substantive body are withdrawn.

## Caller-proven role

For the normal type-`0x0c` path, `0x180059580` calls `0x18005baf0` twice before calling `0x18005a350`.

The two calls use the two retained-feature arrays in opposite/directional matching contexts. The exact ABI still contains compact metadata and output work arrays, but the callee itself proves the essential role: pairwise comparison of two `0x3c` feature lists and production of distance/correspondence state.

The special branch associated with sensor types `0x09` / `0x12` uses a different helper and is not the active path for this device family `0x0c`.

## Direct `0x3c` feature-list traversal

The complete `0x5baf0` body contains explicit feature-stride arithmetic:

```text
index * 0x3c
inner feature += 0x3c
outer feature += 0x3c
```

The nested loops are real:

```text
outer loop: advances one feature list by 0x3c
inner loop: advances the other feature list by 0x3c
```

This is not a quality gate, spatial-map comparison, or matcher wrapper. It is a pairwise retained-feature comparison primitive.

## Primary binary descriptor comparison

For each feature pair, the first comparison block starts from each record at offset `+0x10`.

The loop processes four DWORDs in two two-DWORD groups, covering the 16-byte region:

```text
feature +0x10 .. +0x1f
```

This region was independently proven during feature extraction to contain the 128-bit Pass-A sign/projection descriptor.

For every compared DWORD, `0x5baf0` performs:

```text
xor_word = descriptor_A_word XOR descriptor_B_word
```

Then it extracts each of the four bytes of the XOR result and indexes a fixed DWORD lookup table at:

```text
0x180079730
```

The four table values are summed into the descriptor-pair cost.

Operationally this is a bytewise Hamming-like binary-descriptor distance. Do not yet call `0x180079730` an exact popcount table until its 256 table entries are independently dumped/verified, although its usage is strongly consistent with such a table.

## Additional binary-distance terms

After the primary `+0x10..+0x1f` comparison, `0x5baf0` performs additional XOR + byte-lookup cost calculations on selected later DWORDs in the retained record.

The same table `0x180079730` is reused.

The exact semantic grouping/weighting of all later descriptor DWORDs is not yet closed, so do not over-name those secondary terms. The important proven point is that the matcher directly consumes binary fields from the retained `0x3c` record beyond the initial 128-bit comparison.

## Pair rejection / thresholds

The compact metadata argument provides thresholds used during pair evaluation.

The function compares accumulated descriptor-distance terms against these thresholds and can reject a feature pair early.

A sentinel cost of `0xc0` (192) is used on some paths when one distance variant is not accepted while another path is retained.

Exact public names for each metadata threshold remain intentionally unclaimed until the caller structure is fully labeled.

## Per-pair output state

For accepted/evaluated pairs, `0x5baf0` computes two candidate accumulated costs and selects the smaller value.

It writes at least two forms of pairwise state:

1. a byte-valued pair cost in a work matrix/array;
2. a byte flag recording which cost variant won.

The work region advances by `0xb4` bytes per outer feature.

The exact semantic reason for the `0xb4` row capacity/stride remains to be closed; do not infer a public maximum minutia count solely from this stride.

## Best-match tracking

For each outer-list feature, `0x5baf0` maintains the best two observed pair costs and associated inner-list indices.

When a new cost is better than the current best:

```text
old best -> second-best slot
new cost -> best slot
new inner index -> best-index slot
```

If it is only better than the current second-best, only the second-best cost/index is replaced.

Therefore `0x5baf0` is not merely constructing a dense distance matrix; it also emits nearest-candidate correspondence/ranking state for later consolidation.

## No delegated comparison core

The complete logical function has only one direct call:

```text
0x18000c170
```

at function teardown, consistent with the stack-cookie/security check.

There is no substantive matcher child below `0x5baf0` on this path. The XOR/lookup comparison loops and nearest-candidate updates are implemented directly inside `0x5baf0`.

This closes a major uncertainty: the actual pairwise binary descriptor comparison primitive has been reached.

## Relationship to previously proven descriptor layout

The retained `0x3c` record currently has the following proven/substantially proven structure:

```text
+0x00        local flag / quality-like field (exact name unclaimed)
+0x02        X Q8
+0x04        Y Q8
+0x06        signed Q12-radian direction
+0x08        response/strength-like field (exact name unclaimed)
+0x0c        Y-border classification after extraction orchestration
+0x10..+0x1f 128-bit Pass-A sign/projection descriptor
+0x20..+0x27 64-bit Pass-A lower-median-threshold descriptor
+0x28..+0x2b 32-bit Pass-B sign/projection descriptor
+0x2c..+0x2f 32-bit Pass-B lower-median-threshold descriptor
+0x30..+0x37 cleared/reserved on the current normal type-0x0c extraction path before later consumers
+0x38        status/validity-like byte
+0x39        neighborhood/spatial-support score
```

Matcher-side `0x5baf0` now independently proves that binary descriptor material beginning at `+0x10` is not dead extraction output: it is consumed directly during candidate matching.

## Other matcher findings already established

### `0x18000ede0`

Whole-feature-list finalizer/reorder stage. It partitions/reorders retained `0x3c` features according to low status bits and swaps the parallel per-feature `object+0x164` score entries in lockstep. It writes a partition/count-like value at `output+0x108`.

### `0x180016930`

Computes one local image/map-derived score per retained minutia. One caller stores these per-feature scores at `object+0x164`.

### `0x180016bd0`

Computes neighborhood/spatial-support evidence across retained minutiae. It writes a clamped support-like score to `feature+0x39` and contributes to final per-feature status refinement.

### `0x180050c80`

Primarily a spatial/image-representation similarity channel. It consumes representations around `+0x28`, uses helpers including `0x180050940`, and produces matcher metrics consumed later by policy/score fusion. It does not directly traverse the `+0xf8` retained minutia list.

### `0x1800258c0` and `0x18001c920`

Threshold/policy predicates over already-computed match metrics. They are not raw descriptor comparators.

## Immediate next target

The next decisive function is:

```text
0x18005a350
```

It runs immediately after the two directional `0x18005baf0` calls in `0x180059580`.

Goal: determine exactly how the two directional best-match/cost/index result sets are merged into a final correspondence set consumed by later reduction (`0x18001f840`) and score fusion.

Priority questions:

1. Does `0x5a350` enforce reciprocal/mutual nearest-neighbor matches?
2. How are pair costs, alternate-cost flags, and best/second-best indices filtered?
3. What correspondence record/list does it output?
4. Which output count/state is consumed by `0x18001f840`?
5. Where do geometry/direction consistency constraints enter relative to descriptor distance?

Do not descend back into preprocessing, `0x258c0`, `0x1c920`, or `0x50c80` unless `0x5a350` proves a dependency.

## Safety boundary

Static analysis only. Never commit or publish plaintext factory PSK or PSK hashes, full OTP, fingerprint images/raw/templates, Windows biometric DB material, `goodix.dat`, `goodix_calib.dat`, `Goodix_Cache.bin`, proprietary Goodix DLL/EXE/CAT files, full unit-specific runtime configuration, unit-specific runtime-config hash, or process/memory dumps.

No firmware erase/flash, PSK rewrite/reprovision, destructive 5117 tooling, arbitrary persistent register writes, or Windows fingerprint removal/re-enrollment shortcuts. Keep the Windows partition read-only during analysis.