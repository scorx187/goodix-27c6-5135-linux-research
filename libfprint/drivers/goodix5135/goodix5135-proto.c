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
