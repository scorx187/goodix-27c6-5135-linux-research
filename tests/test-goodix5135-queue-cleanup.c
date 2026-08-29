#include <glib.h>

#include "../libfprint/drivers/goodix5135/goodix5135-queue-cleanup.h"

static void
test_initial_state (void)
{
  Goodix5135QueueCleanup cleanup;

  goodix5135_queue_cleanup_init (&cleanup);

  g_assert_cmpint (
    cleanup.state,
    ==,
    GOODIX5135_QUEUE_CLEANUP_IDLE);
  g_assert_cmpuint (cleanup.discarded_packets, ==, 0);
  g_assert_cmpuint (cleanup.max_discarded_packets, ==, 0);
}

static void
test_immediate_timeout (void)
{
  Goodix5135QueueCleanup cleanup;

  goodix5135_queue_cleanup_init (&cleanup);

  g_assert_cmpint (
    goodix5135_queue_cleanup_begin (&cleanup, 4),
    ==,
    GOODIX5135_QUEUE_ACTION_READ_AGAIN);

  g_assert_cmpint (
    goodix5135_queue_cleanup_read_complete (
      &cleanup,
      GOODIX5135_QUEUE_READ_EMPTY_TIMEOUT),
    ==,
    GOODIX5135_QUEUE_ACTION_DONE);

  g_assert_cmpint (
    cleanup.state,
    ==,
    GOODIX5135_QUEUE_CLEANUP_DONE);
  g_assert_cmpuint (cleanup.discarded_packets, ==, 0);
}

static void
test_one_stale_then_timeout (void)
{
  Goodix5135QueueCleanup cleanup;

  goodix5135_queue_cleanup_init (&cleanup);

  g_assert_cmpint (
    goodix5135_queue_cleanup_begin (&cleanup, 4),
    ==,
    GOODIX5135_QUEUE_ACTION_READ_AGAIN);

  g_assert_cmpint (
    goodix5135_queue_cleanup_read_complete (
      &cleanup,
      GOODIX5135_QUEUE_READ_DATA),
    ==,
    GOODIX5135_QUEUE_ACTION_READ_AGAIN);

  g_assert_cmpuint (cleanup.discarded_packets, ==, 1);

  g_assert_cmpint (
    goodix5135_queue_cleanup_read_complete (
      &cleanup,
      GOODIX5135_QUEUE_READ_EMPTY_TIMEOUT),
    ==,
    GOODIX5135_QUEUE_ACTION_DONE);

  g_assert_cmpint (
    cleanup.state,
    ==,
    GOODIX5135_QUEUE_CLEANUP_DONE);
}

static void
test_multiple_stale_under_bound (void)
{
  Goodix5135QueueCleanup cleanup;

  goodix5135_queue_cleanup_init (&cleanup);

  g_assert_cmpint (
    goodix5135_queue_cleanup_begin (&cleanup, 4),
    ==,
    GOODIX5135_QUEUE_ACTION_READ_AGAIN);

  for (guint i = 0; i < 3; i++)
    {
      g_assert_cmpint (
        goodix5135_queue_cleanup_read_complete (
          &cleanup,
          GOODIX5135_QUEUE_READ_DATA),
        ==,
        GOODIX5135_QUEUE_ACTION_READ_AGAIN);
    }

  g_assert_cmpuint (cleanup.discarded_packets, ==, 3);

  g_assert_cmpint (
    goodix5135_queue_cleanup_read_complete (
      &cleanup,
      GOODIX5135_QUEUE_READ_EMPTY_TIMEOUT),
    ==,
    GOODIX5135_QUEUE_ACTION_DONE);
}

static void
test_bound_fails_closed (void)
{
  Goodix5135QueueCleanup cleanup;

  goodix5135_queue_cleanup_init (&cleanup);

  g_assert_cmpint (
    goodix5135_queue_cleanup_begin (&cleanup, 3),
    ==,
    GOODIX5135_QUEUE_ACTION_READ_AGAIN);

  g_assert_cmpint (
    goodix5135_queue_cleanup_read_complete (
      &cleanup,
      GOODIX5135_QUEUE_READ_DATA),
    ==,
    GOODIX5135_QUEUE_ACTION_READ_AGAIN);

  g_assert_cmpint (
    goodix5135_queue_cleanup_read_complete (
      &cleanup,
      GOODIX5135_QUEUE_READ_DATA),
    ==,
    GOODIX5135_QUEUE_ACTION_READ_AGAIN);

  g_assert_cmpint (
    goodix5135_queue_cleanup_read_complete (
      &cleanup,
      GOODIX5135_QUEUE_READ_DATA),
    ==,
    GOODIX5135_QUEUE_ACTION_FAILED);

  g_assert_cmpuint (cleanup.discarded_packets, ==, 3);
  g_assert_cmpint (
    cleanup.state,
    ==,
    GOODIX5135_QUEUE_CLEANUP_FAILED);
}

static void
test_fatal_fails_closed (void)
{
  Goodix5135QueueCleanup cleanup;

  goodix5135_queue_cleanup_init (&cleanup);

  g_assert_cmpint (
    goodix5135_queue_cleanup_begin (&cleanup, 4),
    ==,
    GOODIX5135_QUEUE_ACTION_READ_AGAIN);

  g_assert_cmpint (
    goodix5135_queue_cleanup_read_complete (
      &cleanup,
      GOODIX5135_QUEUE_READ_FATAL),
    ==,
    GOODIX5135_QUEUE_ACTION_FAILED);

  g_assert_cmpint (
    cleanup.state,
    ==,
    GOODIX5135_QUEUE_CLEANUP_FAILED);
}

static void
test_invalid_order_fails_closed (void)
{
  Goodix5135QueueCleanup cleanup;

  goodix5135_queue_cleanup_init (&cleanup);

  g_assert_cmpint (
    goodix5135_queue_cleanup_read_complete (
      &cleanup,
      GOODIX5135_QUEUE_READ_EMPTY_TIMEOUT),
    ==,
    GOODIX5135_QUEUE_ACTION_FAILED);

  g_assert_cmpint (
    cleanup.state,
    ==,
    GOODIX5135_QUEUE_CLEANUP_FAILED);
}

static void
test_zero_bound_rejected (void)
{
  Goodix5135QueueCleanup cleanup;

  goodix5135_queue_cleanup_init (&cleanup);

  g_assert_cmpint (
    goodix5135_queue_cleanup_begin (&cleanup, 0),
    ==,
    GOODIX5135_QUEUE_ACTION_FAILED);

  g_assert_cmpint (
    cleanup.state,
    ==,
    GOODIX5135_QUEUE_CLEANUP_FAILED);
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func (
    "/goodix5135/queue-cleanup/initial",
    test_initial_state);

  g_test_add_func (
    "/goodix5135/queue-cleanup/immediate-timeout",
    test_immediate_timeout);

  g_test_add_func (
    "/goodix5135/queue-cleanup/one-stale-timeout",
    test_one_stale_then_timeout);

  g_test_add_func (
    "/goodix5135/queue-cleanup/multiple-under-bound",
    test_multiple_stale_under_bound);

  g_test_add_func (
    "/goodix5135/queue-cleanup/bound-fails-closed",
    test_bound_fails_closed);

  g_test_add_func (
    "/goodix5135/queue-cleanup/fatal-fails-closed",
    test_fatal_fails_closed);

  g_test_add_func (
    "/goodix5135/queue-cleanup/invalid-order",
    test_invalid_order_fails_closed);

  g_test_add_func (
    "/goodix5135/queue-cleanup/zero-bound",
    test_zero_bound_rejected);

  return g_test_run ();
}
