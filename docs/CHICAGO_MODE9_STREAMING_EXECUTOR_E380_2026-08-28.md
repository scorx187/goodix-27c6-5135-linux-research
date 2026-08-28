# Chicago mode-9 streaming executor — `0x18004e380`

Date: 2026-08-28

## Status

**PROVEN at orchestration level** for the tested family/type `0x0c` path.

The logical routine begins at `0x18004e380` and continues across multiple PE runtime-function/unwind regions to the real return at `0x18004e817`. Treating the first unwind region (`0x18004e380..0x18004e46c`) as the whole function is incorrect.

## Authoritative caller mapping

From `0x18004fff0`:

```text
RCX  = mode-9 context
RDX  = raw input pointer at the computed start position
R8D  = input row-byte/stride-like value
R9D  = active span/row count
arg5 = output.data
arg6 = output row-byte/stride-like value
```

Mode `9` already has a proven static 1D Gaussian kernel:

```text
count = 5
coefficients = [7869, 15328, 19142, 15328, 7869]
sum = 65536
sigma = 1.5
representation = normalized Q16
```

## What `0x18004e380` does

The routine is a streaming/ring-buffer executor around the actual pixel helper(s), not the coefficient MAC loop itself.

It:

1. reads geometry, format and persistent streaming state from the context;
2. adjusts the raw input start pointer and active region;
3. copies incoming row/span data into context-owned temporary/ring storage using `0x1800428f0` and explicit gather/copy loops;
4. updates rolling indices including context fields at `+0x6c`, `+0x78` and `+0x7c`;
5. constructs row pointers from the context-owned storage at `ctx+0x60` using stride-like field `ctx+0x68`;
6. dispatches the per-row processing helper;
7. flushes remaining rows to the caller-provided output buffer through a separate output helper.

No direct use of the known Gaussian constants appears in this parent routine, and there is no direct Q16 multiply-accumulate loop in `0x18004e380` itself.

## Mode-9 decisive branch

The context flag at `ctx+0x88` was already proven to be `0` for mode `9` (it is `1` only for modes `6` or `8`).

Therefore this branch is exact for mode `9`:

```asm
cmp byte ptr [rbx+0x88],0
jne 0x18004e696
call 0x18004f5f0
```

So the mode-9 per-row processing helper is:

```text
0x18004f5f0   PROVEN selected for mode 9
```

and the alternate helper:

```text
0x18004f790   NOT selected for mode 9
```

The same flag controls the flush/output path:

```asm
cmp byte ptr [rbx+0x88],0
jne 0x18004e7b6
call 0x18004fd20
```

Thus:

```text
0x18004fd20   PROVEN selected mode-9 flush/output helper
0x18004f940   NOT selected for mode 9
```

## Relevant child calls

```text
0x1800428f0  input/span copy into context-owned working storage
0x18004f5f0  selected mode-9 row-processing helper
0x18004f790  alternate path only when ctx+0x88 != 0
0x18004fd20  selected mode-9 flush/output helper
0x18004f940  alternate flush path only when ctx+0x88 != 0
```

## Important non-claims

Do **not** yet claim from this routine alone that the Gaussian is:

- horizontal-only;
- vertical-only;
- horizontal then vertical;
- a 5x5 2D kernel;
- using any particular Q16 rounding constant in the actual MAC loop.

Those semantics belong to `0x18004f5f0` and, if needed, `0x18004fd20`.

## Immediate next target

Reverse exact routine:

```text
AlgoChicago.dll 0x18004f5f0
```

Need to prove:

1. whether it consumes the five coefficients directly;
2. the exact source-row/pixel neighborhood;
3. accumulation width and Q16 rounding;
4. whether it performs one axis of a separable Gaussian;
5. what it stores back into the ring/context buffer;
6. whether `0x18004fd20` performs the second axis or only flush/copy logic.

Only inspect `0x18004fd20` after `0x18004f5f0` unless the latter proves it delegates arithmetic there.

## Safety

This checkpoint was derived from static `AlgoChicago.dll` disassembly only. No fingerprint image, template, PSK, OTP, calibration payload, `goodix.dat`, private biometric data, or unit-specific runtime configuration was accessed or published.
