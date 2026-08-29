#include "goodix5135-tls-request.h"

#include "goodix5135-proto.h"

#include <string.h>

#define GOODIX5135_TLS_REQUEST_OUTER_LENGTH 6U
#define GOODIX5135_TLS_REQUEST_INNER_LENGTH 3U

static guint8
goodix5135_tls_request_outer_checksum (guint8  flags,
                                      guint16 length)
{
  guint value;

  value = flags;
  value += length & 0xffU;
  value += (length >> 8) & 0xffU;

  return (guint8) (value & 0xffU);
}

static guint8
goodix5135_tls_request_protocol_checksum (const guint8 *data,
                                         gsize         length)
{
  guint sum = 0;
  gsize index;

  for (index = 0; index < length; index++)
    sum += data[index];

  return (guint8) ((0xaaU - (sum & 0xffU)) & 0xffU);
}

gboolean
goodix5135_tls_request_build (guint8 *packet,
                              gsize   packet_size,
                              gsize  *logical_length)
{
  g_return_val_if_fail (packet != NULL, FALSE);
  g_return_val_if_fail (logical_length != NULL, FALSE);

  if (packet_size < GOODIX5135_TLS_REQUEST_USB_LENGTH)
    return FALSE;

  memset (packet,
          0,
          packet_size);

  /*
   * Public Goodix request_tls_connection() contract:
   *
   *   outer flags: 0xa0
   *   command:     0xd0
   *   payload:     00 00
   *
   * Logical frame:
   *
   *   a0 06 00 a6
   *   d0 03 00 00 00 d7
   *
   * USB transfer is one zero-padded 64-byte packet.
   */
  packet[0] = GOODIX5135_PACK_FLAGS_MESSAGE_PROTOCOL;
  packet[1] = GOODIX5135_TLS_REQUEST_OUTER_LENGTH & 0xffU;
  packet[2] = (GOODIX5135_TLS_REQUEST_OUTER_LENGTH >> 8) & 0xffU;

  packet[3] =
    goodix5135_tls_request_outer_checksum (
      packet[0],
      GOODIX5135_TLS_REQUEST_OUTER_LENGTH);

  packet[4] = GOODIX5135_TLS_REQUEST_COMMAND;
  packet[5] = GOODIX5135_TLS_REQUEST_INNER_LENGTH & 0xffU;
  packet[6] = (GOODIX5135_TLS_REQUEST_INNER_LENGTH >> 8) & 0xffU;

  packet[7] = 0x00U;
  packet[8] = 0x00U;

  packet[9] =
    goodix5135_tls_request_protocol_checksum (
      packet + 4,
      5);

  *logical_length =
    GOODIX5135_TLS_REQUEST_LOGICAL_LENGTH;

  return TRUE;
}

gboolean
goodix5135_tls_request_parse_ack (const guint8 *data,
                                  gsize         data_length)
{
  guint16 outer_length;
  guint16 inner_length;
  guint8  expected_outer_checksum;
  guint8  expected_inner_checksum;
  guint8  ack_status;

  if (data == NULL ||
      data_length < GOODIX5135_TLS_REQUEST_LOGICAL_LENGTH)
    return FALSE;

  if (data[0] != GOODIX5135_PACK_FLAGS_MESSAGE_PROTOCOL)
    return FALSE;

  outer_length =
    ((guint16) data[1]) |
    ((guint16) data[2] << 8);

  if (outer_length != GOODIX5135_TLS_REQUEST_OUTER_LENGTH)
    return FALSE;

  expected_outer_checksum =
    goodix5135_tls_request_outer_checksum (
      data[0],
      outer_length);

  if (data[3] != expected_outer_checksum)
    return FALSE;

  if (data[4] != GOODIX5135_COMMAND_ACK)
    return FALSE;

  inner_length =
    ((guint16) data[5]) |
    ((guint16) data[6] << 8);

  if (inner_length != GOODIX5135_TLS_REQUEST_INNER_LENGTH)
    return FALSE;

  if (data[7] != GOODIX5135_TLS_REQUEST_COMMAND)
    return FALSE;

  ack_status = data[8];

  /*
   * Public Goodix decode_ack() requires bit 0.
   * Bit 1 is a separate configuration-state flag and is not interpreted here.
   */
  if ((ack_status & 0x01U) == 0)
    return FALSE;

  expected_inner_checksum =
    goodix5135_tls_request_protocol_checksum (
      data + 4,
      5);

  if (data[9] != expected_inner_checksum)
    return FALSE;

  return TRUE;
}

void
goodix5135_tls_request_transaction_init (
  Goodix5135TlsRequestTransaction *transaction)
{
  g_return_if_fail (transaction != NULL);

  transaction->state =
    GOODIX5135_TLS_REQUEST_TRANSACTION_IDLE;
}

gboolean
goodix5135_tls_request_transaction_begin (
  Goodix5135TlsRequestTransaction *transaction,
  guint8                          *packet,
  gsize                            packet_size,
  gsize                           *logical_length)
{
  g_return_val_if_fail (transaction != NULL, FALSE);

  if (transaction->state !=
      GOODIX5135_TLS_REQUEST_TRANSACTION_IDLE)
    {
      transaction->state =
        GOODIX5135_TLS_REQUEST_TRANSACTION_FAILED;

      return FALSE;
    }

  if (!goodix5135_tls_request_build (
        packet,
        packet_size,
        logical_length))
    {
      transaction->state =
        GOODIX5135_TLS_REQUEST_TRANSACTION_FAILED;

      return FALSE;
    }

  transaction->state =
    GOODIX5135_TLS_REQUEST_TRANSACTION_WAIT_OUT;

  return TRUE;
}

gboolean
goodix5135_tls_request_transaction_out_complete (
  Goodix5135TlsRequestTransaction *transaction,
  gboolean                         transport_can_advance)
{
  g_return_val_if_fail (transaction != NULL, FALSE);

  if (transaction->state !=
      GOODIX5135_TLS_REQUEST_TRANSACTION_WAIT_OUT)
    {
      transaction->state =
        GOODIX5135_TLS_REQUEST_TRANSACTION_FAILED;

      return FALSE;
    }

  if (!transport_can_advance)
    {
      transaction->state =
        GOODIX5135_TLS_REQUEST_TRANSACTION_FAILED;

      return FALSE;
    }

  transaction->state =
    GOODIX5135_TLS_REQUEST_TRANSACTION_WAIT_ACK;

  return TRUE;
}

gboolean
goodix5135_tls_request_transaction_ack_complete (
  Goodix5135TlsRequestTransaction *transaction,
  gboolean                         transport_can_advance,
  const guint8                    *data,
  gsize                            data_length)
{
  g_return_val_if_fail (transaction != NULL, FALSE);

  if (transaction->state !=
      GOODIX5135_TLS_REQUEST_TRANSACTION_WAIT_ACK)
    {
      transaction->state =
        GOODIX5135_TLS_REQUEST_TRANSACTION_FAILED;

      return FALSE;
    }

  if (!transport_can_advance ||
      !goodix5135_tls_request_parse_ack (
        data,
        data_length))
    {
      transaction->state =
        GOODIX5135_TLS_REQUEST_TRANSACTION_FAILED;

      return FALSE;
    }

  transaction->state =
    GOODIX5135_TLS_REQUEST_TRANSACTION_DONE;

  return TRUE;
}
