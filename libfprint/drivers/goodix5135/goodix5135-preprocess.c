/*
 * Host-side preprocessing primitives for Goodix 27c6:5135 ChicagoHU.
 *
 * No USB/device access is performed here.
 *
 * This file deliberately does not attempt to reconstruct missing
 * device-/session-specific preprocessing state.
 */

#include "goodix5135-preprocess.h"

static const guint32 goodix5135_gaussian_q16[5] = {
  7869U,
  15328U,
  19142U,
  15328U,
  7869U,
};


gint
goodix5135_preprocess_reflect101 (
  gint coordinate,
  gint extent)
{
  gint64 x;
  const gint64 n = extent;

  if (extent <= 1)
    return 0;

  x = coordinate;

  while (x < 0 ||
         x >= n)
    {
      if (x < 0)
        x = -x;
      else
        x =
          (2 * n) -
          2 -
          x;
    }

  return (gint) x;
}


guint32
goodix5135_preprocess_q13_mul (
  guint16 a,
  guint16 b)
{
  const guint64 product =
    (guint64) a *
    (guint64) b;

  return (guint32) (
    (product + 0x1000U) >> 13
  );
}


guint16
goodix5135_preprocess_state_minus_source_u16 (
  guint16 state_value,
  guint16 source_value)
{
  return (guint16) (
    state_value -
    source_value
  );
}


gboolean
goodix5135_preprocess_local_contrast_map_u8 (
  guint16  source,
  guint16  low,
  guint16  high,
  guint8  *output)
{
  gint64 ratio;

  if (output == NULL)
    return FALSE;

  if (high < low)
    return FALSE;

  if (high == low)
    {
      ratio = 255;
    }
  else
    {
      const gint64 numerator =
        (
          (gint64) source -
          (gint64) low
        ) * 255;

      const gint64 denominator =
        (gint64) high -
        (gint64) low;

      ratio =
        numerator /
        denominator;

      ratio =
        CLAMP (
          ratio,
          (gint64) 0,
          (gint64) 255);
    }

  *output =
    (guint8) (
      255 -
      ratio
    );

  return TRUE;
}


gboolean
goodix5135_preprocess_gaussian_mode9_u16 (
  const guint16 *source,
  gsize          source_pixels,
  guint          width,
  guint          height,
  guint16       *output,
  gsize          output_pixels)
{
  guint32 *horizontal;
  gsize pixel_count;

  if (source == NULL ||
      output == NULL ||
      width == 0 ||
      height == 0)
    return FALSE;

  if ((gsize) width >
      G_MAXSIZE / (gsize) height)
    return FALSE;

  pixel_count =
    (gsize) width *
    (gsize) height;

  if (source_pixels < pixel_count ||
      output_pixels < pixel_count)
    return FALSE;

  horizontal =
    g_try_new0 (
      guint32,
      pixel_count);

  if (horizontal == NULL)
    return FALSE;

  /*
   * Proven mode-9 horizontal pass:
   *
   *   kernel =
   *     [7869,15328,19142,15328,7869]
   *
   *   Q16 sum -> >>16 truncation
   *   REFLECT_101 border
   */
  for (guint y = 0;
       y < height;
       y++)
    {
      for (guint x = 0;
           x < width;
           x++)
        {
          guint64 accumulator = 0;

          for (gint k = 0;
               k < 5;
               k++)
            {
              const gint source_x =
                goodix5135_preprocess_reflect101 (
                  (gint) x +
                  k -
                  2,
                  (gint) width);

              const guint16 sample =
                source[
                  (gsize) y *
                  width +
                  (guint) source_x
                ];

              accumulator +=
                (guint64)
                goodix5135_gaussian_q16[k] *
                sample;
            }

          horizontal[
            (gsize) y *
            width +
            x
          ] =
            (guint32) (
              accumulator >> 16
            );
        }
    }

  /*
   * Proven mode-9 vertical pass:
   *
   *   same Q16 kernel
   *   bias = 0
   *   shift = 16
   *   REFLECT_101 border
   */
  for (guint y = 0;
       y < height;
       y++)
    {
      for (guint x = 0;
           x < width;
           x++)
        {
          guint64 accumulator = 0;
          guint64 result;

          for (gint k = 0;
               k < 5;
               k++)
            {
              const gint source_y =
                goodix5135_preprocess_reflect101 (
                  (gint) y +
                  k -
                  2,
                  (gint) height);

              const guint32 sample =
                horizontal[
                  (gsize) source_y *
                  width +
                  x
                ];

              accumulator +=
                (guint64)
                goodix5135_gaussian_q16[k] *
                sample;
            }

          result =
            accumulator >> 16;

          if (result > G_MAXUINT16)
            {
              g_free (horizontal);

              return FALSE;
            }

          output[
            (gsize) y *
            width +
            x
          ] =
            (guint16) result;
        }
    }

  g_free (horizontal);

  return TRUE;
}
