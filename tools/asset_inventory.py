"""Inventory a Unity *_Data folder: counts/sizes per asset type, texture formats,
mesh poly counts, audio formats, and per-scene object counts. Read-only."""
import sys, json, collections, UnityPy

root = sys.argv[1]
env = UnityPy.load(root)
by_type = collections.Counter()
tex, meshes, audio, shaders = [], [], [], []
scenes = collections.defaultdict(collections.Counter)

for obj in env.objects:
    t = obj.type.name
    by_type[t] += 1
    src = obj.assets_file.name
    if src.startswith("level"):
        scenes[src][t] += 1
    try:
        if t == "Texture2D":
            d = obj.read()
            tex.append((d.m_Width, d.m_Height, str(d.m_TextureFormat).split(".")[-1], d.m_MipCount, getattr(d, "m_Name", "")))
        elif t == "Mesh":
            d = obj.read()
            tris = sum(sm.indexCount for sm in d.m_SubMeshes) // 3
            verts = d.m_VertexData.m_VertexCount if hasattr(d, "m_VertexData") else 0
            meshes.append((tris, verts, d.m_Name))
        elif t == "AudioClip":
            d = obj.read()
            audio.append((round(d.m_Length, 1), d.m_Frequency, d.m_Channels, str(d.m_CompressionFormat).split(".")[-1], d.m_Name))
        elif t == "Shader":
            d = obj.read()
            shaders.append(d.m_ParsedForm.m_Name if hasattr(d, "m_ParsedForm") and d.m_ParsedForm else getattr(d, "m_Name", "?"))
    except Exception as e:
        pass

def tex_bytes(w, h, fmt, mips):
    bpp = {"DXT1": 4, "DXT1Crunched": 4, "DXT5": 8, "DXT5Crunched": 8, "BC7": 8, "BC5": 8, "BC4": 4,
           "RGBA32": 32, "ARGB32": 32, "RGB24": 24, "Alpha8": 8, "RGBAHalf": 64, "RGBAFloat": 128, "R16": 16}.get(fmt, 32)
    b = w * h * bpp / 8
    return b * (4 / 3 if mips > 1 else 1)

print("== object types ==")
for t, n in by_type.most_common(30): print(f"{n:7d} {t}")
print("\n== textures ==", len(tex))
fmts = collections.Counter(f for _, _, f, _, _ in tex); print(dict(fmts))
sizes = collections.Counter(f"{w}x{h}" for w, h, *_ in tex); print("sizes:", sizes.most_common(12))
total = sum(tex_bytes(w, h, f, m) for w, h, f, m, _ in tex); print(f"GPU texture memory (all, as-is): {total/2**20:.0f} MiB")
print("largest:", sorted(tex, key=lambda x: -x[0]*x[1])[:10])
print("\n== meshes ==", len(meshes), "total tris", sum(m[0] for m in meshes))
print("largest:", sorted(meshes, reverse=True)[:12])
print("\n== audio ==", len(audio), f"total {sum(a[0] for a in audio)/60:.0f} min")
print(collections.Counter(a[3] for a in audio)); print("longest:", sorted(audio, reverse=True)[:8])
print("\n== shaders ==", len(shaders)); print(sorted(set(shaders)))
print("\n== scenes ==")
for s in sorted(scenes, key=lambda x: int(''.join(c for c in x if c.isdigit()) or 0)):
    c = scenes[s]
    print(s, {k: c[k] for k in ("GameObject", "MeshRenderer", "MeshCollider", "BoxCollider", "Light", "ParticleSystem", "MonoBehaviour") if c[k]})
