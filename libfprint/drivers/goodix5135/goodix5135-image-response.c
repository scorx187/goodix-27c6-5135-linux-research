/*
 * Goodix 27c6:5135 ChicagoHU host-side image response decoder.
 */

#include "goodix5135-image-response.h"

#include "goodix5135-image.h"
#include "goodix5135-proto.h"

gboolean
goodix5135_decode_image_response (const guint8 *data,
                                  gsize         data_length,
                                  guint16      *output,
                                  gsize         output_pixels)
{
  Goodix5135ImageFrame frame = { 0 };
  guint16 transport_pixels[GOODIX5135_IMAGE_PIXELS];
  guint32 expected_crc;
  guint32 actual_crc;

  if (data == NULL || output == NULL)
    return FALSE;

  if (output_pixels < GOODIX5135_IMAGE_PIXELS)
    return FALSE;

  if (!goodix5135_parse_image_frame (data,
                                     data_length,
                                     &frame))
    return FALSE;

  if (!goodix5135_crc32_from_stored (frame.stored_crc,
                                     frame.stored_crc_length,
                                     &expected_crc))
    return FALSE;

  actual_crc = goodix5135_crc32_mpeg2 (frame.packed_raw12,
                                       frame.packed_raw12_length);

  if (actual_crc != expected_crc)
    return FALSE;

  if (!goodix5135_decode_raw12 (frame.packed_raw12,
                                frame.packed_raw12_length,
                                transport_pixels,
                                G_N_ELEMENTS (transport_pixels)))
    return FALSE;

  if (!goodix5135_chicagohu_regroup_u16 (
        transport_pixels,
        G_N_ELEMENTS (transport_pixels),
        output,
        output_pixels))
    return FALSE;

  return TRUE;
}
