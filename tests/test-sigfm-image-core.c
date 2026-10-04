/*
 * Host-only SIGFM FpImage tests.
 *
 * Synthetic pixels only.
 * No USB, sensor, PSK, FDT or biometric fixture.
 * No exact SIGFM match scores are printed.
 */

#include <glib.h>
#include <string.h>

#include "fp-print-private.h"
#include "fpi-image.h"
#include "fpi-image-device.h"
#include "fpi-print.h"
#include "sigfm/sigfm.hpp"


#define TEST_WIDTH 80
#define TEST_HEIGHT 64


G_STATIC_ASSERT (
  FPI_PRINT_UNDEFINED == 0);

G_STATIC_ASSERT (
  FPI_PRINT_RAW == 1);

G_STATIC_ASSERT (
  FPI_PRINT_NBIS == 2);

G_STATIC_ASSERT (
  FPI_PRINT_SIGFM == 3);

G_STATIC_ASSERT (
  FPI_IMAGE_DEVICE_ALGORITHM_NBIS == 0);


typedef struct
{
  GMainLoop *loop;
  gboolean   success;
  GError    *error;
} ExtractState;


static guint8 *
make_synthetic_image (guint32 seed)
{
  guint8 *image =
    g_new0 (
      guint8,
      TEST_WIDTH * TEST_HEIGHT);

  guint32 state = seed;

  for (gint y = 0;
       y < TEST_HEIGHT;
       y++)
    {
      for (gint x = 0;
           x < TEST_WIDTH;
           x++)
        {
          state ^= state << 13;
          state ^= state >> 17;
          state ^= state << 5;

          guint value =
            state & 0xffU;

          if (((x / 5) + (y / 5)) & 1)
            value =
              MIN (
                255U,
                value + 55U);

          if ((x % 13) < 2 ||
              (y % 11) < 2)
            value =
              255U - value;

          if (x > TEST_WIDTH / 4 &&
              x < (3 * TEST_WIDTH) / 4 &&
              y > TEST_HEIGHT / 4 &&
              y < (3 * TEST_HEIGHT) / 4)
            value =
              (value * 3U + 127U) /
              4U;

          image[
            (gsize) y * TEST_WIDTH +
            (gsize) x
          ] =
            (guint8) value;
        }
    }

  return image;
}


static guint8 *
find_rich_synthetic_image (guint32 base)
{
  const guint32 step =
    0x9e3779b9U;

  for (guint attempt = 0;
       attempt < 128;
       attempt++)
    {
      g_autofree guint8 *pixels =
        make_synthetic_image (
          base + attempt * step);

      SigfmImgInfo *info =
        sigfm_extract (
          pixels,
          TEST_WIDTH,
          TEST_HEIGHT);

      if (info != NULL)
        {
          const gboolean rich =
            sigfm_keypoints_count (info) >= 25;

          sigfm_free_info (info);

          if (rich)
            return
              g_steal_pointer (
                &pixels);
        }
    }

  g_error (
    "Could not create rich deterministic synthetic image");

  return NULL;
}


static void
test_hflip (guint8 *pixels)
{
  for (gint y = 0;
       y < TEST_HEIGHT;
       y++)
    {
      for (gint x = 0;
           x < TEST_WIDTH / 2;
           x++)
        {
          const gsize left =
            (gsize) y * TEST_WIDTH + x;

          const gsize right =
            (gsize) y * TEST_WIDTH +
            (TEST_WIDTH - 1 - x);

          const guint8 tmp =
            pixels[left];

          pixels[left] =
            pixels[right];

          pixels[right] =
            tmp;
        }
    }
}


static void
test_invert (guint8 *pixels)
{
  for (gsize i = 0;
       i < TEST_WIDTH * TEST_HEIGHT;
       i++)
    pixels[i] =
      0xffU - pixels[i];
}


