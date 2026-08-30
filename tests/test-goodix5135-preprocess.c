/*
 * Host-only Goodix 27c6:5135 preprocessing primitive tests.
 *
 * Every image/vector in this file is synthetic public test data.
 *
 * No fingerprint capture, template, calibration payload, device secret,
 * unit-specific configuration, or Windows biometric material is used.
 */

#include <glib.h>
#include <string.h>

#include "../libfprint/drivers/goodix5135/goodix5135-preprocess.h"


static void
test_reflect101 (void)
{
  g_assert_cmpint (
    goodix5135_preprocess_reflect101 (-1, 8),
    ==,
    1);

  g_assert_cmpint (
    goodix5135_preprocess_reflect101 (-2, 8),
    ==,
    2);

  g_assert_cmpint (
    goodix5135_preprocess_reflect101 (8, 8),
    ==,
    6);

  g_assert_cmpint (
    goodix5135_preprocess_reflect101 (9, 8),
    ==,
    5);

  g_assert_cmpint (
    goodix5135_preprocess_reflect101 (0, 1),
    ==,
    0);

  g_assert_cmpint (
    goodix5135_preprocess_reflect101 (-100, 1),
    ==,
    0);
}


static void
test_q13_composition (void)
{
  g_assert_cmpuint (
    goodix5135_preprocess_q13_mul (
      0x2000U,
      10000U),
    ==,
    10000U);

  g_assert_cmpuint (
    goodix5135_preprocess_q13_mul (
      0x2000U,
      0x2000U),
    ==,
    0x2000U);

  g_assert_cmpuint (
    goodix5135_preprocess_q13_mul (
      0U,
      0x2000U),
    ==,
    0U);
}


static void
test_state_minus_source (void)
{
  g_assert_cmpuint (
    goodix5135_preprocess_state_minus_source_u16 (
      200U,
      100U),
    ==,
    100U);

  g_assert_cmpuint (
    goodix5135_preprocess_state_minus_source_u16 (
      100U,
      200U),
    ==,
    65436U);
}


static void
test_local_contrast (void)
{
  guint8 output = 0;

  g_assert_true (
    goodix5135_preprocess_local_contrast_map_u8 (
      0U,
      0U,
      100U,
      &output));

  g_assert_cmpuint (output, ==, 255U);

  g_assert_true (
    goodix5135_preprocess_local_contrast_map_u8 (
      100U,
      0U,
      100U,
      &output));

  g_assert_cmpuint (output, ==, 0U);

  g_assert_true (
    goodix5135_preprocess_local_contrast_map_u8 (
      50U,
      0U,
      100U,
      &output));

  /*
   * 50*255/100 = 127 using integer truncation,
   * therefore 255-127 = 128.
   */
  g_assert_cmpuint (output, ==, 128U);

  g_assert_true (
    goodix5135_preprocess_local_contrast_map_u8 (
      5U,
      10U,
      110U,
      &output));

  g_assert_cmpuint (output, ==, 255U);

  g_assert_true (
    goodix5135_preprocess_local_contrast_map_u8 (
      120U,
      10U,
      110U,
      &output));

  g_assert_cmpuint (output, ==, 0U);

  g_assert_true (
    goodix5135_preprocess_local_contrast_map_u8 (
      75U,
      50U,
      50U,
      &output));

  g_assert_cmpuint (output, ==, 0U);

  g_assert_false (
    goodix5135_preprocess_local_contrast_map_u8 (
      50U,
      100U,
      10U,
      &output));

  g_assert_false (
    goodix5135_preprocess_local_contrast_map_u8 (
      50U,
      0U,
      100U,
      NULL));
}


static void
test_gaussian_constant (void)
{
  enum
  {
    WIDTH = 7,
    HEIGHT = 6,
    PIXELS = WIDTH * HEIGHT,
  };

  guint16 source[PIXELS];
  guint16 output[PIXELS];

  for (gsize i = 0;
       i < G_N_ELEMENTS (source);
       i++)
    source[i] = 1234U;

  memset (
    output,
    0,
    sizeof (output));

  g_assert_true (
    goodix5135_preprocess_gaussian_mode9_u16 (
      source,
      G_N_ELEMENTS (source),
      WIDTH,
      HEIGHT,
      output,
      G_N_ELEMENTS (output)));

  for (gsize i = 0;
       i < G_N_ELEMENTS (output);
       i++)
    g_assert_cmpuint (
      output[i],
      ==,
      1234U);
}


