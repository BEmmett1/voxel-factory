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
  resource production itself is expandable. The hotbar lists all placeables (keys 1-9, 0
  jump to the first ten; mouse wheel cycles all; `Input::wheelSteps`).

UI: an **F1 help overlay** (goal + quickstart + controls) built on `UiRenderer`; the
bitmap font also supports `>` and `+`. Esc closes the topmost overlay (machine panel,
help, crafting menu); with nothing open it toggles the **pause menu** (RESUME /
SAVE GAME / SAVE AND QUIT). While paused the engine stops accruing simulation time
(`Application::setPaused` — onTick simply doesn't run, and no backlog builds up),
so machines, growth, and weather truly freeze. Quitting lives on the pause menu's
SAVE AND QUIT row (the window close button still quits + saves too).

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

Weather & the water economy:
- **Rain fronts** — a clear/rain state machine ticks in `updateWeather()` (seeded
  phase durations; saved). Visuals ease via `m_rainIntensity`: storm-grey sky,
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
- **Flight stone (user's vision):** flight is deliberately absent; a late-game alchemy
  relic will grant it as an earned power.

Player physics (pressure & pull):
- **Walking only** — AABB player vs. voxels (axis-separated move-and-slide in
  `onUpdate`), gravity + Space jump, LCtrl sprint, no flight by design. Feel knobs are
  grouped in `VoxelGameInternal.h` (`kWalkSpeed`, `kGravity`, `kJumpSpeed`, ...) —
  tuned by hands-on play, not scripted verification.
- **Falling off the island wipes the entire inventory** and respawns the player on the
  plateau (`kVoidY`); machines/belts keep their buffers. Hardcore by user decision.
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
  belts (facing/cargo), source + sapling timers, and weather state.
  Auto-load on launch (fresh island if absent/invalid), auto-save on every quit path via
  the engine's `onExit()` hook, F5 quick-saves. Bump `kVersion` whenever enums or layout
  change — old saves are then discarded rather than misread.
