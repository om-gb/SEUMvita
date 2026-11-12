#include "level.h"
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>

static float num(json_t *j, float def = 0) { return json_is_number(j) ? (float)json_number_value(j) : def; }
static Vec3 vec3(json_t *a) { return {num(json_array_get(a, 0)), num(json_array_get(a, 1)), num(json_array_get(a, 2))}; }
static Quat quat(json_t *a) { return {num(json_array_get(a, 0)), num(json_array_get(a, 1)), num(json_array_get(a, 2)), num(json_array_get(a, 3), 1)}; }
static bool contains(const std::string &s, const char *sub) { return s.find(sub) != std::string::npos; }

bool loadLevelMeta(int id, LevelMeta &out) {
    static json_t *meta = nullptr;
    if (!meta) {
        json_error_t err;
        meta = json_load_file((plat::dataRoot() + "levelmeta.json").c_str(), 0, &err);
        if (!meta) { printf("levelmeta.json: %s\n", err.text); return false; }
    }
    json_t *r = json_object_get(json_object_get(meta, "levels"), std::to_string(id).c_str());
    if (!r) return false;
    out.name = json_string_value(json_object_get(r, "name")) ?: "";
    out.environment = json_string_value(json_object_get(r, "environment")) ?: "Hell1";
    out.position = vec3(json_object_get(r, "position"));
    out.envRotation = quat(json_object_get(r, "environmentRotation"));
    return true;
}

// Map the game's shaders onto fixed-function states (see shaderUsage in the manifest).
static void classify(Material &m, json_t *floats) {
    const std::string &s = m.shader;
    if (contains(s, "Distortion") || contains(s, "MirrorReflection")) { m.blend = Blend::Skip; return; }
    bool particle = contains(s, "Particle") || contains(s, "Effects/") || contains(s, "Glow");
    if (contains(s, "Additive") || contains(s, "ParticleAdd") || contains(s, "Glow")) m.blend = Blend::Additive;
    else if (contains(s, "Multiply")) m.blend = Blend::Multiply;
    else if (contains(s, "Cutout") || contains(s, "CutOut")) m.blend = Blend::Cutout;
    else if (contains(s, "Alpha Blended") || contains(s, "AlphaBlended") || contains(s, "Transparent") ||
             contains(s, "AlphaDiffuse") || contains(s, "Sprites/"))
        m.blend = Blend::Alpha;
    if (s == "Standard") {
        int mode = (int)num(json_object_get(floats, "_Mode"));
        m.blend = mode == 1 ? Blend::Cutout : mode >= 2 ? Blend::Alpha : Blend::Opaque;
    }
    m.lit = !(particle || contains(s, "Unlit") || contains(s, "Self-Illumin"));
    m.vertexColor = particle;
    m.cutoff = num(json_object_get(floats, "_Cutoff"), 0.5f);
}

static void loadMaterials(json_t *arr, TextureCache &tex, std::vector<Material> &out) {
    size_t i;
    json_t *jm;
    json_array_foreach(arr, i, jm) {
        Material m;
        m.name = json_string_value(json_object_get(jm, "name")) ?: "";
        m.shader = json_string_value(json_object_get(jm, "shader")) ?: "";
        json_t *texs = json_object_get(jm, "textures");
        json_t *main = json_object_get(texs, "_MainTex");
        if (!main) {   // a few custom shaders name their base map differently
            const char *k; json_t *v;
            json_object_foreach(texs, k, v) { if (json_string_value(json_object_get(v, "tex"))) { main = v; break; } }
        }
        if (main) {
            const char *id = json_string_value(json_object_get(main, "tex"));
            if (id) m.tex = tex.get(id);
            json_t *sc = json_object_get(main, "scale"), *of = json_object_get(main, "offset");
            m.tiling[0] = num(json_array_get(sc, 0), 1); m.tiling[1] = num(json_array_get(sc, 1), 1);
            m.offset[0] = num(json_array_get(of, 0)); m.offset[1] = num(json_array_get(of, 1));
        }
        json_t *cols = json_object_get(jm, "colors");
        // Particle/effect shaders read _TintColor; materials often also carry a
        // stale _Color from a previous shader, so prefer the tint for those.
        bool fx = contains(m.shader, "Particle") || contains(m.shader, "Effects/") || contains(m.shader, "Glow");
        json_t *c = fx ? json_object_get(cols, "_TintColor") : nullptr;
        bool tint = c != nullptr;
        if (!c) c = json_object_get(cols, "_Color");
        if (c)
            for (int k = 0; k < 4; k++) m.color[k] = num(json_array_get(c, k), 1);
        if (tint)   // Unity's particle shaders use 2 * _TintColor
            for (int k = 0; k < 3; k++) m.color[k] = std::min(1.0f, m.color[k] * 2);
        classify(m, json_object_get(jm, "floats"));
        out.push_back(m);
    }
}

