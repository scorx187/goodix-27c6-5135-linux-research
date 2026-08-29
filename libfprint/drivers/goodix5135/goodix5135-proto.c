/*
 * Goodix 27c6:5135 ChicagoHU protocol helpers.
 *
 * Host-side protocol construction/parsing only.
 */

#include <string.h>

#include "goodix5135-proto.h"

const char *
goodix5135_proto_stage_name (void)
{
  return "host-only protocol construction/parser";
}

static guint8
goodix5135_checksum_aa (const guint8 *data,
                       gsize         data_length)
{
  guint sum = 0;
  gsize i;

  for (i = 0; i < data_length; i++)
    sum += data[i];

  return (guint8) ((0xaaU - (sum & 0xffU)) & 0xffU);
}

gboolean
goodix5135_build_firmware_version_request (
  guint8 *packet,
  gsize   packet_size,
  gsize  *logical_length)
{
  guint outer_sum;

  if (packet == NULL || logical_length == NULL)
    return FALSE;

  if (packet_size < GOODIX5135_USB_PACKET_LENGTH)
    return FALSE;

  /*
   * Reference wrapped-protocol request:
   *
   * outer pack:
   *   flags            a0
   *   payload length   06 00
   *   header checksum  a6
   *
   * inner protocol:
   *   command          a8
   *   declared length  03 00
   *   payload          00 00
   *   checksum         ff
   *
   * USBProtocol pads the 10-byte logical frame to one 64-byte
   * full-speed Bulk OUT packet.
   */
  memset (packet, 0, GOODIX5135_USB_PACKET_LENGTH);

  packet[0] = GOODIX5135_PACK_FLAGS_MESSAGE_PROTOCOL;
  packet[1] = 0x06;
  packet[2] = 0x00;

  outer_sum =
    (guint) packet[0] +
    (guint) packet[1] +
    (guint) packet[2];

  packet[3] = (guint8) (outer_sum & 0xffU);

  packet[4] = GOODIX5135_COMMAND_FIRMWARE_VERSION;

  /*
   * Protocol declared length includes:
   *   payload (2 bytes) + checksum (1 byte)
   */
  packet[5] = 0x03;
  packet[6] = 0x00;

  packet[7] = 0x00;
  packet[8] = 0x00;

  packet[9] =
    goodix5135_checksum_aa (packet + 4, 5);

  *logical_length = GOODIX5135_FIRMWARE_REQUEST_LENGTH;

  return TRUE;
}


static gboolean
goodix5135_parse_wrapped_protocol (
  const guint8  *data,
  gsize          data_length,
  guint8         expected_command,
  const guint8 **payload,
  gsize         *payload_length)
{
  guint16 outer_length;
  guint16 protocol_length;
  gsize outer_total_length;
  gsize protocol_total_length;
  const guint8 *protocol;
  guint8 outer_checksum;
  guint8 protocol_checksum;

  if (data == NULL ||
      payload == NULL ||
      payload_length == NULL)
    return FALSE;

  /*
   * Minimum possible frame:
   *
   * outer header      4
   * protocol command  1
   * protocol length   2
   * protocol checksum 1
   */
  if (data_length < 8)
    return FALSE;

  if (data[0] != GOODIX5135_PACK_FLAGS_MESSAGE_PROTOCOL)
    return FALSE;

  outer_length =
    ((guint16) data[1]) |
    ((guint16) data[2] << 8);

  outer_checksum =
    (guint8) (
      ((guint) data[0] +
       (guint) data[1] +
       (guint) data[2]) &
      0xffU);

  if (data[3] != outer_checksum)
    return FALSE;

  outer_total_length = 4U + (gsize) outer_length;

  /*
   * USB reads may contain trailing packet padding.
   * Parse only the declared Goodix logical frame.
   */
  if (data_length < outer_total_length)
    return FALSE;

  protocol = data + 4;

  if (outer_length < 4)
    return FALSE;

  if (protocol[0] != expected_command)
    return FALSE;

  protocol_length =
    ((guint16) protocol[1]) |
    ((guint16) protocol[2] << 8);

  /*
   * The Goodix protocol length includes the final checksum byte.
   */
  if (protocol_length < 1)
    return FALSE;

  protocol_total_length =
    3U + (gsize) protocol_length;

  /*
   * The inner protocol frame must consume exactly the outer payload.
   */
  if (protocol_total_length != (gsize) outer_length)
    return FALSE;

  protocol_checksum =
    goodix5135_checksum_aa (
      protocol,
      2U + (gsize) protocol_length);

  if (protocol[2U + protocol_length] != protocol_checksum)
    return FALSE;

  *payload = protocol + 3;
  *payload_length = (gsize) protocol_length - 1U;

  return TRUE;
}

gboolean
goodix5135_parse_firmware_version_ack (
  const guint8 *data,
  gsize         data_length)
{
  const guint8 *payload;
  gsize payload_length;
  guint8 status;

  if (!goodix5135_parse_wrapped_protocol (
        data,
        data_length,
        GOODIX5135_COMMAND_ACK,
        &payload,
        &payload_length))
    return FALSE;

  /*
   * Reference decode_ack() consumes payload[0] and payload[1] but does
   * not require the ACK payload to end there.  Require only the minimum
   * bytes needed for acknowledged-command and status fields.
   */
  if (payload_length < GOODIX5135_ACK_MIN_PAYLOAD_LENGTH)
    return FALSE;

  if (payload[0] != GOODIX5135_COMMAND_FIRMWARE_VERSION)
    return FALSE;

  status = payload[1];

  /*
   * Reference semantics:
   *
   * bit 0 = ACK structure valid
   * bit 1 = auxiliary boolean status
   *
   * The reference check_ack() returns bit 1 to its caller, but all known
   * command call sites (including firmware_version()) ignore that return
   * value.  They require only:
   *
   *   - valid ACK framing/checksum
   *   - matching acknowledged command
   *   - bit 0 set
   *
   * Therefore both 0x01 and 0x03 are valid ACK status values here.
   */
  if ((status & 0x01U) == 0)
    return FALSE;

  return TRUE;
}

gboolean
goodix5135_parse_firmware_version_response (
  const guint8  *data,
  gsize          data_length,
  const guint8 **firmware,
  gsize         *firmware_length)
{
  const guint8 *payload;
  const guint8 *nul;
  gsize payload_length;

  if (firmware == NULL || firmware_length == NULL)
    return FALSE;

  if (!goodix5135_parse_wrapped_protocol (
        data,
        data_length,
        GOODIX5135_COMMAND_FIRMWARE_VERSION,
        &payload,
        &payload_length))
    return FALSE;

  if (payload_length == 0)
    return FALSE;

  nul = memchr (payload, 0, payload_length);

  *firmware = payload;

  if (nul != NULL)
    *firmware_length = (gsize) (nul - payload);
  else
    *firmware_length = payload_length;

  if (*firmware_length == 0)
    return FALSE;

  return TRUE;
}


gboolean
goodix5135_build_read_sensor_register_request (
  guint16  address,
  guint8   read_length,
  guint8  *packet,
  gsize    packet_size,
  gsize   *logical_length)
{
  guint outer_sum;

  if (packet == NULL || logical_length == NULL)
    return FALSE;

  if (packet_size < GOODIX5135_USB_PACKET_LENGTH)
    return FALSE;

  if (read_length == 0)
    return FALSE;

  /*
   * Reference single-register request:
   *
   * outer pack:
   *   flags            a0
   *   payload length   08 00
   *   header checksum  a8
   *
   * inner protocol:
   *   command          82
   *   declared length  05 00
   *   payload:
   *     mode           00
   *     address        u16 little-endian
   *     read length    u8
   *   checksum         AA-complement
   */
  memset (packet, 0, GOODIX5135_USB_PACKET_LENGTH);

  packet[0] = GOODIX5135_PACK_FLAGS_MESSAGE_PROTOCOL;
  packet[1] = 0x08;
  packet[2] = 0x00;

  outer_sum =
    (guint) packet[0] +
    (guint) packet[1] +
    (guint) packet[2];

  packet[3] = (guint8) (outer_sum & 0xffU);

  packet[4] = GOODIX5135_COMMAND_READ_SENSOR_REGISTER;
  packet[5] = 0x05;
  packet[6] = 0x00;

  packet[7] = 0x00;
  packet[8] = (guint8) (address & 0xffU);
  packet[9] = (guint8) ((address >> 8) & 0xffU);
  packet[10] = read_length;

  packet[11] =
    goodix5135_checksum_aa (packet + 4, 7);

  *logical_length =
    GOODIX5135_REGISTER_READ_REQUEST_LENGTH;

  return TRUE;
}


gboolean
goodix5135_parse_read_sensor_register_ack (
  const guint8 *data,
  gsize         data_length)
{
  const guint8 *payload;
  gsize payload_length;
  guint8 status;

  if (!goodix5135_parse_wrapped_protocol (
        data,
        data_length,
        GOODIX5135_COMMAND_ACK,
        &payload,
        &payload_length))
    return FALSE;

  if (payload_length < GOODIX5135_ACK_MIN_PAYLOAD_LENGTH)
    return FALSE;

  if (payload[0] !=
      GOODIX5135_COMMAND_READ_SENSOR_REGISTER)
    return FALSE;

  status = payload[1];

  /*
   * Match the same reference ACK semantics used by the firmware
   * transaction: bit 0 proves a valid ACK structure. Bit 1 is auxiliary
   * status and is not required by the reference command caller.
   */
  if ((status & 0x01U) == 0)
    return FALSE;

  return TRUE;
}


gboolean
goodix5135_parse_read_sensor_register_response (
  const guint8  *data,
  gsize          data_length,
  gsize          minimum_length,
  const guint8 **value,
  gsize         *value_length)
{
  const guint8 *payload;
  gsize payload_length;

  if (value == NULL || value_length == NULL)
    return FALSE;

  *value = NULL;
  *value_length = 0;

  if (minimum_length == 0)
    return FALSE;

  if (!goodix5135_parse_wrapped_protocol (
        data,
        data_length,
        GOODIX5135_COMMAND_READ_SENSOR_REGISTER,
        &payload,
        &payload_length))
    return FALSE;

  /*
   * Reference read_sensor_register() only requires the response payload
   * to contain at least the requested number of bytes.
   */
  if (payload_length < minimum_length)
    return FALSE;

  *value = payload;
  *value_length = payload_length;

  return TRUE;
}


