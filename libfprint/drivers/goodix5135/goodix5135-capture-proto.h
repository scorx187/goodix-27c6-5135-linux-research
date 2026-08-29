#pragma once

#include <glib.h>

G_BEGIN_DECLS

#define GOODIX5135_FDT_MODE_COMMAND            0x36U
#define GOODIX5135_FDT_DOWN_COMMAND            0x32U
#define GOODIX5135_FDT_UP_COMMAND              0x34U

#define GOODIX5135_CAPTURE_USB_LENGTH          64U

#define GOODIX5135_FDT_ZONE_COUNT              6U
#define GOODIX5135_FDT_REGISTER_BYTES          12U
#define GOODIX5135_FDT_MANUAL_SEED_BYTES       12U

#define GOODIX5135_FDT_RESPONSE_PAYLOAD_LENGTH 16U

typedef struct
{
  guint16 irq;
  guint16 touch_flag;

  guint16 zones[GOODIX5135_FDT_ZONE_COUNT];
} Goodix5135FdtResponse;

gboolean
goodix5135_capture_build_command (
  guint8         command,
  const guint8  *payload,
  gsize          payload_length,
  guint8        *packet,
  gsize          packet_size,
  gsize         *logical_length);

gboolean
goodix5135_capture_parse_ack (
  guint8         expected_command,
  const guint8  *data,
  gsize          data_length);

gboolean
goodix5135_capture_parse_response (
  guint8          expected_command,
  const guint8   *data,
  gsize           data_length,
  const guint8  **payload,
  gsize          *payload_length);

gboolean
goodix5135_capture_build_fdt_manual (
  const guint8 *seed,
  gsize         seed_length,
  guint8       *packet,
  gsize         packet_size,
  gsize        *logical_length);

gboolean
goodix5135_capture_parse_fdt_response (
  guint8                    expected_command,
  const guint8             *data,
  gsize                     data_length,
  Goodix5135FdtResponse    *response);

gboolean
goodix5135_capture_derive_fdt_down_registers (
  const Goodix5135FdtResponse *response,
  guint8                       registers_out[GOODIX5135_FDT_REGISTER_BYTES]);

gboolean
goodix5135_capture_build_fdt_down (
  const guint8 *registers,
  gsize         registers_length,
  guint32       timestamp,
  guint8       *packet,
  gsize         packet_size,
  gsize        *logical_length);

gboolean
goodix5135_capture_build_fdt_up (
  const guint8 *registers,
  gsize         registers_length,
  guint8       *packet,
  gsize         packet_size,
  gsize        *logical_length);

gboolean
goodix5135_capture_build_image_request (
  guint8 *packet,
  gsize   packet_size,
  gsize  *logical_length);

G_END_DECLS
