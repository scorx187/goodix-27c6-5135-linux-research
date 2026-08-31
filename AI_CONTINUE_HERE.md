# AI CONTINUE HERE — Goodix 27c6:5135

**Updated:** 2026-08-31

This is the entry point for a new ChatGPT/AI session.

## Read first

Read this file, then read:

- `handoffs/CURRENT_WORK_HANDOFF_2026-08-31.md`
- `checkpoints/2026-08-31-v5d4b-bank-attempt-disk-full.md`

Do **not** infer state from older chat history. The handoff above is the canonical continuation state.

## Current one-line status

The hardened host-only SIGFM stack through **V5D4B** is fully built/tested and backed up as a cumulative 20-path patch, but the attempt to bank it into the real local libfprint branch was safely rolled back because the machine ran out of disk space during Meson setup.

## Exact current local libfprint baseline

- repo: `~/libfprint`
- branch: `goodix-27c6-5135-chicagohu`
- HEAD: `37ac6876fd6d248b48a7892410cf75144f3882e5`
- expected working tree: CLEAN
- mirrored GitHub branch: `libfprint/goodix-27c6-5135-37ac6876`

The mirror is verified to point to the exact SHA above.

## Latest cumulative artifact

- local filename: `~/goodix5135-v5d4b-sigfm-parser-hardening.patch`
- GitHub: `patches/v5d4/goodix5135-v5d4b-sigfm-parser-hardening.patch`
- SHA256: `9b503c9b969c5848f7319fb86b8cdb8259be564ad6d858a41b669b0fffdaf632`
- cumulative paths: 20
- patch applies cleanly to `37ac6876fd6d248b48a7892410cf75144f3882e5`

## What is already green

- V5D1 SIGFM print core: PASS
- V5D2 FpImage SIGFM + generic algorithm selector: PASS
- V5D3 comparator/geometry hardening: PASS
- V5D4B parser/resource/malformed-data hardening: PASS
- SIGFM robustness test: PASS
- SIGFM geometry test: PASS
- SIGFM image core: PASS
- SIGFM print core: PASS
- libfprint `fpi-device`: PASS
- Goodix regressions: **9/9 PASS**
- hardware used during V5D1–V5D4B host work: NO

## Current blocker

Banking failed only because:

`OSError: [Errno 28] No space left on device`

The banking script then executed its safe rollback and restored the real local branch to `37ac6876...`, clean.

## Next action

1. Check/free disk space, especially the filesystem backing `/tmp` and the repository/build directory.
2. Re-run the V5D4B banking step from the exact clean baseline using the verified cumulative patch.
3. Build and run all host regressions again.
4. Commit only after every gate passes.
5. Back up the new banked commit SHA to GitHub.
6. **Do not perform a live fingerprint run yet.** First resolve the runtime SIGFM keypoint gate (`5` vs preserved upstream extraction gate `25`) and choose a separate SIGFM threshold through host-only evidence.

## Hard safety/privacy rules

Never print, persist, commit, upload, publish, or hash sensitive device/biometric material including plaintext PSK, PSK files/hashes, full OTP, fingerprint images/raw/templates, Goodix cache/calibration data, proprietary Goodix binaries, Windows biometric DB, process dumps, or full unit-specific runtime config/hash.

Never print exact live minutiae counts or exact SIGFM/BZ3 scores. No firmware erase/flash, PSK rewrite, arbitrary persistent sensor writes, or Windows enrollment deletion shortcuts.

## Draft PR

Research Draft PR #2 is intentionally still draft. Do not merge unless explicitly requested.
