# Linux libfprint Goodix 27c6:5135 scaffold — 2026-08-28

Target: Goodix USB fingerprint sensor `27c6:5135`, ChicagoHU/type `0x0c`.

## Local libfprint baseline

- Upstream libfprint source baseline: `v1.94.9`, SHA `dc8b05f0a30e93174e861977cede8427c22f8f76`.
- Development branch: `goodix-27c6-5135-chicagohu`.
- Clean upstream/default build passed before driver changes.
- Targeted `-Ddrivers=goodix5135` build passed.
- Full default-driver regression build completed successfully; subsequent `ninja -n` reported `no work to do`.

## Implementation commits

### 1. Build-only image-device scaffold

Local libfprint commit:

`5598bb061fe406d781c202758e231ff0ce661af2`

Commit subject:

`goodix5135: add build-only ChicagoHU image-device scaffold`

The generated driver registry contains `fpi_device_goodix5135_get_type()` and supported-device metadata contains:

`27c6:5135 | Goodix 27c6:5135 ChicagoHU Fingerprint Sensor`

The scaffold is an `FpImageDevice` with press-sensor semantics and downstream geometry `80x64`.

### 2. Host-only CRC and ChicagoHU layout helpers

Local libfprint commit:

`d73798b21d82509404e402b04ab6f402b601e352`

Commit subject:

`goodix5135: add ChicagoHU image layout helpers`

Added pure host-side CRC-32/MPEG-2 and ChicagoHU regroup helpers. Synthetic tests passed; no device I/O was enabled.

### 3. RAW12 decode and permanent Meson unit tests

Local libfprint commit:

`86c6abf8f6684e4e1015d1fdddfb1a20444912a3`

Commit subject:

`goodix5135: add RAW12 image decoding tests`

Added the proven packed RAW12 decode:

```text
p0 = ((b0 & 0x0f) << 8) | b1
p1 = (b3 << 4) | (b0 >> 4)
p2 = ((b5 & 0x0f) << 8) | b2
p3 = (b4 << 4) | (b5 >> 4)
```

Also added stored CRC-field interpretation:

```text
[a, b, c, d] -> c<<24 | d<<16 | a<<8 | b
```

Permanent Meson unit test `goodix5135-image` covers CRC, CRC-field ordering, RAW12 decode, and ChicagoHU regroup using synthetic vectors only.

### 4. Host-only decrypted image-frame parser

Local libfprint commit:

`42b6bbfcb69fe6a9e2cc38a76e95e233badae8ea`

Commit subject:

`goodix5135: add image response frame parser`

Added a strict host-only parser for the proven decrypted command-`0x20` image response:

```text
command            0x20
declared length     7690 (little-endian)
metadata            5 bytes
packed RAW12        7680 bytes
stored image CRC    4 bytes
trailer             0x88
total plaintext     7693 bytes
```

The parser exposes borrowed views for metadata, packed RAW12, and stored CRC without copying or logging biometric bytes. Permanent Meson test `goodix5135-proto` passes seven synthetic cases: valid frame, wrong command, wrong declared length, wrong trailer, truncated frame, oversized frame, and null arguments. Together, `goodix5135-image` and `goodix5135-proto` pass 2/2 under `meson test`.

### 5. Validated end-to-end host image-response pipeline

Local libfprint commit:

`1c7441fb6c69c092f26c35a055bcb97d4d9106d4`

Commit subject:

`goodix5135: add validated image response pipeline`

Added `goodix5135_decode_image_response()` as a pure host-side composition of the already-proven pieces:

```text
7693-byte decrypted command-0x20 response
      ↓
strict frame validation
      ↓
stored CRC decode + CRC-32/MPEG-2 over packed RAW12
      ↓
Goodix RAW12 decode (7680 bytes -> 5120 u16)
      ↓
ChicagoHU regroup
      ↓
80 x 64 u16 downstream plane
```

Permanent Meson test `goodix5135-image-response` passes seven synthetic cases including full valid decode, packed-data CRC corruption rejection, stored-CRC corruption rejection, bad frame, wrong total length, undersized output, and null arguments. The valid case verifies the transport-to-ChicagoHU mapping across all 5120 output samples, not only corners.

At this checkpoint the three permanent Goodix host suites pass 3/3 under `meson test`:

- `goodix5135-image`
- `goodix5135-proto`
- `goodix5135-image-response`

## Current proven host-side image foundation

```text
0x20 frame validation
      ↓
metadata / packed RAW12 / stored CRC views
      ↓
CRC-32/MPEG-2 + stored CRC interpretation
      ↓
RAW12 7680 bytes -> 5120 u16 samples
      ↓
ChicagoHU regroup
      ↓
80 x 64 u16 downstream plane
```

## Safety state

The current branch still does **not** implement Goodix device-protocol USB transfers, TLS, FDT, activation, runtime configuration upload, image capture, persistent register writes, firmware operations, PSK provisioning, or biometric logging.

No system-installed libfprint/fprintd was replaced or installed from this branch during these checkpoints.

No private capture, fingerprint image/raw/template, factory secret, PSK material, full OTP, unit-specific runtime configuration, proprietary Goodix binary, or Windows biometric database material is present in the tests.

## Next implementation step

Run a fresh full `-Ddrivers=default` regression build at local libfprint commit `1c7441fb6c69c092f26c35a055bcb97d4d9106d4` and re-run the permanent Goodix host suites from that build. Confirm that the Goodix additions do not break any default driver build targets or generated driver metadata. Only after this regression gate is green should work move to an isolated, asynchronous USB transport abstraction with explicit timeout/cancellation semantics. Do not send Goodix protocol commands or create a TLS session as part of the regression gate.
