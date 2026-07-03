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

Load the MSVC dev environment first so cl.exe is on PATH, then configure + build with
Ninja (bundled with Visual Studio):

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B out/build/x64-Debug -G Ninja `
  -DCMAKE_MAKE_PROGRAM="C:/Program Files/Microsoft Visual Studio/18/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"
cmake --build out/build/x64-Debug
```

The first configure compiles SDL3 from source (several minutes); later builds are fast.
Run `out/build/x64-Debug/bin/voxel-factory.exe`. CMake copies `shaders/` and `SDL3.dll`
next to the exe at build time.

There is no test framework.

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
camera → view/projection matrices), `Shader` / `Mesh` (RAII GL program / VAO+VBO).
`engine/GL.h` is the single include point for glad and must precede other GL headers.

### Game layer (`game/`, global namespace)

`VoxelGame` subclasses `Application`. Voxels: `Block` (id enum + property registry),
`Chunk` (16³ block array + dirty flag), `ChunkMesher` (hidden-face-removal meshing →
interleaved pos/normal/color floats). Shaders in `game/shaders/`.

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
  all operate across chunk boundaries), a procedurally generated block texture atlas
  (`Atlas.*` + `engine::Texture`, no art assets), and a screen-space crosshair. The world
  currently renders as one combined buffer rebuilt on edit; per-chunk meshes are a future
  perf step.

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
  duping; layout shared by hit-test + draw via `panelLayout()` in VoxelGame.cpp).
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
bitmap font also supports `>` and `+`. Esc closes help, then the crafting menu, then quits.

- **Miner automation** — a powered Miner harvests the nearest grown resource node within
  radius 4, one per 4 s (`kMineSeconds`/`kMineRadius`, special-cased in `onTick` before
  recipe lookup), dropping the yield into its output buffer for belts to pull; throughput
  is bounded by patch regrowth. Recipe-less machines show an info row in their panel. The
  plateau's south side hosts a demo trio (source + miner + generator + belts).

The core loop is complete, closed, and fully automatable: miners harvest → conduits
transport → machines process → transmute new sources. Possible next directions:
- **Weather + forestry (user's vision):** rain falls occasionally and can be collected as
  water; buckets are crafted from wood; trees grow from saplings (wood becomes a resource
  track alongside copper/sand/etc.).
- **Generator tiers + fuel (user's vision):** multiple kinds of generators, each needing
  fuel to run — wood from trees is the first fuel, tying into the forestry track. (The
  current Generator would become the free/basic tier or gain a fuel requirement.)
- Multi-item/slot belts; belts needing power; machine output auto-eject.
- Per-chunk meshes (perf).
- **Flight stone (user's vision):** flight is deliberately absent; a late-game alchemy
  relic will grant it as an earned power.

Player physics (pressure & pull):
- **Walking only** — AABB player vs. voxels (axis-separated move-and-slide in
  `onUpdate`), gravity + Space jump, LCtrl sprint, no flight by design. Feel knobs are
  grouped at the top of `VoxelGame.cpp` (`kWalkSpeed`, `kGravity`, `kJumpSpeed`, ...) —
  tuned by hands-on play, not scripted verification.
- **Falling off the island wipes the entire inventory** and respawns the player on the
  plateau (`kVoidY`); machines/belts keep their buffers. Hardcore by user decision.
- **Scaffold** — a cheap structural block (Stone ×1 → Scaffold ×4) for climbing and
  bridging, since verticality must be built, not flown.
- Blocks can't be placed overlapping the player's box.

Persistence:
- **Save/load** (`SaveSystem.*`): versioned binary (`save.vxf` in the SDL pref dir —
  `%APPDATA%\benny\voxel-factory\`) holding seed, all chunks, player camera/inventory/
  slot, machines (type/buffers/recipe/progress), belts (facing/cargo), and source timers.
  Auto-load on launch (fresh island if absent/invalid), auto-save on every quit path via
  the engine's `onExit()` hook, F5 quick-saves. Bump `kVersion` whenever enums or layout
  change — old saves are then discarded rather than misread.
