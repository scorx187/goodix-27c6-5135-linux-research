/*
 * Goodix 27c6:5135 host-side asynchronous request bookkeeping.
 *
 * No USB transfer is created or submitted by this module.
 */

#pragma once

#include <glib.h>

#include "goodix5135-io.h"

typedef enum
{
  GOODIX5135_REQUEST_NONE = 0,
  GOODIX5135_REQUEST_BULK_IN,
  GOODIX5135_REQUEST_BULK_OUT,
} Goodix5135RequestKind;

typedef struct
{
  Goodix5135IoToken   token;

  Goodix5135RequestKind kind;
  guint8                 endpoint;
  gsize                  length;
  guint                  timeout_ms;

  gboolean               in_flight;
  gboolean               cancel_requested;
} Goodix5135Request;

void     goodix5135_request_init            (Goodix5135Request     *request);

gboolean goodix5135_request_begin           (Goodix5135IoLifecycle *io,
                                             Goodix5135Request     *request,
                                             Goodix5135RequestKind  kind,
                                             guint8                 endpoint,
                                             gsize                  length,
                                             guint                  timeout_ms);

gboolean goodix5135_request_cancel          (Goodix5135Request     *request);

/*
 * Returns TRUE only when the completion belongs to the currently
 * running lifecycle generation.
 *
 * A FALSE return can therefore represent a stale completion after stop.
 * @was_cancel_requested reports whether cancellation had been requested
 * before this completion arrived.
 */
gboolean goodix5135_request_complete        (Goodix5135IoLifecycle *io,
                                             Goodix5135Request     *request,
                                             gboolean              *was_cancel_requested);
