// We run our own main(), so tell SDL not to hijack it.
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>

#include "game/VoxelGame.h"

#include <cstdio>
#include <exception>

int main(int /*argc*/, char** /*argv*/) {
    SDL_SetMainReady();
    try {
        VoxelGame game;
        game.run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Fatal: %s\n", e.what());
        return 1;
    }
    return 0;
}
