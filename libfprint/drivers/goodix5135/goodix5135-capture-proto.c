#include "goodix5135-capture-proto.h"

#include "goodix5135-proto.h"

#include <string.h>

#define GOODIX5135_CAPTURE_OUTER_HEADER_LENGTH 4U
#define GOODIX5135_CAPTURE_INNER_HEADER_LENGTH 3U
#define GOODIX5135_CAPTURE_CHECKSUM_LENGTH     1U

#define GOODIX5135_CAPTURE_ACK_OUTER_LENGTH    6U
#define GOODIX5135_CAPTURE_ACK_INNER_LENGTH    3U
#define GOODIX5135_CAPTURE_ACK_LOGICAL_LENGTH 10U

#define GOODIX5135_FDT_MANUAL_PAYLOAD_LENGTH  14U
#define GOODIX5135_FDT_DOWN_PAYLOAD_LENGTH    18U
#define GOODIX5135_FDT_UP_PAYLOAD_LENGTH      14U
#define GOODIX5135_IMAGE_REQUEST_PAYLOAD_LENGTH 2U


static guint8
goodix5135_capture_outer_checksum (
  guint8  flags,
  guint16 length)
{
  guint value;

  value = flags;
  value += length & 0xffU;
  value += (length >> 8) & 0xffU;

  return (guint8) (
    value & 0xffU);
}


static guint8
goodix5135_capture_protocol_checksum (
  const guint8 *data,
  gsize         length)
{
  guint sum = 0;
  gsize index;

  for (index = 0;
       index < length;
       index++)
    sum += data[index];

  return (guint8) (
    (0xaaU - (sum & 0xffU)) &
    0xffU);
}


gboolean
goodix5135_capture_build_command (
  guint8         command,
  const guint8  *payload,
  gsize          payload_length,
  guint8        *packet,
  gsize          packet_size,
  gsize         *logical_length)
{
  guint16 inner_length;
  guint16 outer_length;

  gsize required_length;

  g_return_val_if_fail (
    packet != NULL,
    FALSE);

  g_return_val_if_fail (
    logical_length != NULL,
    FALSE);

  if (payload_length > 0 &&
      payload == NULL)
    return FALSE;

  /*
   * Logical wrapped Goodix message:
   *
   *   outer:
   *     flags
   *     little-endian inner frame length
   *     header checksum
   *
   *   inner:
   *     command
   *     little-endian payload-plus-checksum length
   *     payload
   *     protocol checksum
   */
  if (payload_length >
      G_MAXUINT16 - GOODIX5135_CAPTURE_CHECKSUM_LENGTH)
    return FALSE;

  inner_length =
    (guint16) (
      payload_length +
      GOODIX5135_CAPTURE_CHECKSUM_LENGTH);

  outer_length =
    (guint16) (
      1U +
      2U +
      payload_length +
      GOODIX5135_CAPTURE_CHECKSUM_LENGTH);

  required_length =
    GOODIX5135_CAPTURE_OUTER_HEADER_LENGTH +
    outer_length;

  if (required_length >
      GOODIX5135_CAPTURE_USB_LENGTH)
    return FALSE;

  if (packet_size <
      GOODIX5135_CAPTURE_USB_LENGTH)
    return FALSE;

  memset (
    packet,
    0,
    packet_size);

  packet[0] =
    GOODIX5135_PACK_FLAGS_MESSAGE_PROTOCOL;

  packet[1] =
    outer_length & 0xffU;

  packet[2] =
    (outer_length >> 8) & 0xffU;

  packet[3] =
    goodix5135_capture_outer_checksum (
      packet[0],
      outer_length);

  packet[4] =
    command;

  packet[5] =
    inner_length & 0xffU;

  packet[6] =
    (inner_length >> 8) & 0xffU;

  if (payload_length > 0)
    {
      memcpy (
        packet + 7,
        payload,
        payload_length);
    }

  packet[
    7 + payload_length
  ] =
    goodix5135_capture_protocol_checksum (
      packet + 4,
      3 + payload_length);

  *logical_length =
    required_length;

  return TRUE;
}


