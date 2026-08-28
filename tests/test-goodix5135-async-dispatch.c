/*
 * Host-only Goodix5135 async completion/drain ordering tests.
 *
 * No FpiUsbTransfer is constructed or submitted.
 */

#include <glib.h>

#include "../libfprint/drivers/goodix5135/goodix5135.h"
#include "../libfprint/drivers/goodix5135/goodix5135-io.h"
#include "../libfprint/drivers/goodix5135/goodix5135-request.h"
#include "../libfprint/drivers/goodix5135/goodix5135-async-dispatch.h"

typedef struct
{
  Goodix5135IoLifecycle *io;
  Goodix5135Request      replacement;

  guint                  sequence;
  guint                  completion_sequence;
  guint                  drain_sequence;

  gboolean               completion_saw_zero_pending;
  gboolean               drain_saw_finishable;
  gboolean               enqueue_replacement;

  Goodix5135RequestCompletion completion;
} DispatchProbe;

static void
probe_completion (Goodix5135RequestCompletion completion,
                  gpointer                    user_data)
{
  DispatchProbe *probe = user_data;

  probe->sequence += 1;
  probe->completion_sequence = probe->sequence;
  probe->completion = completion;

  probe->completion_saw_zero_pending =
    probe->io->pending == 0;

  if (probe->enqueue_replacement)
    {
      goodix5135_request_init (&probe->replacement);

      g_assert_true (
        goodix5135_request_begin (probe->io,
                                  &probe->replacement,
                                  GOODIX5135_REQUEST_BULK_IN,
                                  GOODIX5135_EP_IN,
                                  64,
                                  1000));
    }
}

static void
probe_drain (Goodix5135IoLifecycle *io,
             gpointer                user_data)
{
  DispatchProbe *probe = user_data;

  probe->sequence += 1;
  probe->drain_sequence = probe->sequence;

  probe->drain_saw_finishable =
    goodix5135_io_can_finish_stop (io);
}

static void
test_current_completion_then_drain (void)
{
  Goodix5135IoLifecycle io;
  Goodix5135Request request;
  DispatchProbe probe = { 0 };

  goodix5135_io_init (&io);
  goodix5135_request_init (&request);

  g_assert_true (goodix5135_io_start (&io));

  g_assert_true (
    goodix5135_request_begin (&io,
                              &request,
                              GOODIX5135_REQUEST_BULK_IN,
                              GOODIX5135_EP_IN,
                              64,
                              1000));

  probe.io = &io;

  g_assert_cmpuint (
    goodix5135_async_dispatch_finish (&io,
                                      &request,
                                      FALSE,
                                      probe_completion,
                                      &probe,
                                      probe_drain,
                                      &probe),
    ==,
    GOODIX5135_REQUEST_COMPLETION_CURRENT);

  g_assert_cmpuint (probe.completion_sequence, ==, 1);
  g_assert_cmpuint (probe.drain_sequence, ==, 2);
  g_assert_true (probe.completion_saw_zero_pending);

  /*
   * Lifecycle is still running, so deactivation is not finishable.
   */
  g_assert_false (probe.drain_saw_finishable);
  g_assert_cmpuint (io.pending, ==, 0);
}

static void
test_completion_can_requeue_before_drain (void)
{
  Goodix5135IoLifecycle io;
  Goodix5135Request request;
  DispatchProbe probe = { 0 };

  goodix5135_io_init (&io);
  goodix5135_request_init (&request);

  g_assert_true (goodix5135_io_start (&io));

  g_assert_true (
    goodix5135_request_begin (&io,
                              &request,
                              GOODIX5135_REQUEST_BULK_IN,
                              GOODIX5135_EP_IN,
                              64,
                              1000));

  probe.io = &io;
  probe.enqueue_replacement = TRUE;

  g_assert_cmpuint (
    goodix5135_async_dispatch_finish (&io,
                                      &request,
                                      FALSE,
                                      probe_completion,
                                      &probe,
                                      probe_drain,
                                      &probe),
    ==,
    GOODIX5135_REQUEST_COMPLETION_CURRENT);

  g_assert_cmpuint (probe.completion_sequence, ==, 1);
  g_assert_cmpuint (probe.drain_sequence, ==, 2);

  /*
   * The first request was already drained when the higher callback ran,
   * then that callback queued the replacement before drain inspection.
   */
  g_assert_true (probe.completion_saw_zero_pending);
  g_assert_cmpuint (io.pending, ==, 1);
  g_assert_false (probe.drain_saw_finishable);

  g_assert_true (
    goodix5135_request_complete (&io,
                                 &probe.replacement,
                                 NULL));

  g_assert_cmpuint (io.pending, ==, 0);
}

static void
test_cancel_before_stop_then_later_stop (void)
{
  Goodix5135IoLifecycle io;
  Goodix5135Request request;
  DispatchProbe probe = { 0 };

  goodix5135_io_init (&io);
  goodix5135_request_init (&request);

  g_assert_true (goodix5135_io_start (&io));

  g_assert_true (
    goodix5135_request_begin (&io,
                              &request,
                              GOODIX5135_REQUEST_BULK_IN,
                              GOODIX5135_EP_IN,
                              64,
                              1000));

  probe.io = &io;

  g_assert_cmpuint (
    goodix5135_async_dispatch_finish (&io,
                                      &request,
                                      TRUE,
                                      probe_completion,
                                      &probe,
                                      probe_drain,
                                      &probe),
    ==,
    GOODIX5135_REQUEST_COMPLETION_CANCELLED);

  g_assert_cmpuint (probe.completion, ==,
                    GOODIX5135_REQUEST_COMPLETION_CANCELLED);
  g_assert_cmpuint (io.pending, ==, 0);

  /*
   * Cancellation callback raced ahead of deactivate; lifecycle is still
   * running, so the drain observer correctly does not finish anything yet.
   */
  g_assert_false (probe.drain_saw_finishable);

  goodix5135_io_stop (&io);

  g_assert_true (goodix5135_io_can_finish_stop (&io));
}

