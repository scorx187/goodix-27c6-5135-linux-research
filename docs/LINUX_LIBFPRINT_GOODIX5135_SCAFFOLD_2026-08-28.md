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

Files added:

- `libfprint/drivers/goodix5135/goodix5135.c`
- `libfprint/drivers/goodix5135/goodix5135.h`
- `libfprint/drivers/goodix5135/goodix5135-proto.c`
- `libfprint/drivers/goodix5135/goodix5135-proto.h`

Build integration changed:

- `libfprint/meson.build`
- top-level `meson.build`

The generated driver registry contains `fpi_device_goodix5135_get_type()` and generated supported-device metadata contains:

`27c6:5135 | Goodix 27c6:5135 ChicagoHU Fingerprint Sensor`

The scaffold is registered as an `FpImageDevice` with press-sensor semantics and downstream geometry `80x64`.

### 2. Host-only CRC and ChicagoHU layout helpers

Local libfprint commit:

`d73798b21d82509404e402b04ab6f402b601e352`

Commit subject:

`goodix5135: add ChicagoHU image layout helpers`

Added pure host-side helpers for:

- CRC-32/MPEG-2 (`poly 0x04C11DB7`, `init 0xFFFFFFFF`, non-reflected, xorout zero);
- ChicagoHU regroup from packed/source order into the proven `80x64` downstream plane using:

```text
dst[(n % 64) * 80 + (n / 64)] = src[n]
```

A public synthetic CRC vector (`123456789`) produced `0x0376E6E7`, and synthetic regroup boundary tests passed. No device I/O was enabled.

### 3. RAW12 decode and permanent Meson unit tests

Local libfprint commit:

`86c6abf8f6684e4e1015d1fdddfb1a20444912a3`

Commit subject:

`goodix5135: add RAW12 image decoding tests`

Added the proven Goodix 5135 packed RAW12 decode, where every six input bytes produce four 12-bit samples:

```text
p0 = ((b0 & 0x0f) << 8) | b1
p1 = (b3 << 4) | (b0 >> 4)
p2 = ((b5 & 0x0f) << 8) | b2
p3 = (b4 << 4) | (b5 >> 4)
```

Also added Windows-compatible interpretation of the four-byte stored image CRC field:

```text
[a, b, c, d] -> c<<24 | d<<16 | a<<8 | b
```

A permanent Meson unit test `goodix5135-image` now covers:

- CRC-32/MPEG-2;
- stored CRC-field ordering;
- RAW12 `7680 bytes -> 5120 u16 samples`;
- ChicagoHU regroup.

The test passes through `meson test` and direct GLib/TAP execution with four passing cases. Test vectors are synthetic and contain no biometric or unit-specific data.

## Current proven host-side image foundation

```text
CRC-32/MPEG-2
      ↓
stored CRC-field decode
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

Implement a host-only parser for the proven decrypted image response framing before enabling real USB transport. The established image response has:

```text
command            0x20
declared length     7690 (little-endian)
metadata            5 bytes
packed RAW12        7680 bytes
stored image CRC    4 bytes
trailer             0x88
total plaintext     7693 bytes
```

The parser should validate all fixed sizes/bounds and expose views/offsets without logging biometric bytes. Synthetic unit tests should cover valid framing plus wrong command, wrong declared length, truncation/oversize, and wrong trailer. Device I/O must remain disabled until the host parser and lifecycle/cancellation boundaries are independently tested.
