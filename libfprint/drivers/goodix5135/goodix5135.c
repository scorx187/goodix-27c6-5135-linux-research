/*
 * Goodix 27c6:5135 ChicagoHU fingerprint reader
 *
 * Host-side libfprint lifecycle scaffold.
 *
 * Runtime bring-up remains deliberately bounded:
 * OPEN performs only read-only Goodix transactions.
 *
 * No reset, activation/configuration command, TLS session,
 * register write, or biometric capture is performed here.
 */

#define FP_COMPONENT "goodix5135"

#include <gusb.h>
#include "drivers_api.h"

#include "goodix5135.h"
#include "goodix5135-async.h"
#include "goodix5135-async-dispatch.h"
#include "goodix5135-queue-cleanup.h"
#include "goodix5135-io.h"
#include "goodix5135-driver-lifecycle.h"
#include "goodix5135-proto.h"
#include "goodix5135-capture-proto.h"
#include "goodix5135-image.h"
#include "goodix5135-image-response.h"
#include "fpi-image.h"
#include "goodix5135-tls-request.h"
#include "goodix5135-tls-session.h"

typedef enum
{
  GOODIX5135_CAPTURE_RUNTIME_IDLE = 0,

  GOODIX5135_CAPTURE_RUNTIME_WAIT_MANUAL_OUT,
  GOODIX5135_CAPTURE_RUNTIME_WAIT_MANUAL_ACK,
  GOODIX5135_CAPTURE_RUNTIME_WAIT_MANUAL_RESPONSE,

  GOODIX5135_CAPTURE_RUNTIME_WAIT_DOWN_OUT,
  GOODIX5135_CAPTURE_RUNTIME_WAIT_DOWN_ACK,
  GOODIX5135_CAPTURE_RUNTIME_WAIT_DOWN_RESPONSE,

  GOODIX5135_CAPTURE_RUNTIME_READY_IMAGE,
  GOODIX5135_CAPTURE_RUNTIME_WAIT_IMAGE_OUT,
  GOODIX5135_CAPTURE_RUNTIME_WAIT_IMAGE_ACK,
  GOODIX5135_CAPTURE_RUNTIME_WAIT_IMAGE_TLS,

  GOODIX5135_CAPTURE_RUNTIME_READY_FINGER_OFF,
  GOODIX5135_CAPTURE_RUNTIME_WAIT_UP_OUT,
  GOODIX5135_CAPTURE_RUNTIME_WAIT_UP_ACK,
  GOODIX5135_CAPTURE_RUNTIME_WAIT_UP_RESPONSE,
} Goodix5135CaptureRuntimeState;


struct _FpiDeviceGoodix5135
{
  FpImageDevice          parent;

  gboolean               active;
  gboolean               deactivating;
  FpiImageDeviceState           state;
  Goodix5135IoLifecycle         io;
  Goodix5135QueueCleanup        queue_cleanup;
  Goodix5135FirmwareTransaction firmware_transaction;
  Goodix5135RegisterReadTransaction register_read_transaction;
  Goodix5135McuStateTransaction     mcu_state_transaction;
  Goodix5135NopTransaction          nop_transaction;

  /*
   * Guarded volatile activation state.
   *
   * No PSK/config/template/biometric material is stored here.
   */
  Goodix5135TlsEstablishedTransaction d4_transaction;
  Goodix5135EnableChipTransaction      enable_chip_transaction;
  Goodix5135SensorResetTransaction     sensor_reset_transaction;
  Goodix5135ActivationSequence         activation_sequence;

  /*
   * READ_OTP state plus derived calibration only.
   * Raw OTP is deliberately never stored here.
   */
  Goodix5135OtpReadTransaction         otp_read_transaction;
  Goodix5135OtpCalibration             otp_calibration;
  gboolean                             otp_calibration_valid;

  /*
   * Volatile preparation state for the future CFG70 upload path.
   *
   * No template, config payload, Windows reference, or biometric
   * material is retained here.
   *
   * The transaction remains IDLE until a later explicitly-gated
   * runtime step supplies an approved CFG70 template.
   */
  Goodix5135ConfigUploadTransaction    config_upload_transaction;
  gboolean                             config_calibration_ready;

  /*
   * Volatile CFG70 preparation state.
   *
   * No template source is owned here. These buffers may only contain
   * host-prepared configuration during a later explicitly gated step,
   * and are wiped at every relevant lifecycle boundary.
   */
  guint8                               config_runtime[GOODIX5135_CONFIG_LENGTH];
  guint8                               config_transfer[GOODIX5135_CONFIG_TRANSFER_LENGTH];
  gsize                                config_logical_length;
  gsize                                config_transport_length;
  gboolean                             config_prepared;

  /*
   * Explicit host-only safety interlock for the future live CFG70
   * transport path.
   *
   * Preparation alone must never authorize USB transmission.
   */
  gboolean                             config_transport_armed;

  /*
   * Native TLS runtime bridge state.
   *
   * No PSK bytes are retained directly in the device object.
   * The session object owns volatile TLS state only while active.
   */
  Goodix5135TlsSession                *tls_session;
  Goodix5135TlsRequestTransaction      tls_request_transaction;
  Goodix5135TlsEstablishedTransaction  tls_runtime_d4_transaction;
  gboolean                             tls_runtime_open_gate;
  guint                                tls_runtime_d4_delay_source;
  guint                                capture_baseline_settle_source;
  guint                                capture_baseline_recheck_count;

  Goodix5135CaptureRuntimeState         capture_runtime_state;

  guint8 capture_fdt_seed[
    GOODIX5135_FDT_MANUAL_SEED_BYTES
  ];

  guint8 capture_fdt_down[
    GOODIX5135_FDT_REGISTER_BYTES
  ];

  guint8 capture_fdt_up[
    GOODIX5135_FDT_REGISTER_BYTES
  ];

  guint8 capture_plaintext[
    GOODIX5135_IMAGE_TOTAL_LENGTH
  ];

  gsize capture_plaintext_length;
  guint capture_tls_reads;
};

G_DECLARE_FINAL_TYPE (FpiDeviceGoodix5135,
                      fpi_device_goodix5135,
                      FPI,
                      DEVICE_GOODIX5135,
                      FpImageDevice);

G_DEFINE_TYPE (FpiDeviceGoodix5135,
               fpi_device_goodix5135,
               FP_TYPE_IMAGE_DEVICE);

static const FpIdEntry goodix5135_id_table[] = {
  {
    .vid = GOODIX5135_USB_VID,
    .pid = GOODIX5135_USB_PID,
  },
  {
    .vid = 0,
    .pid = 0,
    .driver_data = 0,
  },
};

/*
 * Firmware-version OPEN transaction timeout.
 *
 * OPEN performs one read-only command-0xa8 transaction:
 * one Bulk OUT request, followed by ACK and firmware-response Bulk IN.
 */
#define GOODIX5135_FIRMWARE_IO_TIMEOUT_MS 1000U

/*
 * Match the reference driver's 100 ms queue-empty observation window.
 *
 * GOODIX5135_QUEUE_CLEANUP_MAX_PACKETS is a hard consumption budget:
 * once that many non-empty stale packets have been consumed without seeing
 * an empty-queue timeout, cleanup fails closed.
 *
 * We intentionally do not perform one extra read after the budget is
 * exhausted because that read could consume a packet beyond the configured
 * stale-data budget.
 */
#define GOODIX5135_QUEUE_CLEANUP_TIMEOUT_MS 100U
#define GOODIX5135_QUEUE_CLEANUP_MAX_PACKETS 8U

/*
 * First runtime register proof.
 *
 * 0x0220 is read-only here. Its returned bytes are validated for framing
 * and minimum length only; they are never logged, copied, or persisted.
 */
#define GOODIX5135_REGISTER_PROBE_ADDRESS 0x0220U
#define GOODIX5135_REGISTER_PROBE_LENGTH  2U

/*
 * Read-only MCU state query proven by the earlier Linux state probe.
 * Returned bytes are validated structurally only and are not logged.
 */
#define GOODIX5135_MCU_STATE_QUERY 0x55U

/*
 * Public Goodix reference waits exactly 0.1 seconds for the optional
 * NOP ACK. Only an actual USB timed-out error is treated as success.
 */
#define GOODIX5135_NOP_ACK_TIMEOUT_MS 100U

/*
 * Final activation identity gate.
 *
 * READ_SENSOR_REGISTER only:
 *   address 0x0000
 *   length  4
 *
 * Expected logical chip bytes are checked by the host-only
 * activation controller and are never persisted.
 */
#define GOODIX5135_ACTIVATION_CHIP_ID_ADDRESS 0x0000U
#define GOODIX5135_ACTIVATION_CHIP_ID_LENGTH  GOODIX5135_CHIP_ID_LENGTH

static void
goodix5135_capture_runtime_reset (
  FpiDeviceGoodix5135 *self);

static gboolean
goodix5135_live_capture_test_requested (void);

static gboolean
goodix5135_fpimage_test_requested (void);

static FpImage *
goodix5135_capture_make_fpimage (
  const guint16 *pixels,
  gsize          pixel_count);

static gboolean
goodix5135_capture_start_image (
  FpDevice *device);

static gboolean
goodix5135_capture_start_fdt_up (
  FpDevice *device);

static gboolean
goodix5135_capture_runtime_start (
  FpDevice *device);

static gboolean
goodix5135_capture_baseline_settle_cb (
  gpointer user_data);

static void
goodix5135_capture_runtime_fail (
  FpDevice    *device,
  const gchar *reason);

static void
goodix5135_capture_manual_out_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void
goodix5135_capture_manual_ack_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void
goodix5135_capture_manual_response_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void
goodix5135_capture_down_out_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void
goodix5135_capture_down_ack_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void
goodix5135_capture_down_response_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void
goodix5135_capture_image_out_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void
goodix5135_capture_image_ack_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void
goodix5135_capture_accept_tls_plaintext (
  FpDevice      *device,
  const guint8  *data,
  gsize          data_length);

static void
goodix5135_capture_up_out_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void
goodix5135_capture_up_ack_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void
goodix5135_capture_up_response_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void
goodix5135_tls_runtime_reset (
  FpiDeviceGoodix5135 *self);

static gboolean
goodix5135_tls_runtime_submit_sensor_read (
  FpDevice *device);

static gboolean
goodix5135_tls_runtime_submit_host_frame (
  FpDevice   *device,
  GByteArray *host_frame);

static void
goodix5135_tls_runtime_start_d4 (
  FpDevice *device);

static gboolean
goodix5135_tls_runtime_schedule_d4 (
  FpDevice *device);

static gboolean
goodix5135_tls_runtime_d4_delay_cb (
  gpointer user_data);

static void
goodix5135_tls_runtime_d0_out_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void
goodix5135_tls_runtime_d0_ack_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void
goodix5135_tls_runtime_sensor_frame_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void
goodix5135_tls_runtime_host_frame_out_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void
goodix5135_tls_runtime_d4_out_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void
goodix5135_tls_runtime_d4_ack_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

/*
 * This is deliberately dormant.
 *
 * A later explicitly gated private-PSK input seam will call it.
 * ACTIVATE must not call it in this commit.
 */
static gboolean
goodix5135_tls_runtime_start (
  FpDevice      *device,
  const guint8  *psk,
  gsize          psk_len,
  GError       **error) G_GNUC_UNUSED;

static void goodix5135_queue_cleanup_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void goodix5135_firmware_ack_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void goodix5135_firmware_response_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void goodix5135_register_out_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void goodix5135_register_ack_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void goodix5135_register_response_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void goodix5135_start_register_read_transaction (
  FpDevice *device);

static void goodix5135_mcu_state_out_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void goodix5135_mcu_state_ack_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void goodix5135_mcu_state_response_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void goodix5135_start_mcu_state_transaction (
  FpDevice *device);

static void goodix5135_nop_out_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void goodix5135_nop_reply_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void goodix5135_start_nop_transaction (
  FpDevice *device);

static void goodix5135_activation_continue_after_nop (
  FpDevice *device);

static void goodix5135_activation_d4_out_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void goodix5135_activation_d4_ack_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void goodix5135_start_activation_d4 (
  FpDevice *device);

static void goodix5135_activation_enable_out_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void goodix5135_activation_enable_ack_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void goodix5135_start_activation_enable (
  FpDevice *device);

static void goodix5135_activation_firmware_out_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void goodix5135_activation_firmware_ack_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void goodix5135_activation_firmware_response_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void goodix5135_start_activation_firmware (
  FpDevice *device);

static void goodix5135_activation_reset_out_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void goodix5135_activation_reset_ack_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void goodix5135_activation_reset_response_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void goodix5135_start_activation_reset (
  FpDevice *device);

static void goodix5135_activation_chip_id_out_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void goodix5135_activation_chip_id_ack_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void goodix5135_activation_chip_id_response_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void goodix5135_start_activation_chip_id_read (
  FpDevice *device);

static void goodix5135_otp_out_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void goodix5135_otp_ack_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void goodix5135_otp_response_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void goodix5135_start_otp_read (
  FpDevice *device);

static void
goodix5135_config_runtime_state_reset (
  FpiDeviceGoodix5135 *self);


static gboolean
goodix5135_live_config_test_requested (void);

static gboolean
goodix5135_start_live_config_upload_test (
  FpDevice *device);

static void
goodix5135_open_transaction_fail (FpDevice    *device,
                                  const gchar *message)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);
  FpImageDevice *dev = FP_IMAGE_DEVICE (device);
  GUsbDevice *usb_dev;
  GError *release_error = NULL;
  GError *open_error;

  /*
   * Every caller reaches this helper only when the just-completed request
   * has already drained, or when submission failed before a callback could
   * exist.
   */
  if (self->io.running)
    goodix5135_io_stop (&self->io);

  g_assert (goodix5135_io_can_finish_stop (&self->io));

  /*
   * Failed open cannot retain volatile config preparation state.
   */
  goodix5135_config_runtime_state_reset (
    self);

  usb_dev = fpi_device_get_usb_device (device);

  g_usb_device_release_interface (
    usb_dev,
    GOODIX5135_USB_INTERFACE,
    0,
    &release_error);

  g_clear_error (&release_error);

  open_error =
    fpi_device_error_new_msg (
      FP_DEVICE_ERROR_GENERAL,
      "%s",
      message);

  fpi_image_device_open_complete (
    dev,
    open_error);
}

static gboolean
goodix5135_in_transfer_can_parse (
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  const GError                   *error)
{
  if (!goodix5135_async_result_can_advance (
        completion,
        error))
    return FALSE;

  if (transfer == NULL)
    return FALSE;

  /*
   * On the asynchronous GUsb path, a transport error may expose
   * actual_length == -1. Never inspect the receive buffer unless:
   *
   *   CURRENT
   *   error == NULL
   *   actual_length > 0
   *   actual_length <= requested length
   */
  if (transfer->actual_length <= 0)
    return FALSE;

  if (transfer->actual_length > transfer->length)
    return FALSE;

  return TRUE;
}

static void
goodix5135_firmware_out_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);
  gboolean transport_can_advance;

  (void) user_data;

  transport_can_advance =
    goodix5135_async_result_can_advance (
      completion,
      error);

  /*
   * A short Bulk OUT must not advance the protocol even if GUsb did not
   * attach an error to the completion.
   */
  if (transport_can_advance &&
      (transfer == NULL ||
       transfer->actual_length !=
         GOODIX5135_USB_PACKET_LENGTH))
    transport_can_advance = FALSE;

  g_clear_error (&error);

  if (!goodix5135_firmware_transaction_out_complete (
        &self->firmware_transaction,
        transport_can_advance))
    {
      goodix5135_open_transaction_fail (
        device,
        "Goodix firmware-version request transport failed");
      return;
    }

  /*
   * First receive:
   *   successful ACK for command 0xa8.
   *
   * Request one USB packet only. No received byte is logged.
   */
  if (!goodix5135_async_submit (
        device,
        &self->io,
        GOODIX5135_REQUEST_BULK_IN,
        GOODIX5135_USB_PACKET_LENGTH,
        GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
        NULL,
        0,
        goodix5135_firmware_ack_cb,
        NULL))
    {
      goodix5135_firmware_transaction_ack_complete (
        &self->firmware_transaction,
        FALSE,
        NULL,
        0);

      goodix5135_open_transaction_fail (
        device,
        "Could not submit Goodix firmware-version ACK receive");
    }
}

static void
goodix5135_firmware_ack_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);
  gboolean transport_can_advance;
  gboolean protocol_ok;

  (void) user_data;

  transport_can_advance =
    goodix5135_in_transfer_can_parse (
      transfer,
      completion,
      error);

  if (transport_can_advance)
    {
      /*
       * transfer->buffer is borrowed and is consumed synchronously by the
       * parser while the FpiUsbTransfer callback is still active.
       *
       * Nothing is copied, printed, hashed, or retained.
       */
      protocol_ok =
        goodix5135_firmware_transaction_ack_complete (
          &self->firmware_transaction,
          TRUE,
          transfer->buffer,
          (gsize) transfer->actual_length);
    }
  else
    {
      protocol_ok =
        goodix5135_firmware_transaction_ack_complete (
          &self->firmware_transaction,
          FALSE,
          NULL,
          0);
    }

  g_clear_error (&error);

  if (!protocol_ok)
    {
      goodix5135_open_transaction_fail (
        device,
        "Goodix firmware-version ACK was not valid and successful");
      return;
    }

  /*
   * Second receive:
   *   command-0xa8 firmware-version response.
   */
  if (!goodix5135_async_submit (
        device,
        &self->io,
        GOODIX5135_REQUEST_BULK_IN,
        GOODIX5135_USB_PACKET_LENGTH,
        GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
        NULL,
        0,
        goodix5135_firmware_response_cb,
        NULL))
    {
      const guint8 *unused_firmware = NULL;
      gsize unused_firmware_length = 0;

      goodix5135_firmware_transaction_response_complete (
        &self->firmware_transaction,
        FALSE,
        NULL,
        0,
        &unused_firmware,
        &unused_firmware_length);

      goodix5135_open_transaction_fail (
        device,
        "Could not submit Goodix firmware-version response receive");
    }
}

static void
goodix5135_firmware_response_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);
  const guint8 *firmware = NULL;
  gsize firmware_length = 0;
  gboolean transport_can_advance;
  gboolean protocol_ok;

  (void) user_data;

  transport_can_advance =
    goodix5135_in_transfer_can_parse (
      transfer,
      completion,
      error);

  if (transport_can_advance)
    {
      /*
       * firmware is a borrowed view into transfer->buffer.
       *
       * It is validated only while this callback owns the borrowed
       * FpiUsbTransfer lifetime. The bytes are not printed, copied,
       * persisted, or hashed.
       */
      protocol_ok =
        goodix5135_firmware_transaction_response_complete (
          &self->firmware_transaction,
          TRUE,
          transfer->buffer,
          (gsize) transfer->actual_length,
          &firmware,
          &firmware_length);
    }
  else
    {
      protocol_ok =
        goodix5135_firmware_transaction_response_complete (
          &self->firmware_transaction,
          FALSE,
          NULL,
          0,
          &firmware,
          &firmware_length);
    }

  g_clear_error (&error);

  if (!protocol_ok ||
      self->firmware_transaction.state !=
        GOODIX5135_FIRMWARE_TRANSACTION_DONE ||
      firmware == NULL ||
      firmware_length == 0)
    {
      goodix5135_open_transaction_fail (
        device,
        "Goodix firmware-version response validation failed");
      return;
    }

  /*
   * Do not retain the borrowed firmware pointer beyond this callback.
   * Successful validation is sufficient for this first runtime transaction.
   */
  firmware = NULL;
  firmware_length = 0;

  fp_dbg ("Firmware response validated; starting read-only register probe");

  goodix5135_start_register_read_transaction (
    device);
}

static void
goodix5135_register_out_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);
  gboolean transport_can_advance;

  (void) user_data;

  transport_can_advance =
    goodix5135_async_result_can_advance (
      completion,
      error);

  if (transport_can_advance &&
      (transfer == NULL ||
       transfer->actual_length !=
         GOODIX5135_USB_PACKET_LENGTH))
    transport_can_advance = FALSE;

  g_clear_error (&error);

  if (!goodix5135_register_read_transaction_out_complete (
        &self->register_read_transaction,
        transport_can_advance))
    {
      goodix5135_open_transaction_fail (
        device,
        "Goodix read-only register request transport failed");
      return;
    }

  if (!goodix5135_async_submit (
        device,
        &self->io,
        GOODIX5135_REQUEST_BULK_IN,
        GOODIX5135_USB_PACKET_LENGTH,
        GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
        NULL,
        0,
        goodix5135_register_ack_cb,
        NULL))
    {
      goodix5135_register_read_transaction_ack_complete (
        &self->register_read_transaction,
        FALSE,
        NULL,
        0);

      goodix5135_open_transaction_fail (
        device,
        "Could not submit Goodix register-read ACK receive");
    }
}


static void
goodix5135_register_ack_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);
  gboolean transport_can_advance;
  gboolean protocol_ok;

  (void) user_data;

  transport_can_advance =
    goodix5135_in_transfer_can_parse (
      transfer,
      completion,
      error);

  if (transport_can_advance)
    {
      protocol_ok =
        goodix5135_register_read_transaction_ack_complete (
          &self->register_read_transaction,
          TRUE,
          transfer->buffer,
          (gsize) transfer->actual_length);
    }
  else
    {
      protocol_ok =
        goodix5135_register_read_transaction_ack_complete (
          &self->register_read_transaction,
          FALSE,
          NULL,
          0);
    }

  g_clear_error (&error);

  if (!protocol_ok)
    {
      goodix5135_open_transaction_fail (
        device,
        "Goodix register-read ACK was not valid");
      return;
    }

  if (!goodix5135_async_submit (
        device,
        &self->io,
        GOODIX5135_REQUEST_BULK_IN,
        GOODIX5135_USB_PACKET_LENGTH,
        GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
        NULL,
        0,
        goodix5135_register_response_cb,
        NULL))
    {
      const guint8 *unused_value = NULL;
      gsize unused_length = 0;

      goodix5135_register_read_transaction_response_complete (
        &self->register_read_transaction,
        FALSE,
        NULL,
        0,
        &unused_value,
        &unused_length);

      goodix5135_open_transaction_fail (
        device,
        "Could not submit Goodix register-read response receive");
    }
}


