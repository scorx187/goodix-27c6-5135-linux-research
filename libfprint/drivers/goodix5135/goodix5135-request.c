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
