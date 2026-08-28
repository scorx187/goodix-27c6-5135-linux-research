# Chicago feature extraction and post-extraction pruning — 2026-08-28

Target: Goodix `27c6:5135`, ChicagoHS/ChicagoHU, sensor family/type `0x0c`.

This document records the current static reverse-engineering checkpoint inside `AlgoChicago.dll`. It contains no proprietary binaries, device secrets, biometric samples, templates, unit-specific calibration/runtime payloads, or Windows biometric database material.

## Position in the pipeline

The current reconstructed path is:

```text
Windows-compatible 80x64 image
  -> preprocessing/correction/normalization
  -> quality + segmentation gates
  -> accepted fingerprint feature detection
  -> per-feature orientation + local descriptors
  -> retained 0x3c-byte feature list
  -> post-extraction spatial pruning        <-- current checkpoint
  -> representation/probe/template building
  -> matcher / enrollment
```

Preprocessing is no longer the active blocker. The current work is in Phase 8: matcher/enrollment-side representation construction.

## Shared representation builder

`0x180016920` is a thunk to `0x1800139e0`.

Direct-call analysis proves the shared builder is used by both enrollment and identify paths. It is not a thin wrapper.

Known output-object fields:

```text
output +0x00/+0x04   dimensions/geometry
output +0xf0         retained feature count
output +0xf8         retained feature array pointer
output +0x10c        quality-like field
output +0x110        coverage-like field
output +0x148/+0x218/+0x220/+0x228  auxiliary buffers/state
```

The retained fingerprint-feature record stride is exactly:

```text
0x3c bytes
```

## `0x180011080` — feature-extraction orchestrator

`0x180011080` is proven to orchestrate fingerprint-specific feature extraction rather than being a leaf detector.

It prepares image/map work objects, invokes the detector pipeline, reduces/selects candidates, calls per-record refinement, and emits the final retained `0x3c` records into `output+0xf8` while updating `output+0xf0`.

## `0x1800153b0` — feature-candidate scanner

`0x1800153b0` scans the image interior and produces candidate features. It uses a six-pixel border and delegates accepted-feature emission to `0x180014560`.

Candidate fields proven so far include:

```text
+0x00 dword  integer X
+0x04 dword  integer Y
+0x10 word   X << 8
+0x12 word   Y << 8
+0x14 dword  local spatial scale / neighborhood-size parameter
```

Important correction: candidate `+0x14` is not the final direction angle. It controls the local orientation-analysis neighborhood.

## `0x180014560` — accepted-feature emitter

This function is proven to append one accepted feature and increment the feature count.

The destination is computed from:

```text
feature_array + feature_count * 0x3c
```

and the function ends by writing the incremented count back through the supplied count pointer.

Proven record fields:

```text
feature +0x02 = X in Q8
feature +0x04 = Y in Q8
feature +0x06 = signed Q12-radian direction
```

`feature+0x00` is a local flag/quality-like value and `feature+0x08` is response/strength-like; exact public semantic names remain intentionally unclaimed until their consumers close them.

For type `0x0c`, the normal path is selected; the special sensor-type branch is for types `9` and `0x12` (18 decimal).

## `0x180011a60` — local orientation histogram

`0x180011a60` is proven to build a 36-bin circular local orientation histogram around an accepted feature candidate.

Key properties:

- integer/fixed-point implementation; no floating-point path;
- local window size is derived from candidate `+0x14` and capped;
- a spatial-weight lookup is used over the neighborhood;
- local signed Q12-radian orientations are quantized into 36 circular bins;
- in the normal type-`0x0c` path, opposite orientations are accumulated with pi symmetry;
- the histogram is circularly smoothed with exact binomial kernel `[1,4,6,4,1]/16`;
- the selected histogram peak feeds sub-bin interpolation in `0x180014560`, which converts the final angle to signed Q12 radians at feature `+0x06`.

## Local descriptor construction

`0x1800157d0` enriches each retained feature and calls `0x180017730` twice:

```text
Pass A: 128 descriptor components
Pass B:  32 descriptor components
```

### `0x180056b10` — exact threshold rule

`0x180056b10` performs in-place selection and returns:

```text
descriptor[floor((N-1)/2)]
```

Therefore for even `N` it returns the lower median:

```text
Pass A N=128 -> rank 63
Pass B N=32  -> rank 15
```

### `0x180056520` — median-threshold bitset packer

For each configured output bit, `0x180056520` sets the bit iff the selected descriptor component is strictly greater than the lower-median threshold.

For type `0x0c`, the recovered configuration is:

```text
Pass A:
  output bits   = 64
  source stride = 2
  source indices = 1,3,5,...,127
  packed size = 8 bytes

Pass B:
  output bits   = 32
  source stride = 1
  source indices = 0..31
  packed size = 4 bytes
```

Thus:

```text
feature +0x20..+0x27 = Pass-A 64-bit median-threshold descriptor
feature +0x2c..+0x2f = Pass-B 32-bit median-threshold descriptor
```

### `0x180056780` / `0x180056670` — sign/projection descriptor masks

