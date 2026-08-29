/*
 * Goodix 27c6:5135 ChicagoHU protocol helpers.
 *
 * Host-side protocol construction/parsing only at this stage.
 * No USB transfer, TLS session, or device command is implemented here.
 */

#pragma once

#include <glib.h>

#define GOODIX5135_PACK_FLAGS_MESSAGE_PROTOCOL  0xa0U
#define GOODIX5135_COMMAND_NOP                  0x00U
#define GOODIX5135_COMMAND_READ_SENSOR_REGISTER 0x82U
#define GOODIX5135_COMMAND_FIRMWARE_VERSION     0xa8U
#define GOODIX5135_COMMAND_QUERY_MCU_STATE      0xaeU
#define GOODIX5135_COMMAND_ACK                  0xb0U
#define GOODIX5135_ACK_MIN_PAYLOAD_LENGTH       2U
#define GOODIX5135_USB_PACKET_LENGTH            64U
#define GOODIX5135_NOP_REQUEST_LENGTH           12U
#define GOODIX5135_REGISTER_READ_REQUEST_LENGTH 12U
#define GOODIX5135_FIRMWARE_REQUEST_LENGTH      10U
#define GOODIX5135_MCU_STATE_REQUEST_LENGTH      9U

#define GOODIX5135_IMAGE_COMMAND             0x20U
#define GOODIX5135_PROTOCOL_TRAILER          0x88U

#define GOODIX5135_IMAGE_DECLARED_LENGTH     7690U
#define GOODIX5135_IMAGE_TOTAL_LENGTH        7693U

#define GOODIX5135_IMAGE_METADATA_LENGTH     5U
#define GOODIX5135_IMAGE_PACKED_LENGTH       7680U
#define GOODIX5135_IMAGE_STORED_CRC_LENGTH   4U

typedef enum
{
  GOODIX5135_FIRMWARE_TRANSACTION_IDLE = 0,
  GOODIX5135_FIRMWARE_TRANSACTION_WAIT_OUT,
  GOODIX5135_FIRMWARE_TRANSACTION_WAIT_ACK,
  GOODIX5135_FIRMWARE_TRANSACTION_WAIT_RESPONSE,
  GOODIX5135_FIRMWARE_TRANSACTION_DONE,
  GOODIX5135_FIRMWARE_TRANSACTION_FAILED,
} Goodix5135FirmwareTransactionState;

typedef struct
{
  Goodix5135FirmwareTransactionState state;
} Goodix5135FirmwareTransaction;

typedef enum
{
  GOODIX5135_REGISTER_READ_TRANSACTION_IDLE = 0,
  GOODIX5135_REGISTER_READ_TRANSACTION_WAIT_OUT,
  GOODIX5135_REGISTER_READ_TRANSACTION_WAIT_ACK,
  GOODIX5135_REGISTER_READ_TRANSACTION_WAIT_RESPONSE,
  GOODIX5135_REGISTER_READ_TRANSACTION_DONE,
  GOODIX5135_REGISTER_READ_TRANSACTION_FAILED,
} Goodix5135RegisterReadTransactionState;

typedef struct
{
  Goodix5135RegisterReadTransactionState state;
  gsize                                  expected_length;
} Goodix5135RegisterReadTransaction;

typedef enum
{
  GOODIX5135_NOP_REPLY_RECEIVED = 0,
  GOODIX5135_NOP_REPLY_TIMEOUT,
  GOODIX5135_NOP_REPLY_TRANSPORT_FAILURE,
} Goodix5135NopReplyResult;

typedef enum
{
  GOODIX5135_NOP_TRANSACTION_IDLE = 0,
  GOODIX5135_NOP_TRANSACTION_WAIT_OUT,
  GOODIX5135_NOP_TRANSACTION_WAIT_OPTIONAL_ACK,
  GOODIX5135_NOP_TRANSACTION_DONE,
  GOODIX5135_NOP_TRANSACTION_FAILED,
} Goodix5135NopTransactionState;

typedef struct
{
  Goodix5135NopTransactionState state;
} Goodix5135NopTransaction;

