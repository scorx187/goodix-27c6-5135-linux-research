# CURRENT WORK HANDOFF — Goodix 27c6:5135 — 2026-08-31

This file is the canonical continuation state for a new AI/chat session.

## 1. Goal

Implement native Linux/libfprint support for Goodix USB fingerprint sensor `27c6:5135` (ChicagoHU family), with a safe small-image matcher path based on SIGFM rather than relying only on generic NBIS/Bozorth3.

Current phase is still **host-first engineering**. No live SIGFM Goodix run has been approved yet.

---

## 2. Hard privacy/safety invariants

Never ask for, print, persist, commit, publish, upload, or publicly hash any of the following:

- plaintext factory PSK
- PSK files or PSK hashes
- full OTP
- fingerprint images/raw images/templates
- `goodix.dat`
- `goodix_calib.dat`
- `Goodix_Cache.bin`
- proprietary Goodix binaries
- Windows biometric database
- process dumps
- full unit-specific 224-byte runtime config or its hash

Windows partition remains read-only.

Never perform:

- firmware erase/flash
- PSK rewrite/reprovision
- arbitrary persistent sensor writes
- deleting/re-enrolling Windows fingerprints as a shortcut

Diagnostics policy:

- never print exact live minutiae counts
- never print exact BZ3/SIGFM live scores
- only coarse minutiae buckets are allowed: `ZERO`, `ONE_TO_FOUR`, `FIVE_TO_NINE`, `TEN_TO_NINETEEN`, `TWENTY_PLUS`

Synthetic tests may internally use exact values but should emit only coarse PASS/FAIL markers.

---

## 3. Exact real libfprint baseline at handoff

Local repository:

- path: `~/libfprint`
- branch: `goodix-27c6-5135-chicagohu`
- HEAD: `37ac6876fd6d248b48a7892410cf75144f3882e5`
- commit subject: `goodix5135: add host-only conditioning diagnostics`
- expected state: CLEAN

Exact GitHub mirror branch:

- `libfprint/goodix-27c6-5135-37ac6876`
- verified remote SHA: `37ac6876fd6d248b48a7892410cf75144f3882e5`

Important: the V5D host stack is **not yet committed into the real local libfprint branch**. The latest banking attempt was rolled back safely because the disk was full.

---

## 4. Hardware / Windows facts

Sensor:

- USB ID: `27c6:5135`
- interface: `:1.1`
- geometry: `80x64`
- family: ChicagoHS/HU, type/family `0x0c`

Windows INF:

`/mnt/windows/Windows/System32/DriverStore/FileRepository/gfusb.inf_amd64_fc427db8d2172056/gfusb.inf`

Known driver version:

`DriverVer = 05/25/2021,1.1.125.12`

Windows firmware / identification facts already established:

- FW: `GF_HC460SEC_APP_12508`
- logical chip: `0x2504`
- packed 64 fast × 80 slow; libfprint output 80×64
- selector4: `state_plane-source+0x0fff`
- exact corrected U16 path cannot be reconstructed from raw alone because missing state/factor planes are required
- Gaussian mode9 exact kernel: 5×5 separable sigma1.5 Q16 `[7869,15328,19142,15328,7869]`, sum 65536, weighted sum `>>16`, REFLECT_101

---

## 5. Historical live tests — DO NOT RERUN

### V3 — complete

Guard consumed:

`~/.cache/goodix5135-private/minutiae-bz3-v3-f2184c9697b50e586ed42621713d71911c4fd08a.used`

Result:

- one hardware lifecycle
- zero transport/FDT retries
- enrollment mostly `ONE_TO_FOUR`, two `ZERO` retries
- verify `ONE_TO_FOUR`
- gallery below useful Bozorth minimum
- final: `VERIFY_NO_MATCH`

Conclusion:

Transport/TLS/FDT/decrypt/decode/handoff/template/matcher are working. Primary problem is poor feature quality for generic NBIS/Bozorth on the tiny sensor. Do not lower BZ3 threshold to compensate.

`bz3_threshold=24` is only a historical placeholder.

### V4/V4b — complete

Immutable historical instrumentation patch:

- `/home/sam/goodix5135-v4-clean-sixway-instrumentation.patch`
- SHA256: `757dcca1e11c0f9a1837f6f7a6560db2ef32dc286bfd953215e68c54f8f8e19a`

