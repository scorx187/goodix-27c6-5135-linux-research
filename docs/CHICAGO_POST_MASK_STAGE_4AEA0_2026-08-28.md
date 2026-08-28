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

## Exact function boundary

The PE x64 runtime-function table gives:

```text
RVA 0x0004aea0 .. 0x0004b289
size 0x3e9 = 1001 bytes
normal return = 0x18004b288
```

## Exact stack/original-argument recovery

After the seven register pushes and `sub rsp,0x70`, the original Windows x64 arguments are recoverable as:

```text
[rsp+0xb0] = original RCX  = arg1
[rsp+0xc0] = original R8   = arg3
[rsp+0xc8] = original R9D  = arg4
[rsp+0xd0] = stack arg5
[rsp+0xd8] = stack arg6   (later reused as a local scratch slot)
[rsp+0xe0] = stack arg7
[rsp+0xe8] = stack arg8
```

The original `RDX` is preserved immediately in `r12`.

At the known call site from `0x1800484e0`:

```text
arg1 = source-image-side u16 plane
arg2 = second image/state-side pointer from the orchestrator
arg3 = large AlgoChicago state/context object
arg4 = configuration/global DWORD
arg5 = caller-owned descriptor/work structure
arg6 = optional structure containing two caller-visible output pointers
arg7 = geometry/mode/type structure
arg8 = cleaned mask/statistics result structure
```

Exact semantic names remain intentionally conservative where not required by the data flow.

## Geometry and three temporary u16 planes — PROVEN

The routine reads `arg7+0` and `arg7+4`, multiplies them, doubles the product, and allocates that many bytes three times:

```text
plane_words = *(u32 *)(arg7+0) * *(u32 *)(arg7+4)
plane_bytes = plane_words * 2

scratch_A = alloc(plane_bytes)
scratch_B = alloc(plane_bytes)
scratch_C = alloc(plane_bytes)
```

`scratch_C` receives a byte-for-byte copy of the original `arg1` source plane.

The `arg5` descriptor is populated with pointers to `scratch_A`, `scratch_B`, several static/global work arrays, and associated metadata before the child helpers run.

## Type-0x0c pre-scaling branch — PROVEN

The field `arg7+0x18` is tested against bit mask:

```text
0x02473932
```

The set bits include selector `0x0c`, so the tested 5135 path takes the special branch.

For each processed WORD, that branch performs:

```text
scratch_C[i] = 3 * scratch_C[i]
global_0x1800ff920[i] = 3 * state_plus_0x9924[i]
```

The second relation follows exactly from the address algebra in the loop: `r10 = state - scratch_C`, the source load uses `[r10 + rdx + 0x9922]` after `rdx` has advanced by two bytes, yielding `state + 0x9924 + 2*i`.

For selector types not in the mask, the routine instead copies `arg2` directly into the global plane at `0x1800ff920` and leaves `scratch_C` as the copied source plane.

This is a fixed integer pre-scaling step; it is not a clamp or floating-point normalization stage.

## Child-helper sequence

The exact significant order is:

```text
0x18004d3b0
optional 0x18004b290
0x180049ba0
final per-WORD fixed-point loop in 0x18004aea0
free scratch_B
free scratch_A
free scratch_C
return 1
```

### `0x18004d3b0`

This helper is called with the geometry/config structure, the large state object, and several global work arrays. It does not receive the three scratch image planes directly in the first register arguments. Its exact semantic role remains open.

### Optional `0x18004b290`

The call occurs only when `arg7+0x10 != 0`.

Its primary arguments include:

```text
RCX = scratch_C
RDX = arg4
R8  = global plane 0x1800ff920
```

plus dimension/config fields. Its exact transform remains open.

### `0x180049ba0`

Immediately before the final fixed-point output loop, the call receives:

```text
RCX = scratch_C
RDX = state/context
R8  = original source plane (arg1)
R9  = global/static work pointer 0x1800ff914
arg5 = caller descriptor/work structure
arg6 = geometry/mode/type structure
arg7 = cleaned mask/statistics structure
```

