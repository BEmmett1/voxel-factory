# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A 3D voxel automation game (in the spirit of Factorio / Satisfactory) built from
scratch in C++ with OpenGL: place blocks in a voxel world, power machines with
generators wired together, and move items between machines on conveyor belts/tubes.

## Build

Dependencies (SDL3, glm) are fetched automatically via CMake FetchContent. The OpenGL
loader (glad, GL 3.3 core) is vendored pre-generated in `third_party/glad/` — no Python
or codegen needed at build time.

Configure + build via `CMakePresets.json` (`x64-debug` / `x64-release`, Ninja).
cl/ninja must be on PATH, and the vcvars env does not survive between tool
invocations — chain everything through one `cmd /c`:

```powershell
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --preset x64-release && cmake --build --preset x64-release'
```

First-party targets compile at `/W4` and are warning-clean — keep them that way.
Third-party code stays quiet via `/external:anglebrackets /external:W0`, which relies
on a convention: include everything third-party (SDL3/glm/glad and the vendored
single-header libs) with angle brackets, first-party headers with quotes.

**macOS (universal: arm64 + x86_64, macOS 10.15+)**: `brew install cmake ninja`, then
`cmake --preset mac-release && cmake --build --preset mac-release` — no vcvars dance.
Apple only ships GL core 3.2/4.1, so Window.cpp requests a **4.1 core +
forward-compatible** context under `__APPLE__` (glad 3.3 loader + `#version 330 core`
shaders run unchanged on it). miniaudio's CoreAudio frameworks are linked in
engine/CMakeLists.txt; `bin/` is relocatable via an `@loader_path` rpath (the same
zero-prerequisite-folder story as Windows). Retina is deliberately off
(`SDL_WINDOW_HIGH_PIXEL_DENSITY` needs a UI point→pixel pass first — see ROADMAP).
CI builds/selftests both platforms on every push.

The first configure compiles SDL3 from source (several minutes); later builds are fast.
Run `out/build/<preset>/bin/voxel-factory.exe`. CMake copies `shaders/`, `assets/`, and
`SDL3.dll` next to the exe at build time. The MSVC runtime is statically linked
(`CMAKE_MSVC_RUNTIME_LIBRARY`), so the bin folder is a zero-prerequisite zip — don't
undo that without shipping the VC++ Redistributable some other way. The product version comes from the root
`project()` (`VOXEL_FACTORY_VERSION`, shown in the title bar and F3 overlay).

There is no test framework, but `voxel-factory.exe --selftest` runs a headless
save/load round-trip (exit 0/1); CI (`.github/workflows/build.yml`) builds the
Release preset, runs the selftest, and uploads the bin folder as an artifact on
every push. See `ROADMAP.md` for the path to the mid-2027 Steam release.

## Architecture

Two layers, mirroring the conventions of the sibling potion-game project.

### Engine layer (`engine/`, namespace `engine`)

`Application` owns the main loop and calls virtual hooks games override:
- `onStart()` — once before the loop
- `onUpdate(float dt)` — every frame (render rate)
- `onRender()` — every frame
- `onTick()` — fixed 20 Hz simulation step (decoupled from frame rate)

Subsystems: `Window` (SDL3 window + GL 3.3 context + glad load), `Input` (per-frame
keyboard/mouse with edge detection + relative-mouse look), `Camera` (perspective fly
camera → view/projection matrices), `Shader` / `Mesh` (RAII GL program / VAO+VBO),
`Audio` (see **Audio** below).
`engine/GL.h` is the single include point for glad and must precede other GL headers.

### Game layer (`game/`, global namespace)

`VoxelGame` subclasses `Application`. Voxels: `Block` (id enum + property registry),
`Chunk` (16³ block array + dirty flag), `ChunkMesher` (hidden-face-removal meshing →
interleaved pos/normal/color floats). Shaders in `game/shaders/`.

