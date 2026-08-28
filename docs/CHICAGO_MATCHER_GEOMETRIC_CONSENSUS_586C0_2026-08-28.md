# Chicago matcher geometric-consensus stage `0x1800586c0` — 2026-08-28

Target: Goodix USB fingerprint sensor `27c6:5135`, ChicagoHS/ChicagoHU, sensor family/type `0x0c`.

This document records a static reverse-engineering checkpoint inside `AlgoChicago.dll` for Linux compatibility research. It contains no proprietary binary, factory secret, biometric image/template, unit-specific calibration/runtime payload, or Windows biometric database material.

## Position in the matcher

Current relevant path:

```text
0x180059580 primary correspondence builder
  -> 0x18005baf0 pairwise binary descriptor comparison (twice)
  -> 0x18005a350 descriptor correspondence consolidation
  -> flat int32 correspondence index pairs

0x18001f840 correspondence reduction
  -> materializes X/Y/direction for each retained pair
  -> 0x1800586c0 geometric-consensus / inlier-mask estimator
  -> 0x180024880 additional pair-validity filtering
  -> returns surviving inlier count
```

`0x18001f840` is now proven to return a surviving correspondence/inlier count rather than a probabilistic score. The geometry residual metric produced by `0x1800586c0` is a separate output.

## Important unwind correction

The first PE runtime entry for `0x1800586c0` is not the complete function.

Full logical CFG recovery proves:

```text
entry:                 0x1800586c0
last reachable insn:   0x180058f8d
reachable instructions: 590
runtime entries crossed: 2
```

The logical function spans:

```text
RVA 0x000586c0 .. 0x000587c7
RVA 0x000587c7 .. 0x00058f8e
```

## Effective ABI from `0x18001f840`

`0x1f840` first converts the correspondence index-pair list into compact parallel arrays containing:

- candidate/template X/Y coordinates;
- probe X/Y coordinates;
- candidate/template directions;
- probe directions.

The call into `0x586c0` is effectively:

```text
RCX   = coordinate array B
RDX   = coordinate array A
R8    = direction array A
R9    = direction array B
arg5  = valid correspondence count
arg6  = output/best transform buffer (24 bytes)
arg7  = output byte inlier mask
arg8  = output geometric residual metric
arg9  = minimum required inlier count
arg10 = orientation-mode flag
```

The caller passes an output mask with capacity 42 bytes. `0x586c0` itself explicitly bounds the per-correspondence mask loop at `0x2a` entries.

## Orientation arithmetic

The directions are signed Q12 radians, consistent with the earlier feature-record reconstruction.

Default mode constants:

```text
0x3244 ~= pi * 4096
0x6488 ~= 2*pi * 4096
angle consistency tolerance = 0x800 = 0.5 rad in Q12
```

When the extra orientation-mode flag is nonzero:

```text
wrap half-range = 0x1922 ~= (pi/2) * 4096
period          = 0x3244 ~= pi * 4096
angle tolerance = 0x400 = 0.25 rad in Q12
```

So the alternate mode treats orientation as pi-periodic and applies a tighter angular-consistency tolerance.

## Seed generation: three-correspondence geometric hypotheses

The function requires at least three correspondences before entering the substantial path.

It contains three nested correspondence-selection loops. Each candidate seed therefore consists of three correspondence pairs.

Before fitting a transform, all three triangle edges are checked between the two point sets.

For each corresponding edge, the scaled squared distances must satisfy approximately:

```text
5 * dA <= 6 * dB
6 * dA >= 5 * dB
```

which constrains the edge-length ratio to roughly:

```text
5/6 <= dA/dB <= 6/5
```

The same check is performed for all three edges.

Each scaled squared edge distance must also be at least `0x30000`.

Because coordinate differences are Q8 and the squared terms are shifted right by two before comparison, this threshold corresponds to a minimum edge length of approximately `sqrt(12) ~= 3.46` pixels.

The three per-correspondence direction differences are wrapped according to the selected orientation period, averaged using exact signed integer division by three, and required to lie within the configured angular tolerance around the common mean.

This seed filter therefore requires both approximate triangle-scale consistency and common orientation-offset consistency before solving a spatial transform.

## Six-parameter 2D transform

For a surviving three-correspondence seed, `0x586c0` calls:

```text
0x1800430f0
```

with two three-point coordinate sets and a six-DWORD output buffer.

The six returned values are consumed as a Q8 affine-like transform:

```text
[a b tx]
[c d ty]
```

with projection performed as:

```text
x_pred = ((a*x + b*y + 128) >> 8) + tx
y_pred = ((c*x + d*y + 128) >> 8) + ty
```

The exact coefficient-generation formula inside `0x430f0` remains the immediate next target.

