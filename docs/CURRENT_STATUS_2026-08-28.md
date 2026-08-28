# Current status — 2026-08-28

Target: Goodix USB fingerprint sensor `27c6:5135`, firmware `GF_HC460SEC_APP_12508`, logical chip `0x2504`, ChicagoHS / ChicagoHU, sensor family/type `0x0c`.

This is the canonical checkpoint after identifying the Windows biometric layer above `gfusb.dll`, reaching the real Chicago preprocessing implementation, and proving the first exact per-pixel arithmetic used by the tested type-`0x0c` path.

## Plain-language position

The difficult device-communication half is solved. Linux can safely reach the sensor, establish the factory-compatible encrypted session, detect finger state, request a real image, decrypt it, validate it, decode the packed 12-bit samples, and place the image into the same 80x64 downstream layout used by Windows.

The current blocker is now narrower: reproduce the remaining Chicago preprocessing stages, then connect their exact output to identification/enrollment and implement the resulting pipeline under libfprint.

## End-to-end milestone table

```text
Device identity / 27c6:5135 profile                PASS
Factory compatibility constraints                  PASS
USB transport                                      PASS
CFG70 reconstruction                               PASS
Volatile command 0x90 runtime config               PASS
Factory TLS 1.2 PSK session                        PASS
Verified activation sequence                       PASS
FDT manual/down/up                                  PASS (generic FDT-up derivation still open)
Image command 0x20                                 PASS
TLS image transport/decrypt                        PASS
Goodix image framing                               PASS
Windows-compatible image CRC                       PASS on one private capture
RAW12 -> 5120 samples                              PASS
ChicagoHU regroup                                  PROVEN
Live image destination                             PROVEN
ImageBase/live same-index layout                   PROVEN
Candidate A: persisted ImageBase already regrouped PROVEN
Candidate B: regroup persisted ImageBase again     REJECTED
gfusb.dll post-detection result packaging          PROVEN
gfusb.dll -> Windows biometric layer handoff       PROVEN
Windows component above gfusb.dll                  IDENTIFIED
EngineAdapter family 0x0c algorithm selection      PROVEN -> AlgoChicago.dll
AlgoChicago preprocessor export resolution         PROVEN
Real preprocessor implementation entry             PROVEN -> 0x18000e780
Outer preprocessor semantic routine                PROVEN -> 0x18000e780..0x18000e947
Core preprocessing orchestrator                    PROVEN -> 0x1800484e0
First image/state-plane combiner                    PROVEN -> 0x180044970
Type-0x0c first pixel subtraction                  PROVEN
Full normalization/threshold/mask semantics         PARTIAL / IN PROGRESS
Exact matcher input                                 NOT YET PROVEN
Matcher/enrollment                                 NOT YET IMPLEMENTED
libfprint/fprintd integration                       NOT YET IMPLEMENTED
Reboot/suspend/recovery validation                  NOT YET COMPLETE
```

## Windows biometric architecture

The installed package configures Windows Biometric Framework with the Windows built-in SensorAdapter and StorageAdapter plus Goodix `EngineAdapter.dll` as the vendor engine.

For the tested logical chip `0x2504` / family `0x0c`, static EngineAdapter control flow selects:

```text
AlgoChicago.dll
```

EngineAdapter dynamically resolves preprocessing, calibration, sensor-check, identify, enroll and template functions from that DLL.

Therefore final matcher preprocessing is above `gfusb.dll`; the USB driver is no longer the place to search for final pixel arithmetic.

## Exact Chicago outer preprocessor routine

`AlgoChicago.dll` exports `preprocessor_wrapper`, which forwards into the logical routine:

```text
0x18000e780 .. 0x18000e947
```

The real return is at `0x18000e946`. The routine validates state, calls `0x1800484e0`, receives a temporary processed-image object, copies its processed bytes to the caller result descriptor, writes quality/coverage bytes, cleans the temporary object and propagates the helper return code.

The central call is:

```text
0x18000e8ac -> 0x1800484e0
```

## `0x1800484e0` — preprocessing orchestrator

This helper is a 2891-byte orchestrator, not one flat pixel loop.

It proves that the source image is treated as a 16-bit plane whose size must equal:

```text
width * height * 2
```

For 5135 this is consistent with:

```text
80 * 64 * 2 = 10240 bytes
```

It copies that source image into a temporary buffer.

It also copies a second `2 * pixel_count` plane from the internal AlgoChicago preprocessing state at:

```text
state + 0x9924
```

When an internal mode equals `2`, that second plane receives an interpolation pass based on neighboring WORD averages.

Important: this `state+0x9924` plane is proven to be an internal preprocessing/calibration-state plane, but it is **not yet statically proven identical to gfusb.dll's persisted ImageBase**.

## First exact pixel arithmetic — PROVEN

