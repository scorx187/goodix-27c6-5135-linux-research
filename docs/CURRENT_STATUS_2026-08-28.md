# Current status — 2026-08-28

Target: Goodix USB fingerprint sensor `27c6:5135`, firmware `GF_HC460SEC_APP_12508`, logical chip `0x2504`, ChicagoHS / ChicagoHU, sensor family/type `0x0c`.

This is the canonical checkpoint after identifying the Windows biometric layer above `gfusb.dll`, reaching the real Chicago preprocessing implementation, and proving the complete outer control flow of `preprocessor_wrapper`.

## Plain-language position

The difficult device-communication half is already solved. Linux can safely reach the sensor, establish the factory-compatible encrypted session, detect finger state, request a real image, decrypt it, validate it, decode the packed 12-bit samples, and place the image into the same 80x64 downstream layout used by Windows.

The current blocker is no longer USB/TLS/image transport. It is reproducing the Windows image preprocessing/matcher behavior well enough for reliable enrollment and verification under libfprint.

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
Core preprocessing helper                          IDENTIFIED -> 0x1800484e0
Exact preprocessing arithmetic                     NOT YET PROVEN
Matcher/enrollment                                 NOT YET IMPLEMENTED
libfprint/fprintd integration                       NOT YET IMPLEMENTED
Reboot/suspend/recovery validation                  NOT YET COMPLETE
```

## Windows biometric architecture now identified

The installed Goodix package configures the Windows Biometric Framework as:

```text
Windows built-in SensorAdapter
        |
        v
EngineAdapter.dll       (Goodix vendor engine adapter)
        |
        +-- AlgoMilan.dll
        +-- AlgoChicago.dll
        +-- AlgoChicagoT.dll
```

For the tested `0x2504` / family `0x0c` device, static EngineAdapter control flow selects `AlgoChicago.dll`.

EngineAdapter dynamically resolves algorithm functions with `LoadLibraryW` / `GetProcAddress`, including:

```text
preprocessor_wrapper
preprocessor_init_wrapper
preprocessor_exit_wrapper
preprocess_get_calidata_len_wrapper
preprocess_init_calidata_wrapper
preprocess_load_calidata_wrapper
preprocess_save_calidata_wrapper
gx_sensorCheckWrapper
identifyImageWrapper
enrolAddImageWrapper
```

This proves preprocessing/calibration/matching are performed above `gfusb.dll`, inside the Goodix algorithm layer rather than the USB transport driver.

## Exact Chicago preprocessor outer routine — proven

`AlgoChicago.dll` exports:

```text
preprocessor_wrapper RVA 0x0000b560
```

The export is an ABI forwarding shim into:

```text
0x18000e780
```

The first x64 unwind record covers only `0x18000e780..0x18000e880`, but direct control flow continues through adjacent compiler-split runtime-function regions:

```text
0x18000e780 .. 0x18000e880
0x18000e880 .. 0x18000e8e0
0x18000e8e0 .. 0x18000e91a
0x18000e91a .. 0x18000e947
```

The logical routine terminates at the real `ret` at `0x18000e946`. Therefore the complete outer preprocessing routine is now proven as:

```text
0x18000e780 .. 0x18000e947
```

The adjacent routines beginning at `0x18000e950`, `0x18000e990`, and `0x18000e9f0` are separate functions and must not be merged into the preprocessor body merely because they are nearby in the file.

## What the outer routine actually does

The seven-argument outer routine performs validation, builds the parameter set for the core preprocessing helper, receives a temporary processed-image object, copies the processed bytes to the caller's output descriptor, writes quality/coverage metadata, cleans the temporary object, and returns the helper status.

The central call is:

```text
0x18000e8ac -> 0x1800484e0
```

Therefore `0x1800484e0` is the immediate current reverse-engineering target for exact pixel/calibration arithmetic.

### Proven outer-argument structure facts

Without yet assigning all seven arguments higher-level semantic names, static field use proves:

- argument 1 is a source-image-descriptor-like object:
  - `+0x00` is a data pointer passed to `0x1800484e0`;
  - `+0x14` is a size/count passed as `r8d` to `0x1800484e0`;
- argument 4 is an output/result-image-descriptor-like object:
  - `+0x00` is the destination buffer pointer;
  - `+0x14` is the number of processed bytes copied into that destination;
  - `+0x28` receives quality as one byte;
  - `+0x29` receives coverage as one byte;
- argument 5 points to a two-DWORD quality/coverage result pair:
  - `+0x00` is coverage;
  - `+0x04` is quality;
- argument 7 is a byte mode flag; value `1` selects internal mode value `0`, otherwise the outer routine selects internal mode value `2`;
- argument 6 has not yet been given a semantic role from this outer routine and must remain unnamed until proven.

The quality/coverage ordering is proven by the diagnostic call using the format string `preprocessor: quality %d, coverage %d`: the first formatted value is read from argument5+4 and the second from argument5+0.

### Core-helper call shape proven from the outer routine

Immediately before `0x1800484e0`, the outer routine supplies:

```text
RCX = address of local temporary processed-image object
RDX = argument1->data
R8D = argument1->size/count
R9  = global calibration/preprocessor state at 0x18009aa00

