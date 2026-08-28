# Chicago ratio-stage wrapper `0x18004e110` — 2026-08-28

Target: `AlgoChicago.dll`, tested Goodix `27c6:5135`, selector/type `0x0c`.

This checkpoint is based on static analysis only. No proprietary binary, fingerprint image/template, PSK/OTP, calibration payload, unit-specific config, Goodix cache, or Windows biometric database material is committed.

## Exact runtime-function boundary

The PE x64 runtime table gives:

```text
RVA 0x0004e110 .. 0x0004e378
size 0x268 = 616 bytes
normal ret 0x18004e377
```

## Authoritative caller mapping from `0x18004b460`

```text
RCX = temporary u16 ratio image object
RDX = original work/image object
R8  = return value from 0x1800501a0
R9D = 9
stack arg5 = -1
stack arg6 = -1
```

The value in R8 is consumed as two signed 32-bit fields (`low32`, `high32`) rather than as a direct image pointer.

## `0x18004e110` is a wrapper/orchestrator, not the pixel filter

There are no image-pixel loops in this function. It validates the two image objects, checks their data pointers at `+0x18`, validates/adjusts dimensions, temporarily changes image-format metadata, constructs an operation context, dispatches the operation, restores object metadata, and frees the context and its owned allocations.

Therefore the earlier working hypothesis that `0x18004e110` itself was the spatial filter is rejected.

## Object validation / normalization

The function requires both objects to have non-null `+0x18` data pointers and an initial type/format field `+0x10 == 2`.

If dimensions differ it calls `0x180042a60` to reconcile the destination/work object.

During the generic processing path it temporarily writes:

```text
object1+0x10 = 0x42ff0002
object2+0x10 = 0x42ff0002
```

and recomputes each object's `+0x08` stride/byte-count-like field from width and encoded format bits. After dispatch it restores both `+0x10` fields to `2`.

These writes are metadata preparation, not the fingerprint correction equation itself.

## Context construction

The wrapper calls:

```text
0x18004e820(
    RCX = object1_format & 0x0fff,
    RDX = object2_format & 0x0fff,
    R8  = packed two-DWORD value received from 0x1800501a0,
    R9D = mode,
    stack arg5,
    stack arg6
)
```

On the tested caller path:

```text
mode = 9
arg5 = -1
arg6 = -1
```

The return value is an allocated context object `ctx`.

The wrapper verifies context fields at `ctx+0x10` and `ctx+0x0c` have the required odd/parity state before execution.

## Mode-9 flag behavior

Before execution:

```asm
lea eax,[mode-6]
test eax,0xfffffffd
sete al
mov [ctx+0x88],al
```

This makes `ctx+0x88 = 1` only when `mode` is 6 or 8.

For the tested call:

```text
mode = 9
=> ctx+0x88 = 0
```

Therefore constant `9` must not be described as a proven `9x9` window size. At this level it is an operation/mode selector forwarded into context construction.

## Actual execution boundary

The real operation is dispatched by:

```text
0x18004fff0(
    RCX = ctx,
    RDX = temporary ratio object,
    R8  = output/work image object
)
```

This is the first child proven to sit on the actual data-transform boundary after the context has been prepared.

After `0x18004fff0` returns, `0x18004e110` performs cleanup only: restores format fields and frees allocations owned by the context.

## What remains open

Two pieces are now separated cleanly:

1. `0x18004e820` — context builder / mode-9 configuration. Need to determine what operation mode 9 selects, and whether it installs coefficients, callbacks, kernel geometry, or other transform parameters.
2. `0x18004fff0` — execution engine receiving `ctx`, ratio object and output/work object. Need the exact data transform that reaches the later near-unity gate in `0x18004b460`.

Recommended order: inspect the small `0x18004e820` context builder first, then follow only the mode-9-selected execution path in `0x18004fff0`.

## Important arithmetic erratum carried forward

Independent earlier assembly proof for `0x180044970` establishes the tested selector-`0x0c` subtraction as:

```text
diff16[i] = state_plus_0x9924_u16[i] - source_u16[i]
```

The non-type-4 SIMD loop loads the state plane into `xmm1`, source into `xmm0`, then executes `psubw xmm1,xmm0`; the scalar tail performs the same order. Selector 4 adds `0x0fff` to this same `state - source` relation.

Any older checkpoint that states `source - state` is stale and must be corrected.

## Safety boundary

Do not request, publish, upload, commit, or hash real fingerprint frames/templates, PSK/OTP, full unit-specific runtime config, `goodix.dat`, `goodix_calib.dat`, `Goodix_Cache.bin`, proprietary Goodix binaries, Windows biometric DB material, or process dumps. Do not erase/flash firmware or rewrite/re-provision the factory PSK.