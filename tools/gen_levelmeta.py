#!/usr/bin/env python3
"""Extract SEUM's level metadata tables from the decompiled C# into JSON.

    python3 tools/gen_levelmeta.py <decompiled Assembly-CSharp dir> <out_dir>/levelmeta.json

The tables (names, par times, level placement, environment rotation, zones,
environment choice) live in code, not data: LevelMetadata.cs and
LevelSelector.cs. This reproduces LevelMetadata.init() and
Game.suggestedEnvrionmentForLevel() without running the game."""
import json, math, re, sys

src_dir, out = sys.argv[1], sys.argv[2]
meta = open(f"{src_dir}/LevelMetadata.cs", encoding="utf-8").read().replace("Vector3.zero", "new Vector3(0f, 0f, 0f)")
sel = open(f"{src_dir}/LevelSelector.cs", encoding="utf-8").read()


def int_array(text, name):
    m = re.search(r"\b%s\s*=\s*new int\[\d*\]\s*\{([^}]*)\}" % re.escape(name), text)
    return [int(x) for x in re.findall(r"-?\d+", m.group(1))]


def str_array(text, name):
    m = re.search(r"\b%s\s*=\s*new string\[\d*\]\s*\{([^}]*)\}" % re.escape(name), text)
    return re.findall(r'"([^"]*)"', m.group(1))


def jagged(text, name):
    """int[][] / int[][][] initialisers: returns the innermost int lists in order."""
    start = re.search(r"\b%s\s*=\s*new int\[" % re.escape(name), text).end()
    depth, i, body = 0, text.index("{", start), None
    for j in range(i, len(text)):
        if text[j] == "{": depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                body = text[i:j + 1]; break
    return [[int(x) for x in re.findall(r"-?\d+", g)] for g in re.findall(r"new int\[\d+\]\s*\{([^{}]*)\}", body)]


def euler_quat(x, y, z):
    """Unity Quaternion.Euler (degrees): Z, then X, then Y."""
    def q(ax, deg):
        h = math.radians(deg) / 2
        v = [0.0, 0.0, 0.0, math.cos(h)]; v[ax] = math.sin(h); return v
    def mul(a, b):
        ax, ay, az, aw = a; bx, by, bz, bw = b
        return [aw*bx + ax*bw + ay*bz - az*by, aw*by - ax*bz + ay*bw + az*bx,
                aw*bz + ax*by - ay*bx + az*bw, aw*bw - ax*bx - ay*by - az*bz]
    return mul(mul(q(1, y), q(0, x)), q(2, z))


FLOAT = r"(-?\d+(?:\.\d+)?(?:E-?\d+)?)f?"
rec_re = re.compile(
    r"levels\[(\d+)\]\s*=\s*new Record\(\"((?:[^\"\\]|\\.)*)\",\s*(-?\d+),\s*(-?\d+),\s*new Vector3\(" +
    FLOAT + r",\s*" + FLOAT + r",\s*" + FLOAT + r"\),\s*(Quaternion\.identity|Quaternion\.Euler\(" +
    FLOAT + r",\s*" + FLOAT + r",\s*" + FLOAT + r"\))(?:,\s*(-?\d+))?(?:,\s*(true|false))?\)")
levels = {}
for m in rec_re.finditer(meta):
    n, name, normal, uber = int(m.group(1)), m.group(2), int(m.group(3)), int(m.group(4))
    pos = [float(m.group(i)) for i in (5, 6, 7)]
    rot = [0, 0, 0, 1] if m.group(8) == "Quaternion.identity" else euler_quat(*(float(m.group(i)) for i in (9, 10, 11)))
    env_override = int(m.group(12)) if m.group(12) else -1
    keep = m.group(13) == "true"
    # Record() constructor adjusts the par times unless dontModifyTimes
    if not keep:
        uber = int(uber * 1.02) if uber >= 0 else int(normal * 0.91999996)
        normal = int(normal * 1.25)
    levels[n] = dict(name=name, normalTime=normal, uberTime=uber, position=pos, environmentRotation=rot,
                     environmentId=env_override, zone=-1, levelInZone=-1)

zones = jagged(sel, "levelMapping")
ep = jagged(sel, "epLevels")
dlc = jagged(sel, "dlcLevels")
for zi, zone in enumerate(zones):          # calculateLevelInHubIndices
    for i, lv in enumerate(zone):
        if lv in levels: levels[lv].update(zone=zi + 1, levelInZone=i + 1)
for zi, zone in enumerate(ep):
    for i, lv in enumerate(zone):
        if lv in levels: levels[lv].update(zone=zi + 1, levelInZone=i + 1)
for zi, zone in enumerate(dlc):
    for i, lv in enumerate(zone):
        if lv in levels: levels[lv].update(zone=zi + 1, levelInZone=i + 1)

environments = str_array(meta, "environments")
zone_env = str_array(sel, "zoneEnvironment")
secret = int_array(sel, "secretLevels")
helliku = int_array(sel, "hellikuLevels")
dlc_all = {lv for z in dlc for lv in z}
for n, r in levels.items():                # Game.suggestedEnvrionmentForLevel
    env = environments[0]
    if r["zone"] - 1 >= 0:
        env = zone_env[r["zone"] - 1]
    if n in dlc_all:
        env = "Hell7"
    if n in secret or n in helliku:
        env = environments[1]
    r["environment"] = env

json.dump({"levels": {str(k): v for k, v in sorted(levels.items())}, "zones": zones, "epZones": ep,
           "dlcZones": dlc, "secretLevels": secret, "hellikuLevels": helliku, "environments": environments},
          open(out, "w"), indent=1)
print(f"{len(levels)} level records, {len(zones)} zones -> {out}")
