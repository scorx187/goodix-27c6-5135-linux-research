/*
 * Host-side preprocessing primitives for Goodix 27c6:5135 ChicagoHU.
 *
 * These functions implement only isolated numeric operations whose
 * behavior has been established independently.
 *
 * This is NOT yet the complete Chicago preprocessing pipeline.
 *
 * In particular, no currently available Linux buffer is assumed to be
 * equivalent to the private Windows preprocessing state/calibration
 * planes.
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

gint goodix5135_preprocess_reflect101 (
  gint coordinate,
  gint extent);

guint32 goodix5135_preprocess_q13_mul (
  guint16 a,
  guint16 b);

guint16 goodix5135_preprocess_state_minus_source_u16 (
  guint16 state_value,
  guint16 source_value);

gboolean goodix5135_preprocess_local_contrast_map_u8 (
  guint16  source,
  guint16  low,
  guint16  high,
  guint8  *output);

gboolean goodix5135_preprocess_gaussian_mode9_u16 (
  const guint16 *source,
  gsize          source_pixels,
  guint          width,
  guint          height,
  guint16       *output,
  gsize          output_pixels);

G_END_DECLS
