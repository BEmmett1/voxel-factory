#include "engine/Paths.h"

#include <SDL3/SDL.h>

namespace engine {

    std::string prefDir(const char* org, const char* app) {
        std::string result;
        if (char* p = SDL_GetPrefPath(org, app)) { // owned by SDL
            result = p;
            SDL_free(p);
        }
        return result;
    }

} // namespace engine
