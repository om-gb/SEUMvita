# Exported data format (v1)

Produced by `tools/export/export.py`, checked by `tools/export/validate.py`.
The engine reads it from `ux0:data/seum/` on the Vita, or from any folder on Linux.

Conventions are kept **exactly as Unity**: left-handed, +Y up, metres, Unity
quaternion order `[x, y, z, w]`, and local transforms relative to the parent.
Ported gameplay code can then use the original constants unchanged.

```
<out>/
  manifest.json              index of everything below + usage stats
  levels/level_<N>.json      scene graph, components, materials of Levels/Level_N
  levels/level_<N>.smsh      mesh pool for that level
  levels/level_<N>.sap       raw Level_N.sap bytes (parsed by the port of Level.cs)
  prefabs/<resource path>.json/.smsh   every other Resources prefab (character,
                             environments, props, effects…) in the same format
  tex/<file>_<pathid>.dds    textures, shared by all levels
  audio/<file>_<pathid>.ogg  audio clips (Ogg Vorbis q4)
  export.log
```

## manifest.json

| Key | Contents |
|---|---|
| `format`, `unity` | `1`, `"2018.3.7f1"` |
| `levels` | `{N: {objects, meshes, materials, triangles, sapBytes, warnings}}` |
| `prefabs` | `{resourcePath: {objects, meshes}}` |
| `textures` | `{texId: {name, file, w, h, fmt: DXT1/DXT5/RGBA8, mips, alpha, srcW, srcH}}`; failed ones have `error` |
| `sprites` | `{spriteId: {name, tex, rect:[x,y,w,h] in source pixels, pivot, pixelsToUnits}}` |
| `audio` | `{audioId: {name, file, length, channels, frequency}}` |
| `resourceTextures` | `{resourcePath: texId or spriteId}`, for `Resources.Load<Texture>("…Psd/…")` UI art |
| `resourceAudio` | `{resourcePath: audioId}` |
| `scriptUsage`, `shaderUsage` | how often each script class and shader is used |

Ids are `"<serialized file>:<path id>"`, for example `"resources.assets:813"`.

## Level / prefab JSON

```jsonc
{
  "format": 1, "source": "levels/level_1",
  "objects": [                      // index 0 is the root; parent index < own index
    { "name": "…", "parent": -1, "active": true, "layer": 0, "tag": 0,
      "pos": [x,y,z], "rot": [x,y,z,w], "scale": [x,y,z],
      "components": [ … ] }
  ],
  "materials": [ { "name", "shader", "renderQueue", "keywords": [],
                   "textures": { "_MainTex": { "tex": texId, "scale": [u,v], "offset": [u,v] } },
                   "floats": { "_Cutoff": 0.5 }, "colors": { "_Color": [r,g,b,a] } } ],
  "meshCount": 84, "stats": {…}, "warnings": {…}
}
```

### Components

| `type` | Fields |
|---|---|
| `MeshFilter` | `mesh` |
| `MeshRenderer` | `enabled`, `materials: [ref]` (one per submesh), `castShadows`, `lightmapIndex` |
| `SkinnedMeshRenderer` | the MeshRenderer fields plus `mesh`, `bones: [ref]`, `rootBone` |
| `MeshCollider` | `enabled`, `isTrigger`, `convex`, `mesh` |
| `BoxCollider` | `enabled`, `isTrigger`, `center`, `size` |
| `SphereCollider` | `enabled`, `isTrigger`, `center`, `radius` |
| `CapsuleCollider` | `enabled`, `isTrigger`, `center`, `radius`, `height`, `direction` (0=X 1=Y 2=Z) |
| `Light` | `enabled`, `lightType` (0 spot, 1 directional, 2 point, 3 area), `color`, `intensity`, `range`, `spotAngle`, `shadows`, `bakeType` |
| `Script` | `class` (C# class name), `enabled`, `fields`: every serialized field from the game's DLLs |
| anything else | `enabled`, `fields`: the full Unity serialized data (AudioSource, Animator, ParticleSystem, Rigidbody, …) |

### References

Any pointer inside `fields` or `materials` is replaced by one of:

| Form | Meaning |
|---|---|
| `{"obj": i}` | object `i` in this file |
| `{"obj": i, "comp": c}` | component `c` of object `i`, indexed against Unity's original component list, which includes the Transform |
| `{"mesh": i}`, `{"mat": i}` | entry in this file's mesh pool / materials |
| `{"tex": id}`, `{"sprite": id}`, `{"audio": id}` | shared asset in the manifest |
| `{"prefab": id, "name": …}` | a GameObject outside this hierarchy |
| `{"script": "ClassName"}` | a MonoScript |
| `{"asset": type, "id": id, "name": …}` | an asset type not exported yet (AnimatorController, AnimationClip, PhysicMaterial, …) |
| `{"missing": true}` | pointer the Unity data itself can't resolve |
| `null` | null pointer |

## .smsh mesh pool (little-endian)

```
char[4] "SMSH"; u32 version = 1; u32 meshCount
repeat meshCount:
  u32 byteSize                       // size of the record below
  u32 nameLen; u8 name[nameLen]; pad to 4
  u32 flags                          // 1 normals, 2 uv0, 4 uv1, 8 colors, 16 32-bit indices
  u32 vertexCount; u32 indexCount; u32 submeshCount
  { u32 firstIndex; u32 indexCount; u32 topology(0 = triangles) } [submeshCount]
  f32 aabbMin[3]; f32 aabbMax[3]
  f32 position[vertexCount][3]
  f32 normal[vertexCount][3]         if flags & 1
  f32 uv0[vertexCount][2]            if flags & 2
  f32 uv1[vertexCount][2]            if flags & 4
  u8  color[vertexCount][4]          if flags & 8
  u16|u32 index[indexCount]; pad to 4
```

Meshes are unique per file: instances share one entry through `MeshFilter.mesh`.
Every submesh is converted to triangle lists.

## Textures

These are standard DDS files. DXT1 is used when the image is opaque and DXT5
when it has alpha; both have a full mip chain down to 4×4. Every texture is
halved when it's over 64 px and capped at 1024 px. UI art loaded through
`resourceTextures` is RGBA8 without mips, halved to fit 960×544, so menu text
stays sharp.
