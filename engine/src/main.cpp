// SEUM Vita engine: milestone 2 level viewer.
//
// Controls  Vita: left stick move, right stick look, L/R down/up, Cross fast,
//                 Triangle next level, Square previous level, Start quit.
//           PC:   WASD + mouse, Q/E down/up, Shift fast, N/P level, Esc quit.
//
// Linux test harness:
//   seum --data DIR [--level N] [--screenshot out.tga] [--frames N]
//        [--cam spawn|overview|x,y,z,yawDeg,pitchDeg] [--hidden]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <string>
#include "level.h"
#include "platform.h"

struct Camera {
    Vec3 pos;
    float yaw = 0, pitch = 0;   // degrees, Unity convention (yaw about +Y, pitch about +X = look down)
    Quat rot() const { return eulerDeg(pitch, yaw, 0); }
};

static void frameCamera(const Level &lv, Camera &cam, const std::string &mode) {
    float x, y, z, yaw, pitch;
    if (sscanf(mode.c_str(), "%f,%f,%f,%f,%f", &x, &y, &z, &yaw, &pitch) == 5) {
        cam.pos = Vec3(x, y, z); cam.yaw = yaw; cam.pitch = pitch;
        return;
    }
    if (mode != "overview" && lv.hasSpawn) {
        // FPSInputController: Quaternion.Euler(0, spawnPointRotation.eulerAngles.y, 0), +45 on level 126
        cam.pos = lv.spawnPos + Vec3(0, 1.7f, 0);   // eye height
        cam.yaw = unityYawDeg(lv.spawnRot) + (lv.id == 126 ? 45.0f : 0.0f);
        cam.pitch = 5;
        return;
    }
    Vec3 c = (lv.boundsMin + lv.boundsMax) * 0.5f, e = lv.boundsMax - lv.boundsMin;
    float r = std::max(e.x, e.z) * 0.6f;
    cam.pos = c + Vec3(-r * 0.7f, r * 0.6f, -r * 0.7f);
    cam.yaw = 45;
    cam.pitch = 32;
}

static bool loadLevel(Level &lv, int id, TextureCache &tex) {
    lv.unload();
    tex.clear();
    if (!lv.load(id, tex)) return false;
    printf("level %d \"%s\" (%s): %d objects, %zu draw items, %zu meshes (%.1f MB), textures %.1f MB, "
           "%zu dir + %zu point lights, sky %s, spawn %s, %.2fs\n",
           id, lv.meta.name.c_str(), lv.meta.environment.c_str(), lv.objectCount, lv.items.size(), lv.meshes.size(),
           lv.meshBytes / 1048576.0, tex.bytes() / 1048576.0, lv.dirLights.size(), lv.pointLights.size(),
           lv.sky[0] || lv.sky[4] ? "yes" : "no", lv.hasSpawn ? "yes" : "no", lv.loadSeconds);
    return true;
}

int main(int argc, char **argv) {
    std::string data, shot, camMode = "spawn";
    int levelArg = -1, frames = 0;
    bool hidden = false;
    for (int i = 1; i < argc; i++) {
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (!strcmp(argv[i], "--data")) data = next();
        else if (!strcmp(argv[i], "--level")) levelArg = atoi(next().c_str());
        else if (!strcmp(argv[i], "--screenshot")) shot = next();
        else if (!strcmp(argv[i], "--frames")) frames = atoi(next().c_str());
        else if (!strcmp(argv[i], "--cam")) camMode = next();
        else if (!strcmp(argv[i], "--hidden")) hidden = true;
    }
    if (!shot.empty() && frames <= 0) frames = 3;
    if (!plat::init(960, 544, hidden)) return 1;
    if (!data.empty()) plat::setDataRoot(data);

    Manifest man;
    if (!man.load(plat::dataRoot())) { printf("no data at %s\n", plat::dataRoot().c_str()); plat::shutdown(); return 1; }
    TextureCache tex(man);
    size_t li = 0;
    if (levelArg >= 0) {
        auto it = std::find(man.levels.begin(), man.levels.end(), levelArg);
        li = it != man.levels.end() ? it - man.levels.begin() : 0;
    } else {
        auto it = std::find(man.levels.begin(), man.levels.end(), 1);   // first real level
        li = it != man.levels.end() ? it - man.levels.begin() : 0;
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glEnableClientState(GL_VERTEX_ARRAY);
#ifdef GL_NORMALIZE
    glEnable(GL_NORMALIZE);
#endif
    glViewport(0, 0, plat::width(), plat::height());

    Level lv;
    Camera cam;
    if (loadLevel(lv, man.levels[li], tex)) frameCamera(lv, cam, camMode);
    // Settings.fov default 70 (vertical); CharacterView: near = lerp(0.16, 0.1, (fov-60)/30); far 1000.
    const float fov = 70.0f;
    const Mat4 proj = perspectiveLH(fov, plat::width() / (float)plat::height(),
                                    0.16f + (0.1f - 0.16f) * (fov - 60.0f) / 30.0f, 1000.0f);

    double last = plat::seconds(), statT = last;
    int frame = 0, statFrames = 0;
    RenderStats acc;
    for (;;) {
        double now = plat::seconds();
        float dt = std::min(0.1f, (float)(now - last));
        last = now;
        plat::Input in;
        plat::poll(in);
        if (in.quit) break;
        if (in.nextLevel || in.prevLevel) {
            li = (li + (in.nextLevel ? 1 : man.levels.size() - 1)) % man.levels.size();
            if (loadLevel(lv, man.levels[li], tex)) frameCamera(lv, cam, "spawn");
        }
        if (in.lookIsMouse) { cam.yaw += in.lookX; cam.pitch += in.lookY; }
        else { cam.yaw += in.lookX * 150 * dt; cam.pitch += in.lookY * 110 * dt; }
        cam.pitch = std::max(-89.0f, std::min(89.0f, cam.pitch));
        Quat q = cam.rot();
        float speed = (in.fast ? 40.0f : 10.0f) * dt;
        cam.pos += rotate(q, Vec3(in.moveX, 0, in.moveY)) * speed + Vec3(0, in.rise * speed, 0);

        glClearColor(0, 0, 0, 1);   // PlayerCamera background; Hell2/Hell4 have no skybox material
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        RenderStats st;
        lv.draw(cam.pos, viewFrom(cam.pos, q), proj, st);
        acc.drawn += st.drawn; acc.culled += st.culled; acc.tris += st.tris;
        statFrames++;
        frame++;
        if (!shot.empty() && frame == frames) {
            plat::screenshot(shot);
            printf("screenshot %s: drawn %d, culled %d, %ld tris, cam %.1f,%.1f,%.1f yaw %.0f pitch %.0f\n",
                   shot.c_str(), st.drawn, st.culled, st.tris, cam.pos.x, cam.pos.y, cam.pos.z, cam.yaw, cam.pitch);
            break;
        }
        plat::swap();
        if (now - statT >= 2.0) {
            printf("fps %.1f  draws %d  culled %d  tris %ld\n", statFrames / (now - statT), acc.drawn / statFrames,
                   acc.culled / statFrames, acc.tris / statFrames);
            statT = now; statFrames = 0; acc = RenderStats();
        }
    }
    lv.unload();
    tex.clear();
    plat::shutdown();
    return 0;
}
