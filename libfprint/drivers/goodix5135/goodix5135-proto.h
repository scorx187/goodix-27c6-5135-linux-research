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
#define GOODIX5135_COMMAND_UPLOAD_CONFIG_MCU    0x90U
#define GOODIX5135_COMMAND_ENABLE_CHIP          0x96U
#define GOODIX5135_COMMAND_RESET                0xa2U
#define GOODIX5135_COMMAND_FIRMWARE_VERSION     0xa8U
#define GOODIX5135_COMMAND_QUERY_MCU_STATE      0xaeU
#define GOODIX5135_COMMAND_ACK                  0xb0U
#define GOODIX5135_COMMAND_TLS_SUCCESSFULLY_ESTABLISHED 0xd4U
#define GOODIX5135_ACK_MIN_PAYLOAD_LENGTH       2U
#define GOODIX5135_USB_PACKET_LENGTH            64U
#define GOODIX5135_NOP_REQUEST_LENGTH           12U
#define GOODIX5135_REGISTER_READ_REQUEST_LENGTH 12U
#define GOODIX5135_FIRMWARE_REQUEST_LENGTH      10U
#define GOODIX5135_MCU_STATE_REQUEST_LENGTH      9U
#define GOODIX5135_D4_REQUEST_LENGTH             10U
#define GOODIX5135_ENABLE_CHIP_REQUEST_LENGTH    10U
#define GOODIX5135_SENSOR_RESET_REQUEST_LENGTH   10U

/*
 * ChicagoHU runtime configuration transport shape.
 *
 * IMPORTANT:
 *
 * These constants describe LENGTH ONLY.
 * The unit-specific configuration bytes are not stored here.
 *
 * 224-byte payload
 * + command byte
 * + LE16 inner length
 * + protocol checksum
 * + outer 4-byte wrapper
 * = 232 logical bytes
 *
 * USB reference transport pads/splits this to:
 *
 *   4 x 64-byte Bulk OUT transfers = 256 bytes
 */
#define GOODIX5135_CONFIG_LENGTH                 224U
#define GOODIX5135_CONFIG_LOGICAL_LENGTH         232U
#define GOODIX5135_CONFIG_TRANSFER_LENGTH        256U
#define GOODIX5135_CONFIG_USB_PACKET_COUNT       4U

/*
 * Exact historical ChicagoHU reset gate:
 *
 * reset(True, False, 20)
 *
 * bit0 = reset sensor
 * bit1 = soft reset MCU
 * bit2 = reset sensor copy/related block
 *
 * TRUE/FALSE therefore produces flags 0x05.
 */
#define GOODIX5135_SENSOR_RESET_FLAGS            0x05U
#define GOODIX5135_SENSOR_RESET_DELAY            20U

/*
 * Logical ChicagoHU chip identity returned by
 * READ_SENSOR_REGISTER 0x0000 / 4 bytes.
 */
#define GOODIX5135_CHIP_ID_LENGTH                4U

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
  GOODIX5135_ACTIVATION_IDLE = 0,

  GOODIX5135_ACTIVATION_WAIT_NOP1,
  GOODIX5135_ACTIVATION_WAIT_D4,
  GOODIX5135_ACTIVATION_WAIT_NOP2,
  GOODIX5135_ACTIVATION_WAIT_ENABLE_CHIP,
  GOODIX5135_ACTIVATION_WAIT_NOP3,
  GOODIX5135_ACTIVATION_WAIT_FIRMWARE,
  GOODIX5135_ACTIVATION_WAIT_RESET,
  GOODIX5135_ACTIVATION_WAIT_CHIP_ID,

  GOODIX5135_ACTIVATION_DONE,
  GOODIX5135_ACTIVATION_FAILED,
} Goodix5135ActivationSequenceState;

typedef struct
{
  Goodix5135ActivationSequenceState state;
} Goodix5135ActivationSequence;

