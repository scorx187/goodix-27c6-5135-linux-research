# Chicago gated factor update — `0x18004b460` — 2026-08-28

Target: `AlgoChicago.dll`, tested Goodix `27c6:5135`, logical chip `0x2504`, selector/family `0x0c`.

This checkpoint records static reverse-engineering conclusions only. No proprietary binary, fingerprint frame/template, PSK/OTP, unit-specific runtime configuration, calibration payload, Goodix cache file, or Windows biometric database material is committed.

## Exact function boundary

PE x64 runtime-function table:

```text
RVA 0x0004b460 .. 0x0004b71c
size 0x2bc = 700 bytes
normal return 0x18004b71b
```

## Caller role on the tested `0x0c` path

`0x1800497c0` is only a gate in the caller. If it returns zero, the `0x18004b460` branch is skipped. If it returns nonzero, `0x18004b460` is called twice.

Both calls pass:

```text
R8 = scratch_A
```

but `0x18004b460` does not write `scratch_A` directly. Instead, it reads `scratch_A` and adaptively updates other per-pixel factor surfaces that are composed into `scratch_A` / `scratch_B` later in the same `0x180049ba0` invocation.

The two caller invocations use different late-factor destinations:

```text
first call  -> factor surface at caller [rsp+0x68] = 0x18010b890
second call -> factor surface at caller [rsp+0x60] = 0x18011ead0
```

Therefore this branch affects the final denominator surfaces indirectly but materially.

## Argument/data-flow recovery

Inside `0x18004b460`:

```text
arg1 = reference/global u16 plane side
arg2 = work/image object; data pointer at +0x18
arg3 = scratch_A (read-only in this function)
arg4 = pointer to an observation/update counter
arg5 = optional additional u16 factor plane
arg6,arg7 = geometry dimensions
arg8 = late factor surface to be adaptively updated
arg9 = caller-provided u32 full-plane work array
```

The function allocates a temporary u16 image object using `0x1800429b0`.

## First pass — Q13 ratio from reference and `scratch_A`

For every pixel, a 32-bit ratio is computed and stored in the caller-provided u32 work array:

```text
if scratch_A[i] == 0:
    ratio32[i] = reference[i] << 13
else:
    ratio32[i] = round((reference[i] << 13) / scratch_A[i])
```

using explicit half-denominator rounding.

The function also accumulates the ratio values and later computes a rounded full-plane mean:

```text
mean_ratio = round(sum(ratio32) / pixel_count)
```

If this mean is zero, global fallback value `*(u32 *)0x180093088` is used instead.

A global Q13 scale is then built:

```text
global_scale = round((fallback_or_reference_constant << 13) / mean_ratio)
```

The exact semantic name of the constant remains intentionally open.

## Temporary u16 plane generation

If optional `arg5` is absent, the temporary u16 plane receives the low 16-bit ratio values from `ratio32`.

If `arg5` is present, the denominator is first composed in Q13:

```text
combined = Q13_mul(scratch_A[i], arg5[i])
```

where:

```text
Q13_mul(a,b) = (a*b + 0x1000) >> 13
```

and then:

```text
if combined == 0:
    temp[i] = reference[i] << 13
else:
    temp[i] = round((reference[i] << 13) / combined)
```

Thus the optional factor participates before the spatial/statistical helper stage.

## Child spatial/statistical stage

The routine then calls:

```text
0x1800501a0(...)
0x18004e110(temp_object, work_object, ..., 9, -1, -1)
```

`0x18004e110` modifies/uses the temporary image before the final adaptive factor update. Its exact operation is not yet proven and is the immediate remaining unknown inside this branch.

## Final per-pixel update test

Before the final loop, each stored `ratio32[i]` is Q13-scaled by `global_scale`:

```text
ratio32_scaled[i] = Q13_mul(ratio32[i], global_scale)
```

For each pixel, a local Q13 ratio is then derived from the filtered temporary plane and the original work-image value:

```text
if work[i] != 0:
    local_ratio = round((temp_filtered[i] << 13) / work[i])
else:
    local_ratio = 0x2000
```

`0x2000` is Q13 unity.

The factor surface is updated only when:

```text
abs(local_ratio - 0x2000) < 0x148
```

where:

```text
0x148 = 328
```

This is a near-unity acceptance gate.

## Running-average factor update — PROVEN

Let:

```text
n   = *arg4
old = arg8[i]
r   = local_ratio
```

When the near-unity gate passes, the function writes:

```text
arg8[i] = round((old*n + r) / (n+1))
```

using integer half-denominator rounding.

After processing the full plane:

```text
*arg4 = min(*arg4 + 1, 30)
```

So `arg8` is an adaptively learned per-pixel Q13 factor surface with a running-average history count capped at 30 observations.

## Important consequence for the tested preprocessing path

`0x18004b460` does **not** directly mutate `scratch_A`.

Instead:

```text
scratch_A
  -> ratio/statistical analysis
  -> optional filtered local ratio
  -> adaptive update of late factor surface
  -> later Q13 composition in 0x180049ba0
  -> final scratch_A / scratch_B denominators
```

Therefore the `0x1800497c0`-gated branch cannot be discarded when reproducing Windows preprocessing: it can change the late Q13 factor surfaces used later in the same invocation.

## Immediate next target

Reverse exactly:

```text
0x18004e110
```

with the callsite from `0x18004b460` as the authoritative argument mapping.

Goals:

1. prove whether it filters/smooths/normalizes the temporary u16 ratio plane;
2. recover the exact role of the constant `9` and the two `-1` stack arguments;
3. determine whether `0x1800501a0` merely creates an auxiliary kernel/config object or materially changes the formula;
4. recover the exact `temp_filtered[i]` consumed by the near-unity `0x148` acceptance gate;
5. then close the full `0x18004b460` adaptive-factor update and return to the final `scratch_A/scratch_B` composition.

Do not descend into `0x1800497c0` unless a later dependency requires its exact gate predicate.

## Safety boundary

Do not request, publish, commit, upload, or hash real fingerprint frames/templates, PSK/OTP, full unit-specific runtime config, `goodix.dat`, `goodix_calib.dat`, `Goodix_Cache.bin`, proprietary Goodix binaries, Windows biometric database material, or process dumps. Never erase/flash firmware or write/re-provision PSK.
