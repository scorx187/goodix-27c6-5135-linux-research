# Linux libfprint Goodix 27c6:5135 completion-race checkpoint — 2026-08-29

Target: Goodix USB fingerprint sensor `27c6:5135`, ChicagoHU/type `0x0c`.

## Local libfprint checkpoint

Development branch: `goodix-27c6-5135-chicagohu`

Local commit:

`49d4eda12947ff717170fd98061c354a8d7b9c08`

Commit subject:

`goodix5135: classify request completion races`

This checkpoint extends the host-side request model with an explicit completion classifier before any USB submission is enabled.

## Cancellation sequencing established from libfprint

Read-only inspection of the libfprint core established the relevant ordering:

1. an external task cancellation immediately cancels libfprint's internal `current_cancellable`;
2. `fp_device_cancelled_cb()` schedules the device-class cancel vfunc through an idle source rather than calling it synchronously;
3. for image devices, `fp_image_device_cancel_action()` later calls `fpi_image_device_deactivate(..., TRUE)`;
4. an asynchronous USB transfer using `fpi_device_get_cancellable()` may therefore complete with `G_IO_ERROR_CANCELLED` before the driver's image-device `deactivate` vfunc has run, or after it has run;
5. `fpi_usb_transfer_submit()` still invokes the transfer callback on cancellation, including cancellation-before-start.

Therefore driver callbacks must not use the driver's `deactivating` flag or lifecycle `running` state alone to decide whether a completion is allowed to advance protocol state.

## Completion classification

The request model now exposes four completion classes:

```text
INVALID
CURRENT
CANCELLED
STALE
```

The rule is deliberately cancellation-first:

```text
action cancellable already cancelled
            OR
request-level cancellation requested
             ↓
          CANCELLED

otherwise, lifecycle generation stopped
             ↓
            STALE

otherwise
             ↓
          CURRENT
```

`CANCELLED` and `STALE` completions drain outstanding request accounting but must never advance a future Goodix protocol state machine.

This specifically protects the race where the USB cancellation callback arrives before libfprint's idle cancel dispatch enters `goodix5135_deactivate()`.

## Tests

The targeted build completed successfully (`123/123`).

All existing Goodix host suites remained green (`5/5`).

The permanent request test suite now passes `16/16`, including explicit cases for:

- normal current completion;
- action cancellation before lifecycle stop;
- action cancellation after lifecycle stop;
- stale completion without cancellation;
- request-level cancellation;
- invalid completion bookkeeping.

Race-specific tests were also run individually and passed.

## Safety state

At this checkpoint:

- no `fpi_usb_transfer_submit()` call exists in the Goodix5135 driver;
- no USB transfer is submitted;
- the prepare-only `FpiUsbTransfer` wrapper remains isolated from runtime activation/state-change paths;
- no Goodix command is sent;
- no TLS session is created;
- no system-installed libfprint/fprintd is replaced;
- no firmware, PSK provisioning, persistent register write, private biometric payload, unit-specific configuration, or proprietary binary is added to the repository.

## Next step

Implement the first isolated asynchronous bulk submit/callback wrapper using libfprint's action cancellable (`fpi_device_get_cancellable()`), with explicit callback-owned request context and the new completion classifier. The wrapper must remain unreferenced by `activate`, `deactivate`, and `change_state` while it is build-tested, so merely compiling the code cannot send a Goodix protocol command.
