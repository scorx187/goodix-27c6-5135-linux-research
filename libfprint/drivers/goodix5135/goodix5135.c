/*
 * Goodix 27c6:5135 ChicagoHU fingerprint reader
 *
 * Host-side libfprint lifecycle scaffold.
 *
 * IMPORTANT:
 * This revision does not send any Goodix protocol command.
 * No FpiUsbTransfer is submitted by this driver yet.
 */

#define FP_COMPONENT "goodix5135"

#include "drivers_api.h"

#include "goodix5135.h"
#include "goodix5135-async.h"
#include "goodix5135-async-dispatch.h"
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
  Goodix5135FirmwareTransaction firmware_transaction;
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
 * TEMPORARY BUILD-ONLY runtime wiring for the first read-only transaction.
 *
 * This source snapshot lives under /tmp and must not be committed or run
 * until a separate live-hardware gate has been reviewed.
 */
#define GOODIX5135_FIRMWARE_IO_TIMEOUT_MS 1000U

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
  FpImageDevice *dev =
    FP_IMAGE_DEVICE (device);
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

  goodix5135_io_stop (&self->io);

  g_assert (
    goodix5135_io_can_finish_stop (
      &self->io));

  fpi_image_device_open_complete (
    dev,
    NULL);
}

static void
goodix5135_open (FpImageDevice *dev)
{
  FpiDeviceGoodix5135 *self =
    FPI_DEVICE_GOODIX5135 (dev);
  GUsbDevice *usb_dev;
  GError *error = NULL;
  guint8 packet[GOODIX5135_USB_PACKET_LENGTH];
  gsize logical_length = 0;

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

  goodix5135_firmware_transaction_init (
    &self->firmware_transaction);

  if (!goodix5135_firmware_transaction_begin (
        &self->firmware_transaction,
        packet,
        sizeof (packet),
        &logical_length))
    {
      goodix5135_open_transaction_fail (
        FP_DEVICE (dev),
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
        FP_DEVICE (dev),
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
        FP_DEVICE (dev),
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
        FP_DEVICE (dev),
        "Could not submit Goodix firmware-version request");
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

  goodix5135_firmware_transaction_init (
    &self->firmware_transaction);
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