`VoxelGame` is one class split across per-concern implementation files in
`game/src/`: `VoxelGame.cpp` (lifecycle: start/save/load/Esc/title),
`VoxelGameWorldGen.cpp` (island + demo lines), `VoxelGameSim.cpp` (the 20 Hz
tick: machines/belts/power/growth/weather + registries),
`VoxelGamePlayer.cpp` (per-frame input, walking physics, mine/place),
`VoxelGameRender.cpp` (atlas, meshes, onRender), and `VoxelGameUi.cpp` (HUD +
all panels). `VoxelGameInternal.h` (namespace `vg`) holds every gameplay
tuning constant and the helpers shared across those files; single-use helpers
stay in their file's anonymous namespace.

## Conventions

- Engine code in `engine::`; game code in the global namespace.
- Block/item content lives in id-tagged registry tables (`kBlocks` in Block.cpp,
  `kItems` in Item.cpp — name, flags, drops, atlas tiles, all of it), one
  designated-initializer row per enum value, `static_assert`ed against enum order.
  Adding content = append the enum value + one row; enums are APPEND-ONLY because
  ordinals are the save encoding (see SCALABILITY.md).
- Machine behavior is data too: `kMachineTraits` in Machine.h (one row per
  machine — `MachineKind`, power demand/output, fuel + burn time, collection),
  cross-`static_assert`ed against the BlockInfo machine flags. The sim tick,
  `machineAccepts`, the power solve, and the panel UI dispatch on the kind, never
  on BlockIds. A standard recipe machine = a Processor row; the Generator/Rain
  Barrel knobs live in their rows (generator tiers = more rows).
- Block place/break side effects funnel through **`WorldEdit`**
  (WorldEdit.h/.cpp): `breakBlock`/`placeBlock`/`rotateBelt` own setBlock +
  machine/belt/source/sapling registry sync and return facts (drop, handed-back
  buffer items, powerChanged) — the caller does inventory/sounds/UI and the
  power re-solve. Never mutate a registered block's cell directly.
- The machine/belt simulation itself is **`MachineSystem`** (MachineSystem.h/.cpp):
  free functions over `(World&, machine/belt maps, PowerState&)` — the
  PowerSystem interface precedent. `tickSelfPowered` (generators/collectors)
  returns whether a burn state flipped and onTick re-solves power BEFORE
  `tickPowered` (miners/processors) so recipe machines see fresh power the same
  tick; `beltStep` advances conduits. Machine input policy (`machineAccepts`,
  `minerFilter`) lives there too. Power re-solve + chunk dirtying + hum-loop
  audio stay VoxelGame glue (`solvePowerAndMarkDirty`/`updateHums`).
- Private members prefixed `m_`; ownership via `std::unique_ptr`, no raw `delete`.
- Headers in `include/`, implementations in `src/`.
- All movement/logic scaled by `dt`; simulation logic belongs in `onTick()`.
- Right-handed, Y-up; integer block coordinates; chunk size 16.

## Roadmap

Done:
- **M0** — window + fly camera + face-culled voxel scene.
- **M2** — block place/break via center-screen raycast, block selection (1-7), target
  outline (`Raycast.*`, editing in `VoxelGame`).
- **M4** — power networks: connected-component solve over adjacent generator/wire/machine
  blocks; satisfied networks (production >= demand) render energized via a per-vertex
  emissive term (`PowerSystem.*`, `ChunkMesher`, `voxel.frag`). Recomputed on each edit.
- **M1** — multi-chunk `World` (sparse chunk map, world-coord get/set; raycast/power/mesher
  all operate across chunk boundaries), a block texture atlas (`Atlas.*` +
  `engine::Texture`; see **Textures** below for its current form), and a screen-space
  crosshair. The world renders as one mesh per chunk, rebuilt only when dirty
  (see **Performance** below).

Item economy (theme: **Alchemy / Apothecary**; loop: mine → hand-craft → automate):
- **Econ 1** — items + inventory (`Item.*`, `Inventory.h`); resource-node blocks + machines
  (`Block.*`); mining (LMB) collects a block's `blockDrop`; placing (RMB) consumes the held
  item; world-gen scatters nodes.
