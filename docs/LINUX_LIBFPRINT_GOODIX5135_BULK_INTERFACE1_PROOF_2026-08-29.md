# Linux/libfprint Goodix 27c6:5135 bulk interface proof — 2026-08-29

Target USB fingerprint sensor: `27c6:5135`.

## USB descriptor mapping

Read-only sysfs and `lsusb -v` inspection proved the device exposes two interfaces:

```text
interface 0
  class      Communications
  endpoint   0x82 Interrupt IN

interface 1
  class      CDC Data
  endpoint   0x01 Bulk OUT
  endpoint   0x81 Bulk IN
```

Therefore the Goodix bulk transport endpoint pair belongs to USB interface 1, not interface 0.

## Driver correction

The local libfprint driver previously defined:

```text
GOODIX5135_USB_INTERFACE = 0
```

This was corrected to:

```text
GOODIX5135_USB_INTERFACE = 1
```

Local libfprint commit:

```text
05f011e3fefa3a72c2b37cfbd22db3230c48689b
goodix5135: claim bulk transport interface
```

The source change is one constant only.

## Host regression

A fresh host-only build with the corrected interface succeeded. The six Goodix-specific host suites passed:

```text
goodix5135-image          PASS
goodix5135-proto          PASS
goodix5135-image-response PASS
goodix5135-io             PASS
goodix5135-request        PASS
goodix5135-async-dispatch PASS
```

A separate AppStream metadata validation failure was caused by an external GitLab URL reachability warning and was unrelated to the Goodix interface change.

## Live hardware claim/release proof

Using a temporary per-node ACL for the normal development user, the open/close-only harness was linked against the corrected local libfprint build.

Result:

```text
OPEN_OK
CLOSE_OK
OPEN_CLOSE_TEST_PASSED
```

Post-run checks proved:

- interface 1 was released and remained unbound;
- interface 0 remained unbound;
- the original USB-node ACL was restored exactly;
- no activate operation was requested;
- no Bulk IN transfer was submitted;
- no Bulk OUT transfer was submitted;
- zero Goodix protocol commands were sent;
- no TLS traffic was created.

This supersedes the earlier interface-0 open/close milestone: that earlier test proved basic USB claim/release capability, but interface 0 is not the Goodix bulk transport interface.

## Safety state

All defined safety gates passed with no known unsafe behavior. No firmware write/erase, PSK provisioning, persistent register write, runtime config upload, biometric capture, or system libfprint installation occurred in this milestone.
