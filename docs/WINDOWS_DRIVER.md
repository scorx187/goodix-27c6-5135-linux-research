# Windows driver observations

## Package

```text
Device:      Goodix Fingerprint USB Device
Hardware ID: USB\VID_27C6&PID_5135
Provider:    Goodix FP
Version:     1.1.125.12
Date:        2021-05-25
Original INF: gfusb.inf
Published INF: oem173.inf
```

DriverStore package files observed:

```text
AlgoChicago.dll
AlgoChicagoT.dll
AlgoMilan.dll
EngineAdapter.dll
gfusb.cat
gfusb.dll
gfusb.inf
GoodixEventLog.dll
SessionService.exe
```

The same INF explicitly supports both `PID_5125` and `PID_5135`. This does **not** mean the sensor subtypes or firmware are interchangeable.

## Runtime profile selection

A cold initialization log proves:

```text
Get Chip ID: 0x2504
... to init device by chipid 0x2504
!!!! to Open ChicagoHS, sensor type: 12
sensor info ready, chipid:0x2504, sensorType:12, col:80, row:64
```

`gfusb.dll` contains `ChicagoHU*`, `ChicagoT*`, `MilanG*`, and `MilanL*` implementations. For this device, the active path is the ChicagoHS/ChicagoHU path.

## OTP parsing and config modification

Windows validates OTP and extracts:

```text
dac      0x0bf8
dac1     0x00c0
dac2     0x00bf
dac3     0x00bf
tcode    288
fdt_delta 29
```

Then `modify_sensor_config` reports:

```text
reg 0x0220: 0x0808 -> 0x0bf8
reg 0x0236: 0x0080 -> 0x00c0
reg 0x0238: 0x0080 -> 0x00bf
reg 0x023a: 0x0080 -> 0x00bf
reg 0x005c: 0x0180 -> 0x0120
reg 0x0082: 0x1580 -> 0x1d80
```

## Exact config-download sequence observed

The relevant cold-init sequence is:

```text
gf_download_config enter
reset sensor
command 0xa2 reset
ChicagoHUSetMode -> idle
command 0x70 idle
multiple command 0x80 sensor-register writes
ChicagoHUsetDac -> wrote dac 0xbf8 / 0xc0 / 0xbf / 0xbf
command 0x90 upload MCU config, length 0xe0
ACK for 0x90
0x90 response indicates success
gf_download_config exit, ret 1
OTP data valid, download config 1
have config 1
```

The decisive line is:

```text
cmd0-cmd1-Len-ackt-ec:0x9-0-0xe0-1000-0
```

Therefore the Windows MCU configuration payload is exactly **224 bytes**.

## Event logging

The INF enables the `Goodix-FingerprintProvider/Debug` event channel. These EVTX logs are extremely useful because the driver logs function names, command IDs, sensor subtype selection, mode changes, and calibration values.

Do not commit raw EVTX files without reviewing them for host/user identifiers and sensitive biometric-related data.
