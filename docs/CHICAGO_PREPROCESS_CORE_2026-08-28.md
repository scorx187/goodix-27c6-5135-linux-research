# Chicago preprocessing core checkpoint — 2026-08-28

Target: `AlgoChicago.dll` for Goodix `27c6:5135`, logical chip `0x2504`, selector/family `0x0c`.

This document records static reverse-engineering conclusions only. No proprietary binary, biometric frame/template, device secret, unit-specific config, or calibration payload is committed.

## Proven call chain

```text
EngineAdapter.dll
 -> AlgoChicago.dll preprocessor_wrapper
 -> logical outer routine 0x18000e780..0x18000e947
 -> core orchestrator 0x1800484e0
 -> first image/state combiner 0x180044970
 -> mask cleanup 0x180043c40..0x180043ff6
 -> post-mask join
 -> 0x18004aea0
```

`0x1800484e0` is a large preprocessing orchestrator, not a single pixel loop.

## Geometry and state plane

The core validates a 16-bit source plane whose byte length is:

```text
width * height * 2
```

For the tested downstream geometry this is:

```text
80 * 64 * 2 = 10240 bytes
```

It copies the source plane and separately copies a `2*pixel_count` u16 plane from:

```text
state + 0x9924
```

When an internal mode equals 2, the copied state plane receives a neighbor-average interpolation pass.

`state+0x9924` is therefore proven to be an AlgoChicago internal preprocessing/calibration-state plane. It is **not yet proven identical to gfusb persisted ImageBase**.

## `0x180044970` — first exact per-pixel transform

Known call mapping:

```text
RCX = copied source u16 plane
RDX = copied state+0x9924 u16 plane
R8  = temporary result structure
R9D = six-bit sensor/algorithm selector
```

For the tested selector `0x0c`, the active non-type-4 loop is:

```asm
movdqu xmm1,[state_plane+index*2]
movdqu xmm0,[source+index*2]
psubw  xmm1,xmm0
movdqu [output+index*2],xmm1
```

The scalar tail performs the same operand order:

```asm
movzx eax,WORD PTR [state_plane]
sub   ax,WORD PTR [source]
mov   WORD PTR [output],ax
```

Therefore the exact tested relation is:

```text
diff16[i] = state_plane_u16[i] - source_u16[i]
```

This is 16-bit wrapping subtraction with no clamp, saturation or gain in the subtraction loop. Later `movsx` loads prove the result is interpreted as signed 16-bit two's-complement data.

### Selector 4 special path

Selector 4, which is not the tested 5135 path, computes:

```text
state_plane - source + 0x0fff
```

Any older text stating `source - state_plane` or `source - state_plane + 0x0fff` is stale and incorrect.

## Later behavior inside `0x180044970`

After the signed difference, the function computes local/window statistics, minimum/maximum and threshold-like values, creates/updates a byte mask, maintains active counts and writes a percentage-like field:

```text
count * 100 / pixel_count
```

at result offset `+0x0e`.

Exact semantic names for every intermediate threshold/status remain intentionally conservative.

## `0x180043c40` — mask cleanup/source validity stage

The logical routine is compiler-split across adjacent unwind records and normally returns at `0x180043ff5`.

Its result structure uses:

```text
+0x00 active mask count
+0x04 first dimension
+0x08 second dimension
+0x0e active percentage
+0x10 byte mask
```

The stage:

1. aggregates mask support along both axes;
2. performs edge-aware mask hole-fill/cleanup;
3. filters mask positions using the original source u16 values;
4. recomputes active count/coverage.

For selector `0x0c` the validity condition is:

```text
100 < source_u16[i] < 3800
```

The second image/state argument is not consumed in the normal body, so this is not another state/source subtraction.

## Post-mask flow

For the tested path, after:

```text
0x1800489cd call 0x180043c40
```

control jumps to common join `0x180048a43`. Helpers `0x1800447d0` and `0x180044010` are alternative pre-join branches, not stages after `0x180043c40`.

The first substantial common downstream helper is:

```text
0x18004aea0
```

called at `0x180048b02`.

Later analysis has already proven that this branch constructs Q13 correction surfaces and a persistent corrected plane at `state+0x13244`; see the newer post-mask/Q13 checkpoints and `AI_START_HERE.md` rather than re-investigating this core boundary.

## Safety boundary

Do not request, publish, upload, commit, or hash real fingerprint frames/templates, plaintext PSK or hashes, full OTP, full unit-specific runtime config, `goodix.dat`, `goodix_calib.dat`, `Goodix_Cache.bin`, proprietary Goodix binaries, Windows biometric DB material, or process dumps. Do not erase/flash firmware or rewrite/re-provision the factory PSK.
