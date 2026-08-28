# Chicago matcher correspondence consolidation and reduction — 2026-08-28

Target: Goodix USB fingerprint sensor `27c6:5135`, ChicagoHS/ChicagoHU, sensor family/type `0x0c`.

This document records a static reverse-engineering checkpoint inside `AlgoChicago.dll` for Linux compatibility research. It contains no proprietary binary, factory secret, biometric image/template, unit-specific calibration/runtime payload, or Windows biometric database material.

## Scope

This checkpoint extends the already-proven `0x18005baf0` pairwise binary-descriptor comparator and closes the structural roles of:

```text
0x18005a350  correspondence consolidation / selection
0x18001f840  geometric/directional correspondence reduction and survivor count
```

The exact internal inlier policy delegated to `0x1800586c0` and the additional per-pair filter `0x180024880` remain the next active targets.

## Current normal type-0x0c path

```text
0x180059580 primary correspondence builder
  -> 0x18005baf0 pairwise binary descriptor comparison
  -> 0x18005baf0 second pass
  -> 0x18005a350 correspondence consolidation
  -> 0x18001f840 geometric/directional reduction
       -> 0x1800586c0 inlier-mask / geometry helper
       -> 0x180024880 additional pair filter
       -> optional 0x180058f90 refinement side effect
  -> survivor count returned to matcher orchestration
```

`0x18005a350` is used by more than this one path, but the description below is grounded in the normal type-`0x0c` call chain from `0x180059580`.

## `0x18005a350` — correspondence consolidation

### Not reciprocal-nearest-neighbor matching

The recovered function does not implement a direct reciprocal relation such as:

```text
B[A[i]] == i
```

No such index-chasing condition is present in the recovered body.

### Inputs

The function consumes the cost/index work state produced around the two `0x18005baf0` passes plus one complete `0x3c` feature array used for spatial duplicate checks.

Do not over-name the two work buffers as strictly "forward" and "reverse" correspondence sets; the safe structural description is **cost/index work buffers** generated/updated by the surrounding descriptor-comparison passes.

### Best-vs-second-best ambiguity test

For each candidate correspondence, the function reads a best cost and a second cost and applies a multiplicative comparison using two policy/configuration values:

```text
best_cost * cfg_A < second_cost * cfg_B
```

Only pairs passing this inequality continue into the selected correspondence set.

This is a best-vs-second-best ratio/ambiguity gate in integer form. Exact public names for `cfg_A` and `cfg_B` remain unclaimed.

### Spatial duplicate suppression

For a candidate pair that passes the cost gate, the function loads the source feature coordinates from the `0x3c` feature record:

```text
feature+0x02 = X Q8
feature+0x04 = Y Q8
```

It compares this feature against already-selected correspondences using squared Q8 coordinate distance:

```text
d2_q16 = (x_new - x_old)^2 + (y_new - y_old)^2
```

and treats values below:

```text
0x10000
```

as a spatial conflict/duplicate neighborhood.

Because coordinates are Q8, `0x10000` corresponds to a squared distance of one pixel in integer-pixel units after removing the Q8 scale, but retain the exact machine-domain condition above for bit-compatible reconstruction.

When two correspondence candidates conflict spatially, the lower-cost candidate is retained.

### Bounded best-K correspondence set

The selected set is stored internally as 16-byte temporary records containing four DWORD values, including the two feature indices and the associated costs/metrics.

If capacity has not been reached, the new pair is appended.

If capacity is full, the function searches the current selected set for the worst/largest retained cost. The new pair replaces that entry only if its cost is lower.

Thus `0x5a350` implements a bounded top-K correspondence selector after ambiguity and spatial-duplicate filtering.

### Output format

At function exit, the selected records are converted to a flat pair-index output buffer:

```text
[index_A0, index_B0,
 index_A1, index_B1,
 ...]
```

Each element is a signed 32-bit integer.

Unused pair slots are explicitly filled with:

```text
-1 / 0xffffffff
```

The function does not directly use minutia direction `feature+0x06` on this recovered path. Direction enters in the next reducer, `0x18001f840`.

## `0x18001f840` — geometric/directional correspondence reducer

### Important unwind correction

The first PE `RUNTIME_FUNCTION` entry covers only:

```text
0x18001f840 .. 0x18001f879
```

and is not the complete function.

Full CFG recovery proves one logical function across three unwind entries:

```text
0x18001f840 .. 0x18001fbb0
197 reachable instructions
```

with runtime entries:

```text
RVA 0x0001f840 .. 0x0001f879
RVA 0x0001f879 .. 0x0001fa2e
RVA 0x0001fa2e .. 0x0001fbb1
```

### Effective argument roles

For the matcher call path of interest, the safe structural ABI is:

```text
RCX   = candidate/template container used to resolve the selected candidate subobject
RDX   = opposite/probe fingerprint object
R8D   = candidate/subobject index
R9D   = number/limit of correspondence pair slots to inspect
arg5  = minimum-valid-pair threshold
arg6  = geometric filtering mode/policy value
arg7  = flat int32 correspondence pair array
arg8  = matcher geometry/refinement context
```

Exact product-facing names for the policy/context fields remain unclaimed.

### Reads selected candidate and feature arrays

The function resolves a candidate subobject through one of two pointer tables in the container, then loads:

```text
candidate_subobject+0xf8 = candidate retained-feature array
probe_object+0xf8         = probe retained-feature array
```