V4 original guard consumed due USB ACL failure; never rerun:

`~/.cache/goodix5135-private/conditioning-sixway-v4-37ac6876fd6d248b48a7892410cf75144f3882e5.used`

V4b guard consumed; never rerun:

`~/.cache/goodix5135-private/conditioning-sixway-v4b-37ac6876fd6d248b48a7892410cf75144f3882e5.used`

V4b conclusion:

- inversion not the main issue
- global range not enough
- Gaussian+range not enough
- lowering BZ3 cannot fix fewer than 10 minutiae
- generic NBIS/BZ3 is likely a poor fit for an 80×64 small-area sensor

---

## 6. Existing FDT behavior — DO NOT MODIFY casually

Current known behavior:

- `GOODIX5135_FPIMAGE_TEST=TWO_CAPTURE_LIFECYCLE`
- bounded empty FDT-down rearm max8
- human timeout 180000 ms
- initial 250 ms pre-baseline settle
- touch => 100 ms checks up to20
- dynamic env read
- log: `Native FpImage prompt: LIFT_FINGER_NOW`
- empty event log rearm
- keep finger off through TLS/manual baseline; place after first empty rearm; lift at prompt
- after FDT-up private state resets and READY TLS is preserved

Do not alter FDT/manual/up functions while working on SIGFM host matching.

---

## 7. Why SIGFM

SIGFM = “SIFT Is Good For Matching”, from `goodix-fp-linux-dev`, designed around low-resolution small fingerprint sensors in the same 64×80 scale.

Pinned upstream commit:

`07306bbc9256942595e31fb0f407b364ffa24d07`

Core characteristics:

- OpenCV SIFT `detectAndCompute`
- BFMatcher KNN k=2
- Lowe ratio 0.75
- geometric length/angle consistency
- matcher minimum correspondence count 5
- serialization stores keypoints + descriptors, not raw fingerprint images

Original implementation defects identified and hardened later:

- comparator equal-Y bug
- zero-length vector divisions
- unclamped trig domains
- non-finite geometry handling
- unbounded `vector.reserve(size)` during deserialize
- unbounded/unchecked `cv::Mat` allocation metadata
- stream over-read possibility
- trailing bytes not rejected
- malformed/non-finite logical objects not rejected

---

## 8. V5B — standalone SIGFM — COMPLETE

OpenCV installed: `4.10.0`

Host-only results:

- pinned SIGFM source
- compile PASS
- deterministic synthetic 80×64 and 64×80 extraction PASS
- copy PASS
- serialize/deserialize PASS
- self-match PASS
- marker: `SIGFM_SYNTHETIC_REHEARSAL=PASS`
- no biometric data used
- no exact scores printed

---

## 9. V5C — Meson/libfprint scaffold — COMPLETE

Result:

- 4 pinned SIGFM source files integrated
- `libfprint/sigfm/meson.build`
- linked `libsigfm` into libfprint
- exact six-file scaffold scope
- full build PASS
- Goodix tests 9/9 PASS
- `git diff --check` PASS

Historical complete patch:

`~/goodix5135-v5c-sigfm-meson-scaffold-complete.patch`

Research progress branch:

`progress/v5c-sigfm-2026-08-30`

Draft PR:

- PR #2: `Checkpoint: Goodix5135 V5C SIGFM host integration`
- state: OPEN / DRAFT
- **do not merge unless explicitly requested**

---

## 10. V5D1 — FpPrint SIGFM core — COMPLETE

Scope:

- SIGFM print enum/type
- ownership/deep copy
- serialization/deserialization
- matcher path
- deterministic synthetic same/different tests
- no FpImage/FpImageDevice/Goodix runtime change

Robust synthetic fixture:

- deterministic retry bound 128
- accepts first synthetic source with >=25 keypoints
- old 25-keypoint fixture gate preserved for fixture generation
- bounded negative search 64
- threshold chosen internally between same/different synthetic scores
- no exact score output

Verified patch:

- GitHub: `patches/v5d1/goodix5135-v5d1-sigfm-print-core.patch`
- SHA256: `7c5e8f2216195071c2e4375f3b8dd40e0ef83832f06cbb3ae80290adb26165ea`
- original local artifact: `/home/sam/goodix5135-v5d1b-sigfm-print-core.patch`

Tests:

- build PASS
- `sigfm-print-core` PASS
- `fpi-device` PASS
- Goodix 9/9 PASS

