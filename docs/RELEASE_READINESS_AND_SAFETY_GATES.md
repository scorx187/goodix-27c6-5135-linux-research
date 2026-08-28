# Release readiness and safety gates

This document defines what "ready" means for the `27c6:5135` Linux/libfprint work.

Important wording: no reverse-engineered biometric driver can honestly be called **100% safe in the absolute sense**. The practical target is **all defined safety gates passed, no known unsafe behavior, and no persistent device modification required**.

## Gate A — Preserve factory state

Must all be true:

- no MCU firmware erase;
- no firmware flashing;
- no factory PSK write/replacement/re-provisioning;
- no arbitrary persistent register writes;
- no Windows fingerprint deletion/re-enrollment required by Linux;
- runtime configuration writes are limited to the already-proven volatile command `0x90` path;
- Windows Hello still works after Linux tests.

Status: **PASS so far**, but must be revalidated after final integration testing.

## Gate B — Secret and biometric-data containment

Must all be true:

- factory PSK never appears in normal logs;
- OTP/private runtime config are not logged;
- raw fingerprint frames are not logged by default;
- templates stay local and permission-restricted;
- debug image output is explicit opt-in and private-only;
- crash/error paths do not dump biometric buffers;
- repository tests use synthetic/non-biometric fixtures.

Status: **policy established; final implementation audit pending**.

## Gate C — Exact image pipeline

Must all be true:

- image CRC behavior validated on multiple independent captures;
- RAW12 decode exact;
- ChicagoHU regroup exact;
- ImageBase/live layout exact;
- full Windows preprocessing control flow understood enough to reproduce required behavior;
- calibration handling understood;
- exact matcher input geometry/buffer proven;
- no unexplained dependency on proprietary runtime state remains.

Status: **in progress**. Transport/layout are solved; higher-layer preprocessing is the main current blocker.

## Gate D — Enrollment and verification correctness

Must all be true:

- repeated enroll succeeds;
- enrolled finger verifies repeatedly;
- non-enrolled fingers are rejected reliably;
- bad/partial captures recover cleanly;
- template update/study behavior is understood or safely replaced;
- no real biometric fixtures are committed publicly.

Status: **not started end-to-end**.

## Gate E — libfprint driver quality

Must all be true:

- native device discovery for `27c6:5135`;
- finite timeouts everywhere;
- cancellation-safe state machine;
- no indefinite USB waits;
- device disconnect/reconnect recovery;
- no stale TLS/FDT state after errors;
- clean resource/buffer lifetime;
- normal libfprint error mapping;
- no secret/biometric logging.

Status: **not complete**.

## Gate F — power-cycle and lifecycle reliability

Test matrix must include:

- cold boot -> enroll/verify;
- warm reboot -> verify;
- suspend -> resume -> verify;
- repeated device use;
- failed capture -> retry;
- cancellation during finger wait;
- cancellation during image wait;
- USB re-enumeration recovery;
- Windows boot after Linux testing -> Windows Hello verification.

Status: **pending final driver**.

## Gate G — regression and stress testing

At minimum:

- repeated capture loops;
- repeated verify loops;
- repeated enroll/delete cycles using local Linux templates;
- malformed/short frame handling using synthetic fixtures;
- CRC failure handling;
- timeout handling;
- TLS/session failure recovery;
- no memory growth across long runs;
- no persistent device-state drift.

Status: **pending**.

## Gate H — public-source hygiene

Before a public-ready release:

- no proprietary Goodix binaries;
- no extracted proprietary data blobs;
- no PSK material or hashes;
- no real fingerprints/templates;
- no unit-specific full OTP/config;
- licenses/provenance reviewed for any reused code;
- reverse-engineering notes contain conclusions/code interfaces rather than copied proprietary implementation bodies.

Status: **PASS as project policy; must be checked before release**.

## Completion levels

### Research-complete

We can explain and reproduce the complete required capture/preprocess/matcher-input path with synthetic tests.

### Driver-complete

`fprintd-enroll` and `fprintd-verify` work on Linux without proprietary Goodix runtime DLLs.

### Release-ready

All Gates A-H pass on the tested device, including reboot/suspend/error recovery and Windows Hello coexistence.

### "Safety complete"

Use this phrase only to mean **all defined safety gates passed with no known unsafe behavior**. Do not claim mathematical or universal 100% safety.