- **Econ 2** — `engine::UiRenderer` (orthographic 2D quads + atlas icons + embedded bitmap
  font, digits + A-Z + punctuation) and an on-screen hotbar HUD (icons + counts + selection).
- **Econ 3** — hand-craft recipe table (`Recipes.*`, the equipment chain) + crafting menu
  overlay (open with E; cursor released — click/hover/wheel or W/S+Enter; rows show
  inputs, affordability, and owned counts; an INVENTORY grid with hover tooltips shows
  materials on hand; layout via `craftLayout()`). The player starts with raw materials
  and crafts all placeables.
- **Econ 4 / M3** — machine block-entities (`Machine.h`: input/output `Inventory` buffers +
  progress) processing the reagent chain (`MachineRecipe` in `Recipes.*`) over time, gated by
  power, in `onTick()` (20 Hz). RMB on a machine opens its panel (an AUTO row + one MAKE
  row per recipe — activating a MAKE row locks `Machine::selectedRecipe` and loads the
  player's matching inputs; a locked machine also rejects belt items outside its recipe —
  plus TAKE OUTPUTS, IN/OUT buffers as item cells, an INVENTORY grid, power status, live
  progress; cursor released — hover/click or W/S+Enter; Shift+RMB places against a
  machine instead). Items drag-and-drop between the grid and the machine buffers
  (LMB = stack, RMB = one; payload removed at pickup and returned on cancel, so no
  duping; layout shared by hit-test + draw via `panelLayout()` in VoxelGameUi.cpp).
  Floating progress bars + a look-at panel show in-world state. `isMachine()` shared by
  power + game.
- **Econ 5 / M5** — conduit block-entities (`Belt.h`: facing + one carried item). `beltStep()`
  (sub-tick) pushes into the machine ahead, hops items belt→belt (snapshot + claims prevent
  chaining/merging), and pulls from the machine behind. Carried items render as floating
  icons. Facing set from the player's look on placement. The generator→grinder→conduit→
  cauldron loop now runs itself.

World & closed-loop economy:
- **Island** — the world is a floating sky island (6×6 chunks): noise-wobbled circular
  coastline, gentle hills, tapered stone underside, per-launch seed (`m_worldSeed`), and a
  flattened center plateau holding the demo line + spawn (`buildWorld`).
- **Living sources** — glowing `Source*` blocks (BlockInfo has an `emissive` field) grow
  patches of their resource's nodes nearby over time (`updateSources`, cap 5 within r=4,
  ~7 s cadence, registry `m_sources`). Mining a source drops its placeable item
  (relocatable). Node `Spring` replaced the old `WaterSource` name.
- **Philosopher's tier** — Distiller and Transmuter machines complete the reagent chain:
  Elixir → Refined Elixir → Philosopher's Catalyst → Philosopher's Stone.
- **Closed loop** — new sources are hand-craftable from a Catalyst + 8 of the raw, so
  resource production itself is expandable.
- **Curated hotbar + inventory screen** — the hotbar is ten player-assigned slots
  (`m_hotbar` is `std::array<ItemId, kHotbarSlots>`; `ItemId::None` = empty; keys 1-9/0,
  wheel cycles all ten). The **Tab overlay** (`m_invOpen`, `invLayout()`/`update`/
  `drawInventoryUi` in VoxelGameUi.cpp) shows everything owned; dragging a grid item
  onto a slot ASSIGNS it (`m_invDrag` — a reference, nothing leaves the inventory; an
  item lives on at most one slot; RMB clears a slot). Assignments survive count 0
  (drawn grey-tinted, RMB place plays "deny") and the death wipe — restocking
  re-enables them. Fresh games and pre-v12 saves seed `vg::kDefaultHotbar` (the ten
  machine placeables). Tab/E/F1 overlays are mutually exclusive.

