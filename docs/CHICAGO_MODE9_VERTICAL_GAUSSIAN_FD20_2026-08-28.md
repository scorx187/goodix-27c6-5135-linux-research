# Chicago mode-9 vertical Gaussian pass — `0x18004fd20`

## Status

`PROVEN` from static disassembly of the tested `AlgoChicago.dll`.

This checkpoint covers the `ctx+0x88 == 0` mode-9 path reached from the previously proven streaming orchestrator `0x18004e380` after the horizontal helper `0x18004f5f0`.

## Logical boundary

The logical routine begins at:

```text
0x18004fd20
```

and continues across unwind regions to the real return at:

```text
0x18004ffea
```

## Context descriptor

At entry:

```asm
mov r10, [rcx+0x98]
mov eax, [r10]
sar eax, 1
mov rax, [r10+0x20]
lea rdi, [rax+rbx*4]
```

Therefore this helper uses a second kernel descriptor at `ctx+0x98`.

For the proven mode-9 kernel, the descriptor has five taps, so:

```text
count = 5
half = count >> 1 = 2
```

`rdi` points at the center coefficient and subsequent positive-side coefficients.

The same mode-9 kernel selected earlier is:

```text
[7869, 15328, 19142, 15328, 7869]
```

which is the exact normalized Q16 five-tap Gaussian for `sigma=1.5`.

## Input organization

`RDX` is an array of pointers to horizontally filtered intermediate rows. `0x18004fd20` centers that row-pointer array using:

```asm
lea r11, [rdx+rbx*8]
```

With `rbx=2`, `r11` points at the center row pointer, while neighboring pointers are accessed symmetrically on both sides.

The horizontally filtered intermediate pixels are DWORD values produced by `0x18004f5f0`.

## Exact symmetric vertical accumulation

For each output pixel, the helper initializes from the center row using the center coefficient, then adds symmetric row pairs with the matching outer coefficients.

The core structure is:

```text
acc = center_row[x] * coeff_center + bias

for k = 1..half:
    acc += (row[-k][x] + row[+k][x]) * coeff[center+k]
```

For mode 9 this is equivalent to:

```text
acc =
    row[y-2][x] * 7869
  + row[y-1][x] * 15328
  + row[y][x]   * 19142
  + row[y+1][x] * 15328
  + row[y+2][x] * 7869
  + bias
```

The optimized main loop computes four adjacent output pixels in parallel using scalar integer registers. A tail loop handles remaining pixels one at a time.

## Output conversion

The helper loads:

```text
descriptor+0x28 -> additive bias
descriptor+0x2c -> right-shift count
```

and stores final outputs as `WORD` values after the shift:

```asm
shr accumulator, cl
mov WORD PTR [...], accumulator_low16
```

The exact mode-9 values of `descriptor+0x28` and `descriptor+0x2c` are not yet independently proven in this checkpoint. Therefore do not yet label the vertical conversion as truncation or round-to-nearest until those two derived descriptor fields are traced.

## Proven separable structure

Combining this helper with the already proven horizontal helper `0x18004f5f0` proves that mode 9 applies the same five-tap Gaussian in two separable passes:

```text
U16 input plane
  -> horizontal 5-tap Gaussian sigma=1.5, Q16
  -> DWORD intermediate rows
  -> vertical 5-tap Gaussian sigma=1.5, Q16
  -> U16 output plane
```

Thus mode 9 is a separable 5x5 Gaussian operation, not a monolithic 25-coefficient convolution.

The effective 2D kernel is the outer product of the proven 1D vector with itself.

## Remaining exact detail

Before closing the Gaussian stage numerically, prove how the `ctx+0x98` descriptor builder sets:

```text
+0x28 additive bias
+0x2c right-shift count
```

The best next static target is the context/descriptor construction path around `0x18004f480`, because `0x18004fd20` consumes those fields but does not initialize them.

## Safety

Static analysis only. No device I/O, fingerprint image, biometric template, PSK, OTP, calibration payload, `goodix.dat`, or unit-specific runtime configuration was accessed.