gboolean
goodix5135_capture_parse_ack (
  guint8         expected_command,
  const guint8  *data,
  gsize          data_length)
{
  guint16 outer_length;
  guint16 inner_length;

  guint8 expected_outer_checksum;
  guint8 expected_inner_checksum;

  guint8 status;

  if (data == NULL ||
      data_length <
        GOODIX5135_CAPTURE_ACK_LOGICAL_LENGTH)
    return FALSE;

  if (data[0] !=
      GOODIX5135_PACK_FLAGS_MESSAGE_PROTOCOL)
    return FALSE;

  outer_length =
    ((guint16) data[1]) |
    ((guint16) data[2] << 8);

  if (outer_length !=
      GOODIX5135_CAPTURE_ACK_OUTER_LENGTH)
    return FALSE;

  expected_outer_checksum =
    goodix5135_capture_outer_checksum (
      data[0],
      outer_length);

  if (data[3] !=
      expected_outer_checksum)
    return FALSE;

  if (data[4] !=
      GOODIX5135_COMMAND_ACK)
    return FALSE;

  inner_length =
    ((guint16) data[5]) |
    ((guint16) data[6] << 8);

  if (inner_length !=
      GOODIX5135_CAPTURE_ACK_INNER_LENGTH)
    return FALSE;

  if (data[7] !=
      expected_command)
    return FALSE;

  status =
    data[8];

  /*
   * The public wrapped Goodix ACK contract treats bit 0
   * as the command-success bit.
   */
  if ((status & 0x01U) == 0)
    return FALSE;

  expected_inner_checksum =
    goodix5135_capture_protocol_checksum (
      data + 4,
      5);

  if (data[9] !=
      expected_inner_checksum)
    return FALSE;

  return TRUE;
}


gboolean
goodix5135_capture_parse_response (
  guint8          expected_command,
  const guint8   *data,
  gsize           data_length,
  const guint8  **payload,
  gsize          *payload_length)
{
  guint16 outer_length;
  guint16 inner_length;

  guint8 expected_outer_checksum;
  guint8 expected_inner_checksum;

  gsize actual_payload_length;
  gsize logical_length;
  gsize checksum_offset;

  g_return_val_if_fail (
    payload != NULL,
    FALSE);

  g_return_val_if_fail (
    payload_length != NULL,
    FALSE);

  *payload = NULL;
  *payload_length = 0;

  if (data == NULL ||
      data_length < 8)
    return FALSE;

  if (data[0] !=
      GOODIX5135_PACK_FLAGS_MESSAGE_PROTOCOL)
    return FALSE;

  outer_length =
    ((guint16) data[1]) |
    ((guint16) data[2] << 8);

  if (outer_length < 4)
    return FALSE;

  logical_length =
    GOODIX5135_CAPTURE_OUTER_HEADER_LENGTH +
    (gsize) outer_length;

  if (data_length <
      logical_length)
    return FALSE;

  expected_outer_checksum =
    goodix5135_capture_outer_checksum (
      data[0],
      outer_length);

  if (data[3] !=
      expected_outer_checksum)
    return FALSE;

  if (data[4] !=
      expected_command)
    return FALSE;

  inner_length =
    ((guint16) data[5]) |
    ((guint16) data[6] << 8);

  /*
   * Inner length covers:
   *
   *   payload + one protocol checksum byte.
   *
   * Outer length covers:
   *
   *   command + 2-byte inner length + inner-length bytes.
   */
  if (inner_length < 1)
    return FALSE;

  if (outer_length !=
      (guint16) (
        3U +
        inner_length))
    return FALSE;

  actual_payload_length =
    (gsize) inner_length - 1U;

  checksum_offset =
    7U +
    actual_payload_length;

  if (checksum_offset >=
      logical_length)
    return FALSE;

  if (checksum_offset + 1U !=
      logical_length)
    return FALSE;

  expected_inner_checksum =
    goodix5135_capture_protocol_checksum (
      data + 4,
      3U + actual_payload_length);

  if (data[checksum_offset] !=
      expected_inner_checksum)
    return FALSE;

  *payload =
    data + 7;

  *payload_length =
    actual_payload_length;

  return TRUE;
}


gboolean
goodix5135_capture_build_fdt_manual (
  const guint8 *seed,
  gsize         seed_length,
  guint8       *packet,
  gsize         packet_size,
  gsize        *logical_length)
{
  guint8 payload[
    GOODIX5135_FDT_MANUAL_PAYLOAD_LENGTH
  ];

  if (seed == NULL ||
      seed_length !=
        GOODIX5135_FDT_MANUAL_SEED_BYTES)
    return FALSE;

  payload[0] = 0x0dU;
  payload[1] = 0x01U;

  memcpy (
    payload + 2,
    seed,
    GOODIX5135_FDT_MANUAL_SEED_BYTES);

  return goodix5135_capture_build_command (
    GOODIX5135_FDT_MODE_COMMAND,
    payload,
    sizeof (payload),
    packet,
    packet_size,
    logical_length);
}


