/*
 * Goodix 27c6:5135 ChicagoHU protocol helpers.
 *
 * Host-side parsing only.
 */

#include "goodix5135-proto.h"

const char *
goodix5135_proto_stage_name (void)
{
  return "host-only protocol parser";
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