static void
extract_done_cb (GObject      *source_object,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  ExtractState *state =
    user_data;

  state->success =
    fpi_image_extract_sigfm_finish (
      FP_IMAGE (source_object),
      result,
      &state->error);

  g_main_loop_quit (
    state->loop);
}


static gboolean
run_extract (FpImage *image,
             GError **error)
{
  ExtractState state = {0};

  state.loop =
    g_main_loop_new (
      NULL,
      FALSE);

  fpi_image_extract_sigfm (
    image,
    NULL,
    extract_done_cb,
    &state);

  g_main_loop_run (
    state.loop);

  g_main_loop_unref (
    state.loop);

  if (error != NULL)
    *error =
      g_steal_pointer (
        &state.error);
  else
    g_clear_error (
      &state.error);

  return state.success;
}


static gboolean
sigfm_info_equal (SigfmImgInfo *a,
                  SigfmImgInfo *b)
{
  gint a_len = 0;
  gint b_len = 0;

  g_autofree guchar *a_data =
    sigfm_serialize_binary (
      a,
      &a_len);

  g_autofree guchar *b_data =
    sigfm_serialize_binary (
      b,
      &b_len);

  if (a_data == NULL ||
      b_data == NULL ||
      a_len <= 0 ||
      b_len != a_len)
    return FALSE;

  return
    memcmp (
      a_data,
      b_data,
      (gsize) a_len) == 0;
}


static FpPrint *
new_empty_sigfm_print (void)
{
  FpPrint *print =
    g_object_new (
      FP_TYPE_PRINT,
      "driver", "sigfm-image-test",
      "device-id", "synthetic-80x64",
      NULL);

  g_object_ref_sink (
    print);

  fpi_print_set_type (
    print,
    FPI_PRINT_SIGFM);

  return print;
}


static void
test_extract_and_print_copy (void)
{
  g_autofree guint8 *pixels =
    find_rich_synthetic_image (
      0x5135cafeU);

  g_autoptr(FpImage) image =
    fp_image_new (
      TEST_WIDTH,
      TEST_HEIGHT);

  memcpy (
    image->data,
    pixels,
    TEST_WIDTH * TEST_HEIGHT);

  g_autoptr(GError) error = NULL;

  g_assert_true (
    run_extract (
      image,
      &error));

  g_assert_no_error (
    error);

  SigfmImgInfo *image_info =
    fpi_image_get_sigfm_info (
      image);

  g_assert_nonnull (
    image_info);

  g_assert_cmpint (
    sigfm_keypoints_count (
      image_info),
    >=,
    25);

  g_autoptr(FpPrint) print =
    new_empty_sigfm_print ();

  g_assert_true (
    fpi_print_add_from_image (
      print,
      image,
      &error));

  g_assert_no_error (
    error);

  g_assert_cmpuint (
    print->prints->len,
    ==,
    1);

  SigfmImgInfo *print_info =
    g_ptr_array_index (
      print->prints,
      0);

  g_assert_true (
    print_info != image_info);

  g_assert_true (
    sigfm_info_equal (
      image_info,
      print_info));

  /*
   * Remove the retained debugging image on purpose and
   * then destroy our local image reference. The SIGFM
   * print representation must remain self-contained.
   */
  g_clear_object (
    &print->image);

  g_clear_object (
    &image);

  g_autofree guchar *serialized = NULL;
  gsize serialized_len = 0;

  g_assert_true (
    fp_print_serialize (
      print,
      &serialized,
      &serialized_len,
      &error));

  g_assert_no_error (
    error);

  g_assert_cmpuint (
    serialized_len,
    >,
    3);

  g_autoptr(FpPrint) restored =
    fp_print_deserialize (
      serialized,
      serialized_len,
      &error);

  g_assert_no_error (
    error);

  g_assert_nonnull (
    restored);

  g_assert_true (
    fp_print_equal (
      print,
      restored));
}


