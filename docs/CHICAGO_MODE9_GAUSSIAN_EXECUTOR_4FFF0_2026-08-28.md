# Chicago mode-9 Gaussian executor handoff — 2026-08-28

## Scope
Static analysis of `AlgoChicago.dll` only. No device writes, no fingerprint image/template, no PSK/OTP/calibration payload, and no private biometric data.

## Proven upstream mode-9 descriptor

`0x18004fbf0` receives mode `9` and class `4`, selects static record:

```text
base  = 0x1800932b0
index = 9
stride = 0x40
record = 0x1800934f0
```

For class `4`, the first DWORD is the element count and the following DWORDs are copied as the coefficient payload.

Mode-9 record:

```text
count = 5
coefficients = [7869, 15328, 19142, 15328, 7869]
sum = 65536 = 2^16
```

These are exactly the normalized discrete Gaussian coefficients for sample positions `[-2,-1,0,1,2]` at `sigma = 1.5`, rounded to Q16:

```text
[7869, 15328, 19142, 15328, 7869]
```

Therefore mode `9` is PROVEN to select a normalized 5-tap 1D Gaussian kernel, `sigma=1.5`, Q16 coefficients. Do not yet claim 5x5 or horizontal+vertical application until the execution routine proves orientation/passes.

## `0x18004fff0` role — PROVEN dispatcher/geometry wrapper

Exact runtime function:

```text
RVA 0x0004fff0 .. 0x00050081
normal ret 0x180050080
```

Authoritative entry mapping from `0x18004e110`:

```text
RCX = built mode-9 context
RDX = temporary ratio image object
R8  = output/work image object
```

The function does not contain a pixel loop and does not directly apply the five coefficients.

It first reads the input image geometry:

```text
input.width_or_dim0  = [RDX+0x00]
input.height_or_dim1 = [RDX+0x04]
```

and calls `0x1800501a0` to pack/normalize that pair.

Then:

```text
0x18004ddf0(ctx, packed_dims, &local_pair, -1) -> start_index
```

The returned integer is multiplied by `[input+0x08]` and used as an offset into `input.data`:

```text
input_start = input.data + input[0x08] * start_index
```

The final processing call is:

```text
0x18004e380(
    ctx,
    input_start,
    input[0x08],
    ctx[0x74] - ctx[0x6c],
    output.data,
    output[0x08]
)
```

Thus `0x18004fff0` is a geometry/dispatch wrapper. `0x18004ddf0` computes the starting region/index. `0x18004e380` is the first confirmed routine receiving raw input/output pointers, input/output row-byte/stride-like values, and the active row/span count.

## Immediate next task

Reverse exact function:

```text
AlgoChicago.dll 0x18004e380
```

Need to prove:

1. whether the mode-9 five-tap Gaussian is applied horizontally, vertically, or in multiple passes;
2. the exact integer accumulation and Q16 rounding rule;
3. border handling;
4. whether `ctx` provides one or two coefficient descriptors;
5. whether another child helper performs a second separable pass.

Do not descend into `0x18004ddf0` unless exact border/start-index semantics become necessary. It is not currently the decisive pixel transform.
