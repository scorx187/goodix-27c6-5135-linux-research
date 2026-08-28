# Goodix 27c6:5135 full libfprint regression — 2026-08-28

Target local libfprint branch: `goodix-27c6-5135-chicagohu`

Validated local libfprint HEAD:

`1c7441fb6c69c092f26c35a055bcb97d4d9106d4`

Commit subject:

`goodix5135: add validated image response pipeline`

## Full default-driver build

A fresh Meson build was configured with:

```text
-Ddrivers=default
-Ddoc=false
-Dintrospection=false
-Dudev_rules=disabled
-Dudev_hwdb=disabled
```

The generated registry included `fpi_device_goodix5135_get_type()` and generated supported-device metadata contained:

`27c6:5135 | Goodix 27c6:5135 ChicagoHU Fingerprint Sensor`

The complete default-driver build finished successfully at `151/151`. A subsequent `ninja -n` reported `no work to do`.

## Goodix host suites from the default build

All three permanent Goodix host-side suites passed from the full default-driver build:

```text
goodix5135-image           PASS
goodix5135-proto           PASS
goodix5135-image-response  PASS
```

The direct `goodix5135-image-response` GLib/TAP run also passed all seven cases, including valid full decode, packed RAW12 CRC corruption rejection, stored CRC corruption rejection, structural/frame rejection, output-size rejection, and null-argument rejection.

## General unit suite

The `unit-tests` suite result was:

```text
Ok:      5
Fail:    0
Skipped: 1
```

The single skipped test was `fpi-assembling` with Meson exit status 77 because the optional cairo dependency is absent. This is the previously known optional-dependency skip and is not a Goodix regression.

## Safety state

At this checkpoint the Goodix5135 implementation remains host-only beyond interface claim/release in the original build scaffold. The new image/protocol code contains no Goodix bulk/control/interrupt transfer submission and no TLS/OpenSSL session implementation.

No system-installed libfprint/fprintd was replaced. No Goodix protocol command was sent and no TLS session was created during the regression gate.

The local Git worktree remained clean at the validated HEAD.

## Proven host-side path now regression-gated

```text
7693-byte decrypted command-0x20 response
      ↓
strict frame validation
      ↓
stored CRC decode + CRC-32/MPEG-2 validation
      ↓
Goodix RAW12 decode: 7680 bytes -> 5120 u16
      ↓
ChicagoHU regroup
      ↓
80 x 64 u16 downstream plane
```

This full path is covered only with synthetic/public unit-test vectors in the repository. No private biometric captures or unit-specific material are committed.

## Next implementation step

Move to an isolated asynchronous USB transport/lifecycle layer. Before sending any Goodix protocol command, model and test lifecycle behavior around transfer ownership, cancellation, timeout, deactivate/close ordering, and stale-callback protection. Use existing libfprint image-device drivers only as lifecycle/API references. Keep command `0x20`, volatile runtime command `0x90`, TLS setup, FDT, activation, persistent register writes, firmware operations, and PSK provisioning disabled until the transport state machine and cancellation/error paths are independently reviewable and build-tested.
