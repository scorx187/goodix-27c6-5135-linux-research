/*
 * FPrint Image - Private APIs
 * Copyright (C) 2007 Daniel Drake <dsd@gentoo.org>
 * Copyright (C) 2019 Benjamin Berg <bberg@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#define FP_COMPONENT "image"

#include "fpi-image.h"
#include "fpi-log.h"

#include <nbis.h>
#include <config.h>
#include <gio/gio.h>

#ifdef HAVE_PIXMAN
#include <pixman.h>
#endif

/**
 * SECTION: fpi-image
 * @title: Internal FpImage
 * @short_description: Internal image handling routines
 *
 * Internal image handling routines. See #FpImage for public routines.
 */

/**
 * fpi_std_sq_dev:
 * @buf: buffer (usually bitmap, one byte per pixel)
 * @size: size of @buffer
 *
 * Calculates the squared standard deviation of the individual
 * pixels in the buffer, as per the following formula:
 * |[<!-- -->
 *    mean = sum (buf[0..size]) / size
 *    sq_dev = sum ((buf[0.size] - mean) ^ 2)
 * ]|
 * This function is usually used to determine whether image
 * is empty.
 *
 * Returns: the squared standard deviation for @buffer
 */
gint
fpi_std_sq_dev (const guint8 *buf,
                gint          size)
{
  guint64 res = 0, mean = 0;
  gint i;

  for (i = 0; i < size; i++)
    mean += buf[i];

  mean /= size;

  for (i = 0; i < size; i++)
    {
      int dev = (int) buf[i] - mean;
      res += dev * dev;
    }

  return res / size;
}

/**
 * fpi_mean_sq_diff_norm:
 * @buf1: buffer (usually bitmap, one byte per pixel)
 * @buf2: buffer (usually bitmap, one byte per pixel)
 * @size: buffer size of smallest buffer
 *
 * This function calculates the normalized mean square difference of
 * two buffers, usually two lines, as per the following formula:
 * |[<!-- -->
 *    sq_diff = sum ((buf1[0..size] - buf2[0..size]) ^ 2) / size
 * ]|
 *
 * This functions is usually used to get numerical difference
 * between two images.
 *
 * Returns: the normalized mean squared difference between @buf1 and @buf2
 */
gint
fpi_mean_sq_diff_norm (const guint8 *buf1,
                       const guint8 *buf2,
                       gint          size)
{
  int res = 0, i;

  for (i = 0; i < size; i++)
    {
      int dev = (int) buf1[i] - (int) buf2[i];
      res += dev * dev;
    }

  return res / size;
}

FpImage *
fpi_image_resize (FpImage *orig_img,
                  guint    w_factor,
                  guint    h_factor)
{
#ifdef HAVE_PIXMAN
  int new_width = orig_img->width * w_factor;
  int new_height = orig_img->height * h_factor;
  pixman_image_t *orig, *resized;
  pixman_transform_t transform;
  FpImage *newimg;

  orig = pixman_image_create_bits (PIXMAN_a8, orig_img->width, orig_img->height, (uint32_t *) orig_img->data, orig_img->width);
  resized = pixman_image_create_bits (PIXMAN_a8, new_width, new_height, NULL, new_width);

  pixman_transform_init_identity (&transform);
  pixman_transform_scale (NULL, &transform, pixman_int_to_fixed (w_factor), pixman_int_to_fixed (h_factor));
  pixman_image_set_transform (orig, &transform);
  pixman_image_set_filter (orig, PIXMAN_FILTER_BILINEAR, NULL, 0);
  pixman_image_composite32 (PIXMAN_OP_SRC,
                            orig, /* src */
                            NULL, /* mask */
                            resized, /* dst */
                            0, 0, /* src x y */
                            0, 0, /* mask x y */
                            0, 0, /* dst x y */
                            new_width, new_height /* width height */
                           );

  newimg = fp_image_new (new_width, new_height);
  newimg->flags = orig_img->flags;

  memcpy (newimg->data, pixman_image_get_data (resized), new_width * new_height);

  pixman_image_unref (orig);
  pixman_image_unref (resized);

  return newimg;
#else
  fp_err ("Libfprint compiled without pixman support, impossible to resize");

  return g_object_ref (orig_img);
#endif
}