static gboolean
goodix5135_register_read_transaction_fail (
  Goodix5135RegisterReadTransaction *transaction)
{
  if (transaction != NULL)
    transaction->state =
      GOODIX5135_REGISTER_READ_TRANSACTION_FAILED;

  return FALSE;
}


void
goodix5135_register_read_transaction_init (
  Goodix5135RegisterReadTransaction *transaction)
{
  g_return_if_fail (transaction != NULL);

  transaction->state =
    GOODIX5135_REGISTER_READ_TRANSACTION_IDLE;

  transaction->expected_length = 0;
}


gboolean
goodix5135_register_read_transaction_begin (
  Goodix5135RegisterReadTransaction *transaction,
  guint16                            address,
  guint8                             read_length,
  guint8                            *packet,
  gsize                              packet_size,
  gsize                             *logical_length)
{
  if (transaction == NULL)
    return FALSE;

  if (transaction->state !=
      GOODIX5135_REGISTER_READ_TRANSACTION_IDLE)
    return goodix5135_register_read_transaction_fail (
      transaction);

  if (read_length == 0)
    return goodix5135_register_read_transaction_fail (
      transaction);

  if (!goodix5135_build_read_sensor_register_request (
        address,
        read_length,
        packet,
        packet_size,
        logical_length))
    return goodix5135_register_read_transaction_fail (
      transaction);

  transaction->expected_length = read_length;

  transaction->state =
    GOODIX5135_REGISTER_READ_TRANSACTION_WAIT_OUT;

  return TRUE;
}


gboolean
goodix5135_register_read_transaction_out_complete (
  Goodix5135RegisterReadTransaction *transaction,
  gboolean                           transport_can_advance)
{
  if (transaction == NULL)
    return FALSE;

  if (transaction->state !=
      GOODIX5135_REGISTER_READ_TRANSACTION_WAIT_OUT)
    return goodix5135_register_read_transaction_fail (
      transaction);

  if (!transport_can_advance)
    return goodix5135_register_read_transaction_fail (
      transaction);

  transaction->state =
    GOODIX5135_REGISTER_READ_TRANSACTION_WAIT_ACK;

  return TRUE;
}


gboolean
goodix5135_register_read_transaction_ack_complete (
  Goodix5135RegisterReadTransaction *transaction,
  gboolean                           transport_can_advance,
  const guint8                      *data,
  gsize                              data_length)
{
  if (transaction == NULL)
    return FALSE;

  if (transaction->state !=
      GOODIX5135_REGISTER_READ_TRANSACTION_WAIT_ACK)
    return goodix5135_register_read_transaction_fail (
      transaction);

  if (!transport_can_advance)
    return goodix5135_register_read_transaction_fail (
      transaction);

  if (!goodix5135_parse_read_sensor_register_ack (
        data,
        data_length))
    return goodix5135_register_read_transaction_fail (
      transaction);

  transaction->state =
    GOODIX5135_REGISTER_READ_TRANSACTION_WAIT_RESPONSE;

  return TRUE;
}


gboolean
goodix5135_register_read_transaction_response_complete (
  Goodix5135RegisterReadTransaction *transaction,
  gboolean                           transport_can_advance,
  const guint8                      *data,
  gsize                              data_length,
  const guint8                     **value,
  gsize                             *value_length)
{
  if (value != NULL)
    *value = NULL;

  if (value_length != NULL)
    *value_length = 0;

  if (transaction == NULL)
    return FALSE;

  if (transaction->state !=
      GOODIX5135_REGISTER_READ_TRANSACTION_WAIT_RESPONSE)
    return goodix5135_register_read_transaction_fail (
      transaction);

  if (!transport_can_advance)
    return goodix5135_register_read_transaction_fail (
      transaction);

  if (value == NULL || value_length == NULL)
    return goodix5135_register_read_transaction_fail (
      transaction);

  if (transaction->expected_length == 0)
    return goodix5135_register_read_transaction_fail (
      transaction);

  if (!goodix5135_parse_read_sensor_register_response (
        data,
        data_length,
        transaction->expected_length,
        value,
        value_length))
    return goodix5135_register_read_transaction_fail (
      transaction);

  transaction->state =
    GOODIX5135_REGISTER_READ_TRANSACTION_DONE;

  return TRUE;
}


gboolean
goodix5135_build_otp_read_request (
  guint8 *packet,
  gsize   packet_size,
  gsize  *logical_length)
{
  guint outer_sum;

  if (logical_length != NULL)
    *logical_length = 0;

  if (packet == NULL ||
      logical_length == NULL)
    return FALSE;

  if (packet_size <
      GOODIX5135_USB_PACKET_LENGTH)
    return FALSE;

  memset (
    packet,
    0,
    GOODIX5135_USB_PACKET_LENGTH);

  packet[0] =
    GOODIX5135_PACK_FLAGS_MESSAGE_PROTOCOL;

  /*
   * Inner protocol length:
   *
   * command 1
   * LE16    2
   * payload 2
   * csum    1
   *       ---
   *         6
   */
  packet[1] = 0x06;
  packet[2] = 0x00;

  outer_sum =
    (guint) packet[0] +
    (guint) packet[1] +
    (guint) packet[2];

  packet[3] =
    (guint8) (
      outer_sum & 0xffU);

  packet[4] =
    GOODIX5135_COMMAND_READ_OTP;

  /*
   * Payload length 2 + checksum 1.
   */
  packet[5] = 0x03;
  packet[6] = 0x00;

  packet[7] = 0x00;
  packet[8] = 0x00;

  packet[9] =
    goodix5135_checksum_aa (
      packet + 4,
      5);

  *logical_length =
    GOODIX5135_OTP_READ_REQUEST_LENGTH;

  return TRUE;
}


gboolean
goodix5135_parse_otp_read_ack (
  const guint8 *data,
  gsize         data_length)
{
  const guint8 *payload;
  gsize payload_length;

  if (!goodix5135_parse_wrapped_protocol (
        data,
        data_length,
        GOODIX5135_COMMAND_ACK,
        &payload,
        &payload_length))
    return FALSE;

  if (payload_length <
      GOODIX5135_ACK_MIN_PAYLOAD_LENGTH)
    return FALSE;

  if (payload[0] !=
      GOODIX5135_COMMAND_READ_OTP)
    return FALSE;

  if ((payload[1] & 0x01U) == 0)
    return FALSE;

  return TRUE;
}


gboolean
goodix5135_parse_otp_read_response (
  const guint8 *data,
  gsize         data_length,
  guint8       *otp,
  gsize         otp_size)
{
  const guint8 *payload;
  gsize payload_length;

  if (otp == NULL ||
      otp_size <
        GOODIX5135_OTP_LENGTH)
    return FALSE;

  /*
   * Fail closed and avoid leaving stale caller data when the
   * response is rejected.
   */
  memset (
    otp,
    0,
    GOODIX5135_OTP_LENGTH);

  if (!goodix5135_parse_wrapped_protocol (
        data,
        data_length,
        GOODIX5135_COMMAND_READ_OTP,
        &payload,
        &payload_length))
    return FALSE;

  /*
   * The ChicagoHU calibration parser requires exactly
   * 64 OTP bytes. Do not silently accept a short or
   * extended representation.
   */
  if (payload_length !=
      GOODIX5135_OTP_LENGTH)
    return FALSE;

  memcpy (
    otp,
    payload,
    GOODIX5135_OTP_LENGTH);

  return TRUE;
}


static gboolean
goodix5135_otp_read_transaction_fail (
  Goodix5135OtpReadTransaction *transaction)
{
  if (transaction != NULL)
    transaction->state =
      GOODIX5135_OTP_READ_TRANSACTION_FAILED;

  return FALSE;
}


void
goodix5135_otp_read_transaction_init (
  Goodix5135OtpReadTransaction *transaction)
{
  g_return_if_fail (transaction != NULL);

  transaction->state =
    GOODIX5135_OTP_READ_TRANSACTION_IDLE;
}


gboolean
goodix5135_otp_read_transaction_begin (
  Goodix5135OtpReadTransaction *transaction,
  guint8                       *packet,
  gsize                         packet_size,
  gsize                        *logical_length)
{
  if (transaction == NULL)
    return FALSE;

  if (transaction->state !=
      GOODIX5135_OTP_READ_TRANSACTION_IDLE)
    return goodix5135_otp_read_transaction_fail (
      transaction);

  if (!goodix5135_build_otp_read_request (
        packet,
        packet_size,
        logical_length))
    return goodix5135_otp_read_transaction_fail (
      transaction);

  transaction->state =
    GOODIX5135_OTP_READ_TRANSACTION_WAIT_OUT;

  return TRUE;
}


gboolean
goodix5135_otp_read_transaction_out_complete (
  Goodix5135OtpReadTransaction *transaction,
  gboolean                      transport_can_advance)
{
  if (transaction == NULL)
    return FALSE;

  if (transaction->state !=
      GOODIX5135_OTP_READ_TRANSACTION_WAIT_OUT)
    return goodix5135_otp_read_transaction_fail (
      transaction);

  if (!transport_can_advance)
    return goodix5135_otp_read_transaction_fail (
      transaction);

  transaction->state =
    GOODIX5135_OTP_READ_TRANSACTION_WAIT_ACK;

  return TRUE;
}


gboolean
goodix5135_otp_read_transaction_ack_complete (
  Goodix5135OtpReadTransaction *transaction,
  gboolean                      transport_can_advance,
  const guint8                 *data,
  gsize                         data_length)
{
  if (transaction == NULL)
    return FALSE;

  if (transaction->state !=
      GOODIX5135_OTP_READ_TRANSACTION_WAIT_ACK)
    return goodix5135_otp_read_transaction_fail (
      transaction);

  if (!transport_can_advance)
    return goodix5135_otp_read_transaction_fail (
      transaction);

  if (!goodix5135_parse_otp_read_ack (
        data,
        data_length))
    return goodix5135_otp_read_transaction_fail (
      transaction);

  transaction->state =
    GOODIX5135_OTP_READ_TRANSACTION_WAIT_RESPONSE;

  return TRUE;
}