static void
test_cancel_after_stop_finishes_drain (void)
{
  Goodix5135IoLifecycle io;
  Goodix5135Request request;
  DispatchProbe probe = { 0 };

  goodix5135_io_init (&io);
  goodix5135_request_init (&request);

  g_assert_true (goodix5135_io_start (&io));

  g_assert_true (
    goodix5135_request_begin (&io,
                              &request,
                              GOODIX5135_REQUEST_BULK_IN,
                              GOODIX5135_EP_IN,
                              64,
                              1000));

  goodix5135_io_stop (&io);

  g_assert_false (goodix5135_io_can_finish_stop (&io));

  probe.io = &io;

  g_assert_cmpuint (
    goodix5135_async_dispatch_finish (&io,
                                      &request,
                                      TRUE,
                                      probe_completion,
                                      &probe,
                                      probe_drain,
                                      &probe),
    ==,
    GOODIX5135_REQUEST_COMPLETION_CANCELLED);

  g_assert_cmpuint (probe.completion_sequence, ==, 1);
  g_assert_cmpuint (probe.drain_sequence, ==, 2);
  g_assert_true (probe.drain_saw_finishable);
  g_assert_cmpuint (io.pending, ==, 0);
}

static void
test_stale_after_stop_finishes_drain (void)
{
  Goodix5135IoLifecycle io;
  Goodix5135Request request;
  DispatchProbe probe = { 0 };

  goodix5135_io_init (&io);
  goodix5135_request_init (&request);

  g_assert_true (goodix5135_io_start (&io));

  g_assert_true (
    goodix5135_request_begin (&io,
                              &request,
                              GOODIX5135_REQUEST_BULK_OUT,
                              GOODIX5135_EP_OUT,
                              32,
                              500));

  goodix5135_io_stop (&io);

  probe.io = &io;

  g_assert_cmpuint (
    goodix5135_async_dispatch_finish (&io,
                                      &request,
                                      FALSE,
                                      probe_completion,
                                      &probe,
                                      probe_drain,
                                      &probe),
    ==,
    GOODIX5135_REQUEST_COMPLETION_STALE);

  g_assert_cmpuint (probe.completion, ==,
                    GOODIX5135_REQUEST_COMPLETION_STALE);
  g_assert_true (probe.drain_saw_finishable);
}

static void
test_result_policy (void)
{
  g_autoptr(GError) transport_error = NULL;

  transport_error =
    g_error_new_literal (
      g_quark_from_static_string ("goodix5135-test-transport-error"),
      1,
      "synthetic transport failure");

  /*
   * The one and only success combination.
   */
  g_assert_true (
    goodix5135_async_result_can_advance (
      GOODIX5135_REQUEST_COMPLETION_CURRENT,
      NULL));

  /*
   * This is the exact semantic class proven by the live hardware probe:
   * the request may still be CURRENT while the USB transport timed out.
   */
  g_assert_false (
    goodix5135_async_result_can_advance (
      GOODIX5135_REQUEST_COMPLETION_CURRENT,
      transport_error));

  /*
   * Cancellation, stale generation, and invalid completion must never
   * advance the protocol, with or without a transport error.
   */
  g_assert_false (
    goodix5135_async_result_can_advance (
      GOODIX5135_REQUEST_COMPLETION_CANCELLED,
      NULL));

  g_assert_false (
    goodix5135_async_result_can_advance (
      GOODIX5135_REQUEST_COMPLETION_CANCELLED,
      transport_error));

  g_assert_false (
    goodix5135_async_result_can_advance (
      GOODIX5135_REQUEST_COMPLETION_STALE,
      NULL));

  g_assert_false (
    goodix5135_async_result_can_advance (
      GOODIX5135_REQUEST_COMPLETION_STALE,
      transport_error));

  g_assert_false (
    goodix5135_async_result_can_advance (
      GOODIX5135_REQUEST_COMPLETION_INVALID,
      NULL));

  g_assert_false (
    goodix5135_async_result_can_advance (
      GOODIX5135_REQUEST_COMPLETION_INVALID,
      transport_error));
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func (
    "/goodix5135/async-dispatch/current-then-drain",
    test_current_completion_then_drain);

  g_test_add_func (
    "/goodix5135/async-dispatch/requeue-before-drain",
    test_completion_can_requeue_before_drain);

  g_test_add_func (
    "/goodix5135/async-dispatch/cancel-before-stop",
    test_cancel_before_stop_then_later_stop);

  g_test_add_func (
    "/goodix5135/async-dispatch/cancel-after-stop",
    test_cancel_after_stop_finishes_drain);

  g_test_add_func (
    "/goodix5135/async-dispatch/stale-after-stop",
    test_stale_after_stop_finishes_drain);

  g_test_add_func (
    "/goodix5135/async-dispatch/result-policy",
    test_result_policy);

  return g_test_run ();
}
