#!/usr/bin/env python3
"""Read every MonoBehaviour with the generated type trees and report, per
script class, how many fail (a failure means the generated layout doesn't
match the stored bytes exactly).   python3 check_scripts.py <Seum_Data>"""
import os, sys
from collections import Counter
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import unity_env, export

w = export.World(unity_env.load(sys.argv[1]))
ok, bad, sample = Counter(), Counter(), {}
for f in w.files.values():
    for pid, o in f.objects.items():
        if o.type.name != "MonoBehaviour":
            continue
        try:
            p = o.read(check_read=False).m_Script
            cls = w.script_name(o, {"m_Script": {"m_FileID": p.m_FileID, "m_PathID": p.m_PathID}}) or "<missing script>"
        except Exception:
            cls = "<unreadable header>"
        try:
            t = o.read_typetree()
            ok[cls] += 1
            sample.setdefault(cls, t)
        except Exception:
            bad[cls] += 1
print(f"read OK: {sum(ok.values())}   failed: {sum(bad.values())}")
for k, v in bad.most_common():
    print(f"  FAIL {v:5d} {k}")
for k in sys.argv[2:]:
    print(k, {a: b for a, b in sample.get(k, {}).items() if not a.startswith("m_")})
