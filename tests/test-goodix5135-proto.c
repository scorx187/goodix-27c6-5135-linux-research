/*
 * Host-only protocol framing tests for Goodix 27c6:5135.
 *
 * Synthetic byte patterns only.
 * No fingerprint capture, secret, calibration, or unit-specific data.
 */

#include <glib.h>
#include <string.h>

#include "../libfprint/drivers/goodix5135/goodix5135-proto.h"

static void
build_valid_image_frame (guint8 *data)
{
  gsize i;

  memset (data, 0, GOODIX5135_IMAGE_TOTAL_LENGTH);

  data[0] = GOODIX5135_IMAGE_COMMAND;

  data[1] =
    (guint8) (GOODIX5135_IMAGE_DECLARED_LENGTH & 0xff);
  data[2] =
    (guint8) ((GOODIX5135_IMAGE_DECLARED_LENGTH >> 8) & 0xff);

  /*
   * Synthetic recognizable regions.
   * These values have no relation to biometric data.
   */
  for (i = 0; i < GOODIX5135_IMAGE_METADATA_LENGTH; i++)
    data[3 + i] = (guint8) (0x10 + i);

  memset (data + 3 + GOODIX5135_IMAGE_METADATA_LENGTH,
          0xa5,
          GOODIX5135_IMAGE_PACKED_LENGTH);

  data[GOODIX5135_IMAGE_TOTAL_LENGTH - 5] = 0x11;
  data[GOODIX5135_IMAGE_TOTAL_LENGTH - 4] = 0x22;
  data[GOODIX5135_IMAGE_TOTAL_LENGTH - 3] = 0x33;
  data[GOODIX5135_IMAGE_TOTAL_LENGTH - 2] = 0x44;

  data[GOODIX5135_IMAGE_TOTAL_LENGTH - 1] =
    GOODIX5135_PROTOCOL_TRAILER;
}

static void
test_valid_image_frame (void)
{
  guint8 data[GOODIX5135_IMAGE_TOTAL_LENGTH];
  Goodix5135ImageFrame frame = { 0 };

  build_valid_image_frame (data);

  g_assert_true (goodix5135_parse_image_frame (
                   data,
                   sizeof (data),
                   &frame));

  g_assert_nonnull (frame.metadata);
  g_assert_nonnull (frame.packed_raw12);
  g_assert_nonnull (frame.stored_crc);

  g_assert_cmpuint (frame.metadata_length,
                    ==,
                    GOODIX5135_IMAGE_METADATA_LENGTH);

  g_assert_cmpuint (frame.packed_raw12_length,
                    ==,
                    GOODIX5135_IMAGE_PACKED_LENGTH);

  g_assert_cmpuint (frame.stored_crc_length,
                    ==,
                    GOODIX5135_IMAGE_STORED_CRC_LENGTH);

  g_assert_true (frame.metadata == data + 3);

  g_assert_true (
    frame.packed_raw12 ==
    data + 3 + GOODIX5135_IMAGE_METADATA_LENGTH);

  g_assert_true (
    frame.stored_crc ==
    data + GOODIX5135_IMAGE_TOTAL_LENGTH -
    1 - GOODIX5135_IMAGE_STORED_CRC_LENGTH);

  g_assert_cmpuint (frame.metadata[0], ==, 0x10);
  g_assert_cmpuint (frame.packed_raw12[0], ==, 0xa5);

  g_assert_cmpuint (frame.stored_crc[0], ==, 0x11);
  g_assert_cmpuint (frame.stored_crc[1], ==, 0x22);
  g_assert_cmpuint (frame.stored_crc[2], ==, 0x33);
  g_assert_cmpuint (frame.stored_crc[3], ==, 0x44);
}

static void
test_wrong_command (void)
{
  guint8 data[GOODIX5135_IMAGE_TOTAL_LENGTH];
  Goodix5135ImageFrame frame = { 0 };

  build_valid_image_frame (data);
  data[0] = 0x21;

  g_assert_false (goodix5135_parse_image_frame (
                    data,
                    sizeof (data),
                    &frame));
}

static void
test_wrong_declared_length (void)
{
  guint8 data[GOODIX5135_IMAGE_TOTAL_LENGTH];
  Goodix5135ImageFrame frame = { 0 };

  build_valid_image_frame (data);

  data[1] = 0x00;
  data[2] = 0x00;

  g_assert_false (goodix5135_parse_image_frame (
                    data,
                    sizeof (data),
                    &frame));
}

static void
test_wrong_trailer (void)
{
  guint8 data[GOODIX5135_IMAGE_TOTAL_LENGTH];
  Goodix5135ImageFrame frame = { 0 };

  build_valid_image_frame (data);

  data[sizeof (data) - 1] = 0x00;

  g_assert_false (goodix5135_parse_image_frame (
                    data,
                    sizeof (data),
                    &frame));
}

static void
test_truncated_frame (void)
{
  guint8 data[GOODIX5135_IMAGE_TOTAL_LENGTH];
  Goodix5135ImageFrame frame = { 0 };

  build_valid_image_frame (data);

  g_assert_false (goodix5135_parse_image_frame (
                    data,
                    sizeof (data) - 1,
                    &frame));
}

static void
test_oversized_frame (void)
{
  guint8 data[GOODIX5135_IMAGE_TOTAL_LENGTH + 1];
  Goodix5135ImageFrame frame = { 0 };

  memset (data, 0, sizeof (data));
  build_valid_image_frame (data);

  g_assert_false (goodix5135_parse_image_frame (
                    data,
                    sizeof (data),
                    &frame));
}

static void
test_null_arguments (void)
{
  guint8 data[GOODIX5135_IMAGE_TOTAL_LENGTH];
  Goodix5135ImageFrame frame = { 0 };

  build_valid_image_frame (data);

  g_assert_false (goodix5135_parse_image_frame (
                    NULL,
                    sizeof (data),
                    &frame));

  g_assert_false (goodix5135_parse_image_frame (
                    data,
                    sizeof (data),
                    NULL));
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/goodix5135/proto/image-frame/valid",
                   test_valid_image_frame);

  g_test_add_func ("/goodix5135/proto/image-frame/wrong-command",
                   test_wrong_command);

  g_test_add_func ("/goodix5135/proto/image-frame/wrong-length",
                   test_wrong_declared_length);

  g_test_add_func ("/goodix5135/proto/image-frame/wrong-trailer",
                   test_wrong_trailer);

  g_test_add_func ("/goodix5135/proto/image-frame/truncated",
                   test_truncated_frame);

  g_test_add_func ("/goodix5135/proto/image-frame/oversized",
                   test_oversized_frame);

  g_test_add_func ("/goodix5135/proto/image-frame/null-arguments",
                   test_null_arguments);

  return g_test_run ();
}
