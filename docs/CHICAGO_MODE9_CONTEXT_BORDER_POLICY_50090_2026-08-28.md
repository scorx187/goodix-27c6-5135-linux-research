# Chicago mode-9 context / border policy (`0x180050090`) — 2026-08-28

## Scope

Static analysis of `AlgoChicago.dll` only. No device I/O or private biometric/calibration/secret material was accessed.

This checkpoint closes the context values consumed by the already-proven mode-9 separable Gaussian path.

## Proven caller values

At the `0x18004f480 -> 0x180050090` call on the tested mode-9 path:

```text
RCX  = horizontal descriptor
RDX  = vertical descriptor
R8D  = 2
R9D  = 2
arg5 = 4
arg6 = 4
arg7 = 4
```

After the `0x180050090` prologue, the three stack arguments are read from:

```text
[rsp+0x60] = arg5 = 4
[rsp+0x68] = arg6 = 4
[rsp+0x70] = arg7 = 4
```

The routine stores:

```asm
mov r9d,DWORD PTR [rsp+0x60]
mov r8d,DWORD PTR [rsp+0x70]
...
mov DWORD PTR [rax+0x8],r9d
...
mov r9d,DWORD PTR [rsp+0x68]
...
mov DWORD PTR [rax+0x20],r8d
mov DWORD PTR [rax+0x1c],r9d
```

Therefore for mode 9:

```text
ctx+0x08 = 4
ctx+0x1c = 4
ctx+0x20 = 4
```

## Vertical border mapping — PROVEN

`0x18004e380` uses `ctx+0x20` while constructing row pointers. Values `2` and `4` enter the special reflection mapping. For value `4`, the code sets an adjustment flag of `1` and maps out-of-range row indices as:

```text
-1    -> 1
-2    -> 2
N     -> N-2
N+1   -> N-3
```

This is the usual reflect-without-repeating-edge / `REFLECT_101` style mapping for the vertical axis.

Thus the mode-9 vertical Gaussian border policy is:

```text
ctx+0x20 = 4
=> REFLECT_101-style row extension
```

## Gaussian numeric closure already proven

The mode-9 kernel is:

```text
[7869, 15328, 19142, 15328, 7869]
```

which is the normalized five-tap Gaussian for `sigma=1.5` in Q16.

Horizontal pass (`0x18004f5f0`):

```text
H[y,x] = (
    7869  * src[y,x-2]
  + 15328 * src[y,x-1]
  + 19142 * src[y,x]
  + 15328 * src[y,x+1]
  + 7869  * src[y,x+2]
) >> 16
```

Vertical pass (`0x18004fd20`):

```text
V[y,x] = (
    7869  * H[y-2,x]
  + 15328 * H[y-1,x]
  + 19142 * H[y,x]
  + 15328 * H[y+1,x]
  + 7869  * H[y+2,x]
) >> 16
```

`0x18004f480` proves for the second-axis descriptor:

```text
descriptor+0x28 = arg6 << 16 = 0
descriptor+0x2c = 16
```

on the tested path, so the vertical pass also uses Q16 truncation with zero additive bias.

## Remaining edge question

`ctx+0x1c = 4` is also proven. It is strongly associated with the other-axis edge/halo handling in the streaming executor, but the exact horizontal index-remapping formula should be independently tied to this field before claiming the complete two-axis border policy is proven.

Do not infer more than:

```text
vertical border mapping = PROVEN REFLECT_101-style
horizontal border selector value = PROVEN 4
horizontal exact remapping formula = still to prove
```

## Next target

Trace only the code that consumes `ctx+0x1c` / the horizontal halo/index table construction. Once that mapping is proven, close the Gaussian stage completely and return to the main Q13 pipeline / `state+0x13244` downstream path.
