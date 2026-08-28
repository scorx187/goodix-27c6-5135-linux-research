# Chicago mode-9 static kernel proof — 2026-08-28

## Scope

Static reverse engineering of the installed Windows `AlgoChicago.dll` for the tested Goodix `27c6:5135` / family `0x0c` path. No private biometric data, PSK, OTP, calibration payloads, or proprietary binaries are committed here.

## Call path

The gated late-factor update in `0x18004b460` calls the generic wrapper:

```text
0x18004e110(temp_ratio_object, work_object, packed_dims, mode=9, -1, -1)
```

`0x18004e110` builds a context through `0x18004e820`. For the tested path, `0x18004e820` calls:

```text
0x18004fbf0(local_descriptor, mode=9, class=4)
```

## `0x18004fbf0` selector proof

Exact PE runtime function:

```text
RVA 0x0004fbf0 .. 0x0004fd1d
```

For `mode < 100`, it selects records from base RVA:

```text
0x000932b0
```

Each record is exactly `0x40` bytes:

```text
record = base + mode * 0x40
```

Therefore mode 9 selects:

```text
0x000932b0 + 9 * 0x40 = 0x000934f0
```

For `class=4`, the record is interpreted as:

```text
DWORD count
DWORD values[count]
```

and those DWORDs are copied into an owned descriptor payload.

## Exact mode-9 record

The 64-byte record at RVA `0x000934f0` begins:

```text
count = 5
values = [
    7869,
    15328,
    19142,
    15328,
    7869,
]
```

All remaining DWORDs in the record are zero.

Properties:

```text
sum(values) = 65536 = 2^16
palindromic = true
```

Therefore this is a normalized symmetric five-tap Q16 coefficient vector.

## Gaussian identification — PROVEN

For the discrete Gaussian sampled at offsets:

```text
x = [-2, -1, 0, 1, 2]
```

with:

```text
sigma = 1.5
```

using:

```text
w(x) = exp(-(x*x)/(2*sigma*sigma))
```

and normalizing the five weights to sum to 1, the normalized floating-point coefficients are approximately:

```text
[0.1200783842,
 0.2338807566,
 0.2920817183,
 0.2338807566,
 0.1200783842]
```

Multiplying by `65536` and rounding to nearest integer yields exactly:

```text
[7869, 15328, 19142, 15328, 7869]
```

with exact integer sum `65536`.

Thus the mode-9 class-4 descriptor contains a normalized five-tap Gaussian kernel with:

```text
sigma = 1.5
coefficient format = Q16
kernel length = 5
```

## What is not yet proven

The static coefficient vector is proven, but the execution orientation and boundary behavior are not yet closed. Specifically, do not yet claim whether `0x18004fff0` applies the five-tap Gaussian:

- horizontally only;
- vertically only;
- separably in both axes;
- or through another descriptor-driven arrangement.

That must be proven from the context built by `0x18004f480` and the executor path in `0x18004fff0`.

## Immediate next target

Trace the tested mode-9 context and execution path:

```text
0x18004f480
0x18004fff0
```

The next proof goal is the exact spatial application of the five-tap Gaussian coefficients and the rounding/border semantics that produce the filtered ratio plane consumed by `0x18004b460`.
