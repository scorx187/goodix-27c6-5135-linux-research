# Chicago type-0x0c enrollment post-finalizer — `0x180031d90`

Date: 2026-08-28
Target: `AlgoChicago.dll`, family/type `0x0c` (Chicago)
Method: static disassembly only.

## Result

`0x180031d90` is a reusable **per-capture derived mask/coverage cache builder**. In the enrollment-completion path it runs after graph closure, but it is not an enrollment-only one-shot: three direct callers invoke it when template/capture state needs recomputation.

It does not fuse minutiae, does not rerun descriptor matching, does not reorder capture objects, and does not write the pairwise relation graph.

Its durable effects are primarily:

- ensuring each capture has a bit-packed binary support mask at `capture+0x130`;
- recomputing a support/bit-count-like metric at `capture+0x118`;
- recomputing a count of peer captures satisfying a transformed-coverage criterion at `capture+0x138`.

## Logical function boundary

Full CFG recovery proves:

```text
entry:                  0x180031d90
last reachable insn:    0x1800321dd
reachable instructions: 248
runtime/unwind entries: 3
```

No missing direct CFG targets and no indirect jumps were reported.

## Type-0x0c dispatch

`0x180032630` validates the context, reads `context+0x08` sensor type, and tests mask `0x07860680`.

For type `0x0c`, the corresponding bit is clear, so it calls `0x180031d90`. The wrapper then returns success (`EAX = 0`).

Within the completion path this gives:

```text
0x1800194f0 enrollment orchestrator
  -> 0x18005bf40 relation-graph closure
  -> 0x180032630 type dispatcher
       -> 0x180031d90 type-0x0c derived-state builder
```

## Context fields consumed

At entry `0x31d90` reads:

```text
context+0x0c   image/domain dimension
context+0x10   image/domain dimension
context+0x14   half-resolution / scaling-mode flag
context+0x24   capture count
context+0x30   capture pointer array
context+0x1c0  pairwise 0x1c relation-record store
context+0x87e0 relation/index anchor used in graph addressing
```

When `context+0x14 != 0`, the effective `+0x0c` and `+0x10` dimensions are divided by two for this derived coverage computation.

## `capture+0x130`: bit-packed support mask

For every capture, `0x31d90` checks `capture+0x130`. If absent it builds it through:

```text
capture+0x28
  -> 0x180051570
  -> temporary expanded binary map
  -> 0x180053130
  -> capture+0x130 bit-packed map
```

### `0x180051570`

This helper consumes the coarse binary mask/descriptor rooted at `capture+0x28` and expands/resamples it according to the effective dimensions and scaling mode.

The recovered arithmetic uses coarse dimensions based on quarter-scale rounding. The active branches expand source samples over a larger binary byte map, with the scaling flag selecting the effective replication/resampling factor.

Safe role name:

**coarse-mask expansion/resampling helper**.

### `0x180053130`

This helper converts a byte-per-sample binary map into a packed-bit representation. It groups source binary samples into output bytes row by row and allocates the destination representation when required.

Therefore `capture+0x130` is a persistent **bit-packed binary support/coverage mask**, not a minutia array.

## Relation graph use

For active captures (`capture+0x100 != 0`), `0x31d90` reads transforms from the finalized relation store at `context+0x1c0`.

The same canonical graph addressing is visible through:

- capture `+0x104` relation-base offsets;
- 0x1c-byte record stride;
- direct transform copy with `0x1800428f0`;
- reverse-direction transform generation with `0x180042ea0`.

For pairwise coverage evaluation, transforms are composed in local buffers using `0x180043460`.

No write to the relation graph occurs in `0x31d90`.

## Pairwise transformed-coverage stage

For an active capture, `0x31d90` iterates the other active captures and builds a composed transform describing their relation in the current capture's frame.

It then calls:

```text
0x180051bd0
0x180050830
```

### `0x180051bd0`

Static structure shows a transformed-region / scanline coverage rasterizer. It builds per-row span limits in an output structure and returns the accumulated span extent/count.

### `0x180050830`

This helper applies the generated row-span intervals to a bit-packed map. Its masks preserve valid bits at interval edges and clear bytes/bits outside the selected spans.

After these helpers, the caller applies a criterion with `40` percent scaling. A peer passing that criterion increments:

```text
capture+0x138
```

Do not overstate the exact public meaning of every intermediate return until needed for bit-exact reproduction. The safe proven meaning is:

**`capture+0x138` = number of active peer captures satisfying the pairwise transformed-coverage criterion.**

## `capture+0x118`: support metric

At the start of each capture iteration, `0x31d90` initializes:

```text
capture+0x138 = 0
capture+0x118 = effective_width * effective_height
```

For active captures, after the pairwise coverage work, it scans the shared bit-packed support map byte by byte and sums lookup values from `0x180079730`.

The same table is used elsewhere as a byte lookup in binary-descriptor/bit-oriented logic, but its exact mathematical identity should not be overclaimed until independently dumped.

The final rule is:

```text
if summed_lookup_metric < 20:
    capture+0x118 = 0
else:
    capture+0x118 = summed_lookup_metric
```

Safe role name:

**support / bit-count-like coverage metric**.

For captures with `capture+0x100 == 0`, pair processing is skipped and the initialized area-like value remains in `+0x118`, while `+0x138` remains zero.

## No minutia matching in this stage

The full function contains no meaningful references to the retained-feature fields:

```text
capture+0xf0
capture+0xf8
feature stride 0x3c
```

The only textual `0x3c` hit in the analyzer report is a stack-local offset, not a feature stride.

There is no call to the descriptor correspondence or geometric-inlier matcher chain.

Therefore `0x31d90` is not another biometric matcher pass.

## No pruning/reordering

The capture pointer array at `context+0x30` is traversed but not reordered. There is no selection-sort/reorder pattern and no capture-slot pointer mutation in this function.

## Reusable cache recomputation, not one-shot finalization

Three direct callers were found:

1. a completion/maintenance path that calls `0x31d90` when capture count reaches capacity;
2. a path that invokes it again after capture/template state changes;
3. the `0x180032630` type dispatcher.

Thus the correct architectural interpretation is a reusable **derived-state/cache recomputation routine**.

In the enrollment completion path it is the final type-0x0c derived-state transformation observed after `0x5bf40` graph closure before the dispatcher returns success. It is not restricted to that lifecycle moment.

## Enrollment representation after this stage

For type `0x0c`, the reconstructed persistent representation remains:

```text
multiple independent capture fingerprint objects
+ canonical unordered-pair relation graph
+ per-capture bit-packed support masks (+0x130)
+ per-capture derived support/coverage metadata (+0x118, +0x138)
```

There is no evidence that enrollment completion collapses all captures into one fused minutia list.

## Implementation consequence

The minimum decisive static matcher/enrollment architecture is now closed enough to begin a clean Linux reference implementation. Remaining helper details should be revisited only when differential/unit tests demonstrate that bit-exact behavior is required.

The next active work should move from Windows static analysis to a non-destructive Linux implementation baseline, then portable reference code, then libfprint/fprintd integration.

## Safety

Static `AlgoChicago.dll` analysis only. No fingerprint image, enrolled template, PSK, OTP, calibration payload, `goodix.dat`, Windows biometric database, proprietary binary, or unit-specific private biometric data is committed.

Do not firmware erase/flash, rewrite or reprovision factory PSK, perform arbitrary persistent register writes, or remove/re-enroll existing Windows fingerprints as a shortcut. Keep Windows read-only during compatibility work.
