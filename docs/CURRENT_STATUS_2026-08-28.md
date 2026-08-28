# Current status — 2026-08-28

Target: Goodix USB fingerprint sensor `27c6:5135`, firmware `GF_HC460SEC_APP_12508`, logical chip `0x2504`, ChicagoHS / ChicagoHU, sensor family/type `0x0c`.

This is the canonical checkpoint after identifying the Windows biometric layer above `gfusb.dll` and reaching the real Chicago preprocessing implementation.

## Plain-language position

The difficult device-communication half is already solved. Linux can safely reach the sensor, establish the factory-compatible encrypted session, detect finger state, request a real image, decrypt it, validate it, decode the packed 12-bit samples, and place the image into the same 80x64 downstream layout used by Windows.

The current blocker is no longer USB/TLS/image transport. It is reproducing the Windows image preprocessing/matcher behavior well enough for reliable enrollment and verification under libfprint.

## End-to-end milestone table

```text
Device identity / 27c6:5135 profile               PASS
Factory compatibility constraints                 PASS
USB transport                                     PASS
CFG70 reconstruction                              PASS
Volatile command 0x90 runtime config              PASS
Factory TLS 1.2 PSK session                       PASS
Verified activation sequence                      PASS
FDT manual/down/up                                 PASS (generic FDT-up derivation still open)
Image command 0x20                                PASS
TLS image transport/decrypt                       PASS
Goodix image framing                              PASS
Windows-compatible image CRC                      PASS on one private capture
RAW12 -> 5120 samples                             PASS
ChicagoHU regroup                                 PROVEN
Live image destination                            PROVEN
ImageBase/live same-index layout                  PROVEN
Candidate A: persisted ImageBase already regrouped PROVEN
Candidate B: regroup persisted ImageBase again    REJECTED
gfusb.dll post-detection result packaging         PROVEN
gfusb.dll -> Windows biometric layer handoff      PROVEN
Windows component above gfusb.dll                 IDENTIFIED
EngineAdapter family 0x0c algorithm selection     PROVEN -> AlgoChicago.dll
AlgoChicago preprocessor export resolution        PROVEN
Real preprocessor implementation entry            PROVEN -> 0x18000e780
Full semantic preprocessor control-flow           IN PROGRESS
Exact preprocessing arithmetic                    NOT YET PROVEN
Matcher/enrollment                                NOT YET IMPLEMENTED
libfprint/fprintd integration                      NOT YET IMPLEMENTED
Reboot/suspend/recovery validation                 NOT YET COMPLETE
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

## Exact Chicago preprocessor entry reached

`AlgoChicago.dll` exports:

```text
preprocessor_wrapper RVA 0x0000b560
```

The wrapper is only an ABI forwarding shim. It forwards seven arguments to:

```text
0x18000e780
```

Therefore `0x18000e780` is the current real preprocessing implementation entry being traced.

The wrapper/callsites prove the function is used in multiple real EngineAdapter runtime paths, so this is not a dead or unused export.

## Important correction about PE unwind boundaries

The first automatic boundary probe found a `RUNTIME_FUNCTION` region:

```text
0x18000e780 .. 0x18000e880
```

However code inside that region branches to at least:

```text
0x18000e91a
0x18000e92e
```

Therefore that unwind record must **not** be treated as the complete semantic function boundary. The compiler may split one logical routine across additional runtime-function/funclet regions.

The next reverse-engineering step is to build the complete reachable control-flow graph beginning at `0x18000e780`, following all direct branch targets until the true returns are reached.

Do not infer the preprocessing algorithm from the first 0x100-byte fragment alone.

## Facts proven from the first preprocessing fragment

The first fragment already proves several useful behaviors without yet exposing the pixel arithmetic:

- null/invalid argument checks occur before processing;
- preprocessing requires initialized global state;
- preprocessing requires calibrated state;
- the explicit diagnostic `preprocessor is not calibrated` is reachable from this implementation;
- failure code `0x80` is returned by at least initialization/calibration guard paths;
- the function consumes a seven-argument interface;
- one byte argument changes an internal mode value between `0` and `2`;
- the actual pixel/calibration work continues beyond the first unwind region and remains to be traced.

The special `0x84` result observed by EngineAdapter remains intentionally unnamed until its producer/meaning is proven from the algorithm code.

## Immediate technical objective

Trace the complete control-flow reachable from `0x18000e780`, then prove the meaning of the seven arguments and identify the exact structures/buffers corresponding to:

1. persisted ImageBase;
2. current regrouped live image;
3. output/processed image;
4. sensor metadata/type;
5. calibration state/data;
6. quality/coverage outputs;
7. preprocessing mode flags.

After those identities are proven, derive the exact Windows preprocessing operations, including if present:

- baseline subtraction direction;
- signed/unsigned handling;
- clamp/saturation;
- gain/normalization;
- per-pixel calibration/noise correction;
- image growth/crop/geometry changes;
- quality and coverage calculations;
- exact buffer passed to `identifyImageWrapper` / enrollment.

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
- request completion helper `0x18001393c`.

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
