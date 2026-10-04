/*
 * Goodix 27c6:5135 V5D10 in-memory SIGFM calibration harness.
 *
 * Enrolls the right index finger, then runs bounded genuine/impostor
 * verification trials. No image or template is serialized or persisted.
 * No SIGFM score or feature count is printed.
 */

#include <glib.h>
#include <libfprint/fprint.h>
#include <stdio.h>

#define TARGET_DRIVER "goodix5135"
#define ENROLL_STAGES_EXPECTED 30
#define VERIFY_RETRY_MAX 5
#define CLEANUP_DRAIN_MS 5000U

typedef struct
{
  gint total_stages;
} EnrollState;

typedef struct
{
  guint genuine_match;
  guint genuine_no_match;
  guint impostor_false_accept;
  guint impostor_correct_reject;
} CalibrationStats;

typedef struct
{
  const gchar *phase;
  const gchar *finger;
  gboolean expect_match;
  guint count;
} TrialSpec;

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
  gint64 deadline = g_get_monotonic_time () +
                   ((gint64) milliseconds * 1000);

  while (g_get_monotonic_time () < deadline)
    {
      while (g_main_context_pending (NULL))
        g_main_context_iteration (NULL, FALSE);

      g_usleep (10000);
    }

  while (g_main_context_pending (NULL))
    g_main_context_iteration (NULL, FALSE);
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
      g_print ("LIVE_SIGFM_ENROLL_STAGE=RETRY\n");
      fflush (stdout);
      return;
    }

  g_print ("LIVE_SIGFM_ENROLL_STAGE=%d/%d\n",
           completed_stages,
           state->total_stages);
  fflush (stdout);
}

static gboolean
run_verify_trial (FpDevice        *device,
                  FpPrint         *enrolled_print,
                  const TrialSpec *spec,
                  guint            trial_index,
                  CalibrationStats *stats)
{
  for (guint attempt = 1; attempt <= VERIFY_RETRY_MAX; attempt++)
    {
      g_autoptr(GError) error = NULL;
      g_autoptr(FpPrint) scanned_print = NULL;
      gboolean match = FALSE;
      gboolean ok;

      g_print ("LIVE_SIGFM_TRIAL=%s_%u/%u\n",
               spec->phase,
               trial_index,
               spec->count);
      g_print ("LIVE_SIGFM_PLACE_FINGER=%s\n", spec->finger);
      fflush (stdout);

      ok = fp_device_verify_sync (device,
                                  enrolled_print,
                                  NULL,
                                  NULL,
                                  NULL,
                                  &match,
                                  &scanned_print,
                                  &error);

      if (!ok)
        {
          if (error != NULL && error->domain == FP_DEVICE_RETRY)
            {
              g_print ("LIVE_SIGFM_TRIAL_RESULT=RETRY_%u/%u\n",
                       attempt,
                       VERIFY_RETRY_MAX);
              g_print ("LIVE_SIGFM_TRIAL_CLEANUP=LIFT_FINGER_AND_WAIT\n");
              fflush (stdout);
              drain_main_context_for_ms (CLEANUP_DRAIN_MS);
              continue;
            }

          g_printerr ("LIVE_SIGFM_TRIAL_RESULT=ERROR\n");
          return FALSE;
        }

      if (match)
        g_print ("LIVE_SIGFM_TRIAL_RESULT=MATCH\n");
      else
        g_print ("LIVE_SIGFM_TRIAL_RESULT=NO_MATCH\n");

      if (spec->expect_match)
        {
          if (match)
            stats->genuine_match++;
          else
            stats->genuine_no_match++;
        }
      else
        {
          if (match)
            stats->impostor_false_accept++;
          else
            stats->impostor_correct_reject++;
        }

      g_print ("LIVE_SIGFM_TRIAL_CLEANUP=LIFT_FINGER_AND_WAIT\n");
      fflush (stdout);
      drain_main_context_for_ms (CLEANUP_DRAIN_MS);
      g_print ("LIVE_SIGFM_TRIAL_CLEANUP=DRAINED\n");
      fflush (stdout);
      return TRUE;
    }

  g_printerr ("LIVE_SIGFM_TRIAL_RESULT=RETRY_LIMIT\n");
  return FALSE;
}

