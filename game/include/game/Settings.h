#pragma once

#include <SDL3/SDL_scancode.h>

#include <array>
#include <string>

// User settings: display/audio/input knobs, applied live and persisted as a
// human-editable settings.cfg (KEY=VALUE) in the SDL pref dir next to the
// save. Missing file or bad values fall back to the member-initializer
// defaults below — Settings{} is the single source of truth.

// Rebindable gameplay actions. Order is load-bearing: it defines the
// settings.cfg BIND_* keys and the keybinds-panel row order. Esc, mouse
// buttons, hotbar 1-0, menu navigation, and the F3/F4 dev keys are fixed.
enum class Action : int {
    MoveForward,
    MoveBack,
    MoveLeft,
    MoveRight,
    Jump,
    Sprint,
    CraftMenu,
    Inventory,
    WrenchRotate,
    QuickSave,
    Help,
    Count
};
inline constexpr int kActionCount = static_cast<int>(Action::Count);

inline constexpr std::array<SDL_Scancode, kActionCount> kDefaultBinds = {
    SDL_SCANCODE_W,     SDL_SCANCODE_S,   SDL_SCANCODE_A,  SDL_SCANCODE_D,
    SDL_SCANCODE_SPACE, SDL_SCANCODE_LCTRL, SDL_SCANCODE_E, SDL_SCANCODE_TAB,
    SDL_SCANCODE_R,     SDL_SCANCODE_F5,  SDL_SCANCODE_F1};

// Value ranges shared by the UI steppers and the load-time clamp.
inline constexpr float kSensitivityMin = 0.02f;
inline constexpr float kSensitivityMax = 0.40f;
inline constexpr float kSensitivityStep = 0.02f;
inline constexpr float kVolumeMin = 0.0f;
inline constexpr float kVolumeMax = 1.0f;
inline constexpr float kVolumeStep = 0.1f;

struct Settings {
    bool  fullscreen  = false;
    bool  vsync       = true;
    float sensitivity = 0.12f; // mouse look, degrees per pixel
    float volume      = 0.8f;  // master audio, 0..1
    // SDL_SCANCODE_UNKNOWN = unbound (index 0 of Input's state array is
    // never set by events, so an unbound action is safely inert).
    std::array<SDL_Scancode, kActionCount> binds = kDefaultBinds;

    SDL_Scancode key(Action a) const { return binds[static_cast<int>(a)]; }
};

const char* actionName(Action a);   // "MOVE FORWARD", ... (UI row labels)
const char* actionCfgKey(Action a); // "BIND_FORWARD", ... (settings.cfg keys)

// Can this scancode be bound to an action? False for the reserved set
// (Esc/Enter/arrows/hotbar digits/F3/F4/unknown/out-of-range).
bool bindableScancode(SDL_Scancode sc);

// Uppercased SDL_GetScancodeName for UI display; "---" when unbound/unnamed.
std::string scancodeLabel(SDL_Scancode sc);

namespace SettingsIO {
    // false = no/unreadable file; `s` keeps its defaults where the file is
    // silent or invalid (unknown keys skipped, floats clamped, reserved or
    // duplicate binds rejected).
    bool load(const std::string& path, Settings& s);
    // Atomic like SaveSystem: write .tmp, rotate old file to .bak, rename in.
    bool save(const std::string& path, const Settings& s);
}