static void
goodix5135_register_response_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);
  gboolean transport_can_advance;
  gboolean protocol_ok;
  const guint8 *value = NULL;
  gsize value_length = 0;

  (void) user_data;

  transport_can_advance =
    goodix5135_in_transfer_can_parse (
      transfer,
      completion,
      error);

  if (transport_can_advance)
    {
      protocol_ok =
        goodix5135_register_read_transaction_response_complete (
          &self->register_read_transaction,
          TRUE,
          transfer->buffer,
          (gsize) transfer->actual_length,
          &value,
          &value_length);
    }
  else
    {
      protocol_ok =
        goodix5135_register_read_transaction_response_complete (
          &self->register_read_transaction,
          FALSE,
          NULL,
          0,
          &value,
          &value_length);
    }

  g_clear_error (&error);

  if (!protocol_ok ||
      self->register_read_transaction.state !=
        GOODIX5135_REGISTER_READ_TRANSACTION_DONE ||
      value == NULL ||
      value_length < GOODIX5135_REGISTER_PROBE_LENGTH)
    {
      goodix5135_open_transaction_fail (
        device,
        "Goodix read-only register response validation failed");
      return;
    }

  /*
   * Do not print or retain register bytes.
   * Successful framing/length validation is sufficient for this gate.
   */
  value = NULL;
  value_length = 0;

  fp_dbg ("Read-only register probe 0x0220 validated; starting MCU state query");

  goodix5135_start_mcu_state_transaction (
    device);
}


static void
goodix5135_mcu_state_out_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);
  gboolean transport_can_advance;

  (void) user_data;

  transport_can_advance =
    goodix5135_async_result_can_advance (
      completion,
      error);

  if (transport_can_advance &&
      (transfer == NULL ||
       transfer->actual_length !=
         GOODIX5135_USB_PACKET_LENGTH))
    transport_can_advance = FALSE;

  g_clear_error (&error);

  if (!goodix5135_mcu_state_transaction_out_complete (
        &self->mcu_state_transaction,
        transport_can_advance))
    {
      goodix5135_open_transaction_fail (
        device,
        "Goodix MCU-state request transport failed");
      return;
    }

  if (!goodix5135_async_submit (
        device,
        &self->io,
        GOODIX5135_REQUEST_BULK_IN,
        GOODIX5135_USB_PACKET_LENGTH,
        GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
        NULL,
        0,
        goodix5135_mcu_state_ack_cb,
        NULL))
    {
      goodix5135_mcu_state_transaction_ack_complete (
        &self->mcu_state_transaction,
        FALSE,
        NULL,
        0);

      goodix5135_open_transaction_fail (
        device,
        "Could not submit Goodix MCU-state ACK receive");
    }
}


static void
goodix5135_mcu_state_ack_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);
  gboolean transport_can_advance;
  gboolean protocol_ok;

  (void) user_data;

  transport_can_advance =
    goodix5135_in_transfer_can_parse (
      transfer,
      completion,
      error);

  if (transport_can_advance)
    {
      protocol_ok =
        goodix5135_mcu_state_transaction_ack_complete (
          &self->mcu_state_transaction,
          TRUE,
          transfer->buffer,
          (gsize) transfer->actual_length);
    }
  else
    {
      protocol_ok =
        goodix5135_mcu_state_transaction_ack_complete (
          &self->mcu_state_transaction,
          FALSE,
          NULL,
          0);
    }

  g_clear_error (&error);

  if (!protocol_ok)
    {
      goodix5135_open_transaction_fail (
        device,
        "Goodix MCU-state ACK was not valid");
      return;
    }

  if (!goodix5135_async_submit (
        device,
        &self->io,
        GOODIX5135_REQUEST_BULK_IN,
        GOODIX5135_USB_PACKET_LENGTH,
        GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
        NULL,
        0,
        goodix5135_mcu_state_response_cb,
        NULL))
    {
      const guint8 *unused_state = NULL;
      gsize unused_length = 0;

      goodix5135_mcu_state_transaction_response_complete (
        &self->mcu_state_transaction,
        FALSE,
        NULL,
        0,
        &unused_state,
        &unused_length);

      goodix5135_open_transaction_fail (
        device,
        "Could not submit Goodix MCU-state response receive");
    }
}


static void
goodix5135_mcu_state_response_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);
  gboolean transport_can_advance;
  gboolean protocol_ok;
  const guint8 *state_data = NULL;
  gsize state_length = 0;

  (void) user_data;

  transport_can_advance =
    goodix5135_in_transfer_can_parse (
      transfer,
      completion,
      error);

  if (transport_can_advance)
    {
      protocol_ok =
        goodix5135_mcu_state_transaction_response_complete (
          &self->mcu_state_transaction,
          TRUE,
          transfer->buffer,
          (gsize) transfer->actual_length,
          &state_data,
          &state_length);
    }
  else
    {
      protocol_ok =
        goodix5135_mcu_state_transaction_response_complete (
          &self->mcu_state_transaction,
          FALSE,
          NULL,
          0,
          &state_data,
          &state_length);
    }

  g_clear_error (&error);

  if (!protocol_ok ||
      self->mcu_state_transaction.state !=
        GOODIX5135_MCU_STATE_TRANSACTION_DONE ||
      state_data == NULL ||
      state_length == 0)
    {
      goodix5135_open_transaction_fail (
        device,
        "Goodix MCU-state response validation failed");
      return;
    }

  /*
   * State bytes are deliberately not printed, copied, persisted,
   * or interpreted at this bring-up gate.
   */
  state_data = NULL;
  state_length = 0;

  fp_dbg ("Read-only MCU state query 0x55 validated; starting bounded NOP");

  goodix5135_start_nop_transaction (
    device);
}


static void
goodix5135_nop_out_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);
  gboolean transport_can_advance;

  (void) user_data;

  transport_can_advance =
    goodix5135_async_result_can_advance (
      completion,
      error);

  if (transport_can_advance &&
      (transfer == NULL ||
       transfer->actual_length !=
         GOODIX5135_USB_PACKET_LENGTH))
    transport_can_advance = FALSE;

  g_clear_error (&error);

  if (!goodix5135_nop_transaction_out_complete (
        &self->nop_transaction,
        transport_can_advance))
    {
      goodix5135_open_transaction_fail (
        device,
        "Goodix NOP request transport failed");
      return;
    }

  /*
   * The ACK is optional.
   *
   * Public reference:
   *   protocol.read(timeout=0.1)
   *
   * Only G_USB_DEVICE_ERROR_TIMED_OUT is accepted as a
   * successful no-reply completion.
   */
  if (!goodix5135_async_submit (
        device,
        &self->io,
        GOODIX5135_REQUEST_BULK_IN,
        GOODIX5135_USB_PACKET_LENGTH,
        GOODIX5135_NOP_ACK_TIMEOUT_MS,
        NULL,
        0,
        goodix5135_nop_reply_cb,
        NULL))
    {
      goodix5135_nop_transaction_reply_complete (
        &self->nop_transaction,
        GOODIX5135_NOP_REPLY_TRANSPORT_FAILURE,
        NULL,
        0);

      goodix5135_open_transaction_fail (
        device,
        "Could not submit Goodix NOP optional-ACK receive");
    }
}


static void
goodix5135_nop_reply_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);
  Goodix5135NopReplyResult result;
  gboolean protocol_ok;
  gboolean received_ok;

  (void) user_data;

  /*
   * A timeout is special ONLY for NOP.
   *
   * Do not generalize this behavior to any other command.
   */
  if (error != NULL &&
      g_error_matches (
        error,
        G_USB_DEVICE_ERROR,
        G_USB_DEVICE_ERROR_TIMED_OUT))
    {
      result =
        GOODIX5135_NOP_REPLY_TIMEOUT;

      protocol_ok =
        goodix5135_nop_transaction_reply_complete (
          &self->nop_transaction,
          result,
          NULL,
          0);

      g_clear_error (&error);

      if (!protocol_ok)
        {
          goodix5135_open_transaction_fail (
            device,
            "Goodix NOP timeout policy rejected");
          return;
        }

      fp_dbg ("Bounded NOP completed by expected 100 ms USB timeout");
    }
  else
    {
      received_ok =
        goodix5135_in_transfer_can_parse (
          transfer,
          completion,
          error);

      if (received_ok)
        {
          result =
            GOODIX5135_NOP_REPLY_RECEIVED;

          protocol_ok =
            goodix5135_nop_transaction_reply_complete (
              &self->nop_transaction,
              result,
              transfer->buffer,
              (gsize) transfer->actual_length);
        }
      else
        {
          result =
            GOODIX5135_NOP_REPLY_TRANSPORT_FAILURE;

          protocol_ok =
            goodix5135_nop_transaction_reply_complete (
              &self->nop_transaction,
              result,
              NULL,
              0);
        }

      g_clear_error (&error);

      if (!protocol_ok)
        {
          goodix5135_open_transaction_fail (
            device,
            received_ok
              ? "Goodix NOP returned an invalid ACK"
              : "Goodix NOP optional-ACK transport failed");
          return;
        }

      fp_dbg ("Bounded NOP completed with valid ACK");
    }

  if (self->nop_transaction.state !=
      GOODIX5135_NOP_TRANSACTION_DONE)
    {
      goodix5135_open_transaction_fail (
        device,
        "Goodix NOP transaction did not reach DONE");
      return;
    }

  /*
   * The first completed NOP is the final read-only OPEN gate.
   * Later completed NOPs are the three ordered activation gates.
   */
  goodix5135_activation_continue_after_nop (
    device);
}


static void
goodix5135_activation_continue_after_nop (
  FpDevice *device)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  /*
   * activation_sequence remains IDLE throughout all previous
   * read-only OPEN gates.
   *
   * Therefore the first successful bounded NOP starts the
   * guarded state-changing activation sequence but does not
   * itself count as NOP #1.
   */
  if (self->activation_sequence.state ==
      GOODIX5135_ACTIVATION_IDLE)
    {
      if (!goodix5135_activation_sequence_begin (
            &self->activation_sequence))
        {
          goodix5135_open_transaction_fail (
            device,
            "Could not begin Goodix activation sequence");
          return;
        }

      if (self->activation_sequence.state !=
          GOODIX5135_ACTIVATION_WAIT_NOP1)
        {
          goodix5135_open_transaction_fail (
            device,
            "Goodix activation sequence did not enter NOP1");
          return;
        }

      fp_dbg (
        "Read-only OPEN gates validated; starting activation NOP #1");

      goodix5135_start_nop_transaction (
        device);

      return;
    }

  /*
   * Every subsequent successful bounded NOP must correspond
   * exactly to NOP1, NOP2, or NOP3 in the host controller.
   */
  if (!goodix5135_activation_sequence_nop_complete (
        &self->activation_sequence,
        TRUE))
    {
      goodix5135_open_transaction_fail (
        device,
        "Goodix activation NOP occurred out of order");
      return;
    }

  switch (self->activation_sequence.state)
    {
    case GOODIX5135_ACTIVATION_WAIT_D4:
      fp_dbg (
        "Activation NOP #1 validated; starting 0xD4 signal");

      goodix5135_start_activation_d4 (
        device);
      return;

    case GOODIX5135_ACTIVATION_WAIT_ENABLE_CHIP:
      fp_dbg (
        "Activation NOP #2 validated; starting ENABLE_CHIP true");

      goodix5135_start_activation_enable (
        device);
      return;

    case GOODIX5135_ACTIVATION_WAIT_FIRMWARE:
      fp_dbg (
        "Activation NOP #3 validated; starting firmware gate");

      goodix5135_start_activation_firmware (
        device);
      return;

    case GOODIX5135_ACTIVATION_IDLE:
    case GOODIX5135_ACTIVATION_WAIT_NOP1:
    case GOODIX5135_ACTIVATION_WAIT_NOP2:
    case GOODIX5135_ACTIVATION_WAIT_NOP3:
    case GOODIX5135_ACTIVATION_WAIT_RESET:
    case GOODIX5135_ACTIVATION_WAIT_CHIP_ID:
    case GOODIX5135_ACTIVATION_DONE:
    case GOODIX5135_ACTIVATION_FAILED:
      goodix5135_open_transaction_fail (
        device,
        "Goodix activation NOP advanced to an invalid state");
      return;
    }
}



#define GOODIX5135_TLS_RUNTIME_SENSOR_READ_SIZE 0x10000U
#define GOODIX5135_TLS_D4_SETTLE_DELAY_MS       10U

#define GOODIX5135_LIVE_TLS_TEST_ENV "GOODIX5135_LIVE_TLS_TEST"
#define GOODIX5135_LIVE_TLS_PSK_ENV  "GOODIX5135_LIVE_TLS_PSK_FILE"
#define GOODIX5135_LIVE_TLS_TEST_VALUE "ONE_SHOT_NATIVE_TLS"

#define GOODIX5135_LIVE_CAPTURE_TEST_ENV "GOODIX5135_LIVE_CAPTURE_TEST"
#define GOODIX5135_LIVE_CAPTURE_TEST_VALUE "ONE_SHOT_NATIVE_CAPTURE"

#define GOODIX5135_FPIMAGE_TEST_ENV   "GOODIX5135_FPIMAGE_TEST"
#define GOODIX5135_FPIMAGE_TEST_VALUE "TWO_CAPTURE_LIFECYCLE"

#define GOODIX5135_LIVE_FDT_SEED_ENV "GOODIX5135_LIVE_FDT_SEED_FILE"
#define GOODIX5135_LIVE_FDT_UP_ENV   "GOODIX5135_LIVE_FDT_UP_FILE"

#define GOODIX5135_CAPTURE_EVENT_TIMEOUT_MS 45000U

/*
 * Host-side release settling before a new FDT manual baseline.
 * No USB retry, sensor reset, or FDT threshold change is used.
 */
#define GOODIX5135_CAPTURE_BASELINE_SETTLE_MS 250U

/*
 * A protocol-valid manual FDT response may briefly report touch
 * again after an FDT-up event reported finger-off. Recheck the
 * actual sensor state instead of relying on a longer fixed delay.
 */
#define GOODIX5135_CAPTURE_BASELINE_RECHECK_MS 100U
#define GOODIX5135_CAPTURE_BASELINE_RECHECK_MAX 20U
#define GOODIX5135_CAPTURE_IMAGE_TIMEOUT_MS 10000U
#define GOODIX5135_CAPTURE_MAX_TLS_READS    4U

#define GOODIX5135_PRIVATE_PSK_HEX_LENGTH 64U
#define GOODIX5135_PRIVATE_PSK_LENGTH     32U


static void
goodix5135_live_tls_secure_clear (
  gpointer data,
  gsize    length)
{
  volatile guint8 *cursor =
    (volatile guint8 *) data;

  if (cursor == NULL)
    return;

  while (length > 0)
    {
      *cursor = 0;
      cursor++;
      length--;
    }
}


static gboolean
goodix5135_live_tls_test_requested (void)
{
  const gchar *value =
    g_getenv (GOODIX5135_LIVE_TLS_TEST_ENV);

  return g_strcmp0 (
           value,
           GOODIX5135_LIVE_TLS_TEST_VALUE) == 0;
}


static gboolean
goodix5135_live_tls_load_psk (
  guint8 *psk,
  gsize   psk_size)
{
  const gchar *path;
  gchar       *contents = NULL;

  gsize file_length = 0;
  gsize logical_length;
  gsize index;

  GError *error = NULL;

  g_return_val_if_fail (
    psk != NULL,
    FALSE);

  if (psk_size !=
      GOODIX5135_PRIVATE_PSK_LENGTH)
    return FALSE;

  memset (
    psk,
    0,
    psk_size);

  path =
    g_getenv (
      GOODIX5135_LIVE_TLS_PSK_ENV);

  if (path == NULL ||
      *path == '\0')
    return FALSE;

  if (!g_file_get_contents (
        path,
        &contents,
        &file_length,
        &error))
    {
      g_clear_error (&error);
      return FALSE;
    }

  logical_length =
    file_length;

  while (logical_length > 0 &&
         (
           contents[logical_length - 1] == '\n' ||
           contents[logical_length - 1] == '\r'
         ))
    {
      contents[logical_length - 1] = '\0';
      logical_length--;
    }

  if (logical_length !=
      GOODIX5135_PRIVATE_PSK_HEX_LENGTH)
    goto fail;

  for (index = 0;
       index < GOODIX5135_PRIVATE_PSK_LENGTH;
       index++)
    {
      gint high;
      gint low;

      high =
        g_ascii_xdigit_value (
          contents[index * 2]);

      low =
        g_ascii_xdigit_value (
          contents[index * 2 + 1]);

      if (high < 0 ||
          low < 0)
        goto fail;

      psk[index] =
        (guint8) (
          (high << 4) |
          low
        );
    }

  goodix5135_live_tls_secure_clear (
    contents,
    file_length + 1);

  g_free (contents);

  return TRUE;

fail:
  goodix5135_live_tls_secure_clear (
    psk,
    psk_size);

  goodix5135_live_tls_secure_clear (
    contents,
    file_length + 1);

  g_free (contents);

  return FALSE;
}


static gboolean
goodix5135_start_live_tls_open_gate (
  FpDevice *device)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  guint8 psk[GOODIX5135_PRIVATE_PSK_LENGTH] = { 0 };

  GError *error = NULL;

  gboolean started;

  if (!goodix5135_live_tls_test_requested ())
    return FALSE;

  if (!goodix5135_live_tls_load_psk (
        psk,
        sizeof (psk)))
    {
      goodix5135_live_tls_secure_clear (
        psk,
        sizeof (psk));

      goodix5135_open_transaction_fail (
        device,
        "One-shot native TLS private input gate failed");

      return TRUE;
    }

  fp_dbg (
    "One-shot native TLS PSK input gate validated");

  self->tls_runtime_open_gate =
    TRUE;

  started =
    goodix5135_tls_runtime_start (
      device,
      psk,
      sizeof (psk),
      &error);

  goodix5135_live_tls_secure_clear (
    psk,
    sizeof (psk));

  if (!started)
    {
      self->tls_runtime_open_gate =
        FALSE;

      g_clear_error (&error);

      goodix5135_open_transaction_fail (
        device,
        "Could not start one-shot native TLS runtime gate");

      return TRUE;
    }

  g_clear_error (&error);

  fp_dbg (
    "One-shot native TLS runtime gate started");

  return TRUE;
}



static gboolean
goodix5135_live_capture_test_requested (void)
{
  const gchar *value =
    g_getenv (
      GOODIX5135_LIVE_CAPTURE_TEST_ENV);

  return g_strcmp0 (
           value,
           GOODIX5135_LIVE_CAPTURE_TEST_VALUE) == 0;
}


static gboolean
goodix5135_fpimage_test_requested (void)
{
  const gchar *value =
    g_getenv (
      GOODIX5135_FPIMAGE_TEST_ENV);

  return g_strcmp0 (
           value,
           GOODIX5135_FPIMAGE_TEST_VALUE) == 0;
}


static gboolean
goodix5135_capture_load_private_exact (
  const gchar *environment_name,
  guint8      *output,
  gsize        expected_length)
{
  const gchar *path;

  gchar *contents = NULL;

  gsize file_length = 0;

  GError *error = NULL;

  g_return_val_if_fail (
    environment_name != NULL,
    FALSE);

  g_return_val_if_fail (
    output != NULL,
    FALSE);

  if (expected_length == 0)
    return FALSE;

  memset (
    output,
    0,
    expected_length);

  path =
    g_getenv (
      environment_name);

  if (path == NULL ||
      *path == '\0')
    return FALSE;

  if (!g_file_get_contents (
        path,
        &contents,
        &file_length,
        &error))
    {
      g_clear_error (&error);
      return FALSE;
    }

  if (file_length !=
      expected_length)
    goto fail;

  memcpy (
    output,
    contents,
    expected_length);

  goodix5135_live_tls_secure_clear (
    contents,
    file_length + 1U);

  g_free (
    contents);

  return TRUE;

fail:
  goodix5135_live_tls_secure_clear (
    output,
    expected_length);

  if (contents != NULL)
    {
      goodix5135_live_tls_secure_clear (
        contents,
        file_length + 1U);

      g_free (
        contents);
    }

  return FALSE;
}


static void
goodix5135_capture_runtime_reset (
  FpiDeviceGoodix5135 *self)
{
  g_return_if_fail (
    self != NULL);

  if (self->capture_baseline_settle_source != 0)
    {
      g_source_remove (
        self->capture_baseline_settle_source);

      self->capture_baseline_settle_source = 0;
    }

  goodix5135_live_tls_secure_clear (
    self->capture_fdt_seed,
    sizeof (self->capture_fdt_seed));

  goodix5135_live_tls_secure_clear (
    self->capture_fdt_down,
    sizeof (self->capture_fdt_down));

  goodix5135_live_tls_secure_clear (
    self->capture_fdt_up,
    sizeof (self->capture_fdt_up));

  goodix5135_live_tls_secure_clear (
    self->capture_plaintext,
    sizeof (self->capture_plaintext));

  self->capture_plaintext_length =
    0;

  self->capture_tls_reads =
    0;

  self->capture_runtime_state =
    GOODIX5135_CAPTURE_RUNTIME_IDLE;
}


static gboolean
goodix5135_capture_submit_out (
  FpDevice                       *device,
  const guint8                   *packet,
  Goodix5135AsyncCallback         callback)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (
      device);

  g_return_val_if_fail (
    packet != NULL,
    FALSE);

  g_return_val_if_fail (
    callback != NULL,
    FALSE);

  return goodix5135_async_submit (
    device,
    &self->io,
    GOODIX5135_REQUEST_BULK_OUT,
    GOODIX5135_CAPTURE_USB_LENGTH,
    GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
    packet,
    GOODIX5135_CAPTURE_USB_LENGTH,
    callback,
    NULL);
}


