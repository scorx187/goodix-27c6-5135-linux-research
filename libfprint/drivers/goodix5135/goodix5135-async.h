/*
 * Goodix 27c6:5135 isolated asynchronous USB operation wrapper.
 *
 * This API is not wired into the image-device runtime yet.
 */

#pragma once

#include <glib.h>

#include "fpi-device.h"
#include "fpi-usb-transfer.h"

#include "goodix5135-io.h"
#include "goodix5135-request.h"

typedef void (*Goodix5135AsyncCallback) (
  FpDevice                       *device,
  FpiUsbTransfer                 *transfer,
  Goodix5135RequestCompletion     completion,
  GError                         *error,
  gpointer                        user_data);

/*
 * Start one asynchronous Goodix bulk operation.
 *
 * The operation owns its Goodix5135Request on the heap until the USB
 * callback runs.
 *
 * @transfer passed to @callback is borrowed and valid only during the
 * callback unless the callback explicitly takes an additional transfer ref.
 *
 * @error ownership is transferred to @callback. The callback must free,
 * consume, or otherwise take ownership of it when non-NULL.
 *
 * Returns TRUE only after ownership of the prepared FpiUsbTransfer has
 * been handed to fpi_usb_transfer_submit(). If FALSE is returned, no USB
 * callback will occur and lifecycle accounting has already been unwound.
 */
gboolean goodix5135_async_submit (
           FpDevice                   *device,
           Goodix5135IoLifecycle      *io,
           Goodix5135RequestKind       kind,
           gsize                       length,
           guint                       timeout_ms,
           const guint8               *out_data,
           gsize                       out_data_length,
           Goodix5135AsyncCallback     callback,
           gpointer                    user_data);