UI: an **F1 help overlay** (goal + quickstart + controls — the controls lines are
built per draw from the current keybinds) on `UiRenderer`; the bitmap font also
supports `>`, `+`, `<`, and `%`. Esc closes the topmost overlay (machine panel,
inventory, help, crafting menu); with nothing open it toggles the **pause menu**
(RESUME / SETTINGS / SAVE GAME / SAVE AND QUIT). While paused the engine stops
accruing simulation time (`Application::setPaused` — onTick simply doesn't run, and
no backlog builds up), so machines, growth, and weather truly freeze. Quitting lives
on the pause menu's SAVE AND QUIT row (the window close button still quits + saves too).

Settings (`Settings.h`/`Settings.cpp` own the model; UI in VoxelGameUi.cpp):
- **SETTINGS panel** (pause menu row; sim stays frozen): fullscreen (SDL3 borderless
  desktop via `Window::setFullscreen` — the per-frame aspect/viewport refresh absorbs
  the mode change; fine on 100%-scale displays, Retina/scaled waits on the
  HIGH_PIXEL_DENSITY ROADMAP item), vsync (`Window::setVsync`), mouse sensitivity
  (0.02–0.40, read live at the one `addLook` site), master volume — all applied
  live via `applySettings()`. A/D or arrows adjust; Enter/click flips.
- **KEYBINDS subpanel**: the 11 `Action`s (move ×4, jump, sprint, craft, inventory,
  wrench, quick save, help) rebind via press-to-capture (row shows PRESS A KEY;
  Esc cancels the capture; reserved keys — Esc/Enter/arrows/hotbar digits/F3/F4 —
  play deny). A key lives on at most one action: binding steals it and the robbed
  row shows `---` (`SDL_SCANCODE_UNKNOWN` = unbound, safely inert); RESET DEFAULTS
  recovers. Esc backs out one level: capture → keybinds → settings → pause.
- **`settings.cfg`** (SDL pref dir, next to save.vxf): human-editable KEY=VALUE,
  integer scancodes (0 = explicitly unbound), `#` comments; unknown keys skipped,
  floats clamped, bad/reserved values keep defaults, duplicate binds sanitized
  first-wins. Written atomically (tmp → .bak → rename) on settings-close and quit;
  loaded + applied at the top of onStart. Covered by `--selftest` (round-trip,
  rotation, tolerance, explicit-unbound preservation).
- The old `vg::kLookSensitivity` / `vg::kMasterVolume` constants are gone — the
  `Settings{}` member initializers are the single source of truth for defaults.

- **Miner automation** — a powered Miner harvests the nearest grown resource node within
  radius 4, one per 4 s (`kMineSeconds`/`kMineRadius`, special-cased in `onTick` before
  recipe lookup), dropping the yield into its output buffer for belts to pull; throughput
  is bounded by patch regrowth. Recipe-less machines show an info row in their panel. The
  plateau's south side hosts a demo trio (source + miner + generator + belts).

Forestry (saplings → trees → wood):
- **Trees** — a 3-log trunk + 14-leaf canopy, defined once in `treeCells()`/`placeTree()`
  (VoxelGameInternal.h) and shared by world-gen, growth, and the grow-space check. Exactly one
  grown tree spawns near the plateau each game — the starting sapling supply.
- **Renewable loop** — chopping a Log yields Wood; chopping Leaves has a
  `kSaplingDropChance` sapling drop with a pity guarantee (`m_leafPity`, every
  `kSaplingPityLeaves`th dry leaf), so felling a whole tree can't strand the player.
  Saplings place on Grass/Dirt only and grow after `kTreeGrowSeconds` via `m_saplings`
  timers (`updateSaplings`; a blocked or player-overlapped spot retries each tick).
  Leaves with no Log within `kLeafReach` decay staggered (`updateLeafDecay`,
  `kLeafDecaySeconds`/`kLeafDecayChance`); every lost leaf — chopped OR decayed —
  rolls the same sapling drop into the player's pack (`rollLeafSapling`, shared
  pity counter), so trunk-first felling doesn't starve the forest. All knobs sit
  with the other cadence constants in VoxelGameInternal.h.