## Similarity / rigid-like plausibility constraints

After `0x430f0`, `0x586c0` does not accept an arbitrary affine transform.

It checks the linear 2x2 portion using:

```text
abs(a - d) < 50
abs(c + b) < 50
abs(a) < 300
abs(b) < 300
abs(c) < 300
abs(d) < 300
```

The first two relations are characteristic of an approximately scaled-rotation matrix:

```text
[a  b]
[-b a]
```

Therefore the active geometric model is best described as a constrained similarity/rigid-like 2D transform rather than unrestricted affine matching.

Do not yet replace that neutral name with an exact public product term until `0x430f0` and the later transform gate are decoded.

## Inlier test

For every correspondence, the selected model predicts the mapped X/Y position and computes residuals in Q8 coordinate units.

An inlier must satisfy:

```text
abs(dx) <= 0x280
abs(dy) <= 0x280
dx*dx + dy*dy < 0x64000
```

Since `0x280 = 640` Q8 units:

```text
640 / 256 = 2.5 pixels
```

and:

```text
0x64000 = 640^2
```

So the decisive radial threshold is approximately:

```text
Euclidean reprojection error < 2.5 pixels
```

For every passing correspondence:

```text
mask[i] = 1
```

otherwise the mask entry remains zero.

The function also accumulates the squared reprojection error over all inliers.

## Model-selection metric

A candidate model is considered only if its inlier count meets the minimum supplied through `arg9`.

Its residual metric is computed exactly as a rounded integer mean of squared Q8 residuals:

```text
mse_q16 = (sum_squared_error + inlier_count/2) / inlier_count
```

If there are no inliers, the fallback metric is:

```text
0x190000 = 25 * 65536
```

which is `25 px^2` in Q16, equivalent to an RMS error of 5 pixels.

Model selection prefers:

1. larger inlier count;
2. when the count ties, smaller mean squared residual.

A candidate best model is additionally passed through `0x18005e790` with constants `0x191` and `0xa3`; the exact semantic gate performed by that helper is not yet closed.

When a model wins, the function copies:

- its six transform DWORDs to the best-transform buffer;
- its byte inlier mask to the output mask;
- its mean squared residual to the metric output.

The search can terminate early once the best inlier count exceeds 20.

## Meaning of the later `0x4000` threshold

`0x18001f840` later checks the residual metric against:

```text
0x4000
```

The metric is Q16 pixel-squared error, therefore:

```text
0x4000 / 65536 = 0.25 px^2
sqrt(0.25) = 0.5 px RMS
```

So the previously opaque `0x4000` condition is now interpretable as an RMS reprojection-error boundary of approximately 0.5 pixels.

When there are at least four surviving inliers and the metric is above this boundary, `0x1f840` invokes `0x180058f90` for an additional refinement/validation stage.

## Current interpretation

`0x1800586c0` is a deterministic three-correspondence geometric-consensus estimator:

1. enumerate three-correspondence seed hypotheses;
2. reject seeds whose triangle geometry is inconsistent;
3. reject seeds whose orientation offsets are inconsistent;
4. solve a six-parameter Q8 transform via `0x430f0`;
5. reject transforms that are not sufficiently similarity/rigid-like;
6. score every correspondence by reprojection error;
7. build an inlier mask using a 2.5-pixel radius;
8. choose the hypothesis with maximum inliers, tie-breaking on lower MSE;
9. write the best transform, mask, and MSE.

This is RANSAC-like consensus behavior, but the recovered path enumerates hypotheses deterministically rather than proving random sampling. Prefer `geometric-consensus estimator` over claiming literal RANSAC.

## Immediate next targets

Decode only the decisive helpers needed to close this stage:

1. `0x1800430f0` — exact six-parameter transform solver;
2. `0x18005e790` — final transform plausibility/acceptance gate;
3. then `0x180024880` — post-consensus pair validity/direction filtering;
4. then return to final matcher score fusion.

Do not descend back into USB/TLS/preprocessing or already-closed descriptor-generation helpers unless a newly proven dependency requires it.

## Safety boundary

Static analysis only. Never commit or publish plaintext factory PSK or PSK hashes, full OTP, fingerprint images/raw/templates, Windows biometric DB material, `goodix.dat`, `goodix_calib.dat`, `Goodix_Cache.bin`, proprietary Goodix DLL/EXE/CAT files, full unit-specific runtime configuration, unit-specific runtime-config hash, or process/memory dumps.

No firmware erase/flash, PSK rewrite/reprovision, destructive 5117 tooling, arbitrary persistent register writes, or Windows fingerprint removal/re-enrollment shortcuts. Keep the Windows partition read-only during analysis.