---

## 11. V5D2 — FpImage SIGFM + generic selector — COMPLETE

Architecture added:

- `SigfmImgInfo *sigfm_info` ownership in FpImage
- async SIGFM extraction path
- flag normalization before SIGFM
- SIGFM image->print deep copy
- generic `FpImageDevice` selector:
  - zero/default = NBIS
  - optional SIGFM
- separate `sigfm_threshold` field
- enrollment type follows selector with mismatch controlled error
- verify/identify dispatch by selected algorithm
- Goodix driver does **not** opt into SIGFM

Important integration fix:

Private `fpi_image_*SIGFM*` implementation was moved to `fpi-image.c`, not `fp-image.c`, because private objects link through `libfprint-private.a`. `fp-image.c` retains FpImage lifetime/finalizer ownership cleanup.

Cumulative verified patch:

- GitHub: `patches/v5d2/goodix5135-v5d2-sigfm-image-selector.patch`
- SHA256: `f04e87006624aedc784d23ee8aa07833fa15935811771b89fcd533affac126f5`
- cumulative paths: 18

Tests:

- Build PASS
- `sigfm-image-core` PASS
- `sigfm-print-core` PASS
- `fpi-device` PASS
- Goodix 9/9 PASS

Known unresolved design point introduced here:

`SIGFM_MIN_MATCHABLE_KEYPOINTS = 5`

This came from matcher `min_match=5`, but upstream extraction historically rejected `<25` keypoints. **Do not perform live SIGFM testing until 5 vs 25 is deliberately resolved with host evidence.**

---

## 12. V5D3 — comparator + geometry hardening — COMPLETE

Hardened:

- comparator tie-break becomes deterministic strict ordering by `(p1.y, p1.x)`
- zero-length geometry rejected
- non-finite lengths/products rejected
- trig inputs clamped to [-1, 1]
- safe relative difference used instead of divide-by-zero-prone ratio comparisons

Dedicated host test covers comparator, degenerate vectors, finite geometry, clamps, and mismatched lengths.

Cumulative verified patch:

- GitHub: `patches/v5d3/goodix5135-v5d3-sigfm-geometry-hardening.patch`
- SHA256: `9e4ce5607ac2eec198bbb0b9f076e07ac1653cd47c1834fcd1972217140e2e34`
- cumulative paths: 19

Tests:

- build PASS
- `sigfm-geometry-hardening` PASS
- `sigfm-image-core` PASS
- `sigfm-print-core` PASS
- `fpi-device` PASS
- Goodix 9/9 PASS

---

## 13. V5D4B — parser/resource/malformed-data hardening — COMPLETE

V5D4 first compile failed only because GCC 15 `-Werror=missing-braces` rejected a `std::array` test initializer. V5D4B changed the test initializer to double braces and explicitly included `<cstdio>`; parser logic itself was not changed by the B wrapper.

Hardening added:

- bounded `stream::read()`
- max serialized SIGFM object: 4 MiB
- max container elements: 4096
- max descriptor matrix bytes: 4 MiB
- descriptor width required: 128
- descriptor type required: `CV_32FC1`
- descriptor row count bounded before `cv::Mat::create()`
- vector size bounded before `reserve()`
- null API guards
- logical SIGFM object validation
- keypoint count == descriptor rows
- continuous descriptor matrix required
- non-finite keypoint and descriptor values rejected
- trailing serialized bytes rejected
- malformed matcher inputs rejected before OpenCV matcher internals

Robustness test covers:

- direct stream over-read
- null API
- valid roundtrip
- every strict truncation prefix
- trailing byte
- zero/negative length
- oversized serialized length rejection
- attacker-controlled huge vector reserve
- huge matrix row metadata
- invalid matrix type
- invalid descriptor width
- descriptor/keypoint count mismatch
- NaN keypoint
- NaN descriptor
- >4096 logical keypoints
- deterministic byte mutation sweep

Cumulative verified patch:

- local: `~/goodix5135-v5d4b-sigfm-parser-hardening.patch`
- GitHub: `patches/v5d4/goodix5135-v5d4b-sigfm-parser-hardening.patch`
- SHA256: `9b503c9b969c5848f7319fb86b8cdb8259be564ad6d858a41b669b0fffdaf632`
- cumulative paths: 20
- applies cleanly to baseline `37ac6876...`