#define SIGFM_MIN_MATCHABLE_KEYPOINTS 5


typedef struct
{
  SigfmImgInfo  *sigfm_info;
  FpiImageFlags  flags;
  guint8        *image;
  gboolean       image_changed;
} FpiSigfmExtractData;


static void
fpi_sigfm_extract_data_free (FpiSigfmExtractData *data)
{
  g_clear_pointer (
    &data->sigfm_info,
    sigfm_free_info);

  if (data->image_changed)
    g_clear_pointer (
      &data->image,
      g_free);

  g_free (data);
}


G_DEFINE_AUTOPTR_CLEANUP_FUNC (
  FpiSigfmExtractData,
  fpi_sigfm_extract_data_free)


static void
fpi_sigfm_hflip (guint8 *data,
                 guint   width,
                 guint   height)
{
  for (guint y = 0;
       y < height;
       y++)
    {
      for (guint x = 0;
           x < width / 2;
           x++)
        {
          const gsize left =
            (gsize) y * width + x;

          const gsize right =
            (gsize) y * width +
            (width - 1 - x);

          const guint8 tmp =
            data[left];

          data[left] =
            data[right];

          data[right] =
            tmp;
        }
    }
}


static void
fpi_sigfm_vflip (guint8 *data,
                 guint   width,
                 guint   height)
{
  g_autofree guint8 *row =
    g_malloc (width);

  for (guint y = 0;
       y < height / 2;
       y++)
    {
      const gsize top =
        (gsize) y * width;

      const gsize bottom =
        (gsize) (height - 1 - y) *
        width;

      memcpy (
        row,
        data + top,
        width);

      memcpy (
        data + top,
        data + bottom,
        width);

      memcpy (
        data + bottom,
        row,
        width);
    }
}


static void
fpi_sigfm_invert (guint8 *data,
                  guint   width,
                  guint   height)
{
  const gsize len =
    (gsize) width * height;

  for (gsize i = 0;
       i < len;
       i++)
    data[i] =
      0xffU - data[i];
}


static void
fpi_sigfm_extract_thread (GTask        *task,
                          gpointer      source_object,
                          gpointer      task_data,
                          GCancellable *cancellable)
{
  g_autoptr(GTimer) timer = NULL;
  g_autoptr(FpiSigfmExtractData) ret_data = NULL;
  g_autoptr(GTask) thread_task =
    g_steal_pointer (&task);

  FpImage *self =
    FP_IMAGE (source_object);

  const FpiImageFlags transform_flags =
    FPI_IMAGE_H_FLIPPED |
    FPI_IMAGE_V_FLIPPED |
    FPI_IMAGE_COLORS_INVERTED;

  const FpiImageFlags normalized_flags =
    self->flags & ~transform_flags;

  guint8 *image =
    self->data;

  (void) task_data;
  (void) cancellable;

  if ((self->flags &
       transform_flags) != 0)
    image =
      g_memdup2 (
        self->data,
        (gsize) self->width *
        self->height);

  ret_data =
    g_new0 (
      FpiSigfmExtractData,
      1);

  ret_data->flags =
    normalized_flags;

  ret_data->image =
    image;

  ret_data->image_changed =
    image != self->data;

  if (self->flags &
      FPI_IMAGE_H_FLIPPED)
    fpi_sigfm_hflip (
      image,
      self->width,
      self->height);

  if (self->flags &
      FPI_IMAGE_V_FLIPPED)
    fpi_sigfm_vflip (
      image,
      self->width,
      self->height);

  if (self->flags &
      FPI_IMAGE_COLORS_INVERTED)
    fpi_sigfm_invert (
      image,
      self->width,
      self->height);

  timer =
    g_timer_new ();

  ret_data->sigfm_info =
    sigfm_extract (
      image,
      self->width,
      self->height);

  g_timer_stop (
    timer);

  fp_dbg (
    "SIGFM extraction completed in %f secs",
    g_timer_elapsed (
      timer,
      NULL));

  if (g_task_had_error (
        thread_task))
    return;

  if (ret_data->sigfm_info == NULL)
    {
      g_task_return_new_error (
        thread_task,
        G_IO_ERROR,
        G_IO_ERROR_FAILED,
        "SIGFM extraction failed");

      return;
    }

  if (sigfm_keypoints_count (
        ret_data->sigfm_info)
      < SIGFM_MIN_MATCHABLE_KEYPOINTS)
    {
      g_task_return_new_error (
        thread_task,
        G_IO_ERROR,
        G_IO_ERROR_FAILED,
        "Not enough SIGFM keypoints for matching");

      return;
    }

  g_task_return_pointer (
    thread_task,
    g_steal_pointer (&ret_data),
    (GDestroyNotify)
      fpi_sigfm_extract_data_free);
}


