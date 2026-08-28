/*
 * Goodix 27c6:5135 host-side async completion dispatch.
 *
 * This module contains no USB operations.
 */

#pragma once

#include <glib.h>

#include "goodix5135-io.h"
#include "goodix5135-request.h"

typedef void (*Goodix5135AsyncCompletionNotify) (
  Goodix5135RequestCompletion  completion,
  gpointer                     user_data);

typedef void (*Goodix5135AsyncDrainNotify) (
  Goodix5135IoLifecycle       *io,
  gpointer                     user_data);

/*
 * Finish request accounting, invoke the higher-layer completion handler,
 * and only then invoke the post-callback drain notification.
 *
 * The ordering is intentional: a CURRENT completion handler may enqueue a
 * replacement request. The drain observer must see that new pending request
 * before deciding whether deactivation can finish.
 */
/*
 * A request being CURRENT only establishes lifecycle/generation state.
 * Transport success additionally requires that no USB error was reported.
 */
gboolean
goodix5135_async_result_can_advance (
  Goodix5135RequestCompletion completion,
  const GError                *error);

Goodix5135RequestCompletion
goodix5135_async_dispatch_finish (
  Goodix5135IoLifecycle          *io,
  Goodix5135Request              *request,
  gboolean                        action_cancelled,
  Goodix5135AsyncCompletionNotify completion_notify,
  gpointer                        completion_user_data,
  Goodix5135AsyncDrainNotify      drain_notify,
  gpointer                        drain_user_data);
