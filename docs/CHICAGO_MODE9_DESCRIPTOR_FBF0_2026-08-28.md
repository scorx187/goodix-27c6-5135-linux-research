# Chicago mode-9 static descriptor selection at `0x18004fbf0` — 2026-08-28

Target: `AlgoChicago.dll`, tested Goodix `27c6:5135`, logical chip `0x2504`, family/type `0x0c`.

Static reverse-engineering checkpoint only. No proprietary DLL, fingerprint frame/template, PSK/OTP, calibration payload, unit-specific runtime configuration, or private biometric data is committed.

## Proven caller path

For the tested gated factor-update path:

```text
0x18004b460
 -> 0x18004e110(... mode=9 ...)
 -> 0x18004e820(format1=2, format2=2, packed_dims, mode=9, -1, -1)
 -> 0x18004fbf0(local_descriptor, EDX=9, R8D=4)
```

`0x18004e820` is a context/descriptor builder, not the pixel transform itself.

## Exact `0x18004fbf0` boundary

PE x64 runtime-function table:

```text
RVA 0x0004fbf0 .. 0x0004fd1d
size 0x12d = 301 bytes
normal returns at 0x18004fcc4 / 0x18004fd1c
```

## Mode-table dispatch — PROVEN

`EDX` is the mode selector. Four static table bases are selected in ranges of 100 modes:

```text
mode < 100       -> base 0x1800932b0
100 <= mode <200 -> base 0x180093570
200 <= mode <300 -> base 0x1800936f0
mode >=300       -> base 0x180093930
```

The selected mode index is multiplied by `0x40`, so each static mode record is exactly 64 bytes.

For `mode=9`:

```text
base   = 0x1800932b0
index  = 9
record = base + 9*0x40
       = 0x1800934f0
```

Therefore the exact static record consumed by the tested path is:

```text
0x1800934f0 .. 0x18009352f
```

## Class-4 descriptor construction — PROVEN

The tested caller passes `R8D=4`. The function takes its dedicated class-4 path:

```text
dst+0x10 = 4
dst+0x00 = 1
count    = *(u32 *)(record+0x00)
dst+0x04 = count
dst+0x18 = alloc(count * 4)
copy count DWORDs from record+0x04 into dst+0x18
```

So for this path the selected record layout is proven to begin as:

```text
+0x00 : DWORD element_count
+0x04 : DWORD element[0]
+0x08 : DWORD element[1]
...
```

Only `element_count` DWORD values are copied. The remaining bytes of the 64-byte static record may belong to alternate classes/metadata and must not be interpreted until needed.

This proves that mode 9 selects a small static DWORD parameter/coefficients record. The semantic label of those DWORD values is still open until the actual mode-9 record is decoded and its consumer is traced.

## Non-class-4 behavior

For completeness, non-class-4 inputs use the same record but derive a byte length from `class` format bits and copy raw bytes from `record+4`. This path is not the tested `27c6:5135` mode-9 path and should not be used to infer the class-4 record semantics.

## Immediate next target

Read only the static mode-9 record at:

```text
VA  0x1800934f0
RVA 0x000934f0
size 0x40 bytes
```

Need to establish:

1. exact `element_count`;
2. exact copied DWORD sequence from `record+4`;
3. signed/unsigned forms, sums, symmetry and obvious fixed-point structure;
4. whether the values support a filter/kernel/coefficient interpretation or a different parameter structure;
5. then trace only those values through `0x18004f480` / `0x18004fff0` to recover the actual transform.

Do not call mode 9 a 9x9 window unless later arithmetic proves that meaning.
