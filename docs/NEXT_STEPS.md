# Next steps

## Completed prerequisites — do not repeat

CFG70 identification/reconstruction, Linux command `0x90` acceptance, factory TLS, activation ordering, and FDT down/up are already proven. Preserve those milestones rather than reopening them.

Evidence:

- `CFG70_RUNTIME_PROOF_2026-08-27.md`
- `ISSUE_1_RESOLUTION_2026-08-27.md`
- `LINUX_CFG70_UPLOAD_PROOF_2026-08-27.md`
- `FDT_5135_PROOF_2026-08-27.md`
- `IMAGE_TRANSPORT_5135_PROOF_2026-08-27.md`

## Immediate: validate and decode the already captured private image frame

Do not re-run CFG70/TLS/FDT simply to obtain information that is already present in the local private capture.

Safe known metadata:

```text
TLS plaintext: 7693 bytes
Goodix command: 0x20
protocol declared length: 7690
protocol trailer: 0x88
protocol payload: 7689 bytes
```

Upstream 51x0 image handling slices decrypted data `[8:-5]`, which yields exactly 7680 packed bytes for this device.

### Task A — CRC proof

Run the public-safe local inspector against the private capture:

```text
scripts/inspect_private_image_capture_5135.py
```

Test CRC32/MPEG over likely domains without printing bytes. Record only which domain/endian matches.

### Task B — 12-bit decode

Use the upstream Goodix 6-byte -> 4-pixel mapping. Require:

```text
input  7680 bytes
output 5120 samples
range  0..4095
shape  80x64
```

Write the image only to a local private file with mode `0600`. Do not upload it.

### Task C — local visual validation

The user may inspect the image locally. Public notes should record only non-biometric facts: dimensions, CRC status, byte counts, and whether the image was structurally valid.

## After first valid image

1. understand the five image metadata bytes;
2. capture a finger-off reference and compare only locally;
3. reproduce Windows ImageBase/calibration/preprocessing behavior;
4. establish reliable quality/coverage criteria;
5. evaluate matcher/enrollment path;
6. design native libfprint state machine;
7. test cold boot, suspend/resume, cancellation, repeated enroll/verify;
8. confirm Windows Hello still works unchanged.

## Known unfinished research

- generic FDT-up threshold derivation;
- exact image CRC domain until next local inspector run;
- image preprocessing/calibration;
- matcher/enrollment/template format;
- production libfprint/fprintd integration.
