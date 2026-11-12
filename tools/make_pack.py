#!/usr/bin/env python3
"""Copy a subset of an exported data set (some levels + everything they need)
into a folder/zip that goes to ux0:data/seum/ on the Vita.

    python3 tools/make_pack.py <exported dir> <pack dir> --levels 1,9,61 [--zip]

Without --levels, copies everything the viewer uses (no audio yet)."""
import argparse, json, os, re, shutil, zipfile

ap = argparse.ArgumentParser()
ap.add_argument("src"); ap.add_argument("dst")
ap.add_argument("--levels")
ap.add_argument("--zip", action="store_true")
a = ap.parse_args()

man = json.load(open(os.path.join(a.src, "manifest.json")))
meta = json.load(open(os.path.join(a.src, "levelmeta.json")))
levels = [int(x) for x in a.levels.split(",")] if a.levels else sorted(int(k) for k in man["levels"])
files = {"manifest.json", "levelmeta.json"}
tex = set()


def add_part(base):
    for ext in (".json", ".smsh", ".sap"):
        if os.path.exists(os.path.join(a.src, base + ext)):
            files.add(base + ext)
    if os.path.exists(os.path.join(a.src, base + ".json")):
        tex.update(re.findall(r'"tex":"([^"]+)"', open(os.path.join(a.src, base + ".json")).read()))


add_part("scenes/game")
envs = set()
for n in levels:
    add_part(f"levels/level_{n}")
    envs.add(meta["levels"].get(str(n), {}).get("environment", "Hell1"))
for e in envs:
    add_part(f"prefabs/environment/{e.lower()}")
    sky = man.get("resourceMaterials", {}).get(f"environment/materials/skybox{e.lower()}")
    if sky:
        tex.update(t["tex"] for t in sky["textures"].values() if t.get("tex"))
for t in tex:
    info = man["textures"].get(t)
    if info and "file" in info and "error" not in info:
        files.add(info["file"])

total = 0
for f in sorted(files):
    s, d = os.path.join(a.src, f), os.path.join(a.dst, f)
    os.makedirs(os.path.dirname(d), exist_ok=True)
    shutil.copyfile(s, d)
    total += os.path.getsize(d)
print(f"{len(files)} files, {total / 2**20:.1f} MiB, levels {levels}, environments {sorted(envs)}")
if a.zip:
    z = a.dst.rstrip("/") + ".zip"
    with zipfile.ZipFile(z, "w", zipfile.ZIP_DEFLATED) as zf:
        for f in sorted(files):
            zf.write(os.path.join(a.dst, f), f)
    print(f"{z}: {os.path.getsize(z) / 2**20:.1f} MiB")
