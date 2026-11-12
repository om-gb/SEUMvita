#include "platform.h"
#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <vector>

namespace plat {

static int g_w = 960, g_h = 544;
static std::string g_root;
static SDL_GameController *g_pad = nullptr;
static bool g_prevNext = false, g_prevPrev = false;
#ifndef __vita__
static SDL_Window *g_win = nullptr;
static SDL_GLContext g_ctx = nullptr;
#endif

static float axis(SDL_GameControllerAxis a) {
    if (!g_pad) return 0;
    float v = SDL_GameControllerGetAxis(g_pad, a) / 32767.0f;
    const float dead = 0.18f;   // Vita sticks drift a little
    if (fabsf(v) < dead) return 0;
    return (v - (v > 0 ? dead : -dead)) / (1 - dead);
}

bool init(int width, int height, bool hidden) {
    g_w = width; g_h = height;
#ifdef __vita__
    (void)hidden;
    g_root = "ux0:data/seum/";
    // 960x544, 2x MSAA; textures and buffers go to the VRAM pools first.
    vglInitExtended(0, g_w, g_h, 24 * 1024 * 1024, SCE_GXM_MULTISAMPLE_2X);
    if (SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) != 0) {
        printf("SDL_Init: %s\n", SDL_GetError());
        return false;
    }
#else
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        printf("SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    g_win = SDL_CreateWindow("SEUM", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, g_w, g_h,
                             SDL_WINDOW_OPENGL | (hidden ? SDL_WINDOW_HIDDEN : 0));
    if (!g_win) { printf("SDL_CreateWindow: %s\n", SDL_GetError()); return false; }
    g_ctx = SDL_GL_CreateContext(g_win);
    if (!g_ctx) { printf("GL context: %s\n", SDL_GetError()); return false; }
    SDL_GL_SetSwapInterval(1);
    if (!hidden) SDL_SetRelativeMouseMode(SDL_TRUE);
    printf("GL: %s / %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
#endif
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i)) { g_pad = SDL_GameControllerOpen(i); break; }
    return true;
}

void shutdown() {
    if (g_pad) SDL_GameControllerClose(g_pad);
#ifdef __vita__
    // vitaGL has no teardown call; the OS reclaims GPU memory on exit.
#else
    SDL_GL_DeleteContext(g_ctx);
    SDL_DestroyWindow(g_win);
#endif
    SDL_Quit();
}

void poll(Input &in) {
    Input out;
    bool next = false, prev = false;
    SDL_Event e;
    float mdx = 0, mdy = 0;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) out.quit = true;
        if (e.type == SDL_MOUSEMOTION) { mdx += e.motion.xrel; mdy += e.motion.yrel; }
        if (e.type == SDL_CONTROLLERDEVICEADDED && !g_pad) g_pad = SDL_GameControllerOpen(e.cdevice.which);
    }
    out.moveX = axis(SDL_CONTROLLER_AXIS_LEFTX);
    out.moveY = -axis(SDL_CONTROLLER_AXIS_LEFTY);
    out.lookX = axis(SDL_CONTROLLER_AXIS_RIGHTX);
    out.lookY = axis(SDL_CONTROLLER_AXIS_RIGHTY);
    if (g_pad) {
        auto b = [](SDL_GameControllerButton x) { return SDL_GameControllerGetButton(g_pad, x) != 0; };
        out.rise = (b(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) ? 1.f : 0.f) - (b(SDL_CONTROLLER_BUTTON_LEFTSHOULDER) ? 1.f : 0.f);
        out.fast = b(SDL_CONTROLLER_BUTTON_A);
        next = b(SDL_CONTROLLER_BUTTON_Y);
        prev = b(SDL_CONTROLLER_BUTTON_X);
        out.quit = out.quit || b(SDL_CONTROLLER_BUTTON_START);
    }
#ifndef __vita__
    const Uint8 *k = SDL_GetKeyboardState(nullptr);
    if (k[SDL_SCANCODE_W]) out.moveY += 1;
    if (k[SDL_SCANCODE_S]) out.moveY -= 1;
    if (k[SDL_SCANCODE_D]) out.moveX += 1;
    if (k[SDL_SCANCODE_A]) out.moveX -= 1;
    if (k[SDL_SCANCODE_E]) out.rise += 1;
    if (k[SDL_SCANCODE_Q]) out.rise -= 1;
    out.fast = out.fast || k[SDL_SCANCODE_LSHIFT];
    next = next || k[SDL_SCANCODE_N];
    prev = prev || k[SDL_SCANCODE_P];
    out.quit = out.quit || k[SDL_SCANCODE_ESCAPE];
    if (mdx != 0 || mdy != 0) { out.lookIsMouse = true; out.lookX = mdx * 0.15f; out.lookY = mdy * 0.15f; }
#endif
    out.nextLevel = next && !g_prevNext;
    out.prevLevel = prev && !g_prevPrev;
    g_prevNext = next; g_prevPrev = prev;
    in = out;
}

void swap() {
#ifdef __vita__
    vglSwapBuffers(GL_FALSE);
#else
    SDL_GL_SwapWindow(g_win);
#endif
}

double seconds() { return SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency(); }
int width() { return g_w; }
int height() { return g_h; }
std::string dataRoot() { return g_root; }
void setDataRoot(const std::string &root) { g_root = root.empty() || root.back() == '/' ? root : root + "/"; }

bool screenshot(const std::string &path) {
#ifdef __vita__
    (void)path;
    return false;
#else
    std::vector<unsigned char> px(g_w * g_h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, g_w, g_h, GL_BGR, GL_UNSIGNED_BYTE, px.data());
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return false;
    unsigned char hdr[18] = {0, 0, 2};
    hdr[12] = g_w & 255; hdr[13] = g_w >> 8; hdr[14] = g_h & 255; hdr[15] = g_h >> 8; hdr[16] = 24;
    fwrite(hdr, 1, 18, f);
    fwrite(px.data(), 1, px.size(), f);   // GL rows are bottom-up, as TGA expects by default
    fclose(f);
    return true;
#endif
}

}  // namespace plat
