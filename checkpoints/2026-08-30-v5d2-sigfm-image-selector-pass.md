# Goodix 27c6:5135 — V5D2 SIGFM image-selector host PASS

Date: 2026-08-30

## Baseline

- libfprint branch: `goodix-27c6-5135-chicagohu`
- libfprint base HEAD: `37ac6876fd6d248b48a7892410cf75144f3882e5`
- V5D1 patch SHA256: `7c5e8f2216195071c2e4375f3b8dd40e0ef83832f06cbb3ae80290adb26165ea`
- OpenCV: `4.10.0`

## V5D2 final artifact

- Local artifact path at validation time: `~/goodix5135-v5d2e-sigfm-image-selector.patch`
- Patch SHA256: `f04e87006624aedc784d23ee8aa07833fa15935811771b89fcd533affac126f5`
- Complete patch scope: 18 paths
- `git apply --check`: PASS

## Architecture established

- `FpiPrintType` SIGFM support from V5D1 retained.
- `FpImage` stores owned `SigfmImgInfo *` and frees it at object finalization.
- Private SIGFM image extraction implementation lives in `libfprint/fpi-image.c`, not `fp-image.c`.
- H/V flip and color inversion are normalized before SIGFM extraction.
- SIGFM extraction uses the existing `detection_in_progress` async gate pattern.
- `fpi_print_add_from_image()` deep-copies SIGFM data into `FpPrint`.
- `FpImageDeviceClass` has an algorithm selector with NBIS deliberately equal to zero/default.
- SIGFM has a separate `sigfm_threshold`; no live threshold has been chosen.
- Enrollment type mismatch is handled as a controlled error rather than silently changing an already typed print.
- Runtime hard minimum for extractability is 5 SIGFM keypoints; the stronger >=25 gate remains synthetic-test-only.

## Host-only validation

- Full build: PASS
- `sigfm-image-core`: PASS
- `sigfm-print-core`: PASS
- `fpi-device`: PASS
- Goodix regressions: 9/9 PASS
  - conditioning
  - preprocess
  - image
  - proto
  - image-response
  - io
  - request
  - queue-cleanup
  - async-dispatch

## Safety / privacy

- USB accessed: NO
- Sensor opened: NO
- PSK accessed: NO
- FDT accessed: NO
- Fingerprint used: NO
- Biometric fixture used: NO
- Synthetic pixels only: YES
- Exact SIGFM scores printed: NO
- Goodix SIGFM selected: NO
- Goodix runtime changed: NO

## Next gate

V5D3 must harden the imported SIGFM matcher before any Goodix SIGFM opt-in or live biometric run. Required items include:

1. Fix the `match::operator<` comparator so ordering is strict and lexicographic.
2. Reject zero-length geometry before divisions.
3. Clamp trigonometric inputs to valid domains and reject non-finite intermediates.
4. Harden angle/ratio comparison against zero denominators and NaN/Inf.
5. Add host-only synthetic regression tests for comparator behavior, degenerate geometry, finite matcher results, same/different behavior, and malformed/edge inputs.
6. Continue to suppress exact SIGFM score logging.

Goodix5135 remains NBIS/default until V5D3 and later host gates pass.