static gboolean
goodix5135_capture_submit_in (
  FpDevice                       *device,
  gsize                           length,
  guint                           timeout_ms,
  Goodix5135AsyncCallback         callback)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (
      device);

  g_return_val_if_fail (
    callback != NULL,
    FALSE);

  if (length == 0 ||
      timeout_ms == 0)
    return FALSE;

  return goodix5135_async_submit (
    device,
    &self->io,
    GOODIX5135_REQUEST_BULK_IN,
    length,
    timeout_ms,
    NULL,
    0,
    callback,
    NULL);
}


static gboolean
goodix5135_capture_out_completed (
  FpiUsbTransfer              *transfer,
  Goodix5135RequestCompletion  completion,
  GError                      *error)
{
  if (!goodix5135_async_result_can_advance (
        completion,
        error))
    return FALSE;

  if (transfer == NULL)
    return FALSE;

  return transfer->actual_length ==
         GOODIX5135_CAPTURE_USB_LENGTH;
}


static gboolean
goodix5135_capture_submit_tls_image_read (
  FpDevice *device)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (
      device);

  if (self->capture_runtime_state !=
      GOODIX5135_CAPTURE_RUNTIME_WAIT_IMAGE_TLS)
    return FALSE;

  if (self->capture_tls_reads >=
      GOODIX5135_CAPTURE_MAX_TLS_READS)
    return FALSE;

  self->capture_tls_reads++;

  return goodix5135_capture_submit_in (
    device,
    GOODIX5135_TLS_RUNTIME_SENSOR_READ_SIZE,
    GOODIX5135_CAPTURE_IMAGE_TIMEOUT_MS,
    goodix5135_tls_runtime_sensor_frame_cb);
}


static FpImage *
goodix5135_capture_make_fpimage (
  const guint16 *pixels,
  gsize          pixel_count)
{
  FpImage *image;

  gsize index;

  if (pixels == NULL ||
      pixel_count !=
        GOODIX5135_IMAGE_PIXELS)
    return NULL;

  image =
    fp_image_new (
      GOODIX5135_IMAGE_WIDTH,
      GOODIX5135_IMAGE_HEIGHT);

  if (image == NULL)
    return NULL;

  if (image->data == NULL)
    {
      g_object_unref (
        image);

      return NULL;
    }

  for (index = 0;
       index < GOODIX5135_IMAGE_PIXELS;
       index++)
    {
      guint16 value =
        pixels[index];

      if (value > 0x0fffU)
        value = 0x0fffU;

      image->data[index] =
        (guchar) (
          value >> 4
        );
    }

  image->flags =
    FPI_IMAGE_NONE;

  return image;
}


static gboolean
goodix5135_capture_start_image (
  FpDevice *device)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (
      device);

  guint8 packet[
    GOODIX5135_CAPTURE_USB_LENGTH
  ];

  gsize logical_length = 0;

  if (self->capture_runtime_state !=
      GOODIX5135_CAPTURE_RUNTIME_READY_IMAGE)
    return FALSE;

  if (!goodix5135_capture_build_image_request (
        packet,
        sizeof (packet),
        &logical_length))
    return FALSE;

  if (logical_length == 0 ||
      logical_length >
        GOODIX5135_CAPTURE_USB_LENGTH)
    return FALSE;

  self->capture_runtime_state =
    GOODIX5135_CAPTURE_RUNTIME_WAIT_IMAGE_OUT;

  fp_dbg (
    "Native FpImage prompt: KEEP_FINGER_ON_SENSOR");

  fp_dbg (
    "Native FpImage stage: image 0x20 request");

  return goodix5135_capture_submit_out (
    device,
    packet,
    goodix5135_capture_image_out_cb);
}


static gboolean
goodix5135_capture_start_fdt_up (
  FpDevice *device)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (
      device);

  guint8 packet[
    GOODIX5135_CAPTURE_USB_LENGTH
  ];

  gsize logical_length = 0;

  if (self->capture_runtime_state !=
      GOODIX5135_CAPTURE_RUNTIME_READY_FINGER_OFF)
    return FALSE;

  if (!goodix5135_capture_build_fdt_up (
        self->capture_fdt_up,
        sizeof (self->capture_fdt_up),
        packet,
        sizeof (packet),
        &logical_length))
    return FALSE;

  if (logical_length == 0 ||
      logical_length >
        GOODIX5135_CAPTURE_USB_LENGTH)
    return FALSE;

  self->capture_runtime_state =
    GOODIX5135_CAPTURE_RUNTIME_WAIT_UP_OUT;

  fp_dbg (
    "Native FpImage stage: FDT-up 0x34 request");

  fp_dbg (
    "Native FpImage prompt: LIFT_FINGER_NOW");

  return goodix5135_capture_submit_out (
    device,
    packet,
    goodix5135_capture_up_out_cb);
}


static void
goodix5135_capture_runtime_fail (
  FpDevice    *device,
  const gchar *reason)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (
      device);

  /*
   * Do not print private FDT values, image bytes, pixels,
   * TLS plaintext, or local private-input paths.
   */
  fp_warn (
    "Goodix native capture runtime stopped: %s",
    reason);

  if (goodix5135_fpimage_test_requested ())
    {
      GError *action_error =
        fpi_device_error_new_msg (
          FP_DEVICE_ERROR_GENERAL,
          "%s",
          reason);

      goodix5135_capture_runtime_reset (
        self);

      fpi_image_device_session_error (
        FP_IMAGE_DEVICE (device),
        action_error);

      return;
    }

  goodix5135_tls_runtime_reset (
    self);

  goodix5135_open_transaction_fail (
    device,
    reason);
}



static gboolean
goodix5135_capture_runtime_start (
  FpDevice *device)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (
      device);

  guint8 packet[
    GOODIX5135_CAPTURE_USB_LENGTH
  ];

  gsize logical_length = 0;

  if (!goodix5135_live_capture_test_requested () &&
      !goodix5135_fpimage_test_requested ())
    return FALSE;

  if ((!goodix5135_fpimage_test_requested () &&
       !self->tls_runtime_open_gate) ||
      !self->io.running ||
      self->tls_session == NULL ||
      !goodix5135_tls_session_is_ready (
        self->tls_session))
    return FALSE;

  if (self->capture_runtime_state !=
      GOODIX5135_CAPTURE_RUNTIME_IDLE)
    return FALSE;

  if (!goodix5135_capture_load_private_exact (
        GOODIX5135_LIVE_FDT_SEED_ENV,
        self->capture_fdt_seed,
        sizeof (self->capture_fdt_seed)))
    return FALSE;

  if (!goodix5135_capture_load_private_exact (
        GOODIX5135_LIVE_FDT_UP_ENV,
        self->capture_fdt_up,
        sizeof (self->capture_fdt_up)))
    return FALSE;

  fp_dbg (
    "Native capture private FDT input gate validated");

  if (!goodix5135_capture_build_fdt_manual (
        self->capture_fdt_seed,
        sizeof (self->capture_fdt_seed),
        packet,
        sizeof (packet),
        &logical_length))
    return FALSE;

  if (logical_length == 0 ||
      logical_length >
        GOODIX5135_CAPTURE_USB_LENGTH)
    return FALSE;

  self->capture_runtime_state =
    GOODIX5135_CAPTURE_RUNTIME_WAIT_MANUAL_OUT;

  fp_dbg (
    "Native capture stage: FDT manual 0x36 request");

  return goodix5135_capture_submit_out (
    device,
    packet,
    goodix5135_capture_manual_out_cb);
}


static void
goodix5135_capture_manual_out_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (
      device);

  gboolean ok;

  (void) user_data;

  ok =
    self->capture_runtime_state ==
      GOODIX5135_CAPTURE_RUNTIME_WAIT_MANUAL_OUT &&
    goodix5135_capture_out_completed (
      transfer,
      completion,
      error);

  g_clear_error (
    &error);

  if (!ok)
    {
      goodix5135_capture_runtime_fail (
        device,
        "FDT manual OUT failed");

      return;
    }

  self->capture_runtime_state =
    GOODIX5135_CAPTURE_RUNTIME_WAIT_MANUAL_ACK;

  if (!goodix5135_capture_submit_in (
        device,
        GOODIX5135_CAPTURE_USB_LENGTH,
        GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
        goodix5135_capture_manual_ack_cb))
    {
      goodix5135_capture_runtime_fail (
        device,
        "could not receive FDT manual ACK");
    }
}


static void
goodix5135_capture_manual_ack_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (
      device);

  gboolean transport_ok;
  gboolean protocol_ok;

  (void) user_data;

  transport_ok =
    self->capture_runtime_state ==
      GOODIX5135_CAPTURE_RUNTIME_WAIT_MANUAL_ACK &&
    goodix5135_in_transfer_can_parse (
      transfer,
      completion,
      error);

  protocol_ok =
    transport_ok &&
    goodix5135_capture_parse_ack (
      GOODIX5135_FDT_MODE_COMMAND,
      transfer->buffer,
      (gsize) transfer->actual_length);

  g_clear_error (
    &error);

  if (!protocol_ok)
    {
      goodix5135_capture_runtime_fail (
        device,
        transport_ok
          ? "FDT manual returned invalid ACK"
          : "FDT manual ACK transport failed");

      return;
    }

  fp_dbg (
    "Native capture stage: FDT manual ACK validated");

  self->capture_runtime_state =
    GOODIX5135_CAPTURE_RUNTIME_WAIT_MANUAL_RESPONSE;

  if (!goodix5135_capture_submit_in (
        device,
        GOODIX5135_CAPTURE_USB_LENGTH,
        GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
        goodix5135_capture_manual_response_cb))
    {
      goodix5135_capture_runtime_fail (
        device,
        "could not receive FDT manual response");
    }
}


static void
goodix5135_capture_manual_response_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (
      device);

  Goodix5135FdtResponse response = { 0 };

  guint8 packet[
    GOODIX5135_CAPTURE_USB_LENGTH
  ];

  gsize logical_length = 0;

  guint16 timestamp;

  guint recheck_attempt;

  gboolean transport_ok;
  gboolean protocol_ok;

  (void) user_data;

  transport_ok =
    self->capture_runtime_state ==
      GOODIX5135_CAPTURE_RUNTIME_WAIT_MANUAL_RESPONSE &&
    goodix5135_in_transfer_can_parse (
      transfer,
      completion,
      error);

  protocol_ok =
    transport_ok &&
    goodix5135_capture_parse_fdt_response (
      GOODIX5135_FDT_MODE_COMMAND,
      transfer->buffer,
      (gsize) transfer->actual_length,
      &response);

  g_clear_error (
    &error);

  if (!protocol_ok)
    {
      goodix5135_capture_runtime_fail (
        device,
        transport_ok
          ? "FDT manual response was invalid"
          : "FDT manual response transport failed");

      return;
    }

  if (response.touch_flag != 0)
    {
      if (!goodix5135_fpimage_test_requested () ||
          self->state !=
            FPI_IMAGE_DEVICE_STATE_AWAIT_FINGER_ON)
        {
          goodix5135_capture_runtime_fail (
            device,
            "finger was present during FDT manual baseline");

          return;
        }

      if (self->capture_baseline_recheck_count >=
          GOODIX5135_CAPTURE_BASELINE_RECHECK_MAX)
        {
          goodix5135_capture_runtime_fail (
            device,
            "finger-off state did not stabilize during bounded manual FDT rechecks");

          return;
        }

      recheck_attempt =
        self->capture_baseline_recheck_count + 1U;

      fp_dbg (
        "Native FpImage stage: manual baseline still reports touch; "
        "scheduling 100 ms stability recheck %u/%u",
        recheck_attempt,
        GOODIX5135_CAPTURE_BASELINE_RECHECK_MAX);

      /*
       * The completed manual response has no outstanding transport.
       * Reset only the host capture runtime so the next manual FDT
       * transaction starts from the normal IDLE path.
       *
       * This does not send a sensor reset command.
       */
      goodix5135_capture_runtime_reset (
        self);

      self->capture_baseline_recheck_count =
        recheck_attempt;

      self->capture_baseline_settle_source =
        g_timeout_add_full (
          G_PRIORITY_DEFAULT,
          GOODIX5135_CAPTURE_BASELINE_RECHECK_MS,
          goodix5135_capture_baseline_settle_cb,
          g_object_ref (device),
          g_object_unref);

      if (self->capture_baseline_settle_source == 0)
        {
          goodix5135_capture_runtime_fail (
            device,
            "could not schedule manual FDT stability recheck");
        }

      return;
    }

  fp_dbg (
    "Native FpImage stage: stable finger-off baseline confirmed after %u recheck(s)",
    self->capture_baseline_recheck_count);

  self->capture_baseline_recheck_count = 0;

  if (!goodix5135_capture_derive_fdt_down_registers (
        &response,
        self->capture_fdt_down))
    {
      goodix5135_capture_runtime_fail (
        device,
        "could not derive FDT-down thresholds");

      return;
    }

  /*
   * Match the proven 5135 reference:
   *
   *   (monotonic microseconds) & 0xffff
   */
  timestamp =
    (guint16) (
      ((guint64) g_get_monotonic_time ()) &
      0xffffU);

  if (!goodix5135_capture_build_fdt_down (
        self->capture_fdt_down,
        sizeof (self->capture_fdt_down),
        timestamp,
        packet,
        sizeof (packet),
        &logical_length))
    {
      goodix5135_capture_runtime_fail (
        device,
        "could not build FDT-down request");

      return;
    }

  goodix5135_live_tls_secure_clear (
    self->capture_fdt_seed,
    sizeof (self->capture_fdt_seed));

  fp_dbg (
    "Native capture stage: FDT manual baseline validated");

  self->capture_runtime_state =
    GOODIX5135_CAPTURE_RUNTIME_WAIT_DOWN_OUT;

  fp_dbg (
    "Native capture stage: FDT-down 0x32 request");

  if (!goodix5135_capture_submit_out (
        device,
        packet,
        goodix5135_capture_down_out_cb))
    {
      goodix5135_capture_runtime_fail (
        device,
        "could not submit FDT-down request");
    }
}


static void
goodix5135_capture_down_out_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (
      device);

  gboolean ok;

  (void) user_data;

  ok =
    self->capture_runtime_state ==
      GOODIX5135_CAPTURE_RUNTIME_WAIT_DOWN_OUT &&
    goodix5135_capture_out_completed (
      transfer,
      completion,
      error);

  g_clear_error (
    &error);

  if (!ok)
    {
      goodix5135_capture_runtime_fail (
        device,
        "FDT-down OUT failed");

      return;
    }

  self->capture_runtime_state =
    GOODIX5135_CAPTURE_RUNTIME_WAIT_DOWN_ACK;

  if (!goodix5135_capture_submit_in (
        device,
        GOODIX5135_CAPTURE_USB_LENGTH,
        GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
        goodix5135_capture_down_ack_cb))
    {
      goodix5135_capture_runtime_fail (
        device,
        "could not receive FDT-down ACK");
    }
}


static void
goodix5135_capture_down_ack_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (
      device);

  gboolean transport_ok;
  gboolean protocol_ok;

  (void) user_data;

  transport_ok =
    self->capture_runtime_state ==
      GOODIX5135_CAPTURE_RUNTIME_WAIT_DOWN_ACK &&
    goodix5135_in_transfer_can_parse (
      transfer,
      completion,
      error);

  protocol_ok =
    transport_ok &&
    goodix5135_capture_parse_ack (
      GOODIX5135_FDT_DOWN_COMMAND,
      transfer->buffer,
      (gsize) transfer->actual_length);

  g_clear_error (
    &error);

  if (!protocol_ok)
    {
      goodix5135_capture_runtime_fail (
        device,
        transport_ok
          ? "FDT-down returned invalid ACK"
          : "FDT-down ACK transport failed");

      return;
    }

  fp_dbg (
    "Native capture stage: FDT-down ACK validated");

  self->capture_runtime_state =
    GOODIX5135_CAPTURE_RUNTIME_WAIT_DOWN_RESPONSE;

  fp_dbg (
    "Native FpImage prompt: PLACE_FINGER_NOW");

  if (!goodix5135_capture_submit_in (
        device,
        GOODIX5135_CAPTURE_USB_LENGTH,
        GOODIX5135_CAPTURE_EVENT_TIMEOUT_MS,
        goodix5135_capture_down_response_cb))
    {
      goodix5135_capture_runtime_fail (
        device,
        "could not arm FDT-down event receive");
    }
}


static void
goodix5135_capture_down_response_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (
      device);

  Goodix5135FdtResponse response = { 0 };

  gboolean transport_ok;
  gboolean protocol_ok;

  (void) user_data;

  transport_ok =
    self->capture_runtime_state ==
      GOODIX5135_CAPTURE_RUNTIME_WAIT_DOWN_RESPONSE &&
    goodix5135_in_transfer_can_parse (
      transfer,
      completion,
      error);

  protocol_ok =
    transport_ok &&
    goodix5135_capture_parse_fdt_response (
      GOODIX5135_FDT_DOWN_COMMAND,
      transfer->buffer,
      (gsize) transfer->actual_length,
      &response);

  g_clear_error (
    &error);

  if (!protocol_ok)
    {
      goodix5135_capture_runtime_fail (
        device,
        transport_ok
          ? "FDT-down event was invalid"
          : "FDT-down event transport failed");

      return;
    }

  if ((response.touch_flag & 0x3fU) == 0)
    {
      goodix5135_capture_runtime_fail (
        device,
        "FDT-down event reported zero touched zones");

      return;
    }

  fp_dbg (
    "Native capture stage: FDT-down event validated");

  self->capture_runtime_state =
    GOODIX5135_CAPTURE_RUNTIME_READY_IMAGE;

  fp_dbg (
    "Native FpImage stage: reporting finger present");

  fpi_image_device_report_finger_status (
    FP_IMAGE_DEVICE (device),
    TRUE);
}


static void
goodix5135_capture_image_out_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (
      device);

  gboolean ok;

  (void) user_data;

  ok =
    self->capture_runtime_state ==
      GOODIX5135_CAPTURE_RUNTIME_WAIT_IMAGE_OUT &&
    goodix5135_capture_out_completed (
      transfer,
      completion,
      error);

  g_clear_error (
    &error);

  if (!ok)
    {
      goodix5135_capture_runtime_fail (
        device,
        "image 0x20 OUT failed");

      return;
    }

  self->capture_runtime_state =
    GOODIX5135_CAPTURE_RUNTIME_WAIT_IMAGE_ACK;

  if (!goodix5135_capture_submit_in (
        device,
        GOODIX5135_CAPTURE_USB_LENGTH,
        GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
        goodix5135_capture_image_ack_cb))
    {
      goodix5135_capture_runtime_fail (
        device,
        "could not receive image 0x20 ACK");
    }
}


static void
goodix5135_capture_image_ack_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (
      device);

  gboolean transport_ok;
  gboolean protocol_ok;

  (void) user_data;

  transport_ok =
    self->capture_runtime_state ==
      GOODIX5135_CAPTURE_RUNTIME_WAIT_IMAGE_ACK &&
    goodix5135_in_transfer_can_parse (
      transfer,
      completion,
      error);

  protocol_ok =
    transport_ok &&
    goodix5135_capture_parse_ack (
      GOODIX5135_IMAGE_COMMAND,
      transfer->buffer,
      (gsize) transfer->actual_length);

  g_clear_error (
    &error);

  if (!protocol_ok)
    {
      goodix5135_capture_runtime_fail (
        device,
        transport_ok
          ? "image 0x20 returned invalid ACK"
          : "image 0x20 ACK transport failed");

      return;
    }

  fp_dbg (
    "Native capture stage: image 0x20 ACK validated");

  self->capture_plaintext_length =
    0;

  self->capture_tls_reads =
    0;

  self->capture_runtime_state =
    GOODIX5135_CAPTURE_RUNTIME_WAIT_IMAGE_TLS;

  if (!goodix5135_capture_submit_tls_image_read (
        device))
    {
      goodix5135_capture_runtime_fail (
        device,
        "could not receive TLS image frame");
    }
}