- **Wood's first recipe** — Wood ×3 → Bucket (inert until the rain system arrives).

Performance (measured with the **F3 overlay**: frame avg/worst ms, remesh/solve
costs and per-second counts):
- **Per-chunk dirty-driven meshing** — each chunk owns an `engine::Mesh`
  (`m_chunkMeshes`); `remeshDirtyChunks()` at the top of `onRender` rebuilds only
  chunks whose `Chunk::dirty` flag is set, so all tick/edit mutations of a frame
  coalesce. `World::setBlock` skips no-op writes and marks face-neighbor chunks
  when an edge block changes; `World::markDirtyAt` covers non-block mesh state
  (wrench re-aims, power glow). Never call a full-world rebuild — there isn't one.
- **Power solves only on edits** — `solvePowerAndMarkDirty()` runs when a
  placed/broken block `isPowerNode`; the energized-set diff dirties exactly the
  chunks whose glow flipped. Simulation (node spawns, trees, harvests) never
  touches power.
- **Mesher fast path** — `appendChunk` reads its own chunk's array and six
  prefetched neighbor chunks; no hash lookups per cell. Miners cache their
  target node (`Machine::hasTarget`, transient) instead of scanning every tick.
- Baseline → result on a large save: worst frame 100 ms → ~4.5 ms; remesh
  100 ms/47 chunks → ~1 ms/1 chunk. Frustum culling was evaluated and dropped:
  frames are pacing-bound, not render-bound, at this world size.

Textures:
- **Paintable atlas** — `game/assets/atlas.png` (256×128, a 16×8 grid of 16px tiles;
  map in `game/assets/ATLAS.md`) is loaded at startup (`engine::loadImage`, vendored
  stb_image in `third_party/stb/`); if missing or mis-sized the game falls back to
  generated flat-color tiles, so the PNG is never required. Blocks map to tiles via
  `Atlas::tilesForBlock()` (`{top, side, bottom}` — grass tops, log rings, machine
  lids); the mesher picks per face. Material items own icon tiles (`ItemInfo::
  atlasTile`); placeables borrow their block's side tile (`iconTile()`). Repaint the
  PNG in any pixel editor and rebuild (an always-run CMake target copies assets), or
  regenerate the whole starter set with `python tools/make_atlas.py` (pure stdlib —
  overwrites hand edits!).

Audio (first pass — mine/place, machine hum, rain, UI clicks):
- **`engine::Audio`** wraps vendored miniaudio (`third_party/miniaudio/miniaudio.h`,
  compiled only in `engine/src/Audio.cpp`; pImpl keeps it out of headers). Owned by
  `Application` (declared after `m_window` so it dies before `SDL_Quit`); the engine
  loop updates the 3D listener from the camera each frame. If no output device opens
  (headless/CI), it logs once and every call no-ops — audio can never crash the game.
  API: `play` (flat, UI), `playAt` (positional one-shot, linear falloff), and
  `createLoop`/`setLoopGain`/`setLoopPosition`/`setLoopPaused`/`destroyLoop` handles.
- **WAVs are generated assets** — `python tools/make_sounds.py` (pure stdlib, like
  make_atlas.py; overwrites hand edits!) writes `game/assets/sounds/*.wav`, committed
  and auto-copied by `copy-assets`. Any file can be replaced by a hand-made WAV of the
  same stem; a missing file logs and stays silent. Loops are seam-free by construction
  (hum: integer-cycle sines; rain: tail-to-head crossfade).
- **Wiring**: mine/place one-shots at the block (pitch-jittered per cell); the rain
  loop's gain follows `m_rainIntensity`; `updateHums()` (called from every
  `solvePowerAndMarkDirty` + the onStart seed solve) diffs one positional hum loop per
  energized machine (burning generators only, capped at `kMaxHums` nearest);
  open/close/click/craft/deny cover all panels at the same funnels that mutate state.
  Pause mutes hums (sim frozen) but keeps rain. Mix knobs sit in the `// ---- Audio ----`
  block of VoxelGameInternal.h.

