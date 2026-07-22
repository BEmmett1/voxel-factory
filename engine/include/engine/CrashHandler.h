#pragma once

#include <string>

namespace engine {

    // Installs a process-wide crash handler that writes a dump under
    // <dir>/crashes/ when the process faults, so a player's bug report can
    // carry an actionable artifact. On Windows an unhandled-exception filter
    // writes a real minidump (crash-<timestamp>.dmp via MiniDumpWriteDump); on
    // POSIX a signal handler writes an async-signal-safe backtrace
    // (crash-<timestamp>.txt) then re-raises the default handler so the OS
    // still produces its own report. Best-effort and never throws.
    namespace CrashHandler {
        void install(const std::string& dir);
    } // namespace CrashHandler

} // namespace engine
