# AI CONTINUE HERE — Goodix 27c6:5135

**Updated:** 2026-10-04

This is the canonical entry point for the next ChatGPT/AI development session.

## Read first

Read this file, then:

1. `checkpoints/2026-10-04-v5d7-live-sigfm-smoke-harness.md`
2. `checkpoints/2026-10-04-v5d6-sigfm-optin.md`
3. `handoffs/CURRENT_WORK_HANDOFF_2026-08-31.md` for older historical detail only
4. `docs/SAFETY.md`

The 2026-10-04 checkpoints supersede the old V5D4B disk-full blocker.

## Current one-line status

The hardened SIGFM stack is banked, Goodix5135 explicitly uses SIGFM with the `25` extraction-quality gate, matcher-internal `5` correspondence minimum, initial match threshold `12`, and 30 enrollment stages. A guarded one-stage live SIGFM smoke harness is now built, tested host-side, and mirrored to GitHub. The next milestone is its first live one-touch run using the already-proven local private TLS/FDT inputs.

## Exact current local libfprint state

- repo: `~/libfprint`
- branch: `goodix-27c6-5135-chicagohu`
- HEAD: `ceac0b016e13588f1d509f8b6a7d284308443322`
- expected working tree: CLEAN
- GitHub mirror: `libfprint/goodix-27c6-5135-ceac0b01`

Important commits:

- V5D4B banked host SIGFM stack: `8ef22cd196d827d6a7213ed3173c2285961f4247`
- V5D5 template-quality gate restoration: `0e7beb4c245b6313bfbeb556c60c2ed47b731a40`
- V5D6 Goodix SIGFM opt-in: `e2c0eb96cb18f6787d4721bb238271a100f15d5f`
- V5D7 guarded one-stage live smoke harness: `ceac0b016e13588f1d509f8b6a7d284308443322`

## Current SIGFM policy

- template extraction quality gate: `25` keypoints
- matcher internal minimum correspondence/geometric support: `5`
- Goodix5135 SIGFM threshold candidate: `12`
- Goodix5135 enrollment stages: `30`
- sensor geometry: `80x64`

Do not merge the `25` and `5` concepts again. Threshold `12` is an evidence-based starting candidate, not a final FAR/FRR claim for this exact unit.

## Runtime audit

SIGFM configuration is intentionally class-based at runtime:

- `FpImageDeviceClass.algorithm` selects SIGFM extraction and print type;
- `FpImageDeviceClass.sigfm_threshold` is passed to `fpi_print_sigfm_match()`;
- Goodix5135 sets both fields in class init;
- the private `bz3_threshold` remains NBIS-specific.

Do not add duplicate private algorithm/threshold fields unless a future dynamic-threshold requirement specifically needs them.

## V5D7 smoke harness

Executable:

`examples/goodix5135-sigfm-smoke`

Purpose:

`USB -> TLS -> FDT -> image decode -> FpImage -> SIGFM extraction`

Behavior:

- refuses to run without all explicit live guards;
- stops after the first successful enrollment stage;
- does not save a fingerprint image;
- does not serialize/persist a template;
- does not print exact SIGFM scores or live keypoint/minutiae counts.

No-guard safety test: PASS, expected exit code `2` before device access.

Fresh host regression result after V5D7: **14/14 PASS**.

## Live-input recovery state

The development machine still holds the earlier proven local material. Private bytes were not printed or committed.

Confirmed at metadata/redacted-reference level:

- the previously used local TLS key reference still exists;
- the old V5 FDT proof script still carries the proven local `goodix.dat` reference and per-unit FDT-up calibration;
- no standalone 12-byte FDT-up artifact was found;
- existing local fingerprint-image artifacts were deliberately not opened or analyzed.

The correct next step is to reconstruct ephemeral FDT runtime files from the already-proven local V5 proof source, not to invent a generic FDT-up derivation.

## Exact next action

1. Prepare ephemeral local-only FDT seed/up files from the proven V5 proof source without printing their values.
2. Use the existing local TLS key reference without copying/publishing its contents.
3. Set the existing guarded runtime environment.
4. Run `examples/goodix5135-sigfm-smoke`.
5. Require one successful finger placement and `LIVE_SIGFM_FIRST_STAGE=PASS` / `LIVE_SIGFM_SMOKE=PASS`.
6. Remove ephemeral FDT files after the run.
7. If the smoke test passes, proceed to controlled 30-stage enrollment.
8. Then run genuine/impostor verification trials to calibrate threshold `12` before ordinary `fprintd-enroll` / `fprintd-verify` integration.
9. Finish reboot, suspend/resume, cancellation/timeout, Windows Hello compatibility, and privacy/logging gates.

## Hard safety/privacy rules

Never print, persist to Git, upload, publish, or hash sensitive device/biometric material including plaintext PSK, PSK files/hashes, full OTP, fingerprint images/raw/templates, unit-specific FDT values, Goodix cache/calibration data, proprietary Goodix binaries, Windows biometric DB, process dumps, or full unit-specific runtime config/hash.

Never print exact live biometric scores or exact live feature counts.

No firmware erase/flash, PSK rewrite/reprovision, arbitrary persistent sensor writes, or Windows enrollment deletion shortcuts.

## Draft PR

Research Draft PR #2 remains intentionally draft. Do not merge unless explicitly requested.