For the normal type-`0x0c` path:

```text
0x180056780 -> feature +0x10..+0x1f
               four 32-bit sign/projection masks (Pass A)

0x180056670 -> feature +0x28..+0x2b
               one 32-bit sign/projection mask (Pass B)
```

The type-`0x0c` second-pass writer clears a 16-byte region beginning at `+0x28`, then only sets bits in the first DWORD. Consequently `+0x30..+0x37` remain zero on this path before later consumers unless another stage writes them.

## `0x1800371c0` — post-extraction spatial feature pruning

This is the first proven whole-feature-list consumer after extraction.

It receives the retained `0x3c` feature array and `&feature_count`.

It is **not** the matcher and does **not** construct a persistent template object. It filters the feature list in-place.

For each retained feature it rounds Q8 coordinates to pixels:

```text
x = (feature.x_q8 + 0x80) >> 8
y = (feature.y_q8 + 0x80) >> 8
```

and indexes a temporary `WORD` map at:

```text
map[y * width + x]
```

When the map value is greater than `1`, the feature is deleted by:

1. decrementing the retained count;
2. moving the last `0x3c` feature into the deleted slot when needed;
3. zeroing the old last `0x3c` slot;
4. rechecking the moved feature;
5. writing the final count back through `feature_count`.

Therefore `0x1800371c0` is proven to perform real list compaction and mutate `feature_count`.

## `0x18003b820` — exact map meaning

`0x18003b820` is now closed at the algorithmic level.

Caller-proven inputs are:

```text
RCX  = input byte mask/buffer
RDX  = destination WORD map
R8D  = width
R9D  = height
arg5 = local radius/policy
arg6 = pointer to total-zero metric
```

It allocates a temporary `(width+1) x (height+1)` WORD buffer and builds a summed-area/integral image over the predicate:

```text
z(y,x) = 1 if input_byte(y,x) == 0
         0 otherwise
```

At the same time:

```text
*metric = total number of zero-valued input pixels
```

The integral recurrence is equivalent to:

```text
I[y+1,x+1] = z(y,x)
           + I[y,  x+1]
           + I[y+1,x]
           - I[y,  x]
```

Afterward, for every output pixel it uses four integral-image corner reads to compute the number of zero-valued input pixels in a clipped square neighborhood with radius `arg5`.

Therefore the output is exactly:

```text
WORD local_zero_count[y,x]
```

where the nominal interior window is:

```text
(2*radius + 1) x (2*radius + 1)
```

and borders are clipped to the valid image domain.

This interpretation is independently corroborated by the second direct caller, which computes approximately half of `(2*radius+1)^2` and compares returned map counts against that threshold.

### How `0x1800371c0` uses it

The pruning path continues only when:

```text
0x18003b820 returns success
and total_zero_count != 0
and total_zero_count >= 50
```

Then a retained feature is removed when:

```text
local_zero_count[round(y), round(x)] > 1
```

An optional caller flag also causes an auxiliary byte buffer to be zeroed at locations whose local zero-count is nonzero.

The neutral proven description is therefore:

> Remove retained fingerprint features that lie too close to zero-valued regions of the supplied mask/buffer, when the image contains at least 50 zero-valued pixels globally.

Do not rename zero-valued pixels as "background", "invalid", or "segmentation failure" until the producer/semantic polarity of this exact input buffer is independently closed.

## Immediate next target

Do **not** descend further into `0x18003b820`; its behavior is sufficiently closed.

Return to `0x1800139e0` immediately after the `0x1800371c0` call and identify the next stage that transforms the pruned retained-feature list into the first matcher-ready probe/template-side representation.

Prioritize:

1. persistent representation/object construction;
2. enrollment consumer `0x18000cd70`;
3. identify-side matching path and per-candidate matcher `0x180028c90`;
4. only those child helpers required to reproduce matcher behavior.

Do not return to preprocessing or per-pixel image helpers unless a newly proven dependency requires it.

## Still unresolved

- exact semantic name of feature `+0x00`;
- exact semantic name of feature `+0x08`;
- final meaning/use of remaining small status bytes around the end of the `0x3c` record;
- first persistent matcher-ready probe/template object after pruning;
- exact enrollment update/template-storage representation;
- exact matcher scoring/decision semantics in `0x180028c90` and decisive children;
- producer identity of preprocessing `state+0x9924` versus gfusb ImageBase;
- Linux implementation of feature extraction/matcher/enrollment;
- libfprint/fprintd integration and lifecycle validation.

## Safety boundary

Static analysis only. Never commit or publish plaintext factory PSK or PSK hashes, full OTP, fingerprint images/raw/templates, Windows biometric DB material, `goodix.dat`, `goodix_calib.dat`, `Goodix_Cache.bin`, proprietary Goodix DLL/EXE/CAT files, full unit-specific runtime configuration, unit-specific runtime-config hash, or process/memory dumps.

No firmware erase/flash, PSK rewrite/reprovision, destructive 5117 tooling, arbitrary persistent register writes, or Windows fingerprint removal/re-enrollment shortcuts.