### Expands flat pair indices into geometry arrays

For each inspected pair slot:

```text
index_A = pair[2*i]
index_B = pair[2*i + 1]
```

If `index_A < 0`, the pair is skipped.

For every valid pair it reads from both `0x3c` records:

```text
+0x02 X Q8
+0x04 Y Q8
+0x06 signed Q12-radian direction
```

and copies these into temporary parallel coordinate, direction, and index arrays.

This proves that direction consistency enters the correspondence-matching path here even though `0x5a350` itself used only X/Y for spatial de-duplication.

### Minimum-pair gate

The function counts the valid nonnegative pairs encountered into `ESI`.

If:

```text
valid_pair_count <= arg5
```

it skips geometric processing and returns zero.

On the main early matcher path, the caller commonly supplies `arg5 = 2`, so at least three valid input pairs are needed before this reducer proceeds on that call path.

### First inlier-mask stage: `0x1800586c0`

The function calls `0x1800586c0` with the prepared coordinate and direction arrays, valid-pair count, matcher context, an output byte mask, an output metric, and a mode/policy value.

After return, any pair whose mask byte is zero has both temporary pair-index arrays set to `-1`.

Therefore `0x586c0` is structurally an inlier/geometry classifier or mask producer over the prepared pair geometry.

The exact geometric model and thresholds are not yet closed.

### Second pair filter: `0x180024880`

The function then calls `0x180024880` using the selected candidate subobject, probe object, matcher context, and the temporary pair-index arrays.

`0x24880` itself contains `0x3c` stride arithmetic and reads minutia direction `+0x06` from both feature records.

After this call, any pair whose first temporary index is `-1` causes the corresponding byte in the inlier mask to be cleared.

Thus `0x24880` is an additional per-correspondence validity filter downstream of the first geometry mask.

Its exact direction/feature-consistency rule is the next target.

### Exact returned metric

After both filtering stages, the function loops over all valid prepared pairs and increments `EBX` once for every nonzero surviving mask byte:

```text
survivor_count = count(mask[i] != 0)
```

It then returns:

```text
EAX = EBX = survivor_count
```

Therefore the value stored by callers in the initial match-record count fields is not a probabilistic similarity score. It is a **surviving correspondence / inlier count after geometric and directional filtering**.

This is now structurally proven.

### Optional `0x180058f90` refinement

If:

```text
survivor_count >= 4
```

and the metric produced by `0x586c0` is greater than:

```text
0x4000
```

then `0x1f840` calls `0x180058f90` with geometry arrays, the inlier mask, matcher context, and that metric.

Importantly, `0x1f840` does **not** recount the mask after this call before returning; `EAX` is still loaded from the already-computed `EBX` survivor count.

Therefore `0x58f90` may refine side-state/context used later, but it is not required to define the integer returned by `0x1f840`.

## Relationship to matcher output

On the early `0x180028de0` path:

```text
0x59580 -> 0x1f840
```

and the returned survivor count is written to:

```text
match_record+0x00
match_record+0x04
```

A later refined correspondence path through `0x180058420` invokes `0x1f840` again, producing a refined correspondence-count metric used by subsequent score/policy logic.

## What is closed vs open

Closed enough:

- `0x5baf0` directly computes binary descriptor pair costs;
- `0x5a350` performs integer best/second-best ambiguity filtering;
- `0x5a350` performs spatial duplicate suppression using X/Y;
- `0x5a350` retains a bounded best-K selected set;
- `0x5a350` emits flat signed `int32` index pairs and pads unused pairs with `-1`;
- `0x1f840` expands pairs back into X/Y/direction geometry;
- `0x1f840` applies a minimum valid-pair gate;
- `0x1f840` runs two geometry/direction filtering stages;
- `0x1f840` returns the count of surviving correspondence mask entries.

Still open:

- exact geometric model/thresholds implemented by `0x1800586c0`;
- exact additional per-pair rule in `0x180024880`;
- precise side effect/role of optional `0x180058f90` when needed downstream;
- exact later score fusion after the correspondence-count channel;
- enrollment update/template storage behavior.

## Immediate next task

Reverse only the two decisive reducers needed to define the correspondence inlier rule:

```text
0x1800586c0
0x180024880
```

Priorities:

1. determine the geometric transform/consistency model used by `0x586c0`;
2. identify how it fills the per-pair mask and the Q/fixed-point metric checked against `0x4000`;
3. determine the exact direction/feature rule used by `0x24880` to invalidate individual pairs;
4. avoid descending into `0x58f90` unless later score/context consumers prove its side effect is required.

After those two functions are closed, return to the minimum remaining path needed to reproduce final matcher score/policy fusion, then enrollment update/template storage.

## Safety boundary

Static analysis only. Never commit or publish plaintext factory PSK or PSK hashes, full OTP, fingerprint images/raw/templates, Windows biometric DB material, `goodix.dat`, `goodix_calib.dat`, `Goodix_Cache.bin`, proprietary Goodix DLL/EXE/CAT files, full unit-specific runtime configuration, unit-specific runtime-config hash, or process/memory dumps.

No firmware erase/flash, PSK rewrite/reprovision, destructive 5117 tooling, arbitrary persistent register writes, or Windows fingerprint removal/re-enrollment shortcuts. Keep the Windows partition read-only during analysis.