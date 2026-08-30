# V5D1 SIGFM print-core checkpoint — 2026-08-30

Status: **PASS (host-only)**

## Immutable base

- Local libfprint branch: `goodix-27c6-5135-chicagohu`
- Base HEAD: `37ac6876fd6d248b48a7892410cf75144f3882e5`
- GitHub backup branch containing that full history: `libfprint/goodix-27c6-5135-37ac6876`
- Pinned SIGFM source: `goodix-fp-linux-dev/libfprint@07306bbc9256942595e31fb0f407b364ffa24d07`
- OpenCV observed during the successful run: `4.10.0`

## V5D1 artifact

Complete local patch path at successful run:

`~/goodix5135-v5d1b-sigfm-print-core.patch`

SHA-256:

`7c5e8f2216195071c2e4375f3b8dd40e0ef83832f06cbb3ae80290adb26165ea`

The patch was exported from the isolated worktree after staging all paths with `git diff --cached --binary`; unlike the earlier V5C export mistake, the V5D1 patch includes the new untracked SIGFM/test files. Patch path count: **11**.

## Scope proven by the successful run

V5D1 adds host-side SIGFM print representation support only:

- `FPI_PRINT_SIGFM` private print type
- SIGFM deep-copy ownership for print collections
- SIGFM serialization/deserialization through the existing FP3 GVariant envelope
- SIGFM equality through deterministic serialized representation comparison
- SIGFM matcher helper with no exact score logging
- pinned SIGFM C++/OpenCV static library integration
- deterministic synthetic print-core tests

Goodix5135 itself was **not switched to SIGFM** in V5D1.

## Successful gates

- Source patching: PASS
- Diff scope: exactly 11 paths
- FpImage runtime: unchanged
- FpImageDevice runtime: unchanged
- Goodix5135 driver runtime: unchanged
- SIGFM selected by driver: NO
- Meson setup: PASS
- Full build: PASS
- `sigfm-print-core`: PASS
- `fpi-device`: PASS
- Goodix5135 regression suites: **9/9 PASS**
- `git diff --cached --check`: PASS after normalizing `libfprint/fp-print.c` to exactly one final newline
- Real repository after run: CLEAN
- Real repository HEAD after run: still `37ac6876fd6d248b48a7892410cf75144f3882e5`

## Synthetic fixture correction

The first V5D1 test attempt showed that one arbitrary synthetic PRNG seed could produce fewer than the test-only `>=25` SIGFM keypoint gate. This was not a SIGFM core failure.

The final passing test preserved the `>=25` gate and changed only the synthetic fixture:

- deterministic bounded seed search for a rich 80x64 synthetic image
- retry bound: 128
- deterministic bounded negative-case search
- negative search bound: 64
- no exact SIGFM scores printed
- no biometric fixtures

## Privacy / hardware statement

The successful V5D1 run used:

- USB: NO
- sensor open: NO
- PSK: NO
- FDT: NO
- fingerprint input: NO
- fingerprint persistence: NO
- synthetic pixels only: YES
- exact SIGFM scores logged: NO

## Next step

**V5D2**: starting from the V5D1 patch, add host-side `FpImage -> SIGFM` extraction and a generic `FpImageDevice` algorithm selector while keeping the default algorithm NBIS and keeping Goodix5135 explicitly/unconditionally on NBIS until all host-only V5D2 gates pass.

Do not perform a live fingerprint run yet.
