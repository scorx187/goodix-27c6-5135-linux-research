# Goodix 27c6:5135 async callback drain bridge — 2026-08-29

Local libfprint branch: `goodix-27c6-5135-chicagohu`

Parent local commit before this checkpoint: `d32f65be7ac7e0c7bf3a56e088c9e819288e3321` (`goodix5135: add isolated async bulk submit wrapper`).

## What was added

A host-side async completion dispatch layer now enforces this ordering:

```text
request_finish()
  -> lifecycle pending accounting drains
  -> higher protocol callback runs
  -> higher callback may enqueue replacement request
  -> driver drain notification runs
  -> maybe_finish_deactivate() inspects final pending state
```

The driver exposes `goodix5135_driver_async_drained()` as the bridge from the isolated async layer to the existing `FpImageDevice` deactivation drain point.

## Host-only tests

A new permanent `goodix5135-async-dispatch` suite covers five synthetic cases:

1. current completion then drain notification;
2. higher callback requeues a replacement request before drain inspection;
3. cancellation callback racing before lifecycle stop;
4. cancellation completion after lifecycle stop drains the final pending request;
5. stale completion after stop also allows final drain.

Fresh targeted build result:

```text
130/130 build steps
Goodix host suites: 6/6 passed
async-dispatch direct tests: 5/5 passed
request regression: 16/16 passed
```

## Safety state

- `goodix5135_async_submit()` still has zero runtime callers.
- Exactly one `fpi_usb_transfer_submit()` primitive exists, inside the isolated async wrapper only.
- No synchronous USB submit call exists in the Goodix5135 driver.
- No USB transfer was executed by these host tests.
- No Goodix protocol command was sent.
- No TLS implementation/session exists.
- No system-installed libfprint/fprintd was changed.

This checkpoint is intentionally still pre-hardware. The next step is to review and commit the drain bridge, then run a full default-driver regression before introducing any first isolated hardware transaction.