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
