# V5D9 — Live SIGFM full enrollment + verification

Date: 2026-10-04

## Result

The guarded live Goodix 27c6:5135 SIGFM path completed a full in-memory enrollment and then verified the same finger successfully.

Observed high-level result:

- USB access: PASS
- TLS/runtime activation: PASS
- FDT finger detection and image capture: PASS
- 80x64 image handoff into libfprint: PASS
- SIGFM feature extraction during enrollment: PASS, with retryable low-quality captures handled without aborting the session
- full enrollment: PASS
- same-finger verification: MATCH
- overall live harness: PASS
- ephemeral FDT runtime files: cleaned after the run

The harness did not serialize or persist the enrollment template and did not save fingerprint images or print biometric scores/feature counts.

## Important lifecycle findings

1. The earlier close/reopen step between enrollment and verification was wrong for this guarded runtime. The Goodix driver intentionally preserves READY TLS across image-device deactivation inside the same open device lifetime. The successful run therefore used same-open enrollment -> cleanup drain -> verification -> cleanup drain -> close.

2. The temporary early-retouch recovery path that called `fpi_image_device_retry_scan()` while libfprint was in `FPI_IMAGE_DEVICE_STATE_AWAIT_FINGER_ON` produced a generic state-transition warning. It was not required for SIGFM correctness. After the successful live run, that workaround was removed and the bounded manual-baseline finger-off wait was widened instead.

3. The generic libfprint warning `Deactivating image device while it is not idle` can still occur when the final enrollment stage completes before the sensor-side finger-off cleanup has finished. The Goodix driver already defers I/O stop while FDT-up cleanup is in progress, and the successful same-open verify demonstrated that the session remains usable afterward. Treat this as a core lifecycle warning to clean up later, not a matcher failure.

## Current local libfprint state after cleanup

- repo: `~/libfprint`
- branch: `goodix-27c6-5135-chicagohu`
- HEAD: `6d19c3257bbd885bcdb96e327acb9e06eb9f9cfb`
- working tree: CLEAN
- host regression suite after cleanup: 14/14 PASS

Important recent commits:

- `f922dcbbe2078e8dffd06d1d891e2fcde2c3e8b7` — add ephemeral full SIGFM live harness
- `0e133f1d630a7c090508567a612219d621079249` — reopen-before-verify experiment (superseded by same-open lifecycle)
- `81a60ee7816f23928b66a90d3020a2f5843f0dad` — early-retouch recovery experiment
- `6d19c3257bbd885bcdb96e327acb9e06eb9f9cfb` — harden full SIGFM live lifecycle and remove the invalid retry-state transition

## Evidence boundary

The successful full live enrollment + same-finger MATCH was obtained immediately before the final `6d19c32` cleanup. The final cleanup changes are host-tested 14/14 but have not yet repeated the full live enrollment. Do not claim a post-`6d19c32` full live rerun until one is actually performed.

## Next milestone

Do not repeat a 30-stage enrollment just to re-prove the cleanup unless needed. Prefer a short live lifecycle smoke for the final cleanup, then move to controlled verification evidence:

1. establish a temporary in-memory enrollment once;
2. run several genuine same-finger verification attempts;
3. run several other-finger impostor attempts;
4. record only coarse PASS/NO_MATCH outcomes or approved buckets, never exact live scores or feature counts;
5. use the evidence to decide whether threshold `12` remains appropriate;
6. then move to local-only persistent `fprintd-enroll` / `fprintd-verify` integration and PAM/GNOME testing.

## Safety/privacy

Do not publish or log plaintext PSK, PSK hash, OTP, fingerprint images/raw data/templates, exact live biometric scores, exact live keypoint/minutiae counts, unit-specific private FDT values, cache/calibration, proprietary Goodix binaries, Windows biometric DB, process dumps, or full private runtime configuration.

Draft PR #2 remains DRAFT and must not be merged without explicit instruction.
