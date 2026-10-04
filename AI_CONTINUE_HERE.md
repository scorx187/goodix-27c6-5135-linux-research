# AI CONTINUE HERE — Goodix 27c6:5135

**Updated:** 2026-10-04

This is the entry point for a new ChatGPT/AI session.

## Read first

Read this file, then read:

- `checkpoints/2026-10-04-v5d6-sigfm-optin.md`
- `handoffs/CURRENT_WORK_HANDOFF_2026-08-31.md` for older historical detail only

The 2026-10-04 checkpoint supersedes the old V5D4B disk-full blocker.

## Current one-line status

The hardened SIGFM host stack is now banked into the real local libfprint branch, the `5` versus `25` keypoint semantics are resolved, and Goodix5135 explicitly opts into SIGFM with an initial threshold of `12` and `30` enrollment stages. All current host gates pass. The next milestone is the first guarded live SIGFM enrollment/verification experiment.

## Exact current local libfprint state

- repo: `~/libfprint`
- branch: `goodix-27c6-5135-chicagohu`
- HEAD: `e2c0eb96cb18f6787d4721bb238271a100f15d5f`
- expected working tree: CLEAN
- GitHub mirror: `libfprint/goodix-27c6-5135-e2c0eb96`

Important commits:

- V5D4B banked host SIGFM stack: `8ef22cd196d827d6a7213ed3173c2285961f4247`
- V5D5 template-quality gate restoration: `0e7beb4c245b6313bfbeb556c60c2ed47b731a40`
- V5D6 Goodix SIGFM opt-in: `e2c0eb96cb18f6787d4721bb238271a100f15d5f`

## Current SIGFM policy

- template extraction quality gate: `25` keypoints
- matcher internal minimum correspondence/geometric support: `5`
- Goodix5135 SIGFM threshold candidate: `12`
- Goodix5135 enrollment stages: `30`
- sensor geometry: `80x64`

Do not merge the `25` and `5` concepts again. The threshold `12` is an initial sensor-family-informed candidate, not a final FAR/FRR claim.

## Host verification

Fresh targeted V5D6 result: **14/14 PASS**.

Passed:

- SIGFM robustness
- SIGFM geometry hardening
- SIGFM image core
- SIGFM print core
- `fpi-device`
- all nine Goodix5135 regression suites

No hardware was touched during V5D4B banking, V5D5, or V5D6 host verification.

## Live-runtime position

The existing guarded native TLS/FDT/image/FpImage path remains in place. SIGFM exact match scores are not logged by the matcher helper.

At the 2026-10-04 checkpoint, the Harb Agent process did not have the three private runtime input environment variables set. Do not automatically discover or expose private PSK/FDT material. Reuse the locally held private inputs only through an explicit guarded local setup that does not print, hash, upload, or commit them.

No live SIGFM fingerprint run has happened yet.

## Next action

1. Prepare the existing guarded live FpImage runtime with the locally held private inputs without exposing them.
2. Run a controlled SIGFM enrollment/verification experiment from the uninstalled development build.
3. Do not rerun consumed V3 or V4/V4b experiments.
4. Do not print exact live keypoint/minutiae counts or SIGFM/BZ3 scores.
5. If live matching is healthy, move toward ordinary `fprintd-enroll` and `fprintd-verify` use.
6. Then complete cold-boot, reboot, suspend/resume, cancellation/timeout, Windows Hello compatibility, and privacy/logging safety gates.

## Hard safety/privacy rules

Never print, persist, commit, upload, publish, or hash sensitive device/biometric material including plaintext PSK, PSK files/hashes, full OTP, fingerprint images/raw/templates, Goodix cache/calibration data, proprietary Goodix binaries, Windows biometric DB, process dumps, or full unit-specific runtime config/hash.

Never print exact live minutiae/keypoint counts or exact SIGFM/BZ3 scores. No firmware erase/flash, PSK rewrite/reprovision, arbitrary persistent sensor writes, or Windows enrollment deletion shortcuts.

## Draft PR

Research Draft PR #2 remains intentionally draft. Do not merge unless explicitly requested.
