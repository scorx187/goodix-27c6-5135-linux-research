# Linux libfprint Goodix 27c6:5135 async drain checkpoint — 2026-08-29

Target: Goodix USB fingerprint sensor `27c6:5135`, ChicagoHU/type `0x0c`.

## Local libfprint checkpoint

Development branch: `goodix-27c6-5135-chicagohu`

Local commit:

`4ef9e6b8206346323e8adcb5aadf1861bc087021`

Commit subject:

`goodix5135: bridge async callbacks to deactivate drain`

## What this checkpoint adds

The isolated async USB layer now drains request/lifecycle accounting, invokes the higher-level completion callback, and only then notifies the driver that deactivation completion may be re-evaluated.

The ordering is deliberately:

```text
USB callback
    ↓
request_finish() / pending--
    ↓
higher completion callback
    ↓
callback may enqueue replacement request / pending++
    ↓
driver_async_drained()
    ↓
goodix5135_maybe_finish_deactivate()
```

This prevents premature `fpi_image_device_deactivate_complete()` in the window between completion of one request and a replacement request being queued by the same higher-level callback.

## Host-only tests

Fresh targeted `-Ddrivers=goodix5135` build completed successfully.

Goodix host suites: `6/6` passed.

New `goodix5135-async-dispatch` suite: `5/5` passed, covering:

- normal completion then drain notification,
- replacement request requeue before drain inspection,
- cancellation callback racing before lifecycle stop/deactivate,
- cancellation after lifecycle stop,
- stale completion after lifecycle stop.

Existing request suite remained green: `16/16`.

## USB/TLS safety state

The source contains exactly one asynchronous `fpi_usb_transfer_submit()` primitive in the isolated async wrapper, but `goodix5135_async_submit()` still has zero runtime callers.

Therefore the build and tests at this checkpoint executed no Goodix protocol USB transfer.

No synchronous USB submit primitive was introduced.

No TLS implementation exists in the Goodix5135 driver at this checkpoint.

No Goodix command, activation sequence, FDT operation, runtime configuration upload, or image request was sent.

No system libfprint/fprintd installation was changed.

## Next gate

Run a fresh full default-driver regression on this exact local HEAD before enabling any hardware transaction. The regression should build the normal/default driver set, verify the Goodix5135 registration remains present, run the Goodix host suites, run the broader unit-test set, and keep the isolated async submit wrapper without runtime callers.

Only after that full regression is green should the first hardware interaction be designed and tested as an isolated, narrowly scoped transaction. Do not jump directly to TLS, command `0x90`, FDT, or image command `0x20`.
