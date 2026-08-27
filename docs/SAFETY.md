# Safety and privacy rules

This work is intentionally conservative because Goodix provisioning scripts can alter persistent firmware/key state and can break Windows Hello.

## Never do these without a device-specific proof and explicit decision

- Erase MCU firmware.
- Flash firmware intended for another Goodix model/profile.
- Run `run_5117.py` against `27c6:5135`.
- Write or replace the factory PSK.
- Upload an MCU config merely because it resembles a 5125/5117 config.
- Perform arbitrary persistent register writes.
- Remove/re-enroll Windows fingerprints as a troubleshooting shortcut.

## Secrets and biometric data

Never commit:

```text
goodix-psk.hex
goodix-psk.bin
Goodix_Cache.bin
goodix.dat
goodix_calib.dat
*.pgm
*.raw
fingerprint images
templates
Windows biometric database material
```

The factory PSK must remain local and permission-restricted.

## Allowed research style

Preferred order:

1. Offline analysis of public/source material.
2. Offline analysis of the user's own Windows driver/logs.
3. Read-only USB commands.
4. Volatile runtime commands already proven by Windows traces.
5. Only then, device-specific volatile config upload after byte-for-byte parity is established.

Preserving Windows behavior is a project requirement, not an optional goal.
