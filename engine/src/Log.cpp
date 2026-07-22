#include "engine/Log.h"

#include <SDL3/SDL.h>

#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace engine {
namespace {

    std::ofstream g_file;
    std::mutex g_mutex;
    SDL_LogOutputFunction g_prev = nullptr; // the handler we chain to (console)
    void* g_prevUser = nullptr;

    const char* priorityName(SDL_LogPriority p) {
        switch (p) {
            case SDL_LOG_PRIORITY_VERBOSE:  return "VERBOSE";
            case SDL_LOG_PRIORITY_DEBUG:    return "DEBUG";
            case SDL_LOG_PRIORITY_INFO:     return "INFO";
            case SDL_LOG_PRIORITY_WARN:     return "WARN";
            case SDL_LOG_PRIORITY_ERROR:    return "ERROR";
            case SDL_LOG_PRIORITY_CRITICAL: return "CRITICAL";
            default:                        return "LOG";
        }
    }

    void SDLCALL logCallback(void* userdata, int category, SDL_LogPriority priority,
                             const char* message) {
        (void)userdata;
        (void)category;
        if (g_prev) g_prev(g_prevUser, category, priority, message); // keep console

        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_file.is_open()) return;

        std::time_t t = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char stamp[32];
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm);
        g_file << '[' << stamp << "] [" << priorityName(priority) << "] "
               << (message ? message : "") << '\n';
        g_file.flush(); // a crash right after a log line must not lose it
    }

    std::filesystem::path genPath(const std::filesystem::path& logs, int i) {
        return logs / (i == 0 ? std::string("game.log")
                              : "game." + std::to_string(i) + ".log");
    }

} // namespace

void Log::init(const std::string& dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path logs = fs::path(dir) / "logs";
    fs::create_directories(logs, ec);

    // Rotate game.2.log -> game.3.log, ... , game.log -> game.1.log, dropping
    // the oldest. Three prior generations is plenty of recent history.
    constexpr int kGenerations = 3;
    fs::remove(genPath(logs, kGenerations), ec);
    for (int i = kGenerations - 1; i >= 0; --i) {
        if (fs::exists(genPath(logs, i), ec)) {
            fs::rename(genPath(logs, i), genPath(logs, i + 1), ec);
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_file.open(genPath(logs, 0), std::ios::out | std::ios::trunc);
    }

    // Capture the current (default) handler, then chain to it from ours so both
    // the console and the file receive every message.
    SDL_GetLogOutputFunction(&g_prev, &g_prevUser);
    SDL_SetLogOutputFunction(logCallback, nullptr);
    SDL_Log("Log started -> %s", genPath(logs, 0).string().c_str());
}

void Log::shutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file.is_open()) {
        g_file.flush();
        g_file.close();
    }
}

} // namespace engine
