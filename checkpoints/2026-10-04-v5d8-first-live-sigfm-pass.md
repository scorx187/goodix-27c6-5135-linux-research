# V5D8 — First real live SIGFM stage passed

Date: 2026-10-04

## Result

The first guarded live Goodix 27c6:5135 SIGFM smoke run succeeded on the real sensor.

Observed public-safe result markers:

- `LIVE_SIGFM_SMOKE=READY_FOR_FINGER`
- `LIVE_SIGFM_FIRST_STAGE=PASS`
- `LIVE_SIGFM_SMOKE=PASS`
- `LOCAL_LIVE_SIGFM_RESULT=PASS`
- ephemeral FDT cleanup completed

This proves the live path reached:

USB -> native TLS -> FDT -> image acquisition/decode -> FpImage -> SIGFM extraction -> accepted enrollment stage.

No fingerprint image, serialized template, exact feature count, exact matcher score, PSK value, OTP, or private calibration bytes were printed or persisted by the smoke harness.

## Runtime permission fix

A local udev rule for USB 27c6:5135 using `TAG+="uaccess"` was required so the logged-in desktop user could open the USB node read/write without running the fingerprint harness as root.

## Expected smoke-only warning

After the accepted first stage, the one-stage smoke harness intentionally cancels enrollment. Because FDT-up cleanup may still be in flight, the smoke run can emit an `FDT-up ACK transport failed` warning during that deliberate cancellation. The accepted stage and smoke result remain successful. The natural full-enrollment harness does not intentionally cancel after stage one and is the next live test.

## Next gate

Run the new ephemeral full SIGFM harness for 30 enrollment stages, keeping the resulting FpPrint only in memory, followed immediately by a same-finger verification. No template persistence until the live matcher behavior is validated.
