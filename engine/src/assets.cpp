#include "assets.h"
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>

#ifndef GL_COMPRESSED_RGB_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT 0x83F0
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif

bool readFile(const std::string &path, std::vector<uint8_t> &out) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? n : 0);
    bool ok = n <= 0 || fread(out.data(), 1, n, f) == (size_t)n;
    fclose(f);
    return ok;
}

bool Manifest::load(const std::string &root) {
    json_error_t err;
    json_t *j = json_load_file((root + "manifest.json").c_str(), 0, &err);
    if (!j) { printf("manifest: %s (line %d)\n", err.text, err.line); return false; }
    const char *key;
    json_t *v;
    json_object_foreach(json_object_get(j, "levels"), key, v) {
        if (!json_object_get(v, "error")) levels.push_back(atoi(key));
    }
    std::sort(levels.begin(), levels.end());
    json_object_foreach(json_object_get(j, "textures"), key, v) {
        if (json_object_get(v, "error")) continue;
        TextureInfo t;
        t.file = json_string_value(json_object_get(v, "file"));
        t.fmt = json_string_value(json_object_get(v, "fmt"));
        t.w = json_integer_value(json_object_get(v, "w"));
        t.h = json_integer_value(json_object_get(v, "h"));
        t.mips = json_integer_value(json_object_get(v, "mips"));
        t.alpha = json_is_true(json_object_get(v, "alpha"));
        textures[key] = t;
    }
    // Unity's 6-sided skybox: _LeftTex is +X and _RightTex is -X.
    static const char *faceProps[6] = {"_LeftTex", "_RightTex", "_UpTex", "_DownTex", "_FrontTex", "_BackTex"};
    json_object_foreach(json_object_get(j, "resourceMaterials"), key, v) {
        const char *sh = json_string_value(json_object_get(v, "shader"));
        if (!sh || strcmp(sh, "Skybox/6 Sided")) continue;
        SkyboxInfo sb;
        json_t *texs = json_object_get(v, "textures");
        for (int f = 0; f < 6; f++) {
            const char *id = json_string_value(json_object_get(json_object_get(texs, faceProps[f]), "tex"));
            if (id) sb.faces[f] = id;
        }
        json_t *tint = json_object_get(json_object_get(v, "colors"), "_Tint");
        for (int k = 0; k < 3 && tint; k++) sb.tint[k] = json_number_value(json_array_get(tint, k));
        json_t *ex = json_object_get(json_object_get(v, "floats"), "_Exposure");
        if (ex) sb.exposure = json_number_value(ex);
        skyboxes[key] = sb;
    }
    json_decref(j);
    printf("manifest: %zu levels, %zu textures, %zu skyboxes\n", levels.size(), textures.size(), skyboxes.size());
    return !levels.empty();
}

GLuint TextureCache::get(const std::string &id) {
    auto it = cache.find(id);
    if (it != cache.end()) return it->second;
    GLuint tex = 0;
    auto ti = man.textures.find(id);
    std::vector<uint8_t> d;
    if (ti != man.textures.end() && readFile(plat::dataRoot() + ti->second.file, d) && d.size() > 128 &&
        !memcmp(d.data(), "DDS ", 4)) {
        const TextureInfo &t = ti->second;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        size_t off = 128;
        int w = t.w, h = t.h;
        int levels = std::max(1, t.mips);
        for (int lv = 0; lv < levels && off < d.size(); lv++) {
            if (t.fmt == "RGBA8") {
                size_t sz = (size_t)w * h * 4;
                glTexImage2D(GL_TEXTURE_2D, lv, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, d.data() + off);
                off += sz; totalBytes += sz;
            } else {
                bool dxt5 = t.fmt == "DXT5";
                size_t sz = (size_t)std::max(1, (w + 3) / 4) * std::max(1, (h + 3) / 4) * (dxt5 ? 16 : 8);
                glCompressedTexImage2D(GL_TEXTURE_2D, lv,
                                       dxt5 ? GL_COMPRESSED_RGBA_S3TC_DXT5_EXT : GL_COMPRESSED_RGB_S3TC_DXT1_EXT,
                                       w, h, 0, sz, d.data() + off);
                off += sz; totalBytes += sz;
            }
            w = std::max(1, w / 2); h = std::max(1, h / 2);
        }
#ifdef GL_TEXTURE_MAX_LEVEL
        // The DXT chain stops at 4x4; without this desktop GL treats the
        // texture as incomplete and samples white.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels - 1);
#endif
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, levels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }
    cache[id] = tex;
    return tex;
}

