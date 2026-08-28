# Chicago mode-9 Gaussian final numeric semantics — 2026-08-28

## Scope

Static analysis of the Windows `AlgoChicago.dll` family-`0x0c` preprocessing path only. No device I/O and no private biometric or provisioning material was accessed.

## Proven mode-9 kernel

Mode `9` selects the class-4 static record at `0x1800934f0`:

```text
count = 5
coefficients = [7869, 15328, 19142, 15328, 7869]
sum = 65536
```

These are exactly the normalized discrete Gaussian samples for positions `[-2,-1,0,1,2]` with `sigma=1.5`, rounded to Q16.

## Horizontal pass — `0x18004f5f0`

The helper reads the kernel descriptor from `ctx+0x90` and performs a 5-tap U16 -> DWORD convolution along a row.

For the interior logical output centered at `x`:

```text
H[y,x] = (
      7869  * src[y,x-2]
    + 15328 * src[y,x-1]
    + 19142 * src[y,x]
    + 15328 * src[y,x+1]
    + 7869  * src[y,x+2]
) >> 16
```

There is no `+0x8000` before the shift. Therefore the Q16 conversion is truncation, not round-to-nearest.

The helper has an unrolled four-output main loop plus a scalar tail. Intermediate samples are stored as DWORDs.

## Vertical pass — `0x18004fd20`

The helper reads a second descriptor from `ctx+0x98`, consumes an array of pointers to horizontally filtered DWORD rows, and performs the second 5-tap pass.

With `count=5`, it sets `half=count>>1=2` and exploits kernel symmetry:

```text
V[y,x] = (
      H[y,x] * 19142
    + (H[y-1,x] + H[y+1,x]) * 15328
    + (H[y-2,x] + H[y+2,x]) * 7869
    + bias
) >> shift
```

Output is written as U16.

## Bias and shift — proven by `0x18004f480`

The context builder constructs the vertical descriptor and writes:

```asm
mov DWORD PTR [r10+0x2c],0x10
...
mov eax,DWORD PTR [rsp+0x98]
shl eax,0x10
mov DWORD PTR [r10+0x28],eax
```

For the proven mode-9 caller path, the sixth argument is zero. Therefore:

```text
vertical_descriptor+0x28 = bias  = 0
vertical_descriptor+0x2c = shift = 16
```

So the exact vertical conversion is also truncating Q16:

```text
V[y,x] = Gaussian_vertical_sum >> 16
```

There is no Q16 half-LSB rounding bias in either axis.

## Correct interpretation of the `-1` argument

The mode-9 wrapper passes a packed `-1` value into `0x18004f480`. This is not the Gaussian numeric bias. `0x18004f480` uses the two `-1` 32-bit halves as unspecified/default anchor coordinates and replaces them with centered kernel anchors. The actual numeric bias comes from the separate sixth argument, which is zero on the proven path.

## Proven conclusion

Mode `9` is a separable 5x5 Gaussian operation:

```text
U16 input
  -> horizontal 5-tap Gaussian sigma=1.5 Q16, >>16 truncation
  -> DWORD intermediate rows
  -> vertical 5-tap Gaussian sigma=1.5 Q16, bias=0, >>16 truncation
  -> U16 output
```

This closes the interior arithmetic and scaling semantics. Exact border-extension policy remains a separate context/orchestrator detail and should not be guessed until the context field controlling the `0x18004e380` border branch is proven.

## Safety

Static proprietary-binary disassembly only. No fingerprint images/raw frames/templates, PSK/OTP material, unit-specific configuration, or Windows biometric database material was accessed or recorded.
