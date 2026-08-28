/*
 * Goodix 27c6:5135 host-side I/O lifecycle accounting.
 *
 * This module performs no device I/O.
 */

#pragma once

#include <glib.h>

typedef struct
{
  guint64  generation;
  guint    pending;
  gboolean running;
} Goodix5135IoLifecycle;

typedef struct
{
  guint64  generation;
  gboolean outstanding;
} Goodix5135IoToken;

void     goodix5135_io_init            (Goodix5135IoLifecycle *io);

gboolean goodix5135_io_start           (Goodix5135IoLifecycle *io);

void     goodix5135_io_stop            (Goodix5135IoLifecycle *io);

gboolean goodix5135_io_begin           (Goodix5135IoLifecycle *io,
                                        Goodix5135IoToken     *token);

gboolean goodix5135_io_complete        (Goodix5135IoLifecycle *io,
                                        Goodix5135IoToken     *token);

gboolean goodix5135_io_can_finish_stop (const Goodix5135IoLifecycle *io);
