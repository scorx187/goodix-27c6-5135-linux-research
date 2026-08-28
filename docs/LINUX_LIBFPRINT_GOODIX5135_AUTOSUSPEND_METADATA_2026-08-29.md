# Goodix 27c6:5135 — libfprint autosuspend metadata checkpoint

Date: 2026-08-29

## Local libfprint state

Branch: `goodix-27c6-5135-chicagohu`

Local commit:

`8c3dc3c38400576146850c454c20b6ca58bf374a`

Commit message:

`goodix5135: sync autosuspend hardware database`

## Result

`data/autosuspend.hwdb` is now synchronized with the current libfprint HWDB generator for the new Goodix5135 USB driver.

Exact generated stanza:

```text
# Supported by libfprint driver goodix5135
usb:v27C6p5135*
 ID_AUTOSUSPEND=1
 ID_PERSIST=0
```

Verified before commit:

- `data/autosuspend.hwdb` was the only changed repository file.
- The tracked file exactly matched output from the validated local `fprint-list-udev-hwdb` generator.
- HEAD-to-working-tree drift was exactly one Goodix5135 stanza.
- The entry occurred exactly once.
- `git diff --check` and staged patch check passed.
- No `uaccess` rule was added.
- No system HWDB or udev rule was installed or modified.
- No udev reload/trigger was performed.
- No ACL was changed.
- No USB device was opened during this metadata commit step.
- No Goodix protocol command was sent.
- No push of the local libfprint branch was performed.

## USB access model confirmed before this checkpoint

The development harness required a temporary per-node ACL only because it runs directly as the normal desktop user. The production path is different: `fprintd` runs as the system service (root on the tested Ubuntu system), with explicit systemd device access for USB fingerprint hardware. Therefore no per-user `uaccess` rule is required for the intended fprintd path.

## Hardware milestone already passed

A previous guarded harness successfully performed device discovery followed by libfprint open/close only:

- device discovered as `goodix5135`
- USB device opened
- interface 0 claimed and released
- original temporary ACL restored exactly
- no Goodix protocol command
- no Bulk OUT
- no TLS
- no runtime configuration upload
- no biometric operation

## Next safety gate

Before the first real USB protocol transaction, select the least-impact transaction from already-proven Goodix protocol evidence. Do not invent arbitrary payloads. Do not jump directly to TLS, command `0x90`, activation, FDT, image command `0x20`, firmware/PSK changes, persistent writes, or biometric capture.
