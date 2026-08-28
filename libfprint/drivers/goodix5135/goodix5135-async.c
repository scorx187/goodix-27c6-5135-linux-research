/*
 * Goodix 27c6:5135 isolated asynchronous USB operation wrapper.
 *
 * IMPORTANT:
 * The driver runtime does not call this layer yet.
 */

#include "drivers_api.h"

#include "goodix5135-async.h"
#include "goodix5135-transport.h"

#include "goodix5135.h"

typedef struct
{
  Goodix5135IoLifecycle  *io;
  Goodix5135Request       request;

  Goodix5135AsyncCallback callback;
  gpointer                user_data;
} Goodix5135AsyncOperation;

static gboolean
goodix5135_async_kind_endpoint (Goodix5135RequestKind  kind,
                                guint8                 *endpoint)
{
  g_return_val_if_fail (endpoint != NULL, FALSE);

  switch (kind)
    {
    case GOODIX5135_REQUEST_BULK_IN:
      *endpoint = GOODIX5135_EP_IN;
      return TRUE;

    case GOODIX5135_REQUEST_BULK_OUT:
      *endpoint = GOODIX5135_EP_OUT;
      return TRUE;

    case GOODIX5135_REQUEST_NONE:
    default:
      return FALSE;
    }
}

static void
goodix5135_async_transfer_cb (FpiUsbTransfer *transfer,
                              FpDevice       *device,
                              gpointer        user_data,
                              GError         *error)
{
  Goodix5135AsyncOperation *operation = user_data;
  Goodix5135RequestCompletion completion;
  gboolean action_cancelled;

  g_assert (operation != NULL);
  g_assert (operation->io != NULL);
  g_assert (operation->callback != NULL);

  /*
   * Cancellation may be observable here before FpImageDevice's queued
   * cancel dispatch reaches the driver's deactivate vfunc.
   *
   * Also treat an explicit USB cancellation result as cancellation even
   * if the action state has changed between dispatch and this callback.
   */
  action_cancelled =
    fpi_device_action_is_cancelled (device) ||
    (error != NULL &&
     g_error_matches (error,
                      G_IO_ERROR,
                      G_IO_ERROR_CANCELLED));

  completion =
    goodix5135_request_finish (operation->io,
                               &operation->request,
                               action_cancelled);

  /*
   * FpiUsbTransfer is still alive here. libfprint releases its stolen
   * transfer reference only after this callback returns.
   *
   * The higher layer receives the transfer as borrowed and receives
   * ownership of @error.
   */
  operation->callback (device,
                       transfer,
                       completion,
                       error,
                       operation->user_data);

  g_free (operation);
}

gboolean
goodix5135_async_submit (FpDevice                   *device,
                         Goodix5135IoLifecycle      *io,
                         Goodix5135RequestKind       kind,
                         gsize                       length,
                         guint                       timeout_ms,
                         const guint8               *out_data,
                         gsize                       out_data_length,
                         Goodix5135AsyncCallback     callback,
                         gpointer                    user_data)
{
  g_autoptr(FpiUsbTransfer) transfer = NULL;
  Goodix5135AsyncOperation *operation;
  guint8 endpoint;

  g_return_val_if_fail (FP_IS_DEVICE (device), FALSE);
  g_return_val_if_fail (io != NULL, FALSE);
  g_return_val_if_fail (callback != NULL, FALSE);

  if (!goodix5135_async_kind_endpoint (kind, &endpoint))
    return FALSE;

  operation = g_new0 (Goodix5135AsyncOperation, 1);

  operation->io = io;
  operation->callback = callback;
  operation->user_data = user_data;

  goodix5135_request_init (&operation->request);

  if (!goodix5135_request_begin (io,
                                 &operation->request,
                                 kind,
                                 endpoint,
                                 length,
                                 timeout_ms))
    {
      g_free (operation);
      return FALSE;
    }

  transfer =
    goodix5135_transport_prepare_transfer (device,
                                           &operation->request,
                                           out_data,
                                           out_data_length);

  if (transfer == NULL)
    {
      /*
       * The request was accounted as pending before transfer preparation.
       * Undo that accounting because no callback can arrive.
       */
      goodix5135_request_complete (io,
                                   &operation->request,
                                   NULL);

      g_free (operation);
      return FALSE;
    }

  /*
   * fpi_usb_transfer_submit() steals the transfer and guarantees a callback
   * for normal completion, transport error, timeout, or cancellation,
   * including cancellation already active before submission.
   */
  fpi_usb_transfer_submit (g_steal_pointer (&transfer),
                           timeout_ms,
                           fpi_device_get_cancellable (device),
                           goodix5135_async_transfer_cb,
                           operation);

  return TRUE;
}