gboolean
goodix5135_otp_read_transaction_response_complete (
  Goodix5135OtpReadTransaction *transaction,
  gboolean                      transport_can_advance,
  const guint8                 *data,
  gsize                         data_length,
  guint8                       *otp,
  gsize                         otp_size)
{
  if (otp != NULL &&
      otp_size >= GOODIX5135_OTP_LENGTH)
    memset (
      otp,
      0,
      GOODIX5135_OTP_LENGTH);

  if (transaction == NULL)
    return FALSE;

  if (transaction->state !=
      GOODIX5135_OTP_READ_TRANSACTION_WAIT_RESPONSE)
    return goodix5135_otp_read_transaction_fail (
      transaction);

  if (!transport_can_advance)
    return goodix5135_otp_read_transaction_fail (
      transaction);

  if (!goodix5135_parse_otp_read_response (
        data,
        data_length,
        otp,
        otp_size))
    return goodix5135_otp_read_transaction_fail (
      transaction);

  transaction->state =
    GOODIX5135_OTP_READ_TRANSACTION_DONE;

  return TRUE;
}


static guint8
goodix5135_crc8 (
  const guint8 *data,
  gsize         length)
{
  guint8 crc = 0;

  for (gsize i = 0;
       i < length;
       i++)
    {
      crc ^= data[i];

      for (guint bit = 0;
           bit < 8;
           bit++)
        {
          if ((crc & 0x80U) != 0)
            crc =
              (guint8) (
                (crc << 1) ^ 0x07U);
          else
            crc =
              (guint8) (
                crc << 1);
        }
    }

  return crc;
}


static guint8
goodix5135_inv_crc8_parts (
  const guint8 *part1,
  gsize         part1_length,
  const guint8 *part2,
  gsize         part2_length,
  const guint8 *part3,
  gsize         part3_length,
  const guint8 *part4,
  gsize         part4_length)
{
  guint8 buffer[64];
  gsize offset = 0;

  const guint8 *parts[] = {
    part1,
    part2,
    part3,
    part4,
  };

  const gsize lengths[] = {
    part1_length,
    part2_length,
    part3_length,
    part4_length,
  };

  for (guint p = 0; p < G_N_ELEMENTS (parts); p++)
    {
      if (parts[p] == NULL &&
          lengths[p] != 0)
        return 0;

      if (offset + lengths[p] >
          sizeof (buffer))
        return 0;

      if (lengths[p] != 0)
        memcpy (
          buffer + offset,
          parts[p],
          lengths[p]);

      offset += lengths[p];
    }

  return (guint8) ~goodix5135_crc8 (
    buffer,
    offset);
}


static guint16
goodix5135_u16le (
  const guint8 *data,
  gsize         offset)
{
  return
    (guint16) data[offset] |
    ((guint16) data[offset + 1] << 8);
}


static void
goodix5135_put_u16le (
  guint8  *data,
  gsize    offset,
  guint16  value)
{
  data[offset] =
    (guint8) (
      value & 0xffU);

  data[offset + 1] =
    (guint8) (
      (value >> 8) & 0xffU);
}


static guint8
goodix5135_majority_fdt_offset (
  guint8 encoded)
{
  guint8 a;
  guint8 b;
  guint8 c;

  if (encoded == 0)
    return 0;

  a = encoded & 0x03U;
  b = (encoded >> 2) & 0x03U;
  c = (encoded >> 4) & 0x03U;

  if (a == c ||
      a == b)
    return a;

  if (c == b)
    return c;

  return 0;
}


gboolean
goodix5135_validate_otp (
  const guint8 *otp,
  gsize         otp_length)
{
  guint8 cp;
  guint8 mt;
  guint8 ft;
  guint8 ft_dac;
  guint8 mt_dac;

  if (otp == NULL ||
      otp_length != GOODIX5135_OTP_LENGTH)
    return FALSE;

  /*
   * CP:
   *   otp[0:11] + otp[36:40]
   *   stored at otp[60]
   */
  cp =
    goodix5135_inv_crc8_parts (
      otp + 0, 11,
      otp + 36, 4,
      NULL, 0,
      NULL, 0);

  /*
   * FT_DAC:
   *   otp[50:54]
   *   stored at otp[62]
   */
  ft_dac =
    goodix5135_inv_crc8_parts (
      otp + 50, 4,
      NULL, 0,
      NULL, 0,
      NULL, 0);

  /*
   * MT_DAC:
   *   otp[46:50]
   *   stored at otp[22]
   */
  mt_dac =
    goodix5135_inv_crc8_parts (
      otp + 46, 4,
      NULL, 0,
      NULL, 0,
      NULL, 0);

  /*
   * MT:
   *   otp[20:28]
   * + otp[29:36]
   * + otp[40:50]
   * + otp[54:56]
   * stored at otp[63]
   */
  mt =
    goodix5135_inv_crc8_parts (
      otp + 20, 8,
      otp + 29, 7,
      otp + 40, 10,
      otp + 54, 2);

  /*
   * FT:
   *   otp[11:20]
   * + otp[28:29]
   * + otp[50:54]
   * + otp[56:60]
   * + otp[62]
   *
   * The final one-byte part is appended separately below.
   */
  {
    guint8 ft_buffer[19];
    gsize offset = 0;

    memcpy (ft_buffer + offset, otp + 11, 9);
    offset += 9;

    memcpy (ft_buffer + offset, otp + 28, 1);
    offset += 1;

    memcpy (ft_buffer + offset, otp + 50, 4);
    offset += 4;

    memcpy (ft_buffer + offset, otp + 56, 4);
    offset += 4;

    ft_buffer[offset++] = otp[62];

    ft =
      (guint8) ~goodix5135_crc8 (
        ft_buffer,
        offset);
  }

  if (cp != otp[60] ||
      ft != otp[61] ||
      ft_dac != otp[62] ||
      mt != otp[63] ||
      mt_dac != otp[22])
    return FALSE;

  if (memcmp (
        otp + 46,
        otp + 50,
        4) != 0)
    return FALSE;

  return TRUE;
}


gboolean
goodix5135_parse_otp_calibration (
  const guint8             *otp,
  gsize                     otp_length,
  Goodix5135OtpCalibration *calibration)
{
  guint8 b42;
  guint16 tcode;
  guint32 fdt_delta_work;

  if (calibration == NULL)
    return FALSE;

  memset (
    calibration,
    0,
    sizeof (*calibration));

  if (!goodix5135_validate_otp (
        otp,
        otp_length))
    return FALSE;

  b42 = otp[42];

  tcode =
    (guint16) (
      (((b42 >> 4) + 1U) * 16U) +
      64U);

  fdt_delta_work =
    ((guint32) ((b42 & 0x0fU) + 2U) *
     25600U);

  fdt_delta_work /=
    tcode;

  fdt_delta_work /=
    3U;

  fdt_delta_work >>=
    4;

  calibration->tcode =
    tcode;

  calibration->fdt_delta =
    (guint8) (
      fdt_delta_work & 0xffU);

  calibration->fdt_offset =
    goodix5135_majority_fdt_offset (
      otp[27]);

  calibration->dac0 =
    (guint16) (
      ((guint16) otp[46] << 4) |
      0x08U);

  calibration->dac1 =
    otp[47];

  calibration->dac2 =
    otp[48];

  calibration->dac3 =
    otp[49];

  return TRUE;
}


typedef struct
{
  guint16 address;
  gsize   address_offset;
  gsize   value_offset;
  guint16 static_value;
} Goodix5135Cfg70RegisterField;


static const Goodix5135Cfg70RegisterField
goodix5135_cfg70_register_fields[] = {
  { 0x005cU, 0x71U, 0x73U, 0x0180U },
  { 0x0220U, 0x75U, 0x77U, 0x0808U },
  { 0x0236U, 0x79U, 0x7bU, 0x0080U },
  { 0x0238U, 0x7dU, 0x7fU, 0x0080U },
  { 0x023aU, 0x81U, 0x83U, 0x0080U },
  { 0x0082U, 0xadU, 0xafU, 0x1580U },
};


gboolean
goodix5135_validate_cfg70_template (
  const guint8 *template_data,
  gsize         template_length)
{
  static const guint8 prefix[] = {
    GOODIX5135_CFG70_PREFIX_0,
    GOODIX5135_CFG70_PREFIX_1,
    GOODIX5135_CFG70_PREFIX_2,
    GOODIX5135_CFG70_PREFIX_3,
  };

  if (template_data == NULL ||
      template_length !=
        GOODIX5135_CONFIG_LENGTH)
    return FALSE;

  if (memcmp (
        template_data,
        prefix,
        sizeof (prefix)) != 0)
    return FALSE;

  for (guint i = 0;
       i < G_N_ELEMENTS (
         goodix5135_cfg70_register_fields);
       i++)
    {
      const Goodix5135Cfg70RegisterField *field =
        &goodix5135_cfg70_register_fields[i];

      if (goodix5135_u16le (
            template_data,
            field->address_offset) !=
          field->address)
        return FALSE;

      if (goodix5135_u16le (
            template_data,
            field->value_offset) !=
          field->static_value)
        return FALSE;
    }

  return TRUE;
}


gboolean
goodix5135_cfg70_checksum (
  const guint8 *config,
  gsize         config_length,
  guint16      *checksum)
{
  guint32 total = 0xa5a5U;

  if (checksum != NULL)
    *checksum = 0;

  if (config == NULL ||
      checksum == NULL ||
      config_length !=
        GOODIX5135_CONFIG_LENGTH)
    return FALSE;

  for (gsize offset = 0;
       offset < GOODIX5135_CFG70_CHECKSUM_OFFSET;
       offset += 2)
    {
      total +=
        goodix5135_u16le (
          config,
          offset);

      total &= 0xffffU;
    }

  *checksum =
    (guint16) (
      (0U - total) & 0xffffU);

  return TRUE;
}