// Port of the first part of Level.loadLevelFromStream (decompiled Level.cs).
static bool readSpawn(const std::string &path, Vec3 &pos, Quat &rot) {
    std::vector<uint8_t> d;
    if (!readFile(path, d) || d.size() < 32 || memcmp(d.data(), "SAP", 3) || d[3] != 1) return false;
    float f[7];
    memcpy(f, d.data() + 4, sizeof f);
    // file order is x, z, y; then scaled by 2 and rotated by Euler(180,0,0) * Euler(0,0,180)
    Quat fix = eulerDeg(180, 0, 0) * eulerDeg(0, 0, 180);
    pos = rotate(fix, Vec3(f[0], f[2], f[1]) * 2.0f);
    rot = fix * Quat(f[3], f[4], f[5], f[6]);
    return true;
}

bool Level::loadPart(const std::string &base, TextureCache &tex, Root mode, const Mat4 &xf, bool levelBounds) {
    json_error_t err;
    json_t *j = json_load_file((base + ".json").c_str(), 0, &err);
    if (!j) { printf("%s: %s\n", base.c_str(), err.text); return false; }
    int meshBase = (int)meshes.size(), matBase = (int)materials.size();
    std::vector<Mesh> partMeshes;
    if (!loadMeshPool(base + ".smsh", partMeshes, &meshBytes)) { json_decref(j); return false; }
    for (Mesh &m : partMeshes) meshes.push_back(std::move(m));
    loadMaterials(json_object_get(j, "materials"), tex, materials);

    json_t *objs = json_object_get(j, "objects");
    size_t n = json_array_size(objs);
    objectCount += (int)n;
    std::vector<Mat4> world(n);
    std::vector<char> active(n);
    for (size_t i = 0; i < n; i++) {
        json_t *o = json_array_get(objs, i);
        int parent = (int)json_integer_value(json_object_get(o, "parent"));
        Vec3 p = vec3(json_object_get(o, "pos")), s = vec3(json_object_get(o, "scale"));
        Quat q = quat(json_object_get(o, "rot"));
        if (parent < 0) {
            if (mode == Root::Place) { world[i] = xf * trs(Vec3(), Quat(), s); }   // Instantiate(prefab, pos, rot)
            else world[i] = xf * trs(p, q, s);                                     // Keep / SetParent(container)
        } else {
            world[i] = world[parent] * trs(p, q, s);
        }
        active[i] = json_is_true(json_object_get(o, "active")) && (parent < 0 || active[parent]);
        if (!active[i]) continue;

        int meshIdx = -1;
        json_t *renderer = nullptr;
        size_t ci;
        json_t *c;
        json_array_foreach(json_object_get(o, "components"), ci, c) {
            const char *type = json_string_value(json_object_get(c, "type"));
            if (!type) continue;
            if (!strcmp(type, "MeshFilter")) {
                json_t *mr = json_object_get(json_object_get(c, "mesh"), "mesh");
                if (json_is_integer(mr)) meshIdx = meshBase + (int)json_integer_value(mr);
            } else if (!strcmp(type, "MeshRenderer")) {
                if (json_is_true(json_object_get(c, "enabled"))) renderer = c;
            } else if (!strcmp(type, "Light") && json_is_true(json_object_get(c, "enabled"))) {
                int lt = (int)json_integer_value(json_object_get(c, "lightType"));
                json_t *lc = json_object_get(c, "color");
                float in = num(json_object_get(c, "intensity"), 1);
                float col[3] = {num(json_array_get(lc, 0), 1) * in, num(json_array_get(lc, 1), 1) * in, num(json_array_get(lc, 2), 1) * in};
                if (lt == 1) {
                    DirLight d{normalize(world[i].dir(Vec3(0, 0, 1))), {col[0], col[1], col[2]}};
                    dirLights.push_back(d);
                } else if (lt == 2) {
                    PointLight pl{world[i].point(Vec3()), num(json_object_get(c, "range"), 10), {col[0], col[1], col[2]}};
                    pointLights.push_back(pl);
                }
            }
        }
        if (meshIdx < 0 || !renderer || meshIdx >= (int)meshes.size() || !meshes[meshIdx].vbo) continue;
        const Mesh &m = meshes[meshIdx];
        Vec3 c0 = world[i].point((m.aabbMin + m.aabbMax) * 0.5f);
        Vec3 ext = (m.aabbMax - m.aabbMin) * 0.5f;
        float sx = length(world[i].dir(Vec3(1, 0, 0))), sy = length(world[i].dir(Vec3(0, 1, 0))), sz = length(world[i].dir(Vec3(0, 0, 1)));
        float rad = length(Vec3(ext.x * sx, ext.y * sy, ext.z * sz));
        json_t *mats = json_object_get(renderer, "materials");
        for (size_t s = 0; s < m.subs.size(); s++) {
            json_t *mr = json_object_get(json_array_get(mats, s), "mat");
            if (!json_is_integer(mr)) continue;
            int mi = matBase + (int)json_integer_value(mr);
            if (mi < 0 || mi >= (int)materials.size() || materials[mi].blend == Blend::Skip) continue;
            DrawItem it{meshIdx, (int)s, mi, world[i], world[i].det3() < 0, c0, rad, {-1, -1, -1, -1}};
            items.push_back(it);
        }
        if (levelBounds) {
            boundsMin = vmin(boundsMin, c0 - Vec3(rad, rad, rad));
            boundsMax = vmax(boundsMax, c0 + Vec3(rad, rad, rad));
        }
    }
    json_t *rs = json_object_get(j, "renderSettings");
    if (rs && json_integer_value(json_object_get(rs, "ambientMode")) == 3) {   // Flat / custom colour
        json_t *a = json_object_get(rs, "ambientSky");
        for (int k = 0; k < 3; k++) ambient[k] = num(json_array_get(a, k), 0.45f);
    }
    json_decref(j);
    return true;
}