static void
goodix5135_capture_accept_tls_plaintext (
  FpDevice      *device,
  const guint8  *data,
  gsize          data_length)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (
      device);

  guint16 pixels[
    GOODIX5135_IMAGE_PIXELS
  ];

  FpImage *image = NULL;

  gsize remaining;

  gboolean decoded;

  memset (
    pixels,
    0,
    sizeof (pixels));

  if (self->capture_runtime_state !=
      GOODIX5135_CAPTURE_RUNTIME_WAIT_IMAGE_TLS)
    {
      goodix5135_capture_runtime_fail (
        device,
        "TLS image plaintext arrived out of order");

      return;
    }

  remaining =
    sizeof (self->capture_plaintext) -
    self->capture_plaintext_length;

  if (data_length >
      remaining)
    {
      goodix5135_live_tls_secure_clear (
        pixels,
        sizeof (pixels));

      goodix5135_capture_runtime_fail (
        device,
        "TLS image plaintext exceeded expected length");

      return;
    }

  if (data_length > 0)
    {
      memcpy (
        self->capture_plaintext +
          self->capture_plaintext_length,
        data,
        data_length);

      self->capture_plaintext_length +=
        data_length;
    }

  if (self->capture_plaintext_length <
      GOODIX5135_IMAGE_TOTAL_LENGTH)
    {
      goodix5135_live_tls_secure_clear (
        pixels,
        sizeof (pixels));

      if (!goodix5135_capture_submit_tls_image_read (
            device))
        {
          goodix5135_capture_runtime_fail (
            device,
            "TLS image plaintext remained incomplete");
        }

      return;
    }

  if (self->capture_plaintext_length !=
      GOODIX5135_IMAGE_TOTAL_LENGTH)
    {
      goodix5135_live_tls_secure_clear (
        pixels,
        sizeof (pixels));

      goodix5135_capture_runtime_fail (
        device,
        "TLS image plaintext length mismatch");

      return;
    }

  fp_dbg (
    "Native capture stage: TLS image plaintext complete");

  decoded =
    goodix5135_decode_image_response (
      self->capture_plaintext,
      self->capture_plaintext_length,
      pixels,
      G_N_ELEMENTS (pixels));

  goodix5135_live_tls_secure_clear (
    self->capture_plaintext,
    sizeof (self->capture_plaintext));

  self->capture_plaintext_length =
    0;

  if (!decoded)
    {
      goodix5135_live_tls_secure_clear (
        pixels,
        sizeof (pixels));

      goodix5135_capture_runtime_fail (
        device,
        "native image framing, CRC, or RAW12 decode failed");

      return;
    }

  fp_dbg (
    "Native capture stage: image 5120-pixel decode validated");

  image =
    goodix5135_capture_make_fpimage (
      pixels,
      G_N_ELEMENTS (pixels));

  /*
   * The original 12-bit plane is never printed, persisted, hashed,
   * or retained after constructing the FpImage.
   */
  goodix5135_live_tls_secure_clear (
    pixels,
    sizeof (pixels));

  if (image == NULL)
    {
      goodix5135_capture_runtime_fail (
        device,
        "could not construct 80x64 FpImage");

      return;
    }

  self->capture_runtime_state =
    GOODIX5135_CAPTURE_RUNTIME_READY_FINGER_OFF;

  fp_dbg (
    "Native FpImage stage: handing 80x64 image to libfprint");

  /*
   * Ownership transfers to libfprint here.
   * Do not touch or unref image after this call.
   */
  fpi_image_device_image_captured (
    FP_IMAGE_DEVICE (device),
    image);
}


static void
goodix5135_capture_up_out_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (
      device);

  gboolean ok;

  (void) user_data;

  ok =
    self->capture_runtime_state ==
      GOODIX5135_CAPTURE_RUNTIME_WAIT_UP_OUT &&
    goodix5135_capture_out_completed (
      transfer,
      completion,
      error);

  g_clear_error (
    &error);

  if (!ok)
    {
      goodix5135_capture_runtime_fail (
        device,
        "FDT-up OUT failed");

      return;
    }

  self->capture_runtime_state =
    GOODIX5135_CAPTURE_RUNTIME_WAIT_UP_ACK;

  if (!goodix5135_capture_submit_in (
        device,
        GOODIX5135_CAPTURE_USB_LENGTH,
        GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
        goodix5135_capture_up_ack_cb))
    {
      goodix5135_capture_runtime_fail (
        device,
        "could not receive FDT-up ACK");
    }
}


static void
goodix5135_capture_up_ack_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (
      device);

  gboolean transport_ok;
  gboolean protocol_ok;

  (void) user_data;

  transport_ok =
    self->capture_runtime_state ==
      GOODIX5135_CAPTURE_RUNTIME_WAIT_UP_ACK &&
    goodix5135_in_transfer_can_parse (
      transfer,
      completion,
      error);

  protocol_ok =
    transport_ok &&
    goodix5135_capture_parse_ack (
      GOODIX5135_FDT_UP_COMMAND,
      transfer->buffer,
      (gsize) transfer->actual_length);

  g_clear_error (
    &error);

  if (!protocol_ok)
    {
      goodix5135_capture_runtime_fail (
        device,
        transport_ok
          ? "FDT-up returned invalid ACK"
          : "FDT-up ACK transport failed");

      return;
    }

  fp_dbg (
    "Native capture stage: FDT-up ACK validated");

  self->capture_runtime_state =
    GOODIX5135_CAPTURE_RUNTIME_WAIT_UP_RESPONSE;

  fp_dbg (
    "Native capture prompt: LIFT_FINGER_NOW");

  if (!goodix5135_capture_submit_in (
        device,
        GOODIX5135_CAPTURE_USB_LENGTH,
        GOODIX5135_CAPTURE_EVENT_TIMEOUT_MS,
        goodix5135_capture_up_response_cb))
    {
      goodix5135_capture_runtime_fail (
        device,
        "could not arm FDT-up event receive");
    }
}


static void
goodix5135_capture_up_response_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (
      device);

  Goodix5135FdtResponse response = { 0 };

  gboolean transport_ok;
  gboolean protocol_ok;

  (void) user_data;

  transport_ok =
    self->capture_runtime_state ==
      GOODIX5135_CAPTURE_RUNTIME_WAIT_UP_RESPONSE &&
    goodix5135_in_transfer_can_parse (
      transfer,
      completion,
      error);

  protocol_ok =
    transport_ok &&
    goodix5135_capture_parse_fdt_response (
      GOODIX5135_FDT_UP_COMMAND,
      transfer->buffer,
      (gsize) transfer->actual_length,
      &response);

  g_clear_error (
    &error);

  if (!protocol_ok)
    {
      goodix5135_capture_runtime_fail (
        device,
        transport_ok
          ? "FDT-up event was invalid"
          : "FDT-up event transport failed");

      return;
    }

  if (response.touch_flag != 0)
    {
      goodix5135_capture_runtime_fail (
        device,
        "FDT-up event did not clear touch state");

      return;
    }

  fp_dbg (
    "Native capture stage: FDT-up event validated");

  if (goodix5135_fpimage_test_requested ())
    {
      /*
       * Clear private FDT state before returning control to libfprint.
       * TLS remains alive across action deactivation for the explicit
       * two-capture lifecycle gate.
       */
      goodix5135_capture_runtime_reset (
        self);

      fp_dbg (
        "Native FpImage stage: reporting finger absent");

      fpi_image_device_report_finger_status (
        FP_IMAGE_DEVICE (device),
        FALSE);

      return;
    }

  /*
   * Clear all private FDT and fingerprint material before OPEN
   * completes. The established TLS object remains volatile until
   * normal CLOSE.
   */
  goodix5135_capture_runtime_reset (
    self);

  self->tls_runtime_open_gate =
    FALSE;

  goodix5135_io_stop (
    &self->io);

  g_assert (
    goodix5135_io_can_finish_stop (
      &self->io));

  fp_dbg (
    "One-shot native capture OPEN gate completed");

  fpi_image_device_open_complete (
    FP_IMAGE_DEVICE (device),
    NULL);
}


static void
goodix5135_tls_runtime_set_error (
  GError      **error,
  const gchar  *message)
{
  if (error == NULL ||
      *error != NULL)
    return;

  *error =
    fpi_device_error_new_msg (
      FP_DEVICE_ERROR_GENERAL,
      "%s",
      message);
}


static void
goodix5135_tls_runtime_reset (
  FpiDeviceGoodix5135 *self)
{
  g_return_if_fail (self != NULL);

  goodix5135_capture_runtime_reset (
    self);

  if (self->tls_runtime_d4_delay_source != 0)
    {
      g_source_remove (
        self->tls_runtime_d4_delay_source);

      self->tls_runtime_d4_delay_source = 0;
    }

  if (self->tls_session != NULL)
    {
      goodix5135_tls_session_free (
        self->tls_session);

      self->tls_session = NULL;
    }

  self->tls_runtime_open_gate =
    FALSE;

  goodix5135_tls_request_transaction_init (
    &self->tls_request_transaction);

  goodix5135_d4_transaction_init (
    &self->tls_runtime_d4_transaction);
}


static void
goodix5135_tls_runtime_fail (
  FpDevice    *device,
  const gchar *reason)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  gboolean open_gate =
    self->tls_runtime_open_gate;

  /*
   * Never print TLS payloads, PSK material, sensor frame bytes,
   * or private input paths.
   */
  fp_warn (
    "Goodix TLS runtime bridge stopped: %s",
    reason);

  goodix5135_tls_runtime_reset (
    self);

  if (open_gate)
    {
      goodix5135_open_transaction_fail (
        device,
        reason);
    }
}


static gboolean
goodix5135_tls_runtime_submit_sensor_read (
  FpDevice *device)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  if (self->tls_session == NULL)
    return FALSE;

  /*
   * Public Goodix USB reference reads up to 0x10000 bytes.
   *
   * A bulk transfer may span multiple 64-byte USB packets.
   * The transport layer owns the receive buffer.
   */
  return goodix5135_async_submit (
    device,
    &self->io,
    GOODIX5135_REQUEST_BULK_IN,
    GOODIX5135_TLS_RUNTIME_SENSOR_READ_SIZE,
    GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
    NULL,
    0,
    goodix5135_tls_runtime_sensor_frame_cb,
    NULL);
}


static gboolean
goodix5135_tls_runtime_pad_host_frame (
  GByteArray *frame)
{
  static const guint8 zeros[GOODIX5135_USB_PACKET_LENGTH] = { 0 };

  gsize padded_length;
  gsize padding;

  g_return_val_if_fail (frame != NULL, FALSE);

  if (frame->len == 0)
    return FALSE;

  /*
   * Match the public Goodix USB write contract:
   *
   * every outbound logical frame is zero-padded to a complete
   * 64-byte USB packet boundary before Bulk OUT.
   */
  padded_length =
    (
      ((gsize) frame->len +
       GOODIX5135_USB_PACKET_LENGTH - 1)
      /
      GOODIX5135_USB_PACKET_LENGTH
    )
    *
    GOODIX5135_USB_PACKET_LENGTH;

  if (padded_length < frame->len ||
      padded_length > G_MAXUINT)
    return FALSE;

  padding =
    padded_length - frame->len;

  if (padding > 0)
    {
      g_assert (padding <
                GOODIX5135_USB_PACKET_LENGTH);

      g_byte_array_append (
        frame,
        zeros,
        (guint) padding);
    }

  return TRUE;
}


static gboolean
goodix5135_tls_runtime_submit_host_frame (
  FpDevice   *device,
  GByteArray *host_frame)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  guint transfer_length;

  if (self->tls_session == NULL ||
      host_frame == NULL ||
      host_frame->len == 0)
    return FALSE;

  if (!goodix5135_tls_runtime_pad_host_frame (
        host_frame))
    return FALSE;

  transfer_length =
    (guint) host_frame->len;

  return goodix5135_async_submit (
    device,
    &self->io,
    GOODIX5135_REQUEST_BULK_OUT,
    transfer_length,
    GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
    host_frame->data,
    host_frame->len,
    goodix5135_tls_runtime_host_frame_out_cb,
    GUINT_TO_POINTER (transfer_length));
}


static gboolean
goodix5135_tls_runtime_d4_delay_cb (
  gpointer user_data)
{
  FpDevice *device =
    FP_DEVICE (user_data);

  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  self->tls_runtime_d4_delay_source =
    0;

  if (self->tls_session == NULL ||
      !self->io.running)
    return G_SOURCE_REMOVE;

  fp_dbg (
    "Native TLS stage: D4 settle delay completed");

  goodix5135_tls_runtime_start_d4 (
    device);

  return G_SOURCE_REMOVE;
}


static gboolean
goodix5135_tls_runtime_schedule_d4 (
  FpDevice *device)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  if (self->tls_session == NULL ||
      !self->io.running ||
      self->tls_runtime_d4_delay_source != 0)
    return FALSE;

  fp_dbg (
    "Native TLS stage: scheduling D4 after 10 ms settle delay");

  self->tls_runtime_d4_delay_source =
    g_timeout_add_full (
      G_PRIORITY_DEFAULT,
      GOODIX5135_TLS_D4_SETTLE_DELAY_MS,
      goodix5135_tls_runtime_d4_delay_cb,
      g_object_ref (device),
      g_object_unref);

  return
    self->tls_runtime_d4_delay_source != 0;
}


static void
goodix5135_tls_runtime_start_d4 (
  FpDevice *device)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize  logical_length = 0;

  if (self->tls_session == NULL)
    {
      goodix5135_tls_runtime_fail (
        device,
        "D4 requested without an active TLS session");

      return;
    }

  goodix5135_d4_transaction_init (
    &self->tls_runtime_d4_transaction);

  if (!goodix5135_d4_transaction_begin (
        &self->tls_runtime_d4_transaction,
        packet,
        sizeof (packet),
        &logical_length))
    {
      goodix5135_tls_runtime_fail (
        device,
        "could not build TLS-runtime D4 request");

      return;
    }

  if (logical_length !=
      GOODIX5135_D4_REQUEST_LENGTH)
    {
      goodix5135_d4_transaction_out_complete (
        &self->tls_runtime_d4_transaction,
        FALSE);

      goodix5135_tls_runtime_fail (
        device,
        "TLS-runtime D4 request length mismatch");

      return;
    }

  fp_dbg (
    "Native TLS stage: submitting D4 confirmation");

  if (!goodix5135_async_submit (
        device,
        &self->io,
        GOODIX5135_REQUEST_BULK_OUT,
        GOODIX5135_USB_PACKET_LENGTH,
        GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
        packet,
        sizeof (packet),
        goodix5135_tls_runtime_d4_out_cb,
        NULL))
    {
      goodix5135_d4_transaction_out_complete (
        &self->tls_runtime_d4_transaction,
        FALSE);

      goodix5135_tls_runtime_fail (
        device,
        "could not submit TLS-runtime D4 request");
    }
}


static gboolean
goodix5135_tls_runtime_start (
  FpDevice      *device,
  const guint8  *psk,
  gsize          psk_len,
  GError       **error)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  guint8 packet[GOODIX5135_TLS_REQUEST_USB_LENGTH];
  guint8 command = 0;

  gsize logical_length = 0;

  Goodix5135TlsSessionAction action =
    GOODIX5135_TLS_SESSION_ACTION_NONE;

  g_return_val_if_fail (
    error == NULL || *error == NULL,
    FALSE);

  if (psk == NULL ||
      psk_len == 0)
    {
      goodix5135_tls_runtime_set_error (
        error,
        "Goodix TLS runtime requires a non-empty private PSK");

      return FALSE;
    }

  if (!self->io.running)
    {
      goodix5135_tls_runtime_set_error (
        error,
        "Goodix TLS runtime requires a running I/O lifecycle");

      return FALSE;
    }

  if (self->tls_session != NULL)
    {
      goodix5135_tls_runtime_set_error (
        error,
        "Goodix TLS runtime session is already active");

      return FALSE;
    }

  self->tls_session =
    goodix5135_tls_session_new (
      psk,
      psk_len,
      error);

  if (self->tls_session == NULL)
    return FALSE;

  if (!goodix5135_tls_session_start (
        self->tls_session,
        &command,
        &action,
        error))
    {
      goodix5135_tls_runtime_reset (
        self);

      return FALSE;
    }

  if (command !=
        GOODIX5135_TLS_REQUEST_COMMAND ||
      action !=
        GOODIX5135_TLS_SESSION_ACTION_REQUEST_D0)
    {
      goodix5135_tls_runtime_set_error (
        error,
        "Goodix TLS session did not request D0");

      goodix5135_tls_runtime_reset (
        self);

      return FALSE;
    }

  goodix5135_tls_request_transaction_init (
    &self->tls_request_transaction);

  if (!goodix5135_tls_request_transaction_begin (
        &self->tls_request_transaction,
        packet,
        sizeof (packet),
        &logical_length))
    {
      goodix5135_tls_runtime_set_error (
        error,
        "Could not begin Goodix TLS D0 transaction");

      goodix5135_tls_runtime_reset (
        self);

      return FALSE;
    }

  if (logical_length !=
      GOODIX5135_TLS_REQUEST_LOGICAL_LENGTH)
    {
      goodix5135_tls_request_transaction_out_complete (
        &self->tls_request_transaction,
        FALSE);

      goodix5135_tls_runtime_set_error (
        error,
        "Goodix TLS D0 request length mismatch");

      goodix5135_tls_runtime_reset (
        self);

      return FALSE;
    }

  fp_dbg (
    "Native TLS stage: submitting D0 request");

  if (!goodix5135_async_submit (
        device,
        &self->io,
        GOODIX5135_REQUEST_BULK_OUT,
        GOODIX5135_TLS_REQUEST_USB_LENGTH,
        GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
        packet,
        sizeof (packet),
        goodix5135_tls_runtime_d0_out_cb,
        NULL))
    {
      goodix5135_tls_request_transaction_out_complete (
        &self->tls_request_transaction,
        FALSE);

      goodix5135_tls_runtime_set_error (
        error,
        "Could not submit Goodix TLS D0 request");

      goodix5135_tls_runtime_reset (
        self);

      return FALSE;
    }

  return TRUE;
}


static void
goodix5135_tls_runtime_d0_out_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  gboolean transport_can_advance;

  (void) user_data;

  transport_can_advance =
    goodix5135_async_result_can_advance (
      completion,
      error);

  if (transport_can_advance &&
      (transfer == NULL ||
       transfer->actual_length !=
         GOODIX5135_TLS_REQUEST_USB_LENGTH))
    transport_can_advance = FALSE;

  g_clear_error (&error);

  if (!goodix5135_tls_request_transaction_out_complete (
        &self->tls_request_transaction,
        transport_can_advance))
    {
      goodix5135_tls_runtime_fail (
        device,
        "TLS D0 OUT transport failed");

      return;
    }

  if (!goodix5135_async_submit (
        device,
        &self->io,
        GOODIX5135_REQUEST_BULK_IN,
        GOODIX5135_USB_PACKET_LENGTH,
        GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
        NULL,
        0,
        goodix5135_tls_runtime_d0_ack_cb,
        NULL))
    {
      goodix5135_tls_request_transaction_ack_complete (
        &self->tls_request_transaction,
        FALSE,
        NULL,
        0);

      goodix5135_tls_runtime_fail (
        device,
        "could not submit TLS D0 ACK receive");
    }
}


static void
goodix5135_tls_runtime_d0_ack_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  gboolean transport_can_advance;
  gboolean protocol_ok;

  (void) user_data;

  transport_can_advance =
    goodix5135_in_transfer_can_parse (
      transfer,
      completion,
      error);

  protocol_ok =
    goodix5135_tls_request_transaction_ack_complete (
      &self->tls_request_transaction,
      transport_can_advance,
      transport_can_advance
        ? transfer->buffer
        : NULL,
      transport_can_advance
        ? (gsize) transfer->actual_length
        : 0);

  g_clear_error (&error);

  if (!protocol_ok ||
      self->tls_request_transaction.state !=
        GOODIX5135_TLS_REQUEST_TRANSACTION_DONE)
    {
      goodix5135_tls_runtime_fail (
        device,
        transport_can_advance
          ? "TLS D0 returned invalid ACK"
          : "TLS D0 ACK transport failed");

      return;
    }

  fp_dbg (
    "Native TLS stage: D0 ACK validated");

  if (!goodix5135_tls_runtime_submit_sensor_read (
        device))
    {
      goodix5135_tls_runtime_fail (
        device,
        "could not submit first B0 TLS receive");
    }
}


static void
goodix5135_tls_runtime_sensor_frame_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  g_autoptr(GByteArray) host_frame =
    g_byte_array_new ();

  g_autoptr(GByteArray) app_output =
    g_byte_array_new ();

  Goodix5135TlsSessionAction action =
    GOODIX5135_TLS_SESSION_ACTION_NONE;

  guint8 command = 0;

  gboolean transport_can_advance;
  gboolean session_ok;

  (void) user_data;

  transport_can_advance =
    goodix5135_in_transfer_can_parse (
      transfer,
      completion,
      error);

  if (!transport_can_advance)
    {
      g_clear_error (&error);

      goodix5135_tls_runtime_fail (
        device,
        "TLS B0 receive transport failed");

      return;
    }

  session_ok =
    goodix5135_tls_session_feed_sensor_frame (
      self->tls_session,
      transfer->buffer,
      (gsize) transfer->actual_length,
      host_frame,
      app_output,
      &command,
      &action,
      &error);

  g_clear_error (&error);

  if (!session_ok)
    {
      if (self->capture_runtime_state ==
          GOODIX5135_CAPTURE_RUNTIME_WAIT_IMAGE_TLS)
        {
          goodix5135_capture_runtime_fail (
            device,
            "post-handshake TLS image decrypt failed");
        }
      else
        {
          goodix5135_tls_runtime_fail (
            device,
            "TLS sensor frame could not be relayed");
        }

      return;
    }

  /*
   * READY-state application plaintext belongs to the explicitly
   * armed one-shot image capture runtime.
   */
  if (self->capture_runtime_state ==
        GOODIX5135_CAPTURE_RUNTIME_WAIT_IMAGE_TLS &&
      goodix5135_tls_session_is_ready (
        self->tls_session))
    {
      if (host_frame->len != 0 ||
          command != 0 ||
          action !=
            GOODIX5135_TLS_SESSION_ACTION_NONE)
        {
          goodix5135_capture_runtime_fail (
            device,
            "post-handshake TLS image action was inconsistent");

          return;
        }

      goodix5135_capture_accept_tls_plaintext (
        device,
        app_output->data,
        app_output->len);

      return;
    }

  /*
   * During the handshake bridge no application plaintext is expected.
   * Never print or persist it if malformed traffic produces any.
   */
  if (app_output->len != 0)
    {
      goodix5135_tls_runtime_fail (
        device,
        "unexpected application data during TLS handshake");

      return;
    }

  switch (action)
    {
    case GOODIX5135_TLS_SESSION_ACTION_NONE:
      if (host_frame->len != 0 ||
          command != 0)
        {
          goodix5135_tls_runtime_fail (
            device,
            "TLS session returned inconsistent NONE action");

          return;
        }

      if (!goodix5135_tls_runtime_submit_sensor_read (
            device))
        {
          goodix5135_tls_runtime_fail (
            device,
            "could not continue B0 TLS receive");
        }

      return;

    case GOODIX5135_TLS_SESSION_ACTION_SEND_FRAME:
      if (host_frame->len == 0 ||
          command != 0)
        {
          goodix5135_tls_runtime_fail (
            device,
            "TLS SEND_FRAME action had invalid output");

          return;
        }

      fp_dbg (
        "Native TLS stage: sensor TLS frame produced host response");

      if (!goodix5135_tls_runtime_submit_host_frame (
            device,
            host_frame))
        {
          goodix5135_tls_runtime_fail (
            device,
            "could not submit host B0 TLS frame");
        }

      return;

    case GOODIX5135_TLS_SESSION_ACTION_SEND_D4:
      if (host_frame->len != 0 ||
          command != GOODIX5135_TLS_COMMAND_ESTABLISHED)
        {
          goodix5135_tls_runtime_fail (
            device,
            "TLS SEND_D4 action had invalid output");

          return;
        }

      if (!goodix5135_tls_runtime_schedule_d4 (
            device))
        {
          goodix5135_tls_runtime_fail (
            device,
            "could not schedule TLS D4 settle delay");
        }

      return;

    case GOODIX5135_TLS_SESSION_ACTION_REQUEST_D0:
    case GOODIX5135_TLS_SESSION_ACTION_READY:
    default:
      goodix5135_tls_runtime_fail (
        device,
        "TLS sensor frame advanced to an invalid action");

      return;
    }
}