typedef enum
{
  GOODIX5135_CONFIG_UPLOAD_TRANSACTION_IDLE = 0,
  GOODIX5135_CONFIG_UPLOAD_TRANSACTION_WAIT_OUT,
  GOODIX5135_CONFIG_UPLOAD_TRANSACTION_WAIT_ACK,
  GOODIX5135_CONFIG_UPLOAD_TRANSACTION_WAIT_RESPONSE,
  GOODIX5135_CONFIG_UPLOAD_TRANSACTION_DONE,
  GOODIX5135_CONFIG_UPLOAD_TRANSACTION_FAILED,
} Goodix5135ConfigUploadTransactionState;

typedef struct
{
  Goodix5135ConfigUploadTransactionState state;
  guint                                  packets_completed;
} Goodix5135ConfigUploadTransaction;


typedef enum
{
  GOODIX5135_SENSOR_RESET_TRANSACTION_IDLE = 0,
  GOODIX5135_SENSOR_RESET_TRANSACTION_WAIT_OUT,
  GOODIX5135_SENSOR_RESET_TRANSACTION_WAIT_ACK,
  GOODIX5135_SENSOR_RESET_TRANSACTION_WAIT_RESPONSE,
  GOODIX5135_SENSOR_RESET_TRANSACTION_DONE,
  GOODIX5135_SENSOR_RESET_TRANSACTION_FAILED,
} Goodix5135SensorResetTransactionState;

typedef struct
{
  Goodix5135SensorResetTransactionState state;
} Goodix5135SensorResetTransaction;

typedef enum
{
  GOODIX5135_ENABLE_CHIP_TRANSACTION_IDLE = 0,
  GOODIX5135_ENABLE_CHIP_TRANSACTION_WAIT_OUT,
  GOODIX5135_ENABLE_CHIP_TRANSACTION_WAIT_ACK,
  GOODIX5135_ENABLE_CHIP_TRANSACTION_DONE,
  GOODIX5135_ENABLE_CHIP_TRANSACTION_FAILED,
} Goodix5135EnableChipTransactionState;

typedef struct
{
  Goodix5135EnableChipTransactionState state;
} Goodix5135EnableChipTransaction;

typedef enum
{
  GOODIX5135_D4_TRANSACTION_IDLE = 0,
  GOODIX5135_D4_TRANSACTION_WAIT_OUT,
  GOODIX5135_D4_TRANSACTION_WAIT_ACK,
  GOODIX5135_D4_TRANSACTION_DONE,
  GOODIX5135_D4_TRANSACTION_FAILED,
} Goodix5135TlsEstablishedTransactionState;

typedef struct
{
  Goodix5135TlsEstablishedTransactionState state;
} Goodix5135TlsEstablishedTransaction;

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
 * Build the complete wrapped 0x90 MCU configuration frame.
 *
 * The caller supplies exactly GOODIX5135_CONFIG_LENGTH bytes.
 *
 * This function:
 *
 *   - does not know or persist the device's real configuration
 *   - performs no USB access
 *   - writes a padded 256-byte transport buffer
 *   - reports 232 logical Goodix bytes
 *
 * The resulting 256-byte transport buffer consists of four
 * consecutive 64-byte Bulk OUT packets.
 */
gboolean goodix5135_build_config_upload_transfer (
  const guint8 *config,
  gsize         config_length,
  guint8       *transfer,
  gsize         transfer_size,
  gsize        *logical_length,
  gsize        *transport_length);

/*
 * Borrow one of the four 64-byte USB packets from an already
 * constructed transport buffer.
 *
 * No copying is performed.
 */
gboolean goodix5135_config_upload_get_packet (
  const guint8  *transfer,
  gsize          transfer_length,
  guint          packet_index,
  const guint8 **packet,
  gsize         *packet_length);

/*
 * Parse ACK specifically for command 0x90.
 */
gboolean goodix5135_parse_config_upload_ack (
  const guint8 *data,
  gsize         data_length);

/*
 * Parse the 0x90 response.
 *
 * Reference semantics:
 *
 *   response payload[0] == 0x01 -> success
 */
gboolean goodix5135_parse_config_upload_response (
  const guint8 *data,
  gsize         data_length);

/*
 * Host-only multi-packet transaction controller:
 *
 * IDLE
 *  -> WAIT_OUT
 *       packet 1
 *       packet 2
 *       packet 3
 *       packet 4
 *  -> WAIT_ACK
 *  -> WAIT_RESPONSE
 *  -> DONE
 *
 * Any transport/protocol/order error -> FAILED.
 */