void Level::loadSky(TextureCache &tex) {
    std::string path = "environment/materials/skybox" + meta.environment;
    std::transform(path.begin(), path.end(), path.begin(), ::tolower);
    auto it = tex.manifest().skyboxes.find(path);
    if (it == tex.manifest().skyboxes.end()) return;
    for (int f = 0; f < 6; f++) {
        sky[f] = it->second.faces[f].empty() ? 0 : tex.get(it->second.faces[f]);
        if (sky[f]) {
            glBindTexture(GL_TEXTURE_2D, sky[f]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
    }
    // Unity: colour = tex * tint * 2 * exposure (unity_ColorSpaceDouble in gamma space)
    for (int k = 0; k < 3; k++) skyTint[k] = std::min(1.0f, it->second.tint[k] * 2 * it->second.exposure);
}

// Static lights: give each draw item its 4 strongest point lights.
void Level::assignLights() {
    for (DrawItem &it : items) {
        float best[4] = {0, 0, 0, 0};
        for (int k = 0; k < 4; k++) it.lights[k] = -1;
        for (size_t l = 0; l < pointLights.size() && l < 127; l++) {
            const PointLight &pl = pointLights[l];
            float d = std::max(0.0f, length(pl.pos - it.center) - it.radius);
            if (d >= pl.range) continue;
            float r = d / pl.range;
            float score = (pl.color[0] + pl.color[1] + pl.color[2]) / (1 + 25 * r * r);
            for (int k = 0; k < 4; k++)
                if (score > best[k]) {
                    for (int m = 3; m > k; m--) { best[m] = best[m - 1]; it.lights[m] = it.lights[m - 1]; }
                    best[k] = score; it.lights[k] = (signed char)l;
                    break;
                }
        }
    }
}

bool Level::load(int levelId, TextureCache &tex) {
    double t0 = plat::seconds();
    id = levelId;
    meta = LevelMeta();
    meta.environment = "Hell1";
    if (!loadLevelMeta(levelId, meta)) printf("level %d: no metadata, using defaults\n", levelId);
    boundsMin = Vec3(1e9f, 1e9f, 1e9f);
    boundsMax = Vec3(-1e9f, -1e9f, -1e9f);
    const std::string root = plat::dataRoot();

    loadPart(root + "scenes/game", tex, Root::Keep, Mat4::identity(), false);
    std::string base = root + "levels/level_" + std::to_string(levelId);
    if (!loadPart(base, tex, Root::Place, trs(meta.position, Quat(), Vec3(1, 1, 1)), true)) return false;
    hasSpawn = readSpawn(base + ".sap", spawnPos, spawnRot);
    if (hasSpawn) spawnPos = spawnPos + meta.position;   // spawn is relative to the level root
    std::string env = meta.environment;
    std::transform(env.begin(), env.end(), env.begin(), ::tolower);
    loadPart(root + "prefabs/environment/" + env, tex, Root::Parent, trs(Vec3(), meta.envRotation, Vec3(1, 1, 1)), false);
    loadSky(tex);
    assignLights();

    // Opaque first, grouped by material to save state changes.
    std::stable_sort(items.begin(), items.end(), [this](const DrawItem &a, const DrawItem &b) {
        Blend ba = materials[a.mat].blend, bb = materials[b.mat].blend;
        if (ba != bb) return (int)ba < (int)bb;
        return a.mat < b.mat;
    });
    loadSeconds = plat::seconds() - t0;
    return true;
}

void Level::unload() {
    freeMeshPool(meshes);
    materials.clear();
    items.clear();
    dirLights.clear();
    pointLights.clear();
    for (GLuint &s : sky) s = 0;
    meshBytes = 0;
    objectCount = 0;
    hasSpawn = false;
}

static void applyMaterial(const Material &m) {
    if (m.tex) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, m.tex); }
    else glDisable(GL_TEXTURE_2D);
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    glTranslatef(m.offset[0], m.offset[1], 0);
    glScalef(m.tiling[0], m.tiling[1], 1);
    glMatrixMode(GL_MODELVIEW);
    if (m.lit && !getenv("SEUM_NOLIGHT")) glEnable(GL_LIGHTING); else glDisable(GL_LIGHTING);
    glColor4f(m.color[0], m.color[1], m.color[2], m.color[3]);
    glDisable(GL_ALPHA_TEST);
    glDepthMask(GL_TRUE);
    switch (m.blend) {
    case Blend::Opaque: glDisable(GL_BLEND); break;
    case Blend::Cutout:
        glDisable(GL_BLEND);
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(GL_GREATER, m.cutoff);
        break;
    case Blend::Alpha: glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE); break;
    case Blend::Additive: glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE); glDepthMask(GL_FALSE); break;
    case Blend::Multiply: glEnable(GL_BLEND); glBlendFunc(GL_DST_COLOR, GL_ZERO); glDepthMask(GL_FALSE); break;
    case Blend::Skip: break;
    }
}

