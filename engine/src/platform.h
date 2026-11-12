// Window/GL context, input, time and file paths for Linux (SDL2 + desktop GL)
// and PS Vita (vitaGL for display, SDL2 for input).
#pragma once
#include <string>

#ifdef __vita__
#include <vitaGL.h>
#else
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#endif

namespace plat {

struct Input {
    float moveX = 0, moveY = 0;   // left stick / WASD, -1..1 (Y forward)
    float lookX = 0, lookY = 0;   // right stick (per second) or mouse (per frame, degrees)
    bool lookIsMouse = false;
    float rise = 0;               // R/L shoulder, E/Q
    bool fast = false;            // Cross / Shift
    bool nextLevel = false, prevLevel = false;   // edge-triggered
    bool quit = false;
};

bool init(int width, int height, bool hidden);
void shutdown();
void poll(Input &in);
void swap();
double seconds();
int width();
int height();
// Folder holding manifest.json: "ux0:data/seum/" on Vita, set by --data on Linux.
std::string dataRoot();
void setDataRoot(const std::string &root);
// Write the current back buffer to a 24-bit TGA (Linux test harness).
bool screenshot(const std::string &path);

}  // namespace plat