Entities (Blockbench import — the combat pillar's first brick):
- **`engine::BbModel`** loads Blockbench's native `.bbmodel` (JSON via vendored
  nlohmann/json in `third_party/json/`, compiled ONLY in `engine/src/BbModel.cpp`):
  cuboid elements baked into one interleaved `{pos, normal, uv, boneIndex}` mesh,
  bone hierarchy from the outliner (DFS order, parent < child), embedded base64 PNG
  textures (`engine::loadImage` from-memory overload), and keyframe animations
  (rotation/position, linear/step; `evaluateBbPose` → per-bone skin matrices, no
  inverse binds needed). Lenient everywhere: numeric strings/molang degrade with a
  log; box-UV models unsupported (author with per-face UVs). All unit conversion
  funnels through `geoToWorld`/`animPosToWorld`/`animRotToWorld` in BbModel.cpp so
  a handedness fix is a one-line sign flip.
- **Test creature** — `tools/make_sounds.py`-style generator `tools/make_test_model.py`
  emits the committed `game/assets/models/creature.bbmodel` (asymmetric on purpose:
  +X horn, front-face eyes, 25°-rotated tail; idle/walk anims). One creature spawns
  fresh each launch near the plateau (NOT saved), wanders in `updateCreatures()`
  (onTick; gravity + the shared `vg::boxCollides` move-and-slide), and draws skinned
  in `renderCreatures()` (`game/shaders/entity.*`, `uBones[32]` = `kMaxEntityBones`,
  one draw per creature, prev/cur tick interpolation hides the 20 Hz step; same
  `kLightDir` + `uRainDim` as the world). All of it lives in the extracted
  **`CreatureSystem`** (`game/include/game/CreatureSystem.h` + `game/src/
  CreatureSystem.cpp`): owns the model/GPU assets and instances, API of
  `loadAssets` / `spawnTestCreature` / `update(World&)` / `frameAdvance` /
  `render(Camera&, rainDim)` / `tryMeleeAttack` — takes engine services as
  parameters, never VoxelGame&. Knobs in the `// ---- Entities ----` block. Missing/corrupt model = creatureless
  launch + log; failed texture = magenta checker (never fatal).

Weather & the water economy:
- **Rain fronts** — a clear/rain state machine, extracted as the **`Weather`**
  value type (Weather.h/.cpp): `tick(worldSeed)` flips seeded phases at 20 Hz
  (raining + timer are saved), `frameEase(dt)` smooths `intensity` per frame,
  `forceToggle()` is the F4 dev key. Visuals ease via `m_weather.intensity`:
  storm-grey sky,
  `uRainDim` dims lit color in `voxel.frag` (emissive stays bright — energized
  networks read as beacons), and world-space streak lines fall around the camera,
  skipping covered columns (`skyVisible`, `buildRainMesh`). **F4 is a hidden dev
  key** that forces rain on/off for testing. Rain also multiplies source-patch and
  sapling growth (`kRainGrowthMult`).
- **Rain is the ONLY water** — Spring nodes, Water Sources, and their transmute are
  removed; the raw item survives as *Rain Water*. It is collected by the **Rain
  Barrel** (Wood ×6 + Bucket; a machine entity that fills its output during rain
  under open sky — belt-drainable, capped at `kBarrelCap`, runs unpowered and is
  NOT a power node) and by **holding the Bucket** in the rain (hotbar tool; fill
  bar over the hotbar; the place path guards `itemInfo(held).placeable` so tools
  never place blocks).
- **Fuel** — Generators are machines: they hold Wood in their input (belt-feedable),
  burn one per `kWoodBurnSeconds` (`Machine::progress` = burn seconds left → the
  bars double as fuel gauges), and only light a new wood while their network has
  demand (`PowerSystem::solve` now takes the machine map and emits "hungry"
  generators; production counts only burning ones). Burn-state flips re-solve
  power once per tick. No fuel = dark network.

