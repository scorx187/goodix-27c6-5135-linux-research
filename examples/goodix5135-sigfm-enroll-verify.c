/*
 * Ephemeral full-enrollment + same-finger verification harness for
 * Goodix 27c6:5135 SIGFM development testing.
 *
 * No image or template is serialized or persisted by this helper.
 * No biometric score or feature count is printed.
 */

#include <glib.h>
#include <libfprint/fprint.h>
#include <stdio.h>
#include <string.h>

#define TARGET_DRIVER "goodix5135"

typedef struct
{
  gint     total_stages;
  gboolean stage_error_seen;
} EnrollState;

static gboolean
required_gate (const gchar *name,
               const gchar *expected)
{
  const gchar *value = g_getenv (name);

  return value != NULL &&
         *value != '\0' &&
         (expected == NULL || g_strcmp0 (value, expected) == 0);
}

static gboolean
live_gates_ready (void)
{
  return required_gate ("GOODIX5135_LIVE_TLS_TEST",
                        "ONE_SHOT_NATIVE_TLS") &&
         required_gate ("GOODIX5135_FPIMAGE_TEST",
                        "TWO_CAPTURE_LIFECYCLE") &&
         required_gate ("GOODIX5135_LIVE_TLS_PSK_FILE", NULL) &&
         required_gate ("GOODIX5135_LIVE_FDT_SEED_FILE", NULL) &&
         required_gate ("GOODIX5135_LIVE_FDT_UP_FILE", NULL);
}

static FpDevice *
find_target_device (FpContext *context)
{
  GPtrArray *devices = fp_context_get_devices (context);

  if (devices == NULL)
    return NULL;

  for (guint i = 0; i < devices->len; i++)
    {
      FpDevice *candidate = g_ptr_array_index (devices, i);

      if (g_strcmp0 (fp_device_get_driver (candidate), TARGET_DRIVER) == 0)
        return candidate;
    }

  return NULL;
}

static void
drain_main_context_for_ms (guint milliseconds)
{
  gint64 deadline = g_get_monotonic_time () + ((gint64) milliseconds * 1000);

  while (g_get_monotonic_time () < deadline)
    {
      while (g_main_context_iteration (NULL, FALSE))
        ;

      g_usleep (10000);
    }

  while (g_main_context_iteration (NULL, FALSE))
    ;
}

static void
enroll_progress_cb (FpDevice *device,
                    gint      completed_stages,
                    FpPrint  *print,
                    gpointer  user_data,
                    GError   *error)
{
  EnrollState *state = user_data;

  (void) device;
  (void) print;

  if (error != NULL)
    {
      state->stage_error_seen = TRUE;
      g_print ("LIVE_SIGFM_ENROLL_STAGE=RETRY\n");
      fflush (stdout);
      return;
    }

  g_print ("LIVE_SIGFM_ENROLL_STAGE=%d/%d\n",
           completed_stages,
           state->total_stages);
  fflush (stdout);
}

int
main (void)
{
  g_autoptr(FpContext) context = NULL;
  g_autoptr(FpPrint) enrolled_print = NULL;
  g_autoptr(FpPrint) scanned_print = NULL;
  g_autoptr(GError) error = NULL;
  FpPrint *template_print = NULL;
  FpDevice *device = NULL;
  EnrollState state = { 0 };
  gboolean match = FALSE;
  gboolean opened = FALSE;

  if (!live_gates_ready ())
    {
      g_printerr ("LIVE_SIGFM_FULL=REFUSED_MISSING_GUARDS\n");
      return 2;
    }

  context = fp_context_new ();
  device = find_target_device (context);

  if (device == NULL)
    {
      g_printerr ("LIVE_SIGFM_FULL=GOODIX5135_NOT_FOUND\n");
      return 3;
    }

  if (!fp_device_open_sync (device, NULL, &error))
    {
      g_printerr ("LIVE_SIGFM_FULL=OPEN_FAIL\n");
      return 4;
    }

  opened = TRUE;
  state.total_stages = fp_device_get_nr_enroll_stages (device);

  if (state.total_stages != 30)
    {
      g_printerr ("LIVE_SIGFM_FULL=UNEXPECTED_ENROLL_STAGE_COUNT\n");
      goto fail;
    }

  template_print = fp_print_new (device);
  fp_print_set_finger (template_print, FP_FINGER_RIGHT_INDEX);

  g_print ("LIVE_SIGFM_ENROLL=READY\n");
  g_print ("LIVE_SIGFM_ENROLL_STAGES=%d\n", state.total_stages);
  fflush (stdout);

  enrolled_print =
    fp_device_enroll_sync (device,
                           template_print,
                           NULL,
                           enroll_progress_cb,
                           &state,
                           &error);

  if (enrolled_print == NULL || error != NULL)
    {
      g_clear_error (&error);
      g_printerr ("LIVE_SIGFM_ENROLL=FAIL\n");
      goto fail;
    }

  g_print ("LIVE_SIGFM_ENROLL=PASS\n");
  g_print ("LIVE_SIGFM_ENROLL_CLEANUP=LIFT_FINGER_AND_WAIT\n");
  fflush (stdout);

  /* Enrollment completion can precede final finger-off cleanup. */
  drain_main_context_for_ms (5000U);

  g_print ("LIVE_SIGFM_ENROLL_CLEANUP=DRAINED\n");
  g_print ("LIVE_SIGFM_VERIFY=READY_SAME_FINGER\n");
  fflush (stdout);

  if (!fp_device_verify_sync (device,
                              enrolled_print,
                              NULL,
                              NULL,
                              NULL,
                              &match,
                              &scanned_print,
                              &error))
    {
      g_clear_error (&error);
      g_printerr ("LIVE_SIGFM_VERIFY=ERROR\n");
      goto fail;
    }

  if (match)
    g_print ("LIVE_SIGFM_VERIFY=SAME_FINGER_MATCH\n");
  else
    g_print ("LIVE_SIGFM_VERIFY=SAME_FINGER_NO_MATCH\n");

  g_print ("LIVE_SIGFM_VERIFY_CLEANUP=LIFT_FINGER_AND_WAIT\n");
  fflush (stdout);

  /* Verification result can also precede the final finger-off cleanup. */
  drain_main_context_for_ms (5000U);

  g_print ("LIVE_SIGFM_VERIFY_CLEANUP=DRAINED\n");
  fflush (stdout);

  if (!fp_device_close_sync (device, NULL, &error))
    {
      g_clear_error (&error);
      g_printerr ("LIVE_SIGFM_FULL=CLOSE_FAIL\n");
      return 5;
    }

  opened = FALSE;

  if (!match)
    {
      g_printerr ("LIVE_SIGFM_FULL=FAIL\n");
      return 6;
    }

  g_print ("LIVE_SIGFM_FULL=PASS\n");
  return 0;

fail:
  if (opened)
    {
      g_clear_error (&error);
      fp_device_close_sync (device, NULL, &error);
      g_clear_error (&error);
    }

  g_printerr ("LIVE_SIGFM_FULL=FAIL\n");
  return 7;
}
