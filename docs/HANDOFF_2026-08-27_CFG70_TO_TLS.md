> **Historical checkpoint:** CFG70, TLS, FDT, and first image transport have since been completed. Start from `AI_START_HERE.md` and `docs/CURRENT_STATUS_2026-08-27.md`.

# Handoff — CFG70 Complete, Continue at TLS/FDT

**Date:** 2026-08-27

## Start here

Repository: `scorx187/goodix-27c6-5135-linux-research`

Read, in order:

1. `AI_START_HERE.md`
2. `docs/CFG70_RUNTIME_PROOF_2026-08-27.md`
3. `docs/LINUX_CFG70_UPLOAD_PROOF_2026-08-27.md`
4. `docs/PROTOCOL.md`
5. `docs/SAFETY.md`
6. `docs/NEXT_STEPS.md`

## Do not redo

The following are proven and should not be reopened without contradictory evidence:

- target is Goodix USB `27c6:5135`
- firmware tested: `GF_HC460SEC_APP_12508`
- chip logical ID: `0x2504`
- sensor family: ChicagoHS / type 12
- image geometry: 80x64
- Windows runtime config upload command: `0x90`
- exact runtime config payload length: 224 bytes
- static source template for this tested device: CFG70
- command `0x90` does not imply template CFG90
- six OTP-derived config mutation fields are identified
- checksum rule is proven
- Linux can derive the same runtime config from live OTP
- Linux-generated config matched the private Windows runtime reference byte-for-byte
- Linux `upload_config_mcu()` / `0x90` was accepted by the device
- post-upload calibration was verified
- direct / repeated reset experiments are no longer the priority

## Important reset findings

Do not blindly repeat reset attempts.

- `reset(True, False, 20)` directly may timeout.
- initialized sequence `NOP -> enable_chip(True) -> NOP -> firmware -> reset(True, False, 20)` returns `(True, 2048)`.
- despite reset success, runtime calibration remained loaded.
- sysfs authorize toggle recovers transport but preserves config.
- PyUSB/libusb reset preserves config.
- local `usbreset` utility was unreliable.
- `disconnect()` timeout can occur after otherwise successful work.

## Privacy

Never publish:

- full OTP
- PSK or PSK hash
- full runtime config
- per-device private runtime hash
- fingerprints/templates/images
- `goodix.dat`, `goodix_calib.dat`, `Goodix_Cache.bin`
- Windows memory dumps
- proprietary Goodix binaries

The terminal output from the first successful Linux `0x90` test included the full config because the local Goodix library prints function arguments. Do not paste that terminal line into public issues/docs.

## Current local assets

Expected private/local files/tools include:

```text
~/goodix-fp-dump/
~/goodix-fp-dump/.venv/
~/goodix-fp-dump/tls_probe_5135.py
~/goodix-private/cfg70-static.private.bin
```

Public repository scripts should keep private inputs outside Git.

## Exact next task

Continue with the already-configured device.

First rerun:

```text
tls_probe_5135.py
```

Confirm the previously proven TLS 1.2 PSK handshake still succeeds after the Linux CFG70 upload.

Then continue directly to FDT and image acquisition:

```text
TLS
 -> FDT finger-down
 -> FDT finger-up
 -> TLS-protected image request
 -> 12-bit wire unpacking
 -> local-only image analysis
```

Do not request the user to repeat Windows CFG70 reverse engineering.

Do not request another full process dump.

Do not request plaintext PSK or full OTP.
