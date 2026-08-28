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

Immediately before the final fixed-point output loop, the call receives the scaled/copied source-side scratch, state/context-side inputs, the work descriptor, geometry/type information and the cleaned mask result.

This child is now proven to be a substantial correction-surface builder, not a single local-average helper.

## `0x180049ba0` type-0x0c correction-surface behavior — NEW PROOF

The exact `0x180049ba0` body returns at `0x18004ab12` and contains multiple scalar and SIMD full-plane passes.

For the tested selector `0x0c`:

1. the selector is explicitly compared with `0x0c` after helper `0x18004b390`;
2. selector `0x0c` is **not** the special `0x0b` path, therefore execution takes the general branch beginning near `0x180049e8d`;
3. that branch performs non-negative WORD difference/threshold transforms on working planes;
4. after the branch rejoins near `0x18004a040`, the routine initializes the descriptor plane at `arg5+0` — the plane mapped by the parent as `scratch_A` — to:

```text
0x2000
```

for every WORD.

`0x2000 == 8192`, which is exactly unity in Q13 fixed-point representation.

Therefore `scratch_A` is now proven to start as a **Q13 per-pixel multiplicative correction/gain surface**, not as an arbitrary raw image copy.

Subsequent scalar and SIMD paths repeatedly combine WORD surfaces using the same fixed-point pattern. Scalar examples are of the form:

```text
product = a * b
result  = (product + 0x1000) >> 13
```

The SIMD implementation uses the equivalent family of operations:

```text
pmulld
paddd
psrad
```

with the same Q13 rounding/scaling behavior.

Later branches can combine more than two per-pixel surfaces before writing back to the Q13 plane. Consequently the conservative semantic label is:

```text
scratch_A = composed Q13 per-pixel correction/gain surface
```

This is stronger and more precise than the previous placeholder description "denominator/work plane".

### Secondary surface (`scratch_B`)

The parent descriptor maps another full-size allocated plane at descriptor offset `+0x30` (`scratch_B`). `0x180049ba0` carries a second Q13-style surface through related branches. Some paths copy the primary surface into the secondary surface; other paths build/compose it separately before the parent optionally uses it as the denominator for a caller-visible auxiliary output.

The exact type-0x0c branch conditions controlling when `scratch_B` is copied versus independently composed are not yet completely reconstructed, so do not assign it a stronger semantic name yet.

### Other proven `0x180049ba0` behaviors

The function also:

- allocates temporary full-image WORD storage;
- uses the cleaned mask in difference-selection logic;
- contains non-negative clipping via conditional zeroing after signed/WORD differences;
- contains special branches for other sensor selectors including `0x0b`, `0x04`, and others;
- calls specialized spatial/correction helpers including `0x18004b390`, `0x18004d6f0` / `0x18004d890`, `0x1800497c0`, `0x18004b460`, and `0x180049530` depending on selector/config state;
- uses SIMD integer arithmetic, but no floating-point arithmetic was observed in the captured body.

For tested selector `0x0c`, the branch after `0x1800429b0` selects `0x18004d6f0` rather than the `0x04/0x0b`-only `0x18004d890` path.

A later selector dispatch also excludes `0x0c` from bit mask `0x00412030`, then routes `0x0c` through the `selector-0x0b <= 1` branch beginning near `0x18004a727`. This is the immediate region to isolate next.

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

Combining this parent relation with the new `0x180049ba0` proof gives the tested path at a higher semantic level:

```text
scaled source plane
 -> build/combine Q13 per-pixel correction surface(s)
 -> fixed-point per-pixel division with rounding
 -> persistent corrected image-like plane at state+0x13244
```

The conservative description remains `fixed-point ratio/corrected plane`; calling it the final matcher-normalized fingerprint image still requires tracing later consumers.

## What remains unknown

Important open links are now narrower:

1. **Exact type-0x0c composition formula inside `0x180049ba0` after the Q13-unity initialization.** We know the representation and multiplication rule, but not yet which specific source surfaces feed every factor on the tested branch.
2. **Exact secondary `scratch_B` branch for type 0x0c.**
3. **How `state+0x9924` is populated.** It is statically proven to participate in this type-0x0c path, but it is not yet proven identical to gfusb.dll's persisted ImageBase.
4. **Downstream consumer of `state+0x13244`.** This must be tied to matcher-facing processing before calling it the final normalized fingerprint image.
5. Exact semantic meaning of the per-pixel gate plane beginning at `state+0x4`.

## Immediate next target

Do **not** reverse all 25 calls in `0x180049ba0`.

Isolate the tested selector-`0x0c` control-flow only, especially:

```text
0x18004a0c0 .. 0x18004aa94
```

with emphasis on the selector dispatch at:

```text
0x18004a0d1: cmp r8d,0x0c
```

and the later selector-0x0c route through:

```text
0x18004a405
 -> 0x18004a727
 -> ...
 -> 0x18004aa94
```

Goals:

1. map exactly which Q13 factor planes modify `scratch_A` for type `0x0c`;
2. map when `scratch_B` copies or diverges from `scratch_A`;
3. identify the direct role of `0x18004d6f0`, `0x1800497c0`, and `0x18004b460` on the tested branch;
4. only descend into a child helper once its output is proven to feed the Q13 denominator surface used by the parent;
5. then trace `state+0x13244` into its next consumer.

Separately, retain the independent task of proving the producer of `state+0x9924` before equating it with the gfusb persisted ImageBase.

## Safety boundary

Do not request, publish, commit, upload, or hash real fingerprint frames/templates, PSK/OTP, unit-specific full runtime configuration, `goodix.dat`, `goodix_calib.dat`, `Goodix_Cache.bin`, proprietary Goodix binaries, Windows biometric database material, or process dumps.
