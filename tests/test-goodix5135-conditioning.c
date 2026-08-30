/*
 * Host-only tests for Goodix5135 diagnostic conditioning variants.
 *
 * Synthetic data only.
 * No fingerprint image, raw capture, template, calibration, OTP or PSK.
 */

#include <glib.h>
#include <string.h>

#include "../libfprint/drivers/goodix5135/goodix5135-conditioning.h"


static guint64
weighted_checksum (
  const guint8 *data,
  gsize         length)
{
  guint64 checksum = 0;

  for (gsize i = 0;
       i < length;
       i++)
    {
      checksum +=
        (guint64) data[i] *
        (guint64) (i + 1U);
    }

  return checksum;
}


static void
test_names (void)
{
  static const gchar *expected[] = {
    "SHIFT4",
    "SHIFT4_INVERT",
    "GLOBAL_RANGE",
    "GLOBAL_RANGE_INVERT",
    "GAUSSIAN_GLOBAL_RANGE",
    "GAUSSIAN_GLOBAL_RANGE_INVERT",
  };

  for (guint i = 0;
       i < G_N_ELEMENTS (expected);
       i++)
    g_assert_cmpstr (
      goodix5135_conditioning_variant_name (
        (Goodix5135ConditioningVariant) i),
      ==,
      expected[i]);

  g_assert_null (
    goodix5135_conditioning_variant_name (
      GOODIX5135_CONDITION_N_VARIANTS));
}


static void
test_shift4 (void)
{
  static const guint16 source[] = {
    0,
    1,
    15,
    16,
    4095,
    5000,
  };

  static const guint8 expected[] = {
    0,
    0,
    0,
    1,
    255,
    255,
  };

  guint8 output[G_N_ELEMENTS (source)] = { 0 };

  g_assert_true (
    goodix5135_conditioning_apply_u16 (
      GOODIX5135_CONDITION_SHIFT4,
      source,
      G_N_ELEMENTS (source),
      G_N_ELEMENTS (source),
      1,
      output,
      G_N_ELEMENTS (output)));

  g_assert_cmpmem (
    output,
    sizeof (output),
    expected,
    sizeof (expected));
}


static void
test_shift4_invert (void)
{
  static const guint16 source[] = {
    0,
    16,
    4095,
  };

  static const guint8 expected[] = {
    255,
    254,
    0,
  };

  guint8 output[G_N_ELEMENTS (source)] = { 0 };

  g_assert_true (
    goodix5135_conditioning_apply_u16 (
      GOODIX5135_CONDITION_SHIFT4_INVERT,
      source,
      G_N_ELEMENTS (source),
      G_N_ELEMENTS (source),
      1,
      output,
      G_N_ELEMENTS (output)));

  g_assert_cmpmem (
    output,
    sizeof (output),
    expected,
    sizeof (expected));
}


static void
test_global_range (void)
{
  static const guint16 source[] = {
    1000,
    2000,
    3000,
  };

  static const guint8 expected[] = {
    0,
    127,
    255,
  };

  guint8 output[G_N_ELEMENTS (source)] = { 0 };

  g_assert_true (
    goodix5135_conditioning_apply_u16 (
      GOODIX5135_CONDITION_GLOBAL_RANGE,
      source,
      G_N_ELEMENTS (source),
      G_N_ELEMENTS (source),
      1,
      output,
      G_N_ELEMENTS (output)));

  g_assert_cmpmem (
    output,
    sizeof (output),
    expected,
    sizeof (expected));
}


static void
test_global_range_invert (void)
{
  static const guint16 source[] = {
    1000,
    2000,
    3000,
  };

  static const guint8 expected[] = {
    255,
    128,
    0,
  };

  guint8 output[G_N_ELEMENTS (source)] = { 0 };

  g_assert_true (
    goodix5135_conditioning_apply_u16 (
      GOODIX5135_CONDITION_GLOBAL_RANGE_INVERT,
      source,
      G_N_ELEMENTS (source),
      G_N_ELEMENTS (source),
      1,
      output,
      G_N_ELEMENTS (output)));

  g_assert_cmpmem (
    output,
    sizeof (output),
    expected,
    sizeof (expected));
}


