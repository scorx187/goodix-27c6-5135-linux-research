# Goodix 27c6:5135 — V5C SIGFM host-integration checkpoint

Date: 2026-08-30

This checkpoint preserves the current Linux/libfprint research state after the V4/V4b image-quality experiment and the V5B/V5C SIGFM host-only work.

## Canonical local libfprint state

- Local repository: `~/libfprint`
- Branch: `goodix-27c6-5135-chicagohu`
- Exact HEAD: `37ac6876fd6d248b48a7892410cf75144f3882e5`
- Subject: `goodix5135: add host-only conditioning diagnostics`
- Real repository was CLEAN after V5C.

Do not silently advance from another local commit when reproducing this checkpoint.

## Privacy / safety invariants

Never publish or persist private provisioning material, fingerprint frames, fingerprint templates, exact minutiae counts, exact matcher scores, OTP contents, proprietary Goodix binaries, Windows biometric databases, process dumps, or the full unit-specific runtime configuration.

Only coarse minutiae buckets are allowed in diagnostics:

- `ZERO`
- `ONE_TO_FOUR`
- `FIVE_TO_NINE`
- `TEN_TO_NINETEEN`
- `TWENTY_PLUS`

No firmware erase/flash, PSK rewrite/reprovision, arbitrary persistent sensor writes, or destructive Windows-side enrollment changes are part of this work.

## V3 result — NBIS/Bozorth path executes but image quality is weak

V3 completed enrollment and verification through the native Goodix open/TLS/FDT/image pipeline. The observed minutiae quality remained in the low coarse bucket and Bozorth matching did not produce a usable verification result.

Conclusion: transport, TLS, FDT, image capture/decode, libfprint handoff and matcher execution are present. Lowering the matcher threshold is not the first fix.

## V4 / V4b six-way conditioning experiment

The reviewed six-way diagnostic patch identity used for the live experiment was:

- Patch SHA256: `757dcca1e11c0f9a1837f6f7a6560db2ef32dc286bfd953215e68c54f8f8e19a`

The original V4 attempt was consumed before image capture because the user account had read-only access to the USB device node. Postmortem proved:

- USB device `27c6:5135` present
- interface `1.1` present and unbound
- device node owned by `root:root`
- user had read access but no write access
- no exact installed udev rule for `27c6:5135`

A metadata-only ACL preflight proved that a temporary per-user read/write ACL works and can be restored exactly afterwards.

V4b then completed one guarded hardware lifecycle with:

- one public capture request
- one captured image
- six NBIS evaluations from the same decoded U16 frame
- no raw image persistence
- no template persistence
- no exact minutiae counts
- no BZ3 score computation

Results:

| Variant | Coarse minutiae bucket |
| --- | --- |
| `SHIFT4` | `ONE_TO_FOUR` |
| `SHIFT4_INVERT` | `ONE_TO_FOUR` |
| `GLOBAL_RANGE` | `ONE_TO_FOUR` |
| `GLOBAL_RANGE_INVERT` | `ONE_TO_FOUR` |
| `GAUSSIAN_GLOBAL_RANGE` | `ONE_TO_FOUR` |
| `GAUSSIAN_GLOBAL_RANGE_INVERT` | `ONE_TO_FOUR` |

V4b guard is consumed. Do **not** rerun the same V4b experiment.

Interpretation: polarity inversion, global contrast stretching, Gaussian smoothing, and Gaussian+global-range conditioning do not solve the low-minutiae problem. This strongly argues against spending more time on trivial global filters or simply lowering `bz3_threshold`.

## Architecture finding — consider a small-sensor matcher

The current `FpImageDevice` path forces image enrollment through the NBIS representation. libfprint also supports non-NBIS print representations, but replacing the full Goodix5135 enroll/verify actions would unnecessarily fight the existing `FpImageDevice` state machine.

A cleaner direction was found in the Goodix community SIGFM fork: keep `FpImageDevice` and select a per-image-device matcher suitable for small sensors.

Pinned upstream SIGFM source:

- Repository: `goodix-fp-linux-dev/libfprint`
- Commit: `07306bbc9256942595e31fb0f407b364ffa24d07`
- Commit message: `Switch to OpenCV 4.5`

## V5B — standalone SIGFM rehearsal

Host-only standalone validation was performed using only deterministic synthetic images.

Environment:

- OpenCV: `4.10.0`
- Required minimum: `4.5.0`
- No USB access
- No sensor open
- No biometric data

