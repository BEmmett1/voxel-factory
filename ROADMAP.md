# Roadmap — paid Steam release, mid-2027

The core loop (mine → craft → power → automate, closed by craftable sources) is
complete. What remains is the commercial shell around it. Quarters below are
calendar quarters counted from July 2026.

## Q3 2026 — plumbing + foundations (in progress)

- [x] Release build presets (`CMakePresets.json`) + product version
      (`VOXEL_FACTORY_VERSION`, shown in title/F3)
- [x] CI: Windows Release build + headless save selftest + artifact zip on
      every push (`.github/workflows/build.yml`)
- [x] Atomic saves with a `.bak` generation; load falls back to `.bak`
- [x] Save dir off the dev username (`BennyThompson` placeholder org, with
      legacy migration — rename is free later)
- [x] Fatal errors surface in a message box, not a dead console
- [x] Pause menu with **true simulation pause** (`Application::setPaused`;
      Esc toggles it — RESUME / SAVE GAME / SAVE AND QUIT)
- [ ] Settings: fullscreen toggle, vsync, mouse sensitivity, keybinds
      (all currently hardcoded), persisted next to the save
- [ ] Audio system (SDL3 audio or a mixer lib) + first pass of sounds:
      mine/place, machine hum, rain, UI clicks
- [x] Split `game/src/VoxelGame.cpp` (~2.3k lines) into per-concern files:
      WorldGen / Sim / Player / Render / Ui + a shared internal knobs header

## Q4 2026 — content depth + world decisions

- Decide the world-size ceiling: the fixed 6×6-chunk island is a design
  statement, but plan either bigger islands, multiple islands, or vertical
  expansion — logistics distance is the game's difficulty axis
- More machine/recipe tiers (charcoal/essence fuels, generator tiers),
  multi-item belts, machine auto-eject
- The flight-stone relic (late-game earned flight — the vision piece)
- Multiple save slots + "New game / Continue" flow (needs the main menu)

## Q1 2027 — Steam + hardening

- Steamworks integration (app id, overlay, achievements, cloud saves)
- Packaging: installer or Steam depot layout; code signing decision
- Logging to a file + crash handling (minidumps) so player reports are
  actionable
- Performance pass on bigger worlds; soak tests (leave the factory running
  overnight)

## Q2 2027 — beta → launch

- Closed beta; grow `TESTING.md` into a release-checklist + regression suite
- Store page, trailer, screenshots; pricing
- Balance pass driven by beta telemetry/feedback
- Launch

## Known gaps (survey, July 2026)

Kept here so they don't get lost — none are architectural dead-ends:

- No main/pause menu, no settings, no audio, no fullscreen
- Simulation never pauses (menus leave `onTick` running)
- All input hardcoded (scancodes in `VoxelGame::onUpdate`); sensitivity fixed
- Single save slot; no in-game feedback when a save/load fails
- No logging infrastructure, no crash dumps, no telemetry
- `VoxelGame.cpp` bundles ~10 responsibilities; refactor before content push
- World hard-capped at 6×6 chunks, held fully in memory and saved wholesale
- No localization plan (bitmap font is digits + A-Z + punctuation only)
