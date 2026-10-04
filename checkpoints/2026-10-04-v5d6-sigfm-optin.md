# Goodix 27c6:5135 — V5D4B/V5D5/V5D6 checkpoint

Date: 2026-10-04

## Current local libfprint state

Repository: `~/libfprint`

Branch: `goodix-27c6-5135-chicagohu`

Current committed HEAD:

`e2c0eb96cb18f6787d4721bb238271a100f15d5f`

Commit subject:

`goodix5135: opt into SIGFM small-area matching`

Expected working tree: CLEAN.

GitHub mirror:

`libfprint/goodix-27c6-5135-e2c0eb96`

## V5D4B banking — COMPLETE

The previously verified cumulative V5D4B host-only SIGFM stack was successfully banked into the real local libfprint branch after disk space became available.

Banked commit:

`8ef22cd196d827d6a7213ed3173c2285961f4247`

All required host gates passed before the commit was created:

- SIGFM robustness: PASS
- SIGFM geometry: PASS
- SIGFM image core: PASS
- SIGFM print core: PASS
- `fpi-device`: PASS
- Goodix regressions: 9/9 PASS

No sensor/hardware access occurred during banking.

## V5D5 — keypoint gate semantics resolved

The previous unresolved `5` versus `25` SIGFM keypoint question is now deliberately resolved.

The two values represent different concepts:

- `25` = minimum extracted keypoints required to accept an image as a usable SIGFM template. This preserves the upstream SIGFM extraction-quality gate.
- `5` = minimum internal correspondence/geometric support used by the SIGFM matcher itself.

The matcher `min_match = 5` remains unchanged.

The image/template quality gate was restored to `25`.

Commit:

`0e7beb4c245b6313bfbeb556c60c2ed47b731a40`

Subject:

`sigfm: restore template quality keypoint gate`

Fresh host verification passed 14/14 targeted tests, including all four SIGFM suites, `fpi-device`, and the nine Goodix regression suites.

## V5D6 — Goodix SIGFM opt-in — COMPLETE host-side

The Goodix5135 driver now explicitly selects the SIGFM image-device algorithm.

Current small-area matching policy:

- algorithm: SIGFM
- initial SIGFM threshold: `12`
- enrollment stages: `30`
- image geometry: `80x64`

The threshold/stage starting point is based on public evidence from the closely related Goodix ChicagoHS/GXFP5130 family using an 80x64 sensor. It is an initial candidate for this 27c6:5135 unit, not a final FAR/FRR characterization.

Commit:

`e2c0eb96cb18f6787d4721bb238271a100f15d5f`

Subject:

`goodix5135: opt into SIGFM small-area matching`

Fresh V5D6 host verification passed 14/14:

- `sigfm-robustness`
- `sigfm-geometry-hardening`
- `sigfm-image-core`
- `sigfm-print-core`
- `fpi-device`
- `goodix5135-conditioning`
- `goodix5135-preprocess`
- `goodix5135-image`
- `goodix5135-proto`
- `goodix5135-image-response`
- `goodix5135-io`
- `goodix5135-request`
- `goodix5135-queue-cleanup`
- `goodix5135-async-dispatch`

Result: 14 passed, 0 failed.

No hardware access occurred during V5D5 or V5D6 host work.

## Runtime-path audit after V5D6

The Goodix SIGFM class configuration was audited before any live biometric run.

The audit confirmed that the implementation intentionally reads SIGFM policy directly from `FpImageDeviceClass` at runtime:

- `FpImageDeviceClass.algorithm` selects SIGFM extraction and print type;
- `FpImageDeviceClass.sigfm_threshold` is passed directly to `fpi_print_sigfm_match()`;
- Goodix5135 sets both fields in its class init;
- the NBIS-only `bz3_threshold` remains in `FpImageDevicePrivate` for the legacy NBIS path.

Therefore the absence of duplicate `algorithm` / `sigfm_threshold` fields in `FpImageDevicePrivate` is not a bug and no extra runtime copy is required.

This audit prevents a false live test in which a class constant might otherwise have been mistaken for an active runtime setting.

## Live-input recovery status

The development machine still contains the earlier local FDT proof script and its local per-unit references.

Only file names, sizes, and redacted references were inspected. No private FDT bytes, PSK contents, fingerprint pixels, templates, or exact biometric scores were printed or committed.

Important findings:

- the previously used local TLS key file still exists;
- the old `fdt_probe_5135_v5.py` script still contains references to the proven local `goodix.dat` source and the proven unit-specific FDT-up calibration;
- no standalone 12-byte FDT-up artifact was found, so the proven calibration should be reconstructed locally from the old private script rather than replaced with a guessed generic derivation;
- existing local fingerprint image artifacts were intentionally not opened or analyzed during this recovery step.

The first attempt to prepare ephemeral runtime FDT files stopped before any hardware activity because the system Python environment did not include the local Goodix dependencies. No USB transaction occurred.

The Goodix development virtual environment itself is still healthy and imports its USB dependency successfully.

## Live-runtime readiness

The development build contains the existing guarded native TLS/FDT/image/FpImage path. The SIGFM matcher helper does not log exact SIGFM scores.

No live SIGFM fingerprint run has been performed yet.

## Next action

Prepare the existing guarded live FpImage path using the proven locally held private inputs without exposing them, then run a minimal one-stage SIGFM smoke test before full enrollment.

The preferred first proof is:

`USB -> TLS -> FDT -> image decode -> FpImage -> SIGFM extraction`

The smoke test should:

- require only one successful finger placement if possible;
- persist no fingerprint image or template;
- print no exact live keypoint/minutiae count or SIGFM score;
- clear temporary biometric/FDT material after completion;
- preserve the already-proven FDT/manual/up behavior;
- leave firmware, PSK provisioning, and Windows Hello state untouched.

After the smoke test passes, proceed to a controlled 30-stage SIGFM enrollment and genuine/impostor verification trials to calibrate threshold `12` for this exact unit.

Then move toward ordinary `fprintd-enroll` / `fprintd-verify` integration and the reboot/suspend/cancellation safety matrix.

## Hard safety/privacy rules

Never print, persist to Git, upload, publish, or hash sensitive device/biometric material including plaintext PSK, PSK files/hashes, full OTP, fingerprint images/raw/templates, unit-specific FDT values, Goodix cache/calibration data, proprietary Goodix binaries, Windows biometric DB, process dumps, or full unit-specific runtime config/hash.

Never print exact live biometric scores or exact live feature counts.

No firmware erase/flash, PSK rewrite/reprovision, arbitrary persistent sensor writes, or Windows enrollment deletion shortcuts.
