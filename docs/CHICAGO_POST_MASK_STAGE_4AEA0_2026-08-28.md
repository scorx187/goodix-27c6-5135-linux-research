# Chicago post-mask stage `0x18004aea0` — 2026-08-28

Target: `AlgoChicago.dll`, tested Goodix `27c6:5135`, logical chip `0x2504`, Chicago/type selector `0x0c`.

This checkpoint records static reverse-engineering conclusions only. No proprietary binary, fingerprint frame, template, PSK/OTP, unit-specific runtime configuration, or calibration payload is committed.

## Proven upstream path

```text
EngineAdapter.dll
 -> AlgoChicago.dll preprocessor_wrapper
 -> logical preprocessor routine 0x18000e780..0x18000e947
 -> core orchestrator 0x1800484e0
 -> 0x180044970: source - internal state plane (signed 16-bit difference)
 -> 0x180043c40: mask geometry cleanup + source-range validity filtering
 -> common join at 0x180048a43
 -> 0x18004aea0
```

The alternative helpers `0x1800447d0` and `0x180044010` occur on pre-join branches and are not downstream stages after the tested `0x180043c40` path.

## Exact function boundary

The PE x64 runtime-function table gives:

```text
RVA 0x0004aea0 .. 0x0004b289
size 0x3e9 = 1001 bytes
```

Normal return is at `0x18004b288`.

## Call-site argument mapping

At the proven call site `0x180048b02`:

```text
RCX        = r14
RDX        = [rbp-0x50]
R8         = rbx
R9D        = global/config DWORD at 0x1800ec6c8
stack arg5 = &local at rbp+0x80
stack arg6 = &local at rbp+0x20
stack arg7 = &local at rbp+0x50
stack arg8 = r15
```

Inside `0x18004aea0`, the third register argument (`R8`) is treated as a large state/context object: the routine derives pointers at offsets including `+0x13244` and `+0x4` from it. Exact semantic names for all arguments remain intentionally open.

The seventh argument is a structure whose fields at `+0`, `+4`, `+8`, `+0x0c`, `+0x10`, and `+0x18` control dimensions/modes/selectors. Do not rename these fields until their producers/consumers are fully tied together.

## Three full-size 16-bit scratch planes — PROVEN

The routine multiplies two dimension fields, doubles the product, and performs three allocations of that byte count.

Conceptually:

```text
pixel_count = dimensionA * dimensionB
scratch_bytes = pixel_count * 2
allocate scratch_bytes
allocate scratch_bytes
allocate scratch_bytes
```

Therefore this stage operates heavily on full-image 16-bit planes rather than only scalar quality metadata.

One of the allocated full-size buffers receives a byte-for-byte copy of the original first argument through the common copy helper before further processing.

## Tested type-0x0c branch is active

The function examines a selector field at structure offset `+0x18` against bit mask:

```text
0x02473932
```

Selector `0x0c` is included in that bit set, so the corresponding branch is active for the tested 5135 path.

Within that branch, full-plane WORD loops are visible. One loop scales WORD values in the copied plane and also references a WORD plane relative to the second argument at approximately `+0x9924` (`+0x9922` combined with the loop's post-increment addressing). This strongly ties the stage to calibration/state-derived per-pixel data, but the exact semantic relation is not yet named.

## Integer-only arithmetic in this stage scan

The static scan found no matching SIMD or floating-point instructions in the inspected region. Pixel-like processing is implemented through integer/WORD operations such as:

```text
movzx WORD
add/sub WORD
shift
integer divide
conditional WORD replacement
```

This does **not** mean the operation is not normalization-like; it means any such transformation here is fixed-point/integer rather than float/SIMD.

## Important internal calls

Within the exact `0x18004aea0..0x18004b289` body the important calls are:

```text
0x18004d3b0
0x18004b290   # conditional
0x180049ba0
```

along with allocation/copy/free helpers.

`0x180049ba0` is called before a final full-plane WORD loop. That final loop performs conditional value propagation/replacement and the routine then frees all three temporary full-size buffers and returns `1`.

Therefore `0x18004aea0` is now proven to be a real multi-stage full-image preprocessing/calibration helper, not a logging wrapper or trivial dispatcher.

## What is NOT yet proven

Do not yet call `0x18004aea0` the final image normalizer or final processed-image producer.

The remaining missing proof is the ownership/data flow of the caller-visible output across:

```text
0x18004d3b0
 -> optional 0x18004b290
 -> 0x180049ba0
 -> final WORD loop in 0x18004aea0
```

We still need to prove which caller-owned buffer survives after the three scratch planes are freed, and whether that buffer is the image later consumed by `0x1800435a0` and matcher-facing stages.

## Immediate next task

Before descending into a random child helper, reconstruct the exact middle/final portion of `0x18004aea0`, especially:

```text
0x18004b085 .. 0x18004b288
```

Goals:

1. map every live pointer to its original argument or one of the three temporary planes;
2. identify the exact destination written by `0x180049ba0` and the final WORD loop;
3. determine whether `0x18004d3b0` supplies gain/calibration coefficients, a transformed plane, or metadata;
4. only then choose the next child helper to reverse-engineer in depth.

Separately, the earlier open task remains: prove how the AlgoChicago internal state plane near `state+0x9924` is populated before equating it with the persisted `gfusb.dll` ImageBase.

## Safety boundary

Do not request, publish, commit, upload, or hash real fingerprint frames/templates, PSK/OTP, unit-specific full runtime configuration, `goodix.dat`, `goodix_calib.dat`, `Goodix_Cache.bin`, proprietary Goodix binaries, Windows biometric database material, or process dumps.
