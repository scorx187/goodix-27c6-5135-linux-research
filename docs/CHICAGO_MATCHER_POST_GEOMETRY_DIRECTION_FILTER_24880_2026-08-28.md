# Chicago matcher post-geometry direction filter — 0x24880 — 2026-08-28

Target: `AlgoChicago.dll`, normal family/type `0x0c` path for Goodix `27c6:5135`.

This document records static reverse-engineering results only. No fingerprint image, template, PSK, OTP, calibration payload, `goodix.dat`, unit-specific runtime configuration, Windows biometric DB, or proprietary binary is stored here.

## Position in matcher

```text
0x59580 correspondence builder
  -> 0x5baf0 pairwise binary-descriptor costs
  -> 0x5a350 ambiguity/spatial/top-K consolidation
  -> 0x1f840 correspondence reducer
       -> 0x586c0 geometric consensus / inlier mask
            -> 0x430f0 three-point affine solver
            -> 0x5e790 affine linear-part plausibility gate
       -> 0x24880 post-geometry direction-consistency filter
```

## 0x180024880 — proven role

Full function:

```text
0x180024880 .. 0x180024a11
128 reachable instructions
one runtime/unwind entry
```

There is one direct caller, `0x18001f840`.

`0x24880` is a **post-geometry minutia-direction consistency filter**. It does not directly compare binary descriptors and does not use minutia X/Y coordinates for its per-pair test.

The only field read from each referenced `0x3c` feature record is:

```text
feature+0x06 = signed Q12-radian direction
```

The apparent references to offsets `+0x00/+0x04/+0x0c/+0x10` at function entry are fields of the affine linear-transform/context object, not fields of a minutia record.

## Rotation extraction from affine matrix

The input geometry object contributes the affine linear part:

```text
M = [[a,b],
     [c,d]]
```

The function computes the magnitudes of the two matrix columns through the helper at `0x1800576e0`:

```text
s1 ~= sqrt(a*a + c*c)
s2 ~= sqrt(b*b + d*d)
s  = trunc_toward_zero((s1+s2)/2)
```

If `s == 0`, the filter exits without modifying the pair arrays.

It then forms normalized components proportional to:

```text
x = (a + d) / 2
y = (c - b) / 2
```

(the implementation uses integer half-rounding toward zero, Q8 scaling, and division by the average column magnitude).

`0x1800575c0` is an integer/CORDIC-like `atan2` helper. Its special cases prove the expected quadrant semantics:

```text
y == 0, x > 0  -> 0
y == 0, x <= 0 -> pi
x == 0, y > 0  -> +pi/2
x == 0, y < 0  -> -pi/2
```

with Q12-radian constants approximately:

```text
pi     = 0x3244
2*pi   = 0x6488
pi/2   = 0x1922
```

Thus the reference rotation used by `0x24880` is structurally equivalent to:

```text
theta = atan2(c - b, a + d)
```

in signed/wrapped Q12 radians. Scale normalization does not change the angle.

## Per-correspondence direction rule

For each correspondence whose two indices are nonnegative, the function resolves both `0x3c` records and reads their signed directions:

```text
dir_A = feature_A[index_A].direction_q12
dir_B = feature_B[index_B].direction_q12
```

It then forms:

```text
delta = dir_A - dir_B + theta
```

The code computes the circular angular distance of `delta` modulo `2*pi`, then also computes the circular distance of `delta + pi`, and retains the smaller value.

Therefore the effective distance is the angular difference **modulo pi**, matching the 180-degree ambiguity of fingerprint ridge/minutia orientation:

```text
err = angular_distance_mod_pi(delta)
```

The exact rejection threshold is:

```text
0x506 Q12 radians
```

which is approximately:

```text
1286 / 4096 = 0.31396484375 rad
               ~= 17.99 degrees
```

So the recovered acceptance rule is:

```text
angular_distance_mod_pi(dir_A - dir_B + theta) <= 0x506
```

and the pair is rejected when the value is greater than `0x506`.

## Invalidation behavior

`0x24880` does **not** directly write the byte inlier mask.

When a pair fails the angular rule, it writes `-1` to both parallel pair-index arrays:

```text
pair_A[i] = -1
pair_B[i] = -1
```

After `0x24880` returns, `0x1f840` checks the filtered pair-index array and clears the corresponding inlier-mask byte when the index is `-1`.

Thus the sequence is:

```text
0x586c0 geometric mask
  -> pair indices inconsistent with geometry set to -1
  -> 0x24880 applies affine-rotation/minutia-direction consistency
       -> failing pair indices set to -1 on both sides
  -> 0x1f840 reflects rejected indices into mask=0
  -> surviving nonzero mask entries are counted
```

## What is NOT used by the per-pair rule

No evidence in the recovered function shows per-pair use of:

- minutia X/Y (`+0x02/+0x04`);
- descriptor fields (`+0x10..+0x2f`);
- quality/status/support (`+0x38/+0x39`);
- strength-like `+0x08`;
- border classification `+0x0c`.

Those concerns are handled elsewhere in the matching pipeline.

## Matcher milestone

The direct correspondence path is now substantially closed through:

```text
binary descriptor distance
  -> ambiguity / spatial duplicate / top-K selection
  -> affine geometric consensus
  -> affine scale/shear plausibility gate
  -> reprojection inlier mask
  -> minutia direction consistency modulo pi
  -> surviving inlier count
```

The next decisive work should return to the minimum active `0x293c0` type-`0x0c` final score/policy fusion path. Do not continue descending into `0x576e0` or the internal CORDIC tables unless bit-exact Linux reproduction later proves those numerics are required.