void goodix5135_config_upload_transaction_init (
  Goodix5135ConfigUploadTransaction *transaction);

gboolean goodix5135_config_upload_transaction_begin (
  Goodix5135ConfigUploadTransaction *transaction,
  const guint8                      *config,
  gsize                              config_length,
  guint8                            *transfer,
  gsize                              transfer_size,
  gsize                             *logical_length,
  gsize                             *transport_length);

gboolean goodix5135_config_upload_transaction_out_complete (
  Goodix5135ConfigUploadTransaction *transaction,
  gboolean                           transport_can_advance);

gboolean goodix5135_config_upload_transaction_ack_complete (
  Goodix5135ConfigUploadTransaction *transaction,
  gboolean                           transport_can_advance,
  const guint8                      *data,
  gsize                              data_length);

gboolean goodix5135_config_upload_transaction_response_complete (
  Goodix5135ConfigUploadTransaction *transaction,
  gboolean                           transport_can_advance,
  const guint8                      *data,
  gsize                              data_length);


/*
 * Host-only activation sequence controller.
 *
 * This does not build, send, receive, or submit USB data.
 * Individual command transactions remain responsible for
 * protocol and transport validation.
 *
 * Required ordering:
 *
 *   NOP #1
 *    -> 0xd4
 *    -> NOP #2
 *    -> ENABLE_CHIP(true)
 *    -> NOP #3
 *    -> firmware 0xa8
 *    -> sensor reset 0xa2
 *    -> register read 0x0000 / 4
 *    -> chip ID a2 04 25 00
 *    -> DONE
 *
 * Any failure or invalid ordering moves permanently to FAILED
 * until explicit reinitialization.
 */
void goodix5135_activation_sequence_init (
  Goodix5135ActivationSequence *sequence);

gboolean goodix5135_activation_sequence_begin (
  Goodix5135ActivationSequence *sequence);

gboolean goodix5135_activation_sequence_nop_complete (
  Goodix5135ActivationSequence *sequence,
  gboolean                      success);

gboolean goodix5135_activation_sequence_d4_complete (
  Goodix5135ActivationSequence *sequence,
  gboolean                      success);

gboolean goodix5135_activation_sequence_enable_chip_complete (
  Goodix5135ActivationSequence *sequence,
  gboolean                      success);

gboolean goodix5135_activation_sequence_firmware_complete (
  Goodix5135ActivationSequence *sequence,
  gboolean                      success);

gboolean goodix5135_activation_sequence_reset_complete (
  Goodix5135ActivationSequence *sequence,
  gboolean                      success);

gboolean goodix5135_activation_sequence_chip_id_complete (
  Goodix5135ActivationSequence *sequence,
  gboolean                      success,
  const guint8                 *chip_id,
  gsize                         chip_id_length);


/*
 * Build the exact historical sensor reset:
 *
 *   reset(True, False, 20)
 *
 * Payload:
 *   05 14
 *
 * Exact logical frame:
 *
 *   a0 06 00 a6
 *   a2 03 00
 *   05 14
 *   ec
 *
 * This helper performs no USB operation.
 */
gboolean goodix5135_build_sensor_reset_request (
  guint8 *packet,
  gsize   packet_size,
  gsize  *logical_length);

/*
 * Parse the ACK specifically belonging to RESET 0xa2.
 */
gboolean goodix5135_parse_sensor_reset_ack (
  const guint8 *data,
  gsize         data_length);

/*
 * Parse the normal reset response.
 *
 * Expected payload:
 *
 *   byte 0: 0x01 on success
 *   byte 1..2: little-endian result number
 *
 * No response bytes are persisted or logged.
 */
gboolean goodix5135_parse_sensor_reset_response (
  const guint8 *data,
  gsize         data_length,
  guint16      *result_number);

/*
 * Host-only reset transaction:
 *
 * IDLE
 *  -> WAIT_OUT
 *  -> WAIT_ACK
 *  -> WAIT_RESPONSE
 *  -> DONE
 */
void goodix5135_sensor_reset_transaction_init (
  Goodix5135SensorResetTransaction *transaction);

