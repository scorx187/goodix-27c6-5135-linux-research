# Developer roadmap

## Phase 1 — device identity / factory compatibility — DONE

- `27c6:5135`
- `GF_HC460SEC_APP_12508`
- chip `0x2504`
- ChicagoHS / sensor type 12
- 80x64
- preserve Windows Hello / factory PSK / firmware

## Phase 2 — runtime config — DONE for parity/upload

- exact 224-byte command `0x90`
- CFG70 selection proven
- OTP CRC rules proven
- OTP-to-runtime calibration mutations proven
- checksum proven
- private exact Windows parity proven
- one controlled Linux upload accepted

Still optional/later: prove recovery from a naturally missing runtime-config state if this becomes necessary for cold boot behavior.

## Phase 3 — TLS — DONE

- TLS 1.2 PSK
- `PSK-AES128-GCM-SHA256`
- factory key used locally, never reprovisioned
- encrypted NOP works

## Phase 4 — finger detection — DONE

- FDT manual baseline `0x36`
- FDT-down `0x32`
- FDT-up `0x34`
- finite event timeouts

Generic FDT-up threshold derivation is not yet proven; current success used a private Windows-traced per-unit up calibration.

## Phase 5 — image transport — DONE

- command `0x20`, payload `01 00`
- normal ACK
- TLS frame flags `0xb0`
- encrypted payload `7722`
- decrypted message `7693`
- command `0x20`, no-checksum trailer `0x88`, payload `7689`

## Phase 6 — image validation/decoding — NEXT

1. split payload into 5 metadata + 7680 packed + 4 CRC;
2. test CRC32/MPEG candidate domains/endianness without printing bytes;
3. require CRC PASS;
4. apply upstream 6-byte -> 4x12-bit unpacking;
5. require exactly 5120 values in range 0..4095;
6. write local private PGM/PNG for visual validation;
7. compare only non-biometric metadata publicly.

## Phase 7 — calibration / preprocessing

Use official Windows behavior as the oracle where necessary. Do not assume a raw image alone is sufficient for matching. Investigate image-base subtraction/gain/quality logic and the meaning of the 5-byte image metadata.

A recent independent SPI project for ChicagoHS/chip `0x2504` may be useful as a sensor-layer comparison, but USB transport and this exact firmware must remain evidence-driven.

## Phase 8 — enrollment / matching

Options to evaluate:

- reuse/port known Chicago preprocessing and matcher logic if license/provenance permit;
- evaluate `sigfm` or libfprint-compatible matcher paths;
- preserve all captured biometric fixtures locally/private;
- build synthetic/public deterministic tests instead of committing real prints.

## Phase 9 — libfprint/fprintd

Implement a native driver/state machine with:

- USB VID/PID detection;
- safe activation;
- runtime-config bootstrap;
- factory TLS use without PSK rewrite;
- cancellation-safe FDT waits;
- image capture/CRC/decode;
- suspend/resume recovery;
- enrollment/verify integration;
- no logging of secrets/biometric payloads.

## Definition of success

`fprintd-enroll` and `fprintd-verify` work reliably on `27c6:5135` under Linux across reboot/suspend while Windows Hello continues to work unchanged.
