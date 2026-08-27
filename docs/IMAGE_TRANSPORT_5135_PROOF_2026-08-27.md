# First TLS-protected image transport proof — Goodix 27c6:5135

Date: 2026-08-27

## Scope

This proves USB command transport, TLS transport, TLS decryption, and Goodix image-message framing for one local capture. It does not publish or include biometric bytes.

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

Strong structural model:

```text
Goodix protocol header  3
image metadata          5
packed 12-bit image  7680
image CRC               4
protocol trailer        1
--------------------------
total                 7693
```

The upstream 12-bit unpacker works on 6-byte groups and emits four 12-bit samples per group.

## Next proof

Before declaring image decoding complete:

1. determine whether the 4-byte image CRC is CRC32/MPEG over packed pixels only, metadata+pixels, or another exact range;
2. determine CRC byte order;
3. require a successful CRC check;
4. decode the 7680 packed bytes to exactly 5120 12-bit samples;
5. save an `80x64` image locally/private only;
6. visually validate locally without uploading it.

The repository must never contain the capture itself.