static void
test_flat_range (void)
{
  static const guint16 source[] = {
    1234,
    1234,
    1234,
  };

  guint8 output[3] = { 99, 99, 99 };

  g_assert_true (
    goodix5135_conditioning_apply_u16 (
      GOODIX5135_CONDITION_GLOBAL_RANGE,
      source,
      G_N_ELEMENTS (source),
      3,
      1,
      output,
      G_N_ELEMENTS (output)));

  g_assert_cmpuint (output[0], ==, 0);
  g_assert_cmpuint (output[1], ==, 0);
  g_assert_cmpuint (output[2], ==, 0);

  g_assert_true (
    goodix5135_conditioning_apply_u16 (
      GOODIX5135_CONDITION_GLOBAL_RANGE_INVERT,
      source,
      G_N_ELEMENTS (source),
      3,
      1,
      output,
      G_N_ELEMENTS (output)));

  g_assert_cmpuint (output[0], ==, 255);
  g_assert_cmpuint (output[1], ==, 255);
  g_assert_cmpuint (output[2], ==, 255);
}


static void
test_synthetic_80x64_all_variants (void)
{
  enum
  {
    WIDTH = 80,
    HEIGHT = 64,
    PIXELS = WIDTH * HEIGHT,
  };

  static const guint64 expected_checksums[] = {
    G_GUINT64_CONSTANT (1705639984),
    G_GUINT64_CONSTANT (1637348816),
    G_GUINT64_CONSTANT (1699425776),
    G_GUINT64_CONSTANT (1643563024),
    G_GUINT64_CONSTANT (1802926683),
    G_GUINT64_CONSTANT (1540062117),
  };

  guint16 *source =
    g_new0 (
      guint16,
      PIXELS);

  guint8 *output =
    g_new0 (
      guint8,
      PIXELS);

  for (guint y = 0;
       y < HEIGHT;
       y++)
    {
      for (guint x = 0;
           x < WIDTH;
           x++)
        {
          source[
            (gsize) y *
            WIDTH +
            x
          ] =
            (guint16) (
              (
                x * 37U +
                y * 53U +
                x * y
              ) &
              0x0fffU
            );
        }
    }

  for (guint variant = 0;
       variant < GOODIX5135_CONDITION_N_VARIANTS;
       variant++)
    {
      memset (
        output,
        0,
        PIXELS);

      g_assert_true (
        goodix5135_conditioning_apply_u16 (
          (Goodix5135ConditioningVariant) variant,
          source,
          PIXELS,
          WIDTH,
          HEIGHT,
          output,
          PIXELS));

      const guint64 checksum =
        weighted_checksum (
          output,
          PIXELS);

      g_test_message (
        "%s checksum=%"
        G_GUINT64_FORMAT,
        goodix5135_conditioning_variant_name (
          (Goodix5135ConditioningVariant) variant),
        checksum);

      g_assert_cmpuint (
        checksum,
        ==,
        expected_checksums[variant]);
    }

  g_free (source);
  g_free (output);
}


static void
test_invalid_arguments (void)
{
  guint16 source[4] = { 0 };
  guint8 output[4] = { 0 };

  g_assert_false (
    goodix5135_conditioning_apply_u16 (
      GOODIX5135_CONDITION_SHIFT4,
      NULL,
      4,
      2,
      2,
      output,
      4));

  g_assert_false (
    goodix5135_conditioning_apply_u16 (
      GOODIX5135_CONDITION_SHIFT4,
      source,
      4,
      2,
      2,
      NULL,
      4));

  g_assert_false (
    goodix5135_conditioning_apply_u16 (
      GOODIX5135_CONDITION_SHIFT4,
      source,
      3,
      2,
      2,
      output,
      4));

  g_assert_false (
    goodix5135_conditioning_apply_u16 (
      GOODIX5135_CONDITION_SHIFT4,
      source,
      4,
      2,
      2,
      output,
      3));

  g_assert_false (
    goodix5135_conditioning_apply_u16 (
      GOODIX5135_CONDITION_N_VARIANTS,
      source,
      4,
      2,
      2,
      output,
      4));
}


int
main (int argc, char **argv)
{
  g_test_init (
    &argc,
    &argv,
    NULL);

  g_test_add_func (
    "/goodix5135/conditioning/names",
    test_names);

  g_test_add_func (
    "/goodix5135/conditioning/shift4",
    test_shift4);

  g_test_add_func (
    "/goodix5135/conditioning/shift4-invert",
    test_shift4_invert);

  g_test_add_func (
    "/goodix5135/conditioning/global-range",
    test_global_range);

  g_test_add_func (
    "/goodix5135/conditioning/global-range-invert",
    test_global_range_invert);

  g_test_add_func (
    "/goodix5135/conditioning/flat-range",
    test_flat_range);

  g_test_add_func (
    "/goodix5135/conditioning/synthetic-80x64-all",
    test_synthetic_80x64_all_variants);

  g_test_add_func (
    "/goodix5135/conditioning/invalid-arguments",
    test_invalid_arguments);

  return g_test_run ();
}
