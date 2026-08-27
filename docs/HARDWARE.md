# Hardware and USB facts

## USB identity

```text
VID:PID 27c6:5135
Manufacturer: Shenzhen Goodix Technology Co.,Ltd.
Product: Goodix Fingerprint Device
USB 2.00 Full Speed (12 Mbps)
```

Observed USB layout:

- Interface 0: communications / interrupt notification
  - endpoint `0x82` Interrupt IN
- Interface 1: data
  - endpoint `0x01` Bulk OUT
  - endpoint `0x81` Bulk IN

The device is self-powered and advertises remote wakeup.

## Firmware and silicon

```text
Firmware: GF_HC460SEC_APP_12508
Chip ID:  a2042500 on raw Linux probe / Windows reports 0x2504
Profile:  ChicagoHS (driver functions use ChicagoHU naming)
Sensor type: 12
Columns: 80
Rows: 64
```

## Useful read-only values

When Windows runtime configuration was present:

```text
0x0082: 8028
0x0220: f80b  # little-endian 0x0bf8
```

After USB re-enumeration/reset:

```text
0x0220: 0000
```

The firmware remains responsive after the config is lost.

## OTP

- OTP length: `64` bytes.
- OTP CRC checks pass in Windows.
- Full OTP is intentionally omitted from this public repository.
- Device-derived calibration values used by Windows are documented in `WINDOWS_DRIVER.md`.
