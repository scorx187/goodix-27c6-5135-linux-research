/*
 * Goodix 27c6:5135 ChicagoHU host-side image helpers.
 *
 * Pure host-side transformations only.
 * No USB, TLS, device configuration, or biometric logging.
 */

#pragma once

#include <glib.h>

#include "goodix5135.h"

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
