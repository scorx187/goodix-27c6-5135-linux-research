# Chicago matcher geometry checkpoint — 0x586c0 / 0x430f0 — 2026-08-28

Target: `AlgoChicago.dll`, normal family/type `0x0c` path for Goodix `27c6:5135`.

This document records static reverse-engineering results only. No fingerprint image, template, PSK, OTP, calibration payload, `goodix.dat`, unit-specific runtime configuration, Windows biometric DB, or proprietary binary is stored here.

## Position in matcher

```text
0x59580 correspondence builder
  -> 0x5baf0 pairwise descriptor costs (twice)
  -> 0x5a350 ambiguity/spatial/top-K correspondence consolidation
  -> 0x1f840 correspondence inlier reducer
       -> 0x586c0 geometric consensus / inlier-mask stage
            -> 0x430f0 exact three-point affine solver
       -> 0x24880 additional pair validity filter
```

## 0x1800586c0 — geometric consensus stage

Full CFG recovery crosses two unwind entries:

```text
0x1800586c0 .. 0x180058f8d
590 reachable instructions
```

Inputs from `0x1f840` are parallel arrays of matched coordinates and directions plus correspondence count and output work buffers.

The function is not a simple threshold gate. It enumerates three-correspondence geometric hypotheses, checks source/target triangle scale consistency, checks wrapped direction consistency using Q12-radian constants (`pi ~= 0x3244`, `2*pi ~= 0x6488` on the normal mode), solves a six-parameter affine transform via `0x430f0`, then rejects affine solutions whose matrix is not close enough to the expected rotation/scale form.

The triangle-side consistency checks compare squared edge lengths with integer ratios approximately bounded by `5/6 .. 6/5`, and require sufficiently non-trivial edge length (`>= 0x30000` in the recovered squared fixed-point domain).

After `0x430f0`, the six transform DWORDs are consumed as:

```text
[a,b,tx,c,d,ty]

x' = ((a*x + b*y + 128) >> 8) + tx
y' = ((c*x + d*y + 128) >> 8) + ty
```

The matrix is then constrained toward a similarity/rigid-like form: the two diagonal terms must be close, the off-diagonal terms approximately opposite, and coefficient magnitudes are bounded.

For each correspondence, reprojection residuals are computed in Q8 coordinate space. A pair is marked as an inlier only if both axis residuals are within `0x280` and squared residual is below `0x64000`.

Since coordinates are Q8, these correspond to approximately:

```text
|dx|, |dy| <= 2.5 pixels
(dx^2 + dy^2) < 6.25 pixel^2
```

The function writes one byte per tested correspondence (`0` or `1`) into its local candidate mask and, when a hypothesis wins, copies that mask to the caller-provided output mask.

Model selection is lexicographic:

1. prefer larger inlier count;
2. when counts tie, prefer lower mean squared reprojection error.

The error metric is computed from the accumulated squared reprojection residuals divided by the number of inliers (with integer rounding). The metric later checked by `0x1f840` against `0x4000` therefore lives in the same squared Q8 coordinate-error domain. `0x4000 / 65536 = 0.25 pixel^2`, corresponding to about `0.5 px` RMS.

`0x5e790` is used as an additional model acceptance/plausibility gate before the winning transform/mask is committed. Its exact rule remains to be reversed.

## 0x1800430f0 — exact three-point affine solver

Function boundary:

```text
0x1800430f0 .. 0x180043379
197 reachable instructions
one substantive function; only child is stack-cookie support
```

ABI proven from `0x586c0`:

```text
RCX = output six-DWORD transform
RDX = source three-point coordinate set
R8  = target three-point coordinate set
```

Each point set is six signed DWORDs:

```text
[x1,y1,x2,y2,x3,y3]
```

The solver scales target coordinates by `<<10`, forms source coordinate differences, and solves a full affine transform through the three point pairs by determinant/Cramer's-rule-style integer divisions.

Let source points be:

```text
P1=(x1,y1)
P2=(x2,y2)
P3=(x3,y3)
```

and target points:

```text
Q1=(u1,v1)
Q2=(u2,v2)
Q3=(u3,v3)
```

A mathematically equivalent reconstruction of the integer solver is:

```text
D = (x2-x1)*(y3-y1) - (x3-x1)*(y2-y1)

A10 = ((((u2-u1) << 10) * (y3-y1))
      - (((u3-u1) << 10) * (y2-y1))) / D

B10 = (((x2-x1) * ((u3-u1) << 10))
      - ((x3-x1) * ((u2-u1) << 10))) / D

C10 = ((((v2-v1) << 10) * (y3-y1))
      - (((v3-v1) << 10) * (y2-y1))) / D

D10 = (((x2-x1) * ((v3-v1) << 10))
      - ((x3-x1) * ((v2-v1) << 10))) / D
```

The output matrix coefficients are converted from Q10 to Q8:

```text
a = A10 >> 2
b = B10 >> 2
c = C10 >> 2
d = D10 >> 2
```

Translations are recovered in the original Q8 coordinate domain:

```text
tx = ((u1 << 10) - A10*x1 - B10*y1) >> 10
ty = ((v1 << 10) - C10*x1 - D10*y1) >> 10
```

and written in this exact output layout:

```text
+0x00 a
+0x04 b
+0x08 tx
+0x0c c
+0x10 d
+0x14 ty
```

If a required determinant is zero, the solver substitutes `0x7fffffff` for the affected coefficient path. `0x586c0` subsequently rejects such invalid/degenerate transforms through its coefficient plausibility bounds.

Important interpretation correction: `0x430f0` itself solves a general affine transform. The near-similarity/rotation-scale restriction is imposed by `0x586c0` after solving, not by `0x430f0`.

## Current next targets

Do not reopen already-solved descriptor/correspondence stages.

Immediate decisive work:

1. reverse `0x18005e790` to identify the additional winning-model acceptance rule inside `0x586c0`;
2. reverse `0x180024880` to recover the exact post-geometry per-pair direction/feature invalidation rule;
3. then return to the minimum remaining final score/policy fusion path.

Enrollment/template update and Linux/libfprint implementation remain after matcher closure.