static void
goodix5135_tls_runtime_host_frame_out_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  Goodix5135TlsSessionAction action =
    GOODIX5135_TLS_SESSION_ACTION_NONE;

  guint8 command = 0;
  guint expected_length =
    GPOINTER_TO_UINT (user_data);

  gboolean transport_can_advance;
  gboolean session_ok;

  transport_can_advance =
    goodix5135_async_result_can_advance (
      completion,
      error);

  if (transport_can_advance &&
      (transfer == NULL ||
       expected_length == 0 ||
       transfer->actual_length !=
         (gssize) expected_length))
    transport_can_advance = FALSE;

  g_clear_error (&error);

  if (!transport_can_advance)
    {
      goodix5135_tls_runtime_fail (
        device,
        "host B0 TLS OUT transport failed");

      return;
    }

  fp_dbg (
    "Native TLS stage: host TLS frame sent");

  session_ok =
    goodix5135_tls_session_host_frame_sent (
      self->tls_session,
      &command,
      &action,
      &error);

  g_clear_error (&error);

  if (!session_ok)
    {
      goodix5135_tls_runtime_fail (
        device,
        "TLS host-frame completion was out of order");

      return;
    }

  switch (action)
    {
    case GOODIX5135_TLS_SESSION_ACTION_NONE:
      if (command != 0)
        {
          goodix5135_tls_runtime_fail (
            device,
            "TLS host-frame NONE action contained a command");

          return;
        }

      if (!goodix5135_tls_runtime_submit_sensor_read (
            device))
        {
          goodix5135_tls_runtime_fail (
            device,
            "could not continue TLS handshake receive");
        }

      return;

    case GOODIX5135_TLS_SESSION_ACTION_SEND_D4:
      if (command !=
          GOODIX5135_TLS_COMMAND_ESTABLISHED)
        {
          goodix5135_tls_runtime_fail (
            device,
            "TLS host-frame completion produced invalid D4 command");

          return;
        }

      if (!goodix5135_tls_runtime_schedule_d4 (
            device))
        {
          goodix5135_tls_runtime_fail (
            device,
            "could not schedule TLS D4 settle delay");
        }

      return;

    case GOODIX5135_TLS_SESSION_ACTION_REQUEST_D0:
    case GOODIX5135_TLS_SESSION_ACTION_SEND_FRAME:
    case GOODIX5135_TLS_SESSION_ACTION_READY:
    default:
      goodix5135_tls_runtime_fail (
        device,
        "TLS host-frame completion advanced to invalid action");

      return;
    }
}


static void
goodix5135_tls_runtime_d4_out_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  gboolean transport_can_advance;

  (void) user_data;

  transport_can_advance =
    goodix5135_async_result_can_advance (
      completion,
      error);

  if (transport_can_advance &&
      (transfer == NULL ||
       transfer->actual_length !=
         GOODIX5135_USB_PACKET_LENGTH))
    transport_can_advance = FALSE;

  g_clear_error (&error);

  if (!goodix5135_d4_transaction_out_complete (
        &self->tls_runtime_d4_transaction,
        transport_can_advance))
    {
      goodix5135_tls_runtime_fail (
        device,
        "TLS-runtime D4 OUT transport failed");

      return;
    }

  if (!goodix5135_async_submit (
        device,
        &self->io,
        GOODIX5135_REQUEST_BULK_IN,
        GOODIX5135_USB_PACKET_LENGTH,
        GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
        NULL,
        0,
        goodix5135_tls_runtime_d4_ack_cb,
        NULL))
    {
      goodix5135_d4_transaction_ack_complete (
        &self->tls_runtime_d4_transaction,
        FALSE,
        NULL,
        0);

      goodix5135_tls_runtime_fail (
        device,
        "could not submit TLS-runtime D4 ACK receive");
    }
}


static void
goodix5135_tls_runtime_d4_ack_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  Goodix5135TlsSessionAction action =
    GOODIX5135_TLS_SESSION_ACTION_NONE;

  gboolean transport_can_advance;
  gboolean protocol_ok;
  gboolean session_ok;

  (void) user_data;

  transport_can_advance =
    goodix5135_in_transfer_can_parse (
      transfer,
      completion,
      error);

  protocol_ok =
    goodix5135_d4_transaction_ack_complete (
      &self->tls_runtime_d4_transaction,
      transport_can_advance,
      transport_can_advance
        ? transfer->buffer
        : NULL,
      transport_can_advance
        ? (gsize) transfer->actual_length
        : 0);

  g_clear_error (&error);

  if (!protocol_ok ||
      self->tls_runtime_d4_transaction.state !=
        GOODIX5135_D4_TRANSACTION_DONE)
    {
      goodix5135_tls_runtime_fail (
        device,
        transport_can_advance
          ? "TLS-runtime D4 returned invalid ACK"
          : "TLS-runtime D4 ACK transport failed");

      return;
    }

  session_ok =
    goodix5135_tls_session_d4_ack (
      self->tls_session,
      &action,
      &error);

  g_clear_error (&error);

  if (!session_ok ||
      action !=
        GOODIX5135_TLS_SESSION_ACTION_READY ||
      !goodix5135_tls_session_is_ready (
        self->tls_session))
    {
      goodix5135_tls_runtime_fail (
        device,
        "TLS session did not enter READY after D4");

      return;
    }

  fp_dbg (
    "Goodix native TLS runtime bridge reached READY");

  if (self->tls_runtime_open_gate &&
      goodix5135_live_capture_test_requested ())
    {
      if (!goodix5135_capture_runtime_start (
            device))
        {
          goodix5135_capture_runtime_fail (
            device,
            "could not start one-shot native capture runtime");
        }

      return;
    }

  if (self->tls_runtime_open_gate)
    {
      self->tls_runtime_open_gate =
        FALSE;

      goodix5135_io_stop (
        &self->io);

      g_assert (
        goodix5135_io_can_finish_stop (
          &self->io));

      fp_dbg (
        "One-shot native TLS OPEN gate completed");

      fpi_image_device_open_complete (
        FP_IMAGE_DEVICE (device),
        NULL);
    }
}


static void
goodix5135_activation_d4_out_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  gboolean transport_can_advance;

  (void) user_data;

  transport_can_advance =
    goodix5135_async_result_can_advance (
      completion,
      error);

  if (transport_can_advance &&
      (transfer == NULL ||
       transfer->actual_length !=
         GOODIX5135_USB_PACKET_LENGTH))
    transport_can_advance = FALSE;

  g_clear_error (&error);

  if (!goodix5135_d4_transaction_out_complete (
        &self->d4_transaction,
        transport_can_advance))
    {
      goodix5135_activation_sequence_d4_complete (
        &self->activation_sequence,
        FALSE);

      goodix5135_open_transaction_fail (
        device,
        "Goodix 0xD4 OUT transport failed");
      return;
    }

  if (!goodix5135_async_submit (
        device,
        &self->io,
        GOODIX5135_REQUEST_BULK_IN,
        GOODIX5135_USB_PACKET_LENGTH,
        GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
        NULL,
        0,
        goodix5135_activation_d4_ack_cb,
        NULL))
    {
      goodix5135_d4_transaction_ack_complete (
        &self->d4_transaction,
        FALSE,
        NULL,
        0);

      goodix5135_activation_sequence_d4_complete (
        &self->activation_sequence,
        FALSE);

      goodix5135_open_transaction_fail (
        device,
        "Could not submit Goodix 0xD4 ACK receive");
    }
}


static void
goodix5135_activation_d4_ack_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  gboolean transport_can_advance;
  gboolean protocol_ok;

  (void) user_data;

  transport_can_advance =
    goodix5135_in_transfer_can_parse (
      transfer,
      completion,
      error);

  protocol_ok =
    goodix5135_d4_transaction_ack_complete (
      &self->d4_transaction,
      transport_can_advance,
      transport_can_advance
        ? transfer->buffer
        : NULL,
      transport_can_advance
        ? (gsize) transfer->actual_length
        : 0);

  g_clear_error (&error);

  if (!protocol_ok ||
      self->d4_transaction.state !=
        GOODIX5135_D4_TRANSACTION_DONE)
    {
      goodix5135_activation_sequence_d4_complete (
        &self->activation_sequence,
        FALSE);

      goodix5135_open_transaction_fail (
        device,
        transport_can_advance
          ? "Goodix 0xD4 returned invalid ACK"
          : "Goodix 0xD4 ACK transport failed");
      return;
    }

  if (!goodix5135_activation_sequence_d4_complete (
        &self->activation_sequence,
        TRUE))
    {
      goodix5135_open_transaction_fail (
        device,
        "Goodix 0xD4 completed out of activation order");
      return;
    }

  if (self->activation_sequence.state !=
      GOODIX5135_ACTIVATION_WAIT_NOP2)
    {
      goodix5135_open_transaction_fail (
        device,
        "Goodix activation did not enter NOP2");
      return;
    }

  fp_dbg (
    "0xD4 ACK validated; starting activation NOP #2");

  goodix5135_start_nop_transaction (
    device);
}


static void
goodix5135_start_activation_d4 (
  FpDevice *device)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  if (self->activation_sequence.state !=
      GOODIX5135_ACTIVATION_WAIT_D4)
    {
      goodix5135_open_transaction_fail (
        device,
        "Attempted Goodix 0xD4 outside activation D4 gate");
      return;
    }

  goodix5135_d4_transaction_init (
    &self->d4_transaction);

  if (!goodix5135_d4_transaction_begin (
        &self->d4_transaction,
        packet,
        sizeof (packet),
        &logical_length))
    {
      goodix5135_activation_sequence_d4_complete (
        &self->activation_sequence,
        FALSE);

      goodix5135_open_transaction_fail (
        device,
        "Could not build Goodix 0xD4 request");
      return;
    }

  if (logical_length !=
      GOODIX5135_D4_REQUEST_LENGTH)
    {
      goodix5135_d4_transaction_out_complete (
        &self->d4_transaction,
        FALSE);

      goodix5135_activation_sequence_d4_complete (
        &self->activation_sequence,
        FALSE);

      goodix5135_open_transaction_fail (
        device,
        "Unexpected Goodix 0xD4 request length");
      return;
    }

  fp_dbg ("Submitting guarded Goodix 0xD4 signal");

  if (!goodix5135_async_submit (
        device,
        &self->io,
        GOODIX5135_REQUEST_BULK_OUT,
        GOODIX5135_USB_PACKET_LENGTH,
        GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
        packet,
        sizeof (packet),
        goodix5135_activation_d4_out_cb,
        NULL))
    {
      goodix5135_d4_transaction_out_complete (
        &self->d4_transaction,
        FALSE);

      goodix5135_activation_sequence_d4_complete (
        &self->activation_sequence,
        FALSE);

      goodix5135_open_transaction_fail (
        device,
        "Could not submit Goodix 0xD4 request");
    }
}


static void
goodix5135_activation_enable_out_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  gboolean transport_can_advance;

  (void) user_data;

  transport_can_advance =
    goodix5135_async_result_can_advance (
      completion,
      error);

  if (transport_can_advance &&
      (transfer == NULL ||
       transfer->actual_length !=
         GOODIX5135_USB_PACKET_LENGTH))
    transport_can_advance = FALSE;

  g_clear_error (&error);

  if (!goodix5135_enable_chip_transaction_out_complete (
        &self->enable_chip_transaction,
        transport_can_advance))
    {
      goodix5135_activation_sequence_enable_chip_complete (
        &self->activation_sequence,
        FALSE);

      goodix5135_open_transaction_fail (
        device,
        "Goodix ENABLE_CHIP OUT transport failed");
      return;
    }

  if (!goodix5135_async_submit (
        device,
        &self->io,
        GOODIX5135_REQUEST_BULK_IN,
        GOODIX5135_USB_PACKET_LENGTH,
        GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
        NULL,
        0,
        goodix5135_activation_enable_ack_cb,
        NULL))
    {
      goodix5135_enable_chip_transaction_ack_complete (
        &self->enable_chip_transaction,
        FALSE,
        NULL,
        0);

      goodix5135_activation_sequence_enable_chip_complete (
        &self->activation_sequence,
        FALSE);

      goodix5135_open_transaction_fail (
        device,
        "Could not submit Goodix ENABLE_CHIP ACK receive");
    }
}


static void
goodix5135_activation_enable_ack_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  gboolean transport_can_advance;
  gboolean protocol_ok;

  (void) user_data;

  transport_can_advance =
    goodix5135_in_transfer_can_parse (
      transfer,
      completion,
      error);

  protocol_ok =
    goodix5135_enable_chip_transaction_ack_complete (
      &self->enable_chip_transaction,
      transport_can_advance,
      transport_can_advance
        ? transfer->buffer
        : NULL,
      transport_can_advance
        ? (gsize) transfer->actual_length
        : 0);

  g_clear_error (&error);

  if (!protocol_ok ||
      self->enable_chip_transaction.state !=
        GOODIX5135_ENABLE_CHIP_TRANSACTION_DONE)
    {
      goodix5135_activation_sequence_enable_chip_complete (
        &self->activation_sequence,
        FALSE);

      goodix5135_open_transaction_fail (
        device,
        transport_can_advance
          ? "Goodix ENABLE_CHIP returned invalid ACK"
          : "Goodix ENABLE_CHIP ACK transport failed");
      return;
    }

  if (!goodix5135_activation_sequence_enable_chip_complete (
        &self->activation_sequence,
        TRUE))
    {
      goodix5135_open_transaction_fail (
        device,
        "Goodix ENABLE_CHIP completed out of activation order");
      return;
    }

  if (self->activation_sequence.state !=
      GOODIX5135_ACTIVATION_WAIT_NOP3)
    {
      goodix5135_open_transaction_fail (
        device,
        "Goodix activation did not enter NOP3");
      return;
    }

  fp_dbg (
    "ENABLE_CHIP true ACK validated; starting activation NOP #3");

  goodix5135_start_nop_transaction (
    device);
}


static void
goodix5135_start_activation_enable (
  FpDevice *device)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  if (self->activation_sequence.state !=
      GOODIX5135_ACTIVATION_WAIT_ENABLE_CHIP)
    {
      goodix5135_open_transaction_fail (
        device,
        "Attempted ENABLE_CHIP outside activation gate");
      return;
    }

  goodix5135_enable_chip_transaction_init (
    &self->enable_chip_transaction);

  if (!goodix5135_enable_chip_transaction_begin (
        &self->enable_chip_transaction,
        TRUE,
        packet,
        sizeof (packet),
        &logical_length))
    {
      goodix5135_activation_sequence_enable_chip_complete (
        &self->activation_sequence,
        FALSE);

      goodix5135_open_transaction_fail (
        device,
        "Could not build Goodix ENABLE_CHIP true request");
      return;
    }

  if (logical_length !=
      GOODIX5135_ENABLE_CHIP_REQUEST_LENGTH)
    {
      goodix5135_enable_chip_transaction_out_complete (
        &self->enable_chip_transaction,
        FALSE);

      goodix5135_activation_sequence_enable_chip_complete (
        &self->activation_sequence,
        FALSE);

      goodix5135_open_transaction_fail (
        device,
        "Unexpected Goodix ENABLE_CHIP request length");
      return;
    }

  fp_dbg ("Submitting guarded Goodix ENABLE_CHIP true");

  if (!goodix5135_async_submit (
        device,
        &self->io,
        GOODIX5135_REQUEST_BULK_OUT,
        GOODIX5135_USB_PACKET_LENGTH,
        GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
        packet,
        sizeof (packet),
        goodix5135_activation_enable_out_cb,
        NULL))
    {
      goodix5135_enable_chip_transaction_out_complete (
        &self->enable_chip_transaction,
        FALSE);

      goodix5135_activation_sequence_enable_chip_complete (
        &self->activation_sequence,
        FALSE);

      goodix5135_open_transaction_fail (
        device,
        "Could not submit Goodix ENABLE_CHIP request");
    }
}


static void
goodix5135_activation_firmware_out_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  gboolean transport_can_advance;

  (void) user_data;

  transport_can_advance =
    goodix5135_async_result_can_advance (
      completion,
      error);

  if (transport_can_advance &&
      (transfer == NULL ||
       transfer->actual_length !=
         GOODIX5135_USB_PACKET_LENGTH))
    transport_can_advance = FALSE;

  g_clear_error (&error);

  if (!goodix5135_firmware_transaction_out_complete (
        &self->firmware_transaction,
        transport_can_advance))
    {
      goodix5135_activation_sequence_firmware_complete (
        &self->activation_sequence,
        FALSE);

      goodix5135_open_transaction_fail (
        device,
        "Activation firmware request transport failed");
      return;
    }

  if (!goodix5135_async_submit (
        device,
        &self->io,
        GOODIX5135_REQUEST_BULK_IN,
        GOODIX5135_USB_PACKET_LENGTH,
        GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
        NULL,
        0,
        goodix5135_activation_firmware_ack_cb,
        NULL))
    {
      goodix5135_firmware_transaction_ack_complete (
        &self->firmware_transaction,
        FALSE,
        NULL,
        0);

      goodix5135_activation_sequence_firmware_complete (
        &self->activation_sequence,
        FALSE);

      goodix5135_open_transaction_fail (
        device,
        "Could not submit activation firmware ACK receive");
    }
}


static void
goodix5135_activation_firmware_ack_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  gboolean transport_can_advance;
  gboolean protocol_ok;

  (void) user_data;

  transport_can_advance =
    goodix5135_in_transfer_can_parse (
      transfer,
      completion,
      error);

  protocol_ok =
    goodix5135_firmware_transaction_ack_complete (
      &self->firmware_transaction,
      transport_can_advance,
      transport_can_advance
        ? transfer->buffer
        : NULL,
      transport_can_advance
        ? (gsize) transfer->actual_length
        : 0);

  g_clear_error (&error);

  if (!protocol_ok)
    {
      goodix5135_activation_sequence_firmware_complete (
        &self->activation_sequence,
        FALSE);

      goodix5135_open_transaction_fail (
        device,
        transport_can_advance
          ? "Activation firmware returned invalid ACK"
          : "Activation firmware ACK transport failed");
      return;
    }

  if (!goodix5135_async_submit (
        device,
        &self->io,
        GOODIX5135_REQUEST_BULK_IN,
        GOODIX5135_USB_PACKET_LENGTH,
        GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
        NULL,
        0,
        goodix5135_activation_firmware_response_cb,
        NULL))
    {
      const guint8 *firmware = NULL;
      gsize firmware_length = 0;

      goodix5135_firmware_transaction_response_complete (
        &self->firmware_transaction,
        FALSE,
        NULL,
        0,
        &firmware,
        &firmware_length);

      goodix5135_activation_sequence_firmware_complete (
        &self->activation_sequence,
        FALSE);

      goodix5135_open_transaction_fail (
        device,
        "Could not submit activation firmware response receive");
    }
}


static void
goodix5135_activation_firmware_response_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  gboolean transport_can_advance;
  gboolean protocol_ok;

  const guint8 *firmware = NULL;
  gsize firmware_length = 0;

  (void) user_data;

  transport_can_advance =
    goodix5135_in_transfer_can_parse (
      transfer,
      completion,
      error);

  protocol_ok =
    goodix5135_firmware_transaction_response_complete (
      &self->firmware_transaction,
      transport_can_advance,
      transport_can_advance
        ? transfer->buffer
        : NULL,
      transport_can_advance
        ? (gsize) transfer->actual_length
        : 0,
      &firmware,
      &firmware_length);

  g_clear_error (&error);

  /*
   * Borrowed response view is intentionally not logged or copied.
   */
  firmware = NULL;
  firmware_length = 0;

  if (!protocol_ok ||
      self->firmware_transaction.state !=
        GOODIX5135_FIRMWARE_TRANSACTION_DONE)
    {
      goodix5135_activation_sequence_firmware_complete (
        &self->activation_sequence,
        FALSE);

      goodix5135_open_transaction_fail (
        device,
        transport_can_advance
          ? "Activation firmware response was invalid"
          : "Activation firmware response transport failed");
      return;
    }

  if (!goodix5135_activation_sequence_firmware_complete (
        &self->activation_sequence,
        TRUE))
    {
      goodix5135_open_transaction_fail (
        device,
        "Activation firmware completed out of order");
      return;
    }

  if (self->activation_sequence.state !=
      GOODIX5135_ACTIVATION_WAIT_RESET)
    {
      goodix5135_open_transaction_fail (
        device,
        "Goodix activation did not enter RESET gate");
      return;
    }

  fp_dbg (
    "Activation firmware validated; starting bounded sensor reset");

  goodix5135_start_activation_reset (
    device);
}


