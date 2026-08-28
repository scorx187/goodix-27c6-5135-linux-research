# Chicago spatial median filter `0x18004c3b0` — 2026-08-28

Target: `AlgoChicago.dll`, tested Goodix `27c6:5135`, logical chip `0x2504`, family/type `0x0c`.

This checkpoint records static reverse-engineering conclusions only. No proprietary binary, fingerprint frame/template, PSK/OTP, unit-specific runtime configuration, or calibration payload is committed.

## Why this function matters

On the tested type-`0x0c` path, `0x18004d6f0` first builds a temporary Q13 ratio plane and then calls:

```text
0x18004c3b0(tmp_ratio_object, work_object)
```

The filtered output is then compared against another per-pixel work plane. If the absolute difference exceeds the type-`0x0c` threshold `0x708` (1800), `0x18004d6f0` rescales `scratch_A`.

Therefore the exact transform performed by `0x18004c3b0` is part of the proven base Q13 correction logic.

## Logical boundary

The first PE x64 `RUNTIME_FUNCTION` entry begins at:

```text
RVA 0x0004c3b0 .. 0x0004c660
```

Static control-flow reachability from `0x18004c3b0` reaches through `0x18004c65b`.

The function ends with:

```text
0x18004c646  load temporary allocation pointer
...
0x18004c65b  jmp 0x180064d60
```

`0x180064d60` is the already-observed allocation-release helper. This is a tail-call cleanup, so there is no local `ret` before the jump.

## Image-object fields used

Both arguments are image/work objects with the same important layout used elsewhere in this DLL:

```text
+0x00  width in WORD pixels
+0x04  height
+0x08  row byte count
+0x18  u16 data pointer
```

The routine allocates exactly:

```text
4 * width bytes
```

which is two temporary rows of `u16` samples.

## Exact filter — PROVEN

The filter is a **separable median-of-three filter**.

For an input image `src[y][x]`, first define the horizontal 3-sample median:

```text
H(y,x) = median(
    src[y][x-1],
    src[y][x],
    src[y][x+1]
)
```

for interior columns.

The output interior pixel is then:

```text
out[y][x] = median(
    H(y-1,x),
    H(y,x),
    H(y+1,x)
)
```

for:

```text
1 <= x < width-1
1 <= y < height-1
```

This is equivalent to a horizontal median-of-3 pass followed by a vertical median-of-3 pass over the horizontal results.

Important precision boundary:

- this is a separable 3x3 median-style filter;
- it is **not generally identical to the true median of all nine samples in a 3x3 neighborhood**.

## Assembly proof structure

### Horizontal stage

The first loop repeatedly loads three adjacent WORDs, orders them through comparisons, and stores the middle value. It performs this for two adjacent source rows into two temporary u16 rows.

Representative addresses:

```text
0x18004c460 .. 0x18004c495
0x18004c49a .. 0x18004c4cc
```

No arithmetic averaging is present. The sequence is compare/select logic that returns the median of three unsigned WORD values.

### Vertical stage

For every interior output row, the routine:

1. computes the horizontal median for the next source row;
2. retains horizontal-median values for the previous/current rows in the two-row scratch buffer;
3. applies the same median-of-three compare/select pattern vertically;
4. writes the resulting WORD into the output object.

Representative addresses:

```text
0x18004c550 .. 0x18004c58c   next-row horizontal median
0x18004c590 .. 0x18004c5b2   vertical median
```

The two scratch rows are rotated/reused as processing advances down the image.

## Border behavior — PROVEN

The border is preserved rather than filtered:

- the first input row is copied directly to the first output row before filtering;
- for each interior row, the leftmost and rightmost source pixels are copied unchanged;
- the last input row is copied directly to the last output row after the interior loop.

Therefore:

```text
out[0][x]          = src[0][x]
out[height-1][x]   = src[height-1][x]
out[y][0]          = src[y][0]
out[y][width-1]    = src[y][width-1]
```

## Meaning in the type-0x0c correction path

Combined with the already-proven `0x18004d6f0` relation:

```text
ratio_raw[i] = round((reference[i] << 13) / scratch_A[i])
```

(with the zero-denominator special case), this function produces a spatially median-filtered ratio surface:

```text
ratio_filtered = separable_median3x3(ratio_raw)
```

Then `0x18004d6f0` compares `ratio_filtered[i]` against a second per-pixel work value. On type `0x0c`, when:

```text
ratio_filtered[i] != 0
work_value[i]     != 0
abs(ratio_filtered[i] - work_value[i]) > 1800
```

it updates:

```text
scratch_A[i] = round(
    scratch_A[i] * ratio_filtered[i]
    / work_value[i]
)
```

and caps the result at `0x7fff` on the selector families covered by the relevant bit mask (which includes the tested type-`0x0c` path).

Thus the conservative semantic description is:

```text
spatially robust per-pixel Q13 gain correction
```

where a median-filtered local ratio is used to reject/repair large local deviations before the later Q13 factor composition.

## What this closes

`0x18004c3b0` is no longer an unknown helper. It is not a generic blur, floating-point normalization, matcher stage, or new image transport step.

It is a deterministic integer separable median-of-three spatial filter used inside the type-`0x0c` Q13 correction path.

## Immediate next task

Return to the tested `0x180049ba0` caller path. Do **not** descend into another child helper merely because it appears next in address order.

The next unresolved call is `0x1800497c0`, but it does not visibly receive `scratch_A` in the already-recovered call arguments. First inspect the caller region immediately after the call to prove whether its return value or side effects select a branch that later modifies the Q13 surfaces.

Specifically inspect approximately:

```text
0x18004a1d0 .. 0x18004a2f0
```

Goals:

1. map the complete branch after `call 0x1800497c0`;
2. identify all writes/copies involving `scratch_A` / `scratch_B` before the selector dispatch at `0x18004a2f0`;
3. descend into `0x1800497c0` only if the caller proves that its output/side effects feed the correction surfaces used by the parent;
4. otherwise skip it and continue to the next proven denominator-modifying operation.

Separately retain the open task of tracing the producer of `state+0x9924` and the downstream consumer of `state+0x13244`.
