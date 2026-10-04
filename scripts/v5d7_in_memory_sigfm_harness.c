/*
 * Goodix5135 V5D7 controlled live SIGFM harness.
 *
 * Privacy properties:
 * - enrollment print remains in memory only;
 * - no print serialization or local storage;
 * - no image save/export;
 * - no exact SIGFM scores or keypoint/minutiae counts;
 * - output is lifecycle/progress plus MATCH/NO_MATCH only.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <libfprint/fprint.h>

static void
enroll_progress (FpDevice *device,
                 gint      completed_stages,
                 FpPrint  *print,
                 gpointer  user_data,
                 GError   *error)
{
  (void) print;
  (void) user_data;

  if (error)
    {
      g_print ("ENROLL_STAGE_RETRY=%d/%d\n",
               completed_stages,
               fp_device_get_nr_enroll_stages (device));
      return;
    }

  g_print ("ENROLL_STAGE_PASS=%d/%d\n",
           completed_stages,
           fp_device_get_nr_enroll_stages (device));
}

static void
match_report (FpDevice *device,
              FpPrint  *match,
              FpPrint  *print,
              gpointer  user_data,
              GError   *error)
{
  (void) device;
  (void) print;
  (void) user_data;

  if (error)
    {
      g_print ("VERIFY_REPORT=RETRY\n");
      return;
    }

  g_print ("VERIFY_REPORT=%s\n",
           match ? "MATCH" : "NO_MATCH");
}

int
main (void)
{
  g_autoptr(FpContext) ctx = NULL;
  g_autoptr(GError) error = NULL;
  GPtrArray *devices;
  FpDevice *dev = NULL;
  FpPrint *template_print = NULL;
  FpPrint *enrolled = NULL;
  FpPrint *probe = NULL;
  gboolean verified = FALSE;
  gboolean match = FALSE;
  int rc = EXIT_FAILURE;

  ctx = fp_context_new ();
  devices = fp_context_get_devices (ctx);

  if (devices == NULL)
    {
      g_print ("V5D7_RESULT=NO_DEVICE_ARRAY\n");
      return EXIT_FAILURE;
    }

  for (guint i = 0; i < devices->len; i++)
    {
      FpDevice *candidate = g_ptr_array_index (devices, i);

      if (g_strcmp0 (fp_device_get_driver (candidate),
                     "goodix5135") == 0)
        {
          dev = candidate;
          break;
        }
    }

  if (dev == NULL)
    {
      g_print ("V5D7_RESULT=GOODIX5135_NOT_FOUND\n");
      return EXIT_FAILURE;
    }

  if (!fp_device_open_sync (dev, NULL, &error))
    {
      g_print ("V5D7_RESULT=OPEN_FAILED\n");
      return EXIT_FAILURE;
    }

  g_print ("V5D7_OPEN=PASS\n");
  g_print ("V5D7_ENROLL_STAGES=%d\n",
           fp_device_get_nr_enroll_stages (dev));
  g_print ("V5D7_ACTION=ENROLL_IN_MEMORY\n");

  template_print = fp_print_new (dev);
  fp_print_set_finger (template_print,
                       FP_FINGER_RIGHT_INDEX);
  fp_print_set_username (template_print,
                         g_get_user_name ());

  enrolled = fp_device_enroll_sync (dev,
                                    template_print,
                                    NULL,
                                    enroll_progress,
                                    NULL,
                                    &error);

  /* template_print has transfer-floating semantics for enroll. */
  template_print = NULL;

  if (enrolled == NULL)
    {
      g_print ("V5D7_RESULT=ENROLL_FAILED\n");
      goto out;
    }

  g_print ("V5D7_ENROLL=PASS\n");
  g_print ("V5D7_ACTION=VERIFY_IN_MEMORY\n");

  verified = fp_device_verify_sync (dev,
                                    enrolled,
                                    NULL,
                                    match_report,
                                    NULL,
                                    &match,
                                    &probe,
                                    &error);

  if (!verified)
    {
      g_print ("V5D7_RESULT=VERIFY_ERROR\n");
      goto out;
    }

  g_print ("V5D7_RESULT=%s\n",
           match ? "VERIFY_MATCH" : "VERIFY_NO_MATCH");

  rc = match ? EXIT_SUCCESS : 2;

out:
  g_clear_object (&probe);
  g_clear_object (&enrolled);
  g_clear_error (&error);

  if (!fp_device_close_sync (dev, NULL, &error))
    {
      g_print ("V5D7_CLOSE=FAIL\n");
      g_clear_error (&error);
      return 3;
    }

  g_print ("V5D7_CLOSE=PASS\n");
  return rc;
}
