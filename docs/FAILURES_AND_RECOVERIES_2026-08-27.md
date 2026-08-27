# Failures, wrong hypotheses, and recoveries

This file intentionally preserves negative results so future developers do not repeat them.

## `0x90` config length was initially treated as 256 bytes

Wrong. Windows proved payload length `0xe0 = 224` bytes. Bytes after offset `0xe0` in DLL scans were neighboring data.

## CFG90 looked tempting because the command is `0x90`

Wrong reasoning. Command ID and static template prefix are unrelated. Exact Windows parity proved CFG70 for the tested ChicagoHS path.

## Forcing a "config gone" state with resets

Not useful. Protocol reset, PyUSB reset, sysfs re-enumeration, and `usbreset` experiments did not provide a trustworthy controlled config-loss state. Stop spending time on this unless a specific later test requires it.

## `usbreset`

Unreliable in this setup. The sysfs USB authorization toggle is the more reliable transport-recovery mechanism.

## Direct chip read returned `06000000`

This was not proof of damage. The device needed the activation sequence:

```text
NOP -> 0xd4 -> NOP -> enable_chip -> NOP -> firmware -> reset -> chip read
```

Afterward, three reads returned `a2042500`.

## `enable_chip(True)` timed out

When called in the wrong state it produced no ACK. Sending the transient `0xd4` activation-state command first restored the expected path.

## Pending-frame hypothesis

A short read-only drain immediately after opening the device found no pending USB frame. Therefore the broken query/enable behavior was not explained by an old queued FDT event.

## FDT v2

Transport success but bad calibration interpretation. It treated the private 12-byte FDT manual seed as raw `u16` values and produced invalid thresholds.

## FDT v3

Stopped before sensor I/O because it expected six `0x80 xx` pairs in `goodix.dat`. This assumption was false.

## FDT v4

Stopped on chip guard because the activation sequence was incomplete.

## TLS server `Address already in use`

An old OpenSSL process was still bound to port 4433. Kill only the identified stale process or use a dynamically selected localhost port. New public-safe scripts should prefer a free ephemeral local port.

## Image probe v1

Used expected TLS pack flags `0xb2`; failed with `Invalid message pack` after `mcu_get_image()`.

Code inspection showed `mcu_get_image()` already consumes the ACK. The second frame was actually transport flags `0xb0`.

## Disconnect timeout

`device.disconnect()` may throw `TimeoutError: Device is still connected` after a successful proof. Do not let cleanup noise invalidate preceding confirmed operations.
