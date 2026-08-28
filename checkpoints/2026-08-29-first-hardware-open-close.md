# Goodix 27c6:5135 — first hardware open/close checkpoint

Local libfprint branch: `goodix-27c6-5135-chicagohu`

Validated local HEAD:

`4ef9e6b8206346323e8adcb5aadf1861bc087021`

## Result

First real hardware interaction succeeded using the locally built libfprint driver and a temporary per-device ACL.

Observed sequence:

- libfprint discovered one device as driver `goodix5135`
- device name: `Goodix 27c6:5135 ChicagoHU Fingerprint Sensor`
- `fp_device_open_sync()` succeeded
- USB interface 0 was claimed by the driver open path
- `fp_device_close_sync()` succeeded
- USB interface 0 was released by the driver close path
- temporary ACL for the normal user was restored exactly to the original ACL afterward
- source tree remained clean
- interface 0 remained unbound after the test

## Safety properties

At this checkpoint:

- `goodix5135_async_submit()` still has zero runtime callers
- no Goodix protocol command was sent
- no Goodix Bulk OUT was sent by the driver
- no TLS session was created
- no `0x90` runtime configuration was uploaded
- no FDT operation was run
- no image capture, enroll, verify, or identify operation was requested
- no permanent udev rule was installed
- no system installation changed

The local open/close path is limited to USB discovery plus claim/release of interface 0.

## Permission finding

The installed system libfprint udev rule does not yet contain `27c6:5135`. The USB node was therefore `root:root` mode `0664` without a user ACL. A temporary ACL granting the current user read/write access to that one USB node allowed the open/close test to pass.

This confirms the previous open failure was a Linux device-node permission problem, not a Goodix protocol or interface-claim failure.

## Next gate

Before any Bulk transaction, inspect and use the canonical libfprint udev rule generation path for the new `27c6:5135` ID rather than inventing a custom permanent rule. Keep the async submit wrapper uncalled until an explicit first-protocol-transaction plan is reviewed.
