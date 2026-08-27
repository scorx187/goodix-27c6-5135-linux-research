#!/usr/bin/env python3
"""Extract the 224-byte CFG70 static template from a local Goodix gfusb.dll.

No sensor communication or writes are performed.
The Windows binary and extracted template remain local/private.
"""

from __future__ import annotations
import argparse
import hashlib
import os
from pathlib import Path

CFG_LEN = 224
PREFIX = bytes.fromhex("70 11 74 85")
STATIC_TUPLES = {
    0x71: bytes.fromhex("5c 00 80 01"),
    0x75: bytes.fromhex("20 02 08 08"),
    0x79: bytes.fromhex("36 02 80 00"),
    0x7D: bytes.fromhex("38 02 80 00"),
    0x81: bytes.fromhex("3a 02 80 00"),
    0xAD: bytes.fromhex("82 00 80 15"),
}

def matches(buf: bytes) -> bool:
    return (
        len(buf) == CFG_LEN
        and buf.startswith(PREFIX)
        and all(buf[o:o+len(v)] == v for o, v in STATIC_TUPLES.items())
    )

def find_all(data: bytes, needle: bytes):
    start = 0
    while True:
        pos = data.find(needle, start)
        if pos < 0:
            return
        yield pos
        start = pos + 1

def private_write(path: Path, data: bytes):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "wb") as f:
        f.write(data)
    os.chmod(path, 0o600)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dll", type=Path)
    ap.add_argument("-o", "--output", type=Path, required=True)
    args = ap.parse_args()

    raw = args.dll.read_bytes()
    groups = {}
    total = 0

    for off in find_all(raw, PREFIX):
        if off + CFG_LEN > len(raw):
            continue
        candidate = raw[off:off+CFG_LEN]
        if not matches(candidate):
            continue
        total += 1
        digest = hashlib.sha256(candidate).hexdigest()
        groups.setdefault(digest, {"data": candidate, "offsets": []})["offsets"].append(off)

    if not groups:
        raise SystemExit("No structurally valid CFG70 candidate found")

    print(f"Structural occurrences : {total}")
    print(f"Unique 224-byte blobs  : {len(groups)}")

    if len(groups) != 1:
        raise SystemExit("Ambiguous CFG70 templates; refusing automatic selection")

    digest, item = next(iter(groups.items()))
    private_write(args.output, item["data"])

    print(f"Occurrences            : {len(item['offsets'])}")
    print(f"First DLL offset       : 0x{item['offsets'][0]:x}")
    print(f"Static SHA256          : {digest}")
    print(f"Saved private template : {args.output}")
    print("Sensor communication   : NONE")
    print("Sensor writes          : NONE")

if __name__ == "__main__":
    main()
