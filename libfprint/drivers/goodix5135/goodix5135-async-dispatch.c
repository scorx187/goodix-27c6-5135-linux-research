/*
 * Goodix 27c6:5135 host-side async completion dispatch.
 */

#include "goodix5135-async-dispatch.h"

Goodix5135RequestCompletion
goodix5135_async_dispatch_finish (
  Goodix5135IoLifecycle          *io,
  Goodix5135Request              *request,
  gboolean                        action_cancelled,
  Goodix5135AsyncCompletionNotify completion_notify,
  gpointer                        completion_user_data,
  Goodix5135AsyncDrainNotify      drain_notify,
  gpointer                        drain_user_data)
{
  Goodix5135RequestCompletion completion;

  g_return_val_if_fail (io != NULL,
                        GOODIX5135_REQUEST_COMPLETION_INVALID);
  g_return_val_if_fail (request != NULL,
                        GOODIX5135_REQUEST_COMPLETION_INVALID);

  completion =
    goodix5135_request_finish (io,
                               request,
                               action_cancelled);

  /*
   * Higher-level completion processing deliberately runs first.
   * It may enqueue another logical I/O request.
   */
  if (completion_notify != NULL)
    completion_notify (completion,
                       completion_user_data);

  /*
   * Only after the higher layer has finished do we allow the driver to
   * inspect pending accounting and potentially complete deactivation.
   */
  if (drain_notify != NULL)
    drain_notify (io,
                  drain_user_data);

  return completion;
}
