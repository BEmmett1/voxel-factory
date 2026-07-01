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
  overlay (open with E, W/S to select, Enter to craft; rows show inputs, affordability, and
  owned counts). The player now starts with raw materials and crafts all placeables.
- **Econ 4 / M3** — machine block-entities (`Machine.h`: input/output `Inventory` buffers +
  progress) processing the reagent chain (`MachineRecipe` in `Recipes.*`) over time, gated by
  power, in `onTick()` (20 Hz). RMB on a machine opens its panel (recipes as LOAD rows +
  TAKE OUTPUTS, buffers, power status, live progress; cursor released — hover/click or
  W/S+Enter; Shift+RMB places against a machine instead). Floating progress bars + a
  look-at panel show in-world state. `isMachine()` shared by power + game.
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

The core loop is complete and closed: mine → hand-craft → build & power → machines
process → conduits transport → transmute new sources. Possible next directions:
- Miner block auto-extracts raw from an adjacent resource node (a belt source).
- **Weather + forestry (user's vision):** rain falls occasionally and can be collected as
  water; buckets are crafted from wood; trees grow from saplings (wood becomes a resource
  track alongside copper/sand/etc.).
- Multi-item/slot belts; belts needing power; machine output auto-eject.
- Save/load; per-chunk meshes (perf); player gravity/collision.
