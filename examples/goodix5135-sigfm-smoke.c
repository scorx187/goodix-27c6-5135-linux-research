/*
 * Minimal live SIGFM smoke harness for Goodix 27c6:5135.
 *
 * This helper deliberately stops after the first successful enrollment stage.
 * It does not serialize a template, save an image, or print biometric scores.
 * The driver-side guarded runtime environment must already be configured.
 */

#include <glib.h>
#include <libfprint/fprint.h>
#include <string.h>

#define TARGET_DRIVER "goodix5135"

typedef struct
{
  GCancellable *cancellable;
  gboolean      first_stage_seen;
  gboolean      stage_error_seen;
} SmokeState;

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

static void
enroll_progress_cb (FpDevice *device,
                    gint      completed_stages,
                    FpPrint  *print,
                    gpointer  user_data,
                    GError   *error)
{
  SmokeState *state = user_data;

  (void) device;
  (void) completed_stages;
  (void) print;

  if (error != NULL)
    {
      state->stage_error_seen = TRUE;
      g_print ("LIVE_SIGFM_FIRST_STAGE=RETRY_OR_FAIL\n");
      g_cancellable_cancel (state->cancellable);
      return;
    }

  if (!state->first_stage_seen)
    {
      state->first_stage_seen = TRUE;
      g_print ("LIVE_SIGFM_FIRST_STAGE=PASS\n");
      g_cancellable_cancel (state->cancellable);
    }
}

int
main (void)
{
  g_autoptr(FpContext) context = NULL;
  g_autoptr(GCancellable) cancellable = NULL;
  FpPrint *template_print = NULL;
  g_autoptr(FpPrint) result_print = NULL;
  g_autoptr(GError) error = NULL;
  GPtrArray *devices;
  FpDevice *device = NULL;
  SmokeState state = { 0 };
  gboolean opened = FALSE;

  if (!live_gates_ready ())
    {
      g_printerr ("LIVE_SIGFM_SMOKE=REFUSED_MISSING_GUARDS\n");
      return 2;
    }

  context = fp_context_new ();
  devices = fp_context_get_devices (context);

  if (devices == NULL)
    {
      g_printerr ("LIVE_SIGFM_SMOKE=NO_DEVICE_LIST\n");
      return 3;
    }

  for (guint i = 0; i < devices->len; i++)
    {
      FpDevice *candidate = g_ptr_array_index (devices, i);

      if (g_strcmp0 (fp_device_get_driver (candidate),
                     TARGET_DRIVER) == 0)
        {
          device = candidate;
          break;
        }
    }

  if (device == NULL)
    {
      g_printerr ("LIVE_SIGFM_SMOKE=GOODIX5135_NOT_FOUND\n");
      return 4;
    }

  if (!fp_device_open_sync (device, NULL, &error))
    {
      g_printerr ("LIVE_SIGFM_SMOKE=OPEN_FAIL\n");
      return 5;
    }

  opened = TRUE;
  cancellable = g_cancellable_new ();
  state.cancellable = cancellable;

  template_print = fp_print_new (device);
  fp_print_set_finger (template_print, FP_FINGER_RIGHT_INDEX);

  result_print =
    fp_device_enroll_sync (device,
                           template_print,
                           cancellable,
                           enroll_progress_cb,
                           &state,
                           &error);

  g_clear_object (&result_print);
  g_clear_error (&error);

  if (opened)
    {
      if (!fp_device_close_sync (device, NULL, &error))
        {
          g_printerr ("LIVE_SIGFM_SMOKE=CLOSE_FAIL\n");
          return 6;
        }
    }

  if (state.first_stage_seen && !state.stage_error_seen)
    {
      g_print ("LIVE_SIGFM_SMOKE=PASS\n");
      return 0;
    }

  g_printerr ("LIVE_SIGFM_SMOKE=FAIL\n");
  return 7;
}