static void
goodix5135_start_activation_firmware (
  FpDevice *device)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  if (self->activation_sequence.state !=
      GOODIX5135_ACTIVATION_WAIT_FIRMWARE)
    {
      goodix5135_open_transaction_fail (
        device,
        "Attempted activation firmware outside firmware gate");
      return;
    }

  goodix5135_firmware_transaction_init (
    &self->firmware_transaction);

  if (!goodix5135_firmware_transaction_begin (
        &self->firmware_transaction,
        packet,
        sizeof (packet),
        &logical_length))
    {
      goodix5135_activation_sequence_firmware_complete (
        &self->activation_sequence,
        FALSE);

      goodix5135_open_transaction_fail (
        device,
        "Could not build activation firmware request");
      return;
    }

  if (logical_length !=
      GOODIX5135_FIRMWARE_REQUEST_LENGTH)
    {
      goodix5135_firmware_transaction_out_complete (
        &self->firmware_transaction,
        FALSE);

      goodix5135_activation_sequence_firmware_complete (
        &self->activation_sequence,
        FALSE);

      goodix5135_open_transaction_fail (
        device,
        "Unexpected activation firmware request length");
      return;
    }

  fp_dbg (
    "Submitting activation firmware-version gate");

  if (!goodix5135_async_submit (
        device,
        &self->io,
        GOODIX5135_REQUEST_BULK_OUT,
        GOODIX5135_USB_PACKET_LENGTH,
        GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
        packet,
        sizeof (packet),
        goodix5135_activation_firmware_out_cb,
        NULL))
    {
      goodix5135_firmware_transaction_out_complete (
        &self->firmware_transaction,
        FALSE);

      goodix5135_activation_sequence_firmware_complete (
        &self->activation_sequence,
        FALSE);

      goodix5135_open_transaction_fail (
        device,
        "Could not submit activation firmware request");
    }
}


static void
goodix5135_activation_reset_out_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  gboolean transport_can_advance;

  (void) user_data;

  transport_can_advance =
    goodix5135_async_result_can_advance (
      completion,
      error);

  if (transport_can_advance &&
      (transfer == NULL ||
       transfer->actual_length !=
         GOODIX5135_USB_PACKET_LENGTH))
    transport_can_advance = FALSE;

  g_clear_error (&error);

  if (!goodix5135_sensor_reset_transaction_out_complete (
        &self->sensor_reset_transaction,
        transport_can_advance))
    {
      goodix5135_activation_sequence_reset_complete (
        &self->activation_sequence,
        FALSE);

      goodix5135_open_transaction_fail (
        device,
        "Goodix sensor reset OUT transport failed");
      return;
    }

  if (!goodix5135_async_submit (
        device,
        &self->io,
        GOODIX5135_REQUEST_BULK_IN,
        GOODIX5135_USB_PACKET_LENGTH,
        GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
        NULL,
        0,
        goodix5135_activation_reset_ack_cb,
        NULL))
    {
      goodix5135_sensor_reset_transaction_ack_complete (
        &self->sensor_reset_transaction,
        FALSE,
        NULL,
        0);

      goodix5135_activation_sequence_reset_complete (
        &self->activation_sequence,
        FALSE);

      goodix5135_open_transaction_fail (
        device,
        "Could not submit Goodix sensor-reset ACK receive");
    }
}


static void
goodix5135_activation_reset_ack_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  gboolean transport_can_advance;
  gboolean protocol_ok;

  (void) user_data;

  transport_can_advance =
    goodix5135_in_transfer_can_parse (
      transfer,
      completion,
      error);

  protocol_ok =
    goodix5135_sensor_reset_transaction_ack_complete (
      &self->sensor_reset_transaction,
      transport_can_advance,
      transport_can_advance
        ? transfer->buffer
        : NULL,
      transport_can_advance
        ? (gsize) transfer->actual_length
        : 0);

  g_clear_error (&error);

  if (!protocol_ok)
    {
      goodix5135_activation_sequence_reset_complete (
        &self->activation_sequence,
        FALSE);

      goodix5135_open_transaction_fail (
        device,
        transport_can_advance
          ? "Goodix sensor reset returned invalid ACK"
          : "Goodix sensor reset ACK transport failed");
      return;
    }

  if (!goodix5135_async_submit (
        device,
        &self->io,
        GOODIX5135_REQUEST_BULK_IN,
        GOODIX5135_USB_PACKET_LENGTH,
        GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
        NULL,
        0,
        goodix5135_activation_reset_response_cb,
        NULL))
    {
      guint16 reset_result = 0;

      goodix5135_sensor_reset_transaction_response_complete (
        &self->sensor_reset_transaction,
        FALSE,
        NULL,
        0,
        &reset_result);

      goodix5135_activation_sequence_reset_complete (
        &self->activation_sequence,
        FALSE);

      goodix5135_open_transaction_fail (
        device,
        "Could not submit Goodix sensor-reset response receive");
    }
}


static void
goodix5135_activation_reset_response_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  gboolean transport_can_advance;
  gboolean protocol_ok;

  guint16 reset_result = 0;

  (void) user_data;

  transport_can_advance =
    goodix5135_in_transfer_can_parse (
      transfer,
      completion,
      error);

  protocol_ok =
    goodix5135_sensor_reset_transaction_response_complete (
      &self->sensor_reset_transaction,
      transport_can_advance,
      transport_can_advance
        ? transfer->buffer
        : NULL,
      transport_can_advance
        ? (gsize) transfer->actual_length
        : 0,
      &reset_result);

  g_clear_error (&error);

  /*
   * reset_result is validated by the protocol layer but is not
   * persisted or logged.
   */
  (void) reset_result;

  if (!protocol_ok ||
      self->sensor_reset_transaction.state !=
        GOODIX5135_SENSOR_RESET_TRANSACTION_DONE)
    {
      goodix5135_activation_sequence_reset_complete (
        &self->activation_sequence,
        FALSE);

      goodix5135_open_transaction_fail (
        device,
        transport_can_advance
          ? "Goodix sensor reset response was invalid"
          : "Goodix sensor reset response transport failed");
      return;
    }

  if (!goodix5135_activation_sequence_reset_complete (
        &self->activation_sequence,
        TRUE))
    {
      goodix5135_open_transaction_fail (
        device,
        "Goodix sensor reset completed out of activation order");
      return;
    }

  if (self->activation_sequence.state !=
      GOODIX5135_ACTIVATION_WAIT_CHIP_ID)
    {
      goodix5135_open_transaction_fail (
        device,
        "Goodix activation did not enter chip-ID gate");
      return;
    }

  fp_dbg (
    "Sensor reset validated; starting read-only chip-ID gate");

  goodix5135_start_activation_chip_id_read (
    device);
}


static void
goodix5135_start_activation_reset (
  FpDevice *device)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  if (self->activation_sequence.state !=
      GOODIX5135_ACTIVATION_WAIT_RESET)
    {
      goodix5135_open_transaction_fail (
        device,
        "Attempted sensor reset outside activation RESET gate");
      return;
    }

  goodix5135_sensor_reset_transaction_init (
    &self->sensor_reset_transaction);

  if (!goodix5135_sensor_reset_transaction_begin (
        &self->sensor_reset_transaction,
        packet,
        sizeof (packet),
        &logical_length))
    {
      goodix5135_activation_sequence_reset_complete (
        &self->activation_sequence,
        FALSE);

      goodix5135_open_transaction_fail (
        device,
        "Could not build bounded Goodix sensor-reset request");
      return;
    }

  if (logical_length !=
      GOODIX5135_SENSOR_RESET_REQUEST_LENGTH)
    {
      goodix5135_sensor_reset_transaction_out_complete (
        &self->sensor_reset_transaction,
        FALSE);

      goodix5135_activation_sequence_reset_complete (
        &self->activation_sequence,
        FALSE);

      goodix5135_open_transaction_fail (
        device,
        "Unexpected Goodix sensor-reset request length");
      return;
    }

  fp_dbg (
    "Submitting bounded Goodix reset(True, False, 20)");

  if (!goodix5135_async_submit (
        device,
        &self->io,
        GOODIX5135_REQUEST_BULK_OUT,
        GOODIX5135_USB_PACKET_LENGTH,
        GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
        packet,
        sizeof (packet),
        goodix5135_activation_reset_out_cb,
        NULL))
    {
      goodix5135_sensor_reset_transaction_out_complete (
        &self->sensor_reset_transaction,
        FALSE);

      goodix5135_activation_sequence_reset_complete (
        &self->activation_sequence,
        FALSE);

      goodix5135_open_transaction_fail (
        device,
        "Could not submit Goodix sensor-reset request");
    }
}


static void
goodix5135_activation_chip_id_out_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  gboolean transport_can_advance;

  (void) user_data;

  transport_can_advance =
    goodix5135_async_result_can_advance (
      completion,
      error);

  if (transport_can_advance &&
      (transfer == NULL ||
       transfer->actual_length !=
         GOODIX5135_USB_PACKET_LENGTH))
    transport_can_advance = FALSE;

  g_clear_error (&error);

  if (!goodix5135_register_read_transaction_out_complete (
        &self->register_read_transaction,
        transport_can_advance))
    {
      goodix5135_activation_sequence_chip_id_complete (
        &self->activation_sequence,
        FALSE,
        NULL,
        0);

      goodix5135_open_transaction_fail (
        device,
        "Activation chip-ID read OUT transport failed");
      return;
    }

  if (!goodix5135_async_submit (
        device,
        &self->io,
        GOODIX5135_REQUEST_BULK_IN,
        GOODIX5135_USB_PACKET_LENGTH,
        GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
        NULL,
        0,
        goodix5135_activation_chip_id_ack_cb,
        NULL))
    {
      goodix5135_register_read_transaction_ack_complete (
        &self->register_read_transaction,
        FALSE,
        NULL,
        0);

      goodix5135_activation_sequence_chip_id_complete (
        &self->activation_sequence,
        FALSE,
        NULL,
        0);

      goodix5135_open_transaction_fail (
        device,
        "Could not submit activation chip-ID ACK receive");
    }
}


static void
goodix5135_activation_chip_id_ack_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  gboolean transport_can_advance;
  gboolean protocol_ok;

  (void) user_data;

  transport_can_advance =
    goodix5135_in_transfer_can_parse (
      transfer,
      completion,
      error);

  protocol_ok =
    goodix5135_register_read_transaction_ack_complete (
      &self->register_read_transaction,
      transport_can_advance,
      transport_can_advance
        ? transfer->buffer
        : NULL,
      transport_can_advance
        ? (gsize) transfer->actual_length
        : 0);

  g_clear_error (&error);

  if (!protocol_ok)
    {
      goodix5135_activation_sequence_chip_id_complete (
        &self->activation_sequence,
        FALSE,
        NULL,
        0);

      goodix5135_open_transaction_fail (
        device,
        transport_can_advance
          ? "Activation chip-ID read returned invalid ACK"
          : "Activation chip-ID ACK transport failed");
      return;
    }

  if (!goodix5135_async_submit (
        device,
        &self->io,
        GOODIX5135_REQUEST_BULK_IN,
        GOODIX5135_USB_PACKET_LENGTH,
        GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
        NULL,
        0,
        goodix5135_activation_chip_id_response_cb,
        NULL))
    {
      const guint8 *value = NULL;
      gsize value_length = 0;

      goodix5135_register_read_transaction_response_complete (
        &self->register_read_transaction,
        FALSE,
        NULL,
        0,
        &value,
        &value_length);

      goodix5135_activation_sequence_chip_id_complete (
        &self->activation_sequence,
        FALSE,
        NULL,
        0);

      goodix5135_open_transaction_fail (
        device,
        "Could not submit activation chip-ID response receive");
    }
}


static void
goodix5135_activation_chip_id_response_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  gboolean transport_can_advance;
  gboolean protocol_ok;

  const guint8 *value = NULL;
  gsize value_length = 0;

  (void) user_data;

  transport_can_advance =
    goodix5135_in_transfer_can_parse (
      transfer,
      completion,
      error);

  protocol_ok =
    goodix5135_register_read_transaction_response_complete (
      &self->register_read_transaction,
      transport_can_advance,
      transport_can_advance
        ? transfer->buffer
        : NULL,
      transport_can_advance
        ? (gsize) transfer->actual_length
        : 0,
      &value,
      &value_length);

  g_clear_error (&error);

  if (!protocol_ok ||
      self->register_read_transaction.state !=
        GOODIX5135_REGISTER_READ_TRANSACTION_DONE)
    {
      goodix5135_activation_sequence_chip_id_complete (
        &self->activation_sequence,
        FALSE,
        NULL,
        0);

      goodix5135_open_transaction_fail (
        device,
        transport_can_advance
          ? "Activation chip-ID response was invalid"
          : "Activation chip-ID response transport failed");
      return;
    }

  if (value == NULL ||
      value_length !=
        GOODIX5135_ACTIVATION_CHIP_ID_LENGTH)
    {
      goodix5135_activation_sequence_chip_id_complete (
        &self->activation_sequence,
        FALSE,
        NULL,
        0);

      goodix5135_open_transaction_fail (
        device,
        "Activation chip-ID response length mismatch");
      return;
    }

  /*
   * The controller compares the borrowed 4-byte logical identity
   * against a2 04 25 00.
   *
   * The bytes are not logged, copied, persisted, or exported.
   */
  if (!goodix5135_activation_sequence_chip_id_complete (
        &self->activation_sequence,
        TRUE,
        value,
        value_length))
    {
      value = NULL;
      value_length = 0;

      goodix5135_open_transaction_fail (
        device,
        "Activation chip-ID did not match ChicagoHU 0x2504");
      return;
    }

  value = NULL;
  value_length = 0;

  if (self->activation_sequence.state !=
      GOODIX5135_ACTIVATION_DONE)
    {
      goodix5135_open_transaction_fail (
        device,
        "Goodix activation sequence did not reach DONE");
      return;
    }

  fp_dbg (
    "Guarded Goodix activation sequence validated; "
    "starting read-only OTP calibration gate");

  goodix5135_start_otp_read (
    device);
}


static void
goodix5135_secure_zero (
  gpointer data,
  gsize    length)
{
  volatile guint8 *p =
    (volatile guint8 *) data;

  if (p == NULL)
    return;

  while (length > 0)
    {
      *p++ = 0;
      length--;
    }
}


static void
goodix5135_otp_out_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  gboolean transport_can_advance;

  (void) user_data;

  transport_can_advance =
    goodix5135_async_result_can_advance (
      completion,
      error);

  if (transport_can_advance &&
      (transfer == NULL ||
       transfer->actual_length !=
         GOODIX5135_USB_PACKET_LENGTH))
    transport_can_advance = FALSE;

  g_clear_error (&error);

  if (!goodix5135_otp_read_transaction_out_complete (
        &self->otp_read_transaction,
        transport_can_advance))
    {
      goodix5135_open_transaction_fail (
        device,
        "Goodix READ_OTP OUT transport failed");
      return;
    }

  if (!goodix5135_async_submit (
        device,
        &self->io,
        GOODIX5135_REQUEST_BULK_IN,
        GOODIX5135_USB_PACKET_LENGTH,
        GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
        NULL,
        0,
        goodix5135_otp_ack_cb,
        NULL))
    {
      goodix5135_otp_read_transaction_ack_complete (
        &self->otp_read_transaction,
        FALSE,
        NULL,
        0);

      goodix5135_open_transaction_fail (
        device,
        "Could not submit Goodix READ_OTP ACK receive");
    }
}


static void
goodix5135_otp_ack_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  gboolean transport_can_advance;
  gboolean protocol_ok;

  (void) user_data;

  transport_can_advance =
    goodix5135_in_transfer_can_parse (
      transfer,
      completion,
      error);

  protocol_ok =
    goodix5135_otp_read_transaction_ack_complete (
      &self->otp_read_transaction,
      transport_can_advance,
      transport_can_advance
        ? transfer->buffer
        : NULL,
      transport_can_advance
        ? (gsize) transfer->actual_length
        : 0);

  g_clear_error (&error);

  if (!protocol_ok ||
      self->otp_read_transaction.state !=
        GOODIX5135_OTP_READ_TRANSACTION_WAIT_RESPONSE)
    {
      goodix5135_open_transaction_fail (
        device,
        transport_can_advance
          ? "Goodix READ_OTP returned invalid ACK"
          : "Goodix READ_OTP ACK transport failed");
      return;
    }

  if (!goodix5135_async_submit (
        device,
        &self->io,
        GOODIX5135_REQUEST_BULK_IN,
        GOODIX5135_OTP_READ_RESPONSE_LENGTH,
        GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
        NULL,
        0,
        goodix5135_otp_response_cb,
        NULL))
    {
      guint8 otp[GOODIX5135_OTP_LENGTH] = { 0 };

      goodix5135_otp_read_transaction_response_complete (
        &self->otp_read_transaction,
        FALSE,
        NULL,
        0,
        otp,
        sizeof (otp));

      goodix5135_secure_zero (
        otp,
        sizeof (otp));

      goodix5135_open_transaction_fail (
        device,
        "Could not submit Goodix READ_OTP response receive");
    }
}


static void
goodix5135_otp_response_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  FpImageDevice *dev =
    FP_IMAGE_DEVICE (device);

  gboolean transport_can_advance;
  gboolean protocol_ok;
  gboolean calibration_ok = FALSE;

  guint8 otp[GOODIX5135_OTP_LENGTH] = { 0 };
  Goodix5135OtpCalibration calibration = { 0 };

  (void) user_data;

  transport_can_advance =
    goodix5135_in_transfer_can_parse (
      transfer,
      completion,
      error);

  if (transport_can_advance &&
      (transfer == NULL ||
       transfer->actual_length !=
         GOODIX5135_OTP_READ_RESPONSE_LENGTH))
    transport_can_advance = FALSE;

  protocol_ok =
    goodix5135_otp_read_transaction_response_complete (
      &self->otp_read_transaction,
      transport_can_advance,
      transport_can_advance
        ? transfer->buffer
        : NULL,
      transport_can_advance
        ? (gsize) transfer->actual_length
        : 0,
      otp,
      sizeof (otp));

  g_clear_error (&error);

  if (protocol_ok &&
      self->otp_read_transaction.state ==
        GOODIX5135_OTP_READ_TRANSACTION_DONE)
    {
      calibration_ok =
        goodix5135_parse_otp_calibration (
          otp,
          sizeof (otp),
          &calibration);
    }

  /*
   * Raw OTP must not survive this callback.
   */
  goodix5135_secure_zero (
    otp,
    sizeof (otp));

  if (transfer != NULL &&
      transfer->buffer != NULL &&
      transfer->actual_length > 0)
    {
      goodix5135_secure_zero (
        transfer->buffer,
        (gsize) transfer->actual_length);
    }

  if (!protocol_ok ||
      self->otp_read_transaction.state !=
        GOODIX5135_OTP_READ_TRANSACTION_DONE)
    {
      goodix5135_secure_zero (
        &calibration,
        sizeof (calibration));

      self->otp_calibration_valid =
        FALSE;

      goodix5135_secure_zero (
        &self->otp_calibration,
        sizeof (self->otp_calibration));

      goodix5135_open_transaction_fail (
        device,
        transport_can_advance
          ? "Goodix READ_OTP response failed protocol validation"
          : "Goodix READ_OTP response transport/length failed");
      return;
    }

  if (!calibration_ok)
    {
      goodix5135_secure_zero (
        &calibration,
        sizeof (calibration));

      self->otp_calibration_valid =
        FALSE;

      goodix5135_secure_zero (
        &self->otp_calibration,
        sizeof (self->otp_calibration));

      goodix5135_open_transaction_fail (
        device,
        "Goodix OTP calibration validation failed");
      return;
    }

  /*
   * Retain derived calibration only.
   */
  self->otp_calibration =
    calibration;

  self->otp_calibration_valid =
    TRUE;

  /*
   * At this point the only retained device-specific material is
   * the already-validated derived calibration.
   *
   * No CFG70 template has been supplied and no config upload has
   * been prepared or submitted.
   */
  self->config_calibration_ready =
    TRUE;

  goodix5135_secure_zero (
    &calibration,
    sizeof (calibration));

  fp_dbg (
    "Read-only OTP calibration gate validated; "
    "raw OTP discarded");

  /*
   * Normal OPEN remains read-only.  Only the exact research gate below
   * may continue into the one-shot CFG70 live transport.
   */
  if (goodix5135_live_config_test_requested ())
    {
      if (!goodix5135_start_live_config_upload_test (
            device))
        {
          goodix5135_open_transaction_fail (
            device,
            "One-shot Goodix CFG70 structural gate failed");
        }

      return;
    }

  /*
   * Explicit one-shot native TLS research gate.
   *
   * It is inactive unless the exact runtime environment gate is supplied.
   * The private PSK is read into volatile memory only and is immediately
   * cleared after the in-process TLS session copies it.
   */
  if (goodix5135_live_tls_test_requested ())
    {
      if (!goodix5135_start_live_tls_open_gate (
            device))
        {
          goodix5135_open_transaction_fail (
            device,
            "One-shot native TLS gate did not start");
        }

      return;
    }

  goodix5135_io_stop (
    &self->io);

  g_assert (
    goodix5135_io_can_finish_stop (
      &self->io));

  fpi_image_device_open_complete (
    dev,
    NULL);
}



static void
goodix5135_config_runtime_state_reset (
  FpiDeviceGoodix5135 *self)
{
  g_assert (self != NULL);

  /*
   * Volatile CFG70 material/framing must never survive a
   * reset/failure/close lifecycle boundary.
   */
  goodix5135_secure_zero (
    self->config_runtime,
    sizeof (self->config_runtime));

  goodix5135_secure_zero (
    self->config_transfer,
    sizeof (self->config_transfer));

  self->config_logical_length =
    0;

  self->config_transport_length =
    0;

  self->config_prepared =
    FALSE;

  self->config_transport_armed =
    FALSE;

  goodix5135_config_upload_transaction_init (
    &self->config_upload_transaction);

  self->config_calibration_ready =
    FALSE;
}

/*
 * Prepare volatile CFG70/config-upload state from an explicitly supplied
 * template.
 *
 * This helper deliberately owns no template source and performs no USB I/O.
 * It is dormant until an explicitly gated caller invokes it.
 *
 * Failure invalidates config-preparation readiness so a caller cannot retry
 * implicitly with stale state; a fresh validated OTP gate is required before
 * another attempt.
 */
