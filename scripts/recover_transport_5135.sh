#!/usr/bin/env bash
set -euo pipefail

DEV="$({
  for d in /sys/bus/usb/devices/*; do
    [[ -f "$d/idVendor" && -f "$d/idProduct" ]] || continue
    if [[ "$(cat "$d/idVendor")" == "27c6" && "$(cat "$d/idProduct")" == "5135" ]]; then
      printf '%s\n' "$d"
      break
    fi
  done
})"

if [[ -z "${DEV:-}" ]]; then
  echo "27c6:5135 not found" >&2
  exit 1
fi

echo "Recovering USB transport through sysfs authorization toggle: $DEV"
echo 0 | sudo tee "$DEV/authorized" >/dev/null
sleep 2
echo 1 | sudo tee "$DEV/authorized" >/dev/null
sleep 2
echo "USB authorization recovery complete. This does not prove runtime config was cleared."
