/* Minimal toolchain smoke test: SDL2 input + vitaGL clear-colour loop.
 * Prints to stdout so vita-agent-use / psp2-tooling TTY capture can see it. */
#include <stdio.h>
#include <math.h>
#include <vitaGL.h>
#include <SDL2/SDL.h>

int main(void) {
    printf("[hello-vitagl] start\n");
    vglInit(0x800000);
    SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS);
    SDL_GameController *pad = SDL_GameControllerOpen(0);

    unsigned frame = 0;
    int running = 1;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_CONTROLLERBUTTONDOWN &&
                e.cbutton.button == SDL_CONTROLLER_BUTTON_START) running = 0;
        }
        float t = frame / 60.0f;
        glClearColor(0.5f + 0.5f * sinf(t), 0.2f, 0.5f + 0.5f * cosf(t), 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        vglSwapBuffers(GL_FALSE);
        if ((frame++ % 300) == 0) printf("[hello-vitagl] frame %u\n", frame);
    }
    printf("[hello-vitagl] exit\n");
    if (pad) SDL_GameControllerClose(pad);
    SDL_Quit();
    return 0;
}