Full V5D4B host verification:

- Build PASS
- SIGFM robustness PASS
- SIGFM geometry PASS
- SIGFM image PASS
- SIGFM print PASS
- `fpi-device` PASS
- Goodix 9/9 PASS
- Goodix runtime unchanged
- hardware not touched

---

## 14. Latest banking attempt — FAILED ONLY DUE DISK SPACE, SAFE ROLLBACK COMPLETE

Goal was to apply the verified V5D4B 20-path cumulative patch into the real local libfprint branch, rebuild, rerun all tests, and commit only if green.

Pre-build gates passed:

- correct branch
- correct baseline HEAD
- clean repo
- patch SHA verified
- patch path count 20
- no Goodix driver path modified
- `git apply --check` PASS
- patch applied/indexed successfully
- 20 staged paths
- staged diff check PASS
- comparator hardening present
- geometry hardening present
- parser bounds present
- logical validation present
- generic selector present
- Goodix SIGFM opt-in still NO
- live SIGFM threshold still NOT SELECTED

Meson setup then failed with:

`OSError: [Errno 28] No space left on device`

This is a build-environment/storage failure, not a source/test failure.

The banking script executed safe rollback:

- `git reset --hard 37ac6876fd6d248b48a7892410cf75144f3882e5`
- real branch restored
- final HEAD = `37ac6876fd6d248b48a7892410cf75144f3882e5`
- final working tree clean
- hardware touched = NO
- no bank commit was created

See also:

`checkpoints/2026-08-31-v5d4b-bank-attempt-disk-full.md`

---

## 15. GitHub backup/checkpoint state

Research repo:

`scorx187/goodix-27c6-5135-linux-research`

Progress branch:

`progress/v5c-sigfm-2026-08-30`

Known key backup commits:

- V5D1 patch stored: `86787340a43cbd0105ffb4d6e716d647d889d93e`
- V5D2 checkpoint: `fae1c3787d3ebaa9f85150f6b06a5c52408d3324`
- V5D2 patch upload: `a78a62c...`
- V5D3 checkpoint: `243e89150c3972be03219f771385cbdf6a09660c`
- V5D3 patch upload: `feea418...`
- V5D4B checkpoint: `d56d2985a96f5958be3ea557bc97c8cb5d6f4b49`
- V5D4B patch upload: `f30501a...`

Draft PR #2 remains open and draft. It has been updated with progress comments. Do not merge.

Exact historical libfprint mirror branch remains:

`libfprint/goodix-27c6-5135-37ac6876`

and points to exact SHA `37ac6876fd6d248b48a7892410cf75144f3882e5`.

---

## 16. Immediate next step for a new session

**Do not start with live hardware.**

First resolve disk space:

1. inspect disk space for `/`, `/tmp`, and the filesystem containing `~/libfprint`
2. remove only safe build/temp artifacts; do not delete research patches/checkpoints or private guard files
3. confirm real libfprint branch is still clean at `37ac6876...`
4. verify V5D4B patch SHA again
5. rerun banking with a fresh external build directory
6. run Build + SIGFM robustness + geometry + image + print + `fpi-device` + Goodix 9/9
7. only then create real bank commit
8. record/push the new bank commit SHA to GitHub immediately

After banking succeeds, still do **not** live-test until the following are resolved host-only:

- runtime keypoint gate: matcher minimum 5 vs preserved historical extraction quality gate 25
- choose a dedicated `sigfm_threshold` using host evidence; never reuse `bz3_threshold`
- ensure no exact SIGFM score logging on live path
- Goodix opt-in must be a tiny explicit driver change, separate from host stack
- first live run must be guarded and one-shot only

---

## 17. Recommended first prompt for the next chat

Use:

> Open `scorx187/goodix-27c6-5135-linux-research` and start from `AI_CONTINUE_HERE.md`, then read `handoffs/CURRENT_WORK_HANDOFF_2026-08-31.md` completely. Do not rely on previous chats. Continue from the exact handoff. The current blocker is disk space during the V5D4B banking step; the real libfprint branch was safely rolled back to `37ac6876fd6d248b48a7892410cf75144f3882e5`. Do not touch live fingerprint hardware until the handoff says it is allowed.

---

## 18. Interaction preference for continuation

Give one safe copy/paste terminal block at a time, then explain briefly what happened and what comes next. Avoid rerunning historical consumed hardware guards.
