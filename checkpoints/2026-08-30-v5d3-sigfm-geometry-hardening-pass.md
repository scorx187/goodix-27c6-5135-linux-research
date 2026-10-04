# Goodix 27c6:5135 — V5D3 SIGFM geometry hardening PASS

Date: 2026-08-30

## Base

- libfprint branch: `goodix-27c6-5135-chicagohu`
- libfprint base HEAD: `37ac6876fd6d248b48a7892410cf75144f3882e5`
- cumulative V5D2 patch: `/home/sam/goodix5135-v5d2e-sigfm-image-selector.patch`
- V5D2 SHA256: `f04e87006624aedc784d23ee8aa07833fa15935811771b89fcd533affac126f5`

## V5D3 result

Host-only SIGFM comparator and geometry hardening completed successfully.

Hardening performed:

- fixed the `match::operator<` equal-Y/X tie-break bug;
- guarded zero-length vectors;
- guarded non-finite geometry values;
- clamped trigonometric ratios to `[-1, 1]` before `asin`/`acos`;
- replaced unsafe relative-ratio calculations with a finite-safe relative-difference helper;
- did not redesign the matcher formula or choose a Goodix live threshold.

## Validation

- Meson build: PASS
- `sigfm-geometry-hardening`: PASS
- `sigfm-image-core`: PASS
- `sigfm-print-core`: PASS
- `fpi-device`: PASS
- Goodix regression suites: 9/9 PASS
- exported cumulative patch re-apply check: PASS

## V5D3 artifact

- local patch: `/home/sam/goodix5135-v5d3-sigfm-geometry-hardening.patch`
- cumulative paths: 19
- SHA256: `9e4ce5607ac2eec198bbb0b9f076e07ac1653cd47c1834fcd1972217140e2e34`

The patch bytes were still local at checkpoint creation time and must be uploaded separately before relying on GitHub alone for recovery.

## Safety / privacy

- USB accessed: NO
- sensor opened: NO
- PSK accessed: NO
- FDT accessed: NO
- fingerprint used: NO
- biometric fixture used: NO
- exact SIGFM scores printed: NO
- Goodix SIGFM selected: NO
- Goodix runtime changed: NO

## Next

Before any Goodix SIGFM live opt-in, perform one final host-only robustness pass covering malformed SIGFM serialization/deserialization and bounded resource handling. Do not perform a live fingerprint run yet.