void TextureCache::clear() {
    for (auto &kv : cache)
        if (kv.second) glDeleteTextures(1, &kv.second);
    cache.clear();
    totalBytes = 0;
}

template <typename T> static T rd(const uint8_t *&p) { T v; memcpy(&v, p, sizeof v); p += sizeof v; return v; }

bool loadMeshPool(const std::string &path, std::vector<Mesh> &out, size_t *gpuBytes) {
    std::vector<uint8_t> d;
    if (!readFile(path, d) || d.size() < 12 || memcmp(d.data(), "SMSH", 4)) {
        printf("mesh pool %s: missing or bad\n", path.c_str());
        return false;
    }
    const uint8_t *p = d.data() + 4;
    uint32_t ver = rd<uint32_t>(p), n = rd<uint32_t>(p);
    if (ver != 1) return false;
    out.resize(n);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t size = rd<uint32_t>(p);
        const uint8_t *rec = p, *q = p;
        p += size;
        Mesh &m = out[i];
        uint32_t nl = rd<uint32_t>(q);
        m.name.assign((const char *)q, nl);
        q += nl; q += (4 - ((q - rec) & 3)) & 3;
        uint32_t flags = rd<uint32_t>(q);
        m.vertexCount = rd<uint32_t>(q);
        m.indexCount = rd<uint32_t>(q);
        uint32_t sc = rd<uint32_t>(q);
        for (uint32_t s = 0; s < sc; s++) {
            SubMesh sm;
            sm.first = rd<uint32_t>(q); sm.count = rd<uint32_t>(q); rd<uint32_t>(q);
            m.subs.push_back(sm);
        }
        m.aabbMin.x = rd<float>(q); m.aabbMin.y = rd<float>(q); m.aabbMin.z = rd<float>(q);
        m.aabbMax.x = rd<float>(q); m.aabbMax.y = rd<float>(q); m.aabbMax.z = rd<float>(q);
        // Vertex arrays are already laid out back to back: upload them as one VBO.
        const uint8_t *vstart = q;
        long off = 0, vc = m.vertexCount;
        m.offPos = off; off += vc * 12;
        if (flags & 1) { m.offNrm = off; off += vc * 12; }
        if (flags & 2) { m.offUv0 = off; off += vc * 8; }
        if (flags & 4) { m.offUv1 = off; off += vc * 8; }
        if (flags & 8) { m.offCol = off; off += vc * 4; }
        m.idx32 = flags & 16;
        const uint8_t *istart = vstart + off;
        size_t isz = (size_t)m.indexCount * (m.idx32 ? 4 : 2);
        if (vc == 0 || m.indexCount == 0) continue;
        glGenBuffers(1, &m.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
        glBufferData(GL_ARRAY_BUFFER, off, vstart, GL_STATIC_DRAW);
        glGenBuffers(1, &m.ibo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, isz, istart, GL_STATIC_DRAW);
        if (gpuBytes) *gpuBytes += off + isz;
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    return true;
}

void freeMeshPool(std::vector<Mesh> &meshes) {
    for (Mesh &m : meshes) {
        if (m.vbo) glDeleteBuffers(1, &m.vbo);
        if (m.ibo) glDeleteBuffers(1, &m.ibo);
    }
    meshes.clear();
}
