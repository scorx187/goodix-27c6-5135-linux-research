/*
 * Goodix 27c6:5135 ChicagoHU protocol helpers.
 *
 * Host-side protocol construction/parsing only at this stage.
 * No USB transfer, TLS session, or device command is implemented here.
 */

#pragma once

#include <glib.h>

#define GOODIX5135_PACK_FLAGS_MESSAGE_PROTOCOL  0xa0U
#define GOODIX5135_COMMAND_FIRMWARE_VERSION     0xa8U
#define GOODIX5135_COMMAND_ACK                  0xb0U
#define GOODIX5135_ACK_PAYLOAD_LENGTH           2U
#define GOODIX5135_USB_PACKET_LENGTH            64U
#define GOODIX5135_FIRMWARE_REQUEST_LENGTH      10U

#define GOODIX5135_IMAGE_COMMAND             0x20U
#define GOODIX5135_PROTOCOL_TRAILER          0x88U

#define GOODIX5135_IMAGE_DECLARED_LENGTH     7690U
#define GOODIX5135_IMAGE_TOTAL_LENGTH        7693U

#define GOODIX5135_IMAGE_METADATA_LENGTH     5U
#define GOODIX5135_IMAGE_PACKED_LENGTH       7680U
#define GOODIX5135_IMAGE_STORED_CRC_LENGTH   4U

typedef struct
{
  const guint8 *metadata;
  gsize         metadata_length;

  const guint8 *packed_raw12;
  gsize         packed_raw12_length;

  const guint8 *stored_crc;
  gsize         stored_crc_length;
} Goodix5135ImageFrame;

const char *goodix5135_proto_stage_name (void);

/*
 * Build the wrapped, USB-padded read-only firmware-version request.
 *
 * Logical Goodix frame: 10 bytes.
 * USB wire packet:      64 bytes, zero padded.
 */
gboolean goodix5135_build_firmware_version_request (
  guint8 *packet,
  gsize   packet_size,
  gsize  *logical_length);


/*
 * Parse the firmware-version ACK.
 *
 * The ACK is accepted only when:
 *   - wrapped framing/checksums are valid;
 *   - outer flags are 0xa0;
 *   - protocol command is 0xb0;
 *   - acknowledged command is 0xa8;
 *   - ACK valid bit is set;
 *   - ACK success bit is set.
 *
 * A structurally valid negative ACK must return FALSE.
 */
gboolean goodix5135_parse_firmware_version_ack (
  const guint8 *data,
  gsize         data_length);

/*
 * Parse a wrapped command-0xa8 firmware-version response.
 *
 * @firmware and @firmware_length are borrowed views into @data.
 * The returned length stops before the first NUL when present.
 * No response bytes are logged or copied by this helper.
 */
gboolean goodix5135_parse_firmware_version_response (
  const guint8  *data,
  gsize          data_length,
  const guint8 **firmware,
  gsize         *firmware_length);

/*
 * Parse a fully decrypted command-0x20 response.
 *
 * The returned pointers are borrowed views into @data.
 * No biometric bytes are copied or logged by this helper.
 */
gboolean goodix5135_parse_image_frame (const guint8       *data,
                                       gsize               data_length,
                                       Goodix5135ImageFrame *frame);
