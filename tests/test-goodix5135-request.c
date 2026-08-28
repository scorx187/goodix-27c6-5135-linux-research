/*
 * Host-only request bookkeeping tests for Goodix 27c6:5135.
 *
 * No USB operation is performed.
 */

#include <glib.h>

#include "../libfprint/drivers/goodix5135/goodix5135.h"
#include "../libfprint/drivers/goodix5135/goodix5135-io.h"
#include "../libfprint/drivers/goodix5135/goodix5135-request.h"

static void
test_init (void)
{
  Goodix5135Request request;

  goodix5135_request_init (&request);

  g_assert_false (request.in_flight);
  g_assert_false (request.cancel_requested);
  g_assert_false (request.token.outstanding);
  g_assert_cmpuint (request.kind, ==, GOODIX5135_REQUEST_NONE);
  g_assert_cmpuint (request.length, ==, 0);
}

static void
test_inactive_rejects_begin (void)
{
  Goodix5135IoLifecycle io;
  Goodix5135Request request;

  goodix5135_io_init (&io);
  goodix5135_request_init (&request);

  g_assert_false (
    goodix5135_request_begin (&io,
                              &request,
                              GOODIX5135_REQUEST_BULK_IN,
                              GOODIX5135_EP_IN,
                              64,
                              1000));

  g_assert_cmpuint (io.pending, ==, 0);
}

static void
test_bulk_in (void)
{
  Goodix5135IoLifecycle io;
  Goodix5135Request request;
  gboolean cancelled = TRUE;

  goodix5135_io_init (&io);
  goodix5135_request_init (&request);

  g_assert_true (goodix5135_io_start (&io));

  g_assert_true (
    goodix5135_request_begin (&io,
                              &request,
                              GOODIX5135_REQUEST_BULK_IN,
                              GOODIX5135_EP_IN,
                              4096,
                              1500));

  g_assert_true (request.in_flight);
  g_assert_true (request.token.outstanding);
  g_assert_cmpuint (request.token.generation, ==, io.generation);
  g_assert_cmpuint (request.endpoint, ==, GOODIX5135_EP_IN);
  g_assert_cmpuint (request.length, ==, 4096);
  g_assert_cmpuint (request.timeout_ms, ==, 1500);
  g_assert_cmpuint (io.pending, ==, 1);

  g_assert_true (
    goodix5135_request_complete (&io,
                                 &request,
                                 &cancelled));

  g_assert_false (cancelled);
  g_assert_cmpuint (io.pending, ==, 0);
  g_assert_false (request.in_flight);
}

static void
test_bulk_out (void)
{
  Goodix5135IoLifecycle io;
  Goodix5135Request request;

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

  g_assert_true (
    goodix5135_request_complete (&io,
                                 &request,
                                 NULL));
}

static void
test_invalid_parameters (void)
{
  Goodix5135IoLifecycle io;
  Goodix5135Request request;

  goodix5135_io_init (&io);
  goodix5135_request_init (&request);

  g_assert_true (goodix5135_io_start (&io));

  g_assert_false (
    goodix5135_request_begin (&io,
                              &request,
                              GOODIX5135_REQUEST_NONE,
                              GOODIX5135_EP_IN,
                              64,
                              1000));

  g_assert_false (
    goodix5135_request_begin (&io,
                              &request,
                              GOODIX5135_REQUEST_BULK_IN,
                              GOODIX5135_EP_OUT,
                              64,
                              1000));

  g_assert_false (
    goodix5135_request_begin (&io,
                              &request,
                              GOODIX5135_REQUEST_BULK_OUT,
                              GOODIX5135_EP_IN,
                              64,
                              1000));

  g_assert_false (
    goodix5135_request_begin (&io,
                              &request,
                              GOODIX5135_REQUEST_BULK_IN,
                              GOODIX5135_EP_IN,
                              0,
                              1000));

  g_assert_false (
    goodix5135_request_begin (&io,
                              &request,
                              GOODIX5135_REQUEST_BULK_IN,
                              GOODIX5135_EP_IN,
                              64,
                              0));

  g_assert_cmpuint (io.pending, ==, 0);
}

