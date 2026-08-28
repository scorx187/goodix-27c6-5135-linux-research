/*
 * Host-only tests for Goodix 27c6:5135 image primitives.
 *
 * All vectors are synthetic/public test data.
 * No fingerprint capture or unit-specific data is used.
 */

#include <glib.h>
#include <string.h>

#include "../libfprint/drivers/goodix5135/goodix5135-image.h"

static void
test_crc32_mpeg2 (void)
{
  static const guint8 vector[] = "123456789";

  g_assert_cmpuint (goodix5135_crc32_mpeg2 (vector,
                                            sizeof (vector) - 1),
                    ==,
                    0x0376e6e7U);

  g_assert_cmpuint (goodix5135_crc32_mpeg2 (NULL, 0),
                    ==,
                    0xffffffffU);

  g_assert_cmpuint (goodix5135_crc32_mpeg2 (NULL, 1),
                    ==,
                    0);
}

static void
test_stored_crc (void)
{
  static const guint8 stored[] = {
    0x11, 0x22, 0x33, 0x44
  };

  guint32 crc = 0;

  g_assert_true (goodix5135_crc32_from_stored (stored,
                                               sizeof (stored),
                                               &crc));

  g_assert_cmpuint (crc, ==, 0x33441122U);

  g_assert_false (goodix5135_crc32_from_stored (NULL,
                                                sizeof (stored),
                                                &crc));

  g_assert_false (goodix5135_crc32_from_stored (stored,
                                                sizeof (stored) - 1,
                                                &crc));

  g_assert_false (goodix5135_crc32_from_stored (stored,
                                                sizeof (stored),
                                                NULL));
}

static void
test_raw12_decode (void)
{
  static const guint8 group[6] = {
    0xa5, 0xbc, 0xde, 0x12, 0x34, 0xf6
  };

  guint8 packed[GOODIX5135_RAW12_PACKED_BYTES];
  guint16 pixels[GOODIX5135_IMAGE_PIXELS];

  gsize offset;
  gsize i;

  for (offset = 0;
       offset < sizeof (packed);
       offset += sizeof (group))
    memcpy (&packed[offset], group, sizeof (group));

  memset (pixels, 0, sizeof (pixels));

  g_assert_true (goodix5135_decode_raw12 (packed,
                                          sizeof (packed),
                                          pixels,
                                          G_N_ELEMENTS (pixels)));

  /*
   * For the synthetic group:
   *
   * b0=A5 b1=BC b2=DE b3=12 b4=34 b5=F6
   *
   * p0 = 0x5BC
   * p1 = 0x12A
   * p2 = 0x6DE
   * p3 = 0x34F
   */
  for (i = 0; i < G_N_ELEMENTS (pixels); i += 4)
    {
      g_assert_cmpuint (pixels[i + 0], ==, 0x5bc);
      g_assert_cmpuint (pixels[i + 1], ==, 0x12a);
      g_assert_cmpuint (pixels[i + 2], ==, 0x6de);
      g_assert_cmpuint (pixels[i + 3], ==, 0x34f);
    }

  g_assert_false (goodix5135_decode_raw12 (NULL,
                                           sizeof (packed),
                                           pixels,
                                           G_N_ELEMENTS (pixels)));

  g_assert_false (goodix5135_decode_raw12 (packed,
                                           sizeof (packed) - 1,
                                           pixels,
                                           G_N_ELEMENTS (pixels)));

  g_assert_false (goodix5135_decode_raw12 (packed,
                                           sizeof (packed),
                                           NULL,
                                           G_N_ELEMENTS (pixels)));

  g_assert_false (goodix5135_decode_raw12 (packed,
                                           sizeof (packed),
                                           pixels,
                                           G_N_ELEMENTS (pixels) - 1));
}

static void
test_chicagohu_regroup (void)
{
  guint16 src[GOODIX5135_IMAGE_PIXELS];
  guint16 dst[GOODIX5135_IMAGE_PIXELS];

  gsize n;

  memset (dst, 0, sizeof (dst));

  for (n = 0; n < G_N_ELEMENTS (src); n++)
    src[n] = (guint16) n;

  g_assert_true (goodix5135_chicagohu_regroup_u16 (
                   src,
                   G_N_ELEMENTS (src),
                   dst,
                   G_N_ELEMENTS (dst)));

  for (n = 0; n < GOODIX5135_IMAGE_PIXELS; n++)
    {
      const gsize dst_index =
        (n % 64) * GOODIX5135_IMAGE_WIDTH + (n / 64);

      g_assert_cmpuint (dst[dst_index], ==, n);
    }

  g_assert_cmpuint (dst[0], ==, 0);
  g_assert_cmpuint (dst[79], ==, 5056);
  g_assert_cmpuint (dst[80], ==, 1);
  g_assert_cmpuint (dst[GOODIX5135_IMAGE_PIXELS - 1],
                    ==,
                    GOODIX5135_IMAGE_PIXELS - 1);

  g_assert_false (goodix5135_chicagohu_regroup_u16 (
                    NULL,
                    G_N_ELEMENTS (src),
                    dst,
                    G_N_ELEMENTS (dst)));

  g_assert_false (goodix5135_chicagohu_regroup_u16 (
                    src,
                    G_N_ELEMENTS (src),
                    src,
                    G_N_ELEMENTS (src)));

  g_assert_false (goodix5135_chicagohu_regroup_u16 (
                    src,
                    G_N_ELEMENTS (src) - 1,
                    dst,
                    G_N_ELEMENTS (dst)));
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/goodix5135/image/crc32-mpeg2",
                   test_crc32_mpeg2);

  g_test_add_func ("/goodix5135/image/stored-crc",
                   test_stored_crc);

  g_test_add_func ("/goodix5135/image/raw12",
                   test_raw12_decode);

  g_test_add_func ("/goodix5135/image/chicagohu-regroup",
                   test_chicagohu_regroup);

  return g_test_run ();
}