static void
test_extraction_normalizes_flags (void)
{
  g_autofree guint8 *base_pixels =
    find_rich_synthetic_image (
      0x5135babeU);

  g_autofree guint8 *encoded_pixels =
    g_memdup2 (
      base_pixels,
      TEST_WIDTH * TEST_HEIGHT);

  /*
   * Build an encoded image whose declared flags undo
   * these operations during SIGFM extraction.
   */
  test_hflip (
    encoded_pixels);

  test_invert (
    encoded_pixels);

  g_autoptr(FpImage) base =
    fp_image_new (
      TEST_WIDTH,
      TEST_HEIGHT);

  g_autoptr(FpImage) encoded =
    fp_image_new (
      TEST_WIDTH,
      TEST_HEIGHT);

  memcpy (
    base->data,
    base_pixels,
    TEST_WIDTH * TEST_HEIGHT);

  memcpy (
    encoded->data,
    encoded_pixels,
    TEST_WIDTH * TEST_HEIGHT);

  encoded->flags =
    FPI_IMAGE_H_FLIPPED |
    FPI_IMAGE_COLORS_INVERTED;

  g_autoptr(GError) error = NULL;

  g_assert_true (
    run_extract (
      base,
      &error));

  g_assert_no_error (
    error);

  g_assert_true (
    run_extract (
      encoded,
      &error));

  g_assert_no_error (
    error);

  g_assert_cmpint (
    encoded->flags &
      (FPI_IMAGE_H_FLIPPED |
       FPI_IMAGE_V_FLIPPED |
       FPI_IMAGE_COLORS_INVERTED),
    ==,
    0);

  g_assert_cmpint (
    memcmp (
      encoded->data,
      base_pixels,
      TEST_WIDTH * TEST_HEIGHT),
    ==,
    0);

  g_assert_true (
    sigfm_info_equal (
      fpi_image_get_sigfm_info (
        base),
      fpi_image_get_sigfm_info (
        encoded)));
}


static void
test_unmatchable_flat_image_rejected (void)
{
  g_autoptr(FpImage) image =
    fp_image_new (
      TEST_WIDTH,
      TEST_HEIGHT);

  memset (
    image->data,
    0,
    TEST_WIDTH * TEST_HEIGHT);

  g_autoptr(GError) error = NULL;

  g_assert_false (
    run_extract (
      image,
      &error));

  g_assert_error (
    error,
    G_IO_ERROR,
    G_IO_ERROR_FAILED);

  g_assert_null (
    fpi_image_get_sigfm_info (
      image));
}


static void
test_algorithm_selector_defaults (void)
{
  FpImageDeviceClass zero_class;

  memset (
    &zero_class,
    0,
    sizeof (zero_class));

  g_assert_cmpint (
    zero_class.algorithm,
    ==,
    FPI_IMAGE_DEVICE_ALGORITHM_NBIS);

  g_assert_cmpint (
    zero_class.sigfm_threshold,
    ==,
    0);

  zero_class.algorithm =
    FPI_IMAGE_DEVICE_ALGORITHM_SIGFM;

  zero_class.sigfm_threshold = 1;

  g_assert_cmpint (
    zero_class.algorithm,
    ==,
    FPI_IMAGE_DEVICE_ALGORITHM_SIGFM);

  g_assert_cmpint (
    zero_class.sigfm_threshold,
    >,
    0);
}


int
main (int argc, char **argv)
{
  g_test_init (
    &argc,
    &argv,
    NULL);

  g_test_add_func (
    "/sigfm/image/extract-and-print-copy",
    test_extract_and_print_copy);

  g_test_add_func (
    "/sigfm/image/normalizes-flags",
    test_extraction_normalizes_flags);

  g_test_add_func (
    "/sigfm/image/reject-flat",
    test_unmatchable_flat_image_rejected);

  g_test_add_func (
    "/sigfm/image/selector-default",
    test_algorithm_selector_defaults);

  return g_test_run ();
}
