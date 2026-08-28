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

Compose the proven host-only pieces into one end-to-end image-response decoder that accepts exactly one decrypted 7693-byte command-`0x20` response, validates framing, validates the stored image CRC against CRC-32/MPEG-2 over the 7680 packed RAW12 bytes, decodes RAW12, performs ChicagoHU regroup, and returns the `80x64` u16 plane. Add only synthetic unit tests, including CRC corruption rejection and structural failures. Keep USB/TLS/device I/O disabled until this host-side decode path is independently green.
