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

static void
test_firmware_version_request (void)
{
  static const guint8 expected[] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xa8, 0x03, 0x00, 0x00, 0x00, 0xff,
  };

  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;
  gsize i;

  memset (packet, 0x5a, sizeof (packet));

  g_assert_true (
    goodix5135_build_firmware_version_request (
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_cmpuint (
    logical_length,
    ==,
    GOODIX5135_FIRMWARE_REQUEST_LENGTH);

  g_assert_cmpuint (
    sizeof (expected),
    ==,
    GOODIX5135_FIRMWARE_REQUEST_LENGTH);

  g_assert_cmpint (
    memcmp (packet, expected, sizeof (expected)),
    ==,
    0);

  for (i = sizeof (expected); i < sizeof (packet); i++)
    g_assert_cmpuint (packet[i], ==, 0x00);
}

static void
test_firmware_version_request_arguments (void)
{
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  g_assert_false (
    goodix5135_build_firmware_version_request (
      NULL,
      sizeof (packet),
      &logical_length));

  g_assert_false (
    goodix5135_build_firmware_version_request (
      packet,
      sizeof (packet) - 1,
      &logical_length));

  g_assert_false (
    goodix5135_build_firmware_version_request (
      packet,
      sizeof (packet),
      NULL));
}

static void
test_firmware_ack_success (void)
{
  static const guint8 ack[] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xb0, 0x03, 0x00, 0xa8, 0x03, 0x4c,
  };

  g_assert_true (
    goodix5135_parse_firmware_version_ack (
      ack,
      sizeof (ack)));
}

static void
test_firmware_ack_success_padded (void)
{
  static const guint8 logical[] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xb0, 0x03, 0x00, 0xa8, 0x03, 0x4c,
  };

  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];

  memset (packet, 0, sizeof (packet));
  memcpy (packet, logical, sizeof (logical));

  g_assert_true (
    goodix5135_parse_firmware_version_ack (
      packet,
      sizeof (packet)));
}

static void
test_firmware_ack_negative (void)
{
  /*
   * a8 01 is structurally valid but success bit 1 is clear.
   */
  static const guint8 ack[] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xb0, 0x03, 0x00, 0xa8, 0x01, 0x4e,
  };

  g_assert_false (
    goodix5135_parse_firmware_version_ack (
      ack,
      sizeof (ack)));
}

static void
test_firmware_ack_wrong_command (void)
{
  /*
   * Valid successful ACK framing, but acknowledges 0xa6 instead of 0xa8.
   */
  static const guint8 ack[] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xb0, 0x03, 0x00, 0xa6, 0x03, 0x4e,
  };

  g_assert_false (
    goodix5135_parse_firmware_version_ack (
      ack,
      sizeof (ack)));
}

static void
test_firmware_ack_bad_checksum (void)
{
  guint8 ack[] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xb0, 0x03, 0x00, 0xa8, 0x03, 0x4c,
  };

  ack[sizeof (ack) - 1] ^= 0x01;

  g_assert_false (
    goodix5135_parse_firmware_version_ack (
      ack,
      sizeof (ack)));
}

static void
test_firmware_ack_truncated (void)
{
  static const guint8 ack[] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xb0, 0x03, 0x00, 0xa8, 0x03, 0x4c,
  };

  g_assert_false (
    goodix5135_parse_firmware_version_ack (
      ack,
      sizeof (ack) - 1));
}

static void
test_firmware_response_valid (void)
{
  static const guint8 response[] = {
    0xa0, 0x16, 0x00, 0xb6,
    0xa8, 0x13, 0x00,
    0x47, 0x46, 0x5f, 0x54, 0x45, 0x53, 0x54, 0x5f,
    0x41, 0x50, 0x50, 0x5f,
    0x30, 0x30, 0x30, 0x30, 0x30,
    0x00,
    0x34,
  };

  static const guint8 expected[] = "GF_TEST_APP_00000";

  const guint8 *firmware = NULL;
  gsize firmware_length = 0;

  g_assert_true (
    goodix5135_parse_firmware_version_response (
      response,
      sizeof (response),
      &firmware,
      &firmware_length));

  g_assert_nonnull (firmware);

  g_assert_cmpuint (
    firmware_length,
    ==,
    sizeof (expected) - 1);

  g_assert_cmpint (
    memcmp (
      firmware,
      expected,
      firmware_length),
    ==,
    0);
}

static void
test_firmware_response_valid_padded (void)
{
  static const guint8 logical[] = {
    0xa0, 0x16, 0x00, 0xb6,
    0xa8, 0x13, 0x00,
    0x47, 0x46, 0x5f, 0x54, 0x45, 0x53, 0x54, 0x5f,
    0x41, 0x50, 0x50, 0x5f,
    0x30, 0x30, 0x30, 0x30, 0x30,
    0x00,
    0x34,
  };

  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  const guint8 *firmware = NULL;
  gsize firmware_length = 0;

  memset (packet, 0, sizeof (packet));
  memcpy (packet, logical, sizeof (logical));

  g_assert_true (
    goodix5135_parse_firmware_version_response (
      packet,
      sizeof (packet),
      &firmware,
      &firmware_length));

  g_assert_cmpuint (firmware_length, ==, 17);
}

