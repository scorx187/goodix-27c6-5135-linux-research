# Chicago mode-9 Gaussian horizontal pass — `0x18004f5f0`

Date: 2026-08-28

## Scope

Static analysis of `AlgoChicago.dll` only. No fingerprint image, template, PSK, OTP, calibration payload, `goodix.dat`, or private biometric material was accessed.

## Caller path

For family/type `0x0c`, mode `9` is already proven to select:

```text
5-tap Gaussian kernel, sigma=1.5, Q16
coeff = [7869, 15328, 19142, 15328, 7869]
sum   = 65536
```

`0x18004e380` is the streaming/ring-buffer orchestrator. Because mode `9` sets `ctx+0x88 = 0`, its selected per-row helper is:

```text
0x18004f5f0
```

## Logical routine boundary

The PE unwind entry begins at `0x18004f5f0..0x18004f636`, but the logical routine continues through adjacent unwind regions and returns at:

```text
0x18004f78a
```

## Kernel descriptor

At entry:

```asm
mov r10,[rcx+0x90]
mov r11,[r10+0x20]
movsxd rbx,DWORD PTR [r10]
```

For mode `9` this means:

```text
kernel_count = 5
coeff_ptr    = mode-9 Q16 coefficient array
```

The coefficient literals do not appear inline because the routine consumes the context-owned descriptor produced earlier from the static mode-9 record.

## Tested scalar geometry

The fifth argument is loaded into `r13d` and used as the source-sample spacing multiplier:

```asm
movsxd r13,DWORD PTR [rsp+0x60]
...
mov rbp,r13
add rbp,rbp
```

For the tested mode-9 U16 path, `r13 = 1`, therefore one kernel step advances by:

```text
2 bytes = one U16 pixel
```

This proves the pass walks adjacent samples along one row.

## Exact multiply-accumulate

The main loop computes four adjacent outputs in parallel. For the first coefficient it loads four consecutive U16 samples and multiplies each by `coeff[0]`. For each later coefficient it advances the source by one U16 pixel and accumulates the corresponding coefficient.

Equivalent scalar form for output position `x`:

```text
acc(x) =
      src[x+0] * 7869
    + src[x+1] * 15328
    + src[x+2] * 19142
    + src[x+3] * 15328
    + src[x+4] * 7869

horizontal_q16(x) = acc(x) >> 16
```

The vectorized-by-hand four-output body at `0x18004f660..0x18004f6fc` and scalar tail at `0x18004f730..0x18004f77c` implement the same equation.

## Q16 conversion semantics

The routine performs:

```asm
shr ...,0x10
```

with no preceding `+0x8000`.

Therefore the Q16 conversion is truncation/floor for these nonnegative values, not round-to-nearest:

```text
out = floor(acc / 65536)
```

No saturation or clamp is visible in this helper.

## Intermediate storage

The helper stores each result as a DWORD into the context/output working buffer:

```asm
mov DWORD PTR [...],result
```

Thus this stage produces a 32-bit intermediate plane/row result even though the source samples are U16.

## Axis conclusion

Because the source advances by one adjacent U16 sample per coefficient and the helper is invoked as the per-row processor from `0x18004e380`, this is proven as the **horizontal** 1D five-tap Gaussian pass for mode `9`.

The helper itself does not implement border extension. `0x18004e380` prepares the rolling/halo input geometry, so exact border semantics remain owned by the orchestrator.

## Remaining question

The next decisive helper is the mode-9 flush/output path:

```text
0x18004fd20
```

Need to prove whether it performs the second, vertical Gaussian pass over the stored DWORD row intermediates or merely copies/converts them to output.
