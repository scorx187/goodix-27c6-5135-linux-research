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
#include "goodix5135-io.h"
#include "goodix5135-proto.h"

struct _FpiDeviceGoodix5135
{
  FpImageDevice          parent;

  gboolean               active;
  gboolean               deactivating;
  FpiImageDeviceState    state;
  Goodix5135IoLifecycle  io;
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

static void
goodix5135_open (FpImageDevice *dev)
{
  GError *error = NULL;
  GUsbDevice *usb_dev;

  fp_dbg ("Opening %s; no Goodix protocol command is implemented",
          goodix5135_proto_stage_name ());

  usb_dev = fpi_device_get_usb_device (FP_DEVICE (dev));

  if (!g_usb_device_claim_interface (usb_dev,
                                     GOODIX5135_USB_INTERFACE,
                                     0,
                                     &error))
    {
      fpi_image_device_open_complete (dev, error);
      return;
    }

  fpi_image_device_open_complete (dev, NULL);
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
