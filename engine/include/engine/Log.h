#pragma once

#include <string>

namespace engine {

    // A rolling log file that tees every SDL_Log(...) message to disk. init()
    // installs an SDL log-output hook that forwards to the previous handler (so
    // console output is unchanged) and also appends a timestamped line to
    // <dir>/logs/game.log — every existing SDL_Log call site is captured with
    // no code change. Prior generations rotate to game.1.log .. game.3.log, so
    // each launch starts a fresh log while keeping recent history for bug
    // reports. shutdown() flushes and closes. Both are no-ops if the file can't
    // be opened; logging never disrupts the game.
    namespace Log {
        void init(const std::string& dir);
        void shutdown();
    } // namespace Log

} // namespace engine
