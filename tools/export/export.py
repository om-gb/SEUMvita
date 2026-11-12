#!/usr/bin/env python3
"""Convert a SEUM Seum_Data folder into the Vita engine's data set.

    python3 tools/export/export.py <Seum_Data> <out_dir> [--levels 1,2,5-9]
                                   [--no-textures] [--no-audio] [--jobs N]

Output layout and file formats: docs/DATA_FORMAT.md. Coordinates, units and
handedness are left exactly as Unity has them (left-handed, Y up, metres) so
ported gameplay maths stays identical.
"""
import argparse, io, json, math, os, re, struct, subprocess, sys, time, traceback
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import unity_env, meshfile, textures  # noqa: E402
from UnityPy.helpers.MeshHelper import MeshHandler  # noqa: E402

FORMAT_VERSION = 1

# --------------------------------------------------------------------------
# Object lookup and pointer resolution
# --------------------------------------------------------------------------

class World:
    def __init__(self, env):
        self.env = env
        self.files = {os.path.basename(f.name).lower(): f for f in env.assets if getattr(f, "name", None)}
        self.scripts = {}           # (file, pathid) -> class name
        self.resources = {}         # Resources.Load path -> [(file, pathid)]
        for f in self.files.values():
            for pid, o in f.objects.items():
                if o.type.name == "MonoScript":
                    d = o.read()
                    ns = d.m_Namespace
                    self.scripts[(f.name, pid)] = (ns + "." if ns else "") + d.m_ClassName
                elif o.type.name == "ResourceManager":
                    for path, ptr in o.read().m_Container:
                        tf = self.ext_file(f, ptr.m_FileID)
                        if tf is not None:
                            self.resources.setdefault(path, []).append((tf.name, ptr.m_PathID))

    def ext_file(self, f, file_id):
        if file_id == 0:
            return f
        try:
            path = f.externals[file_id - 1].path
        except IndexError:
            return None
        return self.files.get(os.path.basename(path).lower())

    def obj(self, f, file_id, path_id):
        if not path_id:
            return None
        tf = self.ext_file(f, file_id)
        if tf is None:
            return None
        return tf.objects.get(path_id)

    def roots(self, path):
        """Root GameObjects of a Resources.Load path."""
        out = []
        for fname, pid in self.resources.get(path, []):
            o = self.files[fname].objects.get(pid)
            if o is None or o.type.name != "GameObject":
                continue
            go = o.read_typetree()
            tr = self.transform_of(o, go)
            if tr is not None and not tr["m_Father"]["m_PathID"]:
                out.append(o)
        return out

    def transform_of(self, o, go):
        for c in go["m_Component"]:
            ptr = c.get("component", c)
            co = self.obj(o.assets_file, ptr["m_FileID"], ptr["m_PathID"])
            if co is not None and co.type.name in ("Transform", "RectTransform"):
                return co.read_typetree()
        return None

    def script_name(self, o, tree):
        p = tree.get("m_Script") or {}
        so = self.obj(o.assets_file, p.get("m_FileID", 0), p.get("m_PathID", 0))
        if so is None:
            return None
        return self.scripts.get((so.assets_file.name, so.path_id))


def gid(o):
    """Stable global id for an asset object."""
    return "%s:%d" % (o.assets_file.name.lower(), o.path_id)


# --------------------------------------------------------------------------
# Prefab export
# --------------------------------------------------------------------------

def v3(d): return [d["x"], d["y"], d["z"]]
def q4(d): return [d["x"], d["y"], d["z"], d["w"]]
def col(d): return [d["r"], d["g"], d["b"], d["a"]]


