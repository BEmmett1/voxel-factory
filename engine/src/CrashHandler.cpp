#include "engine/CrashHandler.h"

#include <SDL3/SDL.h>

#include <ctime>
#include <filesystem>
#include <string>

namespace engine {
namespace {

    // A filesystem-safe local timestamp (YYYYMMDD-HHMMSS). Used to name the
    // dump file; on POSIX it is formatted at install time, never in the signal
    // handler (strftime is not async-signal-safe).
    std::string crashTimestamp() {
        std::time_t t = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tm);
        return buf;
    }

} // namespace
} // namespace engine

#ifdef _WIN32

#include <windows.h>
// dbghelp.h must follow windows.h.
#include <dbghelp.h>

namespace engine {
namespace {

    std::string g_crashDir;

    LONG WINAPI crashFilter(EXCEPTION_POINTERS* info) {
        const std::string path = g_crashDir + "/crash-" + crashTimestamp() + ".dmp";
        HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mei{};
            mei.ThreadId = GetCurrentThreadId();
            mei.ExceptionPointers = info;
            mei.ClientPointers = FALSE;
            MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                              MiniDumpNormal, &mei, nullptr, nullptr);
            CloseHandle(file);
            SDL_Log("Crash: wrote minidump %s", path.c_str());
        } else {
            SDL_Log("Crash: could not open minidump file %s", path.c_str());
        }
        return EXCEPTION_EXECUTE_HANDLER; // let the process terminate
    }

} // namespace

void CrashHandler::install(const std::string& dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path crashes = fs::path(dir) / "crashes";
    fs::create_directories(crashes, ec);
    g_crashDir = crashes.string();
    SetUnhandledExceptionFilter(crashFilter);
}

} // namespace engine

#else // POSIX (macOS / Linux)

#include <csignal>
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>

namespace engine {
namespace {

    // The full crash-file path, precomputed at install time so the handler only
    // does async-signal-safe work.
    char g_crashPath[1024] = {0};

    void writeStr(int fd, const char* s) {
        std::size_t len = 0;
        while (s[len]) ++len;
        if (::write(fd, s, len) < 0) { /* nothing safe to do on failure */ }
    }

    void crashSignal(int sig) {
        int fd = ::open(g_crashPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            writeStr(fd, "voxel-factory crashed (signal ");
            if (sig >= 10) {
                char tens = static_cast<char>('0' + (sig / 10) % 10);
                if (::write(fd, &tens, 1) < 0) { }
            }
            char ones = static_cast<char>('0' + sig % 10);
            if (::write(fd, &ones, 1) < 0) { }
            writeStr(fd, ")\n");

            void* frames[64];
            int count = backtrace(frames, 64);
            backtrace_symbols_fd(frames, count, fd);
            ::close(fd);
        }
        // Restore the default disposition and re-raise so the OS still reports.
        std::signal(sig, SIG_DFL);
        ::raise(sig);
    }

} // namespace

void CrashHandler::install(const std::string& dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path crashes = fs::path(dir) / "crashes";
    fs::create_directories(crashes, ec);
    const std::string path = (crashes / ("crash-" + crashTimestamp() + ".txt")).string();
    std::size_t n = path.copy(g_crashPath, sizeof(g_crashPath) - 1);
    g_crashPath[n] = '\0';

    for (int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS}) {
        std::signal(sig, crashSignal);
    }
}

} // namespace engine

#endif
