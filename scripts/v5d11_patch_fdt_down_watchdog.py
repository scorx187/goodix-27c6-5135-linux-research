#!/usr/bin/env python3
from pathlib import Path

p = Path('/home/sam/libfprint/libfprint/drivers/goodix5135/goodix5135.c')
s = p.read_text()

repls = []

def one(old, new, label):
    global s
    count = s.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected exactly one anchor, found {count}')
    s = s.replace(old, new, 1)
    repls.append(label)

one(
"  guint                                capture_down_empty_event_count;\n",
"  guint                                capture_down_empty_event_count;\n  guint                                capture_down_timeout_rearm_count;\n",
"struct-counter",
)

one(
"#define GOODIX5135_CAPTURE_DOWN_EMPTY_EVENT_MAX 8U\n#define GOODIX5135_CAPTURE_IMAGE_TIMEOUT_MS 10000U\n",
"#define GOODIX5135_CAPTURE_DOWN_EMPTY_EVENT_MAX 8U\n\n/*\n * Finger-on FDT events should arrive promptly once 0x32 is armed.\n * Treat a silent wait as a recoverable human/event-arm timeout, not as an\n * immediate fatal USB transport error.  Re-arm from a fresh manual baseline\n * a bounded number of times; real transport/protocol errors remain fatal.\n */\n#define GOODIX5135_CAPTURE_FINGER_ON_WATCHDOG_MS 10000U\n#define GOODIX5135_CAPTURE_DOWN_TIMEOUT_REARM_MAX 6U\n\n#define GOODIX5135_CAPTURE_IMAGE_TIMEOUT_MS 10000U\n",
"constants",
)

one(
"        GOODIX5135_CAPTURE_HUMAN_EVENT_TIMEOUT_MS,\n        goodix5135_capture_down_response_cb))\n",
"        GOODIX5135_CAPTURE_FINGER_ON_WATCHDOG_MS,\n        goodix5135_capture_down_response_cb))\n",
"initial-down-watchdog",
)

one(
"            GOODIX5135_CAPTURE_HUMAN_EVENT_TIMEOUT_MS,\n            goodix5135_capture_down_response_cb))\n",
"            GOODIX5135_CAPTURE_FINGER_ON_WATCHDOG_MS,\n            goodix5135_capture_down_response_cb))\n",
"empty-event-watchdog",
)

one(
"  gboolean transport_ok;\n  gboolean protocol_ok;\n\n  (void) user_data;\n\n  transport_ok =\n    self->capture_runtime_state ==\n      GOODIX5135_CAPTURE_RUNTIME_WAIT_DOWN_RESPONSE &&\n    goodix5135_in_transfer_can_parse (\n      transfer,\n      completion,\n      error);\n",
"  gboolean transport_ok;\n  gboolean protocol_ok;\n  gboolean human_wait_timed_out;\n\n  guint timeout_rearm_number;\n\n  (void) user_data;\n\n  human_wait_timed_out =\n    self->capture_runtime_state ==\n      GOODIX5135_CAPTURE_RUNTIME_WAIT_DOWN_RESPONSE &&\n    completion == GOODIX5135_REQUEST_COMPLETION_CURRENT &&\n    error != NULL &&\n    g_error_matches (\n      error,\n      G_USB_DEVICE_ERROR,\n      G_USB_DEVICE_ERROR_TIMED_OUT) &&\n    goodix5135_fpimage_test_requested () &&\n    self->active &&\n    !self->deactivating &&\n    self->state == FPI_IMAGE_DEVICE_STATE_AWAIT_FINGER_ON;\n\n  if (human_wait_timed_out)\n    {\n      g_clear_error (&error);\n\n      if (self->capture_down_timeout_rearm_count >=\n          GOODIX5135_CAPTURE_DOWN_TIMEOUT_REARM_MAX)\n        {\n          goodix5135_capture_runtime_fail (\n            device,\n            \"FDT-down finger-on wait exhausted bounded re-arms\");\n\n          return;\n        }\n\n      timeout_rearm_number =\n        self->capture_down_timeout_rearm_count + 1U;\n\n      fp_warn (\n        \"FDT-down finger-on wait timed out; re-arming detection %u/%u\",\n        timeout_rearm_number,\n        GOODIX5135_CAPTURE_DOWN_TIMEOUT_REARM_MAX);\n\n      /*\n       * The timed-out IN transfer is complete and no response bytes are\n       * being consumed.  Rebuild the normal detection chain from a fresh\n       * manual baseline rather than treating a human/event-arm timeout as\n       * a fatal transport failure.\n       */\n      goodix5135_capture_runtime_reset (self);\n\n      self->capture_down_timeout_rearm_count =\n        timeout_rearm_number;\n      self->capture_baseline_recheck_count = 0;\n\n      self->capture_baseline_settle_source =\n        g_timeout_add_full (\n          G_PRIORITY_DEFAULT,\n          GOODIX5135_CAPTURE_BASELINE_SETTLE_MS,\n          goodix5135_capture_baseline_settle_cb,\n          g_object_ref (device),\n          g_object_unref);\n\n      if (self->capture_baseline_settle_source == 0)\n        {\n          goodix5135_capture_runtime_fail (\n            device,\n            \"could not schedule FDT-down timeout re-arm\");\n        }\n\n      return;\n    }\n\n  transport_ok =\n    self->capture_runtime_state ==\n      GOODIX5135_CAPTURE_RUNTIME_WAIT_DOWN_RESPONSE &&\n    goodix5135_in_transfer_can_parse (\n      transfer,\n      completion,\n      error);\n",
"timeout-handler",
)

one(
"  self->capture_down_empty_event_count = 0;\n\n  fp_dbg (\n    \"Native capture stage: FDT-down event validated\");\n",
"  self->capture_down_empty_event_count = 0;\n  self->capture_down_timeout_rearm_count = 0;\n\n  fp_dbg (\n    \"Native capture stage: FDT-down event validated\");\n",
"successful-touch-reset",
)

one(
"    case FPI_IMAGE_DEVICE_STATE_AWAIT_FINGER_ON:\n      self->capture_baseline_recheck_count = 0;\n",
"    case FPI_IMAGE_DEVICE_STATE_AWAIT_FINGER_ON:\n      self->capture_baseline_recheck_count = 0;\n      self->capture_down_timeout_rearm_count = 0;\n",
"new-stage-reset",
)

# Initialize the new counter next to the existing down-event counter in init.
anchor = "  self->capture_down_empty_event_count = 0;\n\n  goodix5135_capture_runtime_reset (\n    self);\n"
if anchor not in s:
    raise SystemExit('init-counter: anchor missing')
s = s.replace(
    anchor,
    "  self->capture_down_empty_event_count = 0;\n  self->capture_down_timeout_rearm_count = 0;\n\n  goodix5135_capture_runtime_reset (\n    self);\n",
    1,
)
repls.append('init-counter')

p.write_text(s)
print('V5D11_PATCH=PASS')
print('PATCHED=' + ','.join(repls))
