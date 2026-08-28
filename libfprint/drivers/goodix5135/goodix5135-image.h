/*
 * Goodix 27c6:5135 ChicagoHU host-side image helpers.
 *
 * Pure host-side transformations only.
 * No USB, TLS, device configuration, or biometric logging.
 */

#pragma once

#include <glib.h>

#include "goodix5135.h"

#define GOODIX5135_RAW12_PACKED_BYTES \
  ((GOODIX5135_IMAGE_PIXELS * 12) / 8)

#define GOODIX5135_IMAGE_CRC_BYTES 4

/*
 * ChicagoHU transport/source plane:
 *
 *   64 fast-axis samples x 80 slow-axis samples
 *
 * Downstream plane:
 *
 *   80 columns x 64 rows
 *
 * Proven mapping:
 *
 *   dst[(n % 64) * 80 + (n / 64)] = src[n]
 */
gboolean goodix5135_chicagohu_regroup_u16 (const guint16 *src,
                                          gsize          src_pixels,
                                          guint16       *dst,
                                          gsize          dst_pixels);

/*
 * Goodix 5135 packed RAW12 decode.
 *
 * Every six input bytes produce four 12-bit samples:
 *
 * p0 = ((b0 & 0x0f) << 8) | b1
 * p1 = (b3 << 4) | (b0 >> 4)
 * p2 = ((b5 & 0x0f) << 8) | b2
 * p3 = (b4 << 4) | (b5 >> 4)
 *
 * The output remains in transport order. ChicagoHU regrouping is a
 * separate explicit step.
 */
gboolean goodix5135_decode_raw12 (const guint8 *packed,
                                  gsize         packed_length,
                                  guint16      *pixels,
                                  gsize         pixel_count);

/*
 * CRC-32/MPEG-2:
 *
 * polynomial : 0x04C11DB7
 * init       : 0xFFFFFFFF
 * refin      : false
 * refout     : false
 * xorout     : 0x00000000
 */
guint32 goodix5135_crc32_mpeg2 (const guint8 *data,
                                gsize         length);

/*
 * Windows-compatible interpretation of the four-byte CRC field.
 *
 * Stored bytes:
 *
 *   [a, b, c, d]
 *
 * become:
 *
 *   c<<24 | d<<16 | a<<8 | b
 *
 * i.e. the two 16-bit halves are swapped.
 */
gboolean goodix5135_crc32_from_stored (const guint8 *stored,
                                      gsize         stored_length,
                                      guint32      *crc);