gboolean
goodix5135_build_runtime_config (
  const guint8                    *template_data,
  gsize                            template_length,
  const Goodix5135OtpCalibration *calibration,
  guint8                          *runtime_config,
  gsize                            runtime_config_size)
{
  guint16 checksum;

  if (template_data == NULL ||
      calibration == NULL ||
      runtime_config == NULL)
    return FALSE;

  if (template_length !=
      GOODIX5135_CONFIG_LENGTH ||
      runtime_config_size <
        GOODIX5135_CONFIG_LENGTH)
    return FALSE;

  if (!goodix5135_validate_cfg70_template (
        template_data,
        template_length))
    return FALSE;

  memcpy (
    runtime_config,
    template_data,
    GOODIX5135_CONFIG_LENGTH);

  goodix5135_put_u16le (
    runtime_config,
    0x73U,
    calibration->tcode);

  goodix5135_put_u16le (
    runtime_config,
    0x77U,
    calibration->dac0);

  goodix5135_put_u16le (
    runtime_config,
    0x7bU,
    calibration->dac1);

  goodix5135_put_u16le (
    runtime_config,
    0x7fU,
    calibration->dac2);

  goodix5135_put_u16le (
    runtime_config,
    0x83U,
    calibration->dac3);

  goodix5135_put_u16le (
    runtime_config,
    0xafU,
    (guint16) (
      ((guint16) calibration->fdt_delta << 8) |
      0x80U));

  if (!goodix5135_cfg70_checksum (
        runtime_config,
        GOODIX5135_CONFIG_LENGTH,
        &checksum))
    return FALSE;

  goodix5135_put_u16le (
    runtime_config,
    GOODIX5135_CFG70_CHECKSUM_OFFSET,
    checksum);

  {
    guint16 verify;

    if (!goodix5135_cfg70_checksum (
          runtime_config,
          GOODIX5135_CONFIG_LENGTH,
          &verify))
      return FALSE;

    if (goodix5135_u16le (
          runtime_config,
          GOODIX5135_CFG70_CHECKSUM_OFFSET) !=
        verify)
      return FALSE;
  }

  return TRUE;
}


gboolean
goodix5135_build_config_upload_transfer (
  const guint8 *config,
  gsize         config_length,
  guint8       *transfer,
  gsize         transfer_size,
  gsize        *logical_length,
  gsize        *transport_length)
{
  gsize inner_protocol_length;
  gsize inner_length_field;
  guint outer_sum;

  if (logical_length != NULL)
    *logical_length = 0;

  if (transport_length != NULL)
    *transport_length = 0;

  if (config == NULL ||
      transfer == NULL ||
      logical_length == NULL ||
      transport_length == NULL)
    return FALSE;

  if (config_length !=
      GOODIX5135_CONFIG_LENGTH)
    return FALSE;

  if (transfer_size <
      GOODIX5135_CONFIG_TRANSFER_LENGTH)
    return FALSE;

  memset (
    transfer,
    0,
    GOODIX5135_CONFIG_TRANSFER_LENGTH);

  /*
   * Inner protocol:
   *
   *   command           1
   *   LE16 length       2
   *   config          224
   *   checksum           1
   *                  ----
   *                    228 bytes
   */
  inner_protocol_length =
    1U +
    2U +
    config_length +
    1U;

  /*
   * Goodix inner length field includes:
   *
   *   payload + checksum
   *
   * = 224 + 1
   * = 225
   * = 0x00e1
   */
  inner_length_field =
    config_length + 1U;

  /*
   * Outer message-pack header:
   *
   *   a0 e4 00 84
   */
  transfer[0] =
    GOODIX5135_PACK_FLAGS_MESSAGE_PROTOCOL;

  transfer[1] =
    (guint8) (
      inner_protocol_length & 0xffU);

  transfer[2] =
    (guint8) (
      (inner_protocol_length >> 8) & 0xffU);

  outer_sum =
    (guint) transfer[0] +
    (guint) transfer[1] +
    (guint) transfer[2];

  transfer[3] =
    (guint8) (
      outer_sum & 0xffU);

  /*
   * Inner protocol header.
   */
  transfer[4] =
    GOODIX5135_COMMAND_UPLOAD_CONFIG_MCU;

  transfer[5] =
    (guint8) (
      inner_length_field & 0xffU);

  transfer[6] =
    (guint8) (
      (inner_length_field >> 8) & 0xffU);

  /*
   * The caller-provided configuration is copied only into this
   * caller-owned transient transfer buffer.
   */
  memcpy (
    transfer + 7,
    config,
    config_length);

  /*
   * Checksum covers:
   *
   *   command + LE16 length + configuration
   *
   * and is appended immediately after the 224-byte config.
   */
  transfer[7 + config_length] =
    goodix5135_checksum_aa (
      transfer + 4,
      3U + config_length);

  *logical_length =
    4U + inner_protocol_length;

  *transport_length =
    GOODIX5135_CONFIG_TRANSFER_LENGTH;

  if (*logical_length !=
      GOODIX5135_CONFIG_LOGICAL_LENGTH)
    {
      *logical_length = 0;
      *transport_length = 0;
      return FALSE;
    }

  return TRUE;
}


gboolean
goodix5135_config_upload_get_packet (
  const guint8  *transfer,
  gsize          transfer_length,
  guint          packet_index,
  const guint8 **packet,
  gsize         *packet_length)
{
  if (packet != NULL)
    *packet = NULL;

  if (packet_length != NULL)
    *packet_length = 0;

  if (transfer == NULL ||
      packet == NULL ||
      packet_length == NULL)
    return FALSE;

  if (transfer_length !=
      GOODIX5135_CONFIG_TRANSFER_LENGTH)
    return FALSE;

  if (packet_index >=
      GOODIX5135_CONFIG_USB_PACKET_COUNT)
    return FALSE;

  *packet =
    transfer +
    ((gsize) packet_index *
     GOODIX5135_USB_PACKET_LENGTH);

  *packet_length =
    GOODIX5135_USB_PACKET_LENGTH;

  return TRUE;
}


gboolean
goodix5135_parse_config_upload_ack (
  const guint8 *data,
  gsize         data_length)
{
  const guint8 *payload;
  gsize payload_length;

  if (!goodix5135_parse_wrapped_protocol (
        data,
        data_length,
        GOODIX5135_COMMAND_ACK,
        &payload,
        &payload_length))
    return FALSE;

  if (payload_length <
      GOODIX5135_ACK_MIN_PAYLOAD_LENGTH)
    return FALSE;

  if (payload[0] !=
      GOODIX5135_COMMAND_UPLOAD_CONFIG_MCU)
    return FALSE;

  if ((payload[1] & 0x01U) == 0)
    return FALSE;

  return TRUE;
}


gboolean
goodix5135_parse_config_upload_response (
  const guint8 *data,
  gsize         data_length)
{
  const guint8 *payload;
  gsize payload_length;

  if (!goodix5135_parse_wrapped_protocol (
        data,
        data_length,
        GOODIX5135_COMMAND_UPLOAD_CONFIG_MCU,
        &payload,
        &payload_length))
    return FALSE;

  if (payload_length < 1U)
    return FALSE;

  return payload[0] == 0x01U;
}


static gboolean
goodix5135_config_upload_transaction_fail (
  Goodix5135ConfigUploadTransaction *transaction)
{
  if (transaction != NULL)
    transaction->state =
      GOODIX5135_CONFIG_UPLOAD_TRANSACTION_FAILED;

  return FALSE;
}


void
goodix5135_config_upload_transaction_init (
  Goodix5135ConfigUploadTransaction *transaction)
{
  g_return_if_fail (transaction != NULL);

  transaction->state =
    GOODIX5135_CONFIG_UPLOAD_TRANSACTION_IDLE;

  transaction->packets_completed = 0;
}


gboolean
goodix5135_config_upload_transaction_begin (
  Goodix5135ConfigUploadTransaction *transaction,
  const guint8                      *config,
  gsize                              config_length,
  guint8                            *transfer,
  gsize                              transfer_size,
  gsize                             *logical_length,
  gsize                             *transport_length)
{
  if (transaction == NULL)
    return FALSE;

  if (transaction->state !=
      GOODIX5135_CONFIG_UPLOAD_TRANSACTION_IDLE)
    return goodix5135_config_upload_transaction_fail (
      transaction);

  if (!goodix5135_build_config_upload_transfer (
        config,
        config_length,
        transfer,
        transfer_size,
        logical_length,
        transport_length))
    return goodix5135_config_upload_transaction_fail (
      transaction);

  transaction->packets_completed = 0;

  transaction->state =
    GOODIX5135_CONFIG_UPLOAD_TRANSACTION_WAIT_OUT;

  return TRUE;
}


gboolean
goodix5135_config_upload_transaction_out_complete (
  Goodix5135ConfigUploadTransaction *transaction,
  gboolean                           transport_can_advance)
{
  if (transaction == NULL)
    return FALSE;

  if (transaction->state !=
      GOODIX5135_CONFIG_UPLOAD_TRANSACTION_WAIT_OUT)
    return goodix5135_config_upload_transaction_fail (
      transaction);

  if (!transport_can_advance)
    return goodix5135_config_upload_transaction_fail (
      transaction);

  if (transaction->packets_completed >=
      GOODIX5135_CONFIG_USB_PACKET_COUNT)
    return goodix5135_config_upload_transaction_fail (
      transaction);

  transaction->packets_completed++;

  if (transaction->packets_completed ==
      GOODIX5135_CONFIG_USB_PACKET_COUNT)
    transaction->state =
      GOODIX5135_CONFIG_UPLOAD_TRANSACTION_WAIT_ACK;

  return TRUE;
}


gboolean
goodix5135_config_upload_transaction_ack_complete (
  Goodix5135ConfigUploadTransaction *transaction,
  gboolean                           transport_can_advance,
  const guint8                      *data,
  gsize                              data_length)
{
  if (transaction == NULL)
    return FALSE;

  if (transaction->state !=
      GOODIX5135_CONFIG_UPLOAD_TRANSACTION_WAIT_ACK)
    return goodix5135_config_upload_transaction_fail (
      transaction);

  if (!transport_can_advance)
    return goodix5135_config_upload_transaction_fail (
      transaction);

  if (!goodix5135_parse_config_upload_ack (
        data,
        data_length))
    return goodix5135_config_upload_transaction_fail (
      transaction);

  transaction->state =
    GOODIX5135_CONFIG_UPLOAD_TRANSACTION_WAIT_RESPONSE;

  return TRUE;
}


gboolean
goodix5135_config_upload_transaction_response_complete (
  Goodix5135ConfigUploadTransaction *transaction,
  gboolean                           transport_can_advance,
  const guint8                      *data,
  gsize                              data_length)
{
  if (transaction == NULL)
    return FALSE;

  if (transaction->state !=
      GOODIX5135_CONFIG_UPLOAD_TRANSACTION_WAIT_RESPONSE)
    return goodix5135_config_upload_transaction_fail (
      transaction);

  if (!transport_can_advance)
    return goodix5135_config_upload_transaction_fail (
      transaction);

  if (!goodix5135_parse_config_upload_response (
        data,
        data_length))
    return goodix5135_config_upload_transaction_fail (
      transaction);

  transaction->state =
    GOODIX5135_CONFIG_UPLOAD_TRANSACTION_DONE;

  return TRUE;
}