class PrefabExporter:
    """Flattens one prefab hierarchy into objects + components + materials +
    a mesh pool. Shared assets (textures, audio, other prefabs) are referenced
    by global id and collected on the shared Catalog."""

    def __init__(self, world, catalog):
        self.w, self.cat = world, catalog
        self.objects, self.materials = [], []
        self.mat_index = {}
        self.meshes = meshfile.MeshPool()
        self.go_index = {}      # (file, pathid) of GameObject -> object index
        self.comp_index = {}    # (file, pathid) of component -> (object idx, comp idx)
        self.stats = Counter()
        self.warnings = Counter()

    # ---- hierarchy -------------------------------------------------------
    def export(self, root):
        self._collect(root, -1)
        for i, (o, go, tr) in enumerate(self._pending):
            self.objects[i]["components"] = [c for c in (self._component(o, i, ptr) for ptr in self._comp_ptrs(go)) if c]
        return dict(objects=self.objects, materials=self.materials, meshCount=len(self.meshes.meshes))

    def _comp_ptrs(self, go):
        return [c.get("component", c) for c in go["m_Component"]]

    def _collect(self, o, parent):
        self._pending = getattr(self, "_pending", [])
        go = o.read_typetree()
        tr_obj = None
        for ptr in self._comp_ptrs(go):
            co = self.w.obj(o.assets_file, ptr["m_FileID"], ptr["m_PathID"])
            if co is not None and co.type.name in ("Transform", "RectTransform"):
                tr_obj = co
        tr = tr_obj.read_typetree() if tr_obj else None
        idx = len(self.objects)
        self.go_index[(o.assets_file.name, o.path_id)] = idx
        self.objects.append(dict(
            name=go["m_Name"], parent=parent, active=bool(go["m_IsActive"]),
            layer=go["m_Layer"], tag=go["m_Tag"],
            pos=v3(tr["m_LocalPosition"]) if tr else [0, 0, 0],
            rot=q4(tr["m_LocalRotation"]) if tr else [0, 0, 0, 1],
            scale=v3(tr["m_LocalScale"]) if tr else [1, 1, 1]))
        self._pending.append((o, go, tr))
        self.stats["GameObject"] += 1
        # register components first so cross references resolve
        for ci, ptr in enumerate(self._comp_ptrs(go)):
            self.comp_index[(self.w.ext_file(o.assets_file, ptr["m_FileID"]).name if self.w.ext_file(o.assets_file, ptr["m_FileID"]) else "?", ptr["m_PathID"])] = (idx, ci)
        if tr:
            for ch in tr["m_Children"]:
                cto = self.w.obj(tr_obj.assets_file, ch["m_FileID"], ch["m_PathID"])
                if cto is None:
                    self.warnings["missing child transform"] += 1
                    continue
                gp = cto.read_typetree()["m_GameObject"]
                cgo = self.w.obj(cto.assets_file, gp["m_FileID"], gp["m_PathID"])
                if cgo is not None:
                    self._collect(cgo, idx)

    # ---- pointers --------------------------------------------------------
    def ref(self, f, p):
        """Turn a {m_FileID, m_PathID} into a JSON reference."""
        if not p or not p.get("m_PathID"):
            return None
        o = self.w.obj(f, p["m_FileID"], p["m_PathID"])
        if o is None:
            self.warnings["unresolved pointer"] += 1
            return {"missing": True}
        key = (o.assets_file.name, o.path_id)
        t = o.type.name
        if t == "GameObject":
            if key in self.go_index:
                return {"obj": self.go_index[key]}
            return {"prefab": gid(o), "name": o.read_typetree().get("m_Name", "")}
        if key in self.comp_index:
            oi, ci = self.comp_index[key]
            return {"obj": oi, "comp": ci}
        if t == "Material":
            return {"mat": self.material(o)}
        if t == "Mesh":
            mi = self.mesh(o)
            return {"mesh": mi} if mi is not None else {"missing": True, "asset": "Mesh"}
        if t in ("Texture2D", "Cubemap"):
            return {"tex": self.cat.texture(o)}
        if t == "Sprite":
            return {"sprite": self.cat.sprite(o)}
        if t == "AudioClip":
            return {"audio": self.cat.audio(o)}
        if t == "MonoScript":
            return {"script": self.w.scripts.get(key)}
        name = ""
        try: name = o.read_typetree().get("m_Name", "")
        except Exception: pass
        return {"asset": t, "id": gid(o), "name": name}

    def clean(self, f, v):
        """Recursively convert a type-tree value to JSON, resolving pointers."""
        if isinstance(v, dict):
            if set(v) == {"m_FileID", "m_PathID"}:
                return self.ref(f, v)
            return {k: self.clean(f, x) for k, x in v.items()}
        if isinstance(v, list):
            return [self.clean(f, x) for x in v]
        if isinstance(v, (bytes, bytearray)):
            return {"bytes": len(v)}
        if isinstance(v, float) and not math.isfinite(v):
            return None
        return v

    # ---- components ------------------------------------------------------
    def _component(self, o, oi, ptr):
        co = self.w.obj(o.assets_file, ptr["m_FileID"], ptr["m_PathID"])
        if co is None:
            self.warnings["missing component"] += 1
            return None
        t = co.type.name
        if t in ("Transform", "RectTransform"):
            return None
        self.stats[t] += 1
        try:
            tree = co.read_typetree()
        except Exception as e:
            self.warnings["unreadable " + t] += 1
            return {"type": t, "unreadable": True}
        f = co.assets_file
        en = tree.get("m_Enabled", 1)
        if t == "MeshFilter":
            return {"type": t, "mesh": self.ref(f, tree["m_Mesh"])}
        if t in ("MeshRenderer", "SkinnedMeshRenderer"):
            c = {"type": t, "enabled": bool(en),
                 "materials": [self.ref(f, m) for m in tree["m_Materials"]],
                 "castShadows": tree.get("m_CastShadows", 0),
                 "lightmapIndex": tree.get("m_LightmapIndex", 65535)}
            if t == "SkinnedMeshRenderer":
                c["mesh"] = self.ref(f, tree["m_Mesh"])
                c["bones"] = [self.ref(f, b) for b in tree["m_Bones"]]
                c["rootBone"] = self.ref(f, tree.get("m_RootBone"))
            return c
        if t == "MeshCollider":
            return {"type": t, "enabled": bool(en), "isTrigger": bool(tree.get("m_IsTrigger", 0)),
                    "convex": bool(tree.get("m_Convex", 0)), "mesh": self.ref(f, tree["m_Mesh"])}
        if t == "BoxCollider":
            return {"type": t, "enabled": bool(en), "isTrigger": bool(tree["m_IsTrigger"]),
                    "center": v3(tree["m_Center"]), "size": v3(tree["m_Size"])}
        if t == "SphereCollider":
            return {"type": t, "enabled": bool(en), "isTrigger": bool(tree["m_IsTrigger"]),
                    "center": v3(tree["m_Center"]), "radius": tree["m_Radius"]}
        if t == "CapsuleCollider":
            return {"type": t, "enabled": bool(en), "isTrigger": bool(tree["m_IsTrigger"]),
                    "center": v3(tree["m_Center"]), "radius": tree["m_Radius"],
                    "height": tree["m_Height"], "direction": tree["m_Direction"]}
        if t == "Light":
            return {"type": t, "enabled": bool(en), "lightType": tree["m_Type"], "color": col(tree["m_Color"]),
                    "intensity": tree["m_Intensity"], "range": tree["m_Range"], "spotAngle": tree["m_SpotAngle"],
                    "shadows": tree["m_Shadows"]["m_Type"], "bakeType": tree.get("m_Lightmapping", 4)}
        if t == "MonoBehaviour":
            cls = self.w.script_name(co, tree)
            if cls is None:
                self.warnings["MonoBehaviour with missing script"] += 1
                return None
            self.cat.script_counts[cls] += 1
            fields = {k: self.clean(f, v) for k, v in tree.items()
                      if k not in ("m_GameObject", "m_Enabled", "m_Script", "m_Name", "m_EditorHideFlags", "m_EditorClassIdentifier")}
            return {"type": "Script", "class": cls, "enabled": bool(en), "fields": fields}
        # Everything else (AudioSource, Animator, Animation, ParticleSystem,
        # Rigidbody, LensFlare, TrailRenderer...) keeps its full field set.
        fields = {k: self.clean(f, v) for k, v in tree.items() if k not in ("m_GameObject", "m_Enabled")}
        return {"type": t, "enabled": bool(en), "fields": fields}

    # ---- assets ----------------------------------------------------------
    def material(self, o):
        key = gid(o)
        if key in self.mat_index:
            return self.mat_index[key]
        tree = o.read_typetree()
        f = o.assets_file
        sh = self.w.obj(f, tree["m_Shader"]["m_FileID"], tree["m_Shader"]["m_PathID"])
        shader = None
        if sh is not None:
            try:
                d = sh.read()
                pf = getattr(d, "m_ParsedForm", None)
                shader = pf.m_Name if pf else getattr(d, "m_Name", None)
            except Exception:
                pass
        if not shader:
            shader = "builtin:%d" % tree["m_Shader"]["m_PathID"]
        sp = tree["m_SavedProperties"]
        texs = {}
        for name, te in sp["m_TexEnvs"]:
            r = self.ref(f, te["m_Texture"])
            if r:
                texs[name] = {"tex": r.get("tex"), "scale": [te["m_Scale"]["x"], te["m_Scale"]["y"]],
                              "offset": [te["m_Offset"]["x"], te["m_Offset"]["y"]]}
        m = {"name": tree["m_Name"], "shader": shader, "renderQueue": tree.get("m_CustomRenderQueue", -1),
             "keywords": tree.get("m_ShaderKeywords", "").split(),
             "textures": texs, "floats": dict(sp["m_Floats"]),
             "colors": {k: col(v) for k, v in sp["m_Colors"]}}
        self.cat.shader_counts[shader] += 1
        self.mat_index[key] = len(self.materials)
        self.materials.append(m)
        return self.mat_index[key]

    def mesh(self, o):
        key = gid(o)
        if key in self.meshes.index:
            return self.meshes.index[key]
        try:
            d = o.read(check_read=False)
            h = MeshHandler(d)
            h.process()
        except Exception as e:
            self.warnings["unreadable mesh"] += 1
            return None
        subs, indices = [], []
        base = [getattr(s, "baseVertex", 0) or 0 for s in d.m_SubMeshes]
        for si, tris in enumerate(h.get_triangles()):
            first = len(indices)
            for t in tris:
                indices.extend(i + base[si] for i in t)
            subs.append((first, len(indices) - first, 0))
        pos = h.m_Vertices or []
        colors = h.m_Colors
        if colors and isinstance(colors[0][0], float):
            colors = [tuple(round(c * 255) for c in k) for k in colors]
        self.stats["triangles"] += len(indices) // 3
        return self.meshes.add(key, d.m_Name, pos, h.m_Normals, h.m_UV0, h.m_UV1, colors, subs, indices)


