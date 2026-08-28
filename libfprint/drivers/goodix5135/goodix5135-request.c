/*
 * Goodix 27c6:5135 host-side asynchronous request bookkeeping.
 */

#include "goodix5135-request.h"

#include "goodix5135.h"

static gboolean
goodix5135_request_parameters_valid (Goodix5135RequestKind kind,
                                     guint8                 endpoint,
                                     gsize                  length,
                                     guint                  timeout_ms)
{
  if (length == 0 || timeout_ms == 0)
    return FALSE;

  switch (kind)
    {
    case GOODIX5135_REQUEST_BULK_IN:
      return endpoint == GOODIX5135_EP_IN;

    case GOODIX5135_REQUEST_BULK_OUT:
      return endpoint == GOODIX5135_EP_OUT;

    case GOODIX5135_REQUEST_NONE:
    default:
      return FALSE;
    }
}

void
goodix5135_request_init (Goodix5135Request *request)
{
  g_return_if_fail (request != NULL);

  request->token.generation = 0;
  request->token.outstanding = FALSE;

  request->kind = GOODIX5135_REQUEST_NONE;
  request->endpoint = 0;
  request->length = 0;
  request->timeout_ms = 0;

  request->in_flight = FALSE;
  request->cancel_requested = FALSE;
}

gboolean
goodix5135_request_begin (Goodix5135IoLifecycle *io,
                          Goodix5135Request     *request,
                          Goodix5135RequestKind  kind,
                          guint8                 endpoint,
                          gsize                  length,
                          guint                  timeout_ms)
{
  g_return_val_if_fail (io != NULL, FALSE);
  g_return_val_if_fail (request != NULL, FALSE);

  if (request->in_flight || request->token.outstanding)
    return FALSE;

  if (!goodix5135_request_parameters_valid (kind,
                                            endpoint,
                                            length,
                                            timeout_ms))
    return FALSE;

  if (!goodix5135_io_begin (io, &request->token))
    return FALSE;

  request->kind = kind;
  request->endpoint = endpoint;
  request->length = length;
  request->timeout_ms = timeout_ms;

  request->in_flight = TRUE;
  request->cancel_requested = FALSE;

  return TRUE;
}

gboolean
goodix5135_request_cancel (Goodix5135Request *request)
{
  g_return_val_if_fail (request != NULL, FALSE);

  if (!request->in_flight || !request->token.outstanding)
    return FALSE;

  request->cancel_requested = TRUE;

  return TRUE;
}

gboolean
goodix5135_request_complete (Goodix5135IoLifecycle *io,
                             Goodix5135Request     *request,
                             gboolean              *was_cancel_requested)
{
  gboolean current;

  g_return_val_if_fail (io != NULL, FALSE);
  g_return_val_if_fail (request != NULL, FALSE);

  if (!request->in_flight || !request->token.outstanding)
    return FALSE;

  if (was_cancel_requested != NULL)
    *was_cancel_requested = request->cancel_requested;

  current = goodix5135_io_complete (io, &request->token);

  request->kind = GOODIX5135_REQUEST_NONE;
  request->endpoint = 0;
  request->length = 0;
  request->timeout_ms = 0;

  request->in_flight = FALSE;
  request->cancel_requested = FALSE;

  return current;
}

Goodix5135RequestCompletion
goodix5135_request_finish (Goodix5135IoLifecycle *io,
                           Goodix5135Request     *request,
                           gboolean               action_cancelled)
{
  gboolean request_cancelled = FALSE;
  gboolean current;

  g_return_val_if_fail (io != NULL,
                        GOODIX5135_REQUEST_COMPLETION_INVALID);
  g_return_val_if_fail (request != NULL,
                        GOODIX5135_REQUEST_COMPLETION_INVALID);

  if (!request->in_flight || !request->token.outstanding)
    return GOODIX5135_REQUEST_COMPLETION_INVALID;

  current = goodix5135_request_complete (io,
                                         request,
                                         &request_cancelled);

  /*
   * Cancellation wins over generation state.
   *
   * In particular, the action cancellable may already be cancelled while
   * FpImageDevice's idle cancel dispatch has not yet called deactivate().
   * Such a callback must still never advance the protocol state machine.
   */
  if (action_cancelled || request_cancelled)
    return GOODIX5135_REQUEST_COMPLETION_CANCELLED;

  if (!current)
    return GOODIX5135_REQUEST_COMPLETION_STALE;

  return GOODIX5135_REQUEST_COMPLETION_CURRENT;
}
