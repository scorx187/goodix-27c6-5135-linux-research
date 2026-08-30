# Goodix5135 V5D4B — SIGFM parser/resource hardening PASS

Date: 2026-08-30

## Result

V5D4B completed successfully in an isolated detached worktree from libfprint baseline `37ac6876fd6d248b48a7892410cf75144f3882e5`.

Cumulative patch:

- Path: `/home/sam/goodix5135-v5d4b-sigfm-parser-hardening.patch`
- SHA256: `9b503c9b969c5848f7319fb86b8cdb8259be564ad6d858a41b669b0fffdaf632`
- Cumulative changed paths: 20

## V5D4B host-only gates

- Build: PASS
- SIGFM robustness: PASS
- SIGFM geometry regression: PASS
- SIGFM image regression: PASS
- SIGFM print regression: PASS
- libfprint `fpi-device`: PASS
- Goodix5135 regression suites: 9/9 PASS
- Patch reapply against the exact real baseline: PASS
- Real libfprint repository remained CLEAN at baseline HEAD

## Parser/resource hardening covered

- stream over-read guard
- attacker-controlled vector-size cap before reserve
- descriptor matrix allocation bounds before `cv::Mat::create()`
- serialized SIGFM size cap (4 MiB)
- strict truncation rejection
- strict trailing-byte rejection
- null API guards
- malformed logical object rejection
- non-finite keypoint/descriptor rejection
- deterministic serialized-byte mutation sweep

## Earlier hardening retained

V5D3 hardening remains included:

- comparator tie-break correction
- zero-length geometry protection
- non-finite geometry protection
- trig-domain clamp for `asin`/`acos`
- safe relative-difference handling

V5D2/V5D1 SIGFM integration remains included:

- SIGFM FpPrint type/storage/serialization/matching
- SIGFM FpImage async extraction
- FpImageDevice generic algorithm selector
- separate SIGFM threshold field
- default algorithm remains NBIS

## Safety invariants

- USB accessed: NO
- Sensor opened: NO
- PSK accessed: NO
- FDT accessed: NO
- Fingerprint used: NO
- Biometric fixture used: NO
- Exact SIGFM scores printed: NO
- Goodix SIGFM selected: NO
- Goodix runtime changed: NO

## Next

1. Back up the exact V5D4B cumulative patch and SHA in the research repository.
2. Bank the cumulative host-only SIGFM stack into the real libfprint branch with a controlled commit.
3. Only after banking, design a tiny guarded Goodix SIGFM opt-in/live experiment. No live threshold has been selected yet.