stack arg 5  = packed global preprocessing configuration bits
stack arg 6  = address of global calibrated-state flag 0x18009a9f4
stack arg 7  = global value 0x18009a9dc
stack arg 8  = argument5 + 4   (quality output)
stack arg 9  = argument5       (coverage output)
stack arg 10 = original argument2
stack arg 11 = original argument3
stack arg 12 = 0
```

These names intentionally stop at what is statically proven. Original arguments 2 and 3 still require data-flow tracing through `0x1800484e0` before being labeled ImageBase/live/calibration/etc.

### Processed output path

After `0x1800484e0` returns:

1. its return code is preserved as the outer routine's result;
2. the temporary result pointer is checked;
3. processed data is obtained from temporary-object field `+0x18`;
4. exactly `argument4->+0x14` bytes are copied byte-for-byte into `argument4->+0x00`;
5. temporary result state is cleaned through `0x180043420`;
6. quality and coverage are written into `argument4+0x28/+0x29`.

Thus the outer routine is not itself the main pixel loop. The pixel/calibration transformation is below it, principally in or below `0x1800484e0`.

## Outer routine status behavior proven so far

- null required input/output arguments reach an error path returning `0x81`;
- uninitialized preprocessor state reaches a diagnostic path returning `0x80`;
- uncalibrated state reaches the explicit `preprocessor is not calibrated` path returning `0x80`;
- on the processing path, the return value from `0x1800484e0` is propagated to the caller;
- EngineAdapter's observed special result `0x84` remains intentionally unnamed until its producer and semantics are proven below the core helper.

## Immediate technical objective

Do **not** spend more time expanding `0x18000e780`; its outer semantic role and real end are now established.

The immediate target is:

```text
AlgoChicago.dll 0x1800484e0
```

Trace its complete reachable control flow and prove how these inputs are consumed:

1. source data pointer and size;
2. global calibration/preprocessor state;
3. packed preprocessing configuration;
4. calibrated-state pointer;
5. quality/coverage outputs;
6. original outer arguments 2 and 3;
7. temporary processed-image object.

Then identify the first exact pixel-wise/calibration operation and prove, if present:

- which plane is ImageBase and which is current live image;
- subtraction direction;
- signed/unsigned handling;
- clamp/saturation;
- gain/normalization;
- per-pixel calibration/noise correction;
- crop/grow/geometry changes;
- quality and coverage calculations;
- exact matcher input buffer.

## What is already safe to stop re-investigating

Unless a later dependency contradicts the proof, do not spend more time on:

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
- the outer boundary of `AlgoChicago` preprocessor routine `0x18000e780..0x18000e947`.

## Definition of project completion

The project is not complete merely when an image can be captured.

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

Public repository documentation may contain protocol structure, control-flow conclusions, function identities, and synthetic tests.

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
