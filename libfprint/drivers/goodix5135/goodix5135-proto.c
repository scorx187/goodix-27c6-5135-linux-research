/*
 * Goodix 27c6:5135 ChicagoHU protocol helpers.
 *
 * Host-side parsing only.
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