static void
test_firmware_response_wrong_command (void)
{
  guint8 response[] = {
    0xa0, 0x16, 0x00, 0xb6,
    0xa8, 0x13, 0x00,
    0x47, 0x46, 0x5f, 0x54, 0x45, 0x53, 0x54, 0x5f,
    0x41, 0x50, 0x50, 0x5f,
    0x30, 0x30, 0x30, 0x30, 0x30,
    0x00,
    0x34,
  };

  const guint8 *firmware = NULL;
  gsize firmware_length = 0;

  /*
   * Change command to 0xa6 and recompute the inner checksum so this
   * tests command rejection, not merely checksum rejection.
   */
  response[4] = 0xa6;
  response[sizeof (response) - 1] = 0x36;

  g_assert_false (
    goodix5135_parse_firmware_version_response (
      response,
      sizeof (response),
      &firmware,
      &firmware_length));
}

static void
test_firmware_response_bad_outer_checksum (void)
{
  guint8 response[] = {
    0xa0, 0x16, 0x00, 0xb6,
    0xa8, 0x13, 0x00,
    0x47, 0x46, 0x5f, 0x54, 0x45, 0x53, 0x54, 0x5f,
    0x41, 0x50, 0x50, 0x5f,
    0x30, 0x30, 0x30, 0x30, 0x30,
    0x00,
    0x34,
  };

  const guint8 *firmware = NULL;
  gsize firmware_length = 0;

  response[3] ^= 0x01;

  g_assert_false (
    goodix5135_parse_firmware_version_response (
      response,
      sizeof (response),
      &firmware,
      &firmware_length));
}

static void
test_firmware_response_bad_inner_checksum (void)
{
  guint8 response[] = {
    0xa0, 0x16, 0x00, 0xb6,
    0xa8, 0x13, 0x00,
    0x47, 0x46, 0x5f, 0x54, 0x45, 0x53, 0x54, 0x5f,
    0x41, 0x50, 0x50, 0x5f,
    0x30, 0x30, 0x30, 0x30, 0x30,
    0x00,
    0x34,
  };

  const guint8 *firmware = NULL;
  gsize firmware_length = 0;

  response[sizeof (response) - 1] ^= 0x01;

  g_assert_false (
    goodix5135_parse_firmware_version_response (
      response,
      sizeof (response),
      &firmware,
      &firmware_length));
}

static void
test_firmware_response_truncated (void)
{
  static const guint8 response[] = {
    0xa0, 0x16, 0x00, 0xb6,
    0xa8, 0x13, 0x00,
    0x47, 0x46, 0x5f, 0x54, 0x45, 0x53, 0x54, 0x5f,
    0x41, 0x50, 0x50, 0x5f,
    0x30, 0x30, 0x30, 0x30, 0x30,
    0x00,
    0x34,
  };

  const guint8 *firmware = NULL;
  gsize firmware_length = 0;

  g_assert_false (
    goodix5135_parse_firmware_version_response (
      response,
      sizeof (response) - 1,
      &firmware,
      &firmware_length));
}

static void
test_firmware_response_arguments (void)
{
  static const guint8 response[] = {
    0xa0, 0x16, 0x00, 0xb6,
    0xa8, 0x13, 0x00,
    0x47, 0x46, 0x5f, 0x54, 0x45, 0x53, 0x54, 0x5f,
    0x41, 0x50, 0x50, 0x5f,
    0x30, 0x30, 0x30, 0x30, 0x30,
    0x00,
    0x34,
  };

  const guint8 *firmware = NULL;
  gsize firmware_length = 0;

  g_assert_false (
    goodix5135_parse_firmware_version_response (
      NULL,
      sizeof (response),
      &firmware,
      &firmware_length));

  g_assert_false (
    goodix5135_parse_firmware_version_response (
      response,
      sizeof (response),
      NULL,
      &firmware_length));

  g_assert_false (
    goodix5135_parse_firmware_version_response (
      response,
      sizeof (response),
      &firmware,
      NULL));

  g_assert_false (
    goodix5135_parse_firmware_version_ack (
      NULL,
      sizeof (response)));
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

  g_test_add_func ("/goodix5135/proto/firmware-request/vector",
                   test_firmware_version_request);

  g_test_add_func ("/goodix5135/proto/firmware-request/arguments",
                   test_firmware_version_request_arguments);


  g_test_add_func ("/goodix5135/proto/firmware-ack/success",
                   test_firmware_ack_success);

  g_test_add_func ("/goodix5135/proto/firmware-ack/success-padded",
                   test_firmware_ack_success_padded);

  g_test_add_func ("/goodix5135/proto/firmware-ack/negative",
                   test_firmware_ack_negative);

  g_test_add_func ("/goodix5135/proto/firmware-ack/wrong-command",
                   test_firmware_ack_wrong_command);

  g_test_add_func ("/goodix5135/proto/firmware-ack/bad-checksum",
                   test_firmware_ack_bad_checksum);

  g_test_add_func ("/goodix5135/proto/firmware-ack/truncated",
                   test_firmware_ack_truncated);

  g_test_add_func ("/goodix5135/proto/firmware-response/valid",
                   test_firmware_response_valid);

  g_test_add_func ("/goodix5135/proto/firmware-response/valid-padded",
                   test_firmware_response_valid_padded);

  g_test_add_func ("/goodix5135/proto/firmware-response/wrong-command",
                   test_firmware_response_wrong_command);

  g_test_add_func ("/goodix5135/proto/firmware-response/bad-outer-checksum",
                   test_firmware_response_bad_outer_checksum);

  g_test_add_func ("/goodix5135/proto/firmware-response/bad-inner-checksum",
                   test_firmware_response_bad_inner_checksum);

  g_test_add_func ("/goodix5135/proto/firmware-response/truncated",
                   test_firmware_response_truncated);

  g_test_add_func ("/goodix5135/proto/firmware-response/arguments",
                   test_firmware_response_arguments);

  return g_test_run ();
}