static gboolean
goodix5135_activation_sequence_fail (
  Goodix5135ActivationSequence *sequence)
{
  if (sequence != NULL)
    sequence->state =
      GOODIX5135_ACTIVATION_FAILED;

  return FALSE;
}


void
goodix5135_activation_sequence_init (
  Goodix5135ActivationSequence *sequence)
{
  g_return_if_fail (sequence != NULL);

  sequence->state =
    GOODIX5135_ACTIVATION_IDLE;
}


gboolean
goodix5135_activation_sequence_begin (
  Goodix5135ActivationSequence *sequence)
{
  if (sequence == NULL)
    return FALSE;

  if (sequence->state !=
      GOODIX5135_ACTIVATION_IDLE)
    return goodix5135_activation_sequence_fail (
      sequence);

  sequence->state =
    GOODIX5135_ACTIVATION_WAIT_NOP1;

  return TRUE;
}


gboolean
goodix5135_activation_sequence_nop_complete (
  Goodix5135ActivationSequence *sequence,
  gboolean                      success)
{
  if (sequence == NULL)
    return FALSE;

  if (!success)
    return goodix5135_activation_sequence_fail (
      sequence);

  switch (sequence->state)
    {
    case GOODIX5135_ACTIVATION_WAIT_NOP1:
      sequence->state =
        GOODIX5135_ACTIVATION_WAIT_D4;
      return TRUE;

    case GOODIX5135_ACTIVATION_WAIT_NOP2:
      sequence->state =
        GOODIX5135_ACTIVATION_WAIT_ENABLE_CHIP;
      return TRUE;

    case GOODIX5135_ACTIVATION_WAIT_NOP3:
      sequence->state =
        GOODIX5135_ACTIVATION_WAIT_FIRMWARE;
      return TRUE;

    case GOODIX5135_ACTIVATION_IDLE:
    case GOODIX5135_ACTIVATION_WAIT_D4:
    case GOODIX5135_ACTIVATION_WAIT_ENABLE_CHIP:
    case GOODIX5135_ACTIVATION_WAIT_FIRMWARE:
    case GOODIX5135_ACTIVATION_WAIT_RESET:
    case GOODIX5135_ACTIVATION_WAIT_CHIP_ID:
    case GOODIX5135_ACTIVATION_DONE:
    case GOODIX5135_ACTIVATION_FAILED:
      return goodix5135_activation_sequence_fail (
        sequence);
    }

  /*
   * Defensive fallback for an invalid enum representation.
   *
   * Keep every declared enum value explicit above so that
   * -Wswitch-enum still detects future omissions.
   */
  return goodix5135_activation_sequence_fail (
    sequence);
}


gboolean
goodix5135_activation_sequence_d4_complete (
  Goodix5135ActivationSequence *sequence,
  gboolean                      success)
{
  if (sequence == NULL)
    return FALSE;

  if (sequence->state !=
      GOODIX5135_ACTIVATION_WAIT_D4)
    return goodix5135_activation_sequence_fail (
      sequence);

  if (!success)
    return goodix5135_activation_sequence_fail (
      sequence);

  sequence->state =
    GOODIX5135_ACTIVATION_WAIT_NOP2;

  return TRUE;
}


gboolean
goodix5135_activation_sequence_enable_chip_complete (
  Goodix5135ActivationSequence *sequence,
  gboolean                      success)
{
  if (sequence == NULL)
    return FALSE;

  if (sequence->state !=
      GOODIX5135_ACTIVATION_WAIT_ENABLE_CHIP)
    return goodix5135_activation_sequence_fail (
      sequence);

  if (!success)
    return goodix5135_activation_sequence_fail (
      sequence);

  sequence->state =
    GOODIX5135_ACTIVATION_WAIT_NOP3;

  return TRUE;
}


gboolean
goodix5135_activation_sequence_firmware_complete (
  Goodix5135ActivationSequence *sequence,
  gboolean                      success)
{
  if (sequence == NULL)
    return FALSE;

  if (sequence->state !=
      GOODIX5135_ACTIVATION_WAIT_FIRMWARE)
    return goodix5135_activation_sequence_fail (
      sequence);

  if (!success)
    return goodix5135_activation_sequence_fail (
      sequence);

  sequence->state =
    GOODIX5135_ACTIVATION_WAIT_RESET;

  return TRUE;
}


gboolean
goodix5135_activation_sequence_reset_complete (
  Goodix5135ActivationSequence *sequence,
  gboolean                      success)
{
  if (sequence == NULL)
    return FALSE;

  if (sequence->state !=
      GOODIX5135_ACTIVATION_WAIT_RESET)
    return goodix5135_activation_sequence_fail (
      sequence);

  if (!success)
    return goodix5135_activation_sequence_fail (
      sequence);

  sequence->state =
    GOODIX5135_ACTIVATION_WAIT_CHIP_ID;

  return TRUE;
}


gboolean
goodix5135_activation_sequence_chip_id_complete (
  Goodix5135ActivationSequence *sequence,
  gboolean                      success,
  const guint8                 *chip_id,
  gsize                         chip_id_length)
{
  static const guint8 expected_chip_id[
    GOODIX5135_CHIP_ID_LENGTH
  ] = {
    0xa2,
    0x04,
    0x25,
    0x00,
  };

  if (sequence == NULL)
    return FALSE;

  if (sequence->state !=
      GOODIX5135_ACTIVATION_WAIT_CHIP_ID)
    return goodix5135_activation_sequence_fail (
      sequence);

  if (!success)
    return goodix5135_activation_sequence_fail (
      sequence);

  if (chip_id == NULL ||
      chip_id_length <
        GOODIX5135_CHIP_ID_LENGTH)
    return goodix5135_activation_sequence_fail (
      sequence);

  if (memcmp (
        chip_id,
        expected_chip_id,
        GOODIX5135_CHIP_ID_LENGTH) != 0)
    return goodix5135_activation_sequence_fail (
      sequence);

  sequence->state =
    GOODIX5135_ACTIVATION_DONE;

  return TRUE;
}


gboolean
goodix5135_build_sensor_reset_request (
  guint8 *packet,
  gsize   packet_size,
  gsize  *logical_length)
{
  guint outer_sum;

  if (packet == NULL ||
      logical_length == NULL)
    return FALSE;

  if (packet_size <
      GOODIX5135_USB_PACKET_LENGTH)
    return FALSE;

  memset (
    packet,
    0,
    GOODIX5135_USB_PACKET_LENGTH);

  /*
   * reset(True, False, 20):
   *
   * flags:
   *   bit0 = 1
   *   bit1 = 0
   *   bit2 = 1
   *         ----
   *         0x05
   *
   * delay = 20 = 0x14
   */
  packet[0] =
    GOODIX5135_PACK_FLAGS_MESSAGE_PROTOCOL;

  packet[1] = 0x06;
  packet[2] = 0x00;

  outer_sum =
    (guint) packet[0] +
    (guint) packet[1] +
    (guint) packet[2];

  packet[3] =
    (guint8) (outer_sum & 0xffU);

  packet[4] =
    GOODIX5135_COMMAND_RESET;

  packet[5] = 0x03;
  packet[6] = 0x00;

  packet[7] =
    GOODIX5135_SENSOR_RESET_FLAGS;

  packet[8] =
    GOODIX5135_SENSOR_RESET_DELAY;

  packet[9] =
    goodix5135_checksum_aa (
      packet + 4,
      5);

  *logical_length =
    GOODIX5135_SENSOR_RESET_REQUEST_LENGTH;

  return TRUE;
}


gboolean
goodix5135_parse_sensor_reset_ack (
  const guint8 *data,
  gsize         data_length)
{
  const guint8 *payload;
  gsize payload_length;

  if (!goodix5135_parse_wrapped_protocol (
        data,
        data_length,
        GOODIX5135_COMMAND_ACK,
        &payload,
        &payload_length))
    return FALSE;

  if (payload_length <
      GOODIX5135_ACK_MIN_PAYLOAD_LENGTH)
    return FALSE;

  if (payload[0] !=
      GOODIX5135_COMMAND_RESET)
    return FALSE;

  if ((payload[1] & 0x01U) == 0)
    return FALSE;

  return TRUE;
}


gboolean
goodix5135_parse_sensor_reset_response (
  const guint8 *data,
  gsize         data_length,
  guint16      *result_number)
{
  const guint8 *payload;
  gsize payload_length;

  if (result_number == NULL)
    return FALSE;

  *result_number = 0;

  if (!goodix5135_parse_wrapped_protocol (
        data,
        data_length,
        GOODIX5135_COMMAND_RESET,
        &payload,
        &payload_length))
    return FALSE;

  if (payload_length < 1)
    return FALSE;

  if (payload[0] != 0x01U)
    return FALSE;

  if (payload_length < 3)
    return FALSE;

  *result_number =
    (guint16) payload[1] |
    ((guint16) payload[2] << 8);

  return TRUE;
}


static gboolean
goodix5135_sensor_reset_transaction_fail (
  Goodix5135SensorResetTransaction *transaction)
{
  if (transaction != NULL)
    transaction->state =
      GOODIX5135_SENSOR_RESET_TRANSACTION_FAILED;

  return FALSE;
}


void
goodix5135_sensor_reset_transaction_init (
  Goodix5135SensorResetTransaction *transaction)
{
  g_return_if_fail (transaction != NULL);

  transaction->state =
    GOODIX5135_SENSOR_RESET_TRANSACTION_IDLE;
}


gboolean
goodix5135_sensor_reset_transaction_begin (
  Goodix5135SensorResetTransaction *transaction,
  guint8                           *packet,
  gsize                             packet_size,
  gsize                            *logical_length)
{
  if (transaction == NULL)
    return FALSE;

  if (transaction->state !=
      GOODIX5135_SENSOR_RESET_TRANSACTION_IDLE)
    return goodix5135_sensor_reset_transaction_fail (
      transaction);

  if (!goodix5135_build_sensor_reset_request (
        packet,
        packet_size,
        logical_length))
    return goodix5135_sensor_reset_transaction_fail (
      transaction);

  transaction->state =
    GOODIX5135_SENSOR_RESET_TRANSACTION_WAIT_OUT;

  return TRUE;
}


