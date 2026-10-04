# Goodix 27c6:5135 — V5D7 guarded live SIGFM smoke harness

Date: 2026-10-04

## Result

A dedicated one-stage live SIGFM smoke harness is now part of the libfprint development branch.

The purpose is to prove the complete live path with one successful finger placement before attempting a full 30-stage enrollment:

`USB -> TLS -> FDT -> image decode -> FpImage -> SIGFM extraction`

The helper deliberately cancels enrollment immediately after the first successful enrollment stage. It does not serialize a template, save an image, or print biometric scores.

## Commit

Local libfprint branch:

`goodix-27c6-5135-chicagohu`

Commit:

`ceac0b016e13588f1d509f8b6a7d284308443322`

Subject:

`goodix5135: add guarded one-stage SIGFM smoke harness`

GitHub mirror:

`libfprint/goodix-27c6-5135-ceac0b01`

Expected local working tree: CLEAN.

## Files

Added:

- `examples/goodix5135-sigfm-smoke.c`

Updated:

- `examples/meson.build`

## Guard behavior

The helper refuses to run unless the existing research runtime gates are explicitly present.

A no-guard execution was tested and produced the expected refusal before device discovery/open:

`LIVE_SIGFM_SMOKE=REFUSED_MISSING_GUARDS`

Exit code: `2`

Result:

`NO_GUARD_SAFETY=PASS`

Therefore merely building or accidentally invoking the helper without the explicit live environment does not touch the sensor.

## Privacy behavior

The helper:

- does not save a fingerprint image;
- does not serialize or persist the partial enrollment template;
- does not print exact SIGFM scores;
- does not print exact live keypoint/minutiae counts;
- cancels immediately after the first accepted enrollment stage;
- uses only the already-existing guarded TLS/FDT runtime path.

## Host verification

After adding the smoke harness, the selected host regression matrix passed again:

- SIGFM geometry hardening: PASS
- SIGFM robustness: PASS
- SIGFM image core: PASS
- SIGFM print core: PASS
- `fpi-device`: PASS
- Goodix5135 conditioning: PASS
- Goodix5135 preprocess: PASS
- Goodix5135 image: PASS
- Goodix5135 protocol: PASS
- Goodix5135 image response: PASS
- Goodix5135 I/O: PASS
- Goodix5135 request: PASS
- Goodix5135 queue cleanup: PASS
- Goodix5135 async dispatch: PASS

Result: **14/14 PASS**.

`git diff --check`: PASS before commit.

## Hardware status

No live fingerprint was captured during V5D7 implementation or host verification.

The live smoke harness has not yet been run with the private runtime inputs.

## Live-input recovery status

The machine still contains the earlier local proof material needed to prepare the guarded runtime:

- the previously used local TLS key reference exists;
- the old V5 FDT proof script retains the proven local `goodix.dat` reference and the proven per-unit FDT-up calibration;
- private values were not printed, hashed, uploaded, or committed;
- existing local fingerprint image artifacts were not opened or analyzed.

A standalone 12-byte FDT-up artifact was not found, so the correct next step is to reconstruct the temporary runtime input locally from the already-proven private V5 proof source rather than invent a new threshold derivation.

## Next action

1. Create ephemeral local-only FDT runtime files from the proven V5 proof source without printing their bytes.
2. Point the existing guarded runtime environment at those ephemeral files and the existing local TLS key reference.
3. Run `examples/goodix5135-sigfm-smoke`.
4. Ask for one finger placement only.
5. Require `LIVE_SIGFM_FIRST_STAGE=PASS` and final `LIVE_SIGFM_SMOKE=PASS`.
6. Clear the ephemeral FDT material after the run.
7. Only after this passes, proceed to a controlled 30-stage enrollment and genuine/impostor threshold calibration.

## Safety rules

Do not publish or commit private PSK/FDT/OTP/config/biometric material. Do not print exact live feature counts or match scores. Do not erase/flash firmware, rewrite PSK/provisioning, perform arbitrary persistent writes, or alter Windows Hello enrollment state.
