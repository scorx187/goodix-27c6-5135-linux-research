/*
 * Goodix 27c6:5135 ChicagoHU fingerprint reader
 *
 * Initial Linux/libfprint scaffold.
 *
 * This file contains only public device geometry/transport constants.
 * No firmware, factory secret, biometric payload, calibration payload,
 * or unit-specific configuration is embedded here.
 */

#pragma once

#define GOODIX5135_USB_VID          0x27c6
#define GOODIX5135_USB_PID          0x5135

#define GOODIX5135_USB_INTERFACE    0

#define GOODIX5135_EP_IN            0x81
#define GOODIX5135_EP_OUT           0x01

#define GOODIX5135_IMAGE_WIDTH      80
#define GOODIX5135_IMAGE_HEIGHT     64
#define GOODIX5135_IMAGE_PIXELS     (GOODIX5135_IMAGE_WIDTH * GOODIX5135_IMAGE_HEIGHT)