Economy v2 (difficulty by design; hand table in `Recipes.cpp`):
- **Machine-made plates** — Copper Plate is a Grinder recipe (Ingot → Plate); only
  the Generator and Grinder hand-craft without plates (from ingots + stone/wood),
  so the tech tree bootstraps: chop the starting tree → ingots → Generator +
  Grinder → fueled Grinder presses plates → frames → everything else.
- Deeper recipes (Machine Frame = Plate ×3 + Crystal + Wood ×2; each alchemy
  machine = Frame + extras), a lean starting kit (exactly the bootstrap pair plus
  slack), logs drop Wood ×2, and sources scatter beyond `kSourceMinRadius` so the
  outer band is where the resources are — logistics distance is the point.

The core loop is complete, closed, and fully automatable. Possible next directions:
- **Generator tiers / better fuels:** charcoal or essence-based fuels with longer
  burns; higher-output generator tiers.
- Multi-item/slot belts; belts needing power; machine output auto-eject.
- **Flight stone (user's vision):** flight is deliberately absent; it is earned as the
  final boss's drop at the end of the combat pillar (weapons/armor/boss dungeons —
  entity layer, machine-crafted gear, crafted teleport keys; see ROADMAP.md Q4 2026 /
  Q1 2027).

Dimensions & the first boss (the combat pillar's opening move):
- **True dimension system** — `DimensionId { Overworld, BossArena }`
  (Dimension.h); VoxelGame owns one `World` per dimension with `m_world`
  pointing at the ACTIVE one (player/render/raycast compile unchanged) and
  `overworld()` for the factory. **The simulation is Overworld-only by
  design**: machines/belts/power/growth/weather and all registries are
  Overworld-semantic; the arena has no automation and NO block edits (mine/
  place/wrench/machine-panel deny there — coordinates overlap numerically
  across dimensions, so an arena lookup could alias a home machine).
  `switchDimension()` swaps the world, drops + re-dirties the mesh cache (one
  travel hitch), and pauses hums; rain/sky/glow gate per frame. Arena sky is
  a flat void purple.
- **Travel** — the **Teleport Key** (hand-craft: Philosopher's Catalyst +
  Crystal ×4 + Essence ×2) is a hotbar tool: RMB consumes it, remembers
  `m_homePose`, regenerates the arena (`buildArena`: a voidstone disc + four
  pillars at its own origin) and spawns the boss. Every road home — victory
  linger (`kVictorySeconds`), death (the hardcore rule; dimension follows the
  respawn), or save-and-load — lands at home. Saves always bind the
  Overworld + home pose: quitting mid-fight abandons it (fights transient,
  creatures still unsaved).
- **CreatureSystem species registry** — `kSpecies` rows (`SpeciesId`:
  TestCreature, VoidWarden) in the kBlocks/kItems discipline: model path,
  kind (`CreatureKind::Wanderer|Boss`), body, hp, and the boss numbers
  (aggro/strike/damage/cooldown/drop). Creatures carry a `DimensionId` tag;
  update/render/melee filter on the active dimension (the home wanderer
  freezes while away). Boss AI: aggro → chase → contact strikes returned as
  `Events::damageToPlayer` + `Events::playerKnock` (the first enemy damage;
  applied via `PlayerController::damage` + `shove` — each hit throws the
  player back and airborne, so the fight has a hit-and-close rhythm and the
  arena rim is a real threat; `kBossKnockback`/`kBossKnockUp`). `tryMeleeAttack` returns a `MeleeResult` — a
  boss kill hands back its drop (**Void Catalyst**), sets `m_bossDefeated`
  (saved, v13 append), shows VICTORY, and rides home. Boss HP bar top-center
  in drawHud. Model: `tools/make_boss_model.py` → `boss.bbmodel` (same
  lenient loading as the creature). Knobs in `// ---- Boss & arena ----`.
- **F6 is a hidden dev key** (F4's sibling): grants a Teleport Key + Copper
  Sword and assigns them to the hotbar — the boss loop without the
  philosopher grind.

Player physics (pressure & pull):
- **Walking only** — AABB player vs. voxels, gravity + Space jump, LCtrl sprint,
  no flight by design. The body lives in **`PlayerController`**
  (PlayerController.h/.cpp): `move(dt, Input&, Camera&, World&, Settings&,
  Audio&)` does look + axis-separated move-and-slide + fall damage and returns
  `{died, fellOff}` for the caller's pack wipe + title; `health` is a public
  field bound into SaveData. Tools/hotbar/edit reactions stay in
  VoxelGamePlayer.cpp. Feel knobs are grouped in `VoxelGameInternal.h`
  (`kWalkSpeed`, `kGravity`, `kJumpSpeed`, ...) — tuned by hands-on play, not
  scripted verification.
- **Melee (Copper Sword)** — a hotbar tool (Plate ×2 + Wood ×1, atlas tile 79);
  LMB swings along the aim ray (`tryMeleeAttack` in VoxelGameEntities.cpp: shared
  ray-vs-AABB slab test, blocked by nearer solid blocks, `kSwordCooldown` gate,
  "swing"/"hit" sounds). A struck creature takes `kSwordDamage`, flashes red
  (`uFlash` in entity.frag, decays per frame), gets knocked back (decaying
  `Creature::knock` + vertical pop) and flees; at 0 hp it's removed (no drops yet —
  boss loot answers "why fight" later). A missed swing falls through to mining.
  Knobs in the `// ---- Melee ----` block. Save note: v11 grew the ItemId enum —
  `readInventory` accepts older, SHORTER item arrays (append-only enum growth stays
  save-compatible; reordering never is).
- **Health & damage** — `m_health` in hearts (`kMaxHealth`, knobs in the
  `// ---- Health & damage ----` block); heart segments render above the hotbar's
  left end in `drawHud`. Hard landings hurt past `kFallSafeSpeed`
  (`damagePlayer`, "hurt" sound); the **Healing Draught rides the hotbar as a
  tool** (like the Bucket) — RMB drinks when hurt ("heal" sound; the drink
  consumes the click so nothing places/opens). Health is saved (v10).
- **Death = one hardcore rule everywhere**: reaching 0 HP or **falling off the
  island** (`kVoidY`) wipes the entire inventory and respawns the player on the
  plateau at full health; machines/belts keep their buffers. By user decision.
- **Scaffold** — a cheap structural block (Stone ×1 → Scaffold ×4) for climbing and
  bridging, since verticality must be built, not flown.
- Blocks can't be placed overlapping the player's box.

Persistence:
- **Save/load** (`SaveSystem.*`): versioned binary (`save.vxf` in the SDL pref dir —
  `%APPDATA%\BennyThompson\voxel-factory\`; `kOrgName` is a placeholder studio name,
  and a legacy save under `benny\` is migrated on first launch). Writes are atomic:
  save to `.tmp`, rotate the old file to `.bak`, rename in; load falls back to `.bak`
  before regenerating. The file holds seed, all chunks, player camera/inventory/
  slot, machines (type/buffers/recipe/progress — generators/barrels ride along),
  belts (facing/cargo), source + sapling timers, weather state, player
  health (appended in v10; v9 saves still load with full-health default), and the
  hotbar slot assignments (appended in v12; older saves keep `vg::kDefaultHotbar`).
  Auto-load on launch (fresh island if absent/invalid), auto-save on every quit path via
  the engine's `onExit()` hook, F5 quick-saves. Bump `kVersion` whenever enums or layout
  change — old saves are then discarded rather than misread. Exception: a bump that only
  APPENDS trailing fields may keep older versions loadable (`kOldestLoadable`; the
  caller's defaults survive), as v9→v10 did for health and v11→v12 for the hotbar —
  any enum/layout change must drop that compatibility.
