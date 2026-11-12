"""Per-level budget: triangles, unique meshes, colliders, lights, particle systems,
unique textures (and their memory as-is and at half-res DXT) for each
Resources/Levels/Level_N prefab. Read-only."""
import sys, re, collections, UnityPy

env = UnityPy.load(sys.argv[1])
BPP = {10: 4, 12: 8, 4: 32, 3: 24, 1: 8, 2: 16}  # DXT1, DXT5, RGBA32, RGB24, Alpha8, ARGB4444

def walk(go, acc, seen):
    key = go.object_reader.path_id
    if key in seen: return
    seen.add(key)
    for comp in go.m_Component:
        ptr = comp.component if hasattr(comp, "component") else comp[1]
        try: c = ptr.read()
        except Exception: continue
        t = c.object_reader.type.name
        acc["comp"][t] += 1
        if t == "MeshFilter" and c.m_Mesh:
            try:
                m = c.m_Mesh.read()
                acc["tris"] += sum(s.indexCount for s in m.m_SubMeshes) // 3
                acc["meshes"].add(m.object_reader.path_id)
            except Exception: pass
        if t == "MeshRenderer":
            for mp in c.m_Materials:
                try: mat = mp.read()
                except Exception: continue
                for _, te in mat.m_SavedProperties.m_TexEnvs:
                    if te.m_Texture and te.m_Texture.path_id:
                        try:
                            tx = te.m_Texture.read()
                            if tx.object_reader.type.name == "Texture2D":
                                acc["tex"][tx.object_reader.path_id] = (tx.m_Width, tx.m_Height, int(tx.m_TextureFormat))
                        except Exception: pass
        if t == "Transform":
            for ch in c.m_Children:
                try: walk(ch.read().m_GameObject.read(), acc, seen)
                except Exception: pass

# Resources.Load paths live in globalgamemanagers' ResourceManager, one entry per sub-object.
roots = {}
for o in env.objects:
    if o.type.name != "ResourceManager": continue
    for path, ptr in o.read().m_Container:
        m = re.fullmatch(r"levels/level_(\d+)", path.lower())
        if not m: continue
        try: obj = ptr.deref()
        except Exception: continue
        if obj.type.name != "GameObject": continue
        go = obj.read()
        tr = next((c.read() for c in (x.component if hasattr(x, "component") else x[1] for x in go.m_Component)
                   if c.type.name == "Transform"), None)
        if tr is not None and not tr.m_Father.path_id:
            roots[int(m.group(1))] = go
rows = []
for lvl, go in roots.items():
    m = re.match(r"(\d+)", str(lvl))
    acc = {"tris": 0, "meshes": set(), "comp": collections.Counter(), "tex": {}}
    walk(go, acc, set())
    mem = sum(w*h*BPP.get(f, 32)/8*4/3 for w, h, f in acc["tex"].values())
    vita = sum((w//2)*(h//2)*(4 if f != 12 else 8)/8*4/3 for w, h, f in acc["tex"].values())
    c = acc["comp"]
    rows.append((int(m.group(1)), acc["tris"], len(acc["meshes"]), c["MeshCollider"], c["BoxCollider"]+c["SphereCollider"]+c["CapsuleCollider"],
                 c["Light"], c["ParticleSystem"], c["MonoBehaviour"], len(acc["tex"]), mem/2**20, vita/2**20))
rows.sort()
print("lvl   tris  meshes meshcol primcol lights psys scripts tex  texMiB  vitaMiB(half-res DXT)")
for r in rows: print("%3d %7d %6d %7d %7d %6d %4d %7d %4d %7.1f %7.1f" % r)
import statistics as st
for i, name in [(1, "tris"), (9, "texMiB"), (10, "vitaMiB")]:
    v = [r[i] for r in rows]; print(f"{name}: n={len(v)} median={st.median(v):.0f} p90={sorted(v)[int(.9*len(v))-1]:.0f} max={max(v):.0f}")
