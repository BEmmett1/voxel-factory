#include "game/Settings.h"

#include <SDL3/SDL_keyboard.h>
#include <glm/glm.hpp>

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

    constexpr const char* kActionNames[kActionCount] = {
        "MOVE FORWARD", "MOVE BACK", "MOVE LEFT", "MOVE RIGHT",
        "JUMP", "SPRINT", "CRAFT MENU", "INVENTORY",
        "WRENCH ROTATE", "QUICK SAVE", "HELP"};

    constexpr const char* kActionCfgKeys[kActionCount] = {
        "BIND_FORWARD", "BIND_BACK", "BIND_LEFT", "BIND_RIGHT",
        "BIND_JUMP", "BIND_SPRINT", "BIND_CRAFT", "BIND_INVENTORY",
        "BIND_WRENCH", "BIND_QUICKSAVE", "BIND_HELP"};

    std::string trim(const std::string& s) {
        std::size_t b = 0, e = s.size();
        while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
        while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
        return s.substr(b, e - b);
    }

    // Strict float parse; returns false (leaving `out` untouched) on junk.
    bool parseFloat(const std::string& v, float& out) {
        const char* c = v.c_str();
        char* end = nullptr;
        const float f = std::strtof(c, &end);
        if (end == c || *end != '\0') return false;
        out = f;
        return true;
    }

    bool parseInt(const std::string& v, long& out) {
        const char* c = v.c_str();
        char* end = nullptr;
        const long n = std::strtol(c, &end, 10);
        if (end == c || *end != '\0') return false;
        out = n;
        return true;
    }

} // namespace

const char* actionName(Action a) { return kActionNames[static_cast<int>(a)]; }
const char* actionCfgKey(Action a) { return kActionCfgKeys[static_cast<int>(a)]; }

bool bindableScancode(SDL_Scancode sc) {
    if (sc <= SDL_SCANCODE_UNKNOWN || sc >= SDL_SCANCODE_COUNT) return false;
    switch (sc) {
        case SDL_SCANCODE_ESCAPE:   // engine-routed (onEscape)
        case SDL_SCANCODE_RETURN:   // menu activate
        case SDL_SCANCODE_KP_ENTER:
        case SDL_SCANCODE_UP:       // menu navigation
        case SDL_SCANCODE_DOWN:
        case SDL_SCANCODE_LEFT:
        case SDL_SCANCODE_RIGHT:
        case SDL_SCANCODE_1:        // hotbar slots
        case SDL_SCANCODE_2:
        case SDL_SCANCODE_3:
        case SDL_SCANCODE_4:
        case SDL_SCANCODE_5:
        case SDL_SCANCODE_6:
        case SDL_SCANCODE_7:
        case SDL_SCANCODE_8:
        case SDL_SCANCODE_9:
        case SDL_SCANCODE_0:
        case SDL_SCANCODE_F3:       // dev keys
        case SDL_SCANCODE_F4:
            return false;
        default:
            return true;
    }
}

std::string scancodeLabel(SDL_Scancode sc) {
    if (sc == SDL_SCANCODE_UNKNOWN) return "---";
    const char* name = SDL_GetScancodeName(sc);
    if (!name || !name[0]) return "---";
    std::string label(name);
    for (char& c : label) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return label;
}

namespace SettingsIO {

bool load(const std::string& path, Settings& s) {
    std::ifstream in(path);
    if (!in) return false;

    std::string line;
    while (std::getline(in, line)) {
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        const std::size_t eq = t.find('=');
        if (eq == std::string::npos) continue; // junk line
        const std::string key = trim(t.substr(0, eq));
        const std::string val = trim(t.substr(eq + 1));

        long n = 0;
        if (key == "FULLSCREEN") {
            if (parseInt(val, n)) s.fullscreen = n != 0;
        } else if (key == "VSYNC") {
            if (parseInt(val, n)) s.vsync = n != 0;
        } else if (key == "SENSITIVITY") {
            float f = 0.0f;
            if (parseFloat(val, f)) s.sensitivity = glm::clamp(f, kSensitivityMin, kSensitivityMax);
        } else if (key == "VOLUME") {
            float f = 0.0f;
            if (parseFloat(val, f)) s.volume = glm::clamp(f, kVolumeMin, kVolumeMax);
        } else {
            for (int a = 0; a < kActionCount; ++a) {
                if (key != kActionCfgKeys[a]) continue;
                // 0 = an EXPLICIT unbound (how conflict-stolen binds save);
                // rejecting it would resurrect the default and then the
                // duplicate pass below would strip the wrong action.
                if (parseInt(val, n) &&
                    (n == 0 || bindableScancode(static_cast<SDL_Scancode>(n)))) {
                    s.binds[static_cast<std::size_t>(a)] = static_cast<SDL_Scancode>(n);
                }
                break;
            }
            // Unknown keys are skipped: hand edits and future fields survive.
        }
    }

    // A key may appear on at most one action (hand edits can violate this);
    // first in enum order wins, later duplicates become unbound.
    for (int a = 0; a < kActionCount; ++a) {
        for (int b = a + 1; b < kActionCount; ++b) {
            if (s.binds[static_cast<std::size_t>(b)] != SDL_SCANCODE_UNKNOWN &&
                s.binds[static_cast<std::size_t>(b)] == s.binds[static_cast<std::size_t>(a)]) {
                s.binds[static_cast<std::size_t>(b)] = SDL_SCANCODE_UNKNOWN;
            }
        }
    }
    return true;
}

bool save(const std::string& path, const Settings& s) {
    // Same crash-safe rotation as SaveSystem: temp sibling, old file -> .bak,
    // rename in.
    const std::string tmpPath = path + ".tmp";
    {
        std::ofstream out(tmpPath, std::ios::trunc);
        if (!out) return false;
        out << "# Voxel Factory settings (KEY=VALUE; unknown keys ignored;\n";
        out << "# missing file or bad values fall back to defaults)\n";
        out << "FULLSCREEN=" << (s.fullscreen ? 1 : 0) << "\n";
        out << "VSYNC=" << (s.vsync ? 1 : 0) << "\n";
        out << "SENSITIVITY=" << s.sensitivity << "\n";
        out << "VOLUME=" << s.volume << "\n";
        for (int a = 0; a < kActionCount; ++a) {
            out << kActionCfgKeys[a] << "="
                << static_cast<int>(s.binds[static_cast<std::size_t>(a)]) << "\n";
        }
        out.close();
        if (!out.good()) return false;
    }

    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        std::filesystem::rename(path, path + ".bak", ec); // replaces any old .bak
        if (ec) return false;
    }
    std::filesystem::rename(tmpPath, path, ec);
    return !ec;
}

} // namespace SettingsIO
