# `gfusb.dll` config-template analysis

Offline static analysis of the Windows `gfusb.dll` found many repeated template-like byte arrays. No proprietary binary is included in this repository.

## Repeated prefixes

Three recurring templates were observed:

```text
CFG30 starts: 30 11 64 75 ...
CFG70 starts: 70 11 74 85 ...
CFG90 starts: 90 11 74 85 ...
```

The analyzed DLL contained 40 `.rdata` occurrences of each template family after excluding one `.data` copy.

## Default register tuples

CFG70 and CFG90 both contain the defaults later mentioned by Windows `modify_sensor_config`:

```text
+0x71: 0x005c = 0x0180
+0x75: 0x0220 = 0x0808
+0x79: 0x0236 = 0x0080
+0x7d: 0x0238 = 0x0080
+0x81: 0x023a = 0x0080
+0xad: 0x0082 = 0x1580
```

This proves the templates are relevant to the same sensor-config representation, but does **not** by itself identify which template is selected for `0x2504 / ChicagoHS`.

## `.data` comparison

One mutable-looking CFG70 copy was byte-identical to its `.rdata` template across the earlier 256-byte analysis window.

The CFG90 `.data` copy differed from its `.rdata` copy only at:

```text
+0xe0..+0xe3
+0xf2..+0xff
```

Windows later proved that the actual uploaded config length is exactly `0xe0` bytes.

Therefore those CFG90 differences are **outside the MCU config payload** and were merely adjacent data included by the earlier incorrect 256-byte window.

This is an important correction: the CFG90 `.data` difference is not evidence of OTP calibration.

## Xrefs

Direct RIP-relative/immediate xrefs from x64 `.text` into the repeated template bodies were not found, even when scanning all copies.

Most likely explanations:

- an indirect descriptor/pointer table,
- runtime copying from a larger structure,
- selection through an object/table whose pointer, rather than the config body, is referenced by code.

## Resolution

This historical uncertainty is now closed for the tested `0x2504 / ChicagoHS / sensor type 12` unit. Structural extraction found one unique 224-byte CFG70 family, and a Linux runtime build from live OTP matched a private Windows runtime reference byte-for-byte before a controlled command `0x90` upload was allowed.

See `CFG70_RUNTIME_PROOF_2026-08-27.md` for the exact Windows runtime reconstruction evidence and `LINUX_CFG70_UPLOAD_PROOF_2026-08-27.md` for the Linux live-OTP build/upload proof. The full per-device runtime bytes and unit-specific hash remain private.
