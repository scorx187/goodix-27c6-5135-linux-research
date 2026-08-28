# Chicago post-mask stage `0x18004aea0` — 2026-08-28

Target: `AlgoChicago.dll`, tested Goodix `27c6:5135`, logical chip `0x2504`, Chicago/type selector `0x0c`.

This checkpoint records static reverse-engineering conclusions only. No proprietary binary, fingerprint frame, template, PSK/OTP, unit-specific runtime configuration, or calibration payload is committed.

## Proven upstream path

```text
EngineAdapter.dll
 -> AlgoChicago.dll preprocessor_wrapper
 -> logical preprocessor routine 0x18000e780..0x18000e947
 -> core orchestrator 0x1800484e0
 -> 0x180044970: source - internal state plane (signed 16-bit difference)
 -> 0x180043c40: mask geometry cleanup + source-range validity filtering
 -> common join at 0x180048a43
 -> 0x18004aea0
```

## Exact `0x18004aea0` boundary

PE x64 runtime-function table:

```text
RVA 0x0004aea0 .. 0x0004b289
size 0x3e9 = 1001 bytes
normal return = 0x18004b288
```

## Temporary planes and descriptor mapping

`0x18004aea0` allocates three full-size u16 planes:

```text
plane_words = *(u32 *)(arg7+0) * *(u32 *)(arg7+4)
plane_bytes = plane_words * 2

scratch_A = alloc(plane_bytes)
scratch_B = alloc(plane_bytes)
scratch_C = alloc(plane_bytes)
```

`scratch_C` receives a byte-for-byte copy of the source-side plane.

The work descriptor passed into `0x180049ba0` is populated as:

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

Inside `0x180049ba0` this resolves to:

```text
r15 = scratch_A
r13 = scratch_B
rbp = 0x1801151b0
r12 = 0x1800ff920
[rsp+0x68] = 0x18010b890
[rsp+0xa8] = 0x1800ff91c
[rsp+0x60] = 0x18011ead0
```

## Type-0x0c source/reference pre-scaling — PROVEN

The parent tests `arg7+0x18` against bit mask `0x02473932`; selector `0x0c` is included.

For the tested path:

```text
scratch_C[i] = 3 * scratch_C[i]
global_0x1800ff920[i] = 3 * state_plus_0x9924[i]
```

This is integer pre-scaling, not floating-point normalization.

`state+0x9924` remains an AlgoChicago internal preprocessing/calibration-state plane. It must not yet be equated with gfusb.dll's persisted ImageBase.

## `0x180049ba0` — substantial correction-surface builder

The exact body returns at `0x18004ab12` and contains multiple full-plane scalar and SIMD passes.

For selector `0x0c`, execution takes the general non-`0x0b` path. Near `0x18004a040`, the primary plane is initialized to:

```text
scratch_A[i] = 0x2000
```

for every WORD.

`0x2000 == 8192`, which is unity in Q13 fixed-point representation.

The repeated scalar composition primitive is exactly:

```text
Q13_mul(a,b) = (a*b + 0x1000) >> 13
```

and the vectorized implementation uses equivalent `pmulld` / `paddd` / `psrad` sequences.

Thus `scratch_A` is proven to be a Q13 per-pixel multiplicative correction/gain surface, not a raw image copy or generic local-average plane.

## Exact selector-0x0c helper choice before final composition — PROVEN

After the type test:

```text
0x18004a0d1: cmp r8d,0x0c
```

selector `0x0c` does not take the `0x04/0x0b` helper path. When the surrounding condition enables this branch, the call is:

```text
0x18004d6f0(
    RCX = 0x1800ff920,
    RDX = temporary full-image/work buffer,
    R8  = scratch_A,
    R9D = one geometry dimension,
    stack = other geometry dimension + selector 0x0c
)
```

Therefore `0x18004d6f0` is the first currently unresolved child that directly receives and may construct/modify the primary Q13 denominator surface for the tested device.

`0x1800497c0` and later helpers remain downstream on the same branch, but should not be reversed before proving what `0x18004d6f0` writes.

## Exact type-0x0c `scratch_B` composition — PROVEN

The later dispatch performs:

```text
selector - 0x0b <= 1
```

so selector `0x0c` enters the branch at `0x18004a727`, where the fixed-point shift is explicitly set to 13.

