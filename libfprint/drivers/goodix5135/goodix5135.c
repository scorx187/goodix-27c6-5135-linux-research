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
  FpImageDevice *dev =
    FP_IMAGE_DEVICE (device);
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

  goodix5135_io_stop (&self->io);

  g_assert (
    goodix5135_io_can_finish_stop (
      &self->io));

  fpi_image_device_open_complete (
    dev,
    NULL);
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
  GError *error = NULL;
  GUsbDevice *usb_dev;

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
  FpiDeviceGoodix5135 *self = FPI_DEVICE_GOODIX5135 (dev);
  GError *error;

  if (!goodix5135_io_start (&self->io))
    {
      error =
        fpi_device_error_new_msg (
          FP_DEVICE_ERROR_GENERAL,
          "Goodix5135 I/O lifecycle could not start");

      self->active = FALSE;

      fpi_image_device_activate_complete (dev, error);
      return;
    }

  self->active = TRUE;
  self->deactivating = FALSE;

  fp_dbg ("Started host I/O lifecycle generation %" G_GUINT64_FORMAT,
          self->io.generation);

  /*
   * No activation/configuration/TLS/FDT command is sent yet.
   */
  fpi_image_device_activate_complete (dev, NULL);
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

static void
goodix5135_change_state (FpImageDevice      *dev,
                         FpiImageDeviceState state)
{
  FpiDeviceGoodix5135 *self = FPI_DEVICE_GOODIX5135 (dev);

  self->state = state;

  /*
   * Do not perform device I/O in the scaffold.
   * Later revisions will map these states to FDT/capture lifecycle.
   */
  fp_dbg ("Image device state changed to %d (scaffold only)", state);
}

static void
fpi_device_goodix5135_init (FpiDeviceGoodix5135 *self)
{
  self->active = FALSE;
  self->deactivating = FALSE;
  self->state = 0;

  goodix5135_io_init (&self->io);

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
