/*
 * End-to-end host-only image response tests for Goodix 27c6:5135.
 *
 * All payloads are synthetic.
 */

#include <glib.h>
#include <string.h>

#include "../libfprint/drivers/goodix5135/goodix5135-image-response.h"
#include "../libfprint/drivers/goodix5135/goodix5135-image.h"
#include "../libfprint/drivers/goodix5135/goodix5135-proto.h"

static const guint8 synthetic_raw12_group[6] = {
  0xa5, 0xbc, 0xde, 0x12, 0x34, 0xf6
};

static void
write_stored_crc (guint8 *stored,
                  guint32 crc)
{
  /*
   * Parser reconstructs:
   *
   *   stored[2]<<24 |
   *   stored[3]<<16 |
   *   stored[0]<<8  |
   *   stored[1]
   */
  stored[0] = (guint8) ((crc >> 8) & 0xff);
  stored[1] = (guint8) (crc & 0xff);
  stored[2] = (guint8) ((crc >> 24) & 0xff);
  stored[3] = (guint8) ((crc >> 16) & 0xff);
}

static void
build_valid_response (guint8 *data)
{
  guint8 *packed;
  guint8 *stored_crc;
  guint32 crc;
  gsize offset;
  gsize i;

  memset (data, 0, GOODIX5135_IMAGE_TOTAL_LENGTH);

  data[0] = GOODIX5135_IMAGE_COMMAND;

  data[1] =
    (guint8) (GOODIX5135_IMAGE_DECLARED_LENGTH & 0xff);
  data[2] =
    (guint8) ((GOODIX5135_IMAGE_DECLARED_LENGTH >> 8) & 0xff);

  for (i = 0; i < GOODIX5135_IMAGE_METADATA_LENGTH; i++)
    data[3 + i] = (guint8) (0x40 + i);

  packed =
    data + 3 + GOODIX5135_IMAGE_METADATA_LENGTH;

  for (offset = 0;
       offset < GOODIX5135_IMAGE_PACKED_LENGTH;
       offset += sizeof (synthetic_raw12_group))
    {
      memcpy (packed + offset,
              synthetic_raw12_group,
              sizeof (synthetic_raw12_group));
    }

  crc = goodix5135_crc32_mpeg2 (
          packed,
          GOODIX5135_IMAGE_PACKED_LENGTH);

  stored_crc =
    packed + GOODIX5135_IMAGE_PACKED_LENGTH;

  write_stored_crc (stored_crc, crc);

  data[GOODIX5135_IMAGE_TOTAL_LENGTH - 1] =
    GOODIX5135_PROTOCOL_TRAILER;
}

static guint16
expected_transport_pixel (gsize index)
{
  switch (index % 4)
    {
    case 0:
      return 0x5bc;

    case 1:
      return 0x12a;

    case 2:
      return 0x6de;

    case 3:
      return 0x34f;

    default:
      g_assert_not_reached ();
    }
}

static void
test_valid_pipeline (void)
{
  guint8 data[GOODIX5135_IMAGE_TOTAL_LENGTH];
  guint16 output[GOODIX5135_IMAGE_PIXELS];
  gsize n;

  build_valid_response (data);
  memset (output, 0, sizeof (output));

  g_assert_true (goodix5135_decode_image_response (
                   data,
                   sizeof (data),
                   output,
                   G_N_ELEMENTS (output)));

  /*
   * Verify the complete transport->ChicagoHU mapping, not only corners.
   */
  for (n = 0; n < GOODIX5135_IMAGE_PIXELS; n++)
    {
      const gsize dst_index =
        (n % 64) * GOODIX5135_IMAGE_WIDTH + (n / 64);

      g_assert_cmpuint (output[dst_index],
                        ==,
                        expected_transport_pixel (n));
    }
}

static void
test_packed_data_crc_corruption (void)
{
  guint8 data[GOODIX5135_IMAGE_TOTAL_LENGTH];
  guint16 output[GOODIX5135_IMAGE_PIXELS];
  guint8 *packed;

  build_valid_response (data);

  packed =
    data + 3 + GOODIX5135_IMAGE_METADATA_LENGTH;

  /*
   * Change packed data after the CRC has been generated.
   */
  packed[1234] ^= 0x01;

  g_assert_false (goodix5135_decode_image_response (
                    data,
                    sizeof (data),
                    output,
                    G_N_ELEMENTS (output)));
}

static void
test_stored_crc_corruption (void)
{
  guint8 data[GOODIX5135_IMAGE_TOTAL_LENGTH];
  guint16 output[GOODIX5135_IMAGE_PIXELS];

  build_valid_response (data);

  data[GOODIX5135_IMAGE_TOTAL_LENGTH - 2] ^= 0x01;

  g_assert_false (goodix5135_decode_image_response (
                    data,
                    sizeof (data),
                    output,
                    G_N_ELEMENTS (output)));
}

static void
test_bad_frame (void)
{
  guint8 data[GOODIX5135_IMAGE_TOTAL_LENGTH];
  guint16 output[GOODIX5135_IMAGE_PIXELS];

  build_valid_response (data);

  data[GOODIX5135_IMAGE_TOTAL_LENGTH - 1] = 0x00;

  g_assert_false (goodix5135_decode_image_response (
                    data,
                    sizeof (data),
                    output,
                    G_N_ELEMENTS (output)));
}

static void
test_wrong_total_length (void)
{
  guint8 data[GOODIX5135_IMAGE_TOTAL_LENGTH];
  guint16 output[GOODIX5135_IMAGE_PIXELS];

  build_valid_response (data);

  g_assert_false (goodix5135_decode_image_response (
                    data,
                    sizeof (data) - 1,
                    output,
                    G_N_ELEMENTS (output)));
}

static void
test_small_output (void)
{
  guint8 data[GOODIX5135_IMAGE_TOTAL_LENGTH];
  guint16 output[GOODIX5135_IMAGE_PIXELS];

  build_valid_response (data);

  g_assert_false (goodix5135_decode_image_response (
                    data,
                    sizeof (data),
                    output,
                    G_N_ELEMENTS (output) - 1));
}

static void
test_null_arguments (void)
{
  guint8 data[GOODIX5135_IMAGE_TOTAL_LENGTH];
  guint16 output[GOODIX5135_IMAGE_PIXELS];

  build_valid_response (data);

  g_assert_false (goodix5135_decode_image_response (
                    NULL,
                    sizeof (data),
                    output,
                    G_N_ELEMENTS (output)));

  g_assert_false (goodix5135_decode_image_response (
                    data,
                    sizeof (data),
                    NULL,
                    G_N_ELEMENTS (output)));
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func (
    "/goodix5135/image-response/valid",
    test_valid_pipeline);

  g_test_add_func (
    "/goodix5135/image-response/packed-crc-corruption",
    test_packed_data_crc_corruption);

  g_test_add_func (
    "/goodix5135/image-response/stored-crc-corruption",
    test_stored_crc_corruption);

  g_test_add_func (
    "/goodix5135/image-response/bad-frame",
    test_bad_frame);

  g_test_add_func (
    "/goodix5135/image-response/wrong-total-length",
    test_wrong_total_length);

  g_test_add_func (
    "/goodix5135/image-response/small-output",
    test_small_output);

  g_test_add_func (
    "/goodix5135/image-response/null-arguments",
    test_null_arguments);

  return g_test_run ();
}
