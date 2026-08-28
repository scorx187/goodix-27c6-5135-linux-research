# Chicago preprocessing core checkpoint — 2026-08-28

Target: `AlgoChicago.dll` for Goodix `27c6:5135`, logical chip `0x2504` / family `0x0c`.

This document records only static conclusions required to continue the reverse engineering. No proprietary binary or biometric/calibration payload is committed.

## Call chain reached

```text
EngineAdapter.dll
  -> AlgoChicago.dll preprocessor_wrapper (RVA 0x0000b560)
  -> logical outer routine 0x18000e780..0x18000e947
  -> core helper/orchestrator 0x1800484e0
  -> first image/state-plane combiner 0x180044970
  -> full mask cleanup/filter routine 0x180043c40..0x180043ff6
```

`0x1800484e0` is a preprocessing orchestrator with many allocations, scratch buffers and specialized helper calls. It is not one simple flat pixel loop.

## Input image geometry validation

The core helper decodes two image dimensions from the packed preprocessing configuration, multiplies them to obtain `pixel_count`, doubles that value, and compares it with the supplied input byte count.

Therefore its primary image input is a 16-bit plane with byte length:

```text
width * height * 2
```

For the proven 5135 downstream plane this is consistent with `80 * 64 * 2 = 10240` bytes.

The source image is copied to a temporary `2 * pixel_count` buffer before algorithm-specific processing.

## Internal preprocessing-state plane

`R9` of `0x1800484e0` is the large preprocessor state object. The helper later copies a second `2 * pixel_count` 16-bit plane from:

```text
state + 0x9924
```

into a temporary buffer.

When an internal mode value equals `2`, this copied state plane receives an interpolation pass which replaces selected intermediate WORDs with integer averages of neighbors.

This plane is therefore proven to be an internal preprocessing/calibration-state plane. It is **not yet proven identical to gfusb.dll's persisted ImageBase**, and documentation must not collapse those two concepts without a direct data-flow proof.

## `0x180044970` — exact first pixel-wise transform

The core helper calls:

```text
0x180044970(
    copied_source_u16,
    copied_state_plane_u16,
    output_structure,
    selector
)
```

with:

```text
RCX = copied source-image u16 plane
RDX = copied state+0x9924 u16 plane
R8  = temporary output/result structure
R9D = six-bit selector decoded from packed preprocessing configuration
```

The selector is the field obtained in `0x1800484e0` by shifting the packed configuration right by 3 and masking with `0x3f`. The active algorithm repeatedly treats this field as a sensor/algorithm type selector. For the tested device the relevant value is type/family `0x0c`.

### Type 0x0c active subtraction path — PROVEN

At `0x180044ad8`, `0x180044970` checks whether the selector equals `4`.

The tested `27c6:5135` path uses selector `0x0c`, therefore execution takes the `!= 4` branch beginning at `0x180044bb1`.

The vectorized loop is:

```asm
movdqu xmm1, [source + index*2]
movdqu xmm0, [state_plane + index*2]
psubw  xmm1, xmm0
movdqu [output + index*2], xmm1
```

The scalar tail performs the same operation:

```asm
movzx eax, WORD PTR [source]
sub   ax, WORD PTR [state_plane]
mov   WORD PTR [output], ax
```

Therefore the first exact per-pixel relation for the tested Chicago/type-0x0c path is:

```text
diff16[i] = source_u16[i] - state_plane_u16[i]    (16-bit subtraction)
```

No clamp, saturation or gain operation occurs in this subtraction loop.

The SIMD instruction is `psubw`, so arithmetic wraps at 16 bits. Later in the same function the produced WORD values are loaded with `movsx`, proving that downstream code interprets this difference plane as **signed 16-bit values**. In other words, a negative physical difference is intentionally represented in two's-complement 16-bit form and then sign-extended later.

### Selector 4 special path — not the tested 5135 path

When selector `== 4`, the function instead computes the equivalent 16-bit form of:

```text
source - state_plane + 0x0fff
```

using `psubw` followed by `paddw` with a constant vector and the equivalent scalar tail.

That offset path must **not** be applied to the tested 5135/type-0x0c implementation.

## What follows the subtraction in `0x180044970`

Static control flow proves that it then:

- computes local block/window statistics over the signed difference plane;
- uses repeated 16-sample neighborhoods and integer averages;
- derives minimum/maximum and threshold-like values;
- partitions pixels relative to a dynamically selected threshold;
- stores a threshold/status value in the result structure;
- creates/updates a byte mask in the result structure;
- computes a percentage-like field as `count * 100 / pixel_count` into result offset `+0x0e`;
- returns success (`0`) after freeing its temporary difference buffer.

For selector/type `0x0c`, later threshold-selection logic contains explicit type-12 branches, confirming that this function is active for the tested device.

Exact semantic names for every threshold, mask, and status field remain open until their consumers are traced.

## Important distinction: internal calibration plane vs gfusb ImageBase

The subtraction direction is now proven, but the identity of the second operand must be stated accurately:

```text
source image - AlgoChicago internal state plane at state+0x9924
```

It is not yet statically proven that `state+0x9924` is the exact same persisted ImageBase plane previously proven in `gfusb.dll`.

