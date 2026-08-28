# Current status — 2026-08-28

Target: Goodix USB fingerprint sensor `27c6:5135`, firmware `GF_HC460SEC_APP_12508`, logical chip `0x2504`, ChicagoHS / ChicagoHU, sensor family/type `0x0c`.

This is the canonical high-level checkpoint for the Linux enablement effort. Static Windows reverse engineering is being used only to reproduce compatible behavior; no proprietary Goodix binaries, biometric samples, or device secrets are committed.

## Plain-language position

The difficult transport half is solved: Linux can safely reach the sensor, establish the factory-compatible TLS session, detect finger state, request/decrypt/validate an image, decode RAW12 and reproduce the Windows-compatible 80x64 downstream layout.

The current work is inside `AlgoChicago.dll` preprocessing. The main correction pipeline is substantially reconstructed: initial signed state/source difference, mask cleanup, Q13 correction surfaces, adaptive ratio correction, a separable median stage, persistent corrected plane at `state+0x13244`, and a gated late-factor learner. The immediate open transform is the generic mode-9 operation configured by `0x18004e820` and executed through `0x18004fff0`.

## Milestones

```text
Device identity / 27c6:5135 profile                PASS
Factory compatibility constraints                  PASS
USB transport                                      PASS
CFG70 reconstruction                               PASS
Volatile command 0x90 runtime config               PASS
Factory TLS 1.2 PSK session                        PASS
Activation sequence                                PASS
FDT manual/down/up                                  PASS on tested unit
Image command 0x20                                 PASS
TLS image transport/decrypt                        PASS
Goodix framing / CRC                               PASS on one private capture
RAW12 -> 5120 samples                              PASS
ChicagoHU regroup                                  PROVEN
ImageBase/live same-index layout                   PROVEN
Candidate A                                        PROVEN
Candidate B double-regroup                         REJECTED
gfusb post-detection packaging                     PROVEN
WBDI / EngineAdapter layer                         PROVEN
family 0x0c -> AlgoChicago.dll                     PROVEN
outer preprocessor 0x18000e780..0x18000e947        PROVEN
core orchestrator 0x1800484e0                      PROVEN
state-source signed subtraction 0x180044970        PROVEN
mask cleanup/source filter 0x180043c40              PROVEN
post-mask correction parent 0x18004aea0             PROVEN
Q13 correction surfaces                            PROVEN
base adaptive update 0x18004d6f0                  PROVEN
separable median filter 0x18004c3b0                PROVEN
gated factor updater 0x18004b460                  PROVEN at parent level
0x18004e110 wrapper role                           PROVEN
mode-9 context builder 0x18004e820                 CURRENT
mode-9 executor path in 0x18004fff0                NEXT
exact matcher input                                NOT YET PROVEN
matcher/enrollment                                 NOT YET IMPLEMENTED
libfprint/fprintd                                  NOT YET IMPLEMENTED
lifecycle/stress safety matrix                     NOT YET COMPLETE
```

## Critical arithmetic correction — `0x180044970`

For selector/type `0x0c`, the exact first difference is:

```text
diff16[i] = state_plus_0x9924_u16[i] - source_u16[i]
```

The direction is proven twice.

SIMD:

```asm
movdqu xmm1,[state_plane + index*2]
movdqu xmm0,[source      + index*2]
psubw  xmm1,xmm0
```

Scalar tail likewise loads the state WORD then subtracts the source WORD.

The result wraps as 16-bit arithmetic and is later sign-extended, so it is intentionally interpreted as signed `s16`.

Selector `4` uses the separate relation:

```text
state_plane - source + 0x0fff
```

The tested 5135/type-`0x0c` path does not use that offset branch.

Important: `state+0x9924` is proven only as an AlgoChicago internal preprocessing/calibration-state plane. It is **not yet proven identical to gfusb persisted ImageBase**.

## Mask stage `0x180043c40`

This stage performs mask geometry cleanup/hole filling, source-range validity filtering and coverage recomputation. For selector `0x0c`, an active mask pixel can survive only when:

```text
100 < source_u16[i] < 3800
```

It does not perform another image subtraction or normalization.

## Post-mask Q13 correction

`0x18004aea0` allocates three temporary u16 planes and invokes `0x180049ba0` to construct correction surfaces.