gboolean
goodix5135_sensor_reset_transaction_out_complete (
  Goodix5135SensorResetTransaction *transaction,
  gboolean                          transport_can_advance)
{
  if (transaction == NULL)
    return FALSE;

  if (transaction->state !=
      GOODIX5135_SENSOR_RESET_TRANSACTION_WAIT_OUT)
    return goodix5135_sensor_reset_transaction_fail (
      transaction);

  if (!transport_can_advance)
    return goodix5135_sensor_reset_transaction_fail (
      transaction);

  transaction->state =
    GOODIX5135_SENSOR_RESET_TRANSACTION_WAIT_ACK;

  return TRUE;
}


gboolean
goodix5135_sensor_reset_transaction_ack_complete (
  Goodix5135SensorResetTransaction *transaction,
  gboolean                          transport_can_advance,
  const guint8                     *data,
  gsize                             data_length)
{
  if (transaction == NULL)
    return FALSE;

  if (transaction->state !=
      GOODIX5135_SENSOR_RESET_TRANSACTION_WAIT_ACK)
    return goodix5135_sensor_reset_transaction_fail (
      transaction);

  if (!transport_can_advance)
    return goodix5135_sensor_reset_transaction_fail (
      transaction);

  if (!goodix5135_parse_sensor_reset_ack (
        data,
        data_length))
    return goodix5135_sensor_reset_transaction_fail (
      transaction);

  transaction->state =
    GOODIX5135_SENSOR_RESET_TRANSACTION_WAIT_RESPONSE;

  return TRUE;
}


gboolean
goodix5135_sensor_reset_transaction_response_complete (
  Goodix5135SensorResetTransaction *transaction,
  gboolean                          transport_can_advance,
  const guint8                     *data,
  gsize                             data_length,
  guint16                          *result_number)
{
  if (result_number != NULL)
    *result_number = 0;

  if (transaction == NULL)
    return FALSE;

  if (transaction->state !=
      GOODIX5135_SENSOR_RESET_TRANSACTION_WAIT_RESPONSE)
    return goodix5135_sensor_reset_transaction_fail (
      transaction);

  if (!transport_can_advance)
    return goodix5135_sensor_reset_transaction_fail (
      transaction);

  if (result_number == NULL)
    return goodix5135_sensor_reset_transaction_fail (
      transaction);

  if (!goodix5135_parse_sensor_reset_response (
        data,
        data_length,
        result_number))
    return goodix5135_sensor_reset_transaction_fail (
      transaction);

  transaction->state =
    GOODIX5135_SENSOR_RESET_TRANSACTION_DONE;

  return TRUE;
}


gboolean
goodix5135_build_enable_chip_request (
  gboolean enable,
  guint8  *packet,
  gsize    packet_size,
  gsize   *logical_length)
{
  guint outer_sum;

  if (packet == NULL ||
      logical_length == NULL)
    return FALSE;

  if (packet_size <
      GOODIX5135_USB_PACKET_LENGTH)
    return FALSE;

  memset (
    packet,
    0,
    GOODIX5135_USB_PACKET_LENGTH);

  /*
   * Reference:
   *
   * enable_chip(true):
   *
   *   payload = 01 00
   *
   * Inner:
   *   96 03 00 01 00 10
   *
   * Outer:
   *   a0 06 00 a6
   */
  packet[0] =
    GOODIX5135_PACK_FLAGS_MESSAGE_PROTOCOL;

  packet[1] = 0x06;
  packet[2] = 0x00;

  outer_sum =
    (guint) packet[0] +
    (guint) packet[1] +
    (guint) packet[2];

  packet[3] =
    (guint8) (outer_sum & 0xffU);

  packet[4] =
    GOODIX5135_COMMAND_ENABLE_CHIP;

  packet[5] = 0x03;
  packet[6] = 0x00;

  packet[7] =
    enable ? 0x01U : 0x00U;

  packet[8] = 0x00;

  packet[9] =
    goodix5135_checksum_aa (
      packet + 4,
      5);

  *logical_length =
    GOODIX5135_ENABLE_CHIP_REQUEST_LENGTH;

  return TRUE;
}


gboolean
goodix5135_parse_enable_chip_ack (
  const guint8 *data,
  gsize         data_length)
{
  const guint8 *payload;
  gsize payload_length;
  guint8 status;

  if (!goodix5135_parse_wrapped_protocol (
        data,
        data_length,
        GOODIX5135_COMMAND_ACK,
        &payload,
        &payload_length))
    return FALSE;

  if (payload_length <
      GOODIX5135_ACK_MIN_PAYLOAD_LENGTH)
    return FALSE;

  if (payload[0] !=
      GOODIX5135_COMMAND_ENABLE_CHIP)
    return FALSE;

  status = payload[1];

  if ((status & 0x01U) == 0)
    return FALSE;

  return TRUE;
}


static gboolean
goodix5135_enable_chip_transaction_fail (
  Goodix5135EnableChipTransaction *transaction)
{
  if (transaction != NULL)
    transaction->state =
      GOODIX5135_ENABLE_CHIP_TRANSACTION_FAILED;

  return FALSE;
}


void
goodix5135_enable_chip_transaction_init (
  Goodix5135EnableChipTransaction *transaction)
{
  g_return_if_fail (transaction != NULL);

  transaction->state =
    GOODIX5135_ENABLE_CHIP_TRANSACTION_IDLE;
}


gboolean
goodix5135_enable_chip_transaction_begin (
  Goodix5135EnableChipTransaction *transaction,
  gboolean                         enable,
  guint8                          *packet,
  gsize                            packet_size,
  gsize                           *logical_length)
{
  if (transaction == NULL)
    return FALSE;

  if (transaction->state !=
      GOODIX5135_ENABLE_CHIP_TRANSACTION_IDLE)
    return goodix5135_enable_chip_transaction_fail (
      transaction);

  if (!goodix5135_build_enable_chip_request (
        enable,
        packet,
        packet_size,
        logical_length))
    return goodix5135_enable_chip_transaction_fail (
      transaction);

  transaction->state =
    GOODIX5135_ENABLE_CHIP_TRANSACTION_WAIT_OUT;

  return TRUE;
}


gboolean
goodix5135_enable_chip_transaction_out_complete (
  Goodix5135EnableChipTransaction *transaction,
  gboolean                         transport_can_advance)
{
  if (transaction == NULL)
    return FALSE;

  if (transaction->state !=
      GOODIX5135_ENABLE_CHIP_TRANSACTION_WAIT_OUT)
    return goodix5135_enable_chip_transaction_fail (
      transaction);

  if (!transport_can_advance)
    return goodix5135_enable_chip_transaction_fail (
      transaction);

  transaction->state =
    GOODIX5135_ENABLE_CHIP_TRANSACTION_WAIT_ACK;

  return TRUE;
}


gboolean
goodix5135_enable_chip_transaction_ack_complete (
  Goodix5135EnableChipTransaction *transaction,
  gboolean                         transport_can_advance,
  const guint8                    *data,
  gsize                            data_length)
{
  if (transaction == NULL)
    return FALSE;

  if (transaction->state !=
      GOODIX5135_ENABLE_CHIP_TRANSACTION_WAIT_ACK)
    return goodix5135_enable_chip_transaction_fail (
      transaction);

  if (!transport_can_advance)
    return goodix5135_enable_chip_transaction_fail (
      transaction);

  if (!goodix5135_parse_enable_chip_ack (
        data,
        data_length))
    return goodix5135_enable_chip_transaction_fail (
      transaction);

  transaction->state =
    GOODIX5135_ENABLE_CHIP_TRANSACTION_DONE;

  return TRUE;
}


gboolean
goodix5135_build_d4_request (
  guint8 *packet,
  gsize   packet_size,
  gsize  *logical_length)
{
  guint outer_sum;

  if (packet == NULL ||
      logical_length == NULL)
    return FALSE;

  if (packet_size <
      GOODIX5135_USB_PACKET_LENGTH)
    return FALSE;

  memset (
    packet,
    0,
    GOODIX5135_USB_PACKET_LENGTH);

  /*
   * Public reference:
   *
   * encode_message_protocol(
   *   b"\x00\x00",
   *   COMMAND_TLS_SUCCESSFULLY_ESTABLISHED)
   *
   * Inner:
   *   d4 03 00 00 00 d3
   *
   * Outer:
   *   a0 06 00 a6
   */
  packet[0] =
    GOODIX5135_PACK_FLAGS_MESSAGE_PROTOCOL;

  packet[1] = 0x06;
  packet[2] = 0x00;

  outer_sum =
    (guint) packet[0] +
    (guint) packet[1] +
    (guint) packet[2];

  packet[3] =
    (guint8) (outer_sum & 0xffU);

  packet[4] =
    GOODIX5135_COMMAND_TLS_SUCCESSFULLY_ESTABLISHED;

  packet[5] = 0x03;
  packet[6] = 0x00;

  packet[7] = 0x00;
  packet[8] = 0x00;

  packet[9] =
    goodix5135_checksum_aa (
      packet + 4,
      5);

  *logical_length =
    GOODIX5135_D4_REQUEST_LENGTH;

  return TRUE;
}


gboolean
goodix5135_parse_d4_ack (
  const guint8 *data,
  gsize         data_length)
{
  const guint8 *payload;
  gsize payload_length;
  guint8 status;

  if (!goodix5135_parse_wrapped_protocol (
        data,
        data_length,
        GOODIX5135_COMMAND_ACK,
        &payload,
        &payload_length))
    return FALSE;

  if (payload_length <
      GOODIX5135_ACK_MIN_PAYLOAD_LENGTH)
    return FALSE;

  if (payload[0] !=
      GOODIX5135_COMMAND_TLS_SUCCESSFULLY_ESTABLISHED)
    return FALSE;

  status = payload[1];

  if ((status & 0x01U) == 0)
    return FALSE;

  return TRUE;
}


static gboolean
goodix5135_d4_transaction_fail (
  Goodix5135TlsEstablishedTransaction *transaction)
{
  if (transaction != NULL)
    transaction->state =
      GOODIX5135_D4_TRANSACTION_FAILED;

  return FALSE;
}


void
goodix5135_d4_transaction_init (
  Goodix5135TlsEstablishedTransaction *transaction)
{
  g_return_if_fail (transaction != NULL);

  transaction->state =
    GOODIX5135_D4_TRANSACTION_IDLE;
}