static gboolean G_GNUC_UNUSED
goodix5135_prepare_runtime_config_from_template (
  FpiDeviceGoodix5135 *self,
  const guint8        *template_data,
  gsize                template_length)
{
  g_assert (self != NULL);

  if (template_data == NULL ||
      template_length != GOODIX5135_CONFIG_LENGTH ||
      !self->otp_calibration_valid ||
      !self->config_calibration_ready ||
      self->config_prepared ||
      self->config_upload_transaction.state !=
        GOODIX5135_CONFIG_UPLOAD_TRANSACTION_IDLE)
    return FALSE;

  /*
   * Start from a known zeroed volatile-output state while retaining the
   * already-validated OTP-derived calibration for this single preparation
   * attempt.
   */
  goodix5135_secure_zero (
    self->config_runtime,
    sizeof (self->config_runtime));

  goodix5135_secure_zero (
    self->config_transfer,
    sizeof (self->config_transfer));

  self->config_logical_length =
    0;

  self->config_transport_length =
    0;

  self->config_prepared =
    FALSE;

  if (!goodix5135_prepare_config_upload (
        template_data,
        template_length,
        &self->otp_calibration,
        self->config_runtime,
        sizeof (self->config_runtime),
        &self->config_upload_transaction,
        self->config_transfer,
        sizeof (self->config_transfer),
        &self->config_logical_length,
        &self->config_transport_length))
    goto fail;

  if (self->config_logical_length !=
        GOODIX5135_CONFIG_LOGICAL_LENGTH ||
      self->config_transport_length !=
        GOODIX5135_CONFIG_TRANSFER_LENGTH ||
      self->config_upload_transaction.state !=
        GOODIX5135_CONFIG_UPLOAD_TRANSACTION_WAIT_OUT ||
      self->config_upload_transaction.packets_completed != 0)
    goto fail;

  self->config_prepared =
    TRUE;

  /*
   * A successfully prepared CFG70 transfer is still dormant.
   * A later explicit gate must arm transport separately.
   */
  self->config_transport_armed =
    FALSE;

  return TRUE;

fail:
  /*
   * Preparation failure must not leave partial config/framing material.
   *
   * Do not use goodix5135_config_runtime_state_reset() here because that
   * helper also describes a full lifecycle reset.  Perform the precise
   * failure cleanup locally and require a fresh calibration gate.
   */
  goodix5135_secure_zero (
    self->config_runtime,
    sizeof (self->config_runtime));

  goodix5135_secure_zero (
    self->config_transfer,
    sizeof (self->config_transfer));

  self->config_logical_length =
    0;

  self->config_transport_length =
    0;

  self->config_prepared =
    FALSE;

  self->config_transport_armed =
    FALSE;

  goodix5135_config_upload_transaction_init (
    &self->config_upload_transaction);

  self->config_calibration_ready =
    FALSE;

  return FALSE;
}

/*
 * Explicitly arm a fully prepared CFG70 transfer for a later transport
 * implementation.
 *
 * This helper performs no USB I/O, does not expose configuration bytes,
 * and does not advance the config-upload transaction.
 */
static gboolean G_GNUC_UNUSED
goodix5135_arm_config_upload_transport (
  FpiDeviceGoodix5135 *self)
{
  g_assert (self != NULL);

  if (!self->otp_calibration_valid ||
      !self->config_calibration_ready ||
      !self->config_prepared ||
      self->config_transport_armed ||
      self->config_logical_length !=
        GOODIX5135_CONFIG_LOGICAL_LENGTH ||
      self->config_transport_length !=
        GOODIX5135_CONFIG_TRANSFER_LENGTH ||
      self->config_upload_transaction.state !=
        GOODIX5135_CONFIG_UPLOAD_TRANSACTION_WAIT_OUT ||
      self->config_upload_transaction.packets_completed != 0)
    return FALSE;

  self->config_transport_armed =
    TRUE;

  return TRUE;
}

/*
 * Stage exactly one 64-byte packet from the already prepared volatile
 * CFG70 transport buffer.
 *
 * The current packet is selected exclusively from the protocol
 * transaction's packets_completed counter.
 *
 * This helper performs no USB I/O and does not advance the transaction.
 * A later transport-completion path must advance protocol state only
 * after an actual successful USB completion.
 */
static gboolean G_GNUC_UNUSED
goodix5135_stage_current_config_packet (
  FpiDeviceGoodix5135 *self,
  guint8              *packet,
  gsize                packet_size)
{
  gsize packet_index;
  gsize packet_offset;

  g_assert (self != NULL);

  if (packet == NULL ||
      packet_size != GOODIX5135_USB_PACKET_LENGTH ||
      !self->otp_calibration_valid ||
      !self->config_calibration_ready ||
      !self->config_prepared ||
      !self->config_transport_armed ||
      self->config_logical_length !=
        GOODIX5135_CONFIG_LOGICAL_LENGTH ||
      self->config_transport_length !=
        GOODIX5135_CONFIG_TRANSFER_LENGTH ||
      self->config_upload_transaction.state !=
        GOODIX5135_CONFIG_UPLOAD_TRANSACTION_WAIT_OUT)
    return FALSE;

  packet_index =
    (gsize) self->config_upload_transaction.packets_completed;

  if (packet_index >=
      GOODIX5135_CONFIG_USB_PACKET_COUNT)
    return FALSE;

  packet_offset =
    packet_index * GOODIX5135_USB_PACKET_LENGTH;

  if (packet_offset >
        self->config_transport_length ||
      GOODIX5135_USB_PACKET_LENGTH >
        self->config_transport_length - packet_offset)
    return FALSE;

  memcpy (
    packet,
    self->config_transfer + packet_offset,
    GOODIX5135_USB_PACKET_LENGTH);

  return TRUE;
}

/*
 * Advance the prepared CFG70 transaction after exactly one staged
 * 64-byte OUT packet has completed.
 *
 * This helper performs no USB I/O.
 */
static gboolean G_GNUC_UNUSED
goodix5135_complete_config_out_packet (
  FpiDeviceGoodix5135 *self,
  gboolean             transport_ok)
{
  g_assert (self != NULL);

  if (!self->config_prepared ||
      !self->config_transport_armed ||
      self->config_upload_transaction.state !=
        GOODIX5135_CONFIG_UPLOAD_TRANSACTION_WAIT_OUT)
    return FALSE;

  return
    goodix5135_config_upload_transaction_out_complete (
      &self->config_upload_transaction,
      transport_ok);
}


/*
 * Advance the CFG70 transaction after the command acknowledgement has
 * been received.
 *
 * The response bytes are parsed by the protocol layer.  This helper
 * performs no USB I/O.
 */
static gboolean G_GNUC_UNUSED
goodix5135_complete_config_ack (
  FpiDeviceGoodix5135 *self,
  gboolean             transport_ok,
  const guint8        *response,
  gsize                response_length)
{
  g_assert (self != NULL);

  if (!self->config_prepared ||
      !self->config_transport_armed ||
      self->config_upload_transaction.state !=
        GOODIX5135_CONFIG_UPLOAD_TRANSACTION_WAIT_ACK)
    return FALSE;

  return
    goodix5135_config_upload_transaction_ack_complete (
      &self->config_upload_transaction,
      transport_ok,
      response,
      response_length);
}


/*
 * Complete the CFG70 protocol transaction after the final response.
 *
 * This helper only advances protocol state.  Lifecycle cleanup and
 * open completion remain the responsibility of the future live
 * transport caller.
 *
 * This helper performs no USB I/O.
 */
static gboolean G_GNUC_UNUSED
goodix5135_complete_config_response (
  FpiDeviceGoodix5135 *self,
  gboolean             transport_ok,
  const guint8        *response,
  gsize                response_length)
{
  gboolean ok;

  g_assert (self != NULL);

  if (!self->config_prepared ||
      !self->config_transport_armed ||
      self->config_upload_transaction.state !=
        GOODIX5135_CONFIG_UPLOAD_TRANSACTION_WAIT_RESPONSE)
    return FALSE;

  ok =
    goodix5135_config_upload_transaction_response_complete (
      &self->config_upload_transaction,
      transport_ok,
      response,
      response_length);

  if (!ok)
    return FALSE;

  return
    self->config_upload_transaction.state ==
      GOODIX5135_CONFIG_UPLOAD_TRANSACTION_DONE;
}

/*
 * Research-only, one-shot live CFG70 gate.
 *
 * The normal OPEN path remains unchanged unless the exact environment
 * gate is explicitly supplied by the controlled test harness.
 *
 * No private template path or private bytes are compiled into the
 * driver.
 */
static gboolean goodix5135_live_cfg70_consumed = FALSE;


static void
goodix5135_live_config_out_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void
goodix5135_live_config_ack_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

static void
goodix5135_live_config_response_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);


static gboolean
goodix5135_live_config_test_requested (void)
{
  return
    g_strcmp0 (
      g_getenv ("GOODIX5135_LIVE_CFG70_TEST"),
      "VALIDATED_OTP_CFG70_ONE_SHOT") == 0;
}


/*
 * Require the exact byte-position delta already proven for this unit
 * against the private Windows runtime CFG70 during the earlier
 * research gate.
 *
 * Values themselves are never logged or exposed.
 */
static gboolean
goodix5135_live_runtime_shape_valid (
  FpiDeviceGoodix5135 *self,
  const guint8        *template_data,
  gsize                template_length)
{
  static const guint expected_offsets[] =
    {
      0x73,
      0x77,
      0x78,
      0x7b,
      0x7f,
      0x83,
      0xb0,
      0xde,
      0xdf,
    };

  guint differences[GOODIX5135_CONFIG_LENGTH] = { 0 };
  gsize difference_count = 0;

  g_assert (self != NULL);

  if (template_data == NULL ||
      template_length != GOODIX5135_CONFIG_LENGTH ||
      !self->config_prepared ||
      self->config_logical_length !=
        GOODIX5135_CONFIG_LOGICAL_LENGTH ||
      self->config_transport_length !=
        GOODIX5135_CONFIG_TRANSFER_LENGTH ||
      self->config_upload_transaction.state !=
        GOODIX5135_CONFIG_UPLOAD_TRANSACTION_WAIT_OUT ||
      self->config_upload_transaction.packets_completed != 0)
    return FALSE;

  for (gsize i = 0;
       i < GOODIX5135_CONFIG_LENGTH;
       i++)
    {
      if (self->config_runtime[i] !=
          template_data[i])
        {
          if (difference_count >=
              G_N_ELEMENTS (expected_offsets))
            return FALSE;

          differences[difference_count++] =
            (guint) i;
        }
    }

  if (difference_count !=
      G_N_ELEMENTS (expected_offsets))
    return FALSE;

  for (gsize i = 0;
       i < G_N_ELEMENTS (expected_offsets);
       i++)
    {
      if (differences[i] !=
          expected_offsets[i])
        return FALSE;
    }

  return TRUE;
}


static gboolean
goodix5135_submit_live_config_packet (
  FpDevice *device)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  guint8 packet[GOODIX5135_USB_PACKET_LENGTH] = { 0 };
  gboolean staged;
  gboolean submitted;

  staged =
    goodix5135_stage_current_config_packet (
      self,
      packet,
      sizeof (packet));

  if (!staged)
    {
      goodix5135_secure_zero (
        packet,
        sizeof (packet));

      return FALSE;
    }

  submitted =
    goodix5135_async_submit (
      device,
      &self->io,
      GOODIX5135_REQUEST_BULK_OUT,
      GOODIX5135_USB_PACKET_LENGTH,
      GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
      packet,
      sizeof (packet),
      goodix5135_live_config_out_cb,
      NULL);

  goodix5135_secure_zero (
    packet,
    sizeof (packet));

  return submitted;
}


static gboolean
goodix5135_submit_live_config_ack_read (
  FpDevice *device)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  return
    goodix5135_async_submit (
      device,
      &self->io,
      GOODIX5135_REQUEST_BULK_IN,
      GOODIX5135_USB_PACKET_LENGTH,
      GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
      NULL,
      0,
      goodix5135_live_config_ack_cb,
      NULL);
}


static gboolean
goodix5135_submit_live_config_response_read (
  FpDevice *device)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  return
    goodix5135_async_submit (
      device,
      &self->io,
      GOODIX5135_REQUEST_BULK_IN,
      GOODIX5135_USB_PACKET_LENGTH,
      GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
      NULL,
      0,
      goodix5135_live_config_response_cb,
      NULL);
}


static gboolean
goodix5135_start_live_config_upload_test (
  FpDevice *device)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  const gchar *template_path;

  gchar *template_data = NULL;
  gsize template_length = 0;

  gboolean ok = FALSE;

  if (!goodix5135_live_config_test_requested ())
    return FALSE;

  if (goodix5135_live_cfg70_consumed)
    return FALSE;

  /*
   * Consume the process gate before preparation or USB submission.
   * There is no automatic second attempt.
   */
  goodix5135_live_cfg70_consumed =
    TRUE;

  template_path =
    g_getenv ("GOODIX5135_CFG70_TEMPLATE_FILE");

  if (template_path == NULL)
    goto out;

  if (!g_file_get_contents (
        template_path,
        &template_data,
        &template_length,
        NULL))
    goto out;

  if (template_length !=
      GOODIX5135_CONFIG_LENGTH)
    goto out;

  if (!goodix5135_prepare_runtime_config_from_template (
        self,
        (const guint8 *) template_data,
        template_length))
    goto out;

  /*
   * This gate checks the exact structural delta previously observed in
   * the byte-for-byte Windows comparison.  No private values are
   * printed or retained.
   */
  if (!goodix5135_live_runtime_shape_valid (
        self,
        (const guint8 *) template_data,
        template_length))
    goto out;

  if (!goodix5135_arm_config_upload_transport (
        self))
    goto out;

  /*
   * Remove live-test environment authorization before the first OUT
   * submission.
   */
  g_unsetenv ("GOODIX5135_LIVE_CFG70_TEST");
  g_unsetenv ("GOODIX5135_CFG70_TEMPLATE_FILE");

  ok =
    goodix5135_submit_live_config_packet (
      device);

out:
  if (template_data != NULL)
    {
      goodix5135_secure_zero (
        template_data,
        template_length);

      g_free (template_data);
    }

  if (!ok)
    {
      g_unsetenv ("GOODIX5135_LIVE_CFG70_TEST");
      g_unsetenv ("GOODIX5135_CFG70_TEMPLATE_FILE");
    }

  return ok;
}


static void
goodix5135_live_config_out_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  gboolean transport_ok;

  (void) completion;
  (void) user_data;

  transport_ok =
    transfer != NULL &&
    error == NULL &&
    transfer->actual_length ==
      GOODIX5135_USB_PACKET_LENGTH;

  if (transfer != NULL &&
      transfer->buffer != NULL)
    {
      goodix5135_secure_zero (
        transfer->buffer,
        GOODIX5135_USB_PACKET_LENGTH);
    }

  g_clear_error (&error);

  if (!goodix5135_complete_config_out_packet (
        self,
        transport_ok))
    {
      goodix5135_open_transaction_fail (
        device,
        "Goodix CFG70 OUT completion failed");
      return;
    }

  if (self->config_upload_transaction.state ==
      GOODIX5135_CONFIG_UPLOAD_TRANSACTION_WAIT_OUT)
    {
      if (!goodix5135_submit_live_config_packet (
            device))
        {
          goodix5135_open_transaction_fail (
            device,
            "Could not submit next Goodix CFG70 packet");
        }

      return;
    }

  if (self->config_upload_transaction.state !=
      GOODIX5135_CONFIG_UPLOAD_TRANSACTION_WAIT_ACK)
    {
      goodix5135_open_transaction_fail (
        device,
        "Unexpected Goodix CFG70 state after OUT packets");
      return;
    }

  if (!goodix5135_submit_live_config_ack_read (
        device))
    {
      goodix5135_open_transaction_fail (
        device,
        "Could not submit Goodix CFG70 ACK read");
    }
}


static void
goodix5135_live_config_ack_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  gboolean transport_ok;
  gboolean protocol_ok;

  (void) user_data;

  transport_ok =
    goodix5135_in_transfer_can_parse (
      transfer,
      completion,
      error);

  protocol_ok =
    goodix5135_complete_config_ack (
      self,
      transport_ok,
      transport_ok && transfer != NULL
        ? transfer->buffer
        : NULL,
      transport_ok && transfer != NULL
        ? (gsize) transfer->actual_length
        : 0);

  if (transfer != NULL &&
      transfer->buffer != NULL &&
      transfer->actual_length > 0)
    {
      goodix5135_secure_zero (
        transfer->buffer,
        (gsize) transfer->actual_length);
    }

  g_clear_error (&error);

  if (!protocol_ok ||
      self->config_upload_transaction.state !=
        GOODIX5135_CONFIG_UPLOAD_TRANSACTION_WAIT_RESPONSE)
    {
      goodix5135_open_transaction_fail (
        device,
        "Goodix CFG70 ACK validation failed");
      return;
    }

  if (!goodix5135_submit_live_config_response_read (
        device))
    {
      goodix5135_open_transaction_fail (
        device,
        "Could not submit Goodix CFG70 response read");
    }
}


static void
goodix5135_live_config_response_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  FpImageDevice *dev =
    FP_IMAGE_DEVICE (device);

  gboolean transport_ok;
  gboolean protocol_ok;

  (void) user_data;

  transport_ok =
    goodix5135_in_transfer_can_parse (
      transfer,
      completion,
      error);

  protocol_ok =
    goodix5135_complete_config_response (
      self,
      transport_ok,
      transport_ok && transfer != NULL
        ? transfer->buffer
        : NULL,
      transport_ok && transfer != NULL
        ? (gsize) transfer->actual_length
        : 0);

  if (transfer != NULL &&
      transfer->buffer != NULL &&
      transfer->actual_length > 0)
    {
      goodix5135_secure_zero (
        transfer->buffer,
        (gsize) transfer->actual_length);
    }

  g_clear_error (&error);

  if (!protocol_ok ||
      self->config_upload_transaction.state !=
        GOODIX5135_CONFIG_UPLOAD_TRANSACTION_DONE)
    {
      goodix5135_open_transaction_fail (
        device,
        "Goodix CFG70 response validation failed");
      return;
    }

  fp_dbg (
    "One-shot Goodix CFG70 live transaction completed");

  goodix5135_io_stop (
    &self->io);

  g_assert (
    goodix5135_io_can_finish_stop (
      &self->io));

  fpi_image_device_open_complete (
    dev,
    NULL);
}





static void
goodix5135_start_otp_read (
  FpDevice *device)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  if (self->activation_sequence.state !=
      GOODIX5135_ACTIVATION_DONE)
    {
      goodix5135_open_transaction_fail (
        device,
        "Attempted READ_OTP before activation identity DONE");
      return;
    }

  self->otp_calibration_valid =
    FALSE;

  goodix5135_secure_zero (
    &self->otp_calibration,
    sizeof (self->otp_calibration));

  /*
   * A new OTP gate invalidates any previous config-preparation
   * readiness and restores the upload transaction to IDLE.
   *
   * This performs no USB I/O.
   */
  goodix5135_config_runtime_state_reset (
    self);

  goodix5135_otp_read_transaction_init (
    &self->otp_read_transaction);

  if (!goodix5135_otp_read_transaction_begin (
        &self->otp_read_transaction,
        packet,
        sizeof (packet),
        &logical_length))
    {
      goodix5135_open_transaction_fail (
        device,
        "Could not build Goodix READ_OTP request");
      return;
    }

  if (logical_length !=
      GOODIX5135_OTP_READ_REQUEST_LENGTH)
    {
      goodix5135_otp_read_transaction_out_complete (
        &self->otp_read_transaction,
        FALSE);

      goodix5135_open_transaction_fail (
        device,
        "Unexpected Goodix READ_OTP request length");
      return;
    }

  fp_dbg (
    "Submitting read-only Goodix OTP calibration gate");

  if (!goodix5135_async_submit (
        device,
        &self->io,
        GOODIX5135_REQUEST_BULK_OUT,
        GOODIX5135_USB_PACKET_LENGTH,
        GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
        packet,
        sizeof (packet),
        goodix5135_otp_out_cb,
        NULL))
    {
      goodix5135_otp_read_transaction_out_complete (
        &self->otp_read_transaction,
        FALSE);

      goodix5135_open_transaction_fail (
        device,
        "Could not submit Goodix READ_OTP request");
    }
}


static void
goodix5135_start_activation_chip_id_read (
  FpDevice *device)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  if (self->activation_sequence.state !=
      GOODIX5135_ACTIVATION_WAIT_CHIP_ID)
    {
      goodix5135_open_transaction_fail (
        device,
        "Attempted chip-ID read outside activation identity gate");
      return;
    }

  goodix5135_register_read_transaction_init (
    &self->register_read_transaction);

  if (!goodix5135_register_read_transaction_begin (
        &self->register_read_transaction,
        GOODIX5135_ACTIVATION_CHIP_ID_ADDRESS,
        GOODIX5135_ACTIVATION_CHIP_ID_LENGTH,
        packet,
        sizeof (packet),
        &logical_length))
    {
      goodix5135_activation_sequence_chip_id_complete (
        &self->activation_sequence,
        FALSE,
        NULL,
        0);

      goodix5135_open_transaction_fail (
        device,
        "Could not build activation chip-ID read request");
      return;
    }

  if (logical_length !=
      GOODIX5135_REGISTER_READ_REQUEST_LENGTH)
    {
      goodix5135_register_read_transaction_out_complete (
        &self->register_read_transaction,
        FALSE);

      goodix5135_activation_sequence_chip_id_complete (
        &self->activation_sequence,
        FALSE,
        NULL,
        0);

      goodix5135_open_transaction_fail (
        device,
        "Unexpected activation chip-ID request length");
      return;
    }

  fp_dbg (
    "Submitting read-only activation chip-ID gate 0x0000");

  if (!goodix5135_async_submit (
        device,
        &self->io,
        GOODIX5135_REQUEST_BULK_OUT,
        GOODIX5135_USB_PACKET_LENGTH,
        GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
        packet,
        sizeof (packet),
        goodix5135_activation_chip_id_out_cb,
        NULL))
    {
      goodix5135_register_read_transaction_out_complete (
        &self->register_read_transaction,
        FALSE);

      goodix5135_activation_sequence_chip_id_complete (
        &self->activation_sequence,
        FALSE,
        NULL,
        0);

      goodix5135_open_transaction_fail (
        device,
        "Could not submit activation chip-ID request");
    }
}


