/*
 * Diagnostic conditioning variants for Goodix 27c6:5135.
 *
 * Host-side only at this checkpoint.
 *
 * IMPORTANT:
 * These transformations are hypotheses for determining which image
 * characteristics NBIS requires from the decoded ChicagoHU 80x64 plane.
 * They are not claimed to reproduce Windows preprocessing.
 */

#include "goodix5135-conditioning.h"
#include "goodix5135-preprocess.h"

#define GOODIX5135_DIAG_RAW12_MAX 0x0fffU


static guint16
clamp_raw12 (
  guint16 value)
{
  return MIN (
    value,
    (guint16) GOODIX5135_DIAG_RAW12_MAX);
}


static gboolean
condition_shift4 (
  const guint16 *source,
  gsize          pixel_count,
  gboolean       invert,
  guint8        *output)
{
  for (gsize i = 0;
       i < pixel_count;
       i++)
    {
      guint8 value =
        (guint8) (
          clamp_raw12 (source[i]) >> 4
        );

      if (invert)
        value =
          (guint8) (
            255U -
            value
          );

      output[i] = value;
    }

  return TRUE;
}


static gboolean
normalize_u16_range (
  const guint16 *source,
  gsize          pixel_count,
  gboolean       clamp_to_raw12,
  gboolean       invert,
  guint8        *output)
{
  guint16 minimum;
  guint16 maximum;

  if (pixel_count == 0)
    return FALSE;

  minimum =
    clamp_to_raw12
      ? clamp_raw12 (source[0])
      : source[0];

  maximum = minimum;

  for (gsize i = 1;
       i < pixel_count;
       i++)
    {
      const guint16 value =
        clamp_to_raw12
          ? clamp_raw12 (source[i])
          : source[i];

      minimum = MIN (minimum, value);
      maximum = MAX (maximum, value);
    }

  if (maximum == minimum)
    {
      memset (
        output,
        invert ? 255 : 0,
        pixel_count);

      return TRUE;
    }

  const guint32 range =
    (guint32) maximum -
    (guint32) minimum;

  for (gsize i = 0;
       i < pixel_count;
       i++)
    {
      const guint16 source_value =
        clamp_to_raw12
          ? clamp_raw12 (source[i])
          : source[i];

      const guint32 relative =
        (guint32) source_value -
        (guint32) minimum;

      guint8 value =
        (guint8) (
          (
            (guint64) relative *
            255U
          ) /
          range
        );

      if (invert)
        value =
          (guint8) (
            255U -
            value
          );

      output[i] = value;
    }

  return TRUE;
}


static gboolean
condition_gaussian_global_range (
  const guint16 *source,
  gsize          pixel_count,
  guint          width,
  guint          height,
  gboolean       invert,
  guint8        *output)
{
  guint16 *clamped;
  guint16 *filtered;
  gboolean result;

  clamped =
    g_try_new (
      guint16,
      pixel_count);

  filtered =
    g_try_new (
      guint16,
      pixel_count);

  if (clamped == NULL ||
      filtered == NULL)
    {
      g_free (clamped);
      g_free (filtered);

      return FALSE;
    }

  for (gsize i = 0;
       i < pixel_count;
       i++)
    clamped[i] =
      clamp_raw12 (source[i]);

  result =
    goodix5135_preprocess_gaussian_mode9_u16 (
      clamped,
      pixel_count,
      width,
      height,
      filtered,
      pixel_count);

  if (result)
    result =
      normalize_u16_range (
        filtered,
        pixel_count,
        FALSE,
        invert,
        output);

  g_free (clamped);
  g_free (filtered);

  return result;
}


const gchar *
goodix5135_conditioning_variant_name (
  Goodix5135ConditioningVariant variant)
{
  switch (variant)
    {
    case GOODIX5135_CONDITION_SHIFT4:
      return "SHIFT4";

    case GOODIX5135_CONDITION_SHIFT4_INVERT:
      return "SHIFT4_INVERT";

    case GOODIX5135_CONDITION_GLOBAL_RANGE:
      return "GLOBAL_RANGE";

    case GOODIX5135_CONDITION_GLOBAL_RANGE_INVERT:
      return "GLOBAL_RANGE_INVERT";

    case GOODIX5135_CONDITION_GAUSSIAN_GLOBAL_RANGE:
      return "GAUSSIAN_GLOBAL_RANGE";

    case GOODIX5135_CONDITION_GAUSSIAN_GLOBAL_RANGE_INVERT:
      return "GAUSSIAN_GLOBAL_RANGE_INVERT";

    case GOODIX5135_CONDITION_N_VARIANTS:
    default:
      return NULL;
    }
}


gboolean
goodix5135_conditioning_apply_u16 (
  Goodix5135ConditioningVariant variant,
  const guint16               *source,
  gsize                        source_pixels,
  guint                        width,
  guint                        height,
  guint8                      *output,
  gsize                        output_pixels)
{
  gsize pixel_count;

  if (source == NULL ||
      output == NULL ||
      width == 0 ||
      height == 0)
    return FALSE;

  if ((gsize) width >
      G_MAXSIZE /
      (gsize) height)
    return FALSE;

  pixel_count =
    (gsize) width *
    (gsize) height;

  if (source_pixels < pixel_count ||
      output_pixels < pixel_count)
    return FALSE;

  switch (variant)
    {
    case GOODIX5135_CONDITION_SHIFT4:
      return condition_shift4 (
        source,
        pixel_count,
        FALSE,
        output);

    case GOODIX5135_CONDITION_SHIFT4_INVERT:
      return condition_shift4 (
        source,
        pixel_count,
        TRUE,
        output);

    case GOODIX5135_CONDITION_GLOBAL_RANGE:
      return normalize_u16_range (
        source,
        pixel_count,
        TRUE,
        FALSE,
        output);

    case GOODIX5135_CONDITION_GLOBAL_RANGE_INVERT:
      return normalize_u16_range (
        source,
        pixel_count,
        TRUE,
        TRUE,
        output);

    case GOODIX5135_CONDITION_GAUSSIAN_GLOBAL_RANGE:
      return condition_gaussian_global_range (
        source,
        pixel_count,
        width,
        height,
        FALSE,
        output);

    case GOODIX5135_CONDITION_GAUSSIAN_GLOBAL_RANGE_INVERT:
      return condition_gaussian_global_range (
        source,
        pixel_count,
        width,
        height,
        TRUE,
        output);

    case GOODIX5135_CONDITION_N_VARIANTS:
    default:
      return FALSE;
    }
}