static void
test_gaussian_exact_vector (void)
{
  static const guint16 source[5] = {
    0U,
    0U,
    65535U,
    0U,
    0U,
  };

  static const guint16 expected[5] = {
    15737U,
    15327U,
    19141U,
    15327U,
    15737U,
  };

  guint16 output[5] = { 0 };

  g_assert_true (
    goodix5135_preprocess_gaussian_mode9_u16 (
      source,
      G_N_ELEMENTS (source),
      5,
      1,
      output,
      G_N_ELEMENTS (output)));

  for (gsize i = 0;
       i < G_N_ELEMENTS (output);
       i++)
    g_assert_cmpuint (
      output[i],
      ==,
      expected[i]);
}


static void
test_gaussian_80x64 (void)
{
  enum
  {
    WIDTH = 80,
    HEIGHT = 64,
    PIXELS = WIDTH * HEIGHT,
  };

  guint16 *source;
  guint16 *output;

  guint64 checksum = 0;

  source =
    g_new0 (
      guint16,
      PIXELS);

  output =
    g_new0 (
      guint16,
      PIXELS);

  /*
   * Deterministic synthetic pattern only.
   * This is not fingerprint data.
   */
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
                (x * y)
              ) &
              0x0fffU
            );
        }
    }

  g_assert_true (
    goodix5135_preprocess_gaussian_mode9_u16 (
      source,
      PIXELS,
      WIDTH,
      HEIGHT,
      output,
      PIXELS));

  for (gsize i = 0;
       i < PIXELS;
       i++)
    {
      checksum +=
        (guint64) output[i] *
        (guint64) (i + 1U);
    }

  g_test_message (
    "synthetic 80x64 checksum=%"
    G_GUINT64_FORMAT,
    checksum);

  g_assert_true (
    checksum ==
    G_GUINT64_CONSTANT (27396722557));

  g_free (source);
  g_free (output);
}


static void
test_gaussian_invalid_arguments (void)
{
  guint16 source[4] = { 0 };
  guint16 output[4] = { 0 };

  g_assert_false (
    goodix5135_preprocess_gaussian_mode9_u16 (
      NULL,
      4,
      2,
      2,
      output,
      4));

  g_assert_false (
    goodix5135_preprocess_gaussian_mode9_u16 (
      source,
      4,
      2,
      2,
      NULL,
      4));

  g_assert_false (
    goodix5135_preprocess_gaussian_mode9_u16 (
      source,
      4,
      0,
      2,
      output,
      4));

  g_assert_false (
    goodix5135_preprocess_gaussian_mode9_u16 (
      source,
      3,
      2,
      2,
      output,
      4));

  g_assert_false (
    goodix5135_preprocess_gaussian_mode9_u16 (
      source,
      4,
      2,
      2,
      output,
      3));
}


int
main (int argc, char **argv)
{
  g_test_init (
    &argc,
    &argv,
    NULL);

  g_test_add_func (
    "/goodix5135/preprocess/reflect101",
    test_reflect101);

  g_test_add_func (
    "/goodix5135/preprocess/q13-composition",
    test_q13_composition);

  g_test_add_func (
    "/goodix5135/preprocess/state-minus-source",
    test_state_minus_source);

  g_test_add_func (
    "/goodix5135/preprocess/local-contrast",
    test_local_contrast);

  g_test_add_func (
    "/goodix5135/preprocess/gaussian-constant",
    test_gaussian_constant);

  g_test_add_func (
    "/goodix5135/preprocess/gaussian-exact-vector",
    test_gaussian_exact_vector);

  g_test_add_func (
    "/goodix5135/preprocess/gaussian-80x64",
    test_gaussian_80x64);

  g_test_add_func (
    "/goodix5135/preprocess/gaussian-invalid-arguments",
    test_gaussian_invalid_arguments);

  return g_test_run ();
}
