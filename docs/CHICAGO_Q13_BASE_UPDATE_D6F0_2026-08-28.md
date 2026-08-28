# Chicago type-0x0c base Q13 update — `0x18004d6f0` — 2026-08-28

Target: `AlgoChicago.dll`, tested Goodix `27c6:5135`, logical chip `0x2504`, selector/family `0x0c`.

This checkpoint records static reverse-engineering conclusions only. No proprietary binary, fingerprint frame/template, PSK/OTP, unit-specific runtime config, calibration payload, or Goodix cache file is committed.

## Exact function boundary

The x64 runtime-function table gives:

```text
RVA 0x0004d6f0 .. 0x0004d882
size 0x192 = 402 bytes
normal return 0x18004d881
```

## Caller mapping on the tested `0x0c` path

At the call from `0x180049ba0`:

```text
RCX = reference/global plane side
RDX = image/work object whose data pointer is at +0x18
R8  = scratch_A, the primary Q13 correction surface
R9D = one geometry dimension
stack arg5 = the other geometry dimension
stack arg6 = sensor selector/type (0x0c on tested device)
```

Inside `0x18004d6f0`:

```text
work_input = *(u16 **)(RDX + 0x18)
scratch_A  = original R8
reference  = original RCX
pixel_count = arg4 * arg5
selector = arg6
```

The helper allocates a second image/work object using `0x1800429b0`; its data pointer becomes a temporary full-plane WORD buffer `tmp`.

## First full-plane pass — exact reciprocal/ratio relation

For every pixel, the routine constructs a Q13 ratio surface in `tmp` from `reference` and the current `scratch_A`:

```text
if scratch_A[i] == 0:
    tmp[i] = reference[i] << 13
else:
    tmp[i] = round((reference[i] << 13) / scratch_A[i])
```

The rounding is explicit integer half-denominator rounding:

```text
(reference << 13) + scratch_A/2
--------------------------------
            scratch_A
```

No floating-point arithmetic is involved.

After the ratio plane is built, the routine calls:

```text
0x18004c3b0(tmp_object, original_work_object)
```

Therefore the value of `tmp` consumed afterward is the result of this spatial/helper stage, not necessarily the raw per-pixel ratio computed before the call.

## Type-0x0c update threshold — PROVEN

After `0x18004c3b0`, a selector-dependent threshold is chosen.

Default:

```text
0x258 = 600
```

For selectors present in bit mask `0x02473800`, the threshold becomes:

```text
0x708 = 1800
```

Selector `0x0c` is present in that mask, therefore the tested device uses:

```text
threshold = 1800
```

Selectors `0x18` / `0x1a` have another special threshold `0x320`; this is not the tested 5135 path.

## Second pass — exact in-place update of `scratch_A`

For each pixel:

```text
q = tmp_after_0x18004c3b0[i]
x = work_input[i]
a = scratch_A[i]
```

The routine first computes:

```text
abs(q - x)
```

`scratch_A[i]` is updated only when all of these are true:

```text
abs(q - x) > threshold
q != 0
x != 0
```

For the tested selector `0x0c`, this means:

```text
abs(q - x) > 1800
q != 0
x != 0
```

When the update is taken, the exact relation is:

```text
scratch_A[i] = round((scratch_A[i] * q) / x)
```

using half-denominator rounding:

```text
scratch_A * q + x/2
-------------------
         x
```

For selector `0x0c`, the result is additionally saturated at:

```text
0x7fff
```

because `0x0c` is also present in the tested saturation bit mask.

Thus the tested-path update can be written conservatively as:

```text
if q != 0 and x != 0 and abs(q-x) > 1800:
    scratch_A[i] = min(
        round((scratch_A[i] * q) / x),
        0x7fff
    )
else:
    scratch_A[i] is unchanged
```

This proves `0x18004d6f0` is a direct per-pixel adaptive modifier of the primary Q13 correction surface.

## Meaning of this stage

The safe semantic interpretation is:

```text
current Q13 correction surface
 + reference/current ratio plane
 + spatial transform from 0x18004c3b0
 -> thresholded adaptive Q13 correction update
```

Do not yet assign a stronger label such as illumination equalization or sensor flat-field correction until the exact operation performed by `0x18004c3b0` is proven.

## Immediate next target

The only remaining unknown inside this exact update equation is the transformation applied by:

```text
AlgoChicago.dll 0x18004c3b0
```

It is called after constructing the Q13 `reference / scratch_A` ratio plane and before the thresholded update compares that plane with the original work-input plane.

Next goals:

1. recover the exact function boundary of `0x18004c3b0`;
2. determine whether it smooths, filters, interpolates, clips, fills, or otherwise spatially transforms the temporary Q13 plane;
3. identify which of its two image/work objects is source versus destination;
4. recover its exact local-neighborhood/window formula if it performs spatial filtering;
5. then combine it with this checkpoint to obtain the complete base `scratch_A` update for selector `0x0c`.

After that, continue with the already-proven Q13 composition passes in `0x180049ba0` and finally the parent division into the persistent corrected plane at `state+0x13244`.

## Safety boundary

Do not request, publish, commit, upload, or hash real fingerprint frames/templates, PSK/OTP, unit-specific full runtime config, `goodix.dat`, `goodix_calib.dat`, `Goodix_Cache.bin`, proprietary Goodix binaries, Windows biometric database material, or process dumps.
