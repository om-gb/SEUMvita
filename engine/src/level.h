// A loaded level, assembled the way Game.changeLevel does it: the Game scene
// (lights, ambient), the level prefab placed at its LevelMetadata position, and
// the zone's environment prefab + skybox under a rotated container.
#pragma once
#include <string>
#include <vector>
#include "assets.h"

enum class Blend { Opaque, Cutout, Alpha, Additive, Multiply, Skip };

struct Material {
    std::string name, shader;
    GLuint tex = 0;
    float tiling[2] = {1, 1}, offset[2] = {0, 0};
    float color[4] = {1, 1, 1, 1};
    float cutoff = 0.5f;
    Blend blend = Blend::Opaque;
    bool lit = true;
    bool vertexColor = false;
};

struct DrawItem {
    int mesh, sub, mat;
    Mat4 world;
    bool mirrored;          // negative scale: flip winding
    Vec3 center;            // world bounding sphere
    float radius;
    signed char lights[4];  // nearest point lights, -1 = none
};

struct PointLight {
    Vec3 pos;
    float range;
    float color[3];
};

struct DirLight {
    Vec3 dir;               // direction the light travels (world)
    float color[3];
};

struct RenderStats {
    int drawn = 0, culled = 0;
    long tris = 0;
};

struct LevelMeta {
    std::string name, environment;
    Vec3 position;
    Quat envRotation;
};
bool loadLevelMeta(int id, LevelMeta &out);

class Level {
public:
    bool load(int id, TextureCache &tex);
    void unload();
    void draw(const Vec3 &camPos, const Mat4 &view, const Mat4 &proj, RenderStats &st);

    int id = -1;
    LevelMeta meta;
    std::vector<Mesh> meshes;
    std::vector<Material> materials;
    std::vector<DrawItem> items;
    std::vector<DirLight> dirLights;
    std::vector<PointLight> pointLights;
    float ambient[3] = {0.45f, 0.45f, 0.45f};
    GLuint sky[6] = {0, 0, 0, 0, 0, 0};    // +X -X +Y -Y +Z -Z
    float skyTint[3] = {1, 1, 1};
    Vec3 boundsMin, boundsMax;              // of the level prefab only
    bool hasSpawn = false;
    Vec3 spawnPos;
    Quat spawnRot;
    size_t meshBytes = 0;
    double loadSeconds = 0;
    int objectCount = 0;

private:
    enum class Root { Keep, Place, Parent };
    bool loadPart(const std::string &base, TextureCache &tex, Root mode, const Mat4 &xf, bool levelBounds);
    void loadSky(TextureCache &tex);
    void drawSky(const Mat4 &view, const Mat4 &proj);
    void assignLights();
    std::vector<int> transparent;    // scratch: back-to-front order
};
