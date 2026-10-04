# V5D10 — SIGFM calibration harness ready

Date: 2026-10-04

## Status

V5D9 proved a full in-memory 30-stage enrollment and same-finger verification match on the Goodix 27c6:5135 device.

The local libfprint tree was then cleaned up so the earlier early-retouch workaround no longer forces an invalid image-device state transition. Instead, the bounded manual-baseline finger-off window is extended to approximately 30 seconds without changing FDT values, matcher behavior, or the SIGFM threshold.

Current local libfprint state:

- branch: `goodix-27c6-5135-chicagohu`
- HEAD: `2631f371e57ff7cb353b1c9389d277064e08c74c`
- working tree: clean
- SIGFM threshold candidate: 12
- enrollment stages: 30
- template extraction quality gate: 25
- matcher internal minimum correspondence: 5

## V5D10 calibration harness

Local source:

`examples/goodix5135-sigfm-calibration.c`

Purpose:

1. Enroll RIGHT_INDEX using 30 accepted stages.
2. Run five genuine RIGHT_INDEX verification trials.
3. Run ten impostor trials across RIGHT_MIDDLE, RIGHT_RING, LEFT_INDEX, LEFT_MIDDLE and LEFT_RING (two trials each).
4. Treat `FP_DEVICE_RETRY` as a retry of the same trial rather than a match/no-match result.
5. Print only MATCH / NO_MATCH / RETRY and aggregate trial counts.
6. Never print SIGFM scores or feature/keypoint counts.
7. Never serialize or persist the enrolled template or captured image.
8. Keep all enrollment and verification data in memory only.

A guarded local runner is stored at:

`scripts/v5d10_run_local.py`

The runner reconstructs ephemeral FDT seed/up files only from the already-proven local V5 proof inputs, uses the existing local TLS key reference, and removes the ephemeral FDT files after the run.

## Host gates

Before live use:

- calibration harness build: PASS
- missing-live-guards refusal: PASS
- selected regression suite: 14/14 PASS
- `git diff --check`: PASS

## Interpretation policy

This is a preliminary threshold sanity test, not a FAR/FRR certification.

- any impostor MATCH is an immediate threshold/security review signal;
- genuine NO_MATCH results indicate usability/coverage review is needed;
- zero false accepts and zero genuine misses across this small trial set means only `PASS_PRELIMINARY`;
- threshold 12 must not be called statistically validated from this run alone.

## Privacy / safety

Do not publish fingerprint images, templates, exact SIGFM scores, exact feature counts, PSK material, OTP, private FDT values, or unit-specific runtime configuration.

Draft PR #2 remains draft and must not be merged without explicit user request.