`0x1800484e0` calls:

```text
0x180044970(
    copied_source_u16,
    copied_state_plane_u16,
    output_structure,
    selector
)
```

with:

```text
RCX = copied source image u16 plane
RDX = copied state+0x9924 u16 plane
R8  = output/result structure
R9D = six-bit sensor/algorithm selector
```

For the tested 5135 path the relevant selector is type/family `0x0c`.

At `0x180044ad8`, the helper checks selector `== 4`. Type `0x0c` takes the other branch.

The active type-0x0c vector loop is equivalent to:

```text
diff16[i] = source_u16[i] - state_plane_u16[i]
```

implemented with `psubw`, with an equivalent scalar WORD tail.

There is no clamp, saturation or gain in this subtraction loop.

The 16-bit subtraction wraps naturally, and later code reads the result with `movsx`, proving the produced difference plane is intended to be interpreted as **signed 16-bit**. Negative differences therefore survive in two's-complement form rather than being clipped to zero.

A selector-4-only branch instead applies `source - state_plane + 0x0fff`; that branch is not the tested 5135/type-0x0c path.

See `docs/CHICAGO_PREPROCESS_CORE_2026-08-28.md` for the assembly-level checkpoint.

## What `0x180044970` does after subtraction

Static analysis further proves that the same function:

- computes block/window statistics over the signed difference plane;
- uses 16-sample neighborhoods and integer averages;
- derives minimum/maximum and dynamic threshold-like values;
- partitions pixels relative to a selected threshold;
- generates/updates a byte mask in its result structure;
- computes a percentage-like result field as `count * 100 / pixel_count` at result offset `+0x0e`;
- contains explicit later branches for selector/type `0x0c`;
- frees its temporary difference plane and returns success (`0`).

Exact semantic names for all threshold/mask/result fields remain intentionally open until their consumers are traced.

## Next active stage

After `0x180044970`, the orchestrator calls:

```text
0x180043c40
```

with the copied source image, copied internal state plane, flags/mode, selector, and a copy of the `0x180044970` result structure.

This is the next primary target.

Need to prove:

1. how `0x180043c40` consumes the signed difference-derived mask/statistics;
2. how `state+0x9924` is populated and whether it derives from gfusb ImageBase;
3. normalization/gain/clamp/crop behavior, if present;
4. the exact processed image buffer returned to the outer preprocessor;
5. the exact buffer passed into `identifyImage` and enrollment.

## Outer routine status behavior

- null required input/output arguments -> `0x81`;
- uninitialized preprocessor state -> `0x80`;
- explicit uncalibrated state -> `0x80`;
- processing path propagates the return value from `0x1800484e0`;
- special values such as EngineAdapter's observed `0x84`, and internal `0x7531`, `0x7532`, `0xc351`, remain unnamed until proven.

## What is safe to stop re-investigating

Unless a later dependency contradicts existing proof, do not return to:

- generic USB transport;
- TLS handshake/cipher discovery;
- CFG70 family selection/checksum;
- image command framing;
- RAW12 unpacking;
- ChicagoHU regroup formula;
- Candidate A vs Candidate B orientation;
- gfusb.dll detector arithmetic;
- gfusb.dll post-detection callback `0x180013280`;
- request completion helper `0x18001393c`;
- the outer boundary of `AlgoChicago` preprocessor routine `0x18000e780..0x18000e947`;
- the first type-0x0c subtraction loop in `0x180044970`.

## Definition of project completion

The project is not complete merely when an image can be captured or the first subtraction is known.

Release success requires all of the following:

1. native libfprint driver/state machine for `27c6:5135`;
2. enrollment succeeds repeatedly with local templates only;
3. verification accepts the enrolled finger reliably;
4. verification rejects non-matching fingers at an acceptable rate;
5. cancellation and timeouts do not wedge the USB device;
6. reboot/cold boot works;
7. suspend/resume works;
8. repeated capture/enroll/verify loops do not leak secrets or biometric payloads;
9. Windows Hello still works unchanged after Linux testing;
10. no firmware erase/flash or factory PSK rewrite is required.

See `docs/RELEASE_READINESS_AND_SAFETY_GATES.md` for the explicit completion gates.

## Privacy / publication boundary

Public documentation may contain protocol structure, control-flow conclusions, function identities, arithmetic descriptions and synthetic tests.

Never publish:

- plaintext factory PSK or PSK material/hashes;
- full OTP;
- unit-specific full 224-byte runtime config/hash;
- fingerprint frames/images/templates;
- `goodix.dat`, `goodix_calib.dat`, or `Goodix_Cache.bin`;
- proprietary Goodix DLL/EXE/CAT binaries;
- Windows biometric database material;
- process-memory dumps.

Static analysis is performed locally against the user's installed Windows package; proprietary binaries are not copied into this repository.