void Level::drawSky(const Mat4 &view, const Mat4 &proj) {
    if (!sky[0] && !sky[4]) return;
    // Cube centred on the camera: take the view matrix without translation.
    Mat4 v = view;
    v.m[12] = v.m[13] = v.m[14] = 0;
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(proj.m);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(v.m);
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    glColor4f(skyTint[0], skyTint[1], skyTint[2], 1);
    glDisableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    // Each face seen from inside the cube, matching Unity's 6-sided skybox
    // layout: u to the right and v up as the viewer faces that direction.
    static const float s = 100;
    static const float faces[6][4][3] = {
        {{ s, -s,  s}, { s, -s, -s}, { s,  s, -s}, { s,  s,  s}},   // +X (_LeftTex)
        {{-s, -s, -s}, {-s, -s,  s}, {-s,  s,  s}, {-s,  s, -s}},   // -X (_RightTex)
        {{-s,  s,  s}, { s,  s,  s}, { s,  s, -s}, {-s,  s, -s}},   // +Y (_UpTex)
        {{-s, -s, -s}, { s, -s, -s}, { s, -s,  s}, {-s, -s,  s}},   // -Y (_DownTex)
        {{-s, -s,  s}, { s, -s,  s}, { s,  s,  s}, {-s,  s,  s}},   // +Z (_FrontTex)
        {{ s, -s, -s}, {-s, -s, -s}, {-s,  s, -s}, { s,  s, -s}},   // -Z (_BackTex)
    };
    static const float uv[8] = {0, 0, 1, 0, 1, 1, 0, 1};
    static const unsigned short idx[6] = {0, 1, 2, 0, 2, 3};
    for (int f = 0; f < 6; f++) {
        if (!sky[f]) continue;
        glBindTexture(GL_TEXTURE_2D, sky[f]);
        glVertexPointer(3, GL_FLOAT, 0, faces[f]);
        glTexCoordPointer(2, GL_FLOAT, 0, uv);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, idx);
    }
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
}

