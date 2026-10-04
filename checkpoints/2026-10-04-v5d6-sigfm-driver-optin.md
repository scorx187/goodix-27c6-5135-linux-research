# V5D6 — Goodix 27c6:5135 SIGFM driver opt-in

Date: 2026-10-04

## Result

The previously hardened SIGFM host stack is now banked into the real local libfprint branch, the extraction-quality gate has been restored to its intended value, and the Goodix 27c6:5135 driver explicitly opts into SIGFM.

No live fingerprint enrollment or verification was performed during V5D4B banking, V5D5, or V5D6.

## Banked libfprint progression

Local libfprint branch:

`goodix-27c6-5135-chicagohu`

Banked commits:

- V5D4B banked stack: `8ef22cd196d827d6a7213ed3173c2285961f4247`
- V5D5 keypoint-gate correction: `0e7beb4c245b6313bfbeb556c60c2ed47b731a40`
- V5D6 Goodix SIGFM opt-in: `e2c0eb96cb18f6787d4721bb238271a100f15d5f`

Each commit was mirrored to GitHub under a SHA-named `libfprint/goodix-27c6-5135-*` branch.

## V5D4B banking

The previous disk-space blocker is resolved.

The guarded banking run completed successfully and the hardened SIGFM stack was committed to the real libfprint branch.

Host regression result after banking:

- SIGFM geometry: PASS
- SIGFM robustness: PASS
- SIGFM image core: PASS
- SIGFM print core: PASS
- libfprint `fpi-device`: PASS
- Goodix regressions: 9/9 PASS

## V5D5 — separate extraction quality from matcher geometry

The earlier `5 vs 25` question was resolved by separating two different concepts:

- `25` is the minimum extracted SIGFM keypoint count required to accept a template as usable.
- `5` remains the matcher-internal minimum geometric correspondence requirement.

They are not interchangeable thresholds.

The SIGFM extraction-quality gate was restored to 25 while the internal matcher minimum remains unchanged.

Host result: 14/14 selected regression targets PASS.

## V5D6 — Goodix driver opts into SIGFM

The Goodix 27c6:5135 image-device class now explicitly configures:

- algorithm: SIGFM
- initial SIGFM match threshold: 12
- enrollment stages: 30

The previous Goodix Bozorth placeholder threshold was removed from the driver class.

The threshold value 12 is a conservative initial bring-up candidate based on a closely related ChicagoHS 80x64 implementation with published same-finger/impostor observations. It is not considered final calibration for this exact 27c6:5135 unit until live measurements are completed.

Thirty enrollment stages are used because the 80x64 sensing area captures only a small part of the finger per touch and needs broad coverage.

Host result after the driver opt-in: 14/14 selected regression targets PASS, including Goodix 9/9.

## Runtime-path audit

A post-V5D6 audit confirmed that SIGFM selection and threshold lookup are intentionally class-based at runtime:

- extraction chooses SIGFM from `FpImageDeviceClass.algorithm`
- matching uses `FpImageDeviceClass.sigfm_threshold`
- Goodix sets both fields in its class init

The lack of a duplicate SIGFM threshold field inside `FpImageDevicePrivate` is therefore not a bug.

## Live-test preparation

The earlier local FDT proof script and private local inputs still exist on the development machine. Only metadata/existence was inspected; private PSK/FDT values were not printed, copied to GitHub, hashed, or exposed in logs.

The previous proven FDT-up calibration is still the preferred source. Do not replace it with an unproven generic derivation simply to make a live test easier.

## Next step

Prepare an ephemeral local-only runtime input directory, then run a minimal live SIGFM smoke test before attempting a full 30-stage enrollment.

The first live smoke test should prove only:

USB -> TLS -> FDT -> image decode -> FpImage -> SIGFM extraction

It should:

- require only one successful finger placement if possible
- persist no fingerprint image or template
- print no exact SIGFM score/keypoint count
- wipe temporary biometric/FDT material after completion
- leave Windows Hello provisioning untouched

Only after that smoke test passes should full SIGFM enrollment/verify calibration begin.

## Safety rules retained

Never publish or commit plaintext PSK, PSK hashes, full OTP, private config, Goodix factory data, FDT unit-specific values, fingerprint images, templates, exact live biometric scores, Windows biometric DB material, or proprietary Goodix binaries.

Do not erase/flash firmware, rewrite PSK, delete Windows enrollments, or perform arbitrary persistent sensor writes.
