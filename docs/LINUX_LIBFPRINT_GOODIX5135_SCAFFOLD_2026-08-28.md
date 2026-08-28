# Linux libfprint Goodix 27c6:5135 scaffold — 2026-08-28

Target: Goodix USB fingerprint sensor `27c6:5135`, ChicagoHU/type `0x0c`.

## Local libfprint baseline

- Upstream libfprint source baseline: `v1.94.9`, SHA `dc8b05f0a30e93174e861977cede8427c22f8f76`.
- Development branch: `goodix-27c6-5135-chicagohu`.
- Clean upstream/default build passed before driver changes.
- Targeted `-Ddrivers=goodix5135` build passed.
- Full default-driver regression build completed successfully; subsequent `ninja -n` reported `no work to do`.

## First implementation commit

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

## Proven integration behavior

The generated driver registry contains `fpi_device_goodix5135_get_type()`.

Generated supported-device metadata contains:

`27c6:5135 | Goodix 27c6:5135 ChicagoHU Fingerprint Sensor`

The scaffold is registered as an `FpImageDevice` with press-sensor semantics and the established downstream image geometry `80x64`.

## Safety state of this commit

This commit is deliberately build-only.

It does **not** implement Goodix device-protocol USB transfers, TLS, FDT, activation, runtime configuration upload, image capture, register writes, firmware operations, PSK provisioning, or biometric logging.

No system-installed libfprint/fprintd was replaced or installed from this branch during the checkpoint.

## Next implementation step

Build a host-only Goodix transport/framing layer with unit-testable serialization/parsing and explicit bounds/cancellation semantics before enabling any real device I/O. Only after that layer is reviewed and build-tested should bulk endpoint operations (`OUT 0x01`, `IN 0x81`) be connected to the libfprint lifecycle.
