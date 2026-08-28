# Goodix 27c6:5135 — USB interface mapping and corrected open/close proof

Date: 2026-08-29

## Result

Read-only descriptor inspection showed the device exposes two USB interfaces:

```text
interface 0
  class 0x02 Communications
  endpoint 0x82 Interrupt IN

interface 1
  class 0x0a CDC Data
  endpoint 0x01 Bulk OUT
  endpoint 0x81 Bulk IN
```

The in-progress libfprint driver had `GOODIX5135_USB_INTERFACE` set to `0`, while its transport endpoint constants are `0x01` and `0x81`. Therefore the driver was claiming the wrong interface for its planned Goodix Bulk transport.

The local driver constant was changed from interface `0` to interface `1` as a one-line working-tree patch. A fresh host-only build completed successfully. All six Goodix5135 host suites passed:

```text
goodix5135-image
goodix5135-proto
goodix5135-image-response
goodix5135-io
goodix5135-request
goodix5135-async-dispatch
```

The broader Meson run had one unrelated AppStream metadata validation failure caused by an unreachable upstream URL. Goodix tests and core unit tests used for this change passed.

## Live hardware open/close proof

Using the corrected local build and a temporary per-user ACL on the current USB device node, the existing minimal harness performed only:

```text
open
claim interface 1
close
release interface 1
```

Observed result:

```text
OPEN_OK
CLOSE_OK
OPEN_CLOSE_TEST_PASSED
```

Post-run checks confirmed interface 1 was released and unbound, interface 0 remained unbound, and the original USB ACL was restored exactly.

No activate path ran. No Bulk IN or Bulk OUT transfer was submitted. No Goodix protocol command was sent. No TLS traffic was created. No firmware, PSK, biometric, or persistent device state was modified.

## Current implementation state

The local libfprint working tree still contains only the one-line interface correction and has not yet committed it at the time of this checkpoint.

This establishes that interface 1 is the physical Goodix Bulk transport interface and that the driver can claim/release it cleanly before any protocol traffic is introduced.