The data-flow task remains to determine how this internal state plane is populated from calibration/base inputs and whether it is derived from, copied from, or independent of gfusb's ImageBase.

## `0x180043c40..0x180043ff6` — complete mask cleanup / geometry / source-range filter

The compiler splits the logical routine into adjacent x64 unwind records:

```text
0x180043c40 .. 0x180043d84
0x180043d84 .. 0x180043d94
0x180043d94 .. 0x180043f49
0x180043f49 .. 0x180043ff6
```

Normal execution returns at `0x180043ff5`. The adjacent `0x180043ff6` and `0x180043ffc` targets call a failure helper and end in `int3`; they are bounds-check failure stubs rather than normal processing stages.

### Argument use proven

At the entry stack layout, argument 6 is loaded into `r14`. At the known call site this is the copied result structure from `0x180044970`.

The function reads:

```text
[result + 0x04] = first dimension
[result + 0x08] = second dimension
[result + 0x10] = byte mask start
[result + 0x00] = active-mask count, recomputed here
[result + 0x0e] = active-mask percentage, recomputed here
```

The original first argument (`RCX`) is saved and later used as a 16-bit source-image plane. The second image/state-plane argument is not consumed anywhere in this routine. Thus this function is **not** another source-minus-calibration transform.

### Per-axis mask aggregation

The first two passes zero two local 256-WORD arrays and accumulate the `result+0x10` byte mask along the two orthogonal axes.

Conceptually:

```text
axisA_sum[a] = sum(mask[a,b])
axisB_sum[b] = sum(mask[a,b])
```

The exact physical row/column naming is intentionally deferred until the local dimension fields are tied to physical orientation at this layer.

The explicit check that `2 * axis_index < 0x200` matches 256-entry WORD arrays.

### Edge-aware mask fill / cleanup

The next nested loop scans every mask position. For a zero-valued mask cell it searches along the four cardinal directions for the nearest non-zero mask support. It then conditionally changes selected zero cells to `1` using edge-distance tests involving constants `2` and `dimension-3`.

This is best described as an **edge-aware mask hole-fill / cleanup pass**. It is not a crop operation and does not rewrite source-image pixels.

Every active mask byte contributes to `result+0x00`, which is rebuilt from zero during this pass.

### Type-0x0c source-value validity filter — PROVEN

After mask cleanup, the routine loads its fifth argument (the selector/type) and selects a pair of raw-source thresholds.

Default thresholds are:

```text
low  = 50
high = 4050
```

For selector values whose bit is set in constant mask `0x02473800`, thresholds become:

```text
low  = 100
high = 3800
```

The set includes selector `0x0c` (12), so **the tested 5135 path uses 100 and 3800**.

The final source-image scan performs:

```text
if source_u16[i] >= 3800 or source_u16[i] <= 100:
    mask[i] = 0
else:
    leave mask[i] unchanged
```

If a cleared position was previously active, `result+0x00` is decremented.

Therefore a type-0x0c pixel can remain in the cleaned mask only when:

```text
100 < source_u16[i] < 3800
```

This comparison is on the original copied source `u16` plane, not on the signed `diff16` plane and not on the internal calibration plane.

### Coverage percentage recomputation

At the end:

```text
result+0x0e = result+0x00 * 100 / (dimensionA * dimensionB)
```

stored as a WORD.

Thus `+0x0e` is proven to be a percentage-like active-mask coverage field after morphology and source-range filtering.

### Semantic consequence

`0x180043c40` is now best classified as:

```text
mask geometry cleanup
+ source-range validity filtering
+ coverage recomputation
```

It does **not** perform image normalization, gain, baseline subtraction, or cropping of the image buffer itself.

## Immediate next target

Return to `0x1800484e0` and identify the **next active helper call after `0x180043c40` for selector/type `0x0c`**.

Do not guess from the remaining helper list. Extract the call-site order and branch conditions around the `0x180043c40` call, then select the next helper that is definitely on the tested type-0x0c path.

For that next helper, determine whether it:

1. consumes the signed difference plane or source image;
2. consumes the cleaned mask;
3. performs normalization/gain/clamp;
4. creates a cropped/grown image;
5. writes the actual processed-image buffer returned to the outer preprocessor.

Separately, continue tracing how `state+0x9924` is initialized before calling it equivalent to gfusb ImageBase.

## Additional orchestration facts already proven

- two internal state arrays at offsets `+0x1cb64` and `+0x217f4` are zeroed for `pixel_count` bytes before later stages;
- temporary structures/buffers include sizes `0x4ca0`, `0x4fc4`, and `2 * pixel_count`;
- later stages call helpers including `0x18004aea0`, `0x1800435a0`, `0x180046930`, `0x1800441c0`, `0x1800547c0`, and `0x180010720`;
- allocation failure returns `0x80000004`;
- unexplained status values such as `0x7531`, `0x7532`, `0xc351`, and EngineAdapter's special `0x84` remain intentionally unnamed until their semantics are proven.

## Safety / publication boundary

Do not publish or request real fingerprint frames, templates, PSK/OTP, unit-specific full runtime config, `goodix.dat`, `goodix_calib.dat`, `Goodix_Cache.bin`, proprietary Goodix binaries, Windows biometric database material, or process dumps.
