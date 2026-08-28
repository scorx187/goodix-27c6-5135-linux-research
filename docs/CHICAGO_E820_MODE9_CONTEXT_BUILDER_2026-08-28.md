# Chicago mode-9 context builder `0x18004e820` — 2026-08-28

Target: `AlgoChicago.dll`, tested Goodix `27c6:5135`, selector/family `0x0c`.

This checkpoint contains static reverse-engineering conclusions only. No proprietary binary, fingerprint image/template, PSK/OTP, unit-specific runtime configuration, or calibration payload is committed.

## Upstream path

`0x18004b460` builds a temporary ratio object and calls:

```text
0x18004e110(temp_ratio_object, work_object, packed_value, mode=9, -1, -1)
```

`0x18004e110` was previously proven to be a wrapper/orchestrator rather than the pixel filter itself. It validates/reconciles object metadata, calls `0x18004e820` to construct an operation context, then dispatches the actual operation through `0x18004fff0`.

## Exact `0x18004e820` boundary

PE runtime-function table:

```text
RVA 0x0004e820 .. 0x0004e9b2
size 0x192 = 402 bytes
normal return 0x18004e9b1
```

## Tested mode-9 branch — PROVEN

For the tested call from `0x18004e110`, the two low-12-bit object format/type codes are both `2`, and `R9D = 9`.

At entry:

```text
ECX  = first object format/type code
EDX  = second object format/type code
R8   = packed two-DWORD value returned earlier by 0x1800501a0
R9D  = operation/mode selector = 9
```

`0x18004e820` saves the first code, masks its low three bits and enforces a minimum value of 4 for the helper-selection path. Since `mode=9` is non-negative, the tested branch is:

```text
local class = max((first_code & 7), 4)
0x18004fbf0(local_descriptor, mode=9, class=4)
```

The negative-mode branch instead uses `0x18004f340`; it is not the tested mode-9 path.

Therefore `0x18004fbf0` is the next decisive helper for determining what mode 9 selects.

## Descriptor cloning / owned auxiliary buffer — PROVEN

After `0x18004fbf0`, `0x18004e820` copies 32 bytes from the returned descriptor (`[rax]` and `[rax+0x10]`) into local descriptors.

It decodes dimensions / format-dependent element size from those fields, allocates an owned buffer with `0x1800667f4`, and copies descriptor-backed payload data into that allocation with `0x1800428f0`.

This proves that mode 9 selects or constructs a descriptor carrying an auxiliary data payload (for example coefficients/kernel-like data is possible, but that semantic name is not yet proven).

Do not call this payload a convolution kernel until `0x18004fbf0` / its returned descriptor is decoded.

## Final context construction — PROVEN

The builder then calls:

```text
0x18004f480(
    ECX = original first format/type code,
    EDX = original second format/type code,
    R8  = address of first local descriptor,
    R9  = address of second local descriptor,
    stack arg5 = -1,
    stack arg6 = 0
)
```

and returns the resulting context pointer.

Thus `0x18004e820` is best classified as:

```text
mode-specific descriptor selection / cloning
+ auxiliary payload ownership
+ final operation-context construction
```

It has no image-pixel loop and does not itself implement the ratio-plane transform.

## Important mode-9 conclusion

The constant `9` remains an operation/mode selector, not a proven `9x9` window size.

The direct tested path is:

```text
mode 9
  -> 0x18004fbf0(..., 9, 4)
  -> clone returned 32-byte descriptor + payload
  -> 0x18004f480(...)
  -> context
  -> 0x18004fff0(context, ratio_object, output/work_object)
```

## Immediate next target

Reverse exactly `0x18004fbf0` first.

Goals:

1. establish its exact PE function boundary;
2. prove how `mode=9` is dispatched;
3. identify the exact static/generated descriptor returned for mode 9;
4. decode descriptor fields sufficiently to determine payload dimensions/type and whether the payload is coefficients, a kernel, a lookup table, or another structure;
5. identify any callback/function-selection information that can let us skip generic `0x18004fff0` logic and jump directly to the selected executor.

Only after that should `0x18004f480` or `0x18004fff0` be reversed, and only along the proven mode-9 path.

## Safety boundary

Never request or publish fingerprint frames/templates, PSK/OTP, unit-specific full runtime configuration, proprietary Goodix binaries, Windows biometric database material, calibration payloads, `goodix.dat`, `goodix_calib.dat`, or process dumps.
