/*
 * Goodix 27c6:5135 libfprint USB transport preparation.
 *
 * IMPORTANT:
 * This file only prepares transfers; it never submits them.
 */

#include "drivers_api.h"

#include "goodix5135-transport.h"

#include "goodix5135.h"

FpiUsbTransfer *
goodix5135_transport_prepare_transfer (
  FpDevice                *device,
  const Goodix5135Request *request,
  const guint8            *out_data,
  gsize                    out_data_length)
{
  g_autoptr(FpiUsbTransfer) transfer = NULL;

  g_return_val_if_fail (FP_IS_DEVICE (device), NULL);
  g_return_val_if_fail (request != NULL, NULL);

  /*
   * Preparation is only valid for a request which is already accounted
   * for by the lifecycle/request model.
   */
  if (!request->in_flight ||
      !request->token.outstanding ||
      request->length == 0 ||
      request->timeout_ms == 0)
    return NULL;

  switch (request->kind)
    {
    case GOODIX5135_REQUEST_BULK_IN:
      if (request->endpoint != GOODIX5135_EP_IN)
        return NULL;

      if (out_data != NULL || out_data_length != 0)
        return NULL;

      transfer = fpi_usb_transfer_new (device);

      fpi_usb_transfer_fill_bulk (transfer,
                                  request->endpoint,
                                  request->length);

      return g_steal_pointer (&transfer);

    case GOODIX5135_REQUEST_BULK_OUT:
      {
        guint8 *owned_data;

        if (request->endpoint != GOODIX5135_EP_OUT)
          return NULL;

        if (out_data == NULL ||
            out_data_length != request->length)
          return NULL;

        /*
         * Do not borrow caller storage for a future asynchronous
         * operation. The transfer owns this copy through g_free.
         */
        owned_data = g_memdup2 (out_data, out_data_length);

        transfer = fpi_usb_transfer_new (device);

        fpi_usb_transfer_fill_bulk_full (transfer,
                                         request->endpoint,
                                         owned_data,
                                         out_data_length,
                                         g_free);

        return g_steal_pointer (&transfer);
      }

    case GOODIX5135_REQUEST_NONE:
    default:
      return NULL;
    }
}
