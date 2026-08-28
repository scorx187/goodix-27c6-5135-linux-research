# Linux/libfprint clean baseline — 2026-08-28

Target device: Goodix USB fingerprint sensor `27c6:5135`, ChicagoHS/ChicagoHU, family/type `0x0c`.

This checkpoint marks the transition from static Windows compatibility research to native Linux implementation.

## Host baseline

- OS: Ubuntu 26.04 LTS
- Kernel: 7.0.0-30-generic
- Architecture: x86_64
- Device visible through USB as `27c6:5135`
- Installed fprintd: 1.94.5-4
- Installed Ubuntu runtime libfprint: 1.95.1+tod1-0ubuntu2
- Development checkout: `/home/sam/libfprint`
- Checkout project version: libfprint 1.94.9
- Baseline checkout SHA observed before implementation work: `dc8b05f0a30e93174e861977cede8427c22f8f76`

## Build toolchain

- GCC 15.2.0
- Meson 1.10.1
- Ninja 1.13.2
- pkg-config 2.5.1
- Python 3.14.4

Development dependencies added for the clean source build include GLib/GIO/GObject, GUsb, GObject Introspection, Pixman, and GUdev. OpenSSL and udev were already discoverable by Meson.

## Clean configuration/build result

A fresh temporary Meson build directory was configured from the unmodified libfprint source checkout:

```text
/tmp/libfprint-goodix-baseline-sam
```

Meson configuration completed successfully with the stock driver set, including `goodixmoc` but no `27c6:5135` implementation.

The clean baseline was then built with Ninja:

```text
141/141 build steps completed
Ninja exit status: 0
```

Therefore the host toolchain and current libfprint source tree have a proven green baseline before adding the ChicagoHU driver.

## Implementation rule

From this point forward:

1. preserve the clean baseline SHA;
2. make Goodix `27c6:5135` work on a dedicated development branch;
3. do not replace the system-installed libfprint/fprintd during early development;
4. build and test from an isolated Meson build directory first;
5. keep all proprietary binaries, PSK/OTP material, biometric images/templates, calibration payloads, and Windows biometric data out of the repository and logs;
6. do not perform firmware erase/flash, PSK reprovisioning, or arbitrary persistent writes.

The next implementation step is to select the correct libfprint image-device/state-machine skeleton for a host-side image sensor, create the dedicated branch, and introduce the minimum driver registration/build scaffolding for `27c6:5135` before implementing transport and image acquisition.