# --------------------------------------------------------------------------
# Shared assets
# --------------------------------------------------------------------------

class Catalog:
    def __init__(self, world):
        self.w = world
        self.textures = {}      # gid -> object
        self.sprites = {}
        self.audios = {}
        self.script_counts = Counter()
        self.shader_counts = Counter()

    def texture(self, o):
        self.textures[gid(o)] = o
        return gid(o)

    def sprite(self, o):
        tree = o.read_typetree()
        rd = tree["m_RD"]
        tex = self.w.obj(o.assets_file, rd["texture"]["m_FileID"], rd["texture"]["m_PathID"])
        tid = self.texture(tex) if tex is not None else None
        r = rd["textureRect"]
        self.sprites[gid(o)] = {"name": tree["m_Name"], "tex": tid,
                                "rect": [r["x"], r["y"], r["width"], r["height"]],
                                "pivot": [tree["m_Pivot"]["x"], tree["m_Pivot"]["y"]],
                                "pixelsToUnits": tree["m_PixelsToUnits"]}
        return gid(o)

    def audio(self, o):
        self.audios[gid(o)] = o
        return gid(o)


def safe_name(gid_):
    return re.sub(r"[^a-z0-9_.-]", "_", gid_.lower())


def export_audio(cat, out_dir, log):
    os.makedirs(os.path.join(out_dir, "audio"), exist_ok=True)
    table, n = {}, 0
    for g, o in sorted(cat.audios.items()):
        d = o.read()
        dst = os.path.join(out_dir, "audio", safe_name(g) + ".ogg")
        table[g] = {"name": d.m_Name, "file": "audio/" + os.path.basename(dst), "length": d.m_Length,
                    "channels": d.m_Channels, "frequency": d.m_Frequency}
        if os.path.exists(dst):
            continue
        try:
            wav = next(iter(d.samples.values()))
        except Exception as e:
            log(f"  audio {d.m_Name}: decode failed: {e}")
            table[g]["error"] = str(e)
            continue
        r = subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", "pipe:0", "-c:a", "libvorbis", "-q:a", "4", dst],
                           input=wav, capture_output=True)
        if r.returncode:
            table[g]["error"] = r.stderr.decode()[-200:]
        n += 1
    log(f"audio: {len(table)} clips ({n} newly encoded)")
    return table


