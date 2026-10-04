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

## Live-runtime readiness

The development build contains the existing guarded native FpImage/TLS/FDT/image pipeline. The SIGFM matcher helper does not log exact SIGFM scores.

The three private runtime input environment variables required by the existing guarded live path were not set in the Harb Agent process environment during this checkpoint. No attempt was made to discover, print, upload, hash, or otherwise expose their private contents automatically.

Therefore no live SIGFM fingerprint run has been performed yet.

## Next action

Prepare the existing guarded live FpImage path using the locally held private inputs without exposing them, then perform the first controlled SIGFM enrollment/verification experiment from the uninstalled development build.

The first live run must preserve these rules:

- do not rerun consumed V3 or V4/V4b experiments;
- do not print exact live SIGFM scores or exact live keypoint/minutiae counts;
- do not persist raw fingerprint images or intermediate biometric payloads;
- do not modify the established FDT/manual/up behavior as part of the matcher experiment;
- no firmware erase/flash;
- no PSK rewrite/reprovision;
- no arbitrary persistent sensor writes;
- preserve Windows Hello compatibility.

After a successful controlled SIGFM run, proceed toward ordinary `fprintd-enroll` / `fprintd-verify` integration and then the reboot/suspend/cancellation safety matrix.
