#!/usr/bin/env python3
"""Extract Goodix Driver 'Message' fields from Windows EVTX files.

Run against a read-only Windows mount. Review output before publishing because
EVTX can contain host/user-specific information.
"""

from pathlib import Path
import argparse
import html
import xml.etree.ElementTree as ET
from Evtx.Evtx import Evtx

p = argparse.ArgumentParser()
p.add_argument("log_dir", type=Path)
p.add_argument("-o", "--output", type=Path, default=Path("goodix-messages.txt"))
a = p.parse_args()

logs = sorted(set(a.log_dir.glob("*Goodix*.evtx")) | set(a.log_dir.glob("*goodix*.evtx")))
if not logs:
    raise SystemExit("no Goodix EVTX files found")

out = []
for log in logs:
    with Evtx(str(log)) as evtx:
        for record in evtx.records():
            try:
                root = ET.fromstring(record.xml())
                for elem in root.iter():
                    if elem.tag.endswith("Data") and elem.attrib.get("Name") == "Message" and elem.text:
                        out.append(f"[record={record.record_num()}] {html.unescape(elem.text).strip()}")
                        break
            except Exception:
                pass

a.output.write_text("\n".join(out), encoding="utf-8")
print("messages:", len(out))
print("saved:", a.output)
