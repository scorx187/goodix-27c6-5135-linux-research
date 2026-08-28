/*
 * Host-only lifecycle tests for Goodix 27c6:5135.
 *
 * No USB operations are performed.
 */

#include <glib.h>

#include "../libfprint/drivers/goodix5135/goodix5135-io.h"

static void
test_inactive_rejects_begin (void)
{
  Goodix5135IoLifecycle io;
  Goodix5135IoToken token = { 0 };

  goodix5135_io_init (&io);

  g_assert_false (goodix5135_io_begin (&io, &token));
  g_assert_cmpuint (io.pending, ==, 0);
  g_assert_false (token.outstanding);
}

static void
test_normal_completion (void)
{
  Goodix5135IoLifecycle io;
  Goodix5135IoToken token = { 0 };

  goodix5135_io_init (&io);

  g_assert_true (goodix5135_io_start (&io));
  g_assert_true (goodix5135_io_begin (&io, &token));

  g_assert_cmpuint (io.pending, ==, 1);
  g_assert_true (token.outstanding);

  g_assert_true (goodix5135_io_complete (&io, &token));

  g_assert_cmpuint (io.pending, ==, 0);
  g_assert_false (token.outstanding);
}

static void
test_stop_makes_callback_stale (void)
{
  Goodix5135IoLifecycle io;
  Goodix5135IoToken token = { 0 };

  goodix5135_io_init (&io);

  g_assert_true (goodix5135_io_start (&io));
  g_assert_true (goodix5135_io_begin (&io, &token));

  goodix5135_io_stop (&io);

  g_assert_false (goodix5135_io_can_finish_stop (&io));

  /*
   * Completion is accounted for but is stale because the logical
   * session has already stopped.
   */
  g_assert_false (goodix5135_io_complete (&io, &token));

  g_assert_true (goodix5135_io_can_finish_stop (&io));
  g_assert_cmpuint (io.pending, ==, 0);
}

static void
test_no_new_io_after_stop (void)
{
  Goodix5135IoLifecycle io;
  Goodix5135IoToken first = { 0 };
  Goodix5135IoToken second = { 0 };

  goodix5135_io_init (&io);

  g_assert_true (goodix5135_io_start (&io));
  g_assert_true (goodix5135_io_begin (&io, &first));

  goodix5135_io_stop (&io);

  g_assert_false (goodix5135_io_begin (&io, &second));
  g_assert_cmpuint (io.pending, ==, 1);

  g_assert_false (goodix5135_io_complete (&io, &first));
  g_assert_true (goodix5135_io_can_finish_stop (&io));
}

static void
test_restart_waits_for_drain (void)
{
  Goodix5135IoLifecycle io;
  Goodix5135IoToken old_token = { 0 };
  Goodix5135IoToken new_token = { 0 };
  guint64 old_generation;

  goodix5135_io_init (&io);

  g_assert_true (goodix5135_io_start (&io));

  old_generation = io.generation;

  g_assert_true (goodix5135_io_begin (&io, &old_token));

  goodix5135_io_stop (&io);

  /*
   * New generation is forbidden until the old callback drains.
   */
  g_assert_false (goodix5135_io_start (&io));

  g_assert_false (goodix5135_io_complete (&io, &old_token));
  g_assert_true (goodix5135_io_can_finish_stop (&io));

  g_assert_true (goodix5135_io_start (&io));
  g_assert_cmpuint (io.generation, !=, old_generation);

  g_assert_true (goodix5135_io_begin (&io, &new_token));
  g_assert_true (goodix5135_io_complete (&io, &new_token));
}

static void
test_double_completion_rejected (void)
{
  Goodix5135IoLifecycle io;
  Goodix5135IoToken token = { 0 };

  goodix5135_io_init (&io);

  g_assert_true (goodix5135_io_start (&io));
  g_assert_true (goodix5135_io_begin (&io, &token));

  g_assert_true (goodix5135_io_complete (&io, &token));

  g_assert_false (goodix5135_io_complete (&io, &token));
  g_assert_cmpuint (io.pending, ==, 0);
}

static void
test_multiple_pending_drain (void)
{
  Goodix5135IoLifecycle io;
  Goodix5135IoToken a = { 0 };
  Goodix5135IoToken b = { 0 };

  goodix5135_io_init (&io);

  g_assert_true (goodix5135_io_start (&io));

  g_assert_true (goodix5135_io_begin (&io, &a));
  g_assert_true (goodix5135_io_begin (&io, &b));

  g_assert_cmpuint (io.pending, ==, 2);

  goodix5135_io_stop (&io);

  g_assert_false (goodix5135_io_complete (&io, &a));
  g_assert_cmpuint (io.pending, ==, 1);
  g_assert_false (goodix5135_io_can_finish_stop (&io));

  g_assert_false (goodix5135_io_complete (&io, &b));
  g_assert_cmpuint (io.pending, ==, 0);
  g_assert_true (goodix5135_io_can_finish_stop (&io));
}

static void
test_stop_without_pending_finishes_immediately (void)
{
  Goodix5135IoLifecycle io;

  goodix5135_io_init (&io);

  g_assert_true (goodix5135_io_start (&io));

  goodix5135_io_stop (&io);

  g_assert_true (goodix5135_io_can_finish_stop (&io));
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func (
    "/goodix5135/io/inactive-rejects-begin",
    test_inactive_rejects_begin);

  g_test_add_func (
    "/goodix5135/io/normal-completion",
    test_normal_completion);

  g_test_add_func (
    "/goodix5135/io/stop-makes-callback-stale",
    test_stop_makes_callback_stale);

  g_test_add_func (
    "/goodix5135/io/no-new-io-after-stop",
    test_no_new_io_after_stop);

  g_test_add_func (
    "/goodix5135/io/restart-waits-for-drain",
    test_restart_waits_for_drain);

  g_test_add_func (
    "/goodix5135/io/double-completion-rejected",
    test_double_completion_rejected);

  g_test_add_func (
    "/goodix5135/io/multiple-pending-drain",
    test_multiple_pending_drain);

  g_test_add_func (
    "/goodix5135/io/stop-without-pending",
    test_stop_without_pending_finishes_immediately);

  return g_test_run ();
}
