# Chicago 0x1800435a0 Local Contrast Normalization

Date: 2026-08-28

## Scope

Static analysis of `AlgoChicago.dll` for the Goodix Chicago family (family/type `0x0c`) only. No fingerprint image, template, PSK, OTP, calibration payload, `goodix.dat`, or unit-specific biometric/private data was accessed.

## Proven caller mapping from 0x1800484e0

Immediately after `0x18004aea0` produces the persistent corrected U16 plane, the core calls:

```text
0x1800435a0(
    RCX  = state + 0x13244,   // corrected U16 plane
    RDX  = state + 0x1cb64,   // byte-plane destination
    R8   = caller geometry/config structure,
    R9   = caller side/context pointer,
    arg5 = optional diagnostics/statistics structure
)
```

## Function boundary

```text
RVA 0x000435a0 .. 0x000439ca
size 0x42a (1066 bytes)
normal ret 0x1800439c9
```

## High-level role

`0x1800435a0` is still preprocessing. It converts the corrected U16 plane at `state+0x13244` into an 8-bit locally normalized/contrast-expanded plane at `state+0x1cb64`.

It is **not yet feature extraction**.

The function allocates four temporary U16 planes, invokes several image helpers to build local lower/upper envelopes, combines those envelopes, and then maps each valid source pixel into an 8-bit output intensity.

## Temporary planes

Four temporary buffers are allocated, each with byte length `2 * pixel_count` and zeroed. They are passed through:

- `0x180046130`
- `0x180047d80`
- `0x1800455e0`
- `0x1800464d0`

The exact semantic names of the individual helpers remain open, but the parent-level behavior is clear from the later merge and normalization loops.

## Envelope merge

After the helper calls, a per-pixel loop combines two pairs of U16 surfaces:

```text
surface_A[i] = max(surface_A[i], surface_D[i])
surface_D[i] = min(surface_B[i], surface_C[i])
```

This produces a high/low local range pair that is subsequently used for intensity normalization.

## Per-pixel normalization

For pixels enabled by the caller-provided mask/context byte plane (`caller_R9 + 0x10`), the function reads:

- local high U16 value,
- local low U16 value,
- source/intermediate U16 value from the large local work object.

If `high == low`, the normalized ratio is forced to `255`.

Otherwise:

```text
ratio = ((source - low) * 255) / (high - low)
ratio = clamp(ratio, 0, 255)
out8  = 255 - ratio
```

The resulting byte is written to the original `RDX` destination, which the caller maps to:

```text
state + 0x1cb64
```

Thus `state+0x1cb64` is a normalized 8-bit image/plane produced from the corrected U16 data at `state+0x13244`.

## Low-dynamic-range diagnostics

For valid pixels, the function also examines the local dynamic range:

```text
range = high - low
```

A per-pixel threshold is selected from the type/config selector:

```text
default threshold = 50
selected types     = 150
```

When an optional diagnostics structure is provided, pixels with `range < threshold` are counted and marked in its byte map.

The function stores the low-range count and flags the diagnostics structure if that count exceeds approximately one-fifth of the total pixel count.

## Histogram

When diagnostics are enabled, the function also builds a histogram of the local dynamic range for valid pixels. The range value is clamped to `0..199` and increments a 200-bin DWORD histogram beginning at diagnostics offset `+0x4c98`.

Observed metadata initialization includes:

```text
+0x4fb8 = 100
+0x4fbc = 100
+0x4fc0 = pixel_count
```

Exact public-facing semantic names for these diagnostics fields remain open.

## Current pipeline checkpoint

The proven preprocessing flow now includes:

```text
corrected U16 plane
state + 0x13244
        ↓
0x1800435a0
local envelope / local contrast normalization
        ↓
normalized 8-bit plane
state + 0x1cb64
```

The next decisive task is to trace the immediate downstream consumer(s) of `state+0x1cb64`, preferably in the continuing `0x1800484e0` path, to determine whether another preprocessing stage remains or whether the pipeline transitions toward feature extraction.