static void
test_double_begin_rejected (void)
{
  Goodix5135IoLifecycle io;
  Goodix5135Request request;

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

  g_assert_false (
    goodix5135_request_begin (&io,
                              &request,
                              GOODIX5135_REQUEST_BULK_IN,
                              GOODIX5135_EP_IN,
                              64,
                              1000));

  g_assert_cmpuint (io.pending, ==, 1);

  g_assert_true (
    goodix5135_request_complete (&io,
                                 &request,
                                 NULL));
}

static void
test_cancel_waits_for_completion (void)
{
  Goodix5135IoLifecycle io;
  Goodix5135Request request;
  gboolean cancelled = FALSE;

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

  g_assert_true (goodix5135_request_cancel (&request));

  /*
   * Cancellation request alone does not drain the callback.
   */
  g_assert_cmpuint (io.pending, ==, 1);

  g_assert_true (
    goodix5135_request_complete (&io,
                                 &request,
                                 &cancelled));

  g_assert_true (cancelled);
  g_assert_cmpuint (io.pending, ==, 0);
}

static void
test_stop_makes_completion_stale (void)
{
  Goodix5135IoLifecycle io;
  Goodix5135Request request;
  gboolean cancelled = FALSE;

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

  g_assert_true (goodix5135_request_cancel (&request));

  goodix5135_io_stop (&io);

  g_assert_false (goodix5135_io_can_finish_stop (&io));

  g_assert_false (
    goodix5135_request_complete (&io,
                                 &request,
                                 &cancelled));

  g_assert_true (cancelled);
  g_assert_cmpuint (io.pending, ==, 0);
  g_assert_true (goodix5135_io_can_finish_stop (&io));
}

static void
test_reuse_after_completion (void)
{
  Goodix5135IoLifecycle io;
  Goodix5135Request request;
  guint64 generation;

  goodix5135_io_init (&io);
  goodix5135_request_init (&request);

  g_assert_true (goodix5135_io_start (&io));

  generation = io.generation;

  g_assert_true (
    goodix5135_request_begin (&io,
                              &request,
                              GOODIX5135_REQUEST_BULK_OUT,
                              GOODIX5135_EP_OUT,
                              16,
                              500));

  g_assert_true (
    goodix5135_request_complete (&io,
                                 &request,
                                 NULL));

  g_assert_true (
    goodix5135_request_begin (&io,
                              &request,
                              GOODIX5135_REQUEST_BULK_IN,
                              GOODIX5135_EP_IN,
                              128,
                              1000));

  g_assert_cmpuint (request.token.generation, ==, generation);

  g_assert_true (
    goodix5135_request_complete (&io,
                                 &request,
                                 NULL));
}

static void
test_cancel_inactive_rejected (void)
{
  Goodix5135Request request;

  goodix5135_request_init (&request);

  g_assert_false (goodix5135_request_cancel (&request));
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/goodix5135/request/init",
                   test_init);

  g_test_add_func ("/goodix5135/request/inactive-rejects-begin",
                   test_inactive_rejects_begin);

  g_test_add_func ("/goodix5135/request/bulk-in",
                   test_bulk_in);

  g_test_add_func ("/goodix5135/request/bulk-out",
                   test_bulk_out);

  g_test_add_func ("/goodix5135/request/invalid-parameters",
                   test_invalid_parameters);

  g_test_add_func ("/goodix5135/request/double-begin",
                   test_double_begin_rejected);

  g_test_add_func ("/goodix5135/request/cancel-waits-for-completion",
                   test_cancel_waits_for_completion);

  g_test_add_func ("/goodix5135/request/stop-makes-completion-stale",
                   test_stop_makes_completion_stale);

  g_test_add_func ("/goodix5135/request/reuse-after-completion",
                   test_reuse_after_completion);

  g_test_add_func ("/goodix5135/request/cancel-inactive",
                   test_cancel_inactive_rejected);

  return g_test_run ();
}