typedef enum
{
  GOODIX5135_MCU_STATE_TRANSACTION_IDLE = 0,
  GOODIX5135_MCU_STATE_TRANSACTION_WAIT_OUT,
  GOODIX5135_MCU_STATE_TRANSACTION_WAIT_ACK,
  GOODIX5135_MCU_STATE_TRANSACTION_WAIT_RESPONSE,
  GOODIX5135_MCU_STATE_TRANSACTION_DONE,
  GOODIX5135_MCU_STATE_TRANSACTION_FAILED,
} Goodix5135McuStateTransactionState;

typedef struct
{
  Goodix5135McuStateTransactionState state;
} Goodix5135McuStateTransaction;

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
 * Build a wrapped single-register READ_SENSOR_REGISTER (0x82) request.
 *
 * Payload:
 *
 *   00                single-register mode
 *   address_lo
 *   address_hi
 *   read_length
 *
 * Logical Goodix frame: 12 bytes.
 * USB wire packet:      64 bytes, zero padded.
 *
 * This helper performs no USB operation.
 */
gboolean goodix5135_build_read_sensor_register_request (
  guint16  address,
  guint8   read_length,
  guint8  *packet,
  gsize    packet_size,
  gsize   *logical_length);

/*
 * Parse the ACK belonging to READ_SENSOR_REGISTER (0x82).
 */
gboolean goodix5135_parse_read_sensor_register_ack (
  const guint8 *data,
  gsize         data_length);

/*
 * Parse a wrapped READ_SENSOR_REGISTER (0x82) response.
 *
 * @value is a borrowed view into @data.
 * The response must contain at least @minimum_length bytes.
 * No register bytes are copied or logged.
 */
gboolean goodix5135_parse_read_sensor_register_response (
  const guint8  *data,
  gsize          data_length,
  gsize          minimum_length,
  const guint8 **value,
  gsize         *value_length);


/*
 * Host-only READ_SENSOR_REGISTER transaction controller.
 *
 * IDLE
 *   -> WAIT_OUT
 *   -> WAIT_ACK
 *   -> WAIT_RESPONSE
 *   -> DONE
 *
 * Any invalid order, transport error, ACK failure, malformed response,
 * or short response transitions to FAILED.
 *
 * This layer performs no USB operation.
 */
void goodix5135_register_read_transaction_init (
  Goodix5135RegisterReadTransaction *transaction);

gboolean goodix5135_register_read_transaction_begin (
  Goodix5135RegisterReadTransaction *transaction,
  guint16                            address,
  guint8                             read_length,
  guint8                            *packet,
  gsize                              packet_size,
  gsize                             *logical_length);

gboolean goodix5135_register_read_transaction_out_complete (
  Goodix5135RegisterReadTransaction *transaction,
  gboolean                           transport_can_advance);

gboolean goodix5135_register_read_transaction_ack_complete (
  Goodix5135RegisterReadTransaction *transaction,
  gboolean                           transport_can_advance,
  const guint8                      *data,
  gsize                              data_length);

gboolean goodix5135_register_read_transaction_response_complete (
  Goodix5135RegisterReadTransaction *transaction,
  gboolean                           transport_can_advance,
  const guint8                      *data,
  gsize                              data_length,
  const guint8                     **value,
  gsize                             *value_length);


/*
 * Build the reference Goodix NOP request.
 *
 * Payload:
 *   00 00 00 00
 *
 * The reference NOP uses the protocol no-checksum trailer 0x88.
 *
 * Logical frame: 12 bytes.
 * USB packet:    64 bytes, zero padded.
 *
 * This helper performs no USB operation.
 */
gboolean goodix5135_build_nop_request (
  guint8 *packet,
  gsize   packet_size,
  gsize  *logical_length);

/*
 * Parse an optional ACK belonging to NOP command 0x00.
 */
gboolean goodix5135_parse_nop_ack (
  const guint8 *data,
  gsize         data_length);

/*
 * Host-only NOP transaction policy.
 *
 * Reference behavior:
 *
 *   IDLE
 *    -> WAIT_OUT
 *    -> WAIT_OPTIONAL_ACK
 *
 * At WAIT_OPTIONAL_ACK:
 *
 *   valid ACK       -> DONE
 *   expected timeout -> DONE
 *   malformed ACK   -> FAILED
 *   other transport failure -> FAILED
 */
void goodix5135_nop_transaction_init (
  Goodix5135NopTransaction *transaction);

gboolean goodix5135_nop_transaction_begin (
  Goodix5135NopTransaction *transaction,
  guint8                   *packet,
  gsize                     packet_size,
  gsize                    *logical_length);

