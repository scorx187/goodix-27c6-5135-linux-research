# First TLS-protected image transport proof — Goodix 27c6:5135

Date: 2026-08-27

## Scope

This proves USB command transport, TLS transport, TLS decryption, Goodix image-message framing, packed RAW12 decoding, and a Windows-compatible image CRC check for one local capture. It does not publish or include biometric bytes, CRC values, OTP, PSK material, or per-unit runtime configuration.

## Windows reference behavior

Windows logs show:

```text
ChicagoHUSetMode: Mode 2, Type 0
setmode: Image
command 0x20, payload length 2
ACK for cmd 0x20
TLS packet received
TLS plaintext length 7693
received command 0x20 length 7690
raw-data stage: 7684 -> 7684 -> 10240
Regroup pixels: 5120
image CRC check Ok
```

## Linux proof

The Linux sequence was:

```text
verified 5135 activation
factory TLS handshake
FDT manual baseline
FDT-down event (finger present)
command 0x20 payload 01 00
normal 0x20 ACK
second Goodix transport frame
TLS decrypt
FDT-up event
```

Observed second Goodix frame:

```text
flags              0xb0
pack length         7722
available bytes     7722
TLS record offset   0
```

Observed decrypted frame metadata:

```text
total bytes          7693
command              0x20
declared protocol len 7690
trailer               0x88
checksum mode          no-checksum / 0x88
protocol payload       7689 bytes
```

This means the local `goodix.py` call must request/accept `FLAGS_TRANSPORT_LAYER_SECURITY` (`0xb0`) for this image path, not `FLAGS_TRANSPORT_LAYER_SECURITY_DATA` (`0xb2`).

## Important debugging correction

An early image probe used upstream `mcu_get_image(..., flags=0xb2)` and failed with `Invalid message pack`.

Inspection of `mcu_get_image()` proved it already consumes the normal ACK before reading the second frame. Therefore the error was not "ACK mistaken for TLS"; it was the wrong expected pack flags for the second frame.

The corrected probe reads the ACK, then decodes the second Goodix pack header before deciding which TLS transport class it is.

## Structural split

Upstream `driver_51x0.py` uses decrypted-data slice `[8:-5]` as input to `tool.decode_image()`.

For the 5135 frame:

```text
7693 total - 8 leading - 5 trailing = 7680 packed bytes
7680 bytes * 8 / 12 = 5120 samples
5120 = 80 * 64
```

Proven structural model:

```text
Goodix protocol header  3
image metadata          5
packed 12-bit image  7680
image CRC               4
protocol trailer        1
--------------------------
total                 7693
```

The upstream 12-bit unpacker works on 6-byte groups and emits four 12-bit samples per group. The local private inspector decoded exactly 5120 samples, all in the 12-bit range 0..4095.

## Windows-compatible image CRC proof

Reverse engineering of the local Windows `gfusb.dll` found the generic CRC implementation and a checker with the same signature and data shape used by the image path.

The CRC implementation initializes its table with polynomial `0x04C11DB7` and uses an initial state of `0xFFFFFFFF`, non-reflected input/output, and no final XOR. This is CRC-32/MPEG-2.

The checker computes CRC over the first `length - 4` bytes, then reconstructs the trailing four-byte field in this order. For stored bytes `[a, b, c, d]`:

```text
crcchip = (c << 24) | (d << 16) | (a << 8) | b
```

Equivalently, the wire field stores the CRC with its two 16-bit halves swapped relative to normal big-endian byte order.

The Windows image receive path passes a pointer five bytes past the image-message metadata and a length equal to the message length minus six. For this 5135 frame that corresponds exactly to:

```text
7680 packed RAW12 bytes + 4 CRC bytes = 7684 bytes
```

A private local reimplementation of that Windows-compatible checker was run against the captured image and returned:

```text
WINDOWS IMAGE CRC MATCH: PASS
CRC domain              : packed RAW12 only (7680 bytes)
Checked block           : packed RAW12 + CRC (7684 bytes)
```

No image bytes, pixel values, CRC values, or private hashes were printed or committed.

### Confidence note

The indirect image-checker function pointer is populated at runtime in a BSS-backed slot, so its exact static target could not be read directly from the PE file. However, the reconstructed Windows checker, its CRC implementation, the image-path pointer/length arithmetic, and the successful real-capture comparison all agree. A second independent capture is still recommended as a cross-capture confirmation before treating the field layout as invariant across every firmware/unit.

## Safety gate for local image output

The public private-capture inspector now refuses `--write-pgm` unless the Windows-compatible image CRC check passes. This prevents visual decoding of a frame that failed integrity verification.

The repository must never contain the capture itself, a fingerprint image, raw/template biometric data, PSK/OTP material, or private per-unit configuration.

## Next proof

1. confirm the same CRC rule on a second independent private capture;
2. write a private `80x64` PGM only after CRC verification passes;
3. visually validate locally without uploading the image;
4. determine the ChicagoHU regroup/orientation expected by the downstream matcher;
5. integrate the proven image path into the libfprint driver without changing firmware or factory PSK state.
