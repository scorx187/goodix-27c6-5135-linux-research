#!/usr/bin/env python3
from pathlib import Path

path = Path("libfprint/drivers/goodix5135/goodix5135.c")
text = path.read_text()
old = '''      if (self->capture_baseline_recheck_count >=
          GOODIX5135_CAPTURE_BASELINE_RECHECK_MAX)
        {
          goodix5135_capture_runtime_fail (
            device,
            "finger-off state did not stabilize during bounded manual FDT rechecks");

          return;
        }
'''
new = '''      if (self->capture_baseline_recheck_count >=
          GOODIX5135_CAPTURE_BASELINE_RECHECK_MAX)
        {
          /*
           * A finger can be placed again before the next manual baseline
           * has settled. This is a recoverable user-interaction condition,
           * not a fatal capture-session error.
           *
           * Move the capture runtime into the normal finger-off path and
           * ask libfprint to report REMOVE_FINGER. The existing FDT-up
           * sequence then waits for a real release before enrollment
           * continues with a fresh baseline.
           */
          fp_dbg (
            "Native FpImage stage: manual baseline remained touched; "
            "requesting finger removal before retry");

          self->capture_baseline_recheck_count = 0;
          self->capture_runtime_state =
            GOODIX5135_CAPTURE_RUNTIME_READY_FINGER_OFF;

          fpi_image_device_retry_scan (
            FP_IMAGE_DEVICE (device),
            FP_DEVICE_RETRY_REMOVE_FINGER);

          return;
        }
'''
if text.count(old) != 1:
    raise SystemExit("expected early-retouch block not found exactly once")
path.write_text(text.replace(old, new, 1))
print("V5D8_EARLY_RETOUCH_EDIT=PASS")