void Level::draw(const Vec3 &camPos, const Mat4 &view, const Mat4 &proj, RenderStats &st) {
    drawSky(view, proj);
    Frustum fr;
    fr.build(proj * view);
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(proj.m);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(view.m);
    // Light positions/directions are transformed by the modelview current at
    // the glLightfv call, so set them all with the view matrix loaded.
    float amb[4] = {ambient[0], ambient[1], ambient[2], 1};
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, amb);
    float zero[4] = {0, 0, 0, 1};
    for (int l = 0; l < 2; l++) {
        GLenum L = GL_LIGHT0 + l;
        if (l < (int)dirLights.size()) {
            const DirLight &d = dirLights[l];
            float ld[4] = {-d.dir.x, -d.dir.y, -d.dir.z, 0};   // GL wants the direction towards the light
            float c[4] = {d.color[0], d.color[1], d.color[2], 1};
            glEnable(L);
            glLightfv(L, GL_POSITION, ld);
            glLightfv(L, GL_DIFFUSE, c);
            glLightfv(L, GL_AMBIENT, zero);
            glLightfv(L, GL_SPECULAR, zero);
        } else glDisable(L);
    }
    for (int k = 0; k < 4; k++) {
        GLenum L = GL_LIGHT2 + k;
        glLightfv(L, GL_AMBIENT, zero);
        glLightfv(L, GL_SPECULAR, zero);
        { float v_ = 1; glLightfv(L, GL_CONSTANT_ATTENUATION, &v_); }
        { float v_ = 0; glLightfv(L, GL_LINEAR_ATTENUATION, &v_); }
        glDisable(L);
    }

    // Transparent items back to front.
    transparent.clear();
    size_t firstT = items.size();
    for (size_t i = 0; i < items.size(); i++)
        if (materials[items[i].mat].blend >= Blend::Alpha) { firstT = i; break; }
    for (size_t i = firstT; i < items.size(); i++) transparent.push_back((int)i);
    std::sort(transparent.begin(), transparent.end(), [&](int a, int b) {
        Vec3 da = items[a].center - camPos, db = items[b].center - camPos;
        return dot(da, da) > dot(db, db);
    });

    int lastMat = -1, lastMesh = -1;
    signed char lastLights[4] = {-2, -2, -2, -2};
    bool lastMirror = false;
    glFrontFace(GL_CW);   // Unity front faces are clockwise on screen
    auto drawOne = [&](const DrawItem &it) {
        if (!fr.sphere(it.center, it.radius)) { st.culled++; return; }
        const Material &mat = materials[it.mat];
        if (it.mat != lastMat) { applyMaterial(mat); lastMat = it.mat; }
        if (it.mirrored != lastMirror) { glFrontFace(it.mirrored ? GL_CCW : GL_CW); lastMirror = it.mirrored; }
        if (mat.lit && memcmp(lastLights, it.lights, 4)) {
            glLoadMatrixf(view.m);
            for (int k = 0; k < 4; k++) {
                GLenum L = GL_LIGHT2 + k;
                if (it.lights[k] < 0) { glDisable(L); continue; }
                const PointLight &pl = pointLights[it.lights[k]];
                float p[4] = {pl.pos.x, pl.pos.y, pl.pos.z, 1};
                float c[4] = {pl.color[0], pl.color[1], pl.color[2], 1};
                glEnable(L);
                glLightfv(L, GL_POSITION, p);
                glLightfv(L, GL_DIFFUSE, c);
                // Unity's legacy point light falloff, roughly 1 / (1 + 25 (d/range)^2)
                { float v_ = 25.0f / (pl.range * pl.range); glLightfv(L, GL_QUADRATIC_ATTENUATION, &v_); }
            }
            memcpy(lastLights, it.lights, 4);
        }
        const Mesh &m = meshes[it.mesh];
        if (it.mesh != lastMesh) {
            glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ibo);
            glVertexPointer(3, GL_FLOAT, 0, (void *)m.offPos);
            if (m.offNrm >= 0) { glEnableClientState(GL_NORMAL_ARRAY); glNormalPointer(GL_FLOAT, 0, (void *)m.offNrm); }
            else glDisableClientState(GL_NORMAL_ARRAY);
            if (m.offUv0 >= 0) { glEnableClientState(GL_TEXTURE_COORD_ARRAY); glTexCoordPointer(2, GL_FLOAT, 0, (void *)m.offUv0); }
            else glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            lastMesh = it.mesh;
        }
        if (mat.vertexColor && m.offCol >= 0) {
            glEnableClientState(GL_COLOR_ARRAY);
            glColorPointer(4, GL_UNSIGNED_BYTE, 0, (void *)m.offCol);
        } else {
            glDisableClientState(GL_COLOR_ARRAY);
            glColor4f(mat.color[0], mat.color[1], mat.color[2], mat.color[3]);
        }
        glLoadMatrixf((view * it.world).m);
        const SubMesh &sm = m.subs[it.sub];
        glDrawElements(GL_TRIANGLES, sm.count, m.idx32 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT,
                       (void *)(uintptr_t)(sm.first * (m.idx32 ? 4 : 2)));
        st.drawn++;
        st.tris += sm.count / 3;
    };
    for (size_t i = 0; i < firstT; i++) drawOne(items[i]);
    for (int i : transparent) drawOne(items[i]);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glDisableClientState(GL_COLOR_ARRAY);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
}
