# Developer roadmap

## Phase 1 — device identity / factory compatibility — DONE

- `27c6:5135`
- `GF_HC460SEC_APP_12508`
- logical chip `0x2504`
- ChicagoHS / ChicagoHU
- sensor family/type `0x0c`
- 5120 pixels
- packed transport 64 fast x 80 slow
- downstream plane 80 columns x 64 rows
- preserve Windows Hello / factory PSK / firmware

## Phase 2 — runtime config — DONE for parity/upload

- exact 224-byte command `0x90`
- CFG70 selection proven
- OTP-derived runtime reconstruction proven
- checksum proven
- private exact Windows runtime parity proven
- one controlled Linux upload accepted

Optional/later: prove whether any naturally missing runtime-config cold-boot state requires explicit Linux bootstrap on other units/boot scenarios.

## Phase 3 — TLS — DONE

- TLS 1.2 PSK
- `PSK-AES128-GCM-SHA256`
- factory key used locally, never reprovisioned
- encrypted NOP works

## Phase 4 — finger detection — DONE on tested unit

- FDT manual baseline `0x36`
- FDT-down `0x32`
- FDT-up `0x34`
- finite event timeouts

Remaining portability item: generic FDT-up threshold derivation is not yet proven; current success used a private Windows-traced per-unit up calibration.

## Phase 5 — image transport — DONE

- command `0x20`, payload `01 00`
- normal ACK
- TLS frame flags `0xb0`
- decrypted Goodix image message
- no-checksum trailer `0x88`

## Phase 6 — image validation / decode / layout — DONE

- split metadata / packed RAW12 / CRC;
- Windows-compatible CRC-32/MPEG-2 rule proven on one private capture;
- 6-byte -> 4x12-bit unpack proven;
- exactly 5120 samples;
- ChicagoHU regroup mapping proven;
- downstream 80x64 `u16` layout proven;
- live-buffer destination proven;
- ImageBase/live same-index layout proven;
- Candidate A proven;
- Candidate B double-regroup rejected.

Still desirable before release: confirm the image CRC rule on additional independent private captures.

## Phase 7 — Windows preprocessing / calibration — IN PROGRESS

The Windows layer above `gfusb.dll` is now identified.

For the tested family/type `0x0c`:

```text
gfusb.dll
  -> Windows Biometric Framework sensor path
  -> EngineAdapter.dll
  -> AlgoChicago.dll
```

EngineAdapter dynamically resolves the Chicago preprocessing and matching exports.

`AlgoChicago.dll`:

```text
preprocessor_wrapper RVA 0x0000b560
    -> real implementation entry 0x18000e780
```

The wrapper is proven live through multiple EngineAdapter runtime call sites and forwards seven arguments.

Current task:

1. build the complete reachable control-flow graph from `0x18000e780`;
2. do not treat a single x64 unwind `RUNTIME_FUNCTION` record as the whole semantic function because observed branches leave the first region;
3. prove the meaning of all seven arguments;
4. identify ImageBase/live/output/calibration/quality structures;
5. recover only the preprocessing behavior required for an interoperable Linux implementation.

Need exact proof, if present, for:

- baseline subtraction direction;
- signedness;
- clamp/saturation;
- normalization/gain scaling;
- per-pixel calibration/noise correction;
- crop/grow/output dimensions;
- quality/coverage calculation;
- role of calibration data;
- exact buffer passed into identification/enrollment.

## Phase 8 — matcher / enrollment / verification

After the required preprocessing is reproduced:

- determine the minimum matcher/template behavior needed by libfprint;
- prefer an open/libfprint-compatible matcher where possible;
- review license/provenance before reusing code from other implementations;
- keep all real biometric fixtures private/local;
- use synthetic/public deterministic fixtures for repository tests;
- validate positive and negative verification behavior.

## Phase 9 — native libfprint/fprintd driver

Implement a native driver/state machine with:

- USB VID/PID detection;
- safe activation;
- runtime-config bootstrap where required;
- factory TLS use without PSK rewrite;
- cancellation-safe FDT waits;
- image capture/CRC/decode/regroup;
- preprocessing/matcher integration;
- finite timeouts;
- disconnect/re-enumeration recovery;
- suspend/resume recovery;
- enrollment/verify integration;
- no logging of secrets/biometric payloads.

## Phase 10 — release and safety validation

Run the full matrix in `docs/RELEASE_READINESS_AND_SAFETY_GATES.md`:

- cold boot;
- reboot;
- suspend/resume;
- repeated capture/verify/enroll;
- cancellation/error recovery;
- malformed-frame synthetic tests;
- no memory/state drift;
- final privacy/logging audit;
- Windows Hello coexistence after Linux testing.

## Definition of success

Research-complete:

- required Windows-compatible image/preprocessing path is understood and reproducible without proprietary runtime DLLs.

Driver-complete:

- `fprintd-enroll` and `fprintd-verify` work reliably on `27c6:5135` under Linux.

Release-ready:

- all release/safety gates pass, including reboot/suspend/error recovery and Windows Hello continuing to work unchanged.

No claim of universal or mathematical "100% safety" should be made. The engineering target is: **all defined safety gates passed and no known unsafe behavior**.
