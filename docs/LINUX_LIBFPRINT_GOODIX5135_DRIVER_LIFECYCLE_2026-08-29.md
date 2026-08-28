# Goodix5135 libfprint driver lifecycle checkpoint — 2026-08-29

## Local libfprint checkpoint

Branch: `goodix-27c6-5135-chicagohu`

Commit: `94ec6c891c8eb684571dff671ab21a5abef9c59f`

Subject: `goodix5135: wire I/O lifecycle into image device`

## What is now wired into the real driver shell

`FpiDeviceGoodix5135` now carries the tested host-side lifecycle state:

- `active`
- `deactivating`
- `Goodix5135IoLifecycle io`

Activation starts a new lifecycle generation with `goodix5135_io_start()` and reports a libfprint device error if the lifecycle cannot start.

Deactivation immediately stops new logical I/O with `goodix5135_io_stop()`, then completes only when `goodix5135_io_can_finish_stop()` is true. The helper `goodix5135_maybe_finish_deactivate()` is the intended completion point once future asynchronous callbacks drain `io.pending` to zero.

## Validation

Fresh targeted `drivers=goodix5135` build completed successfully: 117/117 Ninja targets.

Permanent Goodix host suites all passed:

- `goodix5135-image`
- `goodix5135-proto`
- `goodix5135-image-response`
- `goodix5135-io`

The direct lifecycle test passed all 8 synthetic host-only cases.

## Safety state

At this checkpoint:

- no `FpiUsbTransfer` is created or submitted by the Goodix5135 driver;
- no Goodix protocol command is sent;
- no TLS session is created;
- no system libfprint/fprintd installation was changed;
- only the existing scaffold USB interface claim/release remains in open/close.

This checkpoint therefore establishes driver-level activation/deactivation accounting before any real asynchronous Goodix transport is introduced.

## Next implementation step

Add a transport request abstraction that binds one future asynchronous operation to:

- the lifecycle generation/token;
- timeout policy;
- cancellation state;
- callback completion accounting.

Keep that layer host-testable before the first real `fpi_usb_transfer_submit()` is introduced.
