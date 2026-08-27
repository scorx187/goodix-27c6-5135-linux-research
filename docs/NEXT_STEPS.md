# Next steps

## Resolved blocker: exact 224-byte ChicagoHS MCU config

The former template-selection blocker is closed for the tested `27c6:5135` / `GF_HC460SEC_APP_12508` device.

Proven Windows path:

```text
CFG70 static template
  -> OTP-derived ChicagoHS calibration
  -> recompute 16-bit little-endian checksum
  -> exact 224-byte runtime configuration
  -> upload with command 0x90
```

The clean reconstruction matched the Windows runtime buffer byte-for-byte:

```text
remaining differences: 0
full 224-byte match: true
checksum verification: true
```

See:

- [`CFG70_RUNTIME_PROOF_2026-08-27.md`](CFG70_RUNTIME_PROOF_2026-08-27.md)
- [`ISSUE_1_RESOLUTION_2026-08-27.md`](ISSUE_1_RESOLUTION_2026-08-27.md)

## Immediate blocker: Linux dry-run builder

Implement a ChicagoHS config builder that:

1. Requires USB PID `27c6:5135`.
2. Verifies `GF_HC460SEC_APP_12508` / expected HC460 firmware family before mutation logic.
3. Reads the 64-byte OTP.
4. Derives the six calibration fields:
   - `0x005c`
   - `0x0220`
   - `0x0236`
   - `0x0238`
   - `0x023a`
   - `0x0082`
5. Starts from the proven CFG70 template.
6. Applies only the six derived substitutions.
7. Recomputes the Goodix 16-bit checksum over bytes `0..221`, seed `0xa5a5`, and stores it little-endian at `0xde..0xdf`.
8. Requires an exact final length of 224 bytes.
9. Does not print the full OTP or resulting per-device config by default.
10. Does **not** perform USB writes in dry-run mode.

## Local parity gate

On the original research machine:

1. Build the config from CFG70 + OTP using Linux code.
2. Compare it against the private Windows-derived reference.
3. Require zero byte differences.
4. Verify checksum independently.

Do not commit the private reference, full OTP, process-memory dump, or reconstructed per-device payload.

## Controlled volatile upload

Only after the Linux dry-run builder reaches exact parity:

1. Use only already-understood volatile initialization operations.
2. Upload the validated 224-byte config with command `0x90`.
3. Query MCU state.
4. Verify `have config = 1`.
5. Read the expected calibrated register state.
6. Continue to the already-proven factory TLS path.

No firmware erase, firmware replacement, or PSK provisioning is required.

## Capture pipeline after config + TLS

Then:

1. reproduce FDT down/up,
2. request image mode,
3. decode the 12-bit wire payload,
4. regroup to `80 x 64` samples,
5. keep fingerprint frames private during development,
6. integrate into libfprint only after the userspace path is stable.