Let:

```text
F2 = u16 surface at 0x18011ead0
flag_G = *(u32 *)0x1800ff91c
```

Then the branch proves:

```text
if local_flag != 0:
    scratch_B = copy(scratch_A)
else if flag_G != 0:
    scratch_B = copy(scratch_A)
else:
    scratch_B[i] = Q13_mul(scratch_A[i], F2[i])
```

The copy cases use helper `0x1800428f0`; the independent composition has matching SIMD and scalar tails and writes directly to `scratch_B` (`r13`).

This fully resolves the late type-0x0c copy-versus-compose behavior of the secondary denominator surface.

## Exact late `scratch_A` composition — PROVEN

Define:

```text
F0 = u16 surface at 0x1801151b0
F1 = u16 surface at 0x18010b890
flag_G = *(u32 *)0x1800ff91c
```

At `0x18004a873`, the same global flag selects one of two final primary-surface formulas.

If `flag_G != 0`:

```text
scratch_A[i] = Q13_mul(scratch_A[i], F0[i])
```

If `flag_G == 0`:

```text
tmp          = Q13_mul(scratch_A[i], F0[i])
scratch_A[i] = Q13_mul(tmp,          F1[i])
```

Both formulas are independently visible in their SIMD loops and scalar tails.

Therefore the final primary denominator is a composed Q13 correction surface whose late factors are now exactly known. The remaining unknown is the value of `scratch_A` immediately before these late factors, especially the contribution of `0x18004d6f0` and subsequent tested-branch helpers.

## Parent fixed-point corrected plane at `state+0x13244` — PROVEN

After `0x180049ba0`, the parent writes a persistent state-owned plane at:

```text
state + 0x13244
```

For type `0x0c`, the parent chooses:

```text
shift = 14
```

and computes conceptually:

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

The division uses integer rounding by adding half the denominator before division.

Because the type-0x0c parent pre-scales `scratch_C` by three, the tested high-level path is now:

```text
3 * source-side plane
 -> construct Q13 per-pixel correction surface scratch_A
 -> append proven static/global Q13 factors
 -> rounded fixed-point per-pixel division
 -> persistent corrected image-like plane at state+0x13244
```

If the optional parallel output is requested, `scratch_B` is used as its denominator with the analogous source numerator.

The conservative semantic description remains `fixed-point ratio/corrected plane`; it is not yet proven to be the final matcher-normalized fingerprint image.

## Caller-visible reciprocal/gain-like auxiliary output — PROVEN

Another optional parent output is built from a global per-pixel WORD array using reciprocal-style Q13 arithmetic:

```text
if denominator_word == 0:
    out = constant << 13
else:
    out = round((constant << 13) / denominator_word)
```

This is coefficient/gain-like rather than a copied raw image plane.

## What remains unknown

The remaining blockers are now narrow:

1. **Exact effect of `0x18004d6f0` on `scratch_A` for selector `0x0c`.** It directly receives the primary Q13 plane before the proven final factor composition.
2. After that, determine whether `0x1800497c0` / `0x18004b460` further change the tested `scratch_A` value before `0x18004a727`.
3. Producer of AlgoChicago `state+0x9924`, and whether it derives from gfusb persisted ImageBase.
4. Exact semantic meaning of the per-pixel gate plane at `state+0x4`.
5. Downstream consumer chain from `state+0x13244` to matcher/enrollment-facing processing.

## Immediate next target

Reverse exactly the runtime-function containing:

```text
0x18004d6f0
```

Do not reverse every helper in `0x180049ba0`.

Goals for `0x18004d6f0`:

1. establish exact PE runtime-function boundary;
2. recover its argument roles from first reads/writes;
3. prove whether `R8 = scratch_A` is an output/in-out plane;
4. recover the exact per-pixel or spatial formula written to `scratch_A`;
5. identify any child call that actually generates the factor plane, and descend only into that one if necessary.

After this, continue only along the proven selector-0x0c path.

## Safety boundary

Do not request, publish, commit, upload, or hash real fingerprint frames/templates, PSK/OTP, unit-specific full runtime configuration, `goodix.dat`, `goodix_calib.dat`, `Goodix_Cache.bin`, proprietary Goodix binaries, Windows biometric database material, or process dumps.
