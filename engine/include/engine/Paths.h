#pragma once

#include <string>

namespace engine {

    // The OS-preferred per-user data directory for this app (a thin wrapper over
    // SDL_GetPrefPath), returned with a trailing separator. Empty string if SDL
    // can't resolve one. The save/settings files and the logs/ and crashes/
    // folders all live under it, so main() and the game resolve it identically.
    std::string prefDir(const char* org, const char* app);

} // namespace engine
