#!/usr/bin/env python3
"""Convert a P3595 contract configuration (JSON) to the compact binary form
d4324-embed-config-compact.h reads.  Usage: d4324-embed-config-to-compact.py d4324-embed-config.json d4324-embed-config.bin"""
import json
import struct
import sys

KINDS = {"pre": 1, "post": 2, "contract_assert": 3}
SEMANTICS = {"ignore": 1, "observe": 2, "enforce": 3, "quick_enforce": 4}


def lstr(s):
    b = s.encode()
    if len(b) > 63:
        sys.exit(f"string too long: {s!r}")
    return bytes([len(b)]) + b


def main(src, dst):
    entries = json.load(open(src))
    out = bytearray(b"P3595c\x01")
    out.append(len(entries))
    for e in entries:
        m = e.get("match", {})
        unknown = set(m) - {"kind", "namespace", "function", "location", "caller"}
        if unknown:
            sys.exit(f"unsupported match keys: {sorted(unknown)}")
        file, lines = "", []
        if "location" in m:
            file, _, spec = m["location"].rpartition(":")
            if not file or not spec[:1].isdigit():
                file, spec = m["location"], ""
            for part in filter(None, spec.split(",")):
                lo, _, hi = part.partition("-")
                lines.append((int(lo), int(hi or lo)))
        ns = m.get("namespace", "").removeprefix("::")
        fn = m.get("function", "").removeprefix("::")
        out += bytes([KINDS[m["kind"]] if "kind" in m else 0,
                      1 if m.get("caller") is True else 0,
                      SEMANTICS[e["output"]["semantic"]],
                      len(lines)])
        for lo, hi in lines:
            out += struct.pack("<II", lo, hi)
        out += lstr(ns) + lstr(fn) + lstr(file)
    open(dst, "wb").write(out)


if __name__ == "__main__":
    main(*sys.argv[1:])
