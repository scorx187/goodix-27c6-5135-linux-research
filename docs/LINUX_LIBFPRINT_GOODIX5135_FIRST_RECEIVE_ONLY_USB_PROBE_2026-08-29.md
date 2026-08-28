# Goodix 27c6:5135 — First live receive-only USB probe

Date: 2026-08-29

## Local libfprint checkpoint

Branch: `goodix-27c6-5135-chicagohu`

Commit: `05f011e3fefa3a72c2b37cfbd22db3230c48689b`

The driver now claims USB interface 1, which is the interface that owns the Bulk endpoint pair:

- `0x01` Bulk OUT
- `0x81` Bulk IN

A prior hardware open/close test successfully claimed and released interface 1 without sending any Bulk transfer or Goodix protocol command.

## First live USB transfer

A standalone temporary libusb diagnostic performed exactly one receive-only Bulk transfer:

- direction: IN only
- endpoint: `0x81`
- requested length: 8192 bytes
- timeout: 100 ms
- Bulk OUT bytes: 0
- Goodix application commands: 0
- TLS traffic: 0
- received payload bytes were never printed or inspected
- receive buffer was wiped before free

Result:

- `LIBUSB_ERROR_TIMEOUT` (`-7`)
- transferred bytes: `0`
- interface release result: success (`0`)

This establishes that the first real receive-only Bulk-IN request reached the transport endpoint and timed out without transferring any bytes during this probe.

## Safety state

The original USB ACL was restored exactly after the test. Interface 1 remained unbound afterward. The local libfprint source tree remained clean.

No Bulk OUT transfer, Goodix command, TLS session, configuration upload, FDT operation, image command, firmware operation, PSK operation, enrollment operation, or biometric payload inspection occurred.

All defined safety gates passed with no known unsafe behavior.

## Next gate

Before exercising the libfprint/GUsb async wrapper on hardware, use a temporary diagnostic build that runs the receive-only operation inside a real libfprint action/lifecycle so it uses the driver's own `self->io` and valid action cancellable. Do not call the wrapper from an external lifecycle object or when `current_action == NONE`.