static void
goodix5135_start_nop_transaction (
  FpDevice *device)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  goodix5135_nop_transaction_init (
    &self->nop_transaction);

  if (!goodix5135_nop_transaction_begin (
        &self->nop_transaction,
        packet,
        sizeof (packet),
        &logical_length))
    {
      goodix5135_open_transaction_fail (
        device,
        "Could not build Goodix NOP request");
      return;
    }

  if (logical_length !=
      GOODIX5135_NOP_REQUEST_LENGTH)
    {
      goodix5135_nop_transaction_out_complete (
        &self->nop_transaction,
        FALSE);

      goodix5135_open_transaction_fail (
        device,
        "Unexpected Goodix NOP request length");
      return;
    }

  fp_dbg ("Submitting bounded Goodix NOP");

  if (!goodix5135_async_submit (
        device,
        &self->io,
        GOODIX5135_REQUEST_BULK_OUT,
        GOODIX5135_USB_PACKET_LENGTH,
        GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
        packet,
        sizeof (packet),
        goodix5135_nop_out_cb,
        NULL))
    {
      goodix5135_nop_transaction_out_complete (
        &self->nop_transaction,
        FALSE);

      goodix5135_open_transaction_fail (
        device,
        "Could not submit Goodix NOP request");
    }
}


static void
goodix5135_start_mcu_state_transaction (
  FpDevice *device)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  goodix5135_mcu_state_transaction_init (
    &self->mcu_state_transaction);

  if (!goodix5135_mcu_state_transaction_begin (
        &self->mcu_state_transaction,
        GOODIX5135_MCU_STATE_QUERY,
        packet,
        sizeof (packet),
        &logical_length))
    {
      goodix5135_open_transaction_fail (
        device,
        "Could not build Goodix MCU-state request");
      return;
    }

  if (logical_length !=
      GOODIX5135_MCU_STATE_REQUEST_LENGTH)
    {
      goodix5135_mcu_state_transaction_out_complete (
        &self->mcu_state_transaction,
        FALSE);

      goodix5135_open_transaction_fail (
        device,
        "Unexpected Goodix MCU-state request length");
      return;
    }

  fp_dbg ("Submitting read-only MCU state query 0x55");

  if (!goodix5135_async_submit (
        device,
        &self->io,
        GOODIX5135_REQUEST_BULK_OUT,
        GOODIX5135_USB_PACKET_LENGTH,
        GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
        packet,
        sizeof (packet),
        goodix5135_mcu_state_out_cb,
        NULL))
    {
      goodix5135_mcu_state_transaction_out_complete (
        &self->mcu_state_transaction,
        FALSE);

      goodix5135_open_transaction_fail (
        device,
        "Could not submit Goodix MCU-state request");
    }
}


static void
goodix5135_start_register_read_transaction (
  FpDevice *device)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  goodix5135_register_read_transaction_init (
    &self->register_read_transaction);

  if (!goodix5135_register_read_transaction_begin (
        &self->register_read_transaction,
        GOODIX5135_REGISTER_PROBE_ADDRESS,
        GOODIX5135_REGISTER_PROBE_LENGTH,
        packet,
        sizeof (packet),
        &logical_length))
    {
      goodix5135_open_transaction_fail (
        device,
        "Could not build Goodix read-only register request");
      return;
    }

  if (logical_length !=
      GOODIX5135_REGISTER_READ_REQUEST_LENGTH)
    {
      goodix5135_register_read_transaction_out_complete (
        &self->register_read_transaction,
        FALSE);

      goodix5135_open_transaction_fail (
        device,
        "Unexpected Goodix register-read request length");
      return;
    }

  fp_dbg ("Submitting read-only register probe 0x0220");

  if (!goodix5135_async_submit (
        device,
        &self->io,
        GOODIX5135_REQUEST_BULK_OUT,
        GOODIX5135_USB_PACKET_LENGTH,
        GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
        packet,
        sizeof (packet),
        goodix5135_register_out_cb,
        NULL))
    {
      goodix5135_register_read_transaction_out_complete (
        &self->register_read_transaction,
        FALSE);

      goodix5135_open_transaction_fail (
        device,
        "Could not submit Goodix read-only register request");
    }
}


static void
goodix5135_start_firmware_version_transaction (
  FpDevice *device)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

  goodix5135_firmware_transaction_init (
    &self->firmware_transaction);

  if (!goodix5135_firmware_transaction_begin (
        &self->firmware_transaction,
        packet,
        sizeof (packet),
        &logical_length))
    {
      goodix5135_open_transaction_fail (
        device,
        "Could not build Goodix firmware-version request");
      return;
    }

  if (logical_length !=
      GOODIX5135_FIRMWARE_REQUEST_LENGTH)
    {
      goodix5135_firmware_transaction_out_complete (
        &self->firmware_transaction,
        FALSE);

      goodix5135_open_transaction_fail (
        device,
        "Unexpected Goodix firmware request length");
      return;
    }

  /*
   * Exactly one read-only Goodix application request:
   *
   *   command 0xa8 — firmware version
   *
   * The builder produced:
   *   10 logical bytes
   *   padded to one 64-byte USB Bulk OUT packet.
   *
   * goodix5135_async_submit() copies the OUT packet into transfer-owned
   * storage before this stack buffer goes out of scope.
   */
  if (!goodix5135_async_submit (
        device,
        &self->io,
        GOODIX5135_REQUEST_BULK_OUT,
        GOODIX5135_USB_PACKET_LENGTH,
        GOODIX5135_FIRMWARE_IO_TIMEOUT_MS,
        packet,
        sizeof (packet),
        goodix5135_firmware_out_cb,
        NULL))
    {
      goodix5135_firmware_transaction_out_complete (
        &self->firmware_transaction,
        FALSE);

      goodix5135_open_transaction_fail (
        device,
        "Could not submit Goodix firmware-version request");
    }
}

static gboolean
goodix5135_submit_queue_cleanup_read (
  FpDevice *device)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);

  /*
   * Receive-only cleanup operation. No Goodix command is sent here.
   * Received bytes are intentionally discarded and never logged.
   */
  return goodix5135_async_submit (
    device,
    &self->io,
    GOODIX5135_REQUEST_BULK_IN,
    GOODIX5135_USB_PACKET_LENGTH,
    GOODIX5135_QUEUE_CLEANUP_TIMEOUT_MS,
    NULL,
    0,
    goodix5135_queue_cleanup_cb,
    NULL);
}

static void
goodix5135_queue_cleanup_cb (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (device);
  Goodix5135QueueReadResult result;
  Goodix5135QueueCleanupAction action;

  (void) user_data;

  /*
   * Keep lifecycle/generation semantics separate from USB transport
   * semantics.
   *
   * CURRENT + data/no-error:
   *   one stale packet was consumed.
   *
   * CURRENT + GUsb timeout:
   *   endpoint queue is considered empty for this cleanup window.
   *
   * CANCELLED, STALE, malformed completion, or any other transport error:
   *   fail closed.
   */
  if (completion ==
        GOODIX5135_REQUEST_COMPLETION_CURRENT &&
      error == NULL &&
      transfer != NULL &&
      transfer->actual_length > 0 &&
      transfer->actual_length <= transfer->length)
    {
      result = GOODIX5135_QUEUE_READ_DATA;
    }
  else if (completion ==
             GOODIX5135_REQUEST_COMPLETION_CURRENT &&
           error != NULL &&
           g_error_matches (
             error,
             G_USB_DEVICE_ERROR,
             G_USB_DEVICE_ERROR_TIMED_OUT))
    {
      result =
        GOODIX5135_QUEUE_READ_EMPTY_TIMEOUT;
    }
  else
    {
      result = GOODIX5135_QUEUE_READ_FATAL;
    }

  action =
    goodix5135_queue_cleanup_read_complete (
      &self->queue_cleanup,
      result);

  g_clear_error (&error);

  switch (action)
    {
    case GOODIX5135_QUEUE_ACTION_READ_AGAIN:
      if (!goodix5135_submit_queue_cleanup_read (
            device))
        {
          goodix5135_queue_cleanup_read_complete (
            &self->queue_cleanup,
            GOODIX5135_QUEUE_READ_FATAL);

          goodix5135_open_transaction_fail (
            device,
            "Could not submit Goodix pre-command queue cleanup read");
        }
      return;

    case GOODIX5135_QUEUE_ACTION_DONE:
      goodix5135_start_firmware_version_transaction (
        device);
      return;

    case GOODIX5135_QUEUE_ACTION_FAILED:
    default:
      goodix5135_open_transaction_fail (
        device,
        "Goodix pre-command receive-queue cleanup failed");
      return;
    }
}

static void
goodix5135_open (FpImageDevice *dev)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (dev);
  GUsbDevice *usb_dev;
  GError *error = NULL;

  usb_dev =
    fpi_device_get_usb_device (
      FP_DEVICE (dev));

  if (!g_usb_device_claim_interface (
        usb_dev,
        GOODIX5135_USB_INTERFACE,
        0,
        &error))
    {
      fpi_image_device_open_complete (
        dev,
        error);
      return;
    }

  /*
   * The OPEN probe uses one temporary I/O generation.
   * It is stopped before OPEN completes, so ACTIVATE may later start its
   * normal independent generation exactly as before.
   */
  if (!goodix5135_io_start (&self->io))
    {
      GError *release_error = NULL;

      g_usb_device_release_interface (
        usb_dev,
        GOODIX5135_USB_INTERFACE,
        0,
        &release_error);

      g_clear_error (&release_error);

      error =
        fpi_device_error_new_msg (
          FP_DEVICE_ERROR_GENERAL,
          "Goodix firmware-version I/O lifecycle could not start");

      fpi_image_device_open_complete (
        dev,
        error);
      return;
    }

  goodix5135_queue_cleanup_init (
    &self->queue_cleanup);

  if (goodix5135_queue_cleanup_begin (
        &self->queue_cleanup,
        GOODIX5135_QUEUE_CLEANUP_MAX_PACKETS) !=
      GOODIX5135_QUEUE_ACTION_READ_AGAIN)
    {
      goodix5135_open_transaction_fail (
        FP_DEVICE (dev),
        "Could not start Goodix pre-command queue cleanup");
      return;
    }

  if (!goodix5135_submit_queue_cleanup_read (
        FP_DEVICE (dev)))
    {
      goodix5135_queue_cleanup_read_complete (
        &self->queue_cleanup,
        GOODIX5135_QUEUE_READ_FATAL);

      goodix5135_open_transaction_fail (
        FP_DEVICE (dev),
        "Could not submit Goodix pre-command queue cleanup read");
    }
}

static void
goodix5135_close (FpImageDevice *dev)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (dev);

  GError *error = NULL;
  GUsbDevice *usb_dev;

  self->otp_calibration_valid =
    FALSE;

  goodix5135_secure_zero (
    &self->otp_calibration,
    sizeof (self->otp_calibration));

  /*
   * Close cannot retain volatile config preparation state.
   */
  goodix5135_config_runtime_state_reset (
    self);

  /*
   * Close cannot retain volatile TLS state.
   */
  goodix5135_tls_runtime_reset (
    self);

  usb_dev = fpi_device_get_usb_device (FP_DEVICE (dev));

  g_usb_device_release_interface (usb_dev,
                                  GOODIX5135_USB_INTERFACE,
                                  0,
                                  &error);

  fpi_image_device_close_complete (dev, error);
}

static void
goodix5135_maybe_finish_deactivate (FpiDeviceGoodix5135 *self,
                                    FpImageDevice       *dev)
{
  if (!self->deactivating)
    return;

  if (!goodix5135_io_can_finish_stop (&self->io))
    return;

  fp_dbg ("I/O lifecycle drained at generation %" G_GUINT64_FORMAT,
          self->io.generation);

  if (goodix5135_fpimage_test_requested () &&
      self->tls_session != NULL &&
      goodix5135_tls_session_is_ready (
        self->tls_session))
    {
      goodix5135_capture_runtime_reset (
        self);

      fp_dbg (
        "Native FpImage lifecycle preserved READY TLS across deactivation");
    }
  else
    {
      goodix5135_tls_runtime_reset (
        self);
    }

  self->deactivating = FALSE;

  fpi_image_device_deactivate_complete (dev, NULL);
}

void
goodix5135_driver_async_drained (FpDevice              *device,
                                 Goodix5135IoLifecycle *io)
{
  FpiDeviceGoodix5135 *self;

  g_return_if_fail (FP_IS_DEVICE (device));

  self = FPI_DEVICE_GOODIX5135 (device);

  g_return_if_fail (io == &self->io);

  /*
   * The async layer calls this only after the higher-level callback has
   * returned. If that callback queued another request, io.pending will
   * therefore already reflect it.
   */
  goodix5135_maybe_finish_deactivate (self,
                                      FP_IMAGE_DEVICE (device));
}

static void
goodix5135_activate (FpImageDevice *dev)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (dev);

  GError *error;

  if (!goodix5135_io_start (&self->io))
    {
      error =
        fpi_device_error_new_msg (
          FP_DEVICE_ERROR_GENERAL,
          "Goodix5135 I/O lifecycle could not start");

      self->active = FALSE;

      fpi_image_device_activate_complete (
        dev,
        error);

      return;
    }

  self->active = TRUE;
  self->deactivating = FALSE;

  fp_dbg (
    "Started host I/O lifecycle generation %" G_GUINT64_FORMAT,
    self->io.generation);

  if (goodix5135_fpimage_test_requested () &&
      (
        self->tls_session == NULL ||
        !goodix5135_tls_session_is_ready (
          self->tls_session)
      ))
    {
      goodix5135_io_stop (
        &self->io);

      self->active = FALSE;

      error =
        fpi_device_error_new_msg (
          FP_DEVICE_ERROR_GENERAL,
          "Goodix5135 FpImage lifecycle requires an existing READY TLS session");

      fpi_image_device_activate_complete (
        dev,
        error);

      return;
    }

  fpi_image_device_activate_complete (
    dev,
    NULL);
}


static void
goodix5135_deactivate (FpImageDevice *dev)
{
  FpiDeviceGoodix5135 *self = FPI_DEVICE_GOODIX5135 (dev);

  self->active = FALSE;
  self->deactivating = TRUE;

  /*
   * Stop accepting new logical I/O immediately.
   *
   * Once real asynchronous transfers exist, their callbacks will drain
   * io.pending and call goodix5135_maybe_finish_deactivate().
   */
  goodix5135_io_stop (&self->io);

  fp_dbg ("Stopping host I/O lifecycle generation %" G_GUINT64_FORMAT
          " with %u pending operation(s)",
          self->io.generation,
          self->io.pending);

  goodix5135_maybe_finish_deactivate (self, dev);
}

static gboolean
goodix5135_capture_baseline_settle_cb (
  gpointer user_data)
{
  FpImageDevice *dev =
    FP_IMAGE_DEVICE (user_data);

  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (dev);

  /*
   * This GLib source is currently dispatching.
   * Clear the stored source ID first so cleanup cannot try to
   * remove the currently executing source.
   */
  self->capture_baseline_settle_source = 0;

  if (!goodix5135_fpimage_test_requested () ||
      !self->active ||
      self->deactivating ||
      self->state !=
        FPI_IMAGE_DEVICE_STATE_AWAIT_FINGER_ON)
    {
      fp_dbg (
        "Native FpImage stage: pre-baseline settle became stale");

      return G_SOURCE_REMOVE;
    }

  if (self->capture_baseline_recheck_count == 0)
    {
      fp_dbg (
        "Native FpImage stage: initial 250 ms pre-baseline settle completed");
    }
  else
    {
      fp_dbg (
        "Native FpImage stage: 100 ms baseline stability recheck delay completed");
    }

  if (!goodix5135_capture_runtime_start (
        FP_DEVICE (dev)))
    {
      goodix5135_capture_runtime_fail (
        FP_DEVICE (dev),
        "could not start FDT detection after pre-baseline settle");
    }

  return G_SOURCE_REMOVE;
}


static void
goodix5135_change_state (
  FpImageDevice      *dev,
  FpiImageDeviceState state)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (dev);

  self->state = state;

  if (!goodix5135_fpimage_test_requested ())
    {
      fp_dbg (
        "Image device state changed to %d (legacy scaffold)",
        state);

      return;
    }

  fp_dbg (
    "Native FpImage state changed to %d",
    state);

  switch (state)
    {
    case FPI_IMAGE_DEVICE_STATE_AWAIT_FINGER_ON:
      self->capture_baseline_recheck_count = 0;

      if (self->capture_baseline_settle_source != 0)
        {
          fp_dbg (
            "Native FpImage stage: pre-baseline settle already pending");

          return;
        }

      fp_dbg (
        "Native FpImage stage: scheduling 250 ms pre-baseline settle");

      self->capture_baseline_settle_source =
        g_timeout_add_full (
          G_PRIORITY_DEFAULT,
          GOODIX5135_CAPTURE_BASELINE_SETTLE_MS,
          goodix5135_capture_baseline_settle_cb,
          g_object_ref (dev),
          g_object_unref);

      if (self->capture_baseline_settle_source == 0)
        {
          goodix5135_capture_runtime_fail (
            FP_DEVICE (dev),
            "could not schedule pre-baseline settle");
        }

      return;

    case FPI_IMAGE_DEVICE_STATE_CAPTURE:
      if (!goodix5135_capture_start_image (
            FP_DEVICE (dev)))
        {
          goodix5135_capture_runtime_fail (
            FP_DEVICE (dev),
            "could not start native FpImage capture");
        }

      return;

    case FPI_IMAGE_DEVICE_STATE_AWAIT_FINGER_OFF:
      if (!goodix5135_capture_start_fdt_up (
            FP_DEVICE (dev)))
        {
          goodix5135_capture_runtime_fail (
            FP_DEVICE (dev),
            "could not start FDT finger-off detection");
        }

      return;

    case FPI_IMAGE_DEVICE_STATE_IDLE:
    case FPI_IMAGE_DEVICE_STATE_ACTIVATING:
    case FPI_IMAGE_DEVICE_STATE_DEACTIVATING:
    case FPI_IMAGE_DEVICE_STATE_INACTIVE:
      return;

    default:
      goodix5135_capture_runtime_fail (
        FP_DEVICE (dev),
        "unexpected FpImageDevice state");

      return;
    }
}


static void
fpi_device_goodix5135_init (FpiDeviceGoodix5135 *self)
{
  self->active = FALSE;
  self->deactivating = FALSE;
  self->state = 0;

  goodix5135_io_init (&self->io);

  self->tls_session = NULL;
  self->tls_runtime_open_gate = FALSE;
  self->tls_runtime_d4_delay_source = 0;
  self->capture_baseline_settle_source = 0;
  self->capture_baseline_recheck_count = 0;

  goodix5135_capture_runtime_reset (
    self);

  goodix5135_tls_request_transaction_init (
    &self->tls_request_transaction);

  goodix5135_d4_transaction_init (
    &self->tls_runtime_d4_transaction);

  goodix5135_queue_cleanup_init (
    &self->queue_cleanup);

  goodix5135_firmware_transaction_init (
    &self->firmware_transaction);

  goodix5135_register_read_transaction_init (
    &self->register_read_transaction);

  goodix5135_mcu_state_transaction_init (
    &self->mcu_state_transaction);

  goodix5135_nop_transaction_init (
    &self->nop_transaction);

  goodix5135_d4_transaction_init (
    &self->d4_transaction);

  goodix5135_enable_chip_transaction_init (
    &self->enable_chip_transaction);

  goodix5135_sensor_reset_transaction_init (
    &self->sensor_reset_transaction);

  goodix5135_activation_sequence_init (
    &self->activation_sequence);

  goodix5135_otp_read_transaction_init (
    &self->otp_read_transaction);

  memset (
    &self->otp_calibration,
    0,
    sizeof (self->otp_calibration));

  self->otp_calibration_valid =
    FALSE;

  /*
   * Establish explicit zero/IDLE config preparation state.
   */
  goodix5135_config_runtime_state_reset (
    self);
}

static void
fpi_device_goodix5135_class_init (FpiDeviceGoodix5135Class *klass)
{
  FpDeviceClass *dev_class = FP_DEVICE_CLASS (klass);
  FpImageDeviceClass *img_class = FP_IMAGE_DEVICE_CLASS (klass);

  dev_class->id = "goodix5135";
  dev_class->full_name = "Goodix 27c6:5135 ChicagoHU Fingerprint Sensor";
  dev_class->type = FP_DEVICE_TYPE_USB;
  dev_class->id_table = goodix5135_id_table;
  dev_class->scan_type = FP_SCAN_TYPE_PRESS;

  img_class->img_open = goodix5135_open;
  img_class->img_close = goodix5135_close;
  img_class->activate = goodix5135_activate;
  img_class->deactivate = goodix5135_deactivate;
  img_class->change_state = goodix5135_change_state;

  /*
   * Downstream image geometry proven from the Windows-compatible
   * ChicagoHU path. No image is submitted by this scaffold.
   */
  img_class->img_width = GOODIX5135_IMAGE_WIDTH;
  img_class->img_height = GOODIX5135_IMAGE_HEIGHT;

  /*
   * Placeholder inherited from common image-device drivers.
   * It is NOT claimed to be the final Chicago matcher threshold.
   */
  img_class->bz3_threshold = 24;
}
