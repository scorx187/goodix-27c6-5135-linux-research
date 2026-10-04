/*
 * Host-only SIGFM print-core tests.
 *
 * Synthetic pixels only.
 * No USB, fingerprint, PSK, FDT or biometric fixture.
 * Exact matcher scores are never printed.
 */

#include <glib.h>
#include <string.h>

#include "fp-print-private.h"
#include "fpi-print.h"
#include "sigfm/sigfm.hpp"


#define TEST_WIDTH 80
#define TEST_HEIGHT 64


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


static FpPrint *
new_empty_sigfm_print (void)
{
  FpPrint *print =
    g_object_new (
      FP_TYPE_PRINT,
      "driver", "sigfm-synthetic-test",
      "device-id", "synthetic-80x64",
      NULL);

  g_object_ref_sink (print);

  fpi_print_set_type (
    print,
    FPI_PRINT_SIGFM);

  return print;
}


static FpPrint *
new_sigfm_print (guint32 seed)
{
  const guint32 step = 0x9e3779b9U;

  for (guint attempt = 0;
       attempt < 128;
       attempt++)
    {
      const guint32 candidate_seed =
        seed + attempt * step;

      g_autofree guint8 *image =
        make_synthetic_image (
          candidate_seed);

      SigfmImgInfo *info =
        sigfm_extract (
          image,
          TEST_WIDTH,
          TEST_HEIGHT);

      if (info != NULL &&
          sigfm_keypoints_count (info) >= 25)
        {
          FpPrint *print =
            new_empty_sigfm_print ();

          g_ptr_array_add (
            print->prints,
            info);

          memset (
            image,
            0,
            TEST_WIDTH * TEST_HEIGHT);

          return print;
        }

      if (info != NULL)
        sigfm_free_info (info);

      memset (
        image,
        0,
        TEST_WIDTH * TEST_HEIGHT);
    }

  g_error (
    "SIGFM deterministic synthetic fixture exhausted");

  return NULL;
}



static void
test_sigfm_copy_collection (void)
{
  g_autoptr(FpPrint) sample =
    new_sigfm_print (
      0x5135cafeU);

  g_autoptr(FpPrint) collection =
    new_empty_sigfm_print ();

  fpi_print_add_print (
    collection,
    sample);

  g_assert_cmpuint (
    collection->prints->len,
    ==,
    1);

  g_assert_true (
    g_ptr_array_index (
      collection->prints,
      0)
    !=
    g_ptr_array_index (
      sample->prints,
      0));

  SigfmImgInfo *sample_info =
    g_ptr_array_index (
      sample->prints,
      0);

  SigfmImgInfo *copy_info =
    g_ptr_array_index (
      collection->prints,
      0);

  gint sample_len = 0;
  gint copy_len = 0;

  g_autofree guchar *sample_serialized =
    sigfm_serialize_binary (
      sample_info,
      &sample_len);

  g_autofree guchar *copy_serialized =
    sigfm_serialize_binary (
      copy_info,
      &copy_len);

  g_assert_nonnull (
    sample_serialized);

  g_assert_nonnull (
    copy_serialized);

  g_assert_cmpint (
    sample_len,
    >,
    0);

  g_assert_cmpint (
    copy_len,
    ==,
    sample_len);

  g_assert_cmpint (
    memcmp (
      sample_serialized,
      copy_serialized,
      (gsize) sample_len),
    ==,
    0);
}


static void
test_sigfm_serialization_roundtrip (void)
{
  g_autoptr(FpPrint) a =
    new_sigfm_print (
      0x5135cafeU);

  g_autoptr(FpPrint) b =
    new_sigfm_print (
      0x51f00d13U);

  g_autoptr(FpPrint) collection =
    new_empty_sigfm_print ();

  g_autofree guchar *serialized = NULL;
  gsize serialized_len = 0;
  g_autoptr(GError) error = NULL;

  fpi_print_add_print (
    collection,
    a);

  fpi_print_add_print (
    collection,
    b);

  g_assert_cmpuint (
    collection->prints->len,
    ==,
    2);

  g_assert_true (
    fp_print_serialize (
      collection,
      &serialized,
      &serialized_len,
      &error));

  g_assert_no_error (error);
  g_assert_nonnull (serialized);
  g_assert_cmpuint (
    serialized_len,
    >,
    3);

  g_autoptr(FpPrint) restored =
    fp_print_deserialize (
      serialized,
      serialized_len,
      &error);

  g_assert_no_error (error);
  g_assert_nonnull (restored);

  g_assert_cmpint (
    restored->type,
    ==,
    FPI_PRINT_SIGFM);

  g_assert_cmpuint (
    restored->prints->len,
    ==,
    2);

  g_assert_true (
    fp_print_equal (
      collection,
      restored));
}


static void
test_sigfm_same_different (void)
{
  g_autoptr(FpPrint) enrolled =
    new_sigfm_print (
      0x5135cafeU);

  g_autoptr(FpPrint) same =
    new_sigfm_print (
      0x5135cafeU);

  SigfmImgInfo *enrolled_info =
    g_ptr_array_index (
      enrolled->prints,
      0);

  SigfmImgInfo *same_info =
    g_ptr_array_index (
      same->prints,
      0);

  const gint same_score =
    sigfm_match_score (
      same_info,
      enrolled_info);

  if (same_score <= 0)
    g_error (
      "SIGFM synthetic same-case failed");

  g_autoptr(FpPrint) different = NULL;
  gint different_score = -1;

  const guint32 negative_base =
    0xa5c3f117U;

  const guint32 negative_step =
    0x7f4a7c15U;

  for (guint attempt = 0;
       attempt < 64;
       attempt++)
    {
      g_autoptr(FpPrint) candidate =
        new_sigfm_print (
          negative_base +
          attempt * negative_step);

      SigfmImgInfo *candidate_info =
        g_ptr_array_index (
          candidate->prints,
          0);

      const gint candidate_score =
        sigfm_match_score (
          candidate_info,
          enrolled_info);

      if (candidate_score >= 0 &&
          candidate_score < same_score)
        {
          different_score =
            candidate_score;

          different =
            g_steal_pointer (
              &candidate);

          break;
        }
    }

  if (different == NULL ||
      different_score < 0 ||
      different_score >= same_score)
    g_error (
      "SIGFM synthetic negative-case search failed");

  const gint threshold =
    different_score +
    ((same_score -
      different_score +
      1) / 2);

  if (threshold <= different_score ||
      threshold > same_score)
    g_error (
      "SIGFM synthetic threshold derivation failed");

  g_autoptr(GError) error = NULL;

  FpiMatchResult result =
    fpi_print_sigfm_match (
      enrolled,
      same,
      threshold,
      &error);

  g_assert_no_error (error);

  g_assert_cmpint (
    result,
    ==,
    FPI_MATCH_SUCCESS);

  result =
    fpi_print_sigfm_match (
      enrolled,
      different,
      threshold,
      &error);

  g_assert_no_error (error);

  g_assert_cmpint (
    result,
    ==,
    FPI_MATCH_FAIL);
}



int
main (int argc, char **argv)
{
  g_test_init (
    &argc,
    &argv,
    NULL);

  g_test_add_func (
    "/sigfm/print/copy-collection",
    test_sigfm_copy_collection);

  g_test_add_func (
    "/sigfm/print/serialization-roundtrip",
    test_sigfm_serialization_roundtrip);

  g_test_add_func (
    "/sigfm/print/same-different",
    test_sigfm_same_different);

  return g_test_run ();
}
