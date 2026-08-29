/*
 * Goodix 27c6:5135 pre-command receive-queue cleanup policy.
 *
 * This module is host-side policy only. It contains no USB operations and
 * has no dependency on GUsb error domains.
 */

#pragma once

#include <glib.h>

typedef enum
{
  GOODIX5135_QUEUE_CLEANUP_IDLE = 0,
  GOODIX5135_QUEUE_CLEANUP_WAIT_READ,
  GOODIX5135_QUEUE_CLEANUP_DONE,
  GOODIX5135_QUEUE_CLEANUP_FAILED,
} Goodix5135QueueCleanupState;

typedef enum
{
  GOODIX5135_QUEUE_READ_DATA = 0,
  GOODIX5135_QUEUE_READ_EMPTY_TIMEOUT,
  GOODIX5135_QUEUE_READ_FATAL,
} Goodix5135QueueReadResult;

typedef enum
{
  GOODIX5135_QUEUE_ACTION_FAILED = 0,
  GOODIX5135_QUEUE_ACTION_READ_AGAIN,
  GOODIX5135_QUEUE_ACTION_DONE,
} Goodix5135QueueCleanupAction;

typedef struct
{
  Goodix5135QueueCleanupState state;
  guint                       discarded_packets;
  guint                       max_discarded_packets;
} Goodix5135QueueCleanup;

void goodix5135_queue_cleanup_init (
  Goodix5135QueueCleanup *cleanup);

Goodix5135QueueCleanupAction
goodix5135_queue_cleanup_begin (
  Goodix5135QueueCleanup *cleanup,
  guint                   max_discarded_packets);

Goodix5135QueueCleanupAction
goodix5135_queue_cleanup_read_complete (
  Goodix5135QueueCleanup    *cleanup,
  Goodix5135QueueReadResult  result);
