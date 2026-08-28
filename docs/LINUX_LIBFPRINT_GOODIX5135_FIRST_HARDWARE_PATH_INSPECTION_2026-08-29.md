# Goodix 27c6:5135 first hardware path inspection — 2026-08-29

Local libfprint branch: `goodix-27c6-5135-chicagohu`

Local HEAD:

`4ef9e6b8206346323e8adcb5aadf1861bc087021`

## Result

The safest first hardware interaction was inspected without claiming the device or submitting USB transfers.

The current Goodix5135 image-device open/close path is limited to:

```text
open
  -> obtain GUsbDevice
  -> claim USB interface 0
  -> open_complete

close
  -> release USB interface 0
  -> close_complete
```

`goodix5135.c` contains no call to `goodix5135_async_submit()`, `goodix5135_transport_prepare_transfer()`, or `fpi_usb_transfer_submit()`.

System inspection showed:

- device present as `27c6:5135` at `/sys/bus/usb/devices/3-2`,
- device-level driver `usb`,
- interface `3-2:1.0` has no bound kernel driver,
- interface `3-2:1.1` has no bound kernel driver,
- `fprintd.service` is inactive and statically enabled,
- async USB submit wrapper caller count remains zero.

A fresh full default-driver regression build at this same HEAD had already passed before this inspection.

## Safety state

This inspection did not claim any USB interface, submit any USB transfer, send any Goodix protocol command, create a TLS session, capture biometric data, alter firmware, change PSK provisioning, or install/replace the system libfprint/fprintd.

## Next step

The first real hardware test should be an isolated local-build harness that performs only:

```text
discover 27c6:5135
  -> fp_device_open()
     -> Goodix5135 claim interface 0 only
  -> fp_device_close()
     -> release interface 0
```

The harness must not call capture/enroll/verify/identify or any Goodix protocol submit path, and it should use the local build without `ninja install`.
