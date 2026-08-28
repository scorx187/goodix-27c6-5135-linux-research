/*
 * Goodix 27c6:5135 ChicagoHU host-side image response decoder.
 *
 * This layer performs no USB or TLS operation.
 */

#pragma once

#include <glib.h>

#include "goodix5135.h"

/*
 * Decode one fully decrypted command-0x20 response into the proven
 * 80x64 ChicagoHU downstream U16 plane.
 *
 * Processing:
 *   framing -> CRC -> RAW12 -> ChicagoHU regroup
 */
gboolean goodix5135_decode_image_response (const guint8 *data,
                                          gsize         data_length,
                                          guint16      *output,
                                          gsize         output_pixels);
