/*
 * Diagnostic image-conditioning variants for Goodix 27c6:5135.
 *
 * These transformations are intended for controlled host-side and
 * diagnostic comparison only.
 *
 * They do NOT claim Windows AlgoChicago preprocessing parity.
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef enum
{
  GOODIX5135_CONDITION_SHIFT4 = 0,
  GOODIX5135_CONDITION_SHIFT4_INVERT,
  GOODIX5135_CONDITION_GLOBAL_RANGE,
  GOODIX5135_CONDITION_GLOBAL_RANGE_INVERT,
  GOODIX5135_CONDITION_GAUSSIAN_GLOBAL_RANGE,
  GOODIX5135_CONDITION_GAUSSIAN_GLOBAL_RANGE_INVERT,
  GOODIX5135_CONDITION_N_VARIANTS,
} Goodix5135ConditioningVariant;

const gchar *
goodix5135_conditioning_variant_name (
  Goodix5135ConditioningVariant variant);

gboolean
goodix5135_conditioning_apply_u16 (
  Goodix5135ConditioningVariant variant,
  const guint16               *source,
  gsize                        source_pixels,
  guint                        width,
  guint                        height,
  guint8                      *output,
  gsize                        output_pixels);

G_END_DECLS
