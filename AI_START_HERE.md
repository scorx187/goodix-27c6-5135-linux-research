# AI / developer handoff — START HERE

## Goal

Make Goodix USB fingerprint sensor `27c6:5135` work natively under Linux/libfprint **without breaking Windows Hello, factory firmware, factory PSK provisioning, or existing Windows fingerprints**.

## Exact tested device

```text
USB VID:PID: 27c6:5135
Firmware:    GF_HC460SEC_APP_12508
Chip ID:     raw a2042500 / logical 0x2504
Profile:     ChicagoHS / ChicagoHU
Sensor type: 12
Geometry:    80 x 64
Pixels:      5120
USB bulk IN: 0x81
USB bulk OUT:0x01
```

## Current state — do not repeat solved work

As of 2026-08-27 the project is **past config, TLS, FDT, and first image transport**.

```text
CFG70 reconstruction            DONE
command 0x90 upload             DONE
factory TLS                     DONE
verified activation sequence    DONE
FDT manual 0x36                 DONE
FDT-down 0x32                   DONE
FDT-up 0x34                     DONE
image command 0x20 ACK          DONE
TLS image transport             DONE
TLS image decrypt               DONE
image Goodix framing            DONE
CRC range / image decode        NEXT
```

Read first:

1. `docs/CURRENT_STATUS_2026-08-27.md`
2. `docs/IMAGE_TRANSPORT_5135_PROOF_2026-08-27.md`
3. `docs/FDT_5135_PROOF_2026-08-27.md`
4. `docs/LINUX_CFG70_UPLOAD_PROOF_2026-08-27.md`
5. `docs/FAILURES_AND_RECOVERIES_2026-08-27.md`
6. `docs/SAFETY.md`
7. `docs/DEVELOPER_ROADMAP.md`

## Immediate next task

A private local TLS plaintext capture already exists on the user's machine. **Do not ask the user to upload it.**

Its safe metadata is:

```text
total plaintext       7693 bytes
command               0x20
declared protocol len 7690
protocol trailer      0x88
protocol checksum     disabled / 0x88 mode
protocol payload      7689 bytes
```

Upstream `driver_51x0.py` uses decrypted image slice `[8:-5]`. For this frame that produces exactly `7680` packed bytes, which is exactly `5120 * 12 / 8`.

Therefore next:

1. run `scripts/inspect_private_image_capture_5135.py` against the private local capture;
2. determine the exact 4-byte image CRC32/MPEG domain/endian;
3. require CRC PASS before calling the image format fully proven;
4. decode the 7680 bytes using the known 6-byte -> 4x12-bit algorithm;
5. write an 80x64 PGM locally/private and visually inspect it locally;
6. never upload/paste the image or capture.

## Verified 5135 activation sequence

When `enable_chip(True)` times out or register 0 reads `06000000`, use the **tested sequence** rather than assuming hardware damage:

```text
NOP
0xd4 TLS_SUCCESSFULLY_ESTABLISHED (transient activation-state command)
NOP
0x96 ENABLE_CHIP true
NOP
firmware_version
0xa2 reset(True, False, 20)
read register 0x0000
```

This restored three consecutive `a2042500` reads.

## Factory TLS

```text
TLS version: TLS 1.2
cipher:      PSK-AES128-GCM-SHA256
identity:    Client_identity
```

The factory host PSK was recovered privately from the user's own Windows DPAPI-protected Goodix cache. **Never request, print, commit, or re-provision it.**

## CFG70 / command 0x90

The original config blocker is solved.

- Windows upload length: exactly 224 bytes (`0xe0`).
- Correct static family: CFG70, not inferred from command number but proven by private exact parity.
- Linux rebuild from live OTP matched the private Windows runtime reference byte-for-byte.
- Config checksum rule is proven.
- One controlled Linux `0x90` upload was accepted and post-upload calibration verified.
- Do not publish the full unit-specific runtime config or its private hash.

Public proof chain:

- `docs/CFG70_RUNTIME_PROOF_2026-08-27.md` — Windows runtime reconstruction proof.
- `docs/ISSUE_1_RESOLUTION_2026-08-27.md` — original config blocker closure.
- `docs/LINUX_CFG70_UPLOAD_PROOF_2026-08-27.md` — Linux build/upload proof and limitations.

## FDT

- `goodix.dat` layout: `OTP64 + FDT12 + NAV3200 + IMAGE10240 + CRC4`.
- FDT12 is six duplicated manual seed bytes.
- manual `0x36`: `0d01 + seed12`.
- manual event: IRQ/touchflag/six u16 zone values.
- down thresholds: `floor(raw/2)`, encoded as six `80 xx` pairs.
- down `0x32`: `0801 + regs12 + timestampLE`.
- up `0x34`: `0a02 + regs12`.
- current FDT-up success uses a private Windows-traced per-unit threshold set; generic derivation remains future work.

## Image transport facts

Image request:

```text
command 0x20
payload 01 00
```

Reply order:

```text
normal ACK for 0x20
then Goodix transport frame flags 0xb0, len 7722
TLS 1.2 application-data at offset 0
TLS plaintext len 7693
```

The decrypted Goodix message uses `checksum=False` / trailer `0x88`.

Do not repeat the old `0xb2` assumption.

## Mandatory privacy/safety

Never publish or ask the user to upload:

- plaintext factory PSK;
- PSK files or hashes;
- full OTP;
- fingerprint images/raw frames/templates;
- `goodix.dat`, `goodix_calib.dat`, `Goodix_Cache.bin`;
- proprietary Windows DLL/EXE/CAT;
- process memory dumps;
- full unit-specific 224-byte runtime config or its private hash.

Never erase/flash firmware or write/re-provision PSK.

A runtime config upload (`0x90`) is a volatile MCU write. Describe it accurately.

## Working style

The user prefers one terminal block at a time and pastes output. Avoid unnecessary questions. Preserve successful checkpoints in this repository frequently so a chat/session limit cannot destroy progress.