static gboolean
fpi_sigfm_extract_finish_data (FpImage *self,
                               GTask   *task,
                               GError **error)
{
  g_autoptr(FpiSigfmExtractData) data = NULL;

  data =
    g_task_propagate_pointer (
      task,
      error);

  if (data == NULL)
    return FALSE;

  self->flags =
    data->flags;

  if (data->image_changed)
    {
      g_clear_pointer (
        &self->data,
        g_free);

      self->data =
        g_steal_pointer (
          &data->image);
    }

  g_clear_pointer (
    &self->binarized,
    g_free);

  g_clear_pointer (
    &self->minutiae,
    g_ptr_array_unref);

  g_clear_pointer (
    &self->sigfm_info,
    sigfm_free_info);

  self->sigfm_info =
    g_steal_pointer (
      &data->sigfm_info);

  return TRUE;
}


SigfmImgInfo *
fpi_image_get_sigfm_info (FpImage *self)
{
  g_return_val_if_fail (
    FP_IS_IMAGE (self),
    NULL);

  return self->sigfm_info;
}


void
fpi_image_extract_sigfm (FpImage            *self,
                         GCancellable       *cancellable,
                         GAsyncReadyCallback callback,
                         gpointer            user_data)
{
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (
    FP_IS_IMAGE (self));

  g_return_if_fail (
    callback != NULL);

  task =
    g_task_new (
      self,
      cancellable,
      callback,
      user_data);

  g_task_set_source_tag (
    task,
    fpi_image_extract_sigfm);

  g_task_set_check_cancellable (
    task,
    TRUE);

  if (!g_atomic_int_compare_and_exchange (
        &self->detection_in_progress,
        FALSE,
        TRUE))
    {
      g_task_return_new_error (
        task,
        G_IO_ERROR,
        G_IO_ERROR_ADDRESS_IN_USE,
        "Fingerprint feature detection is already in progress");

      return;
    }

  g_task_run_in_thread (
    g_steal_pointer (&task),
    fpi_sigfm_extract_thread);
}


gboolean
fpi_image_extract_sigfm_finish (FpImage      *self,
                                GAsyncResult *result,
                                GError      **error)
{
  GTask *task;
  gboolean changed;

  g_return_val_if_fail (
    FP_IS_IMAGE (self),
    FALSE);

  g_return_val_if_fail (
    g_task_is_valid (
      result,
      self),
    FALSE);

  g_return_val_if_fail (
    g_task_get_source_tag (
      G_TASK (result))
      == fpi_image_extract_sigfm,
    FALSE);

  task =
    G_TASK (result);

  changed =
    g_atomic_int_compare_and_exchange (
      &self->detection_in_progress,
      TRUE,
      FALSE);

  g_assert (
    changed);

  if (g_task_had_error (
        task))
    {
      gpointer data =
        g_task_propagate_pointer (
          task,
          error);

      g_assert (
        data == NULL);

      return FALSE;
    }

  return
    fpi_sigfm_extract_finish_data (
      self,
      task,
      error);
}
