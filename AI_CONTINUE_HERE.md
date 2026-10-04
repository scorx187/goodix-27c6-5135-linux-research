# AI CONTINUE HERE — Goodix 27c6:5135

**Updated:** 2026-10-04

This is the canonical entry point for the next ChatGPT/AI development session.

## Read first

Read this file, then:

1. `checkpoints/2026-10-04-v5d9-live-sigfm-enroll-verify.md`
2. `checkpoints/2026-10-04-v5d7-live-sigfm-smoke-harness.md`
3. `checkpoints/2026-10-04-v5d6-sigfm-optin.md`
4. `handoffs/CURRENT_WORK_HANDOFF_2026-08-31.md` for older historical detail only
5. `docs/SAFETY.md`

The 2026-10-04 checkpoints supersede the old V5D4B disk-full blocker.

## Current one-line status

Goodix 27c6:5135 now has a proven live native path through USB, TLS, FDT, image capture, 80x64 FpImage handoff, SIGFM extraction, full in-memory enrollment, and same-finger verification. The live full harness reached overall PASS. The next milestone is controlled genuine/impostor verification evidence, then local-only fprintd integration.

## Exact current local libfprint state

- repo: `~/libfprint`
- branch: `goodix-27c6-5135-chicagohu`
- HEAD: `6d19c3257bbd885bcdb96e327acb9e06eb9f9cfb`
- expected working tree: CLEAN
- final cleanup host regressions: 14/14 PASS

Important commits:

- V5D4B banked host SIGFM stack: `8ef22cd196d827d6a7213ed3173c2285961f4247`
- V5D5 template-quality gate restoration: `0e7beb4c245b6313bfbeb556c60c2ed47b731a40`
- V5D6 Goodix SIGFM opt-in: `e2c0eb96cb18f6787d4721bb238271a100f15d5f`
- V5D7 guarded one-stage live smoke harness: `ceac0b016e13588f1d509f8b6a7d284308443322`
- full in-memory SIGFM harness: `f922dcbbe2078e8dffd06d1d891e2fcde2c3e8b7`
- current hardened lifecycle cleanup: `6d19c3257bbd885bcdb96e327acb9e06eb9f9cfb`

## Current SIGFM policy

- template extraction quality gate: `25` keypoints
- matcher internal minimum correspondence/geometric support: `5`
- Goodix5135 SIGFM threshold candidate: `12`
- Goodix5135 enrollment stages: `30`
- sensor geometry: `80x64`

Do not merge the `25` and `5` concepts again. Threshold `12` is still an evidence-based starting candidate, not a final FAR/FRR claim for this exact unit.

## Live proof now completed

V5D7 one-stage smoke proved:

`USB -> TLS -> FDT -> image decode -> FpImage -> SIGFM extraction`

V5D9 full live harness then proved:

`USB -> TLS -> repeated FDT/capture -> SIGFM enrollment -> same-open verification -> MATCH`

The successful full run:

- completed the configured enrollment sequence;
- tolerated retryable low-quality captures;
- completed enrollment successfully;
- verified the same finger successfully;
- returned overall PASS;
- removed ephemeral FDT runtime files afterward.

No image or template was published or persisted by the harness, and no exact live biometric scores or feature counts were printed.

## Lifecycle notes

- Same-open enrollment -> verification is the correct guarded runtime path. A close/reopen experiment after enrollment was rejected because close clears volatile TLS state and introduced a lifecycle race.
- The driver intentionally preserves READY TLS across image-device deactivation while the USB device remains open.
- A temporary early-retouch workaround used `fpi_image_device_retry_scan()` from `AWAIT_FINGER_ON`; it worked functionally but produced an invalid generic state-transition warning. It has been removed in current HEAD.
- Current HEAD instead keeps the proven FDT behavior and widens the bounded manual-baseline finger-off stabilization window. This cleanup is host-tested 14/14.
- Generic libfprint may warn that it is deactivating a non-idle image device when the final enrollment stage finishes before sensor-side finger-off cleanup. The Goodix driver defers I/O stop during FDT-up cleanup, and the successful same-open verification demonstrated that the session remains usable afterward. Treat this as a later core-lifecycle cleanup item, not a matcher failure.

## Evidence boundary

The successful full live enrollment and same-finger match happened immediately before the final `6d19c32` cleanup. The cleanup itself is host-tested 14/14 but has not repeated a full live enrollment yet. Do not claim a post-`6d19c32` full live rerun until one is actually performed.

## Exact next action

1. Prefer a short live lifecycle smoke for current HEAD rather than another 30-stage enrollment solely to re-prove cleanup.
2. Perform a controlled in-memory enrollment when needed for verification evidence.
3. Run several genuine same-finger verification attempts.
4. Run several other-finger impostor attempts.
5. Record only coarse MATCH/NO_MATCH outcomes or approved buckets; never exact live scores or feature counts.
6. Decide whether threshold `12` remains appropriate from that evidence.
7. Move to local-only persistent `fprintd-enroll` / `fprintd-verify` integration.
8. Then test GNOME login/unlock, sudo/PAM, reboot, suspend/resume, cancellation/timeout, and Windows Hello compatibility.

## Hard safety/privacy rules

Never print, persist to Git, upload, publish, or hash sensitive device/biometric material including plaintext PSK, PSK files/hashes, full OTP, fingerprint images/raw/templates, unit-specific FDT values, Goodix cache/calibration data, proprietary Goodix binaries, Windows biometric DB, process dumps, or full unit-specific runtime config/hash.

Never print exact live biometric scores or exact live feature counts.

No firmware erase/flash, PSK rewrite/reprovision, arbitrary persistent sensor writes, or Windows enrollment deletion shortcuts.

## Draft PR

Research Draft PR #2 remains intentionally draft. Do not merge unless explicitly requested.