gboolean
goodix5135_d4_transaction_begin (
  Goodix5135TlsEstablishedTransaction *transaction,
  guint8                              *packet,
  gsize                                packet_size,
  gsize                               *logical_length)
{
  if (transaction == NULL)
    return FALSE;

  if (transaction->state !=
      GOODIX5135_D4_TRANSACTION_IDLE)
    return goodix5135_d4_transaction_fail (
      transaction);

  if (!goodix5135_build_d4_request (
        packet,
        packet_size,
        logical_length))
    return goodix5135_d4_transaction_fail (
      transaction);

  transaction->state =
    GOODIX5135_D4_TRANSACTION_WAIT_OUT;

  return TRUE;
}


gboolean
goodix5135_d4_transaction_out_complete (
  Goodix5135TlsEstablishedTransaction *transaction,
  gboolean                             transport_can_advance)
{
  if (transaction == NULL)
    return FALSE;

  if (transaction->state !=
      GOODIX5135_D4_TRANSACTION_WAIT_OUT)
    return goodix5135_d4_transaction_fail (
      transaction);

  if (!transport_can_advance)
    return goodix5135_d4_transaction_fail (
      transaction);

  transaction->state =
    GOODIX5135_D4_TRANSACTION_WAIT_ACK;

  return TRUE;
}


gboolean
goodix5135_d4_transaction_ack_complete (
  Goodix5135TlsEstablishedTransaction *transaction,
  gboolean                             transport_can_advance,
  const guint8                        *data,
  gsize                                data_length)
{
  if (transaction == NULL)
    return FALSE;

  if (transaction->state !=
      GOODIX5135_D4_TRANSACTION_WAIT_ACK)
    return goodix5135_d4_transaction_fail (
      transaction);

  if (!transport_can_advance)
    return goodix5135_d4_transaction_fail (
      transaction);

  if (!goodix5135_parse_d4_ack (
        data,
        data_length))
    return goodix5135_d4_transaction_fail (
      transaction);

  transaction->state =
    GOODIX5135_D4_TRANSACTION_DONE;

  return TRUE;
}


gboolean
goodix5135_build_nop_request (
  guint8 *packet,
  gsize   packet_size,
  gsize  *logical_length)
{
  guint outer_sum;

  if (packet == NULL ||
      logical_length == NULL)
    return FALSE;

  if (packet_size <
      GOODIX5135_USB_PACKET_LENGTH)
    return FALSE;

  memset (
    packet,
    0,
    GOODIX5135_USB_PACKET_LENGTH);

  /*
   * Reference Goodix NOP:
   *
   * outer:
   *   a0 08 00 a8
   *
   * protocol:
   *   00             command NOP
   *   05 00          four payload bytes + trailer
   *   00 00 00 00    payload
   *   88             no-checksum trailer
   */
  packet[0] =
    GOODIX5135_PACK_FLAGS_MESSAGE_PROTOCOL;

  packet[1] = 0x08;
  packet[2] = 0x00;

  outer_sum =
    (guint) packet[0] +
    (guint) packet[1] +
    (guint) packet[2];

  packet[3] =
    (guint8) (outer_sum & 0xffU);

  packet[4] =
    GOODIX5135_COMMAND_NOP;

  packet[5] = 0x05;
  packet[6] = 0x00;

  packet[7] = 0x00;
  packet[8] = 0x00;
  packet[9] = 0x00;
  packet[10] = 0x00;

  packet[11] = 0x88;

  *logical_length =
    GOODIX5135_NOP_REQUEST_LENGTH;

  return TRUE;
}


gboolean
goodix5135_parse_nop_ack (
  const guint8 *data,
  gsize         data_length)
{
  const guint8 *payload;
  gsize payload_length;
  guint8 status;

  if (!goodix5135_parse_wrapped_protocol (
        data,
        data_length,
        GOODIX5135_COMMAND_ACK,
        &payload,
        &payload_length))
    return FALSE;

  if (payload_length <
      GOODIX5135_ACK_MIN_PAYLOAD_LENGTH)
    return FALSE;

  if (payload[0] !=
      GOODIX5135_COMMAND_NOP)
    return FALSE;

  status = payload[1];

  if ((status & 0x01U) == 0)
    return FALSE;

  return TRUE;
}


static gboolean
goodix5135_nop_transaction_fail (
  Goodix5135NopTransaction *transaction)
{
  if (transaction != NULL)
    transaction->state =
      GOODIX5135_NOP_TRANSACTION_FAILED;

  return FALSE;
}


void
goodix5135_nop_transaction_init (
  Goodix5135NopTransaction *transaction)
{
  g_return_if_fail (transaction != NULL);

  transaction->state =
    GOODIX5135_NOP_TRANSACTION_IDLE;
}


gboolean
goodix5135_nop_transaction_begin (
  Goodix5135NopTransaction *transaction,
  guint8                   *packet,
  gsize                     packet_size,
  gsize                    *logical_length)
{
  if (transaction == NULL)
    return FALSE;

  if (transaction->state !=
      GOODIX5135_NOP_TRANSACTION_IDLE)
    return goodix5135_nop_transaction_fail (
      transaction);

  if (!goodix5135_build_nop_request (
        packet,
        packet_size,
        logical_length))
    return goodix5135_nop_transaction_fail (
      transaction);

  transaction->state =
    GOODIX5135_NOP_TRANSACTION_WAIT_OUT;

  return TRUE;
}


gboolean
goodix5135_nop_transaction_out_complete (
  Goodix5135NopTransaction *transaction,
  gboolean                  transport_can_advance)
{
  if (transaction == NULL)
    return FALSE;

  if (transaction->state !=
      GOODIX5135_NOP_TRANSACTION_WAIT_OUT)
    return goodix5135_nop_transaction_fail (
      transaction);

  if (!transport_can_advance)
    return goodix5135_nop_transaction_fail (
      transaction);

  transaction->state =
    GOODIX5135_NOP_TRANSACTION_WAIT_OPTIONAL_ACK;

  return TRUE;
}


gboolean
goodix5135_nop_transaction_reply_complete (
  Goodix5135NopTransaction *transaction,
  Goodix5135NopReplyResult  result,
  const guint8             *data,
  gsize                     data_length)
{
  if (transaction == NULL)
    return FALSE;

  if (transaction->state !=
      GOODIX5135_NOP_TRANSACTION_WAIT_OPTIONAL_ACK)
    return goodix5135_nop_transaction_fail (
      transaction);

  switch (result)
    {
    case GOODIX5135_NOP_REPLY_TIMEOUT:
      /*
       * The public reference treats the short NOP receive timeout
       * as successful completion.
       */
      transaction->state =
        GOODIX5135_NOP_TRANSACTION_DONE;
      return TRUE;

    case GOODIX5135_NOP_REPLY_RECEIVED:
      if (!goodix5135_parse_nop_ack (
            data,
            data_length))
        return goodix5135_nop_transaction_fail (
          transaction);

      transaction->state =
        GOODIX5135_NOP_TRANSACTION_DONE;
      return TRUE;

    case GOODIX5135_NOP_REPLY_TRANSPORT_FAILURE:
      return goodix5135_nop_transaction_fail (
        transaction);

    default:
      return goodix5135_nop_transaction_fail (
        transaction);
    }
}


gboolean
goodix5135_build_mcu_state_request (
  guint8  query,
  guint8 *packet,
  gsize   packet_size,
  gsize  *logical_length)
{
  guint outer_sum;

  if (packet == NULL || logical_length == NULL)
    return FALSE;

  if (packet_size < GOODIX5135_USB_PACKET_LENGTH)
    return FALSE;

  /*
   * Reference request for query byte 0x55:
   *
   * outer:
   *   a0 05 00 a5
   *
   * protocol:
   *   ae 02 00 <query> <checksum>
   */
  memset (packet, 0, GOODIX5135_USB_PACKET_LENGTH);

  packet[0] =
    GOODIX5135_PACK_FLAGS_MESSAGE_PROTOCOL;

  packet[1] = 0x05;
  packet[2] = 0x00;

  outer_sum =
    (guint) packet[0] +
    (guint) packet[1] +
    (guint) packet[2];

  packet[3] =
    (guint8) (outer_sum & 0xffU);

  packet[4] =
    GOODIX5135_COMMAND_QUERY_MCU_STATE;

  /*
   * Declared length =
   *   one payload byte + checksum.
   */
  packet[5] = 0x02;
  packet[6] = 0x00;

  packet[7] = query;

  packet[8] =
    goodix5135_checksum_aa (
      packet + 4,
      4);

  *logical_length =
    GOODIX5135_MCU_STATE_REQUEST_LENGTH;

  return TRUE;
}


gboolean
goodix5135_parse_mcu_state_ack (
  const guint8 *data,
  gsize         data_length)
{
  const guint8 *payload;
  gsize payload_length;
  guint8 status;

  if (!goodix5135_parse_wrapped_protocol (
        data,
        data_length,
        GOODIX5135_COMMAND_ACK,
        &payload,
        &payload_length))
    return FALSE;

  if (payload_length <
      GOODIX5135_ACK_MIN_PAYLOAD_LENGTH)
    return FALSE;

  if (payload[0] !=
      GOODIX5135_COMMAND_QUERY_MCU_STATE)
    return FALSE;

  status = payload[1];

  /*
   * Same ACK semantics already proven for 0xa8 and 0x82:
   * bit 0 must indicate a valid ACK structure.
   */
  if ((status & 0x01U) == 0)
    return FALSE;

  return TRUE;
}


gboolean
goodix5135_parse_mcu_state_response (
  const guint8  *data,
  gsize          data_length,
  const guint8 **state_data,
  gsize         *state_length)
{
  const guint8 *payload;
  gsize payload_length;

  if (state_data == NULL ||
      state_length == NULL)
    return FALSE;

  *state_data = NULL;
  *state_length = 0;

  if (!goodix5135_parse_wrapped_protocol (
        data,
        data_length,
        GOODIX5135_COMMAND_QUERY_MCU_STATE,
        &payload,
        &payload_length))
    return FALSE;

  if (payload_length == 0)
    return FALSE;

  *state_data = payload;
  *state_length = payload_length;

  return TRUE;
}


static gboolean
goodix5135_mcu_state_transaction_fail (
  Goodix5135McuStateTransaction *transaction)
{
  if (transaction != NULL)
    transaction->state =
      GOODIX5135_MCU_STATE_TRANSACTION_FAILED;

  return FALSE;
}


void
goodix5135_mcu_state_transaction_init (
  Goodix5135McuStateTransaction *transaction)
{
  g_return_if_fail (transaction != NULL);

  transaction->state =
    GOODIX5135_MCU_STATE_TRANSACTION_IDLE;
}


