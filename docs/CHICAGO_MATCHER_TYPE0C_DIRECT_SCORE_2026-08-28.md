# Chicago type-0x0c direct matcher score — 2026-08-28

Target: `AlgoChicago.dll`, sensor family/type `0x0c` used by Goodix `27c6:5135`.

This checkpoint records only static reverse-engineering facts needed for a compatible Linux implementation. No proprietary binaries, biometric samples/templates, device secrets, OTP, calibration payloads, Windows biometric database material, or unit-specific runtime configuration are committed.

## Context

The matcher orchestrator is `0x1800293c0`. The normal type-`0x0c` path already has a substantially closed correspondence pipeline:

```text
0x18005baf0  binary descriptor-cost primitive
0x18005a350  ambiguity/spatial/top-K correspondence consolidation
0x1800586c0  affine geometric consensus / reprojection inlier mask
0x1800430f0  exact 3-point affine solver
0x18005e790  affine singular-value plausibility gate
0x180024880  direction consistency modulo pi
0x18001f840  surviving inlier count
```

`0x1800258c0` and `0x18001c920` are policy/acceptance gates over already-computed matcher metrics, not raw descriptor comparators.

## Type-0x0c per-candidate score accumulation

Inside `0x1800293c0`, type `0x0c` uses divisor `42` (`0x2a`). For every candidate/subobject that reaches the accepted accumulation path, the code adds a Q8-normalized contribution equivalent to:

```text
q8_i = ((match_record_plus_0x04 << 8) + 21) / 42
accumulator += q8_i
accepted_count += 1
```

The `+21` term is half the divisor and therefore implements nearest-style integer rounding for the positive metric values expected on this path.

Do not over-name `match_record+0x04` beyond the currently proven role: it is a correspondence/matcher work metric and was earlier normalized to the maximum of work-record `+0x04` and `+0x08` on the active path.

## Direct score formula

After the candidate loop, if the direct-score branch is selected, the score write is exactly:

```text
score = ((accumulator * 100) / accepted_count) >> 8
```

This is therefore approximately:

```text
score ~= 100 * mean(match_record_plus_0x04 / 42)
```

but the compatible implementation must preserve the integer operation order above rather than replacing it with floating-point arithmetic.

The score destination is the first argument/output pointer saved by `0x1800293c0`.

## Important correction: `0x180024c70` is NOT a score factor

Earlier exploratory notes treated the return value of `0x180024c70` as a possible multiplicative score factor. That interpretation is wrong.

The caller sequence is:

```text
call 0x180024c70
imul eax, DWORD PTR [accumulator], 0x64
cdq
idiv accepted_count
sar eax, 8
mov [score_out], eax
```

The three-operand `imul` overwrites `EAX`; it does not multiply the old return value. Therefore the return value of `0x24c70` is discarded for score arithmetic.

## `0x180024c70` exact structural role

Full CFG recovery spans three unwind entries:

```text
0x180024c70 .. 0x180024d62
66 reachable instructions
no child calls
```

Inputs/state used:

```text
container+0x24   subobject count
container+0x30   subobject pointer array
container+0x87f0 mutable int32 index/order array
```

For each indexed subobject it compares these fields lexicographically:

```text
primary:   subobject+0x100  larger first
secondary: subobject+0x124  larger first
tertiary:  subobject+0x11c  larger first
```

It performs an in-place selection-sort-like reorder of the index array at `container+0x87f0`.

Neutral role name:

**candidate-subobject ranking/index reorder helper**.

Its incidental `EAX` value on return is not part of the direct-score formula.

## Why the ranking side effect exists

Immediately before `0x24c70`, `0x293c0` increments `subobject+0x124` for subobjects selected/used in the current matching pass. `0x24c70` then reorders the persistent subobject index list using `+0x100`, `+0x124`, and `+0x11c` priorities.

This ranking side effect may influence subsequent matcher passes or container state, but it is not an arithmetic factor in the score being written at that call site.

## Direct branch vs general/fallback finalizer

The direct average-score branch requires at least one accepted candidate and additional matcher-context predicates. If those conditions are not satisfied, control falls through to the general finalization path beginning around `0x18002a9ae` and calling:

```text
0x180027490
```

If that path produces a score below 1 and `container+0x8e10 == 1`, an additional helper `0x180027890` is invoked before return.

The immediate next target is therefore `0x180027490`, not more correspondence helpers.

## Current implementation-level formula

For the normal direct type-`0x0c` branch, retain this exact integer structure:

```c
int32_t accumulator = 0;
int32_t accepted_count = 0;

for each accepted candidate {
    int32_t m = match_record_plus_0x04;
    int32_t q8 = ((m << 8) + 21) / 42;
    accumulator += q8;
    accepted_count++;
}

if (direct_score_branch && accepted_count > 0) {
    reorder_candidate_subobject_indices();   // 0x180024c70 side effect
    score = ((accumulator * 100) / accepted_count) >> 8;
}
```

This pseudocode describes only the proven arithmetic/ordering role. It does not yet reproduce the predicates selecting direct score vs the `0x27490` finalizer path.

## Next target

Reverse `0x180027490` only, including any genuinely decisive child it delegates to, and determine:

1. whether it writes the score directly through its first argument;
2. what input metrics it consumes from the probe, candidate container, workspaces, and matcher context;
3. whether it is a general score fusion formula or a wrapper around one decisive child;
4. the meaning of its return value stored by `0x293c0` in `EBX`;
5. when `0x180027890` can alter a sub-1 result.

Do not reopen already-closed correspondence geometry unless a later score consumer proves that additional side state is required.