gboolean goodix5135_sensor_reset_transaction_begin (
  Goodix5135SensorResetTransaction *transaction,
  guint8                           *packet,
  gsize                             packet_size,
  gsize                            *logical_length);

gboolean goodix5135_sensor_reset_transaction_out_complete (
  Goodix5135SensorResetTransaction *transaction,
  gboolean                          transport_can_advance);

gboolean goodix5135_sensor_reset_transaction_ack_complete (
  Goodix5135SensorResetTransaction *transaction,
  gboolean                          transport_can_advance,
  const guint8                     *data,
  gsize                             data_length);

gboolean goodix5135_sensor_reset_transaction_response_complete (
  Goodix5135SensorResetTransaction *transaction,
  gboolean                          transport_can_advance,
  const guint8                     *data,
  gsize                             data_length,
  guint16                          *result_number);


/*
 * Build ENABLE_CHIP command 0x96.
 *
 * Public reference payload:
 *
 *   enable=true  -> 01 00
 *   enable=false -> 00 00
 *
 * Normal Goodix protocol checksum is used.
 *
 * enable=true exact logical frame:
 *
 *   a0 06 00 a6
 *   96 03 00
 *   01 00
 *   10
 *
 * This helper performs no USB operation.
 */
gboolean goodix5135_build_enable_chip_request (
  gboolean enable,
  guint8  *packet,
  gsize    packet_size,
  gsize   *logical_length);

/*
 * Parse the ACK belonging specifically to ENABLE_CHIP 0x96.
 */
gboolean goodix5135_parse_enable_chip_ack (
  const guint8 *data,
  gsize         data_length);

/*
 * Host-only ENABLE_CHIP transaction:
 *
 * IDLE
 *   -> WAIT_OUT
 *   -> WAIT_ACK
 *   -> DONE
 *
 * Any transport/protocol/order failure -> FAILED.
 */
void goodix5135_enable_chip_transaction_init (
  Goodix5135EnableChipTransaction *transaction);

gboolean goodix5135_enable_chip_transaction_begin (
  Goodix5135EnableChipTransaction *transaction,
  gboolean                         enable,
  guint8                          *packet,
  gsize                            packet_size,
  gsize                           *logical_length);

gboolean goodix5135_enable_chip_transaction_out_complete (
  Goodix5135EnableChipTransaction *transaction,
  gboolean                         transport_can_advance);

gboolean goodix5135_enable_chip_transaction_ack_complete (
  Goodix5135EnableChipTransaction *transaction,
  gboolean                         transport_can_advance,
  const guint8                    *data,
  gsize                            data_length);


/*
 * Build command 0xd4:
 * TLS_SUCCESSFULLY_ESTABLISHED.
 *
 * Public Goodix reference payload:
 *   00 00
 *
 * This uses the normal Goodix protocol checksum.
 *
 * Logical frame:
 *   a0 06 00 a6
 *   d4 03 00
 *   00 00
 *   d3
 *
 * This helper performs no USB operation.
 */
gboolean goodix5135_build_d4_request (
  guint8 *packet,
  gsize   packet_size,
  gsize  *logical_length);

/*
 * Parse the USB ACK belonging specifically to command 0xd4.
 */
gboolean goodix5135_parse_d4_ack (
  const guint8 *data,
  gsize         data_length);

/*
 * Host-only transaction controller:
 *
 * IDLE
 *   -> WAIT_OUT
 *   -> WAIT_ACK
 *   -> DONE
 *
 * No timeout-as-success rule exists here.
 * Any transport/protocol/order failure -> FAILED.
 */
void goodix5135_d4_transaction_init (
  Goodix5135TlsEstablishedTransaction *transaction);

gboolean goodix5135_d4_transaction_begin (
  Goodix5135TlsEstablishedTransaction *transaction,
  guint8                              *packet,
  gsize                                packet_size,
  gsize                               *logical_length);

gboolean goodix5135_d4_transaction_out_complete (
  Goodix5135TlsEstablishedTransaction *transaction,
  gboolean                             transport_can_advance);

gboolean goodix5135_d4_transaction_ack_complete (
  Goodix5135TlsEstablishedTransaction *transaction,
  gboolean                             transport_can_advance,
  const guint8                        *data,
  gsize                                data_length);


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
