# Chicago preprocessing core checkpoint — 2026-08-28

Target: `AlgoChicago.dll` for Goodix `27c6:5135`, logical chip `0x2504` / family `0x0c`.

This document records only static conclusions required to continue the reverse engineering. No proprietary binary or biometric/calibration payload is committed.

## Call chain reached

```text
EngineAdapter.dll
  -> AlgoChicago.dll preprocessor_wrapper (RVA 0x0000b560)
  -> logical outer routine 0x18000e780..0x18000e947
  -> core helper 0x1800484e0
```

The core helper is a single x64 runtime-function region:

```text
0x1800484e0 .. 0x18004902b
size 0xb4b / 2891 bytes
```

It is a preprocessing orchestrator with many allocations, scratch buffers and specialized helper calls; it is not one simple flat pixel loop.

## Input image geometry validation

The helper decodes fields from the packed preprocessing descriptor, obtains two geometry dimensions, multiplies them to obtain `pixel_count`, doubles that value, and compares the result with the input byte count.

Therefore the input passed as `RDX` is treated as a 16-bit image plane whose byte length must be:

```text
width * height * 2
```

For the proven 5135 downstream plane this is consistent with `80 * 64 * 2 = 10240` bytes.

The helper allocates a temporary `2 * pixel_count` buffer and copies the input plane into it before the algorithm-specific processing stages.

## Algorithm state / calibration-plane handling

`R9` points to the large global preprocessing state object passed down by the outer routine.

The helper copies a large state block beginning at `state+4`, then later copies a `2 * pixel_count` 16-bit plane from:

```text
state + 0x9924
```

into a temporary buffer.

When an internal mode value equals `2`, the helper performs an in-place interpolation pass over this temporary 16-bit plane. The loop reads neighboring `WORD` values and replaces intermediate values with integer averages. This operation is currently best described as **state/calibration-plane interpolation**; do not call it live-image normalization or baseline subtraction without further proof.

## First algorithm-specific image/calibration call

After the input image copy and the `state+0x9924` plane copy, one active branch calls:

```text
0x180044970(
    input_u16_temp,
    state_plane_u16_temp,
    output_struct_0x4ca0,
    six_bit_selector
)
```

The call-site registers are:

```text
RCX = copied input 16-bit image plane
RDX = copied state+0x9924 16-bit plane
R8  = temporary output structure/buffer (0x4ca0 bytes)
R9D = six-bit selector decoded from packed preprocessing configuration
```

Immediately after this call the `0x4ca0` result is copied to another temporary structure, and a second helper is invoked:

```text
0x180043c40(
    input_u16_temp,
    state_plane_u16_temp,
    mode/flag,
    another flag,
    six_bit_selector,
    copied_0x4ca0_result
)
```

Other selector/family branches use `0x1800447d0` or `0x180044010` instead. Do not assume all of those paths apply to this tested device until the selector value is tied statically to the `0x2504`/family-`0x0c` initialization path.

## Additional proven orchestration facts

- two internal state arrays at offsets `+0x1cb64` and `+0x217f4` are zeroed for `pixel_count` bytes before later stages;
- the helper allocates several temporary image/result structures, including buffers of sizes `0x4ca0`, `0x4fc4`, and `2 * pixel_count`;
- later stages call additional helpers (`0x18004aea0`, `0x1800435a0`, `0x180046930`, `0x1800441c0`, `0x1800547c0`, `0x180010720`, etc.) and finally copy processed data into the temporary result object returned to the outer routine;
- allocation failure returns `0x80000004`;
- unexplained status values such as `0x7531`, `0x7532`, `0xc351`, and EngineAdapter's special `0x84` remain intentionally unnamed until their producers/diagnostics are proven.

## Immediate next target

The cleanest next reverse-engineering target is:

```text
AlgoChicago.dll 0x180044970
```

because its call site supplies exactly the pair we need to understand:

```text
copied input u16 image
+
copied state/calibration u16 plane
```

and a dedicated output structure.

Trace `0x180044970` first. Determine whether it performs the first exact per-pixel relation between those two planes. If it does, prove:

1. subtraction direction or other combination;
2. signed/unsigned behavior;
3. clamp/saturation;
4. scaling/gain;
5. output type/geometry;
6. semantic role of the six-bit selector.

Then follow `0x180043c40` for the next active stage.

## Safety / publication boundary

Do not publish or request real fingerprint frames, templates, PSK/OTP, unit-specific full runtime config, `goodix.dat`, `goodix_calib.dat`, `Goodix_Cache.bin`, proprietary Goodix binaries, or process dumps.
