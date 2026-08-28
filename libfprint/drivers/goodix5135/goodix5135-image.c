/*
 * Goodix 27c6:5135 ChicagoHU host-side image helpers.
 */

#include "goodix5135-image.h"

#define GOODIX5135_CRC32_MPEG2_POLY 0x04c11db7U
#define GOODIX5135_CRC32_MPEG2_INIT 0xffffffffU

guint32
goodix5135_crc32_mpeg2 (const guint8 *data,
                        gsize         length)
{
  guint32 crc = GOODIX5135_CRC32_MPEG2_INIT;
  gsize i;

  if (length != 0 && data == NULL)
    return 0;

  for (i = 0; i < length; i++)
    {
      guint bit;

      crc ^= ((guint32) data[i]) << 24;

      for (bit = 0; bit < 8; bit++)
        {
          if (crc & 0x80000000U)
            crc = (crc << 1) ^ GOODIX5135_CRC32_MPEG2_POLY;
          else
            crc <<= 1;
        }
    }

  return crc;
}

gboolean
goodix5135_crc32_from_stored (const guint8 *stored,
                              gsize         stored_length,
                              guint32      *crc)
{
  if (stored == NULL || crc == NULL)
    return FALSE;

  if (stored_length != GOODIX5135_IMAGE_CRC_BYTES)
    return FALSE;

  *crc = ((guint32) stored[2] << 24) |
         ((guint32) stored[3] << 16) |
         ((guint32) stored[0] << 8) |
         ((guint32) stored[1]);

  return TRUE;
}

gboolean
goodix5135_decode_raw12 (const guint8 *packed,
                         gsize         packed_length,
                         guint16      *pixels,
                         gsize         pixel_count)
{
  gsize input_offset;
  gsize output_offset = 0;

  if (packed == NULL || pixels == NULL)
    return FALSE;

  if (packed_length != GOODIX5135_RAW12_PACKED_BYTES)
    return FALSE;

  if (pixel_count < GOODIX5135_IMAGE_PIXELS)
    return FALSE;

  if ((packed_length % 6) != 0)
    return FALSE;

  for (input_offset = 0;
       input_offset < packed_length;
       input_offset += 6)
    {
      const guint8 *c = packed + input_offset;

      pixels[output_offset++] =
        ((guint16) (c[0] & 0x0f) << 8) |
        (guint16) c[1];

      pixels[output_offset++] =
        ((guint16) c[3] << 4) |
        ((guint16) c[0] >> 4);

      pixels[output_offset++] =
        ((guint16) (c[5] & 0x0f) << 8) |
        (guint16) c[2];

      pixels[output_offset++] =
        ((guint16) c[4] << 4) |
        ((guint16) c[5] >> 4);
    }

  return output_offset == GOODIX5135_IMAGE_PIXELS;
}

gboolean
goodix5135_chicagohu_regroup_u16 (const guint16 *src,
                                  gsize          src_pixels,
                                  guint16       *dst,
                                  gsize          dst_pixels)
{
  gsize n;

  if (src == NULL || dst == NULL)
    return FALSE;

  if (src == dst)
    return FALSE;

  if (src_pixels < GOODIX5135_IMAGE_PIXELS ||
      dst_pixels < GOODIX5135_IMAGE_PIXELS)
    return FALSE;

  for (n = 0; n < GOODIX5135_IMAGE_PIXELS; n++)
    {
      const gsize row = n % 64;
      const gsize col = n / 64;
      const gsize dst_index = row * GOODIX5135_IMAGE_WIDTH + col;

      dst[dst_index] = src[n];
    }

  return TRUE;
}