SIGFM built successfully against the installed OpenCV version. OpenCV headers emitted non-fatal compiler warnings; compilation completed successfully.

Synthetic test results:

### 80x64

- extraction: PASS
- synthetic keypoint gate `>=25`: PASS
- copy: PASS
- binary serialization: PASS
- binary deserialization: PASS
- self-match execution: PASS

### 64x80

- extraction: PASS
- synthetic keypoint gate `>=25`: PASS
- copy: PASS
- binary serialization: PASS
- binary deserialization: PASS
- self-match execution: PASS

No exact SIGFM scores were printed.

## V5C — SIGFM Meson scaffold in libfprint 1.94.9

V5C intentionally changed **build integration only**. It did not switch Goodix5135 to SIGFM and did not modify matching runtime behavior.

The isolated worktree contained exactly:

- modified `libfprint/meson.build`
- added `libfprint/sigfm/binary.hpp`
- added `libfprint/sigfm/img-info.hpp`
- added `libfprint/sigfm/meson.build`
- added `libfprint/sigfm/sigfm.cpp`
- added `libfprint/sigfm/sigfm.hpp`

The SIGFM Meson adapter:

- requires `opencv4 >= 4.5.0`
- builds `sigfm.cpp` as a PIC static library
- links `libsigfm` alongside `libnbis` into the private libfprint library

Runtime-change gates confirmed these remained unchanged in V5C:

- `libfprint/drivers/goodix5135/goodix5135.c`
- `libfprint/fpi-image-device.c`
- `libfprint/fpi-print.c`

V5C results:

- Meson setup: PASS
- OpenCV dependency discovery: PASS (`4.10.0`)
- SIGFM C++ target: PASS
- SIGFM build artifacts: present
- full build: PASS
- Goodix regression suites: `9/9 PASS`
- `git diff --check`: PASS
- real repository after test: CLEAN
- hardware access: NONE

Exported local review patch:

- local path: `~/goodix5135-v5c-sigfm-meson-scaffold.patch`
- SHA256: `8eeb896ed9ab91f8a106e4158f90e3cf8789d748e5568640f3e07921de6148e9`

The hash is a review identity only. The reproducibility script in this research branch is the recovery mechanism if the local patch file is lost.

## Current conclusion

The strongest current hypothesis is no longer "just preprocess harder for NBIS". A matcher designed for the tiny 80x64 image domain is a better path to test.

SIGFM is now proven to:

1. build on the current Ubuntu/OpenCV environment,
2. operate on synthetic 80x64 input,
3. serialize/deserialize its derived representation,
4. self-match after roundtrip,
5. link into current libfprint 1.94.9 without changing runtime behavior,
6. preserve all existing Goodix5135 host tests.

This does **not** yet prove that SIGFM will match real Goodix5135 fingerprints. No such live SIGFM test has been performed.

## Next controlled step — V5D

Do not touch hardware yet.

V5D should be another isolated-worktree, host-only port containing only the smallest current-version equivalents of:

- image-device algorithm selector,
- SIGFM derived representation attached to the image/print path,
- SIGFM extraction dispatch,
- SIGFM print copy/storage/serialization/deserialization,
- SIGFM verification/identification dispatch,
- Goodix5135 selecting SIGFM only after all generic host tests are green.

Required V5D gates before any live fingerprint run:

1. exact base HEAD check,
2. clean real repository,
3. pinned SIGFM commit identity,
4. OpenCV version gate,
5. build PASS,
6. existing Goodix `9/9 PASS`,
7. dedicated synthetic SIGFM print serialization roundtrip tests,
8. same/different synthetic matching tests without printing exact scores,
9. legacy NBIS image-device tests still green,
10. no USB, PSK, FDT or fingerprint access,
11. export a review patch with SHA256,
12. only then design a new one-shot live guard for a later V5E/V6 experiment.

## Do not regress these established facts

- Native Goodix open/TLS/FDT/image capture is already working.
- Current decoded geometry is 80x64 (5120 pixels).
- V4b proved six simple conditioning variants all remain in `ONE_TO_FOUR`.
- Bozorth/NBIS should not be "fixed" by lowering the placeholder threshold first.
- V4 and V4b one-shot guards are consumed and must remain historical records.
- The real local repository at this checkpoint is clean at `37ac6876fd6d248b48a7892410cf75144f3882e5`.
