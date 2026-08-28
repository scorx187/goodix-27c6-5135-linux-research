# Goodix 27c6:5135 — full default-driver async regression — 2026-08-29

Target local libfprint branch: `goodix-27c6-5135-chicagohu`

Validated local HEAD:

`4ef9e6b8206346323e8adcb5aadf1861bc087021`

Commit subject:

`goodix5135: bridge async callbacks to deactivate drain`

## Regression result

A completely fresh default-driver Meson build was configured at:

`/tmp/libfprint-goodix5135-default-async-sam`

Configuration used the normal `default` driver set with documentation, introspection, udev rules, and udev hwdb generation disabled for this isolated development build.

Full default-driver build result:

`168/168` Ninja steps completed successfully.

Generated registry contained `fpi_device_goodix5135_get_type()` and generated metadata contained `27c6:5135`.

## Goodix host tests

All six Goodix host suites passed:

- `goodix5135-image`
- `goodix5135-proto`
- `goodix5135-image-response`
- `goodix5135-io`
- `goodix5135-request`
- `goodix5135-async-dispatch`

Result: `6/6` passed.

Direct request tests passed `16/16`.

Direct async-dispatch tests passed `5/5`, including:

- current completion then drain,
- replacement request queued before drain inspection,
- cancellation callback racing before lifecycle stop,
- cancellation after lifecycle stop,
- stale completion after stop.

## Full unit-test suite

Default build unit-test suite result:

- 8 passed
- 0 failed
- 1 skipped

The only skip was `fpi-assembling`, with exit status 77, because optional Cairo support is unavailable on this host. This is the same known optional-dependency skip and is not a Goodix regression.

## Async safety state

At this checkpoint the Goodix5135 source contains exactly one asynchronous USB submit primitive in `goodix5135-async.c`.

`goodix5135_async_submit()` still has zero runtime callers.

Therefore the build and regression did not execute any Goodix USB protocol transfer.

The source still contains:

- no synchronous Goodix USB submit path,
- no Goodix TLS implementation,
- no runtime configuration command `0x90`,
- no FDT activation sequence,
- no image command `0x20` hardware request,
- no firmware operation,
- no PSK reprovisioning,
- no biometric logging.

The source tree remained clean after the regression and no system libfprint/fprintd installation was changed.

## Hardware-readiness conclusion

The host-only and full-default regression gates for the current async lifecycle foundation are green.

The implementation now has:

```text
FpImageDevice lifecycle
      ↓
generation / pending accounting
      ↓
request bookkeeping
      ↓
race-safe completion classification
      ↓
bulk transfer preparation
      ↓
isolated async submit primitive
      ↓
higher callback
      ↓
post-callback deactivate drain inspection
```

All defined software safety gates before first hardware I/O passed with no known unsafe behavior.

## Next step

Before enabling any Goodix protocol command, inspect and prepare the least invasive real-device exercise using the uninstalled development build. Prefer a discovery/open/claim/release-only validation first if the current scaffold and available development utilities allow it. Do not begin with TLS, volatile command `0x90`, FDT, image command `0x20`, firmware operations, PSK changes, or biometric capture.