gboolean goodix5135_nop_transaction_out_complete (
  Goodix5135NopTransaction *transaction,
  gboolean                  transport_can_advance);

gboolean goodix5135_nop_transaction_reply_complete (
  Goodix5135NopTransaction *transaction,
  Goodix5135NopReplyResult  result,
  const guint8             *data,
  gsize                     data_length);


/*
 * Build QUERY_MCU_STATE (0xae).
 *
 * The reference read-only state probe uses query byte 0x55.
 *
 * Logical Goodix frame: 9 bytes.
 * USB wire packet:      64 bytes, zero padded.
 *
 * This helper performs no USB operation.
 */
gboolean goodix5135_build_mcu_state_request (
  guint8  query,
  guint8 *packet,
  gsize   packet_size,
  gsize  *logical_length);

/*
 * Parse ACK for QUERY_MCU_STATE (0xae).
 */
gboolean goodix5135_parse_mcu_state_ack (
  const guint8 *data,
  gsize         data_length);

/*
 * Parse wrapped QUERY_MCU_STATE response.
 *
 * @state_data is a borrowed view into @data.
 * At least one response byte is required.
 * No bytes are copied or logged.
 */
gboolean goodix5135_parse_mcu_state_response (
  const guint8  *data,
  gsize          data_length,
  const guint8 **state_data,
  gsize         *state_length);

/*
 * Host-only 0xae transaction controller.
 *
 * IDLE
 *   -> WAIT_OUT
 *   -> WAIT_ACK
 *   -> WAIT_RESPONSE
 *   -> DONE
 *
 * Invalid order, transport failure, ACK failure,
 * or invalid/empty response -> FAILED.
 */
void goodix5135_mcu_state_transaction_init (
  Goodix5135McuStateTransaction *transaction);

gboolean goodix5135_mcu_state_transaction_begin (
  Goodix5135McuStateTransaction *transaction,
  guint8                         query,
  guint8                        *packet,
  gsize                          packet_size,
  gsize                         *logical_length);

gboolean goodix5135_mcu_state_transaction_out_complete (
  Goodix5135McuStateTransaction *transaction,
  gboolean                       transport_can_advance);

gboolean goodix5135_mcu_state_transaction_ack_complete (
  Goodix5135McuStateTransaction *transaction,
  gboolean                       transport_can_advance,
  const guint8                  *data,
  gsize                          data_length);

gboolean goodix5135_mcu_state_transaction_response_complete (
  Goodix5135McuStateTransaction *transaction,
  gboolean                       transport_can_advance,
  const guint8                  *data,
  gsize                          data_length,
  const guint8                 **state_data,
  gsize                         *state_length);


/*
 * Host-only transaction controller for the first read-only command.
 *
 * @transport_can_advance MUST be the result of the transport policy
 * represented by:
 *
 *   goodix5135_async_result_can_advance(completion, error)
 *
 * This keeps transport completion semantics separate from protocol parsing.
 */
void goodix5135_firmware_transaction_init (
  Goodix5135FirmwareTransaction *transaction);

gboolean goodix5135_firmware_transaction_begin (
  Goodix5135FirmwareTransaction *transaction,
  guint8                        *packet,
  gsize                          packet_size,
  gsize                         *logical_length);

gboolean goodix5135_firmware_transaction_out_complete (
  Goodix5135FirmwareTransaction *transaction,
  gboolean                       transport_can_advance);

gboolean goodix5135_firmware_transaction_ack_complete (
  Goodix5135FirmwareTransaction *transaction,
  gboolean                       transport_can_advance,
  const guint8                  *data,
  gsize                          data_length);

gboolean goodix5135_firmware_transaction_response_complete (
  Goodix5135FirmwareTransaction *transaction,
  gboolean                       transport_can_advance,
  const guint8                  *data,
  gsize                          data_length,
  const guint8                 **firmware,
  gsize                         *firmware_length);

/*
 * Parse a fully decrypted command-0x20 response.
 *
 * The returned pointers are borrowed views into @data.
 * No biometric bytes are copied or logged by this helper.
 */
gboolean goodix5135_parse_image_frame (const guint8       *data,
                                       gsize               data_length,
                                       Goodix5135ImageFrame *frame);
