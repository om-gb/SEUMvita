#!/usr/bin/env python3
"""Check an exported data set for internal consistency, and against the
independent per-level counts in gamedata/level_budget.txt when given.

    python3 tools/export/validate.py <out_dir> [--budget gamedata/level_budget.txt]

Exit code 0 means every check passed."""
import argparse, json, math, os, re, struct, sys
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import meshfile  # noqa: E402

DDS_BLOCK = {"DXT1": 8, "DXT5": 16}


def check_dds(path, info):
    data = open(path, "rb").read()
    if data[:4] != b"DDS ":
        return "bad magic"
    h, w, mips = struct.unpack_from("<I I", data, 12)[0], struct.unpack_from("<I", data, 16)[0], struct.unpack_from("<I", data, 28)[0]
    fourcc = data[84:88].decode("latin1")
    mips = max(1, mips)
    if (w, h) != (info["w"], info["h"]):
        return f"size {w}x{h} != manifest {info['w']}x{info['h']}"
    expect, cw, ch = 128, w, h
    for _ in range(mips):
        if info["fmt"] == "RGBA8":
            expect += cw * ch * 4
        else:
            expect += max(1, (cw + 3) // 4) * max(1, (ch + 3) // 4) * DDS_BLOCK[fourcc]
        cw, ch = max(1, cw // 2), max(1, ch // 2)
    if len(data) != expect:
        return f"{len(data)} bytes, expected {expect}"
    return None


def walk_refs(v, out):
    if isinstance(v, dict):
        for k in ("obj", "mesh", "mat", "tex", "audio", "sprite", "prefab"):
            if k in v and not isinstance(v[k], (dict, list)):
                out[k].append(v[k])
        if v.get("missing"):
            out["missing"].append(1)
        for x in v.values():
            walk_refs(x, out)
    elif isinstance(v, list):
        for x in v:
            walk_refs(x, out)


def load_budget(path):
    rows = {}
    for line in open(path):
        p = line.split()
        if p and p[0].isdigit():
            rows[int(p[0])] = dict(meshcol=int(p[3]), primcol=int(p[4]), lights=int(p[5]), psys=int(p[6]), scripts=int(p[7]))
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out_dir")
    ap.add_argument("--budget")
    a = ap.parse_args()
    man = json.load(open(os.path.join(a.out_dir, "manifest.json")))
    budget = load_budget(a.budget) if a.budget else {}
    texs, audio = man.get("textures", {}), man.get("audio", {})
    errors, notes = [], Counter()

    def err(msg):
        errors.append(msg)

    # textures
    bad_tex = set()
    for tid, info in texs.items():
        if "error" in info:
            notes["texture conversion errors"] += 1; bad_tex.add(tid); continue
        p = os.path.join(a.out_dir, info["file"])
        if not os.path.exists(p):
            err(f"texture {tid}: file missing"); continue
        e = check_dds(p, info)
        if e:
            err(f"texture {tid}: {e}")
    for aid, info in audio.items():
        if "error" in info:
            notes["audio conversion errors"] += 1
        elif not os.path.getsize(os.path.join(a.out_dir, info["file"])):
            err(f"audio {aid}: empty file")

    sets = [("levels", f"levels/level_{k}", k) for k in man["levels"]] + \
           [("prefabs", f"prefabs/{k}", k) for k in man.get("prefabs", {})]
    tris = 0
    for kind, base, key in sets:
        if man[kind][key].get("error"):
            err(f"{kind} {key}: export failed"); continue
        d = json.load(open(os.path.join(a.out_dir, base + ".json")))
        meshes = meshfile.read(os.path.join(a.out_dir, base + ".smsh"))
        if len(meshes) != d["meshCount"]:
            err(f"{base}: {len(meshes)} meshes in .smsh, json says {d['meshCount']}")
        for m in meshes:
            if m["idx"] and max(m["idx"]) >= m["vc"]:
                err(f"{base}: mesh {m['name']} index out of range")
            if not all(math.isfinite(x) for x in m["pos"]):
                err(f"{base}: mesh {m['name']} has non-finite positions")
            if sum(s[1] for s in m["subs"]) != m["ic"]:
                err(f"{base}: mesh {m['name']} submesh counts don't add up")
            tris += m["ic"] // 3
        n = len(d["objects"])
        for i, o in enumerate(d["objects"]):
            if not (-1 <= o["parent"] < i):
                err(f"{base}: object {i} has bad parent {o['parent']}")
        refs = {k: [] for k in ("obj", "mesh", "mat", "tex", "audio", "sprite", "prefab", "missing")}
        walk_refs([d["objects"], d["materials"]], refs)
        for r in refs["obj"]:
            if not 0 <= r < n: err(f"{base}: object ref {r} out of range")
        for r in refs["mesh"]:
            if not 0 <= r < len(meshes): err(f"{base}: mesh ref {r} out of range")
        for r in refs["mat"]:
            if not 0 <= r < len(d["materials"]): err(f"{base}: material ref {r} out of range")
        for r in refs["tex"]:
            if r not in texs: err(f"{base}: texture {r} not in manifest")
            elif r in bad_tex: notes["refs to unconverted textures"] += 1
        for r in refs["audio"]:
            if audio and r not in audio: err(f"{base}: audio {r} not in manifest")
        notes["unresolved refs"] += len(refs["missing"])
        if kind == "levels":
            if not man["levels"][key].get("sapBytes"):
                err(f"level {key}: no .sap data")
            b = budget.get(int(key))
            if b:
                s = d["stats"]
                got = dict(meshcol=s.get("MeshCollider", 0),
                           primcol=s.get("BoxCollider", 0) + s.get("SphereCollider", 0) + s.get("CapsuleCollider", 0),
                           lights=s.get("Light", 0), psys=s.get("ParticleSystem", 0), scripts=s.get("MonoBehaviour", 0))
                for k in b:
                    if got[k] != b[k]:
                        err(f"level {key}: {k} {got[k]} != Unity {b[k]}")
    missing = set(budget) - {int(k) for k in man["levels"]}
    if missing:
        err(f"levels in Unity data but not exported: {sorted(missing)}")

    print(f"levels {len(man['levels'])}, prefabs {len(man.get('prefabs', {}))}, textures {len(texs)}, "
          f"audio {len(audio)}, unique-mesh triangles {tris}")
    for k, v in notes.items():
        print(f"note: {k}: {v}")
    for e in errors[:50]:
        print("ERROR", e)
    print(f"{len(errors)} errors" if errors else "ALL CHECKS PASSED")
    sys.exit(1 if errors else 0)


if __name__ == "__main__":
    main()