gboolean
goodix5135_mcu_state_transaction_begin (
  Goodix5135McuStateTransaction *transaction,
  guint8                         query,
  guint8                        *packet,
  gsize                          packet_size,
  gsize                         *logical_length)
{
  if (transaction == NULL)
    return FALSE;

  if (transaction->state !=
      GOODIX5135_MCU_STATE_TRANSACTION_IDLE)
    return goodix5135_mcu_state_transaction_fail (
      transaction);

  if (!goodix5135_build_mcu_state_request (
        query,
        packet,
        packet_size,
        logical_length))
    return goodix5135_mcu_state_transaction_fail (
      transaction);

  transaction->state =
    GOODIX5135_MCU_STATE_TRANSACTION_WAIT_OUT;

  return TRUE;
}


gboolean
goodix5135_mcu_state_transaction_out_complete (
  Goodix5135McuStateTransaction *transaction,
  gboolean                       transport_can_advance)
{
  if (transaction == NULL)
    return FALSE;

  if (transaction->state !=
      GOODIX5135_MCU_STATE_TRANSACTION_WAIT_OUT)
    return goodix5135_mcu_state_transaction_fail (
      transaction);

  if (!transport_can_advance)
    return goodix5135_mcu_state_transaction_fail (
      transaction);

  transaction->state =
    GOODIX5135_MCU_STATE_TRANSACTION_WAIT_ACK;

  return TRUE;
}


gboolean
goodix5135_mcu_state_transaction_ack_complete (
  Goodix5135McuStateTransaction *transaction,
  gboolean                       transport_can_advance,
  const guint8                  *data,
  gsize                          data_length)
{
  if (transaction == NULL)
    return FALSE;

  if (transaction->state !=
      GOODIX5135_MCU_STATE_TRANSACTION_WAIT_ACK)
    return goodix5135_mcu_state_transaction_fail (
      transaction);

  if (!transport_can_advance)
    return goodix5135_mcu_state_transaction_fail (
      transaction);

  if (!goodix5135_parse_mcu_state_ack (
        data,
        data_length))
    return goodix5135_mcu_state_transaction_fail (
      transaction);

  transaction->state =
    GOODIX5135_MCU_STATE_TRANSACTION_WAIT_RESPONSE;

  return TRUE;
}


gboolean
goodix5135_mcu_state_transaction_response_complete (
  Goodix5135McuStateTransaction *transaction,
  gboolean                       transport_can_advance,
  const guint8                  *data,
  gsize                          data_length,
  const guint8                 **state_data,
  gsize                         *state_length)
{
  if (state_data != NULL)
    *state_data = NULL;

  if (state_length != NULL)
    *state_length = 0;

  if (transaction == NULL)
    return FALSE;

  if (transaction->state !=
      GOODIX5135_MCU_STATE_TRANSACTION_WAIT_RESPONSE)
    return goodix5135_mcu_state_transaction_fail (
      transaction);

  if (!transport_can_advance)
    return goodix5135_mcu_state_transaction_fail (
      transaction);

  if (state_data == NULL ||
      state_length == NULL)
    return goodix5135_mcu_state_transaction_fail (
      transaction);

  if (!goodix5135_parse_mcu_state_response (
        data,
        data_length,
        state_data,
        state_length))
    return goodix5135_mcu_state_transaction_fail (
      transaction);

  transaction->state =
    GOODIX5135_MCU_STATE_TRANSACTION_DONE;

  return TRUE;
}


static gboolean
goodix5135_firmware_transaction_fail (
  Goodix5135FirmwareTransaction *transaction)
{
  if (transaction != NULL)
    transaction->state =
      GOODIX5135_FIRMWARE_TRANSACTION_FAILED;

  return FALSE;
}

void
goodix5135_firmware_transaction_init (
  Goodix5135FirmwareTransaction *transaction)
{
  g_return_if_fail (transaction != NULL);

  transaction->state =
    GOODIX5135_FIRMWARE_TRANSACTION_IDLE;
}

gboolean
goodix5135_firmware_transaction_begin (
  Goodix5135FirmwareTransaction *transaction,
  guint8                        *packet,
  gsize                          packet_size,
  gsize                         *logical_length)
{
  if (transaction == NULL)
    return FALSE;

  if (transaction->state !=
      GOODIX5135_FIRMWARE_TRANSACTION_IDLE)
    return goodix5135_firmware_transaction_fail (
      transaction);

  if (!goodix5135_build_firmware_version_request (
        packet,
        packet_size,
        logical_length))
    return goodix5135_firmware_transaction_fail (
      transaction);

  transaction->state =
    GOODIX5135_FIRMWARE_TRANSACTION_WAIT_OUT;

  return TRUE;
}

gboolean
goodix5135_firmware_transaction_out_complete (
  Goodix5135FirmwareTransaction *transaction,
  gboolean                       transport_can_advance)
{
  if (transaction == NULL)
    return FALSE;

  if (transaction->state !=
      GOODIX5135_FIRMWARE_TRANSACTION_WAIT_OUT)
    return goodix5135_firmware_transaction_fail (
      transaction);

  if (!transport_can_advance)
    return goodix5135_firmware_transaction_fail (
      transaction);

  transaction->state =
    GOODIX5135_FIRMWARE_TRANSACTION_WAIT_ACK;

  return TRUE;
}

gboolean
goodix5135_firmware_transaction_ack_complete (
  Goodix5135FirmwareTransaction *transaction,
  gboolean                       transport_can_advance,
  const guint8                  *data,
  gsize                          data_length)
{
  if (transaction == NULL)
    return FALSE;

  if (transaction->state !=
      GOODIX5135_FIRMWARE_TRANSACTION_WAIT_ACK)
    return goodix5135_firmware_transaction_fail (
      transaction);

  if (!transport_can_advance)
    return goodix5135_firmware_transaction_fail (
      transaction);

  if (!goodix5135_parse_firmware_version_ack (
        data,
        data_length))
    return goodix5135_firmware_transaction_fail (
      transaction);

  transaction->state =
    GOODIX5135_FIRMWARE_TRANSACTION_WAIT_RESPONSE;

  return TRUE;
}

gboolean
goodix5135_firmware_transaction_response_complete (
  Goodix5135FirmwareTransaction *transaction,
  gboolean                       transport_can_advance,
  const guint8                  *data,
  gsize                          data_length,
  const guint8                 **firmware,
  gsize                         *firmware_length)
{
  if (firmware != NULL)
    *firmware = NULL;

  if (firmware_length != NULL)
    *firmware_length = 0;

  if (transaction == NULL)
    return FALSE;

  if (transaction->state !=
      GOODIX5135_FIRMWARE_TRANSACTION_WAIT_RESPONSE)
    return goodix5135_firmware_transaction_fail (
      transaction);

  if (!transport_can_advance)
    return goodix5135_firmware_transaction_fail (
      transaction);

  if (firmware == NULL || firmware_length == NULL)
    return goodix5135_firmware_transaction_fail (
      transaction);

  if (!goodix5135_parse_firmware_version_response (
        data,
        data_length,
        firmware,
        firmware_length))
    return goodix5135_firmware_transaction_fail (
      transaction);

  transaction->state =
    GOODIX5135_FIRMWARE_TRANSACTION_DONE;

  return TRUE;
}

gboolean
goodix5135_parse_image_frame (const guint8        *data,
                              gsize                data_length,
                              Goodix5135ImageFrame *frame)
{
  guint16 declared_length;
  const guint8 *payload;

  if (data == NULL || frame == NULL)
    return FALSE;

  if (data_length != GOODIX5135_IMAGE_TOTAL_LENGTH)
    return FALSE;

  if (data[0] != GOODIX5135_IMAGE_COMMAND)
    return FALSE;

  declared_length =
    ((guint16) data[1]) |
    ((guint16) data[2] << 8);

  if (declared_length != GOODIX5135_IMAGE_DECLARED_LENGTH)
    return FALSE;

  if (data[data_length - 1] != GOODIX5135_PROTOCOL_TRAILER)
    return FALSE;

  /*
   * Layout:
   *
   * data[0]      command
   * data[1..2]   little-endian declared length
   * data[3..]    protocol payload
   *
   * payload:
   *   5 bytes metadata
   *   7680 bytes packed RAW12
   *   4 bytes stored image CRC
   */
  payload = data + 3;

  frame->metadata = payload;
  frame->metadata_length = GOODIX5135_IMAGE_METADATA_LENGTH;

  frame->packed_raw12 =
    payload + GOODIX5135_IMAGE_METADATA_LENGTH;
  frame->packed_raw12_length =
    GOODIX5135_IMAGE_PACKED_LENGTH;

  frame->stored_crc =
    frame->packed_raw12 + GOODIX5135_IMAGE_PACKED_LENGTH;
  frame->stored_crc_length =
    GOODIX5135_IMAGE_STORED_CRC_LENGTH;

  /*
   * Final structural guard:
   *
   * CRC field must end exactly where the one-byte trailer begins.
   */
  if (frame->stored_crc + frame->stored_crc_length !=
      data + data_length - 1)
    return FALSE;

  return TRUE;
}

gboolean
goodix5135_prepare_config_upload (
    const guint8                      *template_data,
    gsize                              template_length,
    const Goodix5135OtpCalibration    *calibration,
    guint8                            *runtime_config,
    gsize                              runtime_config_size,
    Goodix5135ConfigUploadTransaction *transaction,
    guint8                            *transfer,
    gsize                              transfer_size,
    gsize                             *logical_length,
    gsize                             *transport_length)
{
  if (template_data == NULL ||
      calibration == NULL ||
      runtime_config == NULL ||
      transaction == NULL ||
      transfer == NULL ||
      logical_length == NULL ||
      transport_length == NULL)
    return FALSE;

  if (runtime_config_size < GOODIX5135_CONFIG_LENGTH ||
      transfer_size < GOODIX5135_CONFIG_TRANSFER_LENGTH)
    return FALSE;

  if (!goodix5135_build_runtime_config (
          template_data,
          template_length,
          calibration,
          runtime_config,
          runtime_config_size))
    return FALSE;

  goodix5135_config_upload_transaction_init (transaction);

  if (!goodix5135_config_upload_transaction_begin (
          transaction,
          runtime_config,
          GOODIX5135_CONFIG_LENGTH,
          transfer,
          transfer_size,
          logical_length,
          transport_length))
    return FALSE;

  return TRUE;
}