int
main (void)
{
  static const TrialSpec specs[] = {
    { "GENUINE_RIGHT_INDEX", "RIGHT_INDEX", TRUE, 5 },
    { "IMPOSTOR_RIGHT_MIDDLE", "RIGHT_MIDDLE", FALSE, 2 },
    { "IMPOSTOR_RIGHT_RING", "RIGHT_RING", FALSE, 2 },
    { "IMPOSTOR_LEFT_INDEX", "LEFT_INDEX", FALSE, 2 },
    { "IMPOSTOR_LEFT_MIDDLE", "LEFT_MIDDLE", FALSE, 2 },
    { "IMPOSTOR_LEFT_RING", "LEFT_RING", FALSE, 2 },
  };

  g_autoptr(FpContext) context = NULL;
  g_autoptr(FpPrint) enrolled_print = NULL;
  g_autoptr(GError) error = NULL;
  FpPrint *template_print = NULL;
  FpDevice *device = NULL;
  EnrollState enroll_state = { 0 };
  CalibrationStats stats = { 0 };
  gboolean opened = FALSE;

  if (!live_gates_ready ())
    {
      g_printerr ("LIVE_SIGFM_CALIBRATION=REFUSED_MISSING_GUARDS\n");
      return 2;
    }

  context = fp_context_new ();
  device = find_target_device (context);

  if (device == NULL)
    {
      g_printerr ("LIVE_SIGFM_CALIBRATION=GOODIX5135_NOT_FOUND\n");
      return 3;
    }

  if (!fp_device_open_sync (device, NULL, &error))
    {
      g_printerr ("LIVE_SIGFM_CALIBRATION=OPEN_FAIL\n");
      return 4;
    }

  opened = TRUE;
  enroll_state.total_stages = fp_device_get_nr_enroll_stages (device);

  if (enroll_state.total_stages != ENROLL_STAGES_EXPECTED)
    {
      g_printerr ("LIVE_SIGFM_CALIBRATION=UNEXPECTED_ENROLL_STAGE_COUNT\n");
      goto fail;
    }

  template_print = fp_print_new (device);
  fp_print_set_finger (template_print, FP_FINGER_RIGHT_INDEX);

  g_print ("LIVE_SIGFM_CALIBRATION=READY\n");
  g_print ("LIVE_SIGFM_ENROLL_FINGER=RIGHT_INDEX\n");
  g_print ("LIVE_SIGFM_ENROLL_STAGES=%d\n", enroll_state.total_stages);
  fflush (stdout);

  enrolled_print = fp_device_enroll_sync (device,
                                          template_print,
                                          NULL,
                                          enroll_progress_cb,
                                          &enroll_state,
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
  drain_main_context_for_ms (CLEANUP_DRAIN_MS);
  g_print ("LIVE_SIGFM_ENROLL_CLEANUP=DRAINED\n");
  fflush (stdout);

  for (guint s = 0; s < G_N_ELEMENTS (specs); s++)
    {
      for (guint trial = 1; trial <= specs[s].count; trial++)
        {
          if (!run_verify_trial (device,
                                 enrolled_print,
                                 &specs[s],
                                 trial,
                                 &stats))
            goto fail;
        }
    }

  g_print ("LIVE_SIGFM_CALIBRATION_GENUINE_MATCH=%u/5\n",
           stats.genuine_match);
  g_print ("LIVE_SIGFM_CALIBRATION_GENUINE_NO_MATCH=%u/5\n",
           stats.genuine_no_match);
  g_print ("LIVE_SIGFM_CALIBRATION_IMPOSTOR_FALSE_ACCEPT=%u/10\n",
           stats.impostor_false_accept);
  g_print ("LIVE_SIGFM_CALIBRATION_IMPOSTOR_CORRECT_REJECT=%u/10\n",
           stats.impostor_correct_reject);

  if (stats.impostor_false_accept > 0)
    g_print ("LIVE_SIGFM_CALIBRATION_RESULT=FAIL_FALSE_ACCEPT\n");
  else if (stats.genuine_no_match > 0)
    g_print ("LIVE_SIGFM_CALIBRATION_RESULT=NEEDS_REVIEW_FALSE_REJECT\n");
  else
    g_print ("LIVE_SIGFM_CALIBRATION_RESULT=PASS_PRELIMINARY\n");

  fflush (stdout);

  if (!fp_device_close_sync (device, NULL, &error))
    {
      g_clear_error (&error);
      g_printerr ("LIVE_SIGFM_CALIBRATION=CLOSE_FAIL\n");
      return 5;
    }

  opened = FALSE;

  if (stats.impostor_false_accept > 0)
    return 10;

  if (stats.genuine_no_match > 0)
    return 11;

  g_print ("LIVE_SIGFM_CALIBRATION=PASS\n");
  return 0;

fail:
  if (opened)
    {
      g_clear_error (&error);
      fp_device_close_sync (device, NULL, &error);
      g_clear_error (&error);
    }

  g_printerr ("LIVE_SIGFM_CALIBRATION=FAIL\n");
  return 9;
}
