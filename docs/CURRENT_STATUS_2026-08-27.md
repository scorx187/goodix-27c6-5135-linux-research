# Current status — 2026-08-27

Target: Goodix USB fingerprint sensor `27c6:5135`, firmware `GF_HC460SEC_APP_12508`, logical chip ID `0x2504`, ChicagoHS sensor type 12, geometry `80x64`.

This document is the canonical current-state checkpoint. Older documents are retained as historical evidence and may describe blockers that have since been resolved.

## End-to-end milestones proven on Linux

```text
USB transport                                  PASS
Firmware / chip identification                 PASS
Factory TLS 1.2 PSK handshake                  PASS
Encrypted-session NOP                          PASS
CFG70 static-template identification           PASS
OTP-derived CFG70 reconstruction               PASS
Exact private Windows runtime parity           PASS
Volatile command 0x90 config upload            PASS
Verified 5135 activation sequence              PASS
FDT manual/live baseline (0x36)                PASS
FDT finger-down ACK + event (0x32)              PASS
FDT finger-up ACK + event (0x34)                PASS
MCU_GET_IMAGE ACK (0x20)                       PASS
TLS-protected image transport                  PASS
TLS application-data decrypt                   PASS
Goodix image framing                           PASS
12-bit image payload structural split          PROVEN BY LENGTH / upstream slicing
Image CRC domain                               NEXT
12-bit -> 5120 pixel decode                    NEXT
80x64 local image validation                   NEXT
Matcher/enrollment                             LATER
libfprint/fprintd integration                  LATER
```

## Important activation-state discovery

After FDT/TLS experiments, direct `enable_chip(True)` could time out and register `0x0000` could read `06000000` instead of the expected `a2042500`.

This was not hardware damage. The exact tested recovery/activation sequence is:

```text
NOP
TLS_SUCCESSFULLY_ESTABLISHED (0xd4) as transient activation-state command
NOP
ENABLE_CHIP true (0x96)
NOP
firmware_version
RESET (0xa2): reset(True, False, 20)
read register 0x0000
```

Result, three consecutive reads:

```text
a2042500
a2042500
a2042500
```

Therefore `06000000` must not be treated as a permanent chip-ID change. On this unit it was a state/order problem.

## FDT proof

A private local `goodix.dat` was validated as:

```text
64-byte OTP
12-byte FDT manual seed
3200-byte NAV base
10240-byte IMAGE base
4-byte CRC32/MPEG
```

The 12-byte FDT region is six duplicated one-byte manual thresholds, not six little-endian `u16` values and not already `0x80 xx` down thresholds.

Linux sequence:

1. `0x36` manual FDT with prefix `0d 01` and the private 12-byte manual seed.
2. Parse reply as `irq:u16le`, `touchflag:u16le`, six live `u16le` zone measurements.
3. Derive FDT-down thresholds as `floor(zone/2)` encoded as six `80 xx` pairs.
4. Send `0x32` with `08 01 + 12 threshold bytes + timestampLE`.
5. Receive finger-down event.
6. Send `0x34` with `0a 02 + 12 FDT-up threshold bytes` from a private Windows trace for this unit.
7. Receive finger-up event.

Successful observed Linux event metadata:

```text
manual IRQ       0x0100
manual touchflag 0x0000
finger-down      5/6 or 6/6 zones depending on press
finger-up IRQ    0x0200
finger-up flag   0x0000
finger-up zones  0/6
```

No fingerprint image is needed to prove FDT.

## First TLS-protected image transport — success

With the finger held after a successful FDT-down event, Linux sent:

```text
COMMAND_MCU_GET_IMAGE = 0x20
payload               = 01 00
```

The sensor returned:

1. a normal message-protocol ACK for command `0x20`;
2. a second Goodix pack with transport flags `0xb0`, declared payload length `7722`;
3. that second payload began directly with a TLS 1.2 application-data record;
4. OpenSSL decrypted it to exactly `7693` bytes.

The decrypted frame metadata was inspected without printing biometric bytes:

```text
total plaintext length = 7693
command byte           = 0x20
declared protocol len  = 7690
protocol trailer       = 0x88
checksum=True parse    = FAIL
checksum=False parse   = PASS
protocol payload len   = 7689
```

So image message protocol uses the no-checksum `0x88` trailer path.

## Image payload structure — strongest current interpretation

Upstream `goodix-fp-dump` `driver_51x0.py` passes decrypted image data through:

```python
tool.decode_image(tls_server.stdout.read(...)[8:-5])
```

For this exact 5135 frame:

```text
7693 - 8 - 5 = 7680
```

and `7680 * 8 / 12 = 5120` pixels, exactly `80*64`.

Therefore the framing is structurally consistent with:

```text
3 bytes  Goodix protocol header
5 bytes  image metadata/status
7680     packed 12-bit pixels
4 bytes  image CRC
1 byte   Goodix protocol trailer 0x88
---------------------------------------
7693 total TLS plaintext
```

Equivalently, within the decoded 7689-byte protocol payload:

```text
5-byte image metadata
7680-byte packed pixel stream
4-byte image CRC
```

The next task is to determine the exact image CRC32/MPEG input domain and byte order, then run the known 6-byte -> 4-pixel unpacking algorithm locally.

## Privacy state

The first decrypted image capture was saved locally/private with mode `0600`. It must not be committed, uploaded, pasted, hashed publicly, or attached to issues.

Do not publish:

- factory PSK or any PSK file/hash;
- full OTP;
- full 224-byte unit-specific runtime config or its unit-specific hash;
- `goodix.dat`, `goodix_calib.dat`, `Goodix_Cache.bin`;
- fingerprint images, raw captures, templates;
- proprietary Goodix Windows binaries;
- process memory dumps.

## Cleanup warnings

`device.disconnect()` often times out with `Device is still connected` after an otherwise successful operation. Treat that as cleanup noise unless the preceding operation itself failed.

The sysfs USB `authorized` 0/1 toggle is a proven transport-recovery method. It re-enumerates USB but should not be described as clearing runtime config.
