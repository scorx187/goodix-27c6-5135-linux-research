# Chicago post-mask stage `0x18004aea0` — 2026-08-28

Target: `AlgoChicago.dll`, tested Goodix `27c6:5135`, logical chip `0x2504`, Chicago/type selector `0x0c`.

This checkpoint records static reverse-engineering conclusions only. No proprietary binary, fingerprint frame/template, PSK/OTP, unit-specific runtime config, or calibration payload is committed.

## Proven upstream path

```text
EngineAdapter.dll
 -> AlgoChicago.dll preprocessor_wrapper
 -> logical preprocessor routine 0x18000e780..0x18000e947
 -> core orchestrator 0x1800484e0
 -> 0x180044970: internal state plane - source (signed 16-bit difference)
 -> 0x180043c40: mask geometry cleanup + source-range validity filtering
 -> common join at 0x180048a43
 -> 0x18004aea0
```

For selector `0x0c`, the exact upstream subtraction is:

```text
diff16[i] = state_plus_0x9924_u16[i] - source_u16[i]
```

not the reverse. Selector 4 adds `0x0fff` to this same `state - source` relation.

## Exact boundary

```text
RVA 0x0004aea0 .. 0x0004b289
size 0x3e9 = 1001 bytes
normal return 0x18004b288
```

## Temporary planes / work descriptor

`0x18004aea0` allocates three full-size u16 planes:

```text
scratch_A
scratch_B
scratch_C
```

with:

```text
plane_bytes = width * height * 2
```

`scratch_C` initially receives a copy of the source-side plane.

The descriptor passed to `0x180049ba0` maps:

```text
+0x00 = scratch_A
+0x08 = global/static u16 surface 0x18010b890
+0x10 = global/static u16 surface 0x1801151b0
+0x18 = global/static u16 plane   0x1800ff920
+0x20 = global/static work ptr    0x180109240
+0x28 = global/static flag ptr    0x1800ff91c
+0x30 = scratch_B
+0x38 = global/static u16 surface 0x18011ead0
```

Inside `0x180049ba0`:

```text
r15 = scratch_A
r13 = scratch_B
rbp = 0x1801151b0
r12 = 0x1800ff920
[rsp+0x68] = 0x18010b890
[rsp+0xa8] = 0x1800ff91c
[rsp+0x60] = 0x18011ead0
```

## Type-0x0c pre-scaling

For the tested selector:

```text
scratch_C[i] = 3 * source_side_word[i]
global_0x1800ff920[i] = 3 * state_plus_0x9924[i]
```

`state+0x9924` remains an internal AlgoChicago state/calibration plane and is not yet proven identical to gfusb persisted ImageBase.

## Q13 primary correction surface

For selector `0x0c`, `scratch_A` starts at:

```text
0x2000 = 8192 = Q13 unity
```

The exact rounded composition primitive is:

```text
Q13_mul(a,b) = (a*b + 0x1000) >> 13
```

with equivalent scalar and SIMD implementations.

### Base adaptive update

`0x18004d6f0` directly modifies `scratch_A`. It forms a Q13 ratio from a reference plane and the current correction surface, applies the separable median filter at `0x18004c3b0`, then for selector `0x0c` updates only pixels satisfying:

```text
q != 0
x != 0
abs(q-x) > 1800
```

using:

```text
scratch_A[i] = min(round((scratch_A[i] * q) / x), 0x7fff)
```

### Gated late-factor learning

`0x1800497c0` gates two calls to `0x18004b460`. Those calls read `scratch_A` but update late global/static Q13 factor surfaces rather than writing `scratch_A` directly.

The accepted per-pixel learning sample must satisfy:

```text
abs(local_ratio - 0x2000) < 0x148
```

and is accumulated by a rounded running average with observation count capped at 30.

The still-open transform inside this gated learner is configured through `0x18004e820` with mode 9 and executed by `0x18004fff0`; `0x18004e110` is only the wrapper/orchestrator around those two helpers.

## Late type-0x0c `scratch_B` composition

Let:

```text
F2 = u16 surface at 0x18011ead0
flag_G = *(u32 *)0x1800ff91c
```

Then:

```text
if local_flag != 0:
    scratch_B = copy(scratch_A)
else if flag_G != 0:
    scratch_B = copy(scratch_A)
else:
    scratch_B[i] = Q13_mul(scratch_A[i], F2[i])
```

## Late `scratch_A` composition

Define:

```text
F0 = u16 surface at 0x1801151b0
F1 = u16 surface at 0x18010b890
```

If `flag_G != 0`:

```text
scratch_A[i] = Q13_mul(scratch_A[i], F0[i])
```

If `flag_G == 0`:

```text
tmp          = Q13_mul(scratch_A[i], F0[i])
scratch_A[i] = Q13_mul(tmp, F1[i])
```

## Persistent corrected plane

After `0x180049ba0`, the parent writes:

```text
state + 0x13244
```

For selector `0x0c`, shift 14 is used:

```text
if gate_word[i] != 0:
    if scratch_A[i] == 0:
        processed[i] = scratch_C[i] << 14
    else:
        processed[i] = round((scratch_C[i] << 14) / scratch_A[i])
else:
    processed[i] = scratch_C[i]
```

where:

```text
processed = state + 0x13244
gate_word = state + 0x4
```

Because `scratch_C` was multiplied by three on the tested path:

```text
3 * source-side plane
 -> construct/adapt Q13 correction surface
 -> compose late Q13 factors
 -> rounded per-pixel division
 -> persistent corrected image-like state+0x13244 plane
```

This is proven as a corrected image-like plane, not yet as the final matcher-normalized input.

## Immediate next target

Do not revisit the already-closed parent arithmetic. Continue at:

```text
0x18004e820  # mode-9 context builder
```

then follow only the selected execution path in:

```text
0x18004fff0
```

After the gated factor transform is fully closed, trace the consumer chain from `state+0x13244` toward matcher/enrollment and separately prove the producer of `state+0x9924`.

## Safety boundary

Do not request, publish, upload, commit, or hash real fingerprint frames/templates, PSK/OTP, full unit-specific runtime config, `goodix.dat`, `goodix_calib.dat`, `Goodix_Cache.bin`, proprietary Goodix binaries, Windows biometric DB material, or process dumps. Do not erase/flash firmware or rewrite/re-provision the factory PSK.
