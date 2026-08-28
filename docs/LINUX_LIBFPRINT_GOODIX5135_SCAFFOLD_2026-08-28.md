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

The parser exposes borrowed views for metadata, packed RAW12, and stored CRC without copying or logging biometric bytes. Permanent Meson test `goodix5135-proto` passes seven synthetic cases: valid frame, wrong command, wrong declared length, wrong trailer, truncated frame, oversized frame, and null arguments.

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

### 6. Host-only I/O lifecycle accounting

Local libfprint commit:

`68b36f6b70d9a1cd8223740877a1f9102738a7ff`

Commit subject:

`goodix5135: add I/O lifecycle accounting`

Added a pure host-side generation/pending accounting model for future asynchronous USB operations. The model deliberately contains no `FpiUsbTransfer` submission and no TLS code.

The lifecycle contract is:

```text
start generation
      ↓
begin I/O -> pending++
      ↓
stop/deactivate request
      ↓
reject new I/O
      ↓
outstanding callbacks drain as stale
      ↓
pending-- until zero
      ↓
stop may complete safely
```

Permanent `goodix5135-io` tests cover inactive rejection, normal completion, stale completion after stop, rejection of new I/O after stop, restart only after drain, double-completion rejection, multi-pending drain, and immediate stop with no pending work.

### 7. Wire I/O lifecycle into `FpImageDevice`

Local libfprint commit:

`94ec6c891c8eb684571dff671ab21a5abef9c59f`

Commit subject:

`goodix5135: wire I/O lifecycle into image device`

The driver shell now starts one logical I/O generation during activation, marks deactivation as pending, stops accepting logical I/O immediately, and only calls `fpi_image_device_deactivate_complete()` when the lifecycle model reports all pending work drained. The integration still creates no `FpiUsbTransfer`, sends no Goodix command, and creates no TLS session.

Targeted build passed and all four existing Goodix host suites remained green.

### 8. Host-only transport request bookkeeping

Local libfprint commit:

`532c8bbf845908100f5961f92acf22a8bc3dc312`

Commit subject:

`goodix5135: add transport request bookkeeping`

Added a host-only request model layered on top of lifecycle accounting. Each future request records:

- lifecycle generation token,
- request kind (`BULK_IN` / `BULK_OUT`),
- fixed Goodix endpoint,
- length,
- timeout,
- in-flight state,
- cancellation-request state.

The request model validates endpoint direction, rejects zero lengths/timeouts, rejects double begin, keeps cancellation separate from callback drain, and marks completion stale when its lifecycle generation has already stopped.

Permanent `goodix5135-request` tests pass 10/10 synthetic cases. At this checkpoint all five Goodix host suites pass 5/5:

- `goodix5135-image`
- `goodix5135-proto`
- `goodix5135-image-response`
- `goodix5135-io`
- `goodix5135-request`

No `FpiUsbTransfer` has yet been created or submitted by the Goodix5135 driver.

## Full default-driver regression checkpoint

At local libfprint commit `1c7441fb6c69c092f26c35a055bcb97d4d9106d4`, a fresh `-Ddrivers=default` build completed `151/151` targets successfully. Generated driver registration and supported-device metadata both included `27c6:5135`. The Goodix host suites passed 3/3 from the default build. The unit-test suite reported 5 passed, 0 failed, and one expected skip for `fpi-assembling` because optional Cairo support is unavailable. No system installation or device I/O occurred.

## Current proven foundation

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

plus transport control plane:

FpImageDevice lifecycle
      ↓
logical I/O generation / pending accounting
      ↓
request kind / endpoint / length / timeout / cancellation bookkeeping
      ↓
stop -> reject new work -> drain stale callbacks -> safe deactivation completion
```

## Safety state

The current branch still does **not** implement Goodix device-protocol USB submission, TLS, FDT, activation commands, runtime configuration upload, image capture from hardware, persistent register writes, firmware operations, PSK provisioning, or biometric logging.

The scaffold already claims/releases USB interface 0 on open/close, as it has since the initial build-only driver registration, but no Goodix protocol transfer is submitted.

No system-installed libfprint/fprintd was replaced or installed from this branch during these checkpoints.

No private capture, fingerprint image/raw/template, factory secret, PSK material, full OTP, unit-specific runtime configuration, proprietary Goodix binary, or Windows biometric database material is present in the tests.

## Next implementation step

Introduce a narrow `FpiUsbTransfer` preparation wrapper behind the proven request model. The first step should only construct/fill a bulk transfer from a validated `Goodix5135Request`; it must not submit the transfer, must not be wired into activation, and must not send any Goodix command. Build-test that wrapper first. Only after the ownership/buffer mapping is explicit and compile-tested should asynchronous submission/callback/cancellation wiring be introduced.