gboolean
goodix5135_capture_parse_fdt_response (
  guint8                   expected_command,
  const guint8            *data,
  gsize                    data_length,
  Goodix5135FdtResponse   *response)
{
  const guint8 *payload = NULL;

  gsize payload_length = 0;
  guint index;

  g_return_val_if_fail (
    response != NULL,
    FALSE);

  memset (
    response,
    0,
    sizeof (*response));

  if (expected_command !=
        GOODIX5135_FDT_MODE_COMMAND &&
      expected_command !=
        GOODIX5135_FDT_DOWN_COMMAND &&
      expected_command !=
        GOODIX5135_FDT_UP_COMMAND)
    return FALSE;

  if (!goodix5135_capture_parse_response (
        expected_command,
        data,
        data_length,
        &payload,
        &payload_length))
    return FALSE;

  if (payload_length !=
      GOODIX5135_FDT_RESPONSE_PAYLOAD_LENGTH)
    return FALSE;

  response->irq =
    ((guint16) payload[0]) |
    ((guint16) payload[1] << 8);

  response->touch_flag =
    ((guint16) payload[2]) |
    ((guint16) payload[3] << 8);

  for (index = 0;
       index < GOODIX5135_FDT_ZONE_COUNT;
       index++)
    {
      gsize offset =
        4U + index * 2U;

      response->zones[index] =
        ((guint16) payload[offset]) |
        ((guint16) payload[offset + 1U] << 8);
    }

  return TRUE;
}


gboolean
goodix5135_capture_derive_fdt_down_registers (
  const Goodix5135FdtResponse *response,
  guint8                       registers_out[
                                  GOODIX5135_FDT_REGISTER_BYTES])
{
  guint index;

  g_return_val_if_fail (
    response != NULL,
    FALSE);

  g_return_val_if_fail (
    registers_out != NULL,
    FALSE);

  for (index = 0;
       index < GOODIX5135_FDT_ZONE_COUNT;
       index++)
    {
      guint16 threshold =
        response->zones[index] / 2U;

      /*
       * Historical 5135 FDT-down encoding stores each threshold
       * as the two-byte pair:
       *
       *   0x80, threshold
       *
       * Refuse truncation instead of silently clamping.
       */
      if (threshold > 0xffU)
        return FALSE;

      registers_out[index * 2U] =
        0x80U;

      registers_out[index * 2U + 1U] =
        (guint8) threshold;
    }

  return TRUE;
}


gboolean
goodix5135_capture_build_fdt_down (
  const guint8 *registers,
  gsize         registers_length,
  guint32       timestamp,
  guint8       *packet,
  gsize         packet_size,
  gsize        *logical_length)
{
  guint8 payload[
    GOODIX5135_FDT_DOWN_PAYLOAD_LENGTH
  ];

  if (registers == NULL ||
      registers_length !=
        GOODIX5135_FDT_REGISTER_BYTES)
    return FALSE;

  payload[0] = 0x08U;
  payload[1] = 0x01U;

  memcpy (
    payload + 2,
    registers,
    GOODIX5135_FDT_REGISTER_BYTES);

  payload[14] =
    timestamp & 0xffU;

  payload[15] =
    (timestamp >> 8) & 0xffU;

  payload[16] =
    (timestamp >> 16) & 0xffU;

  payload[17] =
    (timestamp >> 24) & 0xffU;

  return goodix5135_capture_build_command (
    GOODIX5135_FDT_DOWN_COMMAND,
    payload,
    sizeof (payload),
    packet,
    packet_size,
    logical_length);
}


gboolean
goodix5135_capture_build_fdt_up (
  const guint8 *registers,
  gsize         registers_length,
  guint8       *packet,
  gsize         packet_size,
  gsize        *logical_length)
{
  guint8 payload[
    GOODIX5135_FDT_UP_PAYLOAD_LENGTH
  ];

  if (registers == NULL ||
      registers_length !=
        GOODIX5135_FDT_REGISTER_BYTES)
    return FALSE;

  payload[0] = 0x0aU;
  payload[1] = 0x02U;

  memcpy (
    payload + 2,
    registers,
    GOODIX5135_FDT_REGISTER_BYTES);

  return goodix5135_capture_build_command (
    GOODIX5135_FDT_UP_COMMAND,
    payload,
    sizeof (payload),
    packet,
    packet_size,
    logical_length);
}


gboolean
goodix5135_capture_build_image_request (
  guint8 *packet,
  gsize   packet_size,
  gsize  *logical_length)
{
  static const guint8 payload[
    GOODIX5135_IMAGE_REQUEST_PAYLOAD_LENGTH
  ] = {
    0x01U,
    0x00U,
  };

  return goodix5135_capture_build_command (
    GOODIX5135_IMAGE_COMMAND,
    payload,
    sizeof (payload),
    packet,
    packet_size,
    logical_length);
}
