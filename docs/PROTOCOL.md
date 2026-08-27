# Protocol notes

These notes apply to the observed `27c6:5135` device and the shared Goodix protocol implementation used during research.

## Known commands

```text
0x00 NOP
0x20 MCU_GET_IMAGE
0x32 MCU_SWITCH_TO_FDT_DOWN
0x34 MCU_SWITCH_TO_FDT_UP
0x36 MCU_SWITCH_TO_FDT_MODE
0x50 NAV
0x60 MCU_SWITCH_TO_SLEEP_MODE
0x70 MCU_SWITCH_TO_IDLE_MODE
0x80 WRITE_SENSOR_REGISTER
0x82 READ_SENSOR_REGISTER
0x90 UPLOAD_CONFIG_MCU
0x92 SWITCH_TO_SLEEP_MODE
0x94 SET_POWERDOWN_SCAN_FREQUENCY
0x96 ENABLE_CHIP
0xa2 RESET
0xa4 MCU_ERASE_APP                 # DO NOT USE
0xa6 READ_OTP
0xa8 FIRMWARE_VERSION
0xae QUERY_MCU_STATE
0xb0 ACK
0xd0 REQUEST_TLS_CONNECTION
0xd4 TLS_SUCCESSFULLY_ESTABLISHED
0xe0 PRESET_PSK_WRITE              # DO NOT USE
0xe4 PRESET_PSK_READ
0xf0 WRITE_FIRMWARE                # DO NOT USE
0xf2 READ_FIRMWARE
0xf4 CHECK_FIRMWARE
```

## TLS

Factory TLS was successfully established from Linux using TLS 1.2 PSK:

```text
cipher:   PSK-AES128-GCM-SHA256
identity: Client_identity
```

The PSK was recovered from the user's own Windows Goodix cache via Windows DPAPI. The key is not stored here.

## FDT mode examples from Windows

FDT down:

```text
ChicagoHUSetMode: Mode 3, Type 1, base_type 1
switch to FDT mode 1
data sent: 0x80a680b6809d80ae80a480b1
```

A later trace showed a nearby variant:

```text
data sent: 0x80a580b6809d80ad80a480b1
```

FDT manual/base acquisition:

```text
ChicagoHUSetMode: Mode 3, Type 3, base_type 1
switch to FDT mode 3
data sent: 0xa6a6b6b69d9daeaea4a4b1b1
```

## Image capture

Windows image mode:

```text
ChicagoHUSetMode: Mode 2
setmode: Image
command 0x20
ACK cfg flag 0x1
TLS-protected bulk response
```

Observed dimensions and packing:

```text
80 x 64 = 5120 pixels
12-bit packed image = 7680 bytes
16-bit regrouped samples = 10240 bytes
```

## MCU config upload

Confirmed Windows command:

```text
command: 0x90
payload: 0xe0 bytes (224)
```

Do not use a 256-byte blind upload. Earlier 256-byte binary-template scans accidentally included 32 bytes of adjacent DLL data.
