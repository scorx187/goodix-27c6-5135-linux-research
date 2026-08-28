/*
 * Goodix 27c6:5135 host-side I/O lifecycle accounting.
 */

#include "goodix5135-io.h"

void
goodix5135_io_init (Goodix5135IoLifecycle *io)
{
  g_return_if_fail (io != NULL);

  io->generation = 0;
  io->pending = 0;
  io->running = FALSE;
}

gboolean
goodix5135_io_start (Goodix5135IoLifecycle *io)
{
  g_return_val_if_fail (io != NULL, FALSE);

  /*
   * Never start a new logical I/O generation while callbacks from an
   * older generation are still outstanding.
   */
  if (io->running || io->pending != 0)
    return FALSE;

  io->generation++;

  /*
   * Keep generation zero reserved as the uninitialized value even if
   * the counter eventually wraps.
   */
  if (io->generation == 0)
    io->generation = 1;

  io->running = TRUE;

  return TRUE;
}

void
goodix5135_io_stop (Goodix5135IoLifecycle *io)
{
  g_return_if_fail (io != NULL);

  /*
   * Outstanding callbacks remain accounted for. They will be treated
   * as stale by goodix5135_io_complete() because running is now FALSE.
   */
  io->running = FALSE;
}

gboolean
goodix5135_io_begin (Goodix5135IoLifecycle *io,
                     Goodix5135IoToken     *token)
{
  g_return_val_if_fail (io != NULL, FALSE);
  g_return_val_if_fail (token != NULL, FALSE);

  if (!io->running)
    return FALSE;

  if (token->outstanding)
    return FALSE;

  token->generation = io->generation;
  token->outstanding = TRUE;
  io->pending++;

  return TRUE;
}

gboolean
goodix5135_io_complete (Goodix5135IoLifecycle *io,
                        Goodix5135IoToken     *token)
{
  gboolean current;

  g_return_val_if_fail (io != NULL, FALSE);
  g_return_val_if_fail (token != NULL, FALSE);

  if (!token->outstanding)
    return FALSE;

  if (io->pending == 0)
    return FALSE;

  current =
    io->running &&
    token->generation == io->generation;

  token->outstanding = FALSE;
  io->pending--;

  return current;
}

gboolean
goodix5135_io_can_finish_stop (const Goodix5135IoLifecycle *io)
{
  g_return_val_if_fail (io != NULL, FALSE);

  return !io->running && io->pending == 0;
}