For selector `0x0c`:

```text
scratch_C[i] = 3 * source_side_word[i]
global_work[i] = 3 * state_plus_0x9924[i]
```

`scratch_A` is the primary Q13 denominator/correction surface and starts at:

```text
0x2000 == 8192 == Q13 unity
```

The exact composition primitive is:

```text
Q13_mul(a,b) = (a*b + 0x1000) >> 13
```

The parent finally writes the persistent corrected plane at:

```text
state + 0x13244
```

using type-`0x0c` shift 14:

```text
if gate_word[i] != 0:
    if scratch_A[i] == 0:
        processed[i] = scratch_C[i] << 14
    else:
        processed[i] = round((scratch_C[i] << 14) / scratch_A[i])
else:
    processed[i] = scratch_C[i]
```

This is proven as a corrected image-like plane, but not yet proven to be the final matcher input.

## Base adaptive Q13 update

`0x18004d6f0` forms:

```text
ratio_raw[i] =
    reference[i] << 13                              if scratch_A[i] == 0
    round((reference[i] << 13) / scratch_A[i])     otherwise
```

`0x18004c3b0` applies a separable median-of-three filter:

```text
H(y,x)   = median(src[y][x-1], src[y][x], src[y][x+1])
out(y,x) = median(H[y-1][x], H[y][x], H[y+1][x])
```

with borders copied unchanged.

For selector `0x0c`, if filtered ratio `q` and work value `x` are nonzero and:

```text
abs(q-x) > 1800
```

then:

```text
scratch_A[i] = min(round((scratch_A[i] * q) / x), 0x7fff)
```

## Gated late-factor learning

`0x1800497c0` is proven at the caller level to gate an optional branch. If enabled, `0x18004b460` runs twice with `R8 = scratch_A`.

`0x18004b460` does not write `scratch_A` directly. It builds Q13 ratios and adaptively updates late per-pixel factor surfaces which are later multiplied into `scratch_A` / `scratch_B`.

After its child transform, for each pixel:

```text
local_ratio = round((temp_filtered[i] << 13) / work[i])
```

with `0x2000` used when the denominator is zero. A factor sample is accepted only when:

```text
abs(local_ratio - 0x2000) < 0x148
```

and the factor uses a rounded running average with observation count capped at 30.

## `0x18004e110` — generic wrapper, not the filter

`0x18004e110` contains no pixel loop. It validates/reconciles two image objects, temporarily adjusts format/stride metadata, constructs an operation context and dispatches the actual transform.

On the current path:

```text
0x18004e820(..., mode=9, arg5=-1, arg6=-1) -> ctx
0x18004fff0(ctx, ratio_object, output/work_object)
```

For mode 9, `ctx+0x88 = 0`; that flag is set to 1 only for modes 6 or 8. Therefore constant `9` is currently only a proven operation/mode selector, **not** a proven 9x9 window.

See `docs/CHICAGO_E110_WRAPPER_TO_FFF0_2026-08-28.md`.

## Immediate task

Reverse `0x18004e820` first because it is small and determines the context consumed by the executor. For the exact caller values, prove:

1. interpretation of the packed two-DWORD value returned by `0x1800501a0`;
2. fields/callbacks/coefficients installed into the context;
3. exact branch selected by mode 9;
4. which execution path that selects inside `0x18004fff0`.

Then trace only that selected `0x18004fff0` path to recover the transform reaching the near-unity gate in `0x18004b460`.

After closing the correction pipeline, trace `state+0x13244` to matcher/enrollment-facing processing and independently prove the producer of `state+0x9924`.

## Completion / safety gates

Release is not complete until native libfprint enrollment and verification work reliably; cancellation/timeouts recover; cold boot and suspend/resume pass; repeated use does not leak secrets or biometric payloads; and Windows Hello plus existing Windows fingerprints continue to work unchanged. No firmware erase/flash or factory PSK rewrite is permitted.

Never publish or request plaintext PSK/hashes, full OTP, unit-specific full runtime config/hash, fingerprint images/raw/templates, `goodix.dat`, `goodix_calib.dat`, `Goodix_Cache.bin`, proprietary Goodix binaries, Windows biometric DB material, or process dumps.
