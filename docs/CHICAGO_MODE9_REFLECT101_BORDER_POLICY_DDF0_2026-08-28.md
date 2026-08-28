# Chicago mode-9 Gaussian border policy — `0x18004ddf0`

Date: 2026-08-28

## Scope

Static analysis of `AlgoChicago.dll` only. No device I/O and no private biometric, PSK, OTP, calibration, or `goodix.dat` material was accessed.

## Context already proven

For the tested family-`0x0c` mode-9 Gaussian path, `0x180050090` constructs a context with:

```text
ctx+0x1c = 4
ctx+0x20 = 4
ctx+0x28 = allocated horizontal index-map buffer
ctx+0x90 = horizontal Gaussian descriptor
ctx+0x98 = vertical Gaussian descriptor
```

The mode-9 kernel is already proven to be the normalized Q16 five-tap Gaussian (`sigma=1.5`):

```text
[7869, 15328, 19142, 15328, 7869]
```

The horizontal helper `0x18004f5f0` and vertical helper `0x18004fd20` are already proven.

## `0x18004ddf0` horizontal index-map construction

`0x18004ddf0` stores the incoming packed geometry pointer at `ctx+0x34`, allocates several work buffers, and uses the buffer at `ctx+0x28` as a table of mapped horizontal source indices.

The decisive selector is read from `ctx+0x1c` at both horizontal out-of-range handling loops:

```asm
18004df90: mov r10d,DWORD PTR [rbx+0x1c]
...
18004dfa2: lea eax,[r10-0x2]
18004dfa6: test eax,0xfffffffd
18004dfab: jne 0x18004dfda
18004dfad: cmp r10d,0x4
18004dfb4: sete dl
```

and again:

```asm
18004e024: mov edi,DWORD PTR [rbx+0x1c]
...
18004e02f: lea eax,[rdi-0x2]
18004e032: test eax,0xfffffffd
18004e037: jne 0x18004e068
18004e039: cmp edi,0x4
18004e03f: sete dl
```

The `lea selector-2` / `test ...0xfffffffd` pair accepts selector values `2` and `4`. For selector `4`, `DL` becomes `1`.

For image extent `N = r9d` and an out-of-range coordinate `x = ecx`, the mapping loop is equivalent to:

```text
if N == 1:
    x = 0
else while x < 0 or x >= N:
    if x < 0:
        x = -x                  # selector 4
    else:
        x = 2*N - 2 - x        # selector 4
```

Therefore the immediate mappings are:

```text
-1   -> 1
-2   -> 2
N    -> N-2
N+1  -> N-3
```

This is `REFLECT_101` / reflection excluding the edge sample itself.

The mapped coordinates are converted to source offsets and written into `ctx+0x28`:

```asm
18004df60: mov r14,QWORD PTR [rbx+0x28]
...
18004dff0: mov DWORD PTR [rcx],eax
...
18004e094: mov DWORD PTR [r14+rax*4],edx
```

## Proven mode-9 border policy

Since the tested mode-9 context has:

```text
ctx+0x1c = 4
ctx+0x20 = 4
```

and:

- `0x18004ddf0` uses `ctx+0x1c` to build the horizontal source-index map with the selector-4 rule above;
- `0x18004e380` uses `ctx+0x20` for the corresponding stored-row/vertical border mapping and the same selector-4 mapping logic;

mode 9 uses `REFLECT_101` on **both axes**.

## Complete mode-9 Gaussian operation

Mode 9 is now closed at the pixel-operation level:

```text
kernel = [7869, 15328, 19142, 15328, 7869]  # Q16, sum 65536
sigma  = 1.5
border = REFLECT_101 on X and Y
```

Horizontal pass:

```text
H[y,x] = (
    7869  * src[y,x-2]
  + 15328 * src[y,x-1]
  + 19142 * src[y,x]
  + 15328 * src[y,x+1]
  + 7869  * src[y,x+2]
) >> 16
```

No `+0x8000` rounding bias is present. The intermediate is DWORD.

Vertical pass:

```text
out[y,x] = (
    7869  * H[y-2,x]
  + 15328 * H[y-1,x]
  + 19142 * H[y,x]
  + 15328 * H[y+1,x]
  + 7869  * H[y+2,x]
) >> 16
```

`0x18004f480` proves for this path:

```text
vertical_bias  = 0
vertical_shift = 16
```

Thus both Q16 stages use truncation after the 16-bit shift.

## Status

`PROVEN`: mode-9 Gaussian coefficients, separable 5x5 application, Q16 truncation, zero vertical bias, shift 16, and `REFLECT_101` border policy on both axes.

The next useful task is no longer inside this generic image-filter engine. Return to the Chicago preprocessing/Q13 path and trace the persistent corrected plane at `state+0x13244` toward its downstream feature/matcher/enrollment consumer.
