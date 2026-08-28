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
  -> mask/statistics consumer beginning 0x180043c40
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

## What follows the subtraction

`0x180044970` does considerably more than the initial subtraction. Static control flow proves that it then:

- computes local block/window statistics over the signed difference plane;
- uses repeated 16-sample neighborhoods and integer averages;
- derives minimum/maximum and threshold-like values;
- partitions pixels relative to a dynamically selected threshold;
- stores a threshold/status value in the result structure;
- creates/updates a byte mask in the result structure;
- computes a percentage-like field as `count * 100 / pixel_count` into result offset `+0x0e`;
- returns success (`0`) after freeing its temporary difference buffer.

For selector/type `0x0c`, later threshold-selection logic contains explicit type-12 branches, confirming that this function is not a generic unused helper for the tested device.

Exact semantic names for every threshold, mask, and status field remain open until their consumers are traced.

## Important distinction: internal calibration plane vs gfusb ImageBase

The subtraction direction is now proven, but the identity of the second operand must be stated accurately:

```text
source image - AlgoChicago internal state plane at state+0x9924
```

It is not yet statically proven that `state+0x9924` is the exact same persisted ImageBase plane previously proven in `gfusb.dll`.

The next data-flow task is to determine how this internal state plane is populated from calibration/base inputs and whether it is derived from, copied from, or independent of gfusb's ImageBase.

## `0x180043c40` first unwind segment — mask row/column aggregation

`0x180043c40` is called after `0x180044970`. The first x64 `RUNTIME_FUNCTION` entry is only:

```text
0x180043c40 .. 0x180043d84
```

but this is **not the complete logical routine**. There is no `ret` before `0x180043d84`, so execution falls into later adjacent compiler-split unwind regions. Bounds-check branches also leave this first region. The full logical end must therefore be established before assigning the entire routine a semantic name.

The sixth Windows-x64 argument is loaded from the entry stack slot into `r14`. At the proven call site this argument is the copied result structure produced by `0x180044970`.

The first segment reads:

```text
[result + 0x04] -> first dimension
[result + 0x08] -> second dimension
[result + 0x10 + index] -> byte mask
```

It zeroes two local arrays of 256 WORD entries and then performs two nested aggregation passes.

### First pass

For each index of the first dimension, it sums every mask byte across the second dimension and stores the total in a local WORD array.

Equivalent shape:

```text
row_sum[y] = sum(mask[y * width + x])
```

where `row_sum` is a descriptive name only; exact row/column orientation is intentionally deferred until the dimension fields are tied to the 80x64 geometry at this layer.

### Second pass

It then accumulates the same byte mask by the other axis into the second local WORD array:

```text
column_sum[x] += mask[y * width + x]
```

Again, `row_sum`/`column_sum` describe the two orthogonal aggregation axes, not yet a proven physical orientation.

The explicit bound check compares `2 * axis_index` against `0x200`, consistent with the local arrays containing at most 256 16-bit counters.

### Consequence

This first `0x180043c40` segment does **not** perform the next source/calibration pixel transform. In the observed segment it consumes the byte mask/statistics object from `0x180044970` and builds per-axis mask counts. The primary source image (`RCX` at function entry) is saved but not consumed in this first segment; the other image/calibration arguments likewise have not yet been reached in the visible region.

This strongly places the beginning of `0x180043c40` in a mask-geometry / coverage-analysis role rather than as the first normalization loop.

## Immediate next target

Do not stop at the first unwind record of `0x180043c40`.

Expand the adjacent x64 runtime-function regions starting at `0x180043c40` until the real logical `ret`, and trace:

1. how the two per-axis mask-sum arrays are consumed;
2. whether the routine derives crop bounds / finger bounding box / coverage geometry;
3. where saved source-image and state-plane arguments are first consumed;
4. whether any later segment performs normalization, gain, clamp, crop, or pixel rewriting;
5. exact output/result fields written by the full routine;
6. whether bounds-check branches around `0x180043ff6` / `0x180043ffc` are compiler failure stubs or part of normal flow.

Only after the full logical boundary is proven should the routine receive a stronger semantic name.

## Additional orchestration facts already proven

- two internal state arrays at offsets `+0x1cb64` and `+0x217f4` are zeroed for `pixel_count` bytes before later stages;
- temporary structures/buffers include sizes `0x4ca0`, `0x4fc4`, and `2 * pixel_count`;
- later stages call helpers including `0x18004aea0`, `0x1800435a0`, `0x180046930`, `0x1800441c0`, `0x1800547c0`, and `0x180010720`;
- allocation failure returns `0x80000004`;
- unexplained status values such as `0x7531`, `0x7532`, `0xc351`, and EngineAdapter's special `0x84` remain intentionally unnamed until their semantics are proven.

## Safety / publication boundary

Do not publish or request real fingerprint frames, templates, PSK/OTP, unit-specific full runtime config, `goodix.dat`, `goodix_calib.dat`, `Goodix_Cache.bin`, proprietary Goodix binaries, Windows biometric database material, or process dumps.
