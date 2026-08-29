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
test_firmware_ack_bit1_clear (void)
{
  /*
   * Reference-compatible ACK:
   *
   * bit 0 is set, so the ACK is structurally valid.
   * bit 1 is clear, but the reference firmware_version() path ignores
   * the boolean returned for that bit.
   */
  static const guint8 ack[] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xb0, 0x03, 0x00, 0xa8, 0x01, 0x4e,
  };

  g_assert_true (
    goodix5135_parse_firmware_version_ack (
      ack,
      sizeof (ack)));
}

static void
test_firmware_ack_extended_payload (void)
{
  /*
   * Reference-compatible extended ACK.
   *
   * decode_ack() consumes only the first two payload bytes:
   *   payload[0] = acknowledged command 0xa8
   *   payload[1] = status 0x01
   *
   * Additional payload bytes must not invalidate an otherwise valid ACK.
   */
  static const guint8 ack[] = {
    0xa0, 0x08, 0x00, 0xa8,
    0xb0, 0x05, 0x00,
    0xa8, 0x01, 0x12, 0x34,
    0x06,
  };

  g_assert_true (
    goodix5135_parse_firmware_version_ack (
      ack,
      sizeof (ack)));
}

static void
test_firmware_ack_negative (void)
{
  /*
   * bit 0 is clear, so decode_ack() semantics classify this ACK as invalid.
   */
  static const guint8 ack[] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xb0, 0x03, 0x00, 0xa8, 0x00, 0x4f,
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

static const guint8 firmware_success_ack[] = {
  0xa0, 0x06, 0x00, 0xa6,
  0xb0, 0x03, 0x00, 0xa8, 0x03, 0x4c,
};

static const guint8 firmware_extended_ack[] = {
  0xa0, 0x08, 0x00, 0xa8,
  0xb0, 0x05, 0x00,
  0xa8, 0x01, 0x12, 0x34,
  0x06,
};

static const guint8 firmware_negative_ack[] = {
  0xa0, 0x06, 0x00, 0xa6,
  0xb0, 0x03, 0x00, 0xa8, 0x00, 0x4f,
};

static const guint8 firmware_valid_response[] = {
  0xa0, 0x16, 0x00, 0xb6,
  0xa8, 0x13, 0x00,
  0x47, 0x46, 0x5f, 0x54, 0x45, 0x53, 0x54, 0x5f,
  0x41, 0x50, 0x50, 0x5f,
  0x30, 0x30, 0x30, 0x30, 0x30,
  0x00,
  0x34,
};

static void
test_firmware_transaction_happy_path (void)
{
  Goodix5135FirmwareTransaction transaction;
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;
  const guint8 *firmware = NULL;
  gsize firmware_length = 0;

  goodix5135_firmware_transaction_init (
    &transaction);

  g_assert_cmpuint (
    transaction.state,
    ==,
    GOODIX5135_FIRMWARE_TRANSACTION_IDLE);

  g_assert_true (
    goodix5135_firmware_transaction_begin (
      &transaction,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_cmpuint (
    transaction.state,
    ==,
    GOODIX5135_FIRMWARE_TRANSACTION_WAIT_OUT);

  g_assert_cmpuint (
    logical_length,
    ==,
    GOODIX5135_FIRMWARE_REQUEST_LENGTH);

  g_assert_true (
    goodix5135_firmware_transaction_out_complete (
      &transaction,
      TRUE));

  g_assert_cmpuint (
    transaction.state,
    ==,
    GOODIX5135_FIRMWARE_TRANSACTION_WAIT_ACK);

  g_assert_true (
    goodix5135_firmware_transaction_ack_complete (
      &transaction,
      TRUE,
      firmware_success_ack,
      sizeof (firmware_success_ack)));

  g_assert_cmpuint (
    transaction.state,
    ==,
    GOODIX5135_FIRMWARE_TRANSACTION_WAIT_RESPONSE);

  g_assert_true (
    goodix5135_firmware_transaction_response_complete (
      &transaction,
      TRUE,
      firmware_valid_response,
      sizeof (firmware_valid_response),
      &firmware,
      &firmware_length));

  g_assert_cmpuint (
    transaction.state,
    ==,
    GOODIX5135_FIRMWARE_TRANSACTION_DONE);

  g_assert_nonnull (firmware);
  g_assert_cmpuint (firmware_length, ==, 17);

  g_assert_cmpint (
    memcmp (
      firmware,
      "GF_TEST_APP_00000",
      firmware_length),
    ==,
    0);
}

static void
test_firmware_transaction_extended_ack (void)
{
  Goodix5135FirmwareTransaction transaction;
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  goodix5135_firmware_transaction_init (
    &transaction);

  g_assert_true (
    goodix5135_firmware_transaction_begin (
      &transaction,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_true (
    goodix5135_firmware_transaction_out_complete (
      &transaction,
      TRUE));

  g_assert_cmpuint (
    transaction.state,
    ==,
    GOODIX5135_FIRMWARE_TRANSACTION_WAIT_ACK);

  g_assert_true (
    goodix5135_firmware_transaction_ack_complete (
      &transaction,
      TRUE,
      firmware_extended_ack,
      sizeof (firmware_extended_ack)));

  g_assert_cmpuint (
    transaction.state,
    ==,
    GOODIX5135_FIRMWARE_TRANSACTION_WAIT_RESPONSE);
}

static void
test_firmware_transaction_out_transport_failure (void)
{
  Goodix5135FirmwareTransaction transaction;
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  goodix5135_firmware_transaction_init (
    &transaction);

  g_assert_true (
    goodix5135_firmware_transaction_begin (
      &transaction,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_false (
    goodix5135_firmware_transaction_out_complete (
      &transaction,
      FALSE));

  g_assert_cmpuint (
    transaction.state,
    ==,
    GOODIX5135_FIRMWARE_TRANSACTION_FAILED);
}

static void
test_firmware_transaction_ack_transport_failure (void)
{
  Goodix5135FirmwareTransaction transaction;
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  goodix5135_firmware_transaction_init (
    &transaction);

  g_assert_true (
    goodix5135_firmware_transaction_begin (
      &transaction,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_true (
    goodix5135_firmware_transaction_out_complete (
      &transaction,
      TRUE));

  g_assert_false (
    goodix5135_firmware_transaction_ack_complete (
      &transaction,
      FALSE,
      firmware_success_ack,
      sizeof (firmware_success_ack)));

  g_assert_cmpuint (
    transaction.state,
    ==,
    GOODIX5135_FIRMWARE_TRANSACTION_FAILED);
}

static void
test_firmware_transaction_negative_ack (void)
{
  Goodix5135FirmwareTransaction transaction;
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  goodix5135_firmware_transaction_init (
    &transaction);

  g_assert_true (
    goodix5135_firmware_transaction_begin (
      &transaction,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_true (
    goodix5135_firmware_transaction_out_complete (
      &transaction,
      TRUE));

  g_assert_false (
    goodix5135_firmware_transaction_ack_complete (
      &transaction,
      TRUE,
      firmware_negative_ack,
      sizeof (firmware_negative_ack)));

  g_assert_cmpuint (
    transaction.state,
    ==,
    GOODIX5135_FIRMWARE_TRANSACTION_FAILED);
}

static void
test_firmware_transaction_response_transport_failure (void)
{
  Goodix5135FirmwareTransaction transaction;
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;
  const guint8 *firmware = (const guint8 *) 0x1;
  gsize firmware_length = 123;

  goodix5135_firmware_transaction_init (
    &transaction);

  g_assert_true (
    goodix5135_firmware_transaction_begin (
      &transaction,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_true (
    goodix5135_firmware_transaction_out_complete (
      &transaction,
      TRUE));

  g_assert_true (
    goodix5135_firmware_transaction_ack_complete (
      &transaction,
      TRUE,
      firmware_success_ack,
      sizeof (firmware_success_ack)));

  g_assert_false (
    goodix5135_firmware_transaction_response_complete (
      &transaction,
      FALSE,
      firmware_valid_response,
      sizeof (firmware_valid_response),
      &firmware,
      &firmware_length));

  g_assert_cmpuint (
    transaction.state,
    ==,
    GOODIX5135_FIRMWARE_TRANSACTION_FAILED);

  g_assert_null (firmware);
  g_assert_cmpuint (firmware_length, ==, 0);
}

static void
test_firmware_transaction_invalid_response (void)
{
  Goodix5135FirmwareTransaction transaction;
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  guint8 response[sizeof (firmware_valid_response)];
  gsize logical_length = 0;
  const guint8 *firmware = NULL;
  gsize firmware_length = 0;

  memcpy (
    response,
    firmware_valid_response,
    sizeof (response));

  response[sizeof (response) - 1] ^= 0x01;

  goodix5135_firmware_transaction_init (
    &transaction);

  g_assert_true (
    goodix5135_firmware_transaction_begin (
      &transaction,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_true (
    goodix5135_firmware_transaction_out_complete (
      &transaction,
      TRUE));

  g_assert_true (
    goodix5135_firmware_transaction_ack_complete (
      &transaction,
      TRUE,
      firmware_success_ack,
      sizeof (firmware_success_ack)));

  g_assert_false (
    goodix5135_firmware_transaction_response_complete (
      &transaction,
      TRUE,
      response,
      sizeof (response),
      &firmware,
      &firmware_length));

  g_assert_cmpuint (
    transaction.state,
    ==,
    GOODIX5135_FIRMWARE_TRANSACTION_FAILED);
}

static void
test_firmware_transaction_invalid_order (void)
{
  Goodix5135FirmwareTransaction transaction;

  goodix5135_firmware_transaction_init (
    &transaction);

  /*
   * ACK cannot legally arrive before request construction and OUT completion.
   * Fail closed rather than attempting recovery.
   */
  g_assert_false (
    goodix5135_firmware_transaction_ack_complete (
      &transaction,
      TRUE,
      firmware_success_ack,
      sizeof (firmware_success_ack)));

  g_assert_cmpuint (
    transaction.state,
    ==,
    GOODIX5135_FIRMWARE_TRANSACTION_FAILED);
}

static void
test_firmware_transaction_begin_failure (void)
{
  Goodix5135FirmwareTransaction transaction;
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH - 1];
  gsize logical_length = 0;

  goodix5135_firmware_transaction_init (
    &transaction);

  g_assert_false (
    goodix5135_firmware_transaction_begin (
      &transaction,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_cmpuint (
    transaction.state,
    ==,
    GOODIX5135_FIRMWARE_TRANSACTION_FAILED);
}


static void
test_register_read_request (void)
{
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  static const guint8 expected[] = {
    0xa0, 0x08, 0x00, 0xa8,
    0x82, 0x05, 0x00,
    0x00, 0x00, 0x00, 0x04,
    0x1f,
  };

  g_assert_true (
    goodix5135_build_read_sensor_register_request (
      0x0000,
      4,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_cmpuint (
    logical_length,
    ==,
    GOODIX5135_REGISTER_READ_REQUEST_LENGTH);

  g_assert_cmpmem (
    packet,
    logical_length,
    expected,
    sizeof (expected));

  for (gsize i = logical_length;
       i < sizeof (packet);
       i++)
    g_assert_cmpuint (packet[i], ==, 0);
}


static void
test_register_read_request_0220 (void)
{
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  static const guint8 expected[] = {
    0xa0, 0x08, 0x00, 0xa8,
    0x82, 0x05, 0x00,
    0x00, 0x20, 0x02, 0x02,
    0xff,
  };

  g_assert_true (
    goodix5135_build_read_sensor_register_request (
      0x0220,
      2,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_cmpmem (
    packet,
    logical_length,
    expected,
    sizeof (expected));
}


static void
test_register_read_request_rejects_zero_length (void)
{
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 123;

  g_assert_false (
    goodix5135_build_read_sensor_register_request (
      0x0000,
      0,
      packet,
      sizeof (packet),
      &logical_length));
}


static void
test_register_read_ack (void)
{
  guint8 ack[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xb0, 0x03, 0x00,
    0x82, 0x01,
    0x74,
  };

  g_assert_true (
    goodix5135_parse_read_sensor_register_ack (
      ack,
      sizeof (ack)));

  /*
   * Status bit 0 clear -> invalid ACK.
   * Recompute the protocol checksum for payload status 0x00.
   */
  ack[8] = 0x00;
  ack[9] = 0x75;

  g_assert_false (
    goodix5135_parse_read_sensor_register_ack (
      ack,
      sizeof (ack)));
}


static void
test_register_read_response (void)
{
  const guint8 *value = NULL;
  gsize value_length = 0;

  static const guint8 expected_chip_id[] = {
    0xa2, 0x04, 0x25, 0x00,
  };

  guint8 response[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x08, 0x00, 0xa8,
    0x82, 0x05, 0x00,
    0xa2, 0x04, 0x25, 0x00,
    0x58,
  };

  g_assert_true (
    goodix5135_parse_read_sensor_register_response (
      response,
      sizeof (response),
      sizeof (expected_chip_id),
      &value,
      &value_length));

  g_assert_nonnull (value);
  g_assert_cmpuint (
    value_length,
    ==,
    sizeof (expected_chip_id));

  g_assert_cmpmem (
    value,
    value_length,
    expected_chip_id,
    sizeof (expected_chip_id));

  value = NULL;
  value_length = 0;

  g_assert_false (
    goodix5135_parse_read_sensor_register_response (
      response,
      sizeof (response),
      sizeof (expected_chip_id) + 1,
      &value,
      &value_length));
}



static void
test_register_transaction_happy_path (void)
{
  Goodix5135RegisterReadTransaction transaction;
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;
  const guint8 *value = NULL;
  gsize value_length = 0;

  static const guint8 ack[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xb0, 0x03, 0x00,
    0x82, 0x01, 0x74,
  };

  static const guint8 response[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x06, 0x00, 0xa6,
    0x82, 0x03, 0x00,
    0x08, 0x08, 0x15,
  };

  goodix5135_register_read_transaction_init (&transaction);

  g_assert_true (
    goodix5135_register_read_transaction_begin (
      &transaction,
      0x0220,
      2,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_REGISTER_READ_TRANSACTION_WAIT_OUT);

  g_assert_cmpuint (
    transaction.expected_length,
    ==,
    2);

  g_assert_true (
    goodix5135_register_read_transaction_out_complete (
      &transaction,
      TRUE));

  g_assert_true (
    goodix5135_register_read_transaction_ack_complete (
      &transaction,
      TRUE,
      ack,
      sizeof (ack)));

  g_assert_true (
    goodix5135_register_read_transaction_response_complete (
      &transaction,
      TRUE,
      response,
      sizeof (response),
      &value,
      &value_length));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_REGISTER_READ_TRANSACTION_DONE);

  g_assert_nonnull (value);
  g_assert_cmpuint (value_length, ==, 2);
  g_assert_cmpuint (value[0], ==, 0x08);
  g_assert_cmpuint (value[1], ==, 0x08);
}


static void
test_register_transaction_out_failure (void)
{
  Goodix5135RegisterReadTransaction transaction;
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  goodix5135_register_read_transaction_init (&transaction);

  g_assert_true (
    goodix5135_register_read_transaction_begin (
      &transaction,
      0x0220,
      2,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_false (
    goodix5135_register_read_transaction_out_complete (
      &transaction,
      FALSE));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_REGISTER_READ_TRANSACTION_FAILED);
}


static void
test_register_transaction_ack_failure (void)
{
  Goodix5135RegisterReadTransaction transaction;
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  goodix5135_register_read_transaction_init (&transaction);

  g_assert_true (
    goodix5135_register_read_transaction_begin (
      &transaction,
      0x0220,
      2,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_true (
    goodix5135_register_read_transaction_out_complete (
      &transaction,
      TRUE));

  g_assert_false (
    goodix5135_register_read_transaction_ack_complete (
      &transaction,
      FALSE,
      NULL,
      0));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_REGISTER_READ_TRANSACTION_FAILED);
}


static void
test_register_transaction_negative_ack (void)
{
  Goodix5135RegisterReadTransaction transaction;
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  static const guint8 ack[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xb0, 0x03, 0x00,
    0x82, 0x00, 0x75,
  };

  goodix5135_register_read_transaction_init (&transaction);

  g_assert_true (
    goodix5135_register_read_transaction_begin (
      &transaction,
      0x0220,
      2,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_true (
    goodix5135_register_read_transaction_out_complete (
      &transaction,
      TRUE));

  g_assert_false (
    goodix5135_register_read_transaction_ack_complete (
      &transaction,
      TRUE,
      ack,
      sizeof (ack)));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_REGISTER_READ_TRANSACTION_FAILED);
}


static void
test_register_transaction_response_failure (void)
{
  Goodix5135RegisterReadTransaction transaction;
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;
  const guint8 *value = NULL;
  gsize value_length = 0;

  static const guint8 ack[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xb0, 0x03, 0x00,
    0x82, 0x01, 0x74,
  };

  goodix5135_register_read_transaction_init (&transaction);

  g_assert_true (
    goodix5135_register_read_transaction_begin (
      &transaction,
      0x0220,
      2,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_true (
    goodix5135_register_read_transaction_out_complete (
      &transaction,
      TRUE));

  g_assert_true (
    goodix5135_register_read_transaction_ack_complete (
      &transaction,
      TRUE,
      ack,
      sizeof (ack)));

  g_assert_false (
    goodix5135_register_read_transaction_response_complete (
      &transaction,
      FALSE,
      NULL,
      0,
      &value,
      &value_length));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_REGISTER_READ_TRANSACTION_FAILED);
}


static void
test_register_transaction_short_response (void)
{
  Goodix5135RegisterReadTransaction transaction;
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;
  const guint8 *value = NULL;
  gsize value_length = 0;

  static const guint8 ack[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xb0, 0x03, 0x00,
    0x82, 0x01, 0x74,
  };

  static const guint8 response[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x05, 0x00, 0xa5,
    0x82, 0x02, 0x00,
    0x08, 0x1e,
  };

  goodix5135_register_read_transaction_init (&transaction);

  g_assert_true (
    goodix5135_register_read_transaction_begin (
      &transaction,
      0x0220,
      2,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_true (
    goodix5135_register_read_transaction_out_complete (
      &transaction,
      TRUE));

  g_assert_true (
    goodix5135_register_read_transaction_ack_complete (
      &transaction,
      TRUE,
      ack,
      sizeof (ack)));

  g_assert_false (
    goodix5135_register_read_transaction_response_complete (
      &transaction,
      TRUE,
      response,
      sizeof (response),
      &value,
      &value_length));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_REGISTER_READ_TRANSACTION_FAILED);
}


static void
test_register_transaction_invalid_order (void)
{
  Goodix5135RegisterReadTransaction transaction;

  goodix5135_register_read_transaction_init (&transaction);

  g_assert_false (
    goodix5135_register_read_transaction_out_complete (
      &transaction,
      TRUE));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_REGISTER_READ_TRANSACTION_FAILED);
}


static void
test_register_transaction_begin_failure (void)
{
  Goodix5135RegisterReadTransaction transaction;
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  goodix5135_register_read_transaction_init (&transaction);

  g_assert_false (
    goodix5135_register_read_transaction_begin (
      &transaction,
      0x0220,
      0,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_REGISTER_READ_TRANSACTION_FAILED);
}



static void
test_mcu_state_request_vector (void)
{
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  static const guint8 expected[] = {
    0xa0, 0x05, 0x00, 0xa5,
    0xae, 0x02, 0x00,
    0x55,
    0xa5,
  };

  g_assert_true (
    goodix5135_build_mcu_state_request (
      0x55,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_cmpuint (
    logical_length,
    ==,
    GOODIX5135_MCU_STATE_REQUEST_LENGTH);

  g_assert_cmpmem (
    packet,
    logical_length,
    expected,
    sizeof (expected));

  for (gsize i = logical_length;
       i < sizeof (packet);
       i++)
    g_assert_cmpuint (packet[i], ==, 0);
}


static void
test_mcu_state_request_arguments (void)
{
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  g_assert_false (
    goodix5135_build_mcu_state_request (
      0x55,
      NULL,
      sizeof (packet),
      &logical_length));

  g_assert_false (
    goodix5135_build_mcu_state_request (
      0x55,
      packet,
      8,
      &logical_length));

  g_assert_false (
    goodix5135_build_mcu_state_request (
      0x55,
      packet,
      sizeof (packet),
      NULL));
}


static void
test_mcu_state_ack (void)
{
  guint8 ack[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xb0, 0x03, 0x00,
    0xae, 0x01,
    0x48,
  };

  g_assert_true (
    goodix5135_parse_mcu_state_ack (
      ack,
      sizeof (ack)));

  ack[8] = 0x00;
  ack[9] = 0x49;

  g_assert_false (
    goodix5135_parse_mcu_state_ack (
      ack,
      sizeof (ack)));
}


static void
test_mcu_state_response (void)
{
  const guint8 *state_data = NULL;
  gsize state_length = 0;

  /*
   * Structurally valid synthetic response with one payload byte.
   * We deliberately do not attach meaning to this synthetic state byte.
   */
  static const guint8 response[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x05, 0x00, 0xa5,
    0xae, 0x02, 0x00,
    0x00,
    0xfa,
  };

  g_assert_true (
    goodix5135_parse_mcu_state_response (
      response,
      sizeof (response),
      &state_data,
      &state_length));

  g_assert_nonnull (state_data);
  g_assert_cmpuint (state_length, ==, 1);
  g_assert_cmpuint (state_data[0], ==, 0x00);
}


static void
test_mcu_state_transaction_happy_path (void)
{
  Goodix5135McuStateTransaction transaction;
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  const guint8 *state_data = NULL;
  gsize state_length = 0;

  static const guint8 ack[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xb0, 0x03, 0x00,
    0xae, 0x01,
    0x48,
  };

  static const guint8 response[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x05, 0x00, 0xa5,
    0xae, 0x02, 0x00,
    0x00,
    0xfa,
  };

  goodix5135_mcu_state_transaction_init (
    &transaction);

  g_assert_true (
    goodix5135_mcu_state_transaction_begin (
      &transaction,
      0x55,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_MCU_STATE_TRANSACTION_WAIT_OUT);

  g_assert_true (
    goodix5135_mcu_state_transaction_out_complete (
      &transaction,
      TRUE));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_MCU_STATE_TRANSACTION_WAIT_ACK);

  g_assert_true (
    goodix5135_mcu_state_transaction_ack_complete (
      &transaction,
      TRUE,
      ack,
      sizeof (ack)));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_MCU_STATE_TRANSACTION_WAIT_RESPONSE);

  g_assert_true (
    goodix5135_mcu_state_transaction_response_complete (
      &transaction,
      TRUE,
      response,
      sizeof (response),
      &state_data,
      &state_length));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_MCU_STATE_TRANSACTION_DONE);

  g_assert_nonnull (state_data);
  g_assert_cmpuint (state_length, ==, 1);
}


static void
test_mcu_state_transaction_out_failure (void)
{
  Goodix5135McuStateTransaction transaction;
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  goodix5135_mcu_state_transaction_init (
    &transaction);

  g_assert_true (
    goodix5135_mcu_state_transaction_begin (
      &transaction,
      0x55,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_false (
    goodix5135_mcu_state_transaction_out_complete (
      &transaction,
      FALSE));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_MCU_STATE_TRANSACTION_FAILED);
}


static void
test_mcu_state_transaction_ack_failure (void)
{
  Goodix5135McuStateTransaction transaction;
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  goodix5135_mcu_state_transaction_init (
    &transaction);

  g_assert_true (
    goodix5135_mcu_state_transaction_begin (
      &transaction,
      0x55,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_true (
    goodix5135_mcu_state_transaction_out_complete (
      &transaction,
      TRUE));

  g_assert_false (
    goodix5135_mcu_state_transaction_ack_complete (
      &transaction,
      FALSE,
      NULL,
      0));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_MCU_STATE_TRANSACTION_FAILED);
}


static void
test_mcu_state_transaction_response_failure (void)
{
  Goodix5135McuStateTransaction transaction;
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;
  const guint8 *state_data = NULL;
  gsize state_length = 0;

  static const guint8 ack[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xb0, 0x03, 0x00,
    0xae, 0x01,
    0x48,
  };

  goodix5135_mcu_state_transaction_init (
    &transaction);

  g_assert_true (
    goodix5135_mcu_state_transaction_begin (
      &transaction,
      0x55,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_true (
    goodix5135_mcu_state_transaction_out_complete (
      &transaction,
      TRUE));

  g_assert_true (
    goodix5135_mcu_state_transaction_ack_complete (
      &transaction,
      TRUE,
      ack,
      sizeof (ack)));

  g_assert_false (
    goodix5135_mcu_state_transaction_response_complete (
      &transaction,
      FALSE,
      NULL,
      0,
      &state_data,
      &state_length));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_MCU_STATE_TRANSACTION_FAILED);

  g_assert_null (state_data);
  g_assert_cmpuint (state_length, ==, 0);
}


static void
test_mcu_state_transaction_invalid_order (void)
{
  Goodix5135McuStateTransaction transaction;

  goodix5135_mcu_state_transaction_init (
    &transaction);

  g_assert_false (
    goodix5135_mcu_state_transaction_out_complete (
      &transaction,
      TRUE));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_MCU_STATE_TRANSACTION_FAILED);
}



static void
test_nop_request_vector (void)
{
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  static const guint8 expected[] = {
    0xa0, 0x08, 0x00, 0xa8,
    0x00, 0x05, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x88,
  };

  g_assert_true (
    goodix5135_build_nop_request (
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_cmpuint (
    logical_length,
    ==,
    GOODIX5135_NOP_REQUEST_LENGTH);

  g_assert_cmpmem (
    packet,
    logical_length,
    expected,
    sizeof (expected));

  for (gsize i = logical_length;
       i < sizeof (packet);
       i++)
    g_assert_cmpuint (
      packet[i],
      ==,
      0);
}


static void
test_nop_request_arguments (void)
{
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  g_assert_false (
    goodix5135_build_nop_request (
      NULL,
      sizeof (packet),
      &logical_length));

  g_assert_false (
    goodix5135_build_nop_request (
      packet,
      GOODIX5135_USB_PACKET_LENGTH - 1,
      &logical_length));

  g_assert_false (
    goodix5135_build_nop_request (
      packet,
      sizeof (packet),
      NULL));
}


static void
test_nop_ack (void)
{
  guint8 ack[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xb0, 0x03, 0x00,
    0x00, 0x01,
    0xf6,
  };

  g_assert_true (
    goodix5135_parse_nop_ack (
      ack,
      sizeof (ack)));

  ack[8] = 0x00;
  ack[9] = 0xf7;

  g_assert_false (
    goodix5135_parse_nop_ack (
      ack,
      sizeof (ack)));
}


static void
test_nop_transaction_ack_success (void)
{
  Goodix5135NopTransaction transaction;
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  static const guint8 ack[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xb0, 0x03, 0x00,
    0x00, 0x01,
    0xf6,
  };

  goodix5135_nop_transaction_init (
    &transaction);

  g_assert_true (
    goodix5135_nop_transaction_begin (
      &transaction,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_NOP_TRANSACTION_WAIT_OUT);

  g_assert_true (
    goodix5135_nop_transaction_out_complete (
      &transaction,
      TRUE));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_NOP_TRANSACTION_WAIT_OPTIONAL_ACK);

  g_assert_true (
    goodix5135_nop_transaction_reply_complete (
      &transaction,
      GOODIX5135_NOP_REPLY_RECEIVED,
      ack,
      sizeof (ack)));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_NOP_TRANSACTION_DONE);
}


static void
test_nop_transaction_timeout_success (void)
{
  Goodix5135NopTransaction transaction;
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  goodix5135_nop_transaction_init (
    &transaction);

  g_assert_true (
    goodix5135_nop_transaction_begin (
      &transaction,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_true (
    goodix5135_nop_transaction_out_complete (
      &transaction,
      TRUE));

  g_assert_true (
    goodix5135_nop_transaction_reply_complete (
      &transaction,
      GOODIX5135_NOP_REPLY_TIMEOUT,
      NULL,
      0));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_NOP_TRANSACTION_DONE);
}


static void
test_nop_transaction_bad_ack (void)
{
  Goodix5135NopTransaction transaction;
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  static const guint8 bad_ack[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xb0, 0x03, 0x00,
    0x00, 0x00,
    0xf7,
  };

  goodix5135_nop_transaction_init (
    &transaction);

  g_assert_true (
    goodix5135_nop_transaction_begin (
      &transaction,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_true (
    goodix5135_nop_transaction_out_complete (
      &transaction,
      TRUE));

  g_assert_false (
    goodix5135_nop_transaction_reply_complete (
      &transaction,
      GOODIX5135_NOP_REPLY_RECEIVED,
      bad_ack,
      sizeof (bad_ack)));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_NOP_TRANSACTION_FAILED);
}


static void
test_nop_transaction_transport_failure (void)
{
  Goodix5135NopTransaction transaction;
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  goodix5135_nop_transaction_init (
    &transaction);

  g_assert_true (
    goodix5135_nop_transaction_begin (
      &transaction,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_true (
    goodix5135_nop_transaction_out_complete (
      &transaction,
      TRUE));

  g_assert_false (
    goodix5135_nop_transaction_reply_complete (
      &transaction,
      GOODIX5135_NOP_REPLY_TRANSPORT_FAILURE,
      NULL,
      0));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_NOP_TRANSACTION_FAILED);
}


static void
test_nop_transaction_out_failure (void)
{
  Goodix5135NopTransaction transaction;
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  goodix5135_nop_transaction_init (
    &transaction);

  g_assert_true (
    goodix5135_nop_transaction_begin (
      &transaction,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_false (
    goodix5135_nop_transaction_out_complete (
      &transaction,
      FALSE));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_NOP_TRANSACTION_FAILED);
}


static void
test_nop_transaction_invalid_order (void)
{
  Goodix5135NopTransaction transaction;

  goodix5135_nop_transaction_init (
    &transaction);

  g_assert_false (
    goodix5135_nop_transaction_out_complete (
      &transaction,
      TRUE));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_NOP_TRANSACTION_FAILED);
}



static void
test_d4_request_vector (void)
{
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  static const guint8 expected[] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xd4, 0x03, 0x00,
    0x00, 0x00,
    0xd3,
  };

  g_assert_true (
    goodix5135_build_d4_request (
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_cmpuint (
    logical_length,
    ==,
    GOODIX5135_D4_REQUEST_LENGTH);

  g_assert_cmpmem (
    packet,
    logical_length,
    expected,
    sizeof (expected));

  for (gsize i = logical_length;
       i < sizeof (packet);
       i++)
    g_assert_cmpuint (
      packet[i],
      ==,
      0);
}


static void
test_d4_request_arguments (void)
{
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  g_assert_false (
    goodix5135_build_d4_request (
      NULL,
      sizeof (packet),
      &logical_length));

  g_assert_false (
    goodix5135_build_d4_request (
      packet,
      GOODIX5135_USB_PACKET_LENGTH - 1,
      &logical_length));

  g_assert_false (
    goodix5135_build_d4_request (
      packet,
      sizeof (packet),
      NULL));
}


static void
test_d4_ack_success (void)
{
  static const guint8 ack[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xb0, 0x03, 0x00,
    0xd4, 0x01,
    0x22,
  };

  g_assert_true (
    goodix5135_parse_d4_ack (
      ack,
      sizeof (ack)));
}


static void
test_d4_ack_negative (void)
{
  static const guint8 ack[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xb0, 0x03, 0x00,
    0xd4, 0x00,
    0x23,
  };

  g_assert_false (
    goodix5135_parse_d4_ack (
      ack,
      sizeof (ack)));
}


static void
test_d4_transaction_happy_path (void)
{
  Goodix5135TlsEstablishedTransaction transaction;

  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  static const guint8 ack[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xb0, 0x03, 0x00,
    0xd4, 0x01,
    0x22,
  };

  goodix5135_d4_transaction_init (
    &transaction);

  g_assert_true (
    goodix5135_d4_transaction_begin (
      &transaction,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_D4_TRANSACTION_WAIT_OUT);

  g_assert_true (
    goodix5135_d4_transaction_out_complete (
      &transaction,
      TRUE));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_D4_TRANSACTION_WAIT_ACK);

  g_assert_true (
    goodix5135_d4_transaction_ack_complete (
      &transaction,
      TRUE,
      ack,
      sizeof (ack)));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_D4_TRANSACTION_DONE);
}


static void
test_d4_transaction_out_failure (void)
{
  Goodix5135TlsEstablishedTransaction transaction;

  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  goodix5135_d4_transaction_init (
    &transaction);

  g_assert_true (
    goodix5135_d4_transaction_begin (
      &transaction,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_false (
    goodix5135_d4_transaction_out_complete (
      &transaction,
      FALSE));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_D4_TRANSACTION_FAILED);
}


static void
test_d4_transaction_ack_transport_failure (void)
{
  Goodix5135TlsEstablishedTransaction transaction;

  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  goodix5135_d4_transaction_init (
    &transaction);

  g_assert_true (
    goodix5135_d4_transaction_begin (
      &transaction,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_true (
    goodix5135_d4_transaction_out_complete (
      &transaction,
      TRUE));

  g_assert_false (
    goodix5135_d4_transaction_ack_complete (
      &transaction,
      FALSE,
      NULL,
      0));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_D4_TRANSACTION_FAILED);
}


static void
test_d4_transaction_bad_ack (void)
{
  Goodix5135TlsEstablishedTransaction transaction;

  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  static const guint8 bad_ack[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xb0, 0x03, 0x00,
    0xd4, 0x00,
    0x23,
  };

  goodix5135_d4_transaction_init (
    &transaction);

  g_assert_true (
    goodix5135_d4_transaction_begin (
      &transaction,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_true (
    goodix5135_d4_transaction_out_complete (
      &transaction,
      TRUE));

  g_assert_false (
    goodix5135_d4_transaction_ack_complete (
      &transaction,
      TRUE,
      bad_ack,
      sizeof (bad_ack)));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_D4_TRANSACTION_FAILED);
}


static void
test_d4_transaction_invalid_order (void)
{
  Goodix5135TlsEstablishedTransaction transaction;

  goodix5135_d4_transaction_init (
    &transaction);

  g_assert_false (
    goodix5135_d4_transaction_out_complete (
      &transaction,
      TRUE));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_D4_TRANSACTION_FAILED);
}



static void
test_enable_chip_request_true_vector (void)
{
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  static const guint8 expected[] = {
    0xa0, 0x06, 0x00, 0xa6,
    0x96, 0x03, 0x00,
    0x01, 0x00,
    0x10,
  };

  g_assert_true (
    goodix5135_build_enable_chip_request (
      TRUE,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_cmpuint (
    logical_length,
    ==,
    GOODIX5135_ENABLE_CHIP_REQUEST_LENGTH);

  g_assert_cmpmem (
    packet,
    logical_length,
    expected,
    sizeof (expected));

  for (gsize i = logical_length;
       i < sizeof (packet);
       i++)
    g_assert_cmpuint (
      packet[i],
      ==,
      0);
}


static void
test_enable_chip_request_false_vector (void)
{
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  static const guint8 expected[] = {
    0xa0, 0x06, 0x00, 0xa6,
    0x96, 0x03, 0x00,
    0x00, 0x00,
    0x11,
  };

  g_assert_true (
    goodix5135_build_enable_chip_request (
      FALSE,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_cmpmem (
    packet,
    logical_length,
    expected,
    sizeof (expected));
}


static void
test_enable_chip_request_arguments (void)
{
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  g_assert_false (
    goodix5135_build_enable_chip_request (
      TRUE,
      NULL,
      sizeof (packet),
      &logical_length));

  g_assert_false (
    goodix5135_build_enable_chip_request (
      TRUE,
      packet,
      GOODIX5135_USB_PACKET_LENGTH - 1,
      &logical_length));

  g_assert_false (
    goodix5135_build_enable_chip_request (
      TRUE,
      packet,
      sizeof (packet),
      NULL));
}


static void
test_enable_chip_ack_success (void)
{
  static const guint8 ack[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xb0, 0x03, 0x00,
    0x96, 0x01,
    0x60,
  };

  g_assert_true (
    goodix5135_parse_enable_chip_ack (
      ack,
      sizeof (ack)));
}


static void
test_enable_chip_ack_negative (void)
{
  static const guint8 ack[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xb0, 0x03, 0x00,
    0x96, 0x00,
    0x61,
  };

  g_assert_false (
    goodix5135_parse_enable_chip_ack (
      ack,
      sizeof (ack)));
}


static void
test_enable_chip_transaction_happy_path (void)
{
  Goodix5135EnableChipTransaction transaction;

  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  static const guint8 ack[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xb0, 0x03, 0x00,
    0x96, 0x01,
    0x60,
  };

  goodix5135_enable_chip_transaction_init (
    &transaction);

  g_assert_true (
    goodix5135_enable_chip_transaction_begin (
      &transaction,
      TRUE,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_ENABLE_CHIP_TRANSACTION_WAIT_OUT);

  g_assert_true (
    goodix5135_enable_chip_transaction_out_complete (
      &transaction,
      TRUE));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_ENABLE_CHIP_TRANSACTION_WAIT_ACK);

  g_assert_true (
    goodix5135_enable_chip_transaction_ack_complete (
      &transaction,
      TRUE,
      ack,
      sizeof (ack)));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_ENABLE_CHIP_TRANSACTION_DONE);
}


static void
test_enable_chip_transaction_out_failure (void)
{
  Goodix5135EnableChipTransaction transaction;

  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  goodix5135_enable_chip_transaction_init (
    &transaction);

  g_assert_true (
    goodix5135_enable_chip_transaction_begin (
      &transaction,
      TRUE,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_false (
    goodix5135_enable_chip_transaction_out_complete (
      &transaction,
      FALSE));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_ENABLE_CHIP_TRANSACTION_FAILED);
}


static void
test_enable_chip_transaction_ack_transport_failure (void)
{
  Goodix5135EnableChipTransaction transaction;

  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  goodix5135_enable_chip_transaction_init (
    &transaction);

  g_assert_true (
    goodix5135_enable_chip_transaction_begin (
      &transaction,
      TRUE,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_true (
    goodix5135_enable_chip_transaction_out_complete (
      &transaction,
      TRUE));

  g_assert_false (
    goodix5135_enable_chip_transaction_ack_complete (
      &transaction,
      FALSE,
      NULL,
      0));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_ENABLE_CHIP_TRANSACTION_FAILED);
}


static void
test_enable_chip_transaction_bad_ack (void)
{
  Goodix5135EnableChipTransaction transaction;

  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  static const guint8 bad_ack[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xb0, 0x03, 0x00,
    0x96, 0x00,
    0x61,
  };

  goodix5135_enable_chip_transaction_init (
    &transaction);

  g_assert_true (
    goodix5135_enable_chip_transaction_begin (
      &transaction,
      TRUE,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_true (
    goodix5135_enable_chip_transaction_out_complete (
      &transaction,
      TRUE));

  g_assert_false (
    goodix5135_enable_chip_transaction_ack_complete (
      &transaction,
      TRUE,
      bad_ack,
      sizeof (bad_ack)));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_ENABLE_CHIP_TRANSACTION_FAILED);
}


static void
test_enable_chip_transaction_invalid_order (void)
{
  Goodix5135EnableChipTransaction transaction;

  goodix5135_enable_chip_transaction_init (
    &transaction);

  g_assert_false (
    goodix5135_enable_chip_transaction_out_complete (
      &transaction,
      TRUE));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_ENABLE_CHIP_TRANSACTION_FAILED);
}



static void
test_sensor_reset_request_vector (void)
{
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  static const guint8 expected[] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xa2, 0x03, 0x00,
    0x05, 0x14,
    0xec,
  };

  g_assert_true (
    goodix5135_build_sensor_reset_request (
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_cmpuint (
    logical_length,
    ==,
    GOODIX5135_SENSOR_RESET_REQUEST_LENGTH);

  g_assert_cmpmem (
    packet,
    logical_length,
    expected,
    sizeof (expected));
}


static void
test_sensor_reset_request_arguments (void)
{
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  g_assert_false (
    goodix5135_build_sensor_reset_request (
      NULL,
      sizeof (packet),
      &logical_length));

  g_assert_false (
    goodix5135_build_sensor_reset_request (
      packet,
      GOODIX5135_USB_PACKET_LENGTH - 1,
      &logical_length));

  g_assert_false (
    goodix5135_build_sensor_reset_request (
      packet,
      sizeof (packet),
      NULL));
}


static void
test_sensor_reset_ack (void)
{
  static const guint8 good_ack[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xb0, 0x03, 0x00,
    0xa2, 0x01,
    0x54,
  };

  static const guint8 bad_ack[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xb0, 0x03, 0x00,
    0xa2, 0x00,
    0x55,
  };

  g_assert_true (
    goodix5135_parse_sensor_reset_ack (
      good_ack,
      sizeof (good_ack)));

  g_assert_false (
    goodix5135_parse_sensor_reset_ack (
      bad_ack,
      sizeof (bad_ack)));
}


static void
test_sensor_reset_response_success (void)
{
  guint16 number = 0;

  /*
   * Synthetic successful response:
   * success=1, number=2048 (0x0800).
   */
  static const guint8 response[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x07, 0x00, 0xa7,
    0xa2, 0x04, 0x00,
    0x01, 0x00, 0x08,
    0xfb,
  };

  g_assert_true (
    goodix5135_parse_sensor_reset_response (
      response,
      sizeof (response),
      &number));

  g_assert_cmpuint (
    number,
    ==,
    2048);
}


static void
test_sensor_reset_response_negative (void)
{
  guint16 number = 123;

  static const guint8 response[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x05, 0x00, 0xa5,
    0xa2, 0x02, 0x00,
    0x00,
    0x06,
  };

  g_assert_false (
    goodix5135_parse_sensor_reset_response (
      response,
      sizeof (response),
      &number));

  g_assert_cmpuint (number, ==, 0);
}


static void
test_sensor_reset_response_short (void)
{
  guint16 number = 123;

  static const guint8 response[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x05, 0x00, 0xa5,
    0xa2, 0x02, 0x00,
    0x01,
    0x05,
  };

  g_assert_false (
    goodix5135_parse_sensor_reset_response (
      response,
      sizeof (response),
      &number));

  g_assert_cmpuint (number, ==, 0);
}


static void
test_sensor_reset_transaction_happy_path (void)
{
  Goodix5135SensorResetTransaction transaction;

  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;
  guint16 number = 0;

  static const guint8 ack[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xb0, 0x03, 0x00,
    0xa2, 0x01,
    0x54,
  };

  static const guint8 response[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x07, 0x00, 0xa7,
    0xa2, 0x04, 0x00,
    0x01, 0x00, 0x08,
    0xfb,
  };

  goodix5135_sensor_reset_transaction_init (
    &transaction);

  g_assert_true (
    goodix5135_sensor_reset_transaction_begin (
      &transaction,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_true (
    goodix5135_sensor_reset_transaction_out_complete (
      &transaction,
      TRUE));

  g_assert_true (
    goodix5135_sensor_reset_transaction_ack_complete (
      &transaction,
      TRUE,
      ack,
      sizeof (ack)));

  g_assert_true (
    goodix5135_sensor_reset_transaction_response_complete (
      &transaction,
      TRUE,
      response,
      sizeof (response),
      &number));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_SENSOR_RESET_TRANSACTION_DONE);

  g_assert_cmpuint (
    number,
    ==,
    2048);
}


static void
test_sensor_reset_transaction_out_failure (void)
{
  Goodix5135SensorResetTransaction transaction;
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  goodix5135_sensor_reset_transaction_init (
    &transaction);

  g_assert_true (
    goodix5135_sensor_reset_transaction_begin (
      &transaction,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_false (
    goodix5135_sensor_reset_transaction_out_complete (
      &transaction,
      FALSE));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_SENSOR_RESET_TRANSACTION_FAILED);
}


static void
test_sensor_reset_transaction_ack_failure (void)
{
  Goodix5135SensorResetTransaction transaction;
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  goodix5135_sensor_reset_transaction_init (
    &transaction);

  g_assert_true (
    goodix5135_sensor_reset_transaction_begin (
      &transaction,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_true (
    goodix5135_sensor_reset_transaction_out_complete (
      &transaction,
      TRUE));

  g_assert_false (
    goodix5135_sensor_reset_transaction_ack_complete (
      &transaction,
      FALSE,
      NULL,
      0));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_SENSOR_RESET_TRANSACTION_FAILED);
}


static void
test_sensor_reset_transaction_response_failure (void)
{
  Goodix5135SensorResetTransaction transaction;
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;
  guint16 number = 0;

  static const guint8 ack[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xb0, 0x03, 0x00,
    0xa2, 0x01,
    0x54,
  };

  goodix5135_sensor_reset_transaction_init (
    &transaction);

  g_assert_true (
    goodix5135_sensor_reset_transaction_begin (
      &transaction,
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_true (
    goodix5135_sensor_reset_transaction_out_complete (
      &transaction,
      TRUE));

  g_assert_true (
    goodix5135_sensor_reset_transaction_ack_complete (
      &transaction,
      TRUE,
      ack,
      sizeof (ack)));

  g_assert_false (
    goodix5135_sensor_reset_transaction_response_complete (
      &transaction,
      FALSE,
      NULL,
      0,
      &number));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_SENSOR_RESET_TRANSACTION_FAILED);
}


static void
test_sensor_reset_transaction_invalid_order (void)
{
  Goodix5135SensorResetTransaction transaction;

  goodix5135_sensor_reset_transaction_init (
    &transaction);

  g_assert_false (
    goodix5135_sensor_reset_transaction_out_complete (
      &transaction,
      TRUE));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_SENSOR_RESET_TRANSACTION_FAILED);
}



static void
test_activation_sequence_happy_path (void)
{
  Goodix5135ActivationSequence sequence;

  static const guint8 chip_id[] = {
    0xa2, 0x04, 0x25, 0x00,
  };

  goodix5135_activation_sequence_init (
    &sequence);

  g_assert_true (
    goodix5135_activation_sequence_begin (
      &sequence));

  g_assert_cmpint (
    sequence.state,
    ==,
    GOODIX5135_ACTIVATION_WAIT_NOP1);

  g_assert_true (
    goodix5135_activation_sequence_nop_complete (
      &sequence,
      TRUE));

  g_assert_cmpint (
    sequence.state,
    ==,
    GOODIX5135_ACTIVATION_WAIT_D4);

  g_assert_true (
    goodix5135_activation_sequence_d4_complete (
      &sequence,
      TRUE));

  g_assert_cmpint (
    sequence.state,
    ==,
    GOODIX5135_ACTIVATION_WAIT_NOP2);

  g_assert_true (
    goodix5135_activation_sequence_nop_complete (
      &sequence,
      TRUE));

  g_assert_cmpint (
    sequence.state,
    ==,
    GOODIX5135_ACTIVATION_WAIT_ENABLE_CHIP);

  g_assert_true (
    goodix5135_activation_sequence_enable_chip_complete (
      &sequence,
      TRUE));

  g_assert_cmpint (
    sequence.state,
    ==,
    GOODIX5135_ACTIVATION_WAIT_NOP3);

  g_assert_true (
    goodix5135_activation_sequence_nop_complete (
      &sequence,
      TRUE));

  g_assert_cmpint (
    sequence.state,
    ==,
    GOODIX5135_ACTIVATION_WAIT_FIRMWARE);

  g_assert_true (
    goodix5135_activation_sequence_firmware_complete (
      &sequence,
      TRUE));

  g_assert_cmpint (
    sequence.state,
    ==,
    GOODIX5135_ACTIVATION_WAIT_RESET);

  g_assert_true (
    goodix5135_activation_sequence_reset_complete (
      &sequence,
      TRUE));

  g_assert_cmpint (
    sequence.state,
    ==,
    GOODIX5135_ACTIVATION_WAIT_CHIP_ID);

  g_assert_true (
    goodix5135_activation_sequence_chip_id_complete (
      &sequence,
      TRUE,
      chip_id,
      sizeof (chip_id)));

  g_assert_cmpint (
    sequence.state,
    ==,
    GOODIX5135_ACTIVATION_DONE);
}


static void
test_activation_sequence_begin_twice (void)
{
  Goodix5135ActivationSequence sequence;

  goodix5135_activation_sequence_init (
    &sequence);

  g_assert_true (
    goodix5135_activation_sequence_begin (
      &sequence));

  g_assert_false (
    goodix5135_activation_sequence_begin (
      &sequence));

  g_assert_cmpint (
    sequence.state,
    ==,
    GOODIX5135_ACTIVATION_FAILED);
}


static void
test_activation_sequence_wrong_first_command (void)
{
  Goodix5135ActivationSequence sequence;

  goodix5135_activation_sequence_init (
    &sequence);

  g_assert_true (
    goodix5135_activation_sequence_begin (
      &sequence));

  g_assert_false (
    goodix5135_activation_sequence_d4_complete (
      &sequence,
      TRUE));

  g_assert_cmpint (
    sequence.state,
    ==,
    GOODIX5135_ACTIVATION_FAILED);
}


static void
test_activation_sequence_nop_failure (void)
{
  Goodix5135ActivationSequence sequence;

  goodix5135_activation_sequence_init (
    &sequence);

  g_assert_true (
    goodix5135_activation_sequence_begin (
      &sequence));

  g_assert_false (
    goodix5135_activation_sequence_nop_complete (
      &sequence,
      FALSE));

  g_assert_cmpint (
    sequence.state,
    ==,
    GOODIX5135_ACTIVATION_FAILED);
}


static void
test_activation_sequence_d4_failure (void)
{
  Goodix5135ActivationSequence sequence;

  goodix5135_activation_sequence_init (
    &sequence);

  g_assert_true (
    goodix5135_activation_sequence_begin (
      &sequence));

  g_assert_true (
    goodix5135_activation_sequence_nop_complete (
      &sequence,
      TRUE));

  g_assert_false (
    goodix5135_activation_sequence_d4_complete (
      &sequence,
      FALSE));

  g_assert_cmpint (
    sequence.state,
    ==,
    GOODIX5135_ACTIVATION_FAILED);
}


static void
test_activation_sequence_enable_failure (void)
{
  Goodix5135ActivationSequence sequence;

  goodix5135_activation_sequence_init (
    &sequence);

  g_assert_true (
    goodix5135_activation_sequence_begin (
      &sequence));

  g_assert_true (
    goodix5135_activation_sequence_nop_complete (
      &sequence,
      TRUE));

  g_assert_true (
    goodix5135_activation_sequence_d4_complete (
      &sequence,
      TRUE));

  g_assert_true (
    goodix5135_activation_sequence_nop_complete (
      &sequence,
      TRUE));

  g_assert_false (
    goodix5135_activation_sequence_enable_chip_complete (
      &sequence,
      FALSE));

  g_assert_cmpint (
    sequence.state,
    ==,
    GOODIX5135_ACTIVATION_FAILED);
}


static void
test_activation_sequence_firmware_failure (void)
{
  Goodix5135ActivationSequence sequence;

  goodix5135_activation_sequence_init (
    &sequence);

  g_assert_true (
    goodix5135_activation_sequence_begin (
      &sequence));

  g_assert_true (
    goodix5135_activation_sequence_nop_complete (
      &sequence,
      TRUE));

  g_assert_true (
    goodix5135_activation_sequence_d4_complete (
      &sequence,
      TRUE));

  g_assert_true (
    goodix5135_activation_sequence_nop_complete (
      &sequence,
      TRUE));

  g_assert_true (
    goodix5135_activation_sequence_enable_chip_complete (
      &sequence,
      TRUE));

  g_assert_true (
    goodix5135_activation_sequence_nop_complete (
      &sequence,
      TRUE));

  g_assert_false (
    goodix5135_activation_sequence_firmware_complete (
      &sequence,
      FALSE));

  g_assert_cmpint (
    sequence.state,
    ==,
    GOODIX5135_ACTIVATION_FAILED);
}


static void
test_activation_sequence_reset_failure (void)
{
  Goodix5135ActivationSequence sequence;

  goodix5135_activation_sequence_init (
    &sequence);

  g_assert_true (
    goodix5135_activation_sequence_begin (
      &sequence));

  g_assert_true (
    goodix5135_activation_sequence_nop_complete (
      &sequence,
      TRUE));

  g_assert_true (
    goodix5135_activation_sequence_d4_complete (
      &sequence,
      TRUE));

  g_assert_true (
    goodix5135_activation_sequence_nop_complete (
      &sequence,
      TRUE));

  g_assert_true (
    goodix5135_activation_sequence_enable_chip_complete (
      &sequence,
      TRUE));

  g_assert_true (
    goodix5135_activation_sequence_nop_complete (
      &sequence,
      TRUE));

  g_assert_true (
    goodix5135_activation_sequence_firmware_complete (
      &sequence,
      TRUE));

  g_assert_false (
    goodix5135_activation_sequence_reset_complete (
      &sequence,
      FALSE));

  g_assert_cmpint (
    sequence.state,
    ==,
    GOODIX5135_ACTIVATION_FAILED);
}


static void
test_activation_sequence_wrong_chip_id (void)
{
  Goodix5135ActivationSequence sequence;

  static const guint8 wrong_chip_id[] = {
    0xa2, 0x04, 0x24, 0x00,
  };

  goodix5135_activation_sequence_init (
    &sequence);

  g_assert_true (
    goodix5135_activation_sequence_begin (
      &sequence));

  g_assert_true (
    goodix5135_activation_sequence_nop_complete (
      &sequence,
      TRUE));

  g_assert_true (
    goodix5135_activation_sequence_d4_complete (
      &sequence,
      TRUE));

  g_assert_true (
    goodix5135_activation_sequence_nop_complete (
      &sequence,
      TRUE));

  g_assert_true (
    goodix5135_activation_sequence_enable_chip_complete (
      &sequence,
      TRUE));

  g_assert_true (
    goodix5135_activation_sequence_nop_complete (
      &sequence,
      TRUE));

  g_assert_true (
    goodix5135_activation_sequence_firmware_complete (
      &sequence,
      TRUE));

  g_assert_true (
    goodix5135_activation_sequence_reset_complete (
      &sequence,
      TRUE));

  g_assert_false (
    goodix5135_activation_sequence_chip_id_complete (
      &sequence,
      TRUE,
      wrong_chip_id,
      sizeof (wrong_chip_id)));

  g_assert_cmpint (
    sequence.state,
    ==,
    GOODIX5135_ACTIVATION_FAILED);
}


static void
test_activation_sequence_short_chip_id (void)
{
  Goodix5135ActivationSequence sequence;

  static const guint8 short_chip_id[] = {
    0xa2, 0x04, 0x25,
  };

  goodix5135_activation_sequence_init (
    &sequence);

  g_assert_true (
    goodix5135_activation_sequence_begin (
      &sequence));

  g_assert_true (
    goodix5135_activation_sequence_nop_complete (
      &sequence,
      TRUE));

  g_assert_true (
    goodix5135_activation_sequence_d4_complete (
      &sequence,
      TRUE));

  g_assert_true (
    goodix5135_activation_sequence_nop_complete (
      &sequence,
      TRUE));

  g_assert_true (
    goodix5135_activation_sequence_enable_chip_complete (
      &sequence,
      TRUE));

  g_assert_true (
    goodix5135_activation_sequence_nop_complete (
      &sequence,
      TRUE));

  g_assert_true (
    goodix5135_activation_sequence_firmware_complete (
      &sequence,
      TRUE));

  g_assert_true (
    goodix5135_activation_sequence_reset_complete (
      &sequence,
      TRUE));

  g_assert_false (
    goodix5135_activation_sequence_chip_id_complete (
      &sequence,
      TRUE,
      short_chip_id,
      sizeof (short_chip_id)));

  g_assert_cmpint (
    sequence.state,
    ==,
    GOODIX5135_ACTIVATION_FAILED);
}



static void
test_config_upload_request_zero_vector (void)
{
  guint8 config[GOODIX5135_CONFIG_LENGTH] = { 0 };
  guint8 transfer[GOODIX5135_CONFIG_TRANSFER_LENGTH];

  gsize logical_length = 0;
  gsize transport_length = 0;

  g_assert_true (
    goodix5135_build_config_upload_transfer (
      config,
      sizeof (config),
      transfer,
      sizeof (transfer),
      &logical_length,
      &transport_length));

  g_assert_cmpuint (
    logical_length,
    ==,
    GOODIX5135_CONFIG_LOGICAL_LENGTH);

  g_assert_cmpuint (
    transport_length,
    ==,
    GOODIX5135_CONFIG_TRANSFER_LENGTH);

  /*
   * Outer:
   *   a0 e4 00 84
   *
   * Inner:
   *   90 e1 00
   */
  g_assert_cmpuint (transfer[0], ==, 0xa0);
  g_assert_cmpuint (transfer[1], ==, 0xe4);
  g_assert_cmpuint (transfer[2], ==, 0x00);
  g_assert_cmpuint (transfer[3], ==, 0x84);

  g_assert_cmpuint (transfer[4], ==, 0x90);
  g_assert_cmpuint (transfer[5], ==, 0xe1);
  g_assert_cmpuint (transfer[6], ==, 0x00);

  for (gsize i = 0;
       i < GOODIX5135_CONFIG_LENGTH;
       i++)
    g_assert_cmpuint (
      transfer[7 + i],
      ==,
      0);

  /*
   * Synthetic all-zero config checksum.
   */
  g_assert_cmpuint (
    transfer[231],
    ==,
    0x39);

  /*
   * USB padding:
   * bytes 232..255 must be zero.
   */
  for (gsize i = GOODIX5135_CONFIG_LOGICAL_LENGTH;
       i < GOODIX5135_CONFIG_TRANSFER_LENGTH;
       i++)
    g_assert_cmpuint (
      transfer[i],
      ==,
      0);
}


static void
test_config_upload_request_pattern (void)
{
  guint8 config[GOODIX5135_CONFIG_LENGTH];
  guint8 transfer[GOODIX5135_CONFIG_TRANSFER_LENGTH];

  gsize logical_length = 0;
  gsize transport_length = 0;

  for (gsize i = 0;
       i < sizeof (config);
       i++)
    config[i] = (guint8) i;

  g_assert_true (
    goodix5135_build_config_upload_transfer (
      config,
      sizeof (config),
      transfer,
      sizeof (transfer),
      &logical_length,
      &transport_length));

  g_assert_cmpmem (
    transfer + 7,
    GOODIX5135_CONFIG_LENGTH,
    config,
    sizeof (config));

  /*
   * checksum for synthetic sequence 00..df.
   */
  g_assert_cmpuint (
    transfer[231],
    ==,
    0xa9);
}


static void
test_config_upload_request_arguments (void)
{
  guint8 config[GOODIX5135_CONFIG_LENGTH] = { 0 };
  guint8 transfer[GOODIX5135_CONFIG_TRANSFER_LENGTH];

  gsize logical_length = 99;
  gsize transport_length = 99;

  g_assert_false (
    goodix5135_build_config_upload_transfer (
      NULL,
      sizeof (config),
      transfer,
      sizeof (transfer),
      &logical_length,
      &transport_length));

  g_assert_false (
    goodix5135_build_config_upload_transfer (
      config,
      GOODIX5135_CONFIG_LENGTH - 1,
      transfer,
      sizeof (transfer),
      &logical_length,
      &transport_length));

  g_assert_false (
    goodix5135_build_config_upload_transfer (
      config,
      sizeof (config),
      NULL,
      sizeof (transfer),
      &logical_length,
      &transport_length));

  g_assert_false (
    goodix5135_build_config_upload_transfer (
      config,
      sizeof (config),
      transfer,
      GOODIX5135_CONFIG_TRANSFER_LENGTH - 1,
      &logical_length,
      &transport_length));

  g_assert_false (
    goodix5135_build_config_upload_transfer (
      config,
      sizeof (config),
      transfer,
      sizeof (transfer),
      NULL,
      &transport_length));

  g_assert_false (
    goodix5135_build_config_upload_transfer (
      config,
      sizeof (config),
      transfer,
      sizeof (transfer),
      &logical_length,
      NULL));
}


static void
test_config_upload_packet_view_first (void)
{
  guint8 config[GOODIX5135_CONFIG_LENGTH] = { 0 };
  guint8 transfer[GOODIX5135_CONFIG_TRANSFER_LENGTH];

  gsize logical_length = 0;
  gsize transport_length = 0;

  const guint8 *packet = NULL;
  gsize packet_length = 0;

  g_assert_true (
    goodix5135_build_config_upload_transfer (
      config,
      sizeof (config),
      transfer,
      sizeof (transfer),
      &logical_length,
      &transport_length));

  g_assert_true (
    goodix5135_config_upload_get_packet (
      transfer,
      transport_length,
      0,
      &packet,
      &packet_length));

  g_assert_true (
    packet == transfer);

  g_assert_cmpuint (
    packet_length,
    ==,
    GOODIX5135_USB_PACKET_LENGTH);

  g_assert_cmpuint (packet[0], ==, 0xa0);
  g_assert_cmpuint (packet[1], ==, 0xe4);
  g_assert_cmpuint (packet[4], ==, 0x90);
}


static void
test_config_upload_packet_view_last (void)
{
  guint8 config[GOODIX5135_CONFIG_LENGTH] = { 0 };
  guint8 transfer[GOODIX5135_CONFIG_TRANSFER_LENGTH];

  gsize logical_length = 0;
  gsize transport_length = 0;

  const guint8 *packet = NULL;
  gsize packet_length = 0;

  g_assert_true (
    goodix5135_build_config_upload_transfer (
      config,
      sizeof (config),
      transfer,
      sizeof (transfer),
      &logical_length,
      &transport_length));

  g_assert_true (
    goodix5135_config_upload_get_packet (
      transfer,
      transport_length,
      3,
      &packet,
      &packet_length));

  g_assert_true (
    packet ==
      transfer +
      (3U * GOODIX5135_USB_PACKET_LENGTH));

  g_assert_cmpuint (
    packet_length,
    ==,
    GOODIX5135_USB_PACKET_LENGTH);

  /*
   * Checksum is absolute byte 231.
   * Relative to packet 3 starting at byte 192:
   *
   *   231 - 192 = 39
   */
  g_assert_cmpuint (
    packet[39],
    ==,
    0x39);

  for (gsize i = 40;
       i < GOODIX5135_USB_PACKET_LENGTH;
       i++)
    g_assert_cmpuint (
      packet[i],
      ==,
      0);
}


static void
test_config_upload_packet_view_invalid (void)
{
  guint8 transfer[GOODIX5135_CONFIG_TRANSFER_LENGTH] = { 0 };

  const guint8 *packet = NULL;
  gsize packet_length = 0;

  g_assert_false (
    goodix5135_config_upload_get_packet (
      transfer,
      sizeof (transfer),
      GOODIX5135_CONFIG_USB_PACKET_COUNT,
      &packet,
      &packet_length));

  g_assert_false (
    goodix5135_config_upload_get_packet (
      transfer,
      GOODIX5135_CONFIG_TRANSFER_LENGTH - 1,
      0,
      &packet,
      &packet_length));

  g_assert_false (
    goodix5135_config_upload_get_packet (
      NULL,
      sizeof (transfer),
      0,
      &packet,
      &packet_length));
}


static void
test_config_upload_ack_success (void)
{
  static const guint8 ack[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xb0, 0x03, 0x00,
    0x90, 0x01,
    0x66,
  };

  g_assert_true (
    goodix5135_parse_config_upload_ack (
      ack,
      sizeof (ack)));
}


static void
test_config_upload_ack_negative (void)
{
  static const guint8 ack[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xb0, 0x03, 0x00,
    0x90, 0x00,
    0x67,
  };

  g_assert_false (
    goodix5135_parse_config_upload_ack (
      ack,
      sizeof (ack)));
}


static void
test_config_upload_response_success (void)
{
  static const guint8 response[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x05, 0x00, 0xa5,
    0x90, 0x02, 0x00,
    0x01,
    0x17,
  };

  g_assert_true (
    goodix5135_parse_config_upload_response (
      response,
      sizeof (response)));
}


static void
test_config_upload_response_negative (void)
{
  static const guint8 response[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x05, 0x00, 0xa5,
    0x90, 0x02, 0x00,
    0x00,
    0x18,
  };

  g_assert_false (
    goodix5135_parse_config_upload_response (
      response,
      sizeof (response)));
}


static void
test_config_upload_transaction_happy_path (void)
{
  Goodix5135ConfigUploadTransaction transaction;

  guint8 config[GOODIX5135_CONFIG_LENGTH] = { 0 };
  guint8 transfer[GOODIX5135_CONFIG_TRANSFER_LENGTH];

  gsize logical_length = 0;
  gsize transport_length = 0;

  static const guint8 ack[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xb0, 0x03, 0x00,
    0x90, 0x01,
    0x66,
  };

  static const guint8 response[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x05, 0x00, 0xa5,
    0x90, 0x02, 0x00,
    0x01,
    0x17,
  };

  goodix5135_config_upload_transaction_init (
    &transaction);

  g_assert_true (
    goodix5135_config_upload_transaction_begin (
      &transaction,
      config,
      sizeof (config),
      transfer,
      sizeof (transfer),
      &logical_length,
      &transport_length));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_CONFIG_UPLOAD_TRANSACTION_WAIT_OUT);

  for (guint i = 0;
       i < GOODIX5135_CONFIG_USB_PACKET_COUNT;
       i++)
    {
      g_assert_true (
        goodix5135_config_upload_transaction_out_complete (
          &transaction,
          TRUE));

      g_assert_cmpuint (
        transaction.packets_completed,
        ==,
        i + 1);

      if (i + 1 <
          GOODIX5135_CONFIG_USB_PACKET_COUNT)
        g_assert_cmpint (
          transaction.state,
          ==,
          GOODIX5135_CONFIG_UPLOAD_TRANSACTION_WAIT_OUT);
    }

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_CONFIG_UPLOAD_TRANSACTION_WAIT_ACK);

  g_assert_true (
    goodix5135_config_upload_transaction_ack_complete (
      &transaction,
      TRUE,
      ack,
      sizeof (ack)));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_CONFIG_UPLOAD_TRANSACTION_WAIT_RESPONSE);

  g_assert_true (
    goodix5135_config_upload_transaction_response_complete (
      &transaction,
      TRUE,
      response,
      sizeof (response)));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_CONFIG_UPLOAD_TRANSACTION_DONE);
}


static void
test_config_upload_transaction_out_failure (void)
{
  Goodix5135ConfigUploadTransaction transaction;

  guint8 config[GOODIX5135_CONFIG_LENGTH] = { 0 };
  guint8 transfer[GOODIX5135_CONFIG_TRANSFER_LENGTH];

  gsize logical_length = 0;
  gsize transport_length = 0;

  goodix5135_config_upload_transaction_init (
    &transaction);

  g_assert_true (
    goodix5135_config_upload_transaction_begin (
      &transaction,
      config,
      sizeof (config),
      transfer,
      sizeof (transfer),
      &logical_length,
      &transport_length));

  g_assert_false (
    goodix5135_config_upload_transaction_out_complete (
      &transaction,
      FALSE));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_CONFIG_UPLOAD_TRANSACTION_FAILED);
}


static void
test_config_upload_transaction_invalid_order (void)
{
  Goodix5135ConfigUploadTransaction transaction;

  guint8 config[GOODIX5135_CONFIG_LENGTH] = { 0 };
  guint8 transfer[GOODIX5135_CONFIG_TRANSFER_LENGTH];

  gsize logical_length = 0;
  gsize transport_length = 0;

  static const guint8 ack[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xb0, 0x03, 0x00,
    0x90, 0x01,
    0x66,
  };

  goodix5135_config_upload_transaction_init (
    &transaction);

  g_assert_true (
    goodix5135_config_upload_transaction_begin (
      &transaction,
      config,
      sizeof (config),
      transfer,
      sizeof (transfer),
      &logical_length,
      &transport_length));

  /*
   * ACK before all four OUT packets must fail closed.
   */
  g_assert_false (
    goodix5135_config_upload_transaction_ack_complete (
      &transaction,
      TRUE,
      ack,
      sizeof (ack)));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_CONFIG_UPLOAD_TRANSACTION_FAILED);
}



static guint8
test_cfg70_crc8 (
  const guint8 *data,
  gsize         length)
{
  guint8 crc = 0;

  for (gsize i = 0; i < length; i++)
    {
      crc ^= data[i];

      for (guint bit = 0; bit < 8; bit++)
        crc =
          (guint8) (
            (crc & 0x80U)
              ? ((crc << 1) ^ 0x07U)
              : (crc << 1));
    }

  return crc;
}


static guint8
test_cfg70_inv_crc8 (
  const guint8 *data,
  gsize         length)
{
  return (guint8) ~test_cfg70_crc8 (
    data,
    length);
}


static void
test_cfg70_make_valid_otp (
  guint8 otp[GOODIX5135_OTP_LENGTH])
{
  guint8 buffer[32];
  gsize n;

  memset (
    otp,
    0,
    GOODIX5135_OTP_LENGTH);

  /*
   * Synthetic calibration only.
   */
  otp[42] = 0x23;
  otp[27] = 0x15;

  otp[46] = 0x12;
  otp[47] = 0x34;
  otp[48] = 0x56;
  otp[49] = 0x78;

  memcpy (
    otp + 50,
    otp + 46,
    4);

  /*
   * MT_DAC must be established before MT CRC,
   * because byte 22 participates in the MT range.
   */
  otp[22] =
    test_cfg70_inv_crc8 (
      otp + 46,
      4);

  /*
   * FT_DAC must be established before FT CRC,
   * because byte 62 participates in FT.
   */
  otp[62] =
    test_cfg70_inv_crc8 (
      otp + 50,
      4);

  /*
   * CP.
   */
  n = 0;
  memcpy (buffer + n, otp + 0, 11);
  n += 11;
  memcpy (buffer + n, otp + 36, 4);
  n += 4;

  otp[60] =
    test_cfg70_inv_crc8 (
      buffer,
      n);

  /*
   * FT.
   */
  n = 0;
  memcpy (buffer + n, otp + 11, 9);
  n += 9;
  memcpy (buffer + n, otp + 28, 1);
  n += 1;
  memcpy (buffer + n, otp + 50, 4);
  n += 4;
  memcpy (buffer + n, otp + 56, 4);
  n += 4;
  buffer[n++] = otp[62];

  otp[61] =
    test_cfg70_inv_crc8 (
      buffer,
      n);

  /*
   * MT.
   */
  n = 0;
  memcpy (buffer + n, otp + 20, 8);
  n += 8;
  memcpy (buffer + n, otp + 29, 7);
  n += 7;
  memcpy (buffer + n, otp + 40, 10);
  n += 10;
  memcpy (buffer + n, otp + 54, 2);
  n += 2;

  otp[63] =
    test_cfg70_inv_crc8 (
      buffer,
      n);
}


static void
test_cfg70_make_template (
  guint8 cfg[GOODIX5135_CONFIG_LENGTH])
{
  memset (
    cfg,
    0,
    GOODIX5135_CONFIG_LENGTH);

  cfg[0] = 0x70;
  cfg[1] = 0x11;
  cfg[2] = 0x74;
  cfg[3] = 0x85;

#define PUT16(off, value)                   \
  G_STMT_START                              \
    {                                       \
      cfg[(off)] =                          \
        (guint8) ((value) & 0xffU);         \
      cfg[(off) + 1] =                      \
        (guint8) (((value) >> 8) & 0xffU); \
    }                                       \
  G_STMT_END

  PUT16 (0x71, 0x005c);
  PUT16 (0x73, 0x0180);

  PUT16 (0x75, 0x0220);
  PUT16 (0x77, 0x0808);

  PUT16 (0x79, 0x0236);
  PUT16 (0x7b, 0x0080);

  PUT16 (0x7d, 0x0238);
  PUT16 (0x7f, 0x0080);

  PUT16 (0x81, 0x023a);
  PUT16 (0x83, 0x0080);

  PUT16 (0xad, 0x0082);
  PUT16 (0xaf, 0x1580);

#undef PUT16
}


static guint16
test_cfg70_u16le (
  const guint8 *data,
  gsize         offset)
{
  return
    (guint16) data[offset] |
    ((guint16) data[offset + 1] << 8);
}


static void
test_cfg70_otp_valid (void)
{
  guint8 otp[GOODIX5135_OTP_LENGTH];

  test_cfg70_make_valid_otp (
    otp);

  g_assert_true (
    goodix5135_validate_otp (
      otp,
      sizeof (otp)));
}


static void
test_cfg70_otp_bad_length (void)
{
  guint8 otp[GOODIX5135_OTP_LENGTH] = { 0 };

  g_assert_false (
    goodix5135_validate_otp (
      otp,
      GOODIX5135_OTP_LENGTH - 1));
}


static void
test_cfg70_otp_crc_failure (void)
{
  guint8 otp[GOODIX5135_OTP_LENGTH];

  test_cfg70_make_valid_otp (
    otp);

  otp[0] ^= 0x01U;

  g_assert_false (
    goodix5135_validate_otp (
      otp,
      sizeof (otp)));
}


static void
test_cfg70_otp_dac_mirror_failure (void)
{
  guint8 otp[GOODIX5135_OTP_LENGTH];

  test_cfg70_make_valid_otp (
    otp);

  /*
   * Corrupt the mirrored DAC region and deliberately repair
   * the associated FT_DAC + FT CRCs. Validation must still
   * reject the mirror mismatch.
   */
  otp[50] ^= 0x01U;

  otp[62] =
    test_cfg70_inv_crc8 (
      otp + 50,
      4);

  {
    guint8 buffer[19];
    gsize n = 0;

    memcpy (buffer + n, otp + 11, 9);
    n += 9;

    memcpy (buffer + n, otp + 28, 1);
    n += 1;

    memcpy (buffer + n, otp + 50, 4);
    n += 4;

    memcpy (buffer + n, otp + 56, 4);
    n += 4;

    buffer[n++] = otp[62];

    otp[61] =
      test_cfg70_inv_crc8 (
        buffer,
        n);
  }

  g_assert_false (
    goodix5135_validate_otp (
      otp,
      sizeof (otp)));
}


static void
test_cfg70_otp_calibration (void)
{
  guint8 otp[GOODIX5135_OTP_LENGTH];

  Goodix5135OtpCalibration cal;

  test_cfg70_make_valid_otp (
    otp);

  g_assert_true (
    goodix5135_parse_otp_calibration (
      otp,
      sizeof (otp),
      &cal));

  g_assert_cmpuint (
    cal.tcode,
    ==,
    112);

  g_assert_cmpuint (
    cal.fdt_delta,
    ==,
    23);

  g_assert_cmpuint (
    cal.fdt_offset,
    ==,
    1);

  g_assert_cmpuint (
    cal.dac0,
    ==,
    0x0128);

  g_assert_cmpuint (
    cal.dac1,
    ==,
    0x34);

  g_assert_cmpuint (
    cal.dac2,
    ==,
    0x56);

  g_assert_cmpuint (
    cal.dac3,
    ==,
    0x78);
}


static void
test_cfg70_template_valid (void)
{
  guint8 cfg[GOODIX5135_CONFIG_LENGTH];

  test_cfg70_make_template (
    cfg);

  g_assert_true (
    goodix5135_validate_cfg70_template (
      cfg,
      sizeof (cfg)));
}


static void
test_cfg70_template_bad_prefix (void)
{
  guint8 cfg[GOODIX5135_CONFIG_LENGTH];

  test_cfg70_make_template (
    cfg);

  cfg[0] ^= 0x01U;

  g_assert_false (
    goodix5135_validate_cfg70_template (
      cfg,
      sizeof (cfg)));
}


static void
test_cfg70_template_bad_register_layout (void)
{
  guint8 cfg[GOODIX5135_CONFIG_LENGTH];

  test_cfg70_make_template (
    cfg);

  cfg[0x75] ^= 0x01U;

  g_assert_false (
    goodix5135_validate_cfg70_template (
      cfg,
      sizeof (cfg)));
}


static void
test_cfg70_checksum (void)
{
  guint8 cfg[GOODIX5135_CONFIG_LENGTH];

  guint16 checksum = 0;

  test_cfg70_make_template (
    cfg);

  g_assert_true (
    goodix5135_cfg70_checksum (
      cfg,
      sizeof (cfg),
      &checksum));

  /*
   * Do not assert a private/unit value here.
   * Merely prove deterministic calculation.
   */
  guint16 second = 0;

  g_assert_true (
    goodix5135_cfg70_checksum (
      cfg,
      sizeof (cfg),
      &second));

  g_assert_cmpuint (
    checksum,
    ==,
    second);
}


static void
test_cfg70_runtime_config (void)
{
  guint8 otp[GOODIX5135_OTP_LENGTH];
  guint8 template_data[GOODIX5135_CONFIG_LENGTH];
  guint8 runtime[GOODIX5135_CONFIG_LENGTH];

  Goodix5135OtpCalibration cal;
  guint16 checksum;

  test_cfg70_make_valid_otp (
    otp);

  test_cfg70_make_template (
    template_data);

  g_assert_true (
    goodix5135_parse_otp_calibration (
      otp,
      sizeof (otp),
      &cal));

  g_assert_true (
    goodix5135_build_runtime_config (
      template_data,
      sizeof (template_data),
      &cal,
      runtime,
      sizeof (runtime)));

  g_assert_cmpuint (
    test_cfg70_u16le (
      runtime,
      0x73),
    ==,
    cal.tcode);

  g_assert_cmpuint (
    test_cfg70_u16le (
      runtime,
      0x77),
    ==,
    cal.dac0);

  g_assert_cmpuint (
    test_cfg70_u16le (
      runtime,
      0x7b),
    ==,
    cal.dac1);

  g_assert_cmpuint (
    test_cfg70_u16le (
      runtime,
      0x7f),
    ==,
    cal.dac2);

  g_assert_cmpuint (
    test_cfg70_u16le (
      runtime,
      0x83),
    ==,
    cal.dac3);

  g_assert_cmpuint (
    test_cfg70_u16le (
      runtime,
      0xaf),
    ==,
    ((guint16) cal.fdt_delta << 8) |
      0x80U);

  g_assert_true (
    goodix5135_cfg70_checksum (
      runtime,
      sizeof (runtime),
      &checksum));

  g_assert_cmpuint (
    test_cfg70_u16le (
      runtime,
      GOODIX5135_CFG70_CHECKSUM_OFFSET),
    ==,
    checksum);
}


static void
test_cfg70_runtime_invalid_template (void)
{
  guint8 template_data[GOODIX5135_CONFIG_LENGTH] = { 0 };
  guint8 runtime[GOODIX5135_CONFIG_LENGTH];

  Goodix5135OtpCalibration cal = { 0 };

  g_assert_false (
    goodix5135_build_runtime_config (
      template_data,
      sizeof (template_data),
      &cal,
      runtime,
      sizeof (runtime)));
}


static void
test_cfg70_runtime_arguments (void)
{
  guint8 template_data[GOODIX5135_CONFIG_LENGTH];
  guint8 runtime[GOODIX5135_CONFIG_LENGTH];

  Goodix5135OtpCalibration cal = { 0 };

  test_cfg70_make_template (
    template_data);

  g_assert_false (
    goodix5135_build_runtime_config (
      NULL,
      sizeof (template_data),
      &cal,
      runtime,
      sizeof (runtime)));

  g_assert_false (
    goodix5135_build_runtime_config (
      template_data,
      sizeof (template_data),
      NULL,
      runtime,
      sizeof (runtime)));

  g_assert_false (
    goodix5135_build_runtime_config (
      template_data,
      sizeof (template_data),
      &cal,
      NULL,
      sizeof (runtime)));

  g_assert_false (
    goodix5135_build_runtime_config (
      template_data,
      sizeof (template_data),
      &cal,
      runtime,
      sizeof (runtime) - 1));
}



static void
test_otp_read_make_response (
  guint8 *response,
  gsize   response_size,
  gsize   payload_length)
{
  gsize inner_protocol_length;
  guint outer_sum;
  guint inner_sum;

  g_assert_nonnull (response);

  inner_protocol_length =
    1U +
    2U +
    payload_length +
    1U;

  g_assert_cmpuint (
    response_size,
    >=,
    4U + inner_protocol_length);

  memset (
    response,
    0,
    response_size);

  response[0] =
    GOODIX5135_PACK_FLAGS_MESSAGE_PROTOCOL;

  response[1] =
    (guint8) (
      inner_protocol_length & 0xffU);

  response[2] =
    (guint8) (
      (inner_protocol_length >> 8) & 0xffU);

  outer_sum =
    (guint) response[0] +
    (guint) response[1] +
    (guint) response[2];

  response[3] =
    (guint8) (
      outer_sum & 0xffU);

  response[4] =
    GOODIX5135_COMMAND_READ_OTP;

  response[5] =
    (guint8) (
      (payload_length + 1U) &
      0xffU);

  response[6] =
    (guint8) (
      ((payload_length + 1U) >> 8) &
      0xffU);

  for (gsize i = 0;
       i < payload_length;
       i++)
    response[7 + i] =
      (guint8) i;

  inner_sum = 0;

  for (gsize i = 4;
       i < 7U + payload_length;
       i++)
    inner_sum += response[i];

  response[7 + payload_length] =
    (guint8) (
      (0xaaU - inner_sum) & 0xffU);
}


static void
test_otp_read_request_vector (void)
{
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  static const guint8 expected[] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xa6, 0x03, 0x00,
    0x00, 0x00,
    0x01,
  };

  g_assert_true (
    goodix5135_build_otp_read_request (
      packet,
      sizeof (packet),
      &logical_length));

  g_assert_cmpuint (
    logical_length,
    ==,
    GOODIX5135_OTP_READ_REQUEST_LENGTH);

  g_assert_cmpmem (
    packet,
    logical_length,
    expected,
    sizeof (expected));
}


static void
test_otp_read_request_arguments (void)
{
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 123;

  g_assert_false (
    goodix5135_build_otp_read_request (
      NULL,
      sizeof (packet),
      &logical_length));

  g_assert_false (
    goodix5135_build_otp_read_request (
      packet,
      GOODIX5135_USB_PACKET_LENGTH - 1,
      &logical_length));

  g_assert_false (
    goodix5135_build_otp_read_request (
      packet,
      sizeof (packet),
      NULL));
}


static void
test_otp_read_ack_success (void)
{
  static const guint8 ack[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xb0, 0x03, 0x00,
    0xa6, 0x01,
    0x50,
  };

  g_assert_true (
    goodix5135_parse_otp_read_ack (
      ack,
      sizeof (ack)));
}


static void
test_otp_read_ack_negative (void)
{
  static const guint8 ack[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xb0, 0x03, 0x00,
    0xa6, 0x00,
    0x51,
  };

  g_assert_false (
    goodix5135_parse_otp_read_ack (
      ack,
      sizeof (ack)));
}


static void
test_otp_read_response_64_bytes (void)
{
  guint8 response[
    GOODIX5135_OTP_READ_RESPONSE_LENGTH
  ];

  guint8 otp[GOODIX5135_OTP_LENGTH];

  test_otp_read_make_response (
    response,
    sizeof (response),
    GOODIX5135_OTP_LENGTH);

  g_assert_cmpuint (
    response[0],
    ==,
    0xa0);

  g_assert_cmpuint (
    response[1],
    ==,
    0x44);

  g_assert_cmpuint (
    response[2],
    ==,
    0x00);

  g_assert_cmpuint (
    response[3],
    ==,
    0xe4);

  g_assert_cmpuint (
    response[4],
    ==,
    0xa6);

  g_assert_cmpuint (
    response[5],
    ==,
    0x41);

  g_assert_cmpuint (
    response[6],
    ==,
    0x00);

  /*
   * For synthetic payload 00..3f the parser must return the
   * exact 64 bytes to caller-owned memory.
   */
  g_assert_true (
    goodix5135_parse_otp_read_response (
      response,
      sizeof (response),
      otp,
      sizeof (otp)));

  for (gsize i = 0;
       i < sizeof (otp);
       i++)
    g_assert_cmpuint (
      otp[i],
      ==,
      (guint8) i);
}


static void
test_otp_read_response_wrong_length (void)
{
  guint8 response[
    GOODIX5135_OTP_READ_RESPONSE_LENGTH
  ];

  guint8 otp[GOODIX5135_OTP_LENGTH];

  memset (
    otp,
    0xaa,
    sizeof (otp));

  test_otp_read_make_response (
    response,
    sizeof (response),
    GOODIX5135_OTP_LENGTH - 1);

  g_assert_false (
    goodix5135_parse_otp_read_response (
      response,
      sizeof (response),
      otp,
      sizeof (otp)));

  /*
   * Rejected input must not leave stale caller contents.
   */
  for (gsize i = 0;
       i < sizeof (otp);
       i++)
    g_assert_cmpuint (
      otp[i],
      ==,
      0);
}


static void
test_otp_read_transaction_happy_path (void)
{
  Goodix5135OtpReadTransaction transaction;

  guint8 request[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  guint8 response[
    GOODIX5135_OTP_READ_RESPONSE_LENGTH
  ];

  guint8 otp[GOODIX5135_OTP_LENGTH];

  static const guint8 ack[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xb0, 0x03, 0x00,
    0xa6, 0x01,
    0x50,
  };

  test_otp_read_make_response (
    response,
    sizeof (response),
    GOODIX5135_OTP_LENGTH);

  goodix5135_otp_read_transaction_init (
    &transaction);

  g_assert_true (
    goodix5135_otp_read_transaction_begin (
      &transaction,
      request,
      sizeof (request),
      &logical_length));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_OTP_READ_TRANSACTION_WAIT_OUT);

  g_assert_true (
    goodix5135_otp_read_transaction_out_complete (
      &transaction,
      TRUE));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_OTP_READ_TRANSACTION_WAIT_ACK);

  g_assert_true (
    goodix5135_otp_read_transaction_ack_complete (
      &transaction,
      TRUE,
      ack,
      sizeof (ack)));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_OTP_READ_TRANSACTION_WAIT_RESPONSE);

  g_assert_true (
    goodix5135_otp_read_transaction_response_complete (
      &transaction,
      TRUE,
      response,
      sizeof (response),
      otp,
      sizeof (otp)));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_OTP_READ_TRANSACTION_DONE);

  for (gsize i = 0;
       i < sizeof (otp);
       i++)
    g_assert_cmpuint (
      otp[i],
      ==,
      (guint8) i);
}


static void
test_otp_read_transaction_out_failure (void)
{
  Goodix5135OtpReadTransaction transaction;

  guint8 request[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  goodix5135_otp_read_transaction_init (
    &transaction);

  g_assert_true (
    goodix5135_otp_read_transaction_begin (
      &transaction,
      request,
      sizeof (request),
      &logical_length));

  g_assert_false (
    goodix5135_otp_read_transaction_out_complete (
      &transaction,
      FALSE));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_OTP_READ_TRANSACTION_FAILED);
}


static void
test_otp_read_transaction_ack_failure (void)
{
  Goodix5135OtpReadTransaction transaction;

  guint8 request[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  goodix5135_otp_read_transaction_init (
    &transaction);

  g_assert_true (
    goodix5135_otp_read_transaction_begin (
      &transaction,
      request,
      sizeof (request),
      &logical_length));

  g_assert_true (
    goodix5135_otp_read_transaction_out_complete (
      &transaction,
      TRUE));

  g_assert_false (
    goodix5135_otp_read_transaction_ack_complete (
      &transaction,
      FALSE,
      NULL,
      0));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_OTP_READ_TRANSACTION_FAILED);
}


static void
test_otp_read_transaction_response_failure (void)
{
  Goodix5135OtpReadTransaction transaction;

  guint8 request[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  guint8 otp[GOODIX5135_OTP_LENGTH];

  static const guint8 ack[GOODIX5135_USB_PACKET_LENGTH] = {
    0xa0, 0x06, 0x00, 0xa6,
    0xb0, 0x03, 0x00,
    0xa6, 0x01,
    0x50,
  };

  goodix5135_otp_read_transaction_init (
    &transaction);

  g_assert_true (
    goodix5135_otp_read_transaction_begin (
      &transaction,
      request,
      sizeof (request),
      &logical_length));

  g_assert_true (
    goodix5135_otp_read_transaction_out_complete (
      &transaction,
      TRUE));

  g_assert_true (
    goodix5135_otp_read_transaction_ack_complete (
      &transaction,
      TRUE,
      ack,
      sizeof (ack)));

  g_assert_false (
    goodix5135_otp_read_transaction_response_complete (
      &transaction,
      FALSE,
      NULL,
      0,
      otp,
      sizeof (otp)));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_OTP_READ_TRANSACTION_FAILED);
}


static void
test_otp_read_transaction_invalid_order (void)
{
  Goodix5135OtpReadTransaction transaction;

  goodix5135_otp_read_transaction_init (
    &transaction);

  g_assert_false (
    goodix5135_otp_read_transaction_out_complete (
      &transaction,
      TRUE));

  g_assert_cmpint (
    transaction.state,
    ==,
    GOODIX5135_OTP_READ_TRANSACTION_FAILED);
}


static void
test_goodix5135_prepare_config_upload (void)
{
  guint8 template_data[GOODIX5135_CONFIG_LENGTH] = { 0 };
  guint8 runtime_config[GOODIX5135_CONFIG_LENGTH] = { 0 };
  guint8 transfer[GOODIX5135_CONFIG_TRANSFER_LENGTH] = { 0 };
  Goodix5135ConfigUploadTransaction transaction;
  Goodix5135OtpCalibration calibration = {
    .tcode = 0x0100,
    .fdt_delta = 0x12,
    .fdt_offset = 0,
    .dac0 = 0x0456,
    .dac1 = 0x22,
    .dac2 = 0x33,
    .dac3 = 0x44,
  };
  gsize logical_length = 0;
  gsize transport_length = 0;
  guint16 checksum = 0;

  /*
   * Synthetic protocol fixture only.
   * This is not the vendor CFG70 blob.
   */
  template_data[0x00] = 0x70;
  template_data[0x01] = 0x11;
  template_data[0x02] = 0x74;
  template_data[0x03] = 0x85;

  /* 0x005c -> 0x0180 */
  template_data[0x71] = 0x5c;
  template_data[0x72] = 0x00;
  template_data[0x73] = 0x80;
  template_data[0x74] = 0x01;

  /* 0x0220 -> 0x0808 */
  template_data[0x75] = 0x20;
  template_data[0x76] = 0x02;
  template_data[0x77] = 0x08;
  template_data[0x78] = 0x08;

  /* 0x0236 -> 0x0080 */
  template_data[0x79] = 0x36;
  template_data[0x7a] = 0x02;
  template_data[0x7b] = 0x80;
  template_data[0x7c] = 0x00;

  /* 0x0238 -> 0x0080 */
  template_data[0x7d] = 0x38;
  template_data[0x7e] = 0x02;
  template_data[0x7f] = 0x80;
  template_data[0x80] = 0x00;

  /* 0x023a -> 0x0080 */
  template_data[0x81] = 0x3a;
  template_data[0x82] = 0x02;
  template_data[0x83] = 0x80;
  template_data[0x84] = 0x00;

  /* 0x0082 -> 0x1580 */
  template_data[0xad] = 0x82;
  template_data[0xae] = 0x00;
  template_data[0xaf] = 0x80;
  template_data[0xb0] = 0x15;

  g_assert_true (
      goodix5135_prepare_config_upload (
          template_data,
          sizeof (template_data),
          &calibration,
          runtime_config,
          sizeof (runtime_config),
          &transaction,
          transfer,
          sizeof (transfer),
          &logical_length,
          &transport_length));

  g_assert_cmpint (
      transaction.state,
      ==,
      GOODIX5135_CONFIG_UPLOAD_TRANSACTION_WAIT_OUT);

  g_assert_cmpuint (
      transaction.packets_completed,
      ==,
      0);

  g_assert_cmpuint (
      logical_length,
      ==,
      GOODIX5135_CONFIG_LOGICAL_LENGTH);

  g_assert_cmpuint (
      transport_length,
      ==,
      GOODIX5135_CONFIG_TRANSFER_LENGTH);

  g_assert_true (
      goodix5135_cfg70_checksum (
          runtime_config,
          sizeof (runtime_config),
          &checksum));

  g_assert_cmpuint (
      runtime_config[222] |
      ((guint16) runtime_config[223] << 8),
      ==,
      checksum);

  g_assert_cmpuint (
      runtime_config[0x73] |
      ((guint16) runtime_config[0x74] << 8),
      ==,
      calibration.tcode);

  g_assert_cmpuint (
      runtime_config[0x77] |
      ((guint16) runtime_config[0x78] << 8),
      ==,
      calibration.dac0);

  g_assert_false (
      goodix5135_prepare_config_upload (
          template_data,
          sizeof (template_data),
          &calibration,
          runtime_config,
          GOODIX5135_CONFIG_LENGTH - 1,
          &transaction,
          transfer,
          sizeof (transfer),
          &logical_length,
          &transport_length));

  g_assert_false (
      goodix5135_prepare_config_upload (
          template_data,
          sizeof (template_data),
          &calibration,
          runtime_config,
          sizeof (runtime_config),
          &transaction,
          transfer,
          GOODIX5135_CONFIG_TRANSFER_LENGTH - 1,
          &logical_length,
          &transport_length));
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

  g_test_add_func ("/goodix5135/proto/firmware-ack/bit1-clear", test_firmware_ack_bit1_clear);
  g_test_add_func ("/goodix5135/proto/firmware-ack/extended-payload",
                   test_firmware_ack_extended_payload);

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


  g_test_add_func ("/goodix5135/proto/firmware-transaction/happy-path",
                   test_firmware_transaction_happy_path);

  g_test_add_func ("/goodix5135/proto/firmware-transaction/extended-ack",
                   test_firmware_transaction_extended_ack);

  g_test_add_func ("/goodix5135/proto/firmware-transaction/out-transport-failure",
                   test_firmware_transaction_out_transport_failure);

  g_test_add_func ("/goodix5135/proto/firmware-transaction/ack-transport-failure",
                   test_firmware_transaction_ack_transport_failure);

  g_test_add_func ("/goodix5135/proto/firmware-transaction/negative-ack",
                   test_firmware_transaction_negative_ack);

  g_test_add_func ("/goodix5135/proto/firmware-transaction/response-transport-failure",
                   test_firmware_transaction_response_transport_failure);

  g_test_add_func ("/goodix5135/proto/firmware-transaction/invalid-response",
                   test_firmware_transaction_invalid_response);

  g_test_add_func ("/goodix5135/proto/firmware-transaction/invalid-order",
                   test_firmware_transaction_invalid_order);

  g_test_add_func ("/goodix5135/proto/firmware-transaction/begin-failure",
                   test_firmware_transaction_begin_failure);

  g_test_add_func ("/goodix5135/proto/register-read-request",

                   test_register_read_request);

  g_test_add_func ("/goodix5135/proto/register-read-request-0220",

                   test_register_read_request_0220);

  g_test_add_func ("/goodix5135/proto/register-read-zero-length",

                   test_register_read_request_rejects_zero_length);

  g_test_add_func ("/goodix5135/proto/register-read-ack",

                   test_register_read_ack);

  g_test_add_func ("/goodix5135/proto/register-read-response",

                   test_register_read_response);


  g_test_add_func ("/goodix5135/proto/register-transaction/happy-path",


                   test_register_transaction_happy_path);


  g_test_add_func ("/goodix5135/proto/register-transaction/out-failure",


                   test_register_transaction_out_failure);


  g_test_add_func ("/goodix5135/proto/register-transaction/ack-failure",


                   test_register_transaction_ack_failure);


  g_test_add_func ("/goodix5135/proto/register-transaction/negative-ack",


                   test_register_transaction_negative_ack);


  g_test_add_func ("/goodix5135/proto/register-transaction/response-failure",


                   test_register_transaction_response_failure);


  g_test_add_func ("/goodix5135/proto/register-transaction/short-response",


                   test_register_transaction_short_response);


  g_test_add_func ("/goodix5135/proto/register-transaction/invalid-order",


                   test_register_transaction_invalid_order);


  g_test_add_func ("/goodix5135/proto/register-transaction/begin-failure",


                   test_register_transaction_begin_failure);



  g_test_add_func ("/goodix5135/proto/mcu-state/request-vector",



                   test_mcu_state_request_vector);



  g_test_add_func ("/goodix5135/proto/mcu-state/request-arguments",



                   test_mcu_state_request_arguments);



  g_test_add_func ("/goodix5135/proto/mcu-state/ack",



                   test_mcu_state_ack);



  g_test_add_func ("/goodix5135/proto/mcu-state/response",



                   test_mcu_state_response);



  g_test_add_func ("/goodix5135/proto/mcu-state-transaction/happy-path",



                   test_mcu_state_transaction_happy_path);



  g_test_add_func ("/goodix5135/proto/mcu-state-transaction/out-failure",



                   test_mcu_state_transaction_out_failure);



  g_test_add_func ("/goodix5135/proto/mcu-state-transaction/ack-failure",



                   test_mcu_state_transaction_ack_failure);



  g_test_add_func ("/goodix5135/proto/mcu-state-transaction/response-failure",



                   test_mcu_state_transaction_response_failure);



  g_test_add_func ("/goodix5135/proto/mcu-state-transaction/invalid-order",



                   test_mcu_state_transaction_invalid_order);




  g_test_add_func ("/goodix5135/proto/nop/request-vector",




                   test_nop_request_vector);




  g_test_add_func ("/goodix5135/proto/nop/request-arguments",




                   test_nop_request_arguments);




  g_test_add_func ("/goodix5135/proto/nop/ack",




                   test_nop_ack);




  g_test_add_func ("/goodix5135/proto/nop-transaction/ack-success",




                   test_nop_transaction_ack_success);




  g_test_add_func ("/goodix5135/proto/nop-transaction/timeout-success",




                   test_nop_transaction_timeout_success);




  g_test_add_func ("/goodix5135/proto/nop-transaction/bad-ack",




                   test_nop_transaction_bad_ack);




  g_test_add_func ("/goodix5135/proto/nop-transaction/transport-failure",




                   test_nop_transaction_transport_failure);




  g_test_add_func ("/goodix5135/proto/nop-transaction/out-failure",




                   test_nop_transaction_out_failure);




  g_test_add_func ("/goodix5135/proto/nop-transaction/invalid-order",




                   test_nop_transaction_invalid_order);





  g_test_add_func ("/goodix5135/proto/d4/request-vector",





                   test_d4_request_vector);





  g_test_add_func ("/goodix5135/proto/d4/request-arguments",





                   test_d4_request_arguments);





  g_test_add_func ("/goodix5135/proto/d4/ack-success",





                   test_d4_ack_success);





  g_test_add_func ("/goodix5135/proto/d4/ack-negative",





                   test_d4_ack_negative);





  g_test_add_func ("/goodix5135/proto/d4-transaction/happy-path",





                   test_d4_transaction_happy_path);





  g_test_add_func ("/goodix5135/proto/d4-transaction/out-failure",





                   test_d4_transaction_out_failure);





  g_test_add_func ("/goodix5135/proto/d4-transaction/ack-transport-failure",





                   test_d4_transaction_ack_transport_failure);





  g_test_add_func ("/goodix5135/proto/d4-transaction/bad-ack",





                   test_d4_transaction_bad_ack);





  g_test_add_func ("/goodix5135/proto/d4-transaction/invalid-order",





                   test_d4_transaction_invalid_order);






  g_test_add_func ("/goodix5135/proto/enable-chip/request-true",






                   test_enable_chip_request_true_vector);






  g_test_add_func ("/goodix5135/proto/enable-chip/request-false",






                   test_enable_chip_request_false_vector);






  g_test_add_func ("/goodix5135/proto/enable-chip/request-arguments",






                   test_enable_chip_request_arguments);






  g_test_add_func ("/goodix5135/proto/enable-chip/ack-success",






                   test_enable_chip_ack_success);






  g_test_add_func ("/goodix5135/proto/enable-chip/ack-negative",






                   test_enable_chip_ack_negative);






  g_test_add_func ("/goodix5135/proto/enable-chip-transaction/happy-path",






                   test_enable_chip_transaction_happy_path);






  g_test_add_func ("/goodix5135/proto/enable-chip-transaction/out-failure",






                   test_enable_chip_transaction_out_failure);






  g_test_add_func ("/goodix5135/proto/enable-chip-transaction/ack-transport-failure",






                   test_enable_chip_transaction_ack_transport_failure);






  g_test_add_func ("/goodix5135/proto/enable-chip-transaction/bad-ack",






                   test_enable_chip_transaction_bad_ack);






  g_test_add_func ("/goodix5135/proto/enable-chip-transaction/invalid-order",






                   test_enable_chip_transaction_invalid_order);







  g_test_add_func ("/goodix5135/proto/sensor-reset/request-vector",







                   test_sensor_reset_request_vector);







  g_test_add_func ("/goodix5135/proto/sensor-reset/request-arguments",







                   test_sensor_reset_request_arguments);







  g_test_add_func ("/goodix5135/proto/sensor-reset/ack",







                   test_sensor_reset_ack);







  g_test_add_func ("/goodix5135/proto/sensor-reset/response-success",







                   test_sensor_reset_response_success);







  g_test_add_func ("/goodix5135/proto/sensor-reset/response-negative",







                   test_sensor_reset_response_negative);







  g_test_add_func ("/goodix5135/proto/sensor-reset/response-short",







                   test_sensor_reset_response_short);







  g_test_add_func ("/goodix5135/proto/sensor-reset-transaction/happy-path",







                   test_sensor_reset_transaction_happy_path);







  g_test_add_func ("/goodix5135/proto/sensor-reset-transaction/out-failure",







                   test_sensor_reset_transaction_out_failure);







  g_test_add_func ("/goodix5135/proto/sensor-reset-transaction/ack-failure",







                   test_sensor_reset_transaction_ack_failure);







  g_test_add_func ("/goodix5135/proto/sensor-reset-transaction/response-failure",







                   test_sensor_reset_transaction_response_failure);







  g_test_add_func ("/goodix5135/proto/sensor-reset-transaction/invalid-order",







                   test_sensor_reset_transaction_invalid_order);








  g_test_add_func ("/goodix5135/proto/activation-sequence/happy-path",








                   test_activation_sequence_happy_path);








  g_test_add_func ("/goodix5135/proto/activation-sequence/begin-twice",








                   test_activation_sequence_begin_twice);








  g_test_add_func ("/goodix5135/proto/activation-sequence/wrong-first-command",








                   test_activation_sequence_wrong_first_command);








  g_test_add_func ("/goodix5135/proto/activation-sequence/nop-failure",








                   test_activation_sequence_nop_failure);








  g_test_add_func ("/goodix5135/proto/activation-sequence/d4-failure",








                   test_activation_sequence_d4_failure);








  g_test_add_func ("/goodix5135/proto/activation-sequence/enable-failure",








                   test_activation_sequence_enable_failure);








  g_test_add_func ("/goodix5135/proto/activation-sequence/firmware-failure",








                   test_activation_sequence_firmware_failure);








  g_test_add_func ("/goodix5135/proto/activation-sequence/reset-failure",








                   test_activation_sequence_reset_failure);








  g_test_add_func ("/goodix5135/proto/activation-sequence/wrong-chip-id",








                   test_activation_sequence_wrong_chip_id);








  g_test_add_func ("/goodix5135/proto/activation-sequence/short-chip-id",








                   test_activation_sequence_short_chip_id);









  g_test_add_func ("/goodix5135/proto/config-upload/request-zero-vector",









                   test_config_upload_request_zero_vector);









  g_test_add_func ("/goodix5135/proto/config-upload/request-pattern",









                   test_config_upload_request_pattern);









  g_test_add_func ("/goodix5135/proto/config-upload/request-arguments",









                   test_config_upload_request_arguments);









  g_test_add_func ("/goodix5135/proto/config-upload/packet-view-first",









                   test_config_upload_packet_view_first);









  g_test_add_func ("/goodix5135/proto/config-upload/packet-view-last",









                   test_config_upload_packet_view_last);









  g_test_add_func ("/goodix5135/proto/config-upload/packet-view-invalid",









                   test_config_upload_packet_view_invalid);









  g_test_add_func ("/goodix5135/proto/config-upload/ack-success",









                   test_config_upload_ack_success);









  g_test_add_func ("/goodix5135/proto/config-upload/ack-negative",









                   test_config_upload_ack_negative);









  g_test_add_func ("/goodix5135/proto/config-upload/response-success",









                   test_config_upload_response_success);









  g_test_add_func ("/goodix5135/proto/config-upload/response-negative",









                   test_config_upload_response_negative);









  g_test_add_func ("/goodix5135/proto/config-upload-transaction/happy-path",









                   test_config_upload_transaction_happy_path);









  g_test_add_func ("/goodix5135/proto/config-upload-transaction/out-failure",









                   test_config_upload_transaction_out_failure);









  g_test_add_func ("/goodix5135/proto/config-upload-transaction/invalid-order",









                   test_config_upload_transaction_invalid_order);










  g_test_add_func ("/goodix5135/proto/cfg70/otp-valid",










                   test_cfg70_otp_valid);










  g_test_add_func ("/goodix5135/proto/cfg70/otp-bad-length",










                   test_cfg70_otp_bad_length);










  g_test_add_func ("/goodix5135/proto/cfg70/otp-crc-failure",










                   test_cfg70_otp_crc_failure);










  g_test_add_func ("/goodix5135/proto/cfg70/otp-dac-mirror-failure",










                   test_cfg70_otp_dac_mirror_failure);










  g_test_add_func ("/goodix5135/proto/cfg70/otp-calibration",










                   test_cfg70_otp_calibration);










  g_test_add_func ("/goodix5135/proto/cfg70/template-valid",










                   test_cfg70_template_valid);










  g_test_add_func ("/goodix5135/proto/cfg70/template-bad-prefix",










                   test_cfg70_template_bad_prefix);










  g_test_add_func ("/goodix5135/proto/cfg70/template-bad-register-layout",










                   test_cfg70_template_bad_register_layout);










  g_test_add_func ("/goodix5135/proto/cfg70/checksum",










                   test_cfg70_checksum);










  g_test_add_func ("/goodix5135/proto/cfg70/runtime-config",










                   test_cfg70_runtime_config);










  g_test_add_func ("/goodix5135/proto/cfg70/runtime-invalid-template",










                   test_cfg70_runtime_invalid_template);










  g_test_add_func ("/goodix5135/proto/cfg70/runtime-arguments",










                   test_cfg70_runtime_arguments);











  g_test_add_func ("/goodix5135/proto/otp-read/request-vector",











                   test_otp_read_request_vector);











  g_test_add_func ("/goodix5135/proto/otp-read/request-arguments",











                   test_otp_read_request_arguments);











  g_test_add_func ("/goodix5135/proto/otp-read/ack-success",











                   test_otp_read_ack_success);











  g_test_add_func ("/goodix5135/proto/otp-read/ack-negative",











                   test_otp_read_ack_negative);











  g_test_add_func ("/goodix5135/proto/otp-read/response-64-bytes",











                   test_otp_read_response_64_bytes);











  g_test_add_func ("/goodix5135/proto/otp-read/response-wrong-length",











                   test_otp_read_response_wrong_length);











  g_test_add_func ("/goodix5135/proto/otp-read-transaction/happy-path",











                   test_otp_read_transaction_happy_path);











  g_test_add_func ("/goodix5135/proto/otp-read-transaction/out-failure",











                   test_otp_read_transaction_out_failure);











  g_test_add_func ("/goodix5135/proto/otp-read-transaction/ack-failure",











                   test_otp_read_transaction_ack_failure);











  g_test_add_func ("/goodix5135/proto/otp-read-transaction/response-failure",











                   test_otp_read_transaction_response_failure);











  g_test_add_func ("/goodix5135/proto/otp-read-transaction/invalid-order",











                   test_otp_read_transaction_invalid_order);












  g_test_add_func (
        "/goodix5135/proto/config-upload/prepare-host-seam",
        test_goodix5135_prepare_config_upload);

  return g_test_run ();
}
