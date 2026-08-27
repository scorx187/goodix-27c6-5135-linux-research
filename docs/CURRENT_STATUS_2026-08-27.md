# Current status — 2026-08-27

Target: Goodix USB fingerprint sensor `27c6:5135`, firmware `GF_HC460SEC_APP_12508`, logical chip ID `0x2504`, ChicagoHS / ChicagoHU sensor type 12.

This document is the canonical current-state checkpoint. Older documents are retained as historical evidence and may describe blockers that have since been resolved.

## End-to-end milestones

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
Windows-compatible image CRC                   PASS (one private capture)
RAW12 -> 5120 sample decode                     PASS
ChicagoHU regroup mapping                      PROVEN
Windows live-image layout                      PROVEN
ImageBase/live same-index order                PROVEN
Candidate A: ImageBase stored as downstream    PROVEN
Candidate B: regroup ImageBase again            REJECTED
Post-detection callback target                 PROVEN (0x180013280)
gfusb.dll image request handoff boundary       PROVEN
Naive direct PGM                               STRUCTURAL BUT VISUALLY WRONG
Higher-layer matcher preprocessing             NEXT
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

The current FDT-up proof uses a private per-unit Windows-traced threshold set; a generic FDT-up derivation remains future work.

## TLS image transport and framing

With the finger held after a successful FDT-down event, Linux sent:

```text
COMMAND_MCU_GET_IMAGE = 0x20
payload               = 01 00
```

The sensor returned a normal `0x20` ACK followed by a Goodix transport frame with flags `0xb0`. TLS 1.2 application data decrypted to exactly 7693 bytes.

Proven structural model:

```text
Goodix protocol header  3
image metadata          5
packed RAW12         7680
image CRC               4
protocol trailer         1
--------------------------
total                 7693
```

The protocol trailer is `0x88` / no-checksum mode. The old `0xb2` expectation for this second frame was incorrect.

## Windows-compatible image CRC

The Windows image checker uses CRC-32/MPEG-2:

```text
poly    0x04C11DB7
init    0xFFFFFFFF
refin   false
refout  false
xorout  0
```

For the four stored field bytes `[a,b,c,d]`, Windows reconstructs:

```text
(c << 24) | (d << 16) | (a << 8) | b
```

The checked domain for the 5135 image path is exactly:

```text
7680 packed RAW12 bytes + 4 stored CRC bytes
```

A private local reimplementation matched one real capture. No image bytes or CRC value were published. A second independent capture is still desirable as cross-capture confirmation.

## RAW12 and ChicagoHU regroup

The 7680 packed bytes decode to exactly 5120 12-bit samples.

Keep the geometry distinction explicit:

```text
packed transport : 64 fast x 80 slow
samples          : 5120
Chicago output   : 80 columns x 64 rows
16-bit plane     : 10240 bytes
```

The Windows-compatible Chicago regroup mapping is:

```text
dst = (n % 64) * 80 + (n / 64)
```

The mapping is proven from Windows disassembly and agrees with the public ChicagoHU implementation in upstream research.

## Windows ImageBase/live layout — proven end-to-end

The 0x2504 family path selects family/type `0x0c` and initializes the Chicago object at `0x180588dc0`.

The initializer installs full-image callback `0x1800289f8` at `object + 0x13d48`.

On a full 5135 image, that callback:

1. checks image CRC;
2. selects packed length `0x1e00` (7680);
3. calls `0x180023e38` to decode/regroup into a temporary 16-bit plane;
4. copies that plane into the pointer stored at `object + 0x13cc0`.

The key identity is:

```text
0x180588dc0 + 0x13cc0 = 0x18059ca80
```

`0x18059ca80` is the runtime live-image buffer pointer allocated by the common layer. Therefore the Chicago callback writes its regrouped full-image output into the exact live-image buffer later copied into the capture routine.

The capture routine compares that live image with persisted ImageBase from runtime slot `0x18059ca88` using the same pixel index. The persisted ImageBase load/save paths copy the 10240-byte image plane directly and do not apply another regroup.

Therefore:

```text
Candidate A: persisted ImageBase is already in the downstream regrouped layout  PROVEN
Candidate B: regroup persisted ImageBase again                                  WRONG
```

Detailed proof: `docs/WINDOWS_IMAGE_LAYOUT_5135_PROOF_2026-08-27.md`.

## Detector/classifier role

The base/live classifier computes same-index tile statistics and directional pixel differences. Windows labels its return states:

```text
0 = temperature
1 = finger down
2 = void
3 = bad
```

It is a base/live detector/classifier, not the final matcher-image preprocessing stage.

## Post-detection callback and gfusb.dll handoff boundary

Runtime slot `0x18059cb60` is registered through setter `0x1800621b8`. The concrete callback registered into that slot is `0x180013280`.

Capture call sites invoke it with the effective shape:

```text
callback(context, ImageBase, live_image, flags)
```

Static analysis of `0x180013280` proves that it copies the ImageBase and live 10240-byte planes into a large result package and fills metadata. It does not perform per-pixel subtraction, normalization, clamping, or cropping before submission.

The package is then handed to `0x18001393c` with total result size `0xeb88` and payload size `0xeb70`.

`0x18001393c` is a generic pending-request completion helper. On the success path it copies the `0xeb70` payload into the request output buffer at offset `+0x14`, completes the request via `0x18001d64c`, then clears the pending request/output pointers. Other call sites use the same helper for Windows error completions, confirming this role.

Therefore the examined `gfusb.dll` path hands the two already-regrouped image planes to a higher Windows component without matcher preprocessing in the post-detection callback.

## Current blocker / immediate next task

Do not keep searching the detector, `0x180013280`, or `0x18001393c` for matcher arithmetic.

The next task is to identify the Windows user-mode/biometric component that opens this driver interface and consumes the `0xeb88` image result. Trace the two 10240-byte 80x64 `u16` planes from that consumer into the actual matcher/feature extractor and prove, if present:

- subtraction direction;
- clamp/saturation;
- scaling/normalization;
- per-pixel correction;
- crop/output dimensions;
- possible `goodix_calib.dat` involvement;
- exact matcher input buffer.

## Privacy state

Private captures and calibration material remain local-only. Never publish or request:

- plaintext factory PSK or any PSK file/hash;
- full OTP;
- full 224-byte unit-specific runtime config or its private hash;
- `goodix.dat`, `goodix_calib.dat`, `Goodix_Cache.bin`;
- fingerprint images, raw captures, templates;
- proprietary Goodix Windows binaries;
- process memory dumps.

## Cleanup warnings

`device.disconnect()` often times out with `Device is still connected` after an otherwise successful operation. Treat that as cleanup noise unless the preceding operation itself failed.

The sysfs USB `authorized` 0/1 toggle is a proven transport-recovery method. It re-enumerates USB but should not be described as clearing runtime config.
