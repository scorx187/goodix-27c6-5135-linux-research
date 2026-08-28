# Linux libfprint Goodix 27c6:5135 async checkpoint — 2026-08-29

Target: Goodix USB fingerprint sensor `27c6:5135`, ChicagoHU/type `0x0c`.

This checkpoint records the transition from prepare-only USB transfer construction to an isolated asynchronous submit primitive that is intentionally not wired into the image-device runtime.

## Local libfprint branch

Development branch:

`goodix-27c6-5135-chicagohu`

### 10. Race-safe request completion classification

Local libfprint commit:

`49d4eda12947ff717170fd98061c354a8d7b9c08`

Commit subject:

`goodix5135: classify request completion races`

Added explicit request completion states:

- `CURRENT`
- `CANCELLED`
- `STALE`
- `INVALID`

The policy deliberately makes cancellation win over generation state because libfprint cancels the action cancellable immediately while image-device cancellation is dispatched through an idle callback. Therefore a USB cancellation callback may arrive before the driver's `deactivate` vfunc runs.

Permanent request tests cover both cancellation-before-stop and cancellation-after-stop races, stale completion, current completion, explicit request cancellation, and invalid completion. Direct request tests pass 16/16 and all five Goodix host suites remain green.

### 11. Isolated asynchronous bulk submit wrapper

Local libfprint commit:

`d32f65be7ac7e0c7bf3a56e088c9e819288e3321`

Commit subject:

`goodix5135: add isolated async bulk submit wrapper`

Added `goodix5135_async_submit()` and a heap-owned operation context containing its own `Goodix5135Request`.

The ownership/cancellation contract is:

```text
heap Goodix5135AsyncOperation
        ↓
request_begin() -> lifecycle pending++
        ↓
prepare FpiUsbTransfer
        ↓
fpi_usb_transfer_submit(
    transfer,
    timeout_ms,
    fpi_device_get_cancellable(device),
    internal_callback,
    operation)
        ↓
callback guaranteed for completion/error/timeout/cancellation
        ↓
request_finish()
        ↓
CURRENT / CANCELLED / STALE / INVALID
        ↓
higher-layer callback while transfer is still borrowed-valid
        ↓
free heap operation context
        ↓
libfprint releases stolen transfer reference after callback returns
```

The implementation treats either the libfprint action cancellable being cancelled or a `G_IO_ERROR_CANCELLED` transfer result as cancellation, preventing protocol advancement through the previously identified race.

The source now contains exactly one asynchronous `fpi_usb_transfer_submit()` call and no synchronous submit call. The submit primitive exists only in `goodix5135-async.c` and has zero callers outside its own API files.

A fresh targeted `-Ddrivers=goodix5135` build completed `124/124`. All five Goodix host suites remained green, and the direct request regression passed 16/16.

## Safety state

At this checkpoint:

- `goodix5135_async_submit()` exists in source but is not called from `activate`, `deactivate`, `change_state`, or any other Goodix5135 runtime path.
- Building and running the host tests does not invoke the async submit primitive.
- No Goodix protocol command was sent to hardware.
- No TLS session was created.
- No system-installed libfprint/fprintd was replaced.
- No firmware, PSK, OTP, biometric, calibration, or persistent device state was modified.

## Next implementation step

Before the first hardware transaction, connect asynchronous callback drain to the image-device deactivation barrier. The wrapper already decrements lifecycle pending through `goodix5135_request_finish()`, but it does not yet notify the `FpImageDevice` layer that the last pending callback may have drained.

The next layer should therefore add an explicit post-callback drain notification that can call the driver's `goodix5135_maybe_finish_deactivate()` only after higher-level completion handling has finished. This should be testable host-side and must not add a runtime caller to `goodix5135_async_submit()` yet.
