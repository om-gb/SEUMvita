// Loaders for the exported data set (docs/DATA_FORMAT.md).
#pragma once
#include <stdint.h>
#include <string>
#include <unordered_map>
#include <vector>
#include "platform.h"
#include "vmath.h"

bool readFile(const std::string &path, std::vector<uint8_t> &out);

struct TextureInfo {
    std::string file, fmt;
    int w = 0, h = 0, mips = 1;
    bool alpha = false;
};

struct SkyboxInfo {
    std::string faces[6];     // texture ids: +X -X +Y -Y +Z -Z
    float tint[3] = {0.5f, 0.5f, 0.5f};
    float exposure = 1;
};

struct Manifest {
    std::vector<int> levels;                                   // sorted level ids
    std::unordered_map<std::string, TextureInfo> textures;     // texId -> info
    std::unordered_map<std::string, SkyboxInfo> skyboxes;      // resource path -> faces
    bool load(const std::string &root);
};

class TextureCache {
public:
    explicit TextureCache(const Manifest &m) : man(m) {}
    // GL texture for a texture id, loaded on first use; 0 if missing/broken.
    GLuint get(const std::string &id);
    void clear();
    size_t bytes() const { return totalBytes; }
    const Manifest &manifest() const { return man; }
private:
    const Manifest &man;
    std::unordered_map<std::string, GLuint> cache;
    size_t totalBytes = 0;
};

struct SubMesh { uint32_t first, count; };

struct Mesh {
    std::string name;
    GLuint vbo = 0, ibo = 0;
    uint32_t vertexCount = 0, indexCount = 0;
    bool idx32 = false;
    // byte offsets of each attribute inside the vbo; -1 when absent
    long offPos = 0, offNrm = -1, offUv0 = -1, offUv1 = -1, offCol = -1;
    std::vector<SubMesh> subs;
    Vec3 aabbMin, aabbMax;
};

// Loads a .smsh pool into GL buffers.
bool loadMeshPool(const std::string &path, std::vector<Mesh> &out, size_t *gpuBytes);
void freeMeshPool(std::vector<Mesh> &meshes);
