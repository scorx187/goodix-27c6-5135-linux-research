# Windows MCU config upload trace

This is a redacted/minimal transcription of the decisive cold-init window from the Goodix Windows debug channel. It intentionally excludes unrelated host information and any secret/biometric material.

## Before upload

Windows has already selected the device profile and parsed OTP:

```text
FDT_Init, sensortype: 12, fdt_delta:29, tcode:288
gf_download_config ... enter
```

## Reset and idle

```text
gfresetMCUAndfingerprint ... reset sensor
UsbSendDataToDevice ... cmd0-cmd1-Len-ackt-ec:0xa-1-0x2-1000-0
get ack for cmd 0xa2
MCU has no config
...
ChicagoHUSetMode ... Mode 7, Type 0, base_type 0
ChicagoHUSetMode ... setmode: idle
UsbSendDataToDevice ... cmd0-cmd1-Len-ackt-ec:0x7-0-0x2-1000-0
get ack for cmd 0x70
MCU has no config
```

## Sensor register/DAC writes

Several command `0x80` writes occur while the MCU still reports no config:

```text
UsbSendDataToDevice ... cmd0-cmd1-Len-ackt-ec:0x8-0-0x5-1000-0
get ack for cmd 0x80
MCU has no config
```

This repeats multiple times, followed by:

```text
ChicagoHUsetDac ... wrote down dac 0xbf8, dac1 0xc0,dac2 0xbf, dac3 0xbf
```

## Decisive upload

```text
UsbSendDataToDevice ... cmd0-cmd1-Len-ackt-ec:0x9-0-0xe0-1000-0
...
get ack for cmd 0x90
MCU has no config
...
pack ... first 5 bytes: 0x90 0x3 0x0 0x1 0x0
recvd data cmd-len: 0x90-3
...
gf_download_config ... exit, ret 1
OTP data valid, download config 1
otp_valid 1, have config 1
```

## Interpretation

The shared Goodix protocol maps command `0x90` to `UPLOAD_CONFIG_MCU`.

The trace therefore proves:

```text
command = 0x90
payload length = 0xe0 = 224 bytes
result = success
state transition = have config 0 -> 1
```

This length is now the canonical target for Linux parity work.