# --------------------------------------------------------------------------
# Driver
# --------------------------------------------------------------------------

def parse_levels(spec):
    out = set()
    for part in spec.split(","):
        if "-" in part:
            a, b = part.split("-"); out.update(range(int(a), int(b) + 1))
        elif part:
            out.add(int(part))
    return out


def export_prefab(world, cat, path, out_base, log):
    roots = world.roots(path)
    if not roots:
        return None
    if len(roots) > 1:
        # A prefab and its source model can share a Resources path. The game
        # wants the prefab, which is the one with colliders/scripts attached.
        roots.sort(key=lambda o: -len(o.read_typetree()["m_Component"]))
        log(f"  {path}: {len(roots)} roots, using the one with most components")
    ex = PrefabExporter(world, cat)
    data = ex.export(roots[0])
    data.update(format=FORMAT_VERSION, source=path, stats=dict(ex.stats), warnings=dict(ex.warnings))
    os.makedirs(os.path.dirname(out_base), exist_ok=True)
    ex.meshes.write(out_base + ".smsh")
    with open(out_base + ".json", "w") as f:
        json.dump(data, f, separators=(",", ":"))
    return data


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("data_dir"); ap.add_argument("out_dir")
    ap.add_argument("--levels", help="subset, e.g. 1,2,10-20 (default: all)")
    ap.add_argument("--no-textures", action="store_true")
    ap.add_argument("--no-audio", action="store_true")
    ap.add_argument("--jobs", type=int, default=os.cpu_count())
    a = ap.parse_args()
    t0 = time.time()
    os.makedirs(a.out_dir, exist_ok=True)
    logf = open(os.path.join(a.out_dir, "export.log"), "w")

    def log(msg):
        print(msg, flush=True); logf.write(msg + "\n"); logf.flush()

    log(f"loading {a.data_dir}")
    world = World(unity_env.load(a.data_dir))
    cat = Catalog(world)
    log(f"  {len(world.files)} files, {len(world.resources)} resource paths, {len(world.scripts)} scripts ({time.time()-t0:.0f}s)")

    level_ids = sorted(int(m.group(1)) for p in world.resources if (m := re.fullmatch(r"levels/level_(\d+)", p)))
    if a.levels:
        level_ids = sorted(set(level_ids) & parse_levels(a.levels))
    manifest = {"format": FORMAT_VERSION, "unity": unity_env.UNITY_VERSION, "levels": {}, "prefabs": {}}

    # Levels: geometry prefab + .sap blob
    for n in level_ids:
        path = f"levels/level_{n}"
        try:
            d = export_prefab(world, cat, path, os.path.join(a.out_dir, "levels", f"level_{n}"), log)
        except Exception:
            log(f"level {n}: FAILED\n{traceback.format_exc()}")
            manifest["levels"][n] = {"error": True}
            continue
        sap = None
        for fname, pid in world.resources.get(path + ".sap", []):
            o = world.files[fname].objects[pid]
            if o.type.name == "TextAsset":
                raw = o.read().m_Script
                raw = raw.encode("utf-8", "surrogateescape") if isinstance(raw, str) else bytes(raw)
                with open(os.path.join(a.out_dir, "levels", f"level_{n}.sap"), "wb") as f:
                    f.write(raw)
                sap = len(raw)
        manifest["levels"][n] = {"objects": len(d["objects"]), "meshes": d["meshCount"],
                                 "materials": len(d["materials"]), "sapBytes": sap,
                                 "triangles": d["stats"].get("triangles", 0), "warnings": d["warnings"]}
        log(f"level {n}: {len(d['objects'])} objects, {d['meshCount']} meshes, "
            f"{d['stats'].get('triangles', 0)} tris, sap={sap}  [{time.time()-t0:.0f}s]")

    # Shared prefabs the game spawns at runtime (character, environments, props, effects...)
    if not a.levels:
        for path in sorted(world.resources):
            if path.startswith("levels/level_") or not world.roots(path):
                continue
            try:
                d = export_prefab(world, cat, path, os.path.join(a.out_dir, "prefabs", path), log)
                manifest["prefabs"][path] = {"objects": len(d["objects"]), "meshes": d["meshCount"]}
            except Exception:
                log(f"prefab {path}: FAILED\n{traceback.format_exc()}")
                manifest["prefabs"][path] = {"error": True}
        log(f"prefabs: {len(manifest['prefabs'])}")

        # Standalone Resources textures and sprites (menu/HUD art loaded by path)
        # The ResourceManager lists every dependency of an asset under that
        # asset's path, so only an object whose name matches the last path
        # component is the thing Resources.Load(path) actually returns.
        res = {}
        for path, ptrs in world.resources.items():
            leaf = path.rsplit("/", 1)[-1]
            for fname, pid in ptrs:
                o = world.files[fname].objects.get(pid)
                if o is None or o.type.name not in ("Texture2D", "Sprite", "AudioClip"):
                    continue
                try:
                    name = o.read_typetree().get("m_Name", "")
                except Exception:
                    continue
                if name.lower() != leaf:
                    continue
                if o.type.name == "Texture2D":
                    res[path] = cat.texture(o)
                elif o.type.name == "Sprite":
                    res.setdefault(path, cat.sprite(o))
                else:
                    manifest.setdefault("resourceAudio", {})[path] = cat.audio(o)
        manifest["resourceTextures"] = res

    if not a.no_textures:
        # Menu/HUD art lives in the game's "<Screen>Psd/" folders: keep it
        # uncompressed so text stays sharp. Everything else is DXT.
        ui_ids = {t for p, t in manifest.get("resourceTextures", {}).items()
                  if t in cat.textures and "psd/" in p}
        manifest["textures"] = textures.export_all(cat.textures, a.out_dir, a.jobs, log, ui_ids)
    manifest["sprites"] = cat.sprites
    if not a.no_audio:
        manifest["audio"] = export_audio(cat, a.out_dir, log)
    manifest["scriptUsage"] = dict(cat.script_counts.most_common())
    manifest["shaderUsage"] = dict(cat.shader_counts.most_common())
    with open(os.path.join(a.out_dir, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=1)
    log(f"done in {time.time()-t0:.0f}s")


if __name__ == "__main__":
    main()
