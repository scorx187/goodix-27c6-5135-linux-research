/*
 * Goodix 27c6:5135 pre-command receive-queue cleanup policy.
 *
 * No USB operation is performed here. The runtime is responsible for
 * mapping a completed USB read into DATA, EMPTY_TIMEOUT, or FATAL.
 */

#include "goodix5135-queue-cleanup.h"

static Goodix5135QueueCleanupAction
goodix5135_queue_cleanup_fail (
  Goodix5135QueueCleanup *cleanup)
{
  if (cleanup != NULL)
    cleanup->state = GOODIX5135_QUEUE_CLEANUP_FAILED;

  return GOODIX5135_QUEUE_ACTION_FAILED;
}

void
goodix5135_queue_cleanup_init (
  Goodix5135QueueCleanup *cleanup)
{
  g_return_if_fail (cleanup != NULL);

  cleanup->state =
    GOODIX5135_QUEUE_CLEANUP_IDLE;
  cleanup->discarded_packets = 0;
  cleanup->max_discarded_packets = 0;
}

Goodix5135QueueCleanupAction
goodix5135_queue_cleanup_begin (
  Goodix5135QueueCleanup *cleanup,
  guint                   max_discarded_packets)
{
  if (cleanup == NULL)
    return GOODIX5135_QUEUE_ACTION_FAILED;

  if (cleanup->state !=
      GOODIX5135_QUEUE_CLEANUP_IDLE)
    return goodix5135_queue_cleanup_fail (
      cleanup);

  /*
   * A zero bound would either make cleanup meaningless or invite an
   * unbounded implementation elsewhere. Require an explicit finite bound.
   */
  if (max_discarded_packets == 0)
    return goodix5135_queue_cleanup_fail (
      cleanup);

  cleanup->discarded_packets = 0;
  cleanup->max_discarded_packets =
    max_discarded_packets;
  cleanup->state =
    GOODIX5135_QUEUE_CLEANUP_WAIT_READ;

  return GOODIX5135_QUEUE_ACTION_READ_AGAIN;
}

Goodix5135QueueCleanupAction
goodix5135_queue_cleanup_read_complete (
  Goodix5135QueueCleanup    *cleanup,
  Goodix5135QueueReadResult  result)
{
  if (cleanup == NULL)
    return GOODIX5135_QUEUE_ACTION_FAILED;

  if (cleanup->state !=
      GOODIX5135_QUEUE_CLEANUP_WAIT_READ)
    return goodix5135_queue_cleanup_fail (
      cleanup);

  switch (result)
    {
    case GOODIX5135_QUEUE_READ_EMPTY_TIMEOUT:
      /*
       * The reference implementation treats a 100 ms USB timeout as the
       * terminal proof that no more stale reply data is queued.
       */
      cleanup->state =
        GOODIX5135_QUEUE_CLEANUP_DONE;
      return GOODIX5135_QUEUE_ACTION_DONE;

    case GOODIX5135_QUEUE_READ_DATA:
      cleanup->discarded_packets++;

      /*
       * Fail closed once the hard consumption budget is exhausted.
       *
       * At this point we have consumed exactly max_discarded_packets stale
       * packets without observing the queue-empty timeout.
       *
       * A further read could consume packet N+1, which would exceed the
       * caller's stale-data consumption budget, so no final probe is issued.
       */
      if (cleanup->discarded_packets >=
          cleanup->max_discarded_packets)
        return goodix5135_queue_cleanup_fail (
          cleanup);

      return GOODIX5135_QUEUE_ACTION_READ_AGAIN;

    case GOODIX5135_QUEUE_READ_FATAL:
    default:
      return goodix5135_queue_cleanup_fail (
        cleanup);
    }
}