Therefore `0x180049ba0` is the primary remaining producer of the temporary denominator/work planes consumed immediately afterward.

## Caller-visible reciprocal/gain-like auxiliary output — PROVEN

If the second pointer stored in the optional `arg6` structure is non-null, `0x18004aea0` fills it from a global per-pixel WORD array using integer reciprocal-style fixed-point arithmetic.

Conceptually, for each element:

```text
if denominator_word == 0:
    out = constant << 13
else:
    out = round((constant << 13) / denominator_word)
```

The exact semantic label of this optional plane remains open, but it is clearly coefficient/gain-like rather than a raw copied image plane.

## Persistent processed plane at `state+0x13244` — PROVEN

The final loop resolves all relevant pointers relative to `scratch_A` and iterates over the WORD count from `arg7+0x08`.

Its persistent destination is unequivocally:

```text
state + 0x13244
```

The three scratch planes are freed only after this destination has been written.

For the tested type-0x0c branch, the earlier selector test is true, so the loop chooses fixed-point shift:

```text
shift = 14
```

For non-special types the selected shift is `13`.

The principal destination relation is:

```text
if gate_word[i] != 0:
    if scratch_A[i] == 0:
        processed[i] = scratch_C[i] << shift
    else:
        processed[i] = round((scratch_C[i] << shift) / scratch_A[i])
else:
    processed[i] = scratch_C[i]
```

where:

```text
processed = state + 0x13244
gate_word = state + 0x4
```

The division uses integer rounding by adding half the denominator before signed division:

```text
(numerator + denominator/2) / denominator
```

For the tested type-0x0c path, `scratch_C` has already been multiplied by three before this loop.

If the first optional output pointer from `arg6` is non-null, a parallel relation is written using `scratch_B` as its denominator, with the same source numerator and fixed-point shift.

This proves that `0x18004aea0` is not merely an orchestrator: it emits a persistent, state-owned **fixed-point ratio/corrected image-like plane** at `state+0x13244`.

The conservative description is intentionally `fixed-point ratio/corrected plane`; calling it the final matcher-normalized fingerprint image still requires tracing its later consumers.

## No float/SIMD in this exact stage

The exact body contains no matching floating-point or SIMD normalization instructions. Its visible image arithmetic is scalar integer/WORD arithmetic:

```text
WORD loads/stores
multiply-by-3
left shifts
integer divide with rounding
conditional fallback/copy
```

## What remains unknown

Two important links are still open:

1. **How `0x180049ba0` constructs `scratch_A` and `scratch_B`.** These are the denominators that directly determine the persistent plane at `state+0x13244`.
2. **How `state+0x9924` is populated.** It is statically proven to participate in this type-0x0c path, but it is not yet proven identical to gfusb.dll's persisted ImageBase.

Also still open is the exact semantic meaning of the per-pixel gate plane beginning at `state+0x4`.

## Immediate next target

Reverse-engineer `0x180049ba0` next, not `0x18004d3b0` at random.

Reason: `0x180049ba0` runs immediately before the final output loop and must establish or transform the temporary denominator planes (`scratch_A` and `scratch_B`) that are used directly in:

```text
processed[i] = round((scratch_C[i] << 14) / scratch_A[i])
```

for the tested type-0x0c path.

Goals:

1. establish the complete logical function boundary of `0x180049ba0`;
2. determine exactly where it writes `scratch_A` and `scratch_B` through the caller descriptor;
3. recover the per-pixel/local-window formula producing those denominator values;
4. identify whether they represent local illumination, background estimate, gain denominator, smoothed reference, or another correction surface;
5. trace the resulting `state+0x13244` plane into `0x1800435a0` and later matcher-facing stages.

## Safety boundary

Do not request, publish, commit, upload, or hash real fingerprint frames/templates, PSK/OTP, unit-specific full runtime configuration, `goodix.dat`, `goodix_calib.dat`, `Goodix_Cache.bin`, proprietary Goodix binaries, Windows biometric database material, or process dumps.
