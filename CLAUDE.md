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

First-party targets compile at `/W4` (`-Wall -Wextra` on Clang/GCC) and are
warning-clean on all three platforms — keep them that way. The one suppression is
`-Wno-missing-field-initializers`: GCC's `-Wextra` flags every registry row that omits
a trailing field, which is precisely the kBlocks/kItems/kMachineTraits/kBlockShapes
convention (absent field = the default), so the warning is noise against a deliberate
design rather than a finding.
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

**Linux (x86_64, glibc)**: `bash tools/install-linux-deps.sh`, then
`cmake --preset linux-release && cmake --build --preset linux-release`. That script
(apt/dnf/pacman) is the single source of truth for the dependency list and **CI runs
it**, so the documented command is the tested one — don't hand-copy a package list
anywhere else. It has to be complete rather than approximately right, because SDL3 is
compiled from source and its X11 detection is all-or-nothing per sub-feature: one look
at `X11/Xlib.h` turns `SDL_X11` on, and from then on a missing sub-feature is a hard
configure error (`Couldn't find dependency package for XSCRNSAVER`), not a dropped
feature — so a near-miss list fails late, one package per attempt. The one case SDL
*doesn't* catch is having no video backend at all: that configures and builds green,
then dies at `SDL_CreateWindow` with only the dummy driver, so the root CMakeLists
checks for `X11/Xlib.h` / `wayland-client.h` up front and points at the script. Audio
needs nothing installed:
miniaudio `dlopen`s ALSA/PulseAudio at runtime, so the engine links only
`Threads`/`dl`/`m` there. `bin/` is relocatable via an `$ORIGIN` rpath (the same
zero-prerequisite-folder story as Windows and macOS), and the exe links `-rdynamic`
(`ENABLE_EXPORTS`) so CrashHandler's `backtrace_symbols_fd` prints names, not hex.

CI builds/selftests all three platforms on every push.

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
`BlockShape` (sub-cube geometry per block — what to draw and what to collide with),
`Chunk` (16³ block array + dirty flag), `ChunkMesher` (hidden-face-removal meshing →
interleaved pos/normal/color floats). Shaders in `game/shaders/`.

`VoxelGame` is one class split across per-concern implementation files in
`game/src/`: `VoxelGame.cpp` (lifecycle: start/save/load/Esc/title),
`VoxelGameWorldGen.cpp` (island + the ruin), `VoxelGameSim.cpp` (the 20 Hz
tick: machines/belts/power/growth/weather + registries),
`VoxelGamePlayer.cpp` (per-frame input, walking physics, mine/place),
`VoxelGameRender.cpp` (atlas, meshes, onRender), `VoxelGameUi.cpp` (HUD +
all panels), and `VoxelGameEffects.cpp` (the Alchemy Circle's ritual effect —
pure presentation). `VoxelGameInternal.h` (namespace `vg`) holds every gameplay
tuning constant and the helpers shared across those files; single-use helpers
stay in their file's anonymous namespace.

## Conventions

- Engine code in `engine::`; game code in the global namespace.
- **The registries are RUNTIME tables** (Aug 2026). `kBlocks`/`kItems`/
  `kMachineTraitSeed`/`kFuelSeed` are SEEDS copied into vectors at first use;
  a content pack appends. So **`BlockId::Count` means "how many were COMPILED
  IN"**, not how many exist — iterate `blockCount()`/`itemCount()` and use
  `blockRows()`/`itemRows()`/`machineTraitRows()`/`fuelRows()`, never the seed
  arrays. An ordinal past `Count` is a valid id (fixed underlying type), and
  rides every path a compiled one does including a chunk's bytes and a save's
  key table. "No such content" is `content::kNoBlock`/`kNoItem` at the top of
  the underlying type — NOT `::Count`, which a pack turns into a real row.
  Every `static_assert` still guards the seeds (a bad `kBlocks` edit is still
  a compile error); `content::validate()` asks the same questions of the whole
  runtime table. Shapes stay compiled — a shape needs a Blockbench bake.
  Registry writes are STARTUP ONLY: growing a vector invalidates every
  `BlockInfo&` and would move nothing but break everything.
- Block/item content lives in id-tagged registry tables (`kBlocks` in Block.cpp,
  `kItems` in Item.cpp — name, flags, drops, atlas tiles, all of it), one
  designated-initializer row per enum value, `static_assert`ed against enum order.
  Adding content = append the enum value + one row. Every row also carries a
  stable `key` ("core:copper_ingot") which is the content's real identity —
  ordinals are just an encoding, translated on load (see **Content identity**
  below). Enums are NO LONGER append-only; keys are what must never change.
- Machine behavior is data too: `kMachineTraits` in Machine.h (one row per
  machine — `MachineKind`, power demand/output, fuel, collection, plus
  `recipeGroup`/`speedMult` for the manual tier), cross-`static_assert`ed
  against the BlockInfo machine flags. The sim tick, `machineAccepts`, the
  power solve, and the panel UI dispatch on the kind, never on BlockIds. A
  standard recipe machine = a Processor row; the Generator/Rain Barrel knobs
  live in their rows (generator tiers = more rows). What burns and for how
  long is its own registry, `kFuels` — a better fuel is one row, not a code
  change.
- **Recipes are keyed, and the tables are freely editable.** Every row in
  `kRecipes`/`kMachineRecipes`/`kCircleRecipes` carries a stable
  `const char* key` ("press/plate"), and a machine's locked MAKE row is SAVED
  as that key rather than as its position. Rows may therefore be reordered,
  retimed, rebalanced, or deleted at will; a save whose locked recipe no
  longer exists falls back to AUTO instead of silently making something else.
  This was the first place the append-only rule stopped applying; save v22
  extended the same idea to blocks and items, so it now applies nowhere. Two
  things replace it as the safety net, both in `--selftest`: a tech-tree
  **reachability closure** (edit a recipe into a deadlock and it fails) and a
  **circle-pattern shadowing check** (lay every pattern, prove the matcher
  returns it) — both now live in `content::validate()`, below.
  `RECIPES.md` is generated — `voxel-factory --dump-recipes`. Since Aug 2026
  the tables are also loadable from a **content pack**, not only editable in
  source.
- **Content identity is the key, not the ordinal** (`ContentRegistry.h`/.cpp,
  save v22). A `BlockId`/`ItemId` ordinal is an ENCODING — the raw byte in a
  chunk, the slot position in an inventory — and only means anything relative
  to the content set that produced it. Two content sets have to agree in two
  places: a SAVE written by a different build, and (eventually) a SERVER a
  client joined. Both are the same "your ordinal N is my ordinal M" problem, so
  both are served by one object rather than two mechanisms invented apart.
  A save writes two **key tables** (blocks, then items — stable keys in the
  writer's ordinal order) right after the version, and `ContentMap` translates
  the whole body through them. That is what retired the append-only rule and
  what let both enums widen to `uint16_t`. Pre-v22 saves get
  `ContentMap::identity()` (their ordinals are this content set's own
  ancestor's) and read the old 1-byte ids — `readId()` is the one place that
  difference lives. A save naming content this build lacks is refused up
  front, by design: loading it with holes silently eats a factory.
  `--selftest` fabricates a foreign save by swapping two keys in the file and
  asserts the world comes back MIRRORED — a load that ignores the tables
  returns it unchanged, so the check fails loudly if the layer goes decorative.
- **Content packs: recipes are authorable from data** (`ContentPack.h`,
  `ContentDump.cpp`, `ContentPack.cpp`, Aug 2026). `--dump-content` writes
  the whole content set as JSON, by KEY, and `ContentPack.cpp` reads exactly
  that back — the writer IS the format's spec, and `--selftest` holds a
  dump → load → dump round-trip so the two cannot drift. A pack **patches** a
  row whose key matches (in PLACE, which is what makes the round-trip work and
  what keeps a rebalanced circle pattern from falling behind the pattern that
  shadows it), appends anything new, and `"remove": [keys]` deletes. Two doors,
  deliberately different: `packs/*.json` beside the exe is the PLAYER's and
  applies to the game only; `--pack <file>` is the AUTHOR's and applies to any
  mode, so a generator can `--pack draft.json --validate` without installing
  anything. Keeping the folder out of the headless tools is what stops an
  installed pack from rewriting what CI asserts.
  **All-or-nothing:** nothing applies unless everything parses, and because
  "would this tech tree close?" cannot be asked of a table the pack is not in,
  `applyPacks` applies first and judges after — a refusal restores exactly what
  was there. At launch a refused pack is logged and shown in a message box but
  is never fatal (the compiled content is already back).
  A pack authors all three RECIPE tables plus **blocks, items, machine traits
  and fuels** (Aug 2026) — a new machine with its own recipes, a new ore, a
  better fuel, all from JSON. Rows may name content the same pack is adding,
  in either file order: `loadPack` declares every new key before resolving any
  field.
  **A row is a PATCH of the row it names, in every table** (Aug 2026): an
  absent field inherits what that row already had rather than resetting to the
  type's default, so `{"key": "core:stone", "hardness": 6.0}` is Stone but
  harder — not a black, untextured Stone that drops nothing and needs no
  pickaxe. The dump-round-trip could never have caught the difference, because
  a full dump states every non-default field and both readings agree on it;
  what the difference governs is the small hand-written or generated pack,
  which is the case the format exists for. Stating a field explicitly still
  resets it, so nothing is lost. The same rule closed a hole in the recipe
  tables, where an omitted `inputs` used to author a FREE craft in silence —
  `inputs`/`outputs`/`machine` are now inherited when absent and complained
  about when a new row has none.
  Still needs a compiler: block SHAPES (a Blockbench bake), new
  `MachineKind`s/`CreatureKind`s (hand-written dispatch), item effects,
  worldgen presence.
- **`content::validate()`** (`ContentValidate.h`) is the coherence check —
  recipe key uniqueness + round-trip, circle-pattern shadowing, and three
  economy closures — returning DIAGNOSTICS, not an exit code.
  Three callers: `--selftest` (prints and fails), `--validate` (the same
  without the save round-trip), and the pack loader. It answers in English
  because its caller is often not a person.
- **Three closures, one walk** (Aug 2026). `closeOverRecipes` is the shared
  fixpoint over all three recipe surfaces; the checks differ ONLY in what they
  seed it with, which is the whole distinction between them.
  **Reachability** asks whether the tech tree OPENS: seeded from what the island
  is made of, every machine must be buildable and every recipe input obtainable.
  **Renewability** asks whether it STAYS open: seeded from nothing but the
  faucets the SIMULATION refills (source patches, self-seeding forestry, grass
  spread, weather, repeatable boss drops), every item must still be producible —
  an item only reachability can find is one the island generated a finite pile
  of. **Orphans** is reachability's mirror: everything you can get must be FOR
  something, with four allowances *derived* rather than flagged (it places a
  block, it is a tool/weapon/armor, it burns, or a VERB spends it), so a new item
  earns its exemption by being what it claims to be.
- **The two balance closures are scoped to `core:` keys**, and that is not
  decoration: the PACK LOADER calls `validate()`, so without the scoping any pack
  adding an ornament or a trophy would be refused, and `--selftest`'s own "a pack
  may add content" case would fail. They are BALANCE claims about the shipped
  game, not coherence claims about a content set — a mod may ship a trophy with
  no sink; this repo may not. Both are RATCHETS: they come back clean today and
  should only ever speak up when an edit takes a faucet or a sink away.
- **Reachability's seed rule got stricter at the same time.** A block whose drop
  counts as "the world gave it to you" must be one the world PUT there, so
  besides machines and sources it now excludes anything an ITEM places (a Conduit
  dropping a Conduit is not the world handing you one) and anything PLANTED
  (a crop stage dropping its seed is not a seed faucet). Without those the
  closure believed Wire, Conduit, Scaffold and Herb Seed were free, which
  short-circuited the whole Copper Plate line and all of farming — a deadlock
  check creating the blind spot it exists to catch.
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
- Geometry queries against the world are **`Collision`** (Collision.h/.cpp),
  the same free-function shape: `rayAabb` / `boxOverlapsWorld` /
  `surfaceTopAt` / `landingSurface`. Nothing may assume a block is a unit
  cube — walk `blockBoxes(id)` instead (see **3D detailed blocks** below).
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
  chaining/merging), and pulls from the machine behind. Carried items render as flowing
  world-space icons (see **Belts became tubes**). Facing set from the player's look on
  placement. The generator→grinder→conduit→
  cauldron loop now runs itself.

World & closed-loop economy:
- **Island** — the world is a floating sky island (6×6 chunks): noise-wobbled circular
  coastline, gentle hills, tapered stone underside, per-launch seed (`m_worldSeed`), and a
  flattened center plateau holding spawn (`buildWorld`). Since Aug 2026 the plateau
  holds **nothing else but one tree** — the demo line became the ruin (below).
- **Living sources** — glowing `Source*` blocks (BlockInfo has an `emissive` field) grow
  patches of their resource's nodes nearby over time (`updateSources`, cap 5 within r=4,
  ~7 s cadence, registry `m_sources`). Mining a source drops its placeable item
  (relocatable). Node `Spring` replaced the old `WaterSource` name.
- **Philosopher's tier** — Distiller and Transmuter machines complete the reagent chain:
  Elixir → Refined Elixir → Philosopher's Catalyst → Philosopher's Stone.
- **Closed loop** — new sources are hand-craftable from a Catalyst + 8 of the raw, so
  resource production itself is expandable.
- **Source fusion** — the **Fusion Catalyst** (hand-craft, a hotbar tool like the boss
  keys) is RMB'd at a source block; `WorldEdit::fuseSources` consumes it plus one
  orthogonally-adjacent source of a DIFFERENT type, transmuting the aimed cell into a
  **Resonant Source** (the partner cell clears). The hybrid rides every data-driven
  source path unchanged (grows **Resonant Node**s via `updateSources`, drops a
  relocatable `ResonantSourceItem`, glows, Miner-harvestable) — no new sim code. Its
  raw, **Resonance**, is a premium catalyst: `Resonance → Philosopher's Catalyst` (a
  terminal sink feeding the source/key crafts) and `Resonance ×2 → Fusion Catalyst`
  (self-sustaining). The bootstrap catalyst costs a **Philosopher's Stone** — giving the
  chain's former dead-end trophy a use, so fusion is the true endgame reward.
  Overworld-only (arena edits already deny); append-only blocks/items mean no save
  version bump. F6 dev kit grants a Fusion Catalyst.
- **Curated hotbar + inventory screen** — the hotbar is ten player-assigned slots
  (`m_hotbar` is `std::array<ItemId, kHotbarSlots>`; `ItemId::None` = empty; keys 1-9/0,
  wheel cycles all ten). The **Tab overlay** (`m_invOpen`, `invLayout()`/`update`/
  `drawInventoryUi` in VoxelGameUi.cpp) shows everything owned; dragging a grid item
  onto a slot ASSIGNS it (`m_invDrag` — a reference, nothing leaves the inventory; an
  item lives on at most one slot; RMB clears a slot). Assignments survive count 0
  (drawn grey-tinted, RMB place plays "deny") and the death wipe — restocking
  re-enables them. Fresh games and pre-v12 saves seed `vg::kDefaultHotbar` (the ten
  machine placeables). Tab/E/F1 overlays are mutually exclusive.

**Refusals say why** (Aug 2026, the first onboarding item): `VoxelGame::deny()`
is the single funnel — it plays the "deny" sound AND sets `m_denyText` /
`m_denyTimer`, drawn as one amber line over the hotbar in `drawHud`, fading over
its last third (`kDenySeconds`). **Never call `audio().play("deny", ...)`
directly for a player-facing rejection**; the site that knows the reason is the
site that must state it, and binding sound to reason in one call is what keeps
the two from drifting. Messages are UPPERCASE and limited to the bitmap font's
glyphs — A-Z 0-9 and `()-:/.,>+<%`, so **no apostrophes**. The timer decays on
real frame time, not the pause-aware clock, because several call sites are inside
panels where the sim is frozen. Reasons that name a keybind read it live
(`SDL_GetScancodeName(key(Action::...))`) rather than hardcoding the default.
Two of the sites re-derive a distinction the callee folded away
(`tillSoil`/`placeBlock` both return one bool for several refusals) — that is
deliberate: the player's mistake is different in each case. Sound-only "deny"
survives where it is not a refusal at all: the master switch turning OFF, and a
slipped crank grip.

**The pointer is drawn by the game** (Sep 2026, user report: OBS recorded
panels with no cursor). Whenever the mouse is released (every panel and menu)
`Window::setRelativeMouse` keeps the OS cursor hidden over the window and
`drawCursor()` draws a 12x19 arrow LAST in both render paths, before the F2
screenshot reads the frame, so it is in every recording, stream and
screenshot. OBS Game Capture grabs the GL frames and never saw the OS cursor
over this window, even with Capture Cursor on. `Input::syncMousePosition()`
asks SDL for the pointer each frame after the events, since motion events
alone left the position stale after a warp or a relative-mode release, which
a drawn arrow shows immediately. A hardware cursor would sit a frame ahead of
it; that is the trade.

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
- **Screenshot** (`Action::Screenshot`, default **F2**) works over every screen:
  onUpdate flags it, the END of onRender reads the back buffer
  (`Window::saveScreenshot`, `SDL_SavePNG` — no extra dependency) so the PNG is
  the whole frame, HUD included, into `<pref dir>/screenshots/`. Appended LAST
  in `Action` so an old cfg that already used F2 keeps it (first-wins).
- **KEYBINDS subpanel**: the 13 `Action`s (move ×4, jump, sprint, craft, inventory,
  wrench, belt filter, quick save, help, screenshot) rebind via press-to-capture (row shows PRESS A KEY;
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
  ruin's mining bay is one of these, standing (source + miner + generator + belts) — it
  used to sit on the plateau's south side, until the whole ruin moved out (see **The
  hard start**).

Forestry (saplings → trees → wood):
- **Trees** — a trunk of logs, a leaf ring around the top one, a full square layer and
  a plus-shaped cap, built once by `treeCells(size)`/`placeTree()` (VoxelGameInternal.h)
  and shared by world-gen, growth, and the grow-space check. Exactly one grown tree
  spawns near the plateau each game — the starting sapling supply, and now the only
  thing on the plateau besides you.
- **Two sizes, one builder** (Aug 2026). Size 1 is the wild tree (3 logs, 22 leaves);
  size 2 is what a **Grafted Sapling** grows (5 logs, 74 leaves) in the same
  `kTreeGrowSeconds`. Which tree a sapling becomes is `BlockInfo::treeSize`, a REGISTRY
  field — the same move `SoilKind` was, replacing a hardcoded `id == BlockId::Sapling`
  in `WorldEdit` that a second sapling would have grown a branch of and a content pack
  could never have reached. Because the BLOCK carries the kind, the sapling registry
  stays a plain `pos -> float` and **the save format did not move at all**.
  `treeCells` CLAMPS an unknown size rather than indexing past its shapes (a pack may
  write any integer), so a bad value grows the ordinary tree; `content::validate()`
  says so rather than letting it pass silently, and also refuses a sapling with no
  `needsSoil`, which could never be planted and so would never grow.
- **The graft is where the sapling surplus goes.** A grown tree returns ~8 saplings for
  the ONE that made it and only one replaces it, so the rest either compost or become a
  bigger tree on the same plot: `circle/grafted-sapling` is Sapling ×2 + Compost. It is
  on the Circle rather than in the hand menu deliberately — it costs a machine product,
  and the hand tier has to stay buildable from nothing.
- **A blocked sapling backs off.** `updateSaplings` used to re-run the clear-space scan
  every tick forever for a fenced-in sapling — a permanent 25-cell scan at 20 Hz, and 80
  for a grafted one. It now rewinds the timer by `kTreeRetrySeconds` instead. The timer
  IS the backoff, so there is no second field and no save change.
- **Renewable loop** — chopping a Log yields Wood; chopping Leaves has a
  `kSaplingDropChance` sapling drop with a pity guarantee (`m_leafPity`, every
  `kSaplingPityLeaves`th dry leaf), so felling a whole tree can't strand the player.
  Saplings place on Grass/Dirt only and grow after `kTreeGrowSeconds` via `m_saplings`
  timers (`updateSaplings`; a blocked or player-overlapped spot retries each tick).
  Leaves with no Log within `kLeafReach` decay staggered (`updateLeafDecay`,
  `kLeafDecaySeconds`/`kLeafDecayChance`); every lost leaf — chopped OR decayed —
  rolls the same TWO drops through `rollLeafDrops`, so trunk-first felling doesn't
  starve the forest. All knobs sit with the other cadence constants in
  VoxelGameInternal.h.
- **Sticks ride that shared path too** (Aug 2026). They used to hang off the chop path
  alone, which quietly made trunk-first felling the optimal play: three axe swings
  orphaned the whole canopy and walking away collected the saplings for free, while
  punching the 22 leaves yourself was the only way to be taxed for them. The two rolls
  differ only in WHERE they land, and `chopped` is the whole of it — sticks are a ground
  drop at the cell when a player broke it, but go to the pack when a leaf decayed off a
  felled trunk forty blocks away, where a drop is one nobody would ever see. Saplings
  always go to the pack, chopped or not, because the pity counter is a promise about
  your INVENTORY and a drop resting on an unfelled canopy would not keep it. Both roll
  off the SAVED `m_sourceRng` with different salts: decay runs in the sim, and a sim
  roll a save cannot replay is a desync. The transient `m_lootRng` is gone.
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
- **Paintable atlas** — `game/assets/atlas.png` (256×256, a 16×16 grid of 16px tiles;
  map in `game/assets/ATLAS.md`) is loaded at startup (`engine::loadImage`, vendored
  stb_image in `third_party/stb/`); if missing or mis-sized the game falls back to
  generated flat-color tiles, so the PNG is never required. Blocks map to tiles via
  `Atlas::tilesForBlock()` (`{top, side, bottom}` — grass tops, log rings, machine
  lids); the mesher picks per face. Material items own icon tiles (`ItemInfo::
  atlasTile`); placeables borrow their block's side tile (`iconTile()`). Repaint the
  PNG in any pixel editor and rebuild (an always-run CMake target copies assets).
  **Since Oct 2026 the committed atlas.png is the SOURCE**: the PixelLab art pass
  (moodier, alchemical; see `ART_PROGRESS.md`) writes generated tiles into it, so
  `python tools/make_atlas.py` (pure stdlib, the programmatic starter set) now
  refuses to run without `--overwrite-art` rather than silently replacing them.
  The Overworld sky is dusk to match (`kSkyClear`/`kSkyStorm` in
  VoxelGameInternal.h). A SECOND sheet, `assets/shapes.png`, carries the 3D
  detailed blocks' textures as arbitrary regions rather than 16px tiles — it is
  generated, never hand-painted (see below).
- The sheet grew 8 → 16 rows for the recipe overhaul. Because a tile index is
  `row * Cols + col` and **Cols did not change**, every existing index kept its
  meaning — which is exactly why this grid may only ever grow in ROWS. Nothing
  but `Atlas::Rows`, `make_atlas.py`'s `ROWS`, and the map in ATLAS.md moved;
  the loader's size check and the generated fallback read the constants.

3D detailed blocks (sub-cube geometry, authored in Blockbench — July 2026):
- **`isSolid` came apart first.** It used to mean four things at once (gets
  meshed / occludes neighbours / blocks movement / stops rays), which a
  sub-cube shape breaks. `BlockInfo` now carries `solid` (participates in
  physics + raycasts) and **`fullCube`** (fills its cell, so something may be
  hidden behind it); `fullCube` implies `solid`, static_asserted. Three sites
  wanted occlusion rather than solidity: the mesher's neighbour test,
  `skyVisible` (it takes a ROOF to stop rain), and grass spread.
- **`BlockShape.h`** owns the geometry: `ShapeQuad` (one textured face of one
  box), `ShapeAabb`, `ShapeAnim`, and `kBlockShapes` in the kBlocks discipline
  (one row per `ShapeId`, static_asserted into enum order). A block names a
  `shape` on its kBlocks row — declared LAST in `BlockInfo` so a row can append
  it. `ShapeId` is **not** a save encoding (pure presentation), so unlike
  BlockId/ItemId it may be reordered freely. Air gets `ShapeId::Empty`, which
  buys the invariant everything else leans on: *a cell contributes exactly its
  shape's boxes*. `FullCube` deliberately carries no quads — plain blocks keep
  the mesher's `kFaces` fast path.
- **`tools/bbmodel_to_shape.py`** bakes Blockbench `java_block` models into
  `game/include/game/generated/BlockShapes.inl` (data only) + `game/assets/
  shapes.png`, a sibling of make_atlas.py (pure stdlib, hand-rolled PNG I/O).
  Three non-obvious things it handles: box-UV projects write **descending** uv
  rects for mirrored faces (65 of the cauldron's 213 quads), so rects are
  carried with their sign and never sorted — a min/max normalize would silently
  un-mirror a third of a model; sub-texel rects are routine (a half-unit-tall
  box has a half-pixel-tall side face) so the half-texel inset is clamped to a
  quarter of the rect; and a texture N×`uv_height` tall is a Minecraft
  **animation strip**, stacked in the sheet with a `vStride` emitted. It also
  drops faces sealed inside the model, and REJECTS non-cube (mesh) elements,
  face-level UV rotation, textures past the first, and geometry reaching more
  than `CELL_OVERHANG_TOLERANCE` outside its own cell.
- **Quads are fully resolved by the bake** — four corner positions, four UVs,
  and a normal, wound CCW seen from outside. The face-axis mapping, UV
  mirroring, and element rotation are all folded in, so the mesher copies
  vertices and never reconstructs geometry, and a mirrored/upside-down model is
  fixed by one sign in the BAKE with no second copy to drift (the `geoToWorld`
  precedent).
- **Rotated elements** (Blockbench's ±22.5°/±45°) are supported, which the
  Alembic needs — its octagonal cucurbit is 45° yaw and its spout is 45° roll.
  Since a rotated box is not an AABB and this table also backs collision, the
  two split: **geometry is exact, collision uses the rotated box's bounding
  box**. That is why `quads` and `boxes` are separate arrays. Rotation also
  inflates a box's reach by arithmetic (a 10-wide element sweeps a 14.14-wide
  diagonal), so a small out-of-cell overhang is CLAMPED with a warning rather
  than rejected; only a model designed to span cells fails.
- **Rendering is a second pass per chunk.** `appendChunk` fills two buffers:
  plain blocks into the atlas mesh, shaped blocks into `m_chunkShapeMeshes`,
  which binds `shapes.png` instead. Same vertex layout; they are split because
  they sample different sheets, which beats a per-vertex sheet selector across
  the whole world — and it is the pass transparency will need anyway when the
  tube hub grows glass windows. A world with no shaped blocks never binds the
  second sheet. `shapes.png` has no generated fallback: missing = shaped blocks
  don't draw, said once in the log.
- **Alpha CUTOUT in `voxel.frag`** — the world pass samples RGBA and
  `discard`s below `kAlphaCutoff` (0.5). Cutout, deliberately not alpha
  blending: it is order-independent, so chunks can keep being drawn in
  hash-map order with no depth sort and no second pass, which blending would
  force. It shipped inert (every texture then was opaque) and now has two
  users: the crossed-plane crops, and tube cargo, whose item icons are drawn
  in the world pass with their transparent surround discarded. A windowed
  tube hub would be the third. Note `discard` can cost early-Z on some GPUs; if it
  ever shows in F3, the fix is a separate program for cutout geometry, not a
  uniform toggle (drivers key off the discard being present in the shader at
  all, not on whether it executes).
- **Physics/rays are `Collision.h`/.cpp** (free functions over `(World&, ...)`,
  the MachineSystem/WorldEdit precedent): `rayAabb` (also reporting the face
  entered), `boxOverlapsWorld` (with a bounds broad phase for multi-box
  shapes), `surfaceTopAt`, `landingSurface`. `landingSurface` is what replaced
  `floor(y) + 1` in BOTH landing snaps — what you land on is a box top, rarely
  a cell boundary. The raycast no longer treats entering a cell as a hit: it
  tests that cell's boxes and, on a miss, KEEPS STEPPING, so a ray threads the
  gaps in a shape. `RaycastHit` carries `t`/`point`, so `CreatureSystem` reads
  the distance instead of re-deriving it against an assumed unit cube.
  DropSystem and the creature spawn scan rest on surfaces; the target
  highlight wraps `blockBounds()`.
- **Load-bearing assumption:** every query iterates the cells an AABB overlaps
  and tests only THAT cell's boxes, so shape geometry must stay inside its own
  cell. The bake enforces it with a hard error.
- **Every machine, source, soil and sapling is a shaped block** (the art pass
  finished Oct 2026; `ART_PROGRESS.md` logs each one, and most were authored
  in Python with `tools/modelkit` -- `tools/block_models/<block>.py` is how
  each was made and how to remake it). They no longer occlude or keep rain
  out, and you collide with the model rather than the cell. Their atlas tiles stay for the item icon and the generated-atlas
  fallback. Adding the next one is: bake, append a `ShapeId` row + a
  `kBlockShapes` row + a `kShapeNames` entry, then set `fullCube = false` and
  `shape` on the kBlocks row. If any of it should MOVE, author that piece as its
  own named group with a sensible pivot and add one `kPartAnims` row naming it;
  a group nothing names is free and stays still.
  The bake packs ONE sheet, so rerun it over every model at once (the .inl's
  header comment carries the last full command line) — baking one model alone
  drops the others out of `shapes.png`.
- **Animated shape textures play, and only while POWERED** (July 2026). All
  four models were authored as 8-frame strips and had been rendering frame 0
  forever; the bake already stacked every frame contiguously into `shapes.png`
  and emitted `vStride`, so finishing it needed no re-bake and no new art.
  Shaped vertices carry a 10th float — `aAnimBank` — and `voxel.vert` shifts
  `vUv.y` by `uAnimV[bank]`, a per-`ShapeId` offset `updateShapeAnim()`
  recomputes each frame from the pause-aware `m_animClock`. **Never a remesh:**
  advancing a frame is one uniform upload for the whole world, so a room full
  of bubbling cauldrons still reads `X0 PER S` on F3. Rates are per shape
  (Blockbench's `frame_time` is in ticks, and the game ticks at the same
  20 Hz), which is why the vertex carries a bank index rather than its own
  stride — the Auger runs 3 ticks/frame while the Cauldron runs 2, and one
  global counter could not serve both. `kMaxShapeBanks` sizes `uAnimV[]` with
  headroom exactly like `kMaxEntityBones`, static_asserted against
  `ShapeId::Count`.
  The **power gate is free**: the mesher passes bank 0 (`ShapeId::FullCube`,
  whose offset is permanently zero) for an unpowered block, and power was
  already a mesh input for the energized glow — `solvePowerAndMarkDirty`
  dirties exactly the chunks whose glow flipped, so a machine losing power
  re-meshes regardless. A dead machine parks on frame 0. Gating on *crafting*
  instead would NOT be free: that flips constantly and would thrash remeshes,
  so it needs the per-block uniform indirection moving parts will want anyway.
  Only the plain mesh keeps the 9-float layout — it leaves attributes 4-6
  disabled, which read back as bank 0, offset (0,0,0) and part slot 0, all of
  them inert, so the ordinary world pays nothing for either animation path.
- **Block parts MOVE** (Aug 2026) — the Auger's `drill` spins, the Cauldron's
  `contents` sway, the Infuser's `core` turns and its `emitter` throbs, on
  models that already carried the rig. `bbmodel_to_shape.py` now reads the
  `outliner`/`groups` it used to discard and emits a **part** per named group;
  `voxel.vert` shifts each vertex by a per-part `uniform mat3 uPartRot[32]`.
  **The pivot is never baked, because it never has to exist.** A chunk vertex is
  world-space and cannot recover its own cell (`floor(aPos)` is unsafe —
  geometry touching a cell's top face lands in the wrong one), but rotating
  about a pivot rearranges to `p + (M*d - d)` with `d = p - pivot`, and `d` is a
  VECTOR, identical in cell and world space. So the bake stores the offset and
  the shader needs no pivot, no cell origin and no `floor()`. A **3x3** rather
  than a 4x4 for the same reason translation is absent — which halves the
  uniform cost (288 of GL 3.3's guaranteed 1024 vertex uniform components) and
  leaves room for uniform SCALE, so `PartMotion::Pulse` needs no second
  mechanism.
  **Which parts move is C++, not model data**: `kPartAnims` in BlockShape.h
  (kBlocks discipline) names a part by NAME, so re-authoring a model cannot
  silently animate a different lump of it, and a typo is a `static_assert`.
  Only an ANIMATED part costs a slot — the four models carry ~37 groups and
  spend four — and `kPartSlots` resolves `(shape, part) -> slot` at compile
  time so the mesher never does a string compare.
  The power gate is ONE value: `appendShaped` takes an `animShape` that is
  `ShapeId::FullCube` when unpowered, whose bank offset is zero and whose every
  part slot is 0, so "texture parks on frame 0" and "parts stand still" are the
  same decision and cannot disagree. Never a remesh — motion is one uniform
  upload for the world.
  Two things it deliberately does NOT do: a moving part does not move its
  COLLISION (`boxes` is baked once; a spinning drill is scenery to physics), and
  nested groups do not inherit a parent's motion (`ShapePart::parent` is baked
  but unread, so that can be added later with no re-bake).
  Budget: **45 KB (Auger) to 120 KB (Infuser)** of chunk mesh per placed shaped
  block against ~1.3 KB for a plain block, so detailed shapes stay on machines
  rather than anything placed in bulk. Those figures are ~1.5x what this file
  used to say: the bake's size report had assumed a 9-float vertex since before
  shaped vertices carried an animation bank, and parts took it to 14.
- **Parts can MOVE, and a hand can drive them** (Sep 2026, user request — the
  Mortar's pestle). Three additions, all uniform-side, none a re-bake:
  **Translation**: each slot also carries a `uniform vec3 uPartOff[32]`, added
  after the pivot turn, and `PartMotion::Bob` is a plunge along `axis` that
  rests at zero (`amount` in blocks) — a model at rest sits exactly where it was
  authored. The 3x3 stays a 3x3; 96 more vertex uniform components, still well
  inside GL 3.3's 1024. **Composition**: `kPartAnims` rows naming the SAME part
  now share a slot (`kPartRowSlots`) and compose — turns multiply, moves add —
  so the pestle Spins (a cone round the bowl, since it leans 22.5° from a pivot
  at the bottom) and Bobs (down into the powder twice a turn) at once. Before,
  a second row on a part would silently have won. **Cranked rows**
  (`PartAnim::cranked`) take their phase from the player's hand, not the clock:
  every good crank step adds a quarter turn to `m_crankTarget` and
  `m_crankTurns` eases after it (`kCrankAnimEase`), so the part moves exactly as
  far as the hand did and stops where it stops; a slipped grip moves nothing.
  `rate` then means cycles per TURN. A static_assert keeps a part's rows all
  cranked or all clock-driven.
  **The gate for the hand-cranked tier**: none of it is ever a power node, so
  the power gate parked those parts forever. `appendChunk` now also takes a
  `cranking` set — the one hand-cranked machine whose panel is open
  (`updateCrankAnim`) — and animates it despite no power. That set changes only
  when a crank panel opens or closes, so it costs one chunk remesh per edge,
  never per frame: the same bargain the power gate makes. Closing the panel
  parks the part back at rest. Verified in game (a Mortar, eight presses: the
  pestle sweeps the bowl and mashes, stays inside the walls).
  **Cranked parts share their uniform slots** (Oct 2026). Since only that one
  machine moves, `kPartRowSlots` gives clock-driven parts a slot each and puts
  every cranked part in one shared block after them (a shape's first cranked
  part in the first, its second in the next); `updatePartAnim` fills the block
  from `m_crankShape` alone. Eleven hand-cranked models cost one slot, not
  eleven. The `static_assert` against `kMaxShapeParts` checks `kPartSlotCount`
  -- it used to count kPartAnims ROWS, which was the ceiling that would
  actually have bitten first.

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
  Pause mutes hums (sim frozen) and DUCKS rain to `kRainPausedGain` (eased on
  real frame time) rather than cutting it, while the streaks themselves freeze
  mid-fall on the pause-aware `m_animClock` (Oct 2026, user request — they ran
  on the wall clock and kept falling over the pause menu). Mix knobs sit in the `// ---- Audio ----`
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
  **That leniency is player-facing only**: `CreatureSystem::checkModels(dir)`
  (static, no window/GL — `loadBbModel` is pure parsing) parses every `kSpecies`
  row's model and returns English diagnostics, and `--selftest` fails on any.
  It exists because boss #1's `.bbmodel` was never committed and every build
  stayed green for weeks while a launch pillar was absent from fresh clones. It
  asks more than "does the file exist": no geometry, an undecodable texture, a
  missing `idle`/`walk` clip, and a row with `swingImpact > 0` whose model has no
  `attack` clip (a telegraph the player cannot see). **Adding a `kSpecies` row
  therefore means committing its model**, under the convention above.
- **`tools/modelkit/`** (Oct 2026) builds, previews and checks block shapes and
  creatures WITHOUT the game: `preview.py <model>` renders four views plus
  animation strips/GIFs in seconds and checks cell bounds across a part's
  whole motion (the bake only sees the rest pose), mesh budget, bone cap and
  required clips; `spec.Model` writes new models from Python. Blocks load
  through the bake's own `load_model`, posed by the real `kPartAnims` rows; a
  creature through a replica of BbModel.cpp, which `preview.py` cross-checks
  against `voxel-factory --check-bbmodel <file>` -- a headless mode that runs
  the ENGINE's loader on one file and prints bones, vertices and clips (exit 1
  if it fails to load or lacks idle/walk). See models/AUTHORING.md.

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
- **Machine-made plates** — Copper Plate is a **Press** recipe (Ingot → Plate;
  it was a Grinder recipe until July 2026). Nothing hand-craftable costs a
  plate, so the tech tree bootstraps: chop the starting tree → ingots → circle
  → Generator + Press → fueled Press presses plates → frames → everything else.
- Deeper recipes (Machine Frame = Plate ×3 + Crystal + Wood ×2; each alchemy
  machine = Frame + extras), a lean starting kit (exactly the bootstrap pair plus
  slack), logs drop Wood ×2, and sources scatter beyond `kSourceMinRadius` so the
  outer band is where the resources are — logistics distance is the point.

Logistics: buffers, the crate, and belt filters (Aug 2026 — the factory
becomes a puzzle):
- **Machine buffers have a bottom.** Every `Inventory` is an unbounded
  count-per-item array, which for the player's pack is deliberate (hardcore
  death is its pressure) but for a MACHINE meant nothing could ever back up:
  an output never filled, a machine never jammed, and a belt never had to be
  routed anywhere in particular. `kMachineInputCap` (64) and
  `kMachineOutputCap` (32) — per item TYPE, matching how `Inventory` counts —
  are what give every logistics block a job. **The capacity rule lives in two
  places only**: `machineAccepts` (split into `wantsItem` + a room check, so
  the rule isn't repeated down every branch) and a gate in `tickPowered`.
  Belts needed **no change at all** — `beltStep` already leaves an item sitting
  on a belt whose target refuses it, so lines congest for free.
- **A jam HOLDS; it never cancels or eats.** Progress is kept, inputs are
  untouched, and the fuel gate sits *after* the jam check so a jammed burner
  doesn't consume its stock standing still (the generator's "hungry" rule).
  Crucially the check runs BEFORE `rollOutput`, because that roll advances the
  world's shared RNG counter and one thrown away would desync a sifting line
  from its own save — so a weighted recipe needs room for **every** face it
  could roll. `Machine::jammed` is transient (re-derived each tick, never
  saved). Miners and Rune Cores jam by the same rule.
- **Feedback, because an invisible jam is worse than none**: the floating bar
  goes fully amber (a jam is FULL, not partial — a frozen sliver would read as
  "slow"), the panel header shows OUTPUT FULL outranking every other status,
  and the look-at tooltip names which pile is full and says what to do.
- **The Storage Crate** (`MachineKind::Storage`, `demand = 0` so it stays off
  the power graph — the Rain Barrel/Pedestal precedent). Its tick migrates
  `input` → `output` each tick before the power gate, which is the whole trick:
  `beltStep` fills a machine's `input` and drains its `output`, so one buffer
  swap makes a single block both feedable and drainable with no belt code.
  `kChestCap` = 512/item. `machineAccepts` counts BOTH halves for a crate or
  the cap would never bind. **A crate is also the splitter** — every belt
  pointing away from one pulls independently, so one line in feeds two out —
  which is why the roadmap's separate splitter/router block was not needed.
  Hand-craftable (Wood ×8) on purpose: outputs fill long before you own a
  Circle, and gating the fix behind one would mean meeting the problem with no
  way to solve it. Generic machine save records = no save bump.
- **Belt filters** (`Belt::filter`, save **v23**). This retired `beltStep`'s
  rule for draining a mixed output — "whichever item has the lowest ItemId
  ordinal" — which was arbitrary, invisible, and impossible to teach. It binds
  BOTH ways: a filtered belt pulls only its item from a machine, and refuses
  anything else from the belt behind, which is what makes a sorting **lane**
  rather than just a sorting tap. The stall that creates is the intended,
  visible failure (the stuck cargo and the target's filter both draw). Set with
  `Action::BeltFilter` (default **F**, rebindable, needs a Wrench in the pack):
  the aimed conduit takes the selected hotbar item, pressing again with the
  same item clears it. The selected item is a REFERENCE, never consumed, so you
  can plumb a line before the factory has made any of what will run down it.
  An empty filtered belt draws its filter as a ghosted icon (`kFilterGhost` —
  deliberately not `kOutOfStockTint`, which means "you don't own this").
  v23 is a tail append **within the belt record**, read version-gated like
  v20's `burnLeft` and v21's fuel buffer; `kOldestLoadable` does not move and a
  pre-v23 belt loads unfiltered, which is what it was.
- **Build ergonomics, which the above makes load-bearing** — richer logistics
  means laying many more belts, so shipping it without these would have made
  the game worse. Held RMB keeps placing (**only** the place path; every other
  RMB verb re-tests the edge, so a repeat aimed at a machine places against it
  instead of reopening its panel). Two knobs, not one: `kPlaceRepeatDelay`
  (0.28 s) is what stops an ordinary 80–150 ms click placing twice, and only
  past it does `kPlaceRepeatSeconds` matter — the same shape as any key-repeat.
  A belt placed against a MACHINE now aims away from it (`beltStep` only pulls
  from the machine directly behind a belt, so the camera guess was wrong
  exactly when you stand at a machine looking at it). Shift+R reverses
  `rotateBelt`, capping the worst case at two presses instead of five.
  Middle-click picks the aimed block onto the hotbar. F3 finally shows XYZ.
- Covered by `--selftest`: a jam holds progress + spends no fuel + leaves the
  RNG counter alone, a belt facing a full machine keeps its cargo, a crate
  feeds two belts one item each per step, filters pull only their item and
  refuse the rest, and a belt filter round-trips through the save.

Belts became tubes (Sep 2026, merged Oct 2026 as PR #8 — the visual half
of logistics, and **no save change**):
- **One baked hub, six named arms, drawn per CELL.** Conduit
  (`ShapeId::ConduitHub`) and Wire (`ShapeId::WireHub`) are shaped blocks
  (`fullCube = false`), but which arms show is a property of the cell's
  neighbours, not of the BlockId. Rather than bake 64 arrangements, the arms are
  named PARTS of one model (`arm_north..arm_down`), and **`kConnectParts`**
  (BlockShape.h, the `kPartAnims` discipline a second time) binds each part
  NAME to a face index in `kShapeFaceDirs` order. A typo is a `static_assert`;
  `kPartFaces` resolves `(shape, part) -> face` at compile time so the mesher
  never string-compares. `appendShaped` takes an `armMask` and skips a quad
  whose part hangs off a masked-out face; anything without connection parts
  passes `kAllFaces` and pays nothing. Note Blockbench's convention: **north is
  −Z**, and `--selftest` proves each arm's geometry really reaches the wall its
  name claims, since a mirrored model would connect correctly and point the
  wrong way.
- **`kFaces` takes its offsets from `kShapeFaceDirs`** rather than writing the
  six directions a second time: a face INDEX is shared vocabulary now, and a
  divergence would connect the right arm to the wrong side silently.
- **The rules are `TubeShape`** (TubeShape.h/.cpp, free functions over the
  registries, so `--selftest` can ask "does a corner grow the right two arms"
  instead of someone squinting at a screen). They differ on purpose. **A wire
  asks `PowerSystem::isPowerNode`** — the solver's own flood predicate, so a
  wire that looks connected always is. **A conduit follows the items**: out the
  way it faces, back to a machine it pulls from, and sideways to any belt aiming
  INTO it; two belts running side by side share no arm, because nothing passes
  between them.
- **The OUT arm is always drawn, even into open air, and glows**
  (`kConduitFlowGlow` in ChunkMesher.cpp, a per-vertex emissive value — no
  pass). It inherited the retired top-face arrow's job: a conduit must say which
  way it moves things whether or not anything is there to receive them. The
  look-at tooltip gained a conduit case (FLOW: <dir>, its filter, and the
  routing rule), which together with the glowing arm replaces everything the
  arrow said.
- **Cargo is world geometry and it flows** (`buildCargoMesh`, drawn after the
  shaped pass with the atlas bound). The old `UiRenderer` billboards drew
  through walls at any range and teleported between cells. `Belt::cameFrom` is
  TRANSIENT — recorded on the RECEIVER by `beltStep` (on the receiver because a
  corner turns), cleared at the top of every step, set only where an item really
  moved — and the render lerps from `pos + cameFrom` into the cell over the
  following step. **One step behind the sim, never wrong**: predicting the next
  hop would snap back every time a belt lost a claim at a junction. A jammed
  line parks its cargo dead centre. The save fixture sets `cameFrom` and asserts
  it comes back cleared, so a later "helpful" addition to the belt record has to
  argue with a test. Being in the world pass also put the **alpha cutout** to
  work (an icon's transparent surround is discarded, not a black card) and gave
  cargo the ground drops' distance cull (`kDropRenderDist`). The **filter ghost
  stays screen-space** on purpose: it is an annotation, it must read through the
  tube it labels, and cutout cannot tint.
- **Cargo rides ABOVE the hub** (`kCargoLift` 0.38) because the hub texture is
  opaque: an item at the centre would be inside solid pipe. When the art grows
  windows (alpha holes, never translucency — the cutout rule), set `kCargoLift`
  to 0 and nothing else moves.
- **Collision is the hub alone** (`3a48825`). The arms are baked with all six,
  but Collision and raycasts take a `World` and cannot see belt facings, so they
  collided with arms that weren't drawn — a lone conduit stood you 0.25 above
  its visible top. Both shapes now collide with the always-drawn hub, so
  collision can never claim more than the picture; `--selftest` pins that a
  connected shape's boxes stay inside its always-drawn quads. The conduit hub
  is ~0.54 of a block (0.23..0.77), so **you jump onto a run, you do not walk
  over it**; the gap between hubs (0.46) is narrower than a body (0.6), so a run
  does bear you.
- **Placement fixes the tube exposed**: a conduit used to go vertical once
  `|front.y| > 0.7` (a 44° glance), and you look ~55–60° down to lay a run at
  your feet — so floor runs all faced DOWN. Invisible on a cube; a row of
  disconnected stubs on a tube. Now `kVerticalLook` = 0.95 (~72°). A conduit
  added to the END of a run, on the run's own axis, inherits its facing; one
  clicked onto a run's SIDE still branches the way you look.
- **Re-aiming dirties the neighbourhood** (`World::markDirtyAround`): arms
  follow facing, so a rotate can take an arm off the neighbour that fed it.
  Block edits already dirtied their neighbours.
- **Budget** (14-float shaped vertex, ~336 B per quad). Conduit: 126
  always-drawn quads (frame, rivets, core glass) + 30 per arm — **~42 KB alone,
  ~62 KB for a straight run, ~100 KB as a six-way junction**, against 1.3 KB for
  a plain cube. Wire: 6 + 5 per arm, ~5 KB in a run. The conduit is the first
  content placed in BULK at machine-scale cost; not yet measured on a large
  factory in F3, and the fix if it shows is leaner art (rivets/rails into the
  texture), not code. The art itself is a placeholder benny is re-authoring;
  keep the contract — same file stems, the six `arm_*` group names, geometry
  inside the cell. `models/conduit_arm.bbmodel` is unused by the bake.
- `tools/normalize_shape_texture.py` crops an authored animation strip to frame
  0 for a block that can never animate (a conduit is not a power node, a
  pedestal draws no power). `tools/make_wire_model.py` generated the wire
  placeholder — overwrite `models/wire_hub.bbmodel` keeping the group names,
  and then delete the generator's half (the creature-model convention).
- **Deliberately NOT in this change: multi-slot belts.** The roadmap had bundled
  them in to share a save bump, but flowing cargo needed no save change, so the
  bundling bought nothing; slot count is a throughput decision for play, like
  powered belts. When wanted: `Belt` gains `std::array<ItemId, kBeltSlots>` as a
  v23-style in-record tail-append gated on `version >= 26`, `kOldestLoadable`
  unchanged, and `cameFrom` becomes per-slot.

Farming (Aug 2026 — the island's last renewable, and the one with an
economic job rather than flavour):
- **The problem it solves is a hard cap.** A Source grows at most 5 nodes
  within r=4 and a Miner takes the nearest one every 4 s, so the whole
  Herb → Ground Herb → Tincture → Healing Draught branch was bounded by patch
  regrowth no matter how much factory you pointed at it. A field is bounded by
  **area and layout** instead — the game's difficulty axis (logistics distance)
  applied to agriculture. The crop is **Herb** for exactly that reason.
- **`SoilKind`, ordered** (`provides` / `needsSoil` on `BlockInfo`). This
  replaced a hardcoded `if (id == BlockId::Sapling)` in `WorldEdit::placeBlock`,
  which crops would have grown a second branch of and which a content pack could
  never have reached. The whole rule is `provides >= needsSoil`, and the
  ordering pays immediately: Tilled Soil satisfies a sapling for free, because
  worked ground is still ground.
- **Tilled Soil + the Copper Hoe.** RMB the hoe at Grass/Dirt
  (`WorldEdit::tillSoil`, the `fuseSources` shape — a tool RMB transmuting the
  aimed cell). Tilled Soil drops Dirt, so tilling can never duplicate soil, and
  has no item of its own. It **must survive a harvest** or every automated field
  would need re-tilling by hand forever. Emphatically not a durability system.
- **Rich Soil is the next rung, and compost is how you climb it** (Aug 2026).
  `SoilKind::Rich` extends the ordered ladder, so `provides >= needsSoil` gave it
  everything Tilled does for free — including a sapling — with nothing told about
  the new rung. `WorldEdit::enrichSoil` is `tillSoil` one step later and the same
  shape, with one deliberate difference: compost is a plain MATERIAL, not a tool,
  so the CALLER spends it, and only on a true return, or a misclick at a wall
  would eat it. It tests for Tilled Soil by BLOCK rather than `provides < Rich`,
  which is what makes re-enriching a silent no-op instead of an accident (the
  player-facing site then distinguishes "already rich" from "wrong block" from
  "something is sitting on it", because those are three different mistakes).
  Rich Soil drops Dirt like the tilled ground it came from — enriching must not
  be a way to duplicate soil either.
- **Nutrition is a second axis, and it deliberately DOES stack with water.**
  Rain and irrigation share one multiplier because they are the same thing
  arriving two ways; compost is not water. It is a standing build investment
  spent into the cell rather than onto the weather, so a fed and watered field
  runs at `kRainGrowthMult * kRichSoilMult` — the only place two growth
  multipliers meet in the game, and the point of it: it is what makes forestry
  worth pointing at a farm. `kRichSoilMult` is deliberately the smaller of the
  two, since laying rich soil is permanent and free to run while water costs a
  barrel, a belt and an Irrigator forever. `CropSystem` tests `provides >=
  SoilKind::Rich` rather than `BlockId::RichSoil`, so a content pack's own richer
  soil earns the bonus by saying so. `--selftest` pins the departure AND the rule
  it sits beside — irrigating a field it is already raining on must change
  nothing — so a later edit cannot read one as a licence for the other.
- **The Composter is where the tree's surplus goes**, and the reason it exists.
  A grown tree returns ~8 saplings and ~11 sticks for the ONE sapling that made
  it, and only one of those replaces it, so without a sink the most net-positive
  loop in the game dead-ends. Green + brown makes **Compost**, and compost is the
  branch point: `composter/dirt` (soil), `composter/briquette` (fuel), or
  enriching a field. There is deliberately no PlantFiber variant — a second
  compost row would make a mixed-input Composter non-deterministic under AUTO for
  no gain, and AUTO takes the first row whose inputs are present, so row ORDER is
  gameplay here as everywhere.
- **Four stages, four `kBlocks` rows** (`HerbCrop0..3`). The mesher picks a
  shape from the BlockId alone and `Chunk` is a flat BlockId array with no
  per-cell metadata: the timer can live in a side registry, the LOOK cannot.
  Since save v22 those rows are no longer a permanent commitment.
- **`CropSystem`** (CropSystem.h/.cpp) — free functions over
  `(World&, CropMap&, ...)`, the MachineSystem/WorldEdit/DropSystem precedent,
  rather than a fifth `VoxelGame::update*` beside `updateSaplings`. The
  deciding reason is testability: a member of the GL-owning VoxelGame cannot be
  exercised by `--selftest` at all. Timers RESET on each advance rather than
  accumulating, so retuning one stage does not shift the ladder above it. Dig
  the soil out from under a crop and it dies rather than ripening in mid-air.
- **Seeds come off the Sifter**, which means the hand-craftable **Sieve**, so a
  field is reachable before the Alchemy Circle and the island's wild bushes are
  the bootstrap. Deterministic, not a weighted roll: farming's promise is that
  it scales with area, and a seed you might not get would put that behind luck.
- **The crop model is crossed planes** (`tools/make_crop_models.py` →
  `models/herb_crop_*.bbmodel` → the usual bake). This is the first content
  placed in BULK, so the quad budget is the real constraint: **4 quads** against
  the Infuser's 367 — 1.3 KB against 120 KB. It needed one bake
  change — the guard at `bbmodel_to_shape.py` counted ANY zero extent as
  degenerate, so both planes were skipped and the model died on "nothing to
  bake"; it now rejects only two-or-more flat axes and drops a flat box's four
  zero-area faces. A flat element's COLLISION box alone gets a one-unit
  thickness (a zero-thickness AABB overlaps nothing, so the crop would be
  neither walk-into-able nor breakable); genuinely thin boxes are untouched, or
  four shipped models would have quietly fattened. This is also what finally
  **exercises the alpha cutout** in `voxel.frag`, inert since July 2026.
  Crops stay `solid` so you can aim at one; walking through wheat is a later
  change that splits ray boxes from physics boxes.
- **The Harvester is the Miner one field over**, so `tickMiner` became
  `tickReaper`, parameterized by what counts as a target and what the cell
  becomes afterward. It is its own `MachineKind` precisely because of the
  second: a Miner leaves Air, a Harvester must leave a stage-0 seedling on
  intact tilled soil. The replant also needs its growth TIMER, which is why
  `MachineSystem::tickPowered` now takes the crop registry — without it a field
  reaps once and stands still, looking planted. Powered, deliberately: what the
  powered tier sells is not having to be there.
- **The Irrigator closes the weather loop.** It spends Rain Water to keep a
  radius growing at the rain rate, giving the Rain Barrel and Bucket a real
  sink. Rain and irrigation share ONE multiplier so they can never stack into a
  third rate nobody tuned. `demand = 0` (the Rain Barrel precedent) — what it
  spends is water, so Barrel → belt → Irrigator is a complete answer needing no
  grid. `MachineSystem::activeIrrigators` hands CropSystem positions, never the
  machine map: a growth system has no business knowing what a machine is, and
  MachineSystem already includes CropSystem.
- **Save v25** appends the crop timers at the end of the file (the v15/v16/v18
  shape). The plants themselves ride the chunk data like any block, since v22's
  key tables already name them; a pre-v25 save loads with an empty field.
  Verified against a real v23 save: loads intact and re-saves losslessly.
- **No hunger meter, ever** (decided July 2026). Hardcore death is the pressure;
  crops feed the FACTORY, not the player.
- Covered by `--selftest`: tilling only works on plain ground and refuses a
  covered cell, a sapling accepts tilled ground, a crop refuses plain dirt,
  a field ripens and STOPS at ripe, rain is faster, a crop with no soil dies and
  drops its timer, the Harvester takes only ripe plants and leaves stage 0 on
  intact soil and registers it, it jams on a full output, an unpowered one does
  nothing, and two identical plants on identical ticks end at different stages
  when one is in an irrigator's reach.

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
  in drawHud. **A strike is telegraphed and dodgeable** (Aug 2026): a species
  with `swingImpact > 0` COMMITS on contact — spending `strikeCooldown`, playing
  its one-shot `"attack"` clip via `playOnce` — and the damage/knockback land
  `swingImpact` seconds later *only if the player is still inside
  `strikeRange`*, so stepping out means the axe hits nothing. The countdown
  (`Creature::swingLeft`) runs down in `update` (the SIM), never in
  `frameAdvance`: when the blow lands is gameplay and must not drift with frame
  rate; `frameAdvance` owns only `attackLeft`, the render-side lock that stops
  the next step cutting the swing off. `swingImpact = 0` keeps the old
  on-contact behaviour, so it is **opt-in by AUTHORING** — a species without an
  `attack` clip gets it for free, and `checkModels` fails a row that promises a
  wind-up its model cannot show. Model: the hand-authored
  `game/assets/models/void_warden.bbmodel`
  (`vg::kWardenModel`; same lenient loading as the creature), which is what
  forced the four Blockbench-loader capabilities above. Knobs in
  `// ---- Boss & arena ----`.
- **Model files have a convention** (Aug 2026): `models/` holds **block-shape
  bake sources only** — `bbmodel_to_shape.py` input, never loaded at runtime —
  and every CREATURE `.bbmodel` lives only in `game/assets/models/`, which
  `copy-assets` ships. A creature model in `models/` is a copy that will drift;
  the bake correctly ignores it, silently. Generated starters
  (`make_test_model.py`, `make_tempest_model.py`) write straight into
  `game/assets/models/`, so a hand-authored replacement of the same name just
  overwrites the starter — but then **delete the generator's half**, or the
  next run silently reinstates the placeholder the game no longer loads.
- **Boss #2 — THE TEMPEST** (rising tier): the **Storm Key** (Void Catalyst
  ×1 + Crystal ×4 + Rain Water ×4 — the warden's drop is the gate) opens the
  same BossArena dimension with a variant generation: a tighter ring with a
  broken rim lip, under a permanent storm (`m_arenaStorm` drives slate sky,
  full-rate rain streaks/audio, and rain dimming inside the arena only).
  The tempest is faster than a walking player (sprint or die), hits for 2
  hearts, and drops the **Storm Core** (the better-fuels hook for generator
  tiers). Each boss appends its own save flag (`tempestDefeated`, v14).
  Victory titles come from the species name. Boss #3 = one more species row,
  arena variant, and key recipe.
- **F6 is a hidden dev key** (F4's sibling): grants a Teleport Key, Storm
  Key, and Copper Sword and assigns them to the hotbar — the boss loops
  without the philosopher grind.

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
  save-compatible; reordering did not, until v22's key tables made it so).
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

Timed breaking, tool gating & ground drops (foundation for a harder start —
see ROADMAP):
- **Timed breaking** — mining is HELD, not a click: onUpdate accrues
  `m_breakProgress` (dt) against the aimed cell while LMB is down, breaking it
  when it reaches `breakSeconds(block, held)` (`VoxelGamePlayer.cpp`). A thin
  progress bar draws over the target (`drawHud`). Progress resets when the aim
  leaves the cell or the button releases. A pure weapon (the Copper Sword)
  swings on press and never mines; anything else mines at its tool/hand speed.
- **Tool gating** — `BlockInfo` gained `hardness` (seconds to break BY HAND),
  `tool` (`ToolType` class), and `toolTier`. `ItemInfo` gained `tool` +
  `miningSpeed`. `breakSeconds` divides hardness by the tool's speed when the
  class AND tier match, else returns the full by-hand time — so the wrong/no
  tool is slower only in the sense that it forgoes the speed-up; there is no
  extra multiplier (an earlier `kHandBreakPenalty` knob no longer exists, and
  whether wrong-tool mining should cost extra time is an open tuning question).
  `yieldsDrop` forfeits the drop entirely for a gated block (`toolTier > 0`)
  broken without its class at that tier. Copper **Pickaxe** (stone/ore/crystal/
  essence/resonant/voidstone), **Axe** (logs), and **Shovel** (dirt/grass/sand,
  ungated — just faster) are hand-crafted from Copper Plate + Wood. Your own
  placed machines/sources stay retrievable by hand (soft, ungated). Both wood
  and stone gate, so the **starting kit grants one of each Copper tool** to
  avoid a bootstrap deadlock (the real ramp — a crude ungated starter tier,
  tools out of the kit, gated Generator — is a ROADMAP item).
- **Ground drops** (`Drop.h` / `DropSystem.*` — free functions + VoxelGame
  glue, the MachineSystem/WorldEdit precedent) — mining spawns physical
  `DroppedItem`s (`spawnDrop`) instead of adding straight to the pack; they
  fall + settle onto the first solid block (`DropSystem::tick` in onTick) and
  the player auto-collects any within a pickup **cylinder** (`updateDrops`:
  `kPickupRadius` horizontal + `kPickupVertical` band, dimension-filtered — a
  cylinder, not a sphere, so an item resting in the 1-deep pit a just-mined
  block leaves is still in reach). A non-void death scatters the whole pack at
  the death spot (recoverable; the void and arena death still fully wipe — the
  earlier death-drops decision). Rendered as billboarded icons like belt cargo
  (`drawHud`, culled behind-camera + beyond `kDropRenderDist`).
- **Optimized:** `DropSystem::spawn` merges into a nearby like drop (bounds the
  entity count under repeated mining) and caps the population (oldest settled
  evicted); a `settled` flag skips physics for resting drops; pickup is a
  squared-distance pass; billboards reuse `UiRenderer` (no new GPU state).
  Timed breaking is O(1)/frame and only while LMB is held.
- Overworld drops are saved (v16 append; older saves load with an empty list);
  the arena is transient so its drops are never written. Knobs live in the
  `// ---- Mining & tools ----` and `// ---- Drops ----` blocks of
  VoxelGameInternal.h (`kPickupRadius`, `kPickupVertical`,
  `kDropRenderDist`, `kDeathDropPickupDelay`) plus DropSystem.cpp's own physics
  constants.

Combat depth (the pillar's second wave — armor, the armory, and the potions'
combat jobs):
- **Ranged + buff potions** — the previously-inert alchemy products now fight.
  **Mana Vial**: LMB casts a hitscan alchemy bolt along the aim ray
  (`CreatureSystem::tryRangedAttack`, sharing the melee `rayPickCreature` helper —
  blocked by nearer solid blocks, longer `kBoltReach`, its own `kCastCooldown`,
  spends one vial, never mines, a boss kill rides the same `awardBossKill` path
  as the sword). **Elixir of Vigor**: RMB drinks for a timed `m_vigorTimer` buff
  multiplying weapon damage (`kVigorDamageMult`, applied to BOTH sword and bolt);
  transient (not saved), HUD timer bar above the hearts. Knobs in the
  `// ---- Combat gear & potions ----` block of VoxelGameInternal.h.
- **Armor + mitigation** — three equip slots (head/body/feet) in `m_armor`
  (`std::array<ItemId, kArmorSlots>`). `ItemInfo` gained `armorSlot` (an
  `ArmorSlot` enum) + `armor` (flat reduction). Equipped via the Tab overlay:
  the armor strip sits above the hotbar strip (`invLayout` extended), drag a
  matching piece onto its own slot to wear it (the piece LEAVES the pack, unlike
  the reference-only hotbar assignments), RMB unequips. `recomputeArmor()` caches
  the summed reduction (capped at `kArmorMaxReduction`) into `m_armorMitigation`,
  applied ONLY to combat damage (`VoxelGameSim.cpp` boss-strike site) — fall
  damage stays raw by design. On death the worn pieces fold back into the pack
  first, so they scatter (normal death) or wipe (void/arena) uniformly. Saved in
  v18 (append; older saves load unarmored).
- **The Forge** — a `MachineKind::Processor` machine (`BlockId::Forge`,
  hand-crafted from `MachineFrame + Plate ×2`), so the sim tick / power solve /
  panel UI all dispatch on it unchanged. Its `MachineRecipe` rows forge the
  Copper armor set from plates and the **Aegis** set gated on the boss drops
  (**Void Catalyst** / **Storm Core** — the Storm Core's first sink), realizing
  "factories are the real weapon": gearing up is an automation problem. Append-
  only blocks/items; the F6 dev kit grants a Forge + mats + a ready Aegis set.

The shared parts tier (crafting depth — the Alchemy Circle's foundation):
- **The Press** — a `MachineKind::Processor` (`BlockId::Press`, built on the
  Alchemy Circle from `CopperIngot ×4 + Stone ×4`), so the sim tick, power
  solve, `machineAccepts`, and panel UI all dispatch on it unchanged. It presses
  plates and forms the shared parts — **Rod** (Ingot → Rod ×2), **Gear**
  (Rod ×2), **Machine Casing** (Plate ×4), **Etched Plate** (Plate + Crystal
  Dust ×2) — and assembles `Casing + Gear ×2 + Etched Plate` into the
  **Machine Frame**. (Since the recipe overhaul the structural half of that
  chain is IRON; see the overhaul section below.)
- **MachineFrame is no longer hand-craftable.** Its `kRecipes` row is gone, so
  every machine now sits four machine stages behind raw ore instead of one menu
  click (6 ore + 1 crystal → 14 ore + 2 crystals). Intermediates coming out of
  machines is the Forge's "gearing up is an automation problem" principle
  applied to the tech tree itself; it also shrinks the craft menu, which
  overflows short windows (`craftLayout` grows with the recipe count and
  `UiRenderer` has no scissor primitive).
- **`Ingot → Plate` lives on the Press** (moved off the Grinder, July 2026 — a
  press presses). Its own circle pattern must never cost a plate or the tree
  deadlocks behind a Press you cannot build; the pattern is ingots opposite
  ingots, stone opposite stone (a fully-occupied 4-slot necklace, so it can
  collide with nothing). Ladder: smelt Ingot → circle → Press + Generator →
  Plates → parts → Frame. The Grinder stays on the critical path via
  Crystal → Crystal Dust → Etched Plate.
- Atlas tiles 39/40 (block) and 112-115 (icons). F6 grants a Press plus stock at
  every link.
- **Save v19** was the first non-append change in the file's history: the move
  shifted two recipe lists under saved indices, so `load` had to remap them.
  Save **v20** ended that whole class of problem by storing the recipe KEY
  instead — v19's bespoke remap is gone, folded into a frozen v19-order
  snapshot that translates any pre-v20 index through the key it named at the
  time. `SaveSystem::legacyRecipeIndex` is exposed purely so `--selftest` can
  pin it: it reads a format nothing can write any more, so a regression there
  would mis-lock every old save silently rather than fail.
- Known-by-design: with several recipes, an AUTO Press fed mixed inputs makes
  whichever recipe it can first. Lock a MAKE row, or dedicate a Press per part —
  that division of labour is the intended logistics pressure. Plate is listed
  FIRST so a fresh Press fed the player's only raw makes plates, not rods (both
  cost one ingot; rods lose the tie deliberately).

The Alchemy Circle (the crafting overhaul — hand-crafting moves into the world):
- **The multiblock** — `BlockId::RuneCore` + `BlockId::Pedestal`, two new
  `MachineKind`s. The eight ring cells sit at radius 2 in the compass
  directions (`AlchemyCircle::kRingOffsets`, clockwise from north; the four
  CARDINALS are the EVEN indices) — a 5×5 footprint, roomy enough for belts to
  reach the pedestals from outside. Pedestal count is the tier: the 4 cardinals
  = **Lesser** (runs UNPOWERED, `kLesserCircleSlowdown`× slow), all 8 =
  **Greater** (draws power, full speed, unlocks the 8-slot patterns).
- **Geometry/matching is `AlchemyCircle`** (AlchemyCircle.h/.cpp): free
  functions over `(World&, MachineMap&, corePos)` — the MachineSystem/WorldEdit
  precedent. `tierAt` / `ringContents` / `findMatch` / `craftSeconds` /
  `consume`. A ring cell counts only when it is a Pedestal BLOCK *and* has a
  Machine entity (the two disagree for a frame mid-edit).
- **Recipes are NECKLACES** (`CircleRecipe` in Recipes.h): a `ring` of 4 slots
  (the cardinals) or 8 (Greater only), clockwise, plus an optional centre
  catalyst held in the core's own buffer. `{None, 0}` means the slot must be
  EMPTY — the strongest discriminator. Matching is **rotation-invariant**, so
  where you start laying never matters while per-slot COUNT does: Conduit is two
  plates on ONE pedestal, the Wrench is one plate on each of two OPPOSITE ones;
  same for Copper Pickaxe (wood opposite) vs Axe (wood beside). Slots match on
  "holds at least this many" so belts can top a pattern up — which means one
  pattern can be a superset of another, so **table order matters** (the more
  demanding variant first) exactly like the Press's five recipes.
- **The tick** — `tickRuneCore` in MachineSystem runs BEFORE the power gate, so
  a Lesser circle works on a dead network. Pedestals are passive holders
  (`machineAccepts`: empty, or more of the same item, up to `kPedestalCap`), and
  since they hold items in `input` (not `output`) belts feed them but can never
  drain a laid pattern. Output lands in the core's `output`, so belts drain that.
- **The panel** (`updateCircleUi`/`drawCircleUi`/`circleLayout`) draws the eight
  pedestals at their true compass bearings around the catalyst socket; missing
  pedestals draw as `+` ghosts. RMB on ANY part of the circle opens it —
  `openMachineUi` walks the ring offsets backwards from a pedestal to find its
  core. There is deliberately **no blueprint list**: a pattern is laid BY
  HAND, one drag per pedestal — a recipe you perform rather than a row you
  click — so the panel has two action rows, START/STOP and TAKE OUTPUTS, and
  its height does not grow with the recipe table.
- **A circle runs only once STARTED** (Sep 2026, user request). "Holds at least
  this many" means every half-laid pattern on the way to the one you meant is
  itself a pattern — two ingots on one pedestal, on the road to a Press, spell
  `circle/wire` — and a circle that ran whatever the ring currently said
  crafted the road out from under you. START locks `selectedRecipe` to what the
  ring spells at that moment, and **the lock IS "started"**: `tickRuneCore`
  does nothing while it is −1. The lock was already saved as a recipe KEY
  (left over from the blueprint era), so this cost no save change. It then
  STAYS, so a started circle keeps making that recipe whenever belts complete
  its pattern again and can never drift into a pattern it passes through while
  they refill it — which is what keeps the Circle automatable. Any hand edit to
  the ring (a drag onto or off a pedestal or the catalyst cell) clears it, as
  does STOP. The status line reads READY / MAKING / STARTED -- WAITING FOR ITS
  PATTERN. A belt-fed circle in a pre-Sep-2026 save sits idle until START is
  pressed once. `--selftest` pins the Press-vs-Wire case and the refill, and
  was checked to FAIL with the gate removed.
- **What the ring holds is visible from the world**: `buildCargoMesh` draws each
  pedestal's item (and the core's catalyst) as a bobbing camera-facing icon over
  the block, a fanned stack of up to `kCircleItemStack`, in the same buffer and
  draw as tube cargo.
- **The ritual effect** (`VoxelGameEffects.cpp`, the first user of
  **`ParticleSystem`**). `tickPowered` takes an optional
  `std::vector<CircleCompletion>*` and REPORTS each finished ritual (core,
  product, tier, which slot gave which ingredient — read before `consume`
  empties them) rather than the renderer inferring it from buffer counts, which
  a belt draining the output the same tick would hide. While a started circle
  crafts, its items rise and brighten and motes stream into the core, faster as
  progress fills; at the finish the ingredients rush in, then a flash, a spark
  burst, a ground shockwave ring, a column of light, the product popping up over
  the core, and the generated `ritual` sound. Coloured by tier (Lesser cyan,
  Greater violet — the panel header's colours). Tuned for DAYLIGHT: additive
  light on a bright sky only reads once it saturates. Knobs in
  `// ---- Alchemy ritual effect ----`.
- **`ParticleSystem`** (ParticleSystem.h/.cpp, `shaders/particle.*`) knows
  nothing about circles: a fixed 2048 pool (recycled round-robin, never grows),
  camera-facing quads built on the CPU, drawn **additively with depth test on
  and depth writes off** — order-independent, so no sort (the cutout
  precedent), and a wall still hides a spark. Advanced on the pause-aware dt,
  never saved, never read by the sim, and a shader failure just disables it.
  Drawn LAST of the world passes so everything that should hide it is already
  in the depth buffer. Ready for generator smoke, mining debris and boss hits.
- **The hand menu is now a survival tier**: 13 rows (tool ramp, Stone, Ingot,
  Glass, Vial, Bucket, Scaffold + the circle's own two parts, which MUST stay
  hand-craftable or the tree deadlocks). 26 recipes moved to the Circle.
- Append-only blocks/items and generic machine save records mean **no save
  version bump**. `--selftest` covers tier detection, arrangement disambiguation,
  rotation invariance, the Greater power gate, `consume`, the
  registry-vs-world disagreement case, and START (above).

The recipe overhaul (keys, the manual tier, and iron — July 2026):
- **Recipes are keyed.** `Machine::selectedRecipe` used to be a saved INDEX
  into `recipesForMachine()` order, which is what made all three tables
  append-only and what the v19 migration existed for. Every row now owns a
  stable `const char* key` and the save stores THAT (save **v20**, a
  length-prefixed string in the machine record). The tables are consequently
  free: reorder, retime, rebalance, delete. A lock on a deleted recipe resolves
  to AUTO — the honest answer, and the one thing an index could never give.
  Pre-v20 saves are migrated through a **frozen v19 order snapshot** in
  SaveSystem.cpp: history, not content, so it must never be edited to track the
  live tables.
- **The guardrails moved from ordering to meaning.** `--selftest` now proves
  (a) keys are unique and round-trip, (b) every circle pattern, laid exactly,
  matches ITSELF — the "holds at least this many" rule lets one pattern shadow
  another, and this is what catches it, (c) a **tech-tree reachability
  closure**: from world drops alone, every machine must be buildable and every
  recipe input obtainable. Edit a recipe into a deadlock and the build fails.
  It replaced an ad-hoc "Copper Plate has one producer" check that only knew
  about one deadlock.
- **`RECIPES.md` is generated** — `voxel-factory --dump-recipes > RECIPES.md`.
  It used to be a hand-maintained mirror carrying an "update both together"
  warning, which is a promise a repo cannot keep.
- **The manual tier is thirteen data rows plus one branch.** One twin per
  Processor: the Bloomery (fire-driven — see below) plus twelve hand-cranked
  (Sieve, Mortar, Hand Press, Anvil, Blowpipe, Tamper, Compost Heap,
  Mixing Bowl, Infusion Stand, Still, Hand Distiller, Hand Transmuter). Each is a kBlocks row, a kItems row, a `kMachineTraits` row and a
  build recipe; **no new `MachineKind`**. `recipeGroup` points at the powered
  twin so `recipesForMachine()` returns its rows (a recipe stays authored
  exactly once, and a static_assert keeps the delegation one hop), `speedMult`
  (`kManualSlowdown` = 3) is the price, and `demand = 0` bypasses the power gate
  at `tickPowered`'s one `traits.demand > 0 &&` and keeps them off the power
  graph via `isPowerNode` — the Rain Barrel precedent.
- **The Bloomery is the one manual-tier machine that is NOT cranked** (Aug
  2026, user decision). `recipeGroup` and `handCranked` used to be tied by an
  equivalence — cranked *iff* delegating — which silently forced a hand-turned
  fire, and a lit bloomery full of ore did nothing at all unless somebody stood
  there pressing arrows. The two questions are genuinely separate:
  `recipeGroup` asks WHOSE RECIPES do I run, `handCranked` asks WHO SUPPLIES
  THE WORK, and a bloomery answers "the Furnace's" and "the fire". So the
  invariant is now one-directional (cranked ⇒ delegates, not the reverse) in
  BOTH the `static_assert` and `content::validate()`, and the Bloomery runs on
  the clock like every other machine. It still pays its manual-tier dues in the
  other two currencies — `kManualSlowdown` times as long as a Furnace, on fuel
  it wastes (`fuelMult = 0.6`) — so the tier gap is intact while the crank tier
  now means one coherent thing (a machine your ARM drives) instead of two.
  Its panel is a Furnace's — no dial, W/S drives the rows, the header reads
  BURNING/OUT OF FUEL, and it has the same master switch as everything else
  (see above, which this change is what prompted). `--selftest` pins that it runs unattended,
  that it is slower than the Furnace over the same ticks, and that it stops
  when the fuel does; the crank test moved to a Mortar.
- **Every machine has a master switch** (`Machine::enabled`, save **v24**, Aug
  2026, user request). **OFF means FROZEN, not broken**: no work, no power drawn
  or produced, dark — but every buffer, the progress, and the recipe lock are
  kept, so switching back on resumes mid-craft. It is asked ONCE, at the top of
  `tickPowered` and `tickSelfPowered`, before power/fuel/recipes/the crank, so
  "off" means the same thing for every `MachineKind` instead of being
  re-implemented per branch.
  Three things it deliberately does NOT do. It does not stop the machine
  **conducting** — `isPowerNode` is by block id and the flood fill runs on
  those, so idling a machine can never split a network and black out everything
  downstream. It does not stop the machine **accepting** deliveries or giving
  up its output, which is what makes it a logistics tool rather than a wall: an
  idled machine fills to its input cap and the feed line backs up from there on
  its own, with no special case in `beltStep`. And it is not a per-kind opt-in —
  only a Pedestal lacks one (`hasPowerSwitch`), because its tick is already a
  `continue` and a dead control teaches players the live ones might be dead too.
  In `PowerSystem::solve` an off machine contributes no `demand` and no
  `powerOutput` and is skipped when the energized set is filled — that last part
  is the whole of "it looks switched off", because the energized set already
  drives both the emissive glow and the shape animation, so no mesher change was
  needed. The panel row is **LAST** (`switchRow`), so adding it shifted no
  recipe row's index; the row says what pressing it DOES (TURN OFF / TURN ON)
  while the header says the state (OFF outranks even a jam), and the look-at
  tooltip reads OFF so an idled machine isn't confused with a starved one.
  Toggling re-solves power (`solvePowerAndMarkDirty`) — required, not tidy.
  `--selftest` pins the freeze, that it is a pause and not a reset, that belts
  still fill and drain it, that an off consumer leaves the wire live, that an
  off consumer stops making its generator hungry, that an off generator darkens
  what it fed, and the v24 save round-trip (one machine on, one off).
- **Cranking is what makes the tier manual rather than merely slow** (Aug 2026).
  It shipped as pure data — a 3× wall-clock stretch — which meant a Mortar ran
  itself overnight and the powered tier sold nothing but speed. A `handCranked`
  traits flag (static_asserted to IMPLY the manual tier — a handle needs a
  recipe list; the reverse does not hold, see the Bloomery above) makes
  `tickPowered` advance those machines by
  `Machine::crankBanked` instead of `kTickSeconds`. The bank is filled by the
  **panel**: with it open, the four ARROWS pressed in order (`vg::kCrankOrder`,
  clockwise from up) turn the handle, and a completed rotation banks
  `kCrankProgress` seconds. A wrong key resets the turn. `crankStep`/`crankBanked`
  are transient, so a half-turn is not saved and needs no version bump; W/S keep
  the rows via `menuNav`'s new `useArrows` flag, so the two never fight. The tick
  bails BEFORE spending the bank when a cranked machine's fire is out, so the
  turn is owed rather than swallowed — and fuel burns only on a tick that banked
  a crank, so an unattended cranked burner costs nothing. (No cranked machine
  burns fuel any more, now that the Bloomery has left the set, but the rule
  stands for the next one.) **Belts still load a cranked machine but can never
  run one**: those are genuinely un-automatable, and what the powered tier sells
  is not speed but not having to be there. Covered by `--selftest` (400 ticks of
  a loaded Mortar must produce nothing).
- **Fuel is a registry.** `kFuels` (Stick 5s, Sapling 5s, Wood 20s, **Bio
  Briquette 35s**, Charcoal 60s) replaced the Generator's single
  `.fuel`/`.burnSeconds` pair, so a better fuel is one row — which is exactly
  what the briquette turned out to be. It is the rung between wood and charcoal
  and the reason a Composter is worth building before you own a fire: it turns
  the tree's leftovers into fuel without spending the wood you want for
  building. (`kFuels` is listed in burn order for readability only — `pickFuel`
  scans by `seconds`, not by position.) A Processor with `burnsFuel` runs on heat instead of
  electricity (the Furnace and Bloomery; `Machine::burnLeft`, appended in v20).
  It lights fuel only when a craft is ready — the generator's "hungry" rule
  applied to a recipe — and burns the SHORTEST fuel first.
- **Fuel has its own buffer** (`Machine::fuel`, save v21). A machine that burns
  fuel *and* has recipes gets a third belt-reachable buffer and a FUEL strip in
  its panel — that is `usesFuelSlot()`, **derived** (`burnsFuel && has recipes`)
  rather than a hand-set flag, so a future fuel-fired Processor earns a slot by
  existing and a future generator tier stays slotless. A Generator has no recipes
  and so no ambiguity; it still burns out of `input`, via `fuelBuffer()`. This
  **retired** the old rule that *a machine never burns an item its own recipes
  consume* — that existed only because one buffer had to serve two jobs, and it
  cost real behaviour: a Furnace can now char wood while burning wood. The
  inference survives in exactly one place, `bufferFor()`, because a belt has no
  hands and must guess which pile an arriving item joins (ingredient wins). A
  hand-drag lands in the cell you dropped on and infers nothing — which is why
  the FUEL strip needed its own `Drag::Source`, or a cancelled drag would return
  charcoal to `input`. Pre-v21 saves migrate on read (fuel swept out of `input`
  using the retired rule, once, at the boundary).
- The Furnace's traits row was **missing `.demand = 0`** despite the comment
  above it and this file both saying it had one, so it silently required
  electricity *and* fuel — defeating the point of the fuel-fired tier. Fixed
  Aug 2026; the crank selftest is what caught it.
- **Weighted outputs.** `MachineRecipe::output` became
  `std::vector<RecipeOutput>` (stack + weight); one entry is the ordinary
  deterministic case and doesn't touch the RNG at all. More than one makes the
  craft a roll, which is what a Sifter has to be. Randomness rides the existing
  `hash2(...)` + saved `m_sourceRng` counter (the node-growth/weather
  precedent), passed into `tickPowered`, so a sifting line replays identically
  across a save/load.
- **Four new machines.** **Furnace** (fuel-fired: ore/nuggets → ingots, sand →
  glass, wood → charcoal), **Sifter** (sand → a weighted roll of nuggets),
  **Glassblower** (glass → vials; the machine the glass Conduit/tube and
  windows will land on), **Compactor** (dirt + sand → stone). Between them they
  own what used to be four free clicks in the hand menu.
- **Iron does two jobs.** It is the STRUCTURAL metal — Casing, Gear, and so the
  Machine Frame are iron, while copper keeps electricity (Wire, Etched Plate) —
  *and* the tool/armor tier above copper (`kTierIron`, gating the Resonant
  Node). It enters the game **only** through sifting sand, so the Sifter never
  becomes a curiosity: sand is renewable (Sand Source, Grinder Stone → Sand)
  but rate-limited, which makes sand throughput the ceiling on how fast the
  factory can build more factory.
- **The bootstrap.** Smelting, glass, vials and dirt+sand→stone left the hand
  menu, so two machines had to stay hand-craftable or the tree deadlocks (the
  Rune Core costs ingots, and an ingot now costs a fire): **Bloomery**
  (Stone ×8) and **Sieve** (Wood ×4 + Twine ×2 — a sieve is a MESH, so it costs
  twine rather than loose sticks). Ladder: fiber + sticks + pebbles → twine →
  wood/stone tools → Bloomery + Sieve → ingots and iron → hand-craft the Circle
  → the manual tier → the powered tier. The reachability check pins all of it.
- **Charcoal is 1 wood, not 2, and the arithmetic is the reason** (Aug 2026). At
  2:1 a BLOOMERY charring wood was a net energy LOSS: `kManualSlowdown` stretches
  the craft to 18 s and `fuelMult` 0.6 shortens a wood to 12 s, so it spent 1.5
  wood burning plus 2 wood charring — 70 burn-seconds in for 60 out. At 1:1 both
  tiers are positive (Furnace 2.3×, Bloomery 1.2×) and the tier gap survives
  where it belongs, in `speedMult`. Retune this ratio and check the BLOOMERY, not
  the Furnace.
- **Wire is drawn from a rod**, which retired the Copper Rod's long spell as a
  part with no consumer. `press/copper-wire` turns an ingot into two rods into
  four wire, against `circle/wire`'s ingot → two by hand: automating wire is
  worth twice doing it yourself, which is the bargain every machine tier here is
  supposed to offer. Listed after the frame so a Press holding ingots still makes
  plates first, and before `press/copper-rod` so a Press holding rods drains them
  into wire rather than sitting on them.
- **Weapons are data.** `ItemInfo::weaponDamage` (0 = not a weapon) replaced
  the two hardcoded `held == ItemId::CopperSword` tests, so the Iron Sword is a
  registry row and the old `kSwordDamage` knob is gone.

The hard start (Aug 2026 — the primitive tier, and the ruin that stopped
undercutting it):
- **The problem was that the empty starting kit was a fiction.** A working demo
  line stood on the spawn plateau: every block of it is hardness 0.5 and ungated
  (so your own machines stay retrievable by hand) and `breakBlock` hands back
  every buffered item — so a bare-handed player five blocks from spawn collected
  16 Wood, 20 Herb, seven machines and a Herb Source. Sixteen wood alone is a
  Bucket, a Sieve and a Crate without ever owning an axe: the tree, the tool
  ladder and the Bloomery all skipped, in the first minute.
- **The ruin is the same object, sited instead of placed.** As a TEACHING object
  the line works, so the layout is unchanged — generator → wire → grinder →
  conduits → cauldron, a rain barrel plumbed in, a mining bay south of it.
  What changed is where it stands and how much it gives away. `plantRuin` was
  split out of `buildWorld` precisely so the thing could be sited, and it is
  stocked to **limp**: `kRuinFuelWood` (2) and `kRuinHerb` (4), enough to still
  be running when you find it and to die while you watch. That teaches what it
  needs far better than either a dead ruin or a full one. Its Herb Source came
  out with it — one five blocks from spawn contradicted the whole rule that
  sources sit past `kSourceMinRadius` so that reaching them is the problem.
- **Siting is a search, and it may come up empty.** 400 seeded attempts in the
  `kRuinMinRadius`..`kRuinMaxRadius` band (22–30, the same band as the sources,
  because that is where the resources are and so where a factory would have been
  built). The outer bound only saves wasted attempts; what actually keeps the pad
  on land is a **per-column test** over the whole footprint plus a one-cell
  margin, since the coastline wobbles by ±7 and a radius alone cannot tell a
  headland from a bay. A ruin is a BUILT thing, so its builders are assumed to
  have levelled the site: every column must find land within `kRuinLevelSlack`
  of the anchor, and the pad is then cut down and filled up to it. Both bounds
  are driven by that same slack, which is what makes the levelling safe in either
  direction — hence the `static_assert` that `kRuinHeadroom > kRuinLevelSlack`.
  Failure is **never fatal and never a retry loop**: worldgen has to finish, and
  a ruinless island is playable (it was never a kit), just quieter.
  Measured at **12/12 across fresh worlds**, with the sites clustering hard at
  the inner bound (six of ten at radius 22–23) — the outer band rarely satisfies
  the land+level test, so `kRuinMaxRadius` is close to decorative in practice.
- Stamped BEFORE the source scatter, so the scatter's existing "grass with air
  above" test declines to land inside it without having to be told it exists.
- **The primitive tier is what the plateau leaves you with.** Turf pulls apart
  into **Plant Fiber** and topsoil turns up a **Pebble**, both EVERY time, and
  three fiber twist into **Twine** — the binding every wood and stone tool needs,
  and so the true step one, before any tool at all. A sieve is a MESH, so it
  costs twine too.
- **They used to share one 25% roll, and that was two mistakes at once.** It made
  Grass and Dirt the same resource — no reason to dig one over the other — and it
  put a coin flip on the FIRST thing a new game asks you to collect: two wood
  tools cost six pebbles, so ~24 blocks of dirt-punching before the game started.
  Now turf is where binding comes from and topsoil is where stone starts, which
  is two verbs. Leaves keep a roll because a leaf yields two different things.
- **None of it can be a `BlockDrop` row.** Grass and Dirt already spend theirs on
  GrassItem/DirtItem and `BlockDrop` is a single stack, so fiber, pebbles and
  sticks are spawned BESIDE the drop in VoxelGamePlayer's break path. They are
  CODE, not data — which is exactly why `content::validate()`'s closures have to
  name those items by hand, and why that hand-naming is not an oversight.
- **`hand/pebble-stone` (Pebble ×4 → Stone) is load-bearing in two opposite
  directions**, which is the interesting part. It is the only producer of Stone
  that does not COST Stone (compactor/stone needs a Compactor or Tamper; the
  Tamper, the Bloomery and the Circle all cost Stone), so without it the whole
  tech tree rests on whatever the island happened to bury — that is what the
  renewability closure is asking about. But Stone is gated at `kTierWood` and
  this is a HAND recipe, so it is also a way past that gate with no pickaxe,
  harmless only while it stays the SLOWER road. At 4 it is: four bare-handed digs
  is 3 s of Dirt at hardness 0.75, against 1 s for a Wood Pickaxe (miningSpeed 4)
  on Stone at hardness 4 — and that pickaxe costs three pebbles, fewer than one
  Stone does here. Neither closure would notice it being cheapened, since both
  only get GREENER as it gets cheaper, so `--selftest` pins the ordering directly
  ("the pebble route must stay a fallback").

Persistence:
- **Save/load** (`SaveSystem.*`): versioned binary (`save.vxf` in the SDL pref dir —
  `%APPDATA%\BennyThompson\voxel-factory\`; `kOrgName` is a placeholder studio name,
  and a legacy save under `benny\` is migrated on first launch). Writes are atomic:
  save to `.tmp`, rotate the old file to `.bak`, rename in; load falls back to `.bak`
  before regenerating. The file holds seed, all chunks, player camera/inventory/
  slot, machines (type/buffers/recipe/progress — generators/barrels ride along),
  belts (facing/cargo), source + sapling timers, weather state, player
  health (appended in v10; v9 saves still load with full-health default), the
  hotbar slot assignments (appended in v12; older saves keep `vg::kDefaultHotbar`),
  and the Overworld ground-item drops (pos/id/count, appended in v16; older saves
  load with an empty list).
  Auto-load on launch (fresh island if absent/invalid), auto-save on every quit path via
  the engine's `onExit()` hook, F5 quick-saves. Bump `kVersion` whenever enums or layout
  change — old saves are then discarded rather than misread. Exception: a bump that only
  APPENDS trailing fields may keep older versions loadable (`kOldestLoadable`; the
  caller's defaults survive), as v9→v10 did for health, v11→v12 for the hotbar,
  v14→v15 for playtime, and v15→v16 for ground drops — any enum/layout change must
  drop that compatibility. The other way to keep it is to MIGRATE on read, which
  v19, v20 and v21 all do. v19 remapped `selectedRecipe` indices after two
  recipe lists were reordered. **v20 changed the machine record itself** — the
  lock is a length-prefixed KEY now, plus a trailing `burnLeft` float — and
  reads the old i32 when `version < 20`, translating it through a frozen v19
  order snapshot (`SaveSystem::legacyRecipeIndex`, `--selftest`-pinned). That is
  why editing the recipe tables no longer needs a version bump at all: the
  format stopped depending on their order. **v21 appends the fuel buffer** to
  each machine record — a tail append, so `kOldestLoadable` did not move, but
  one that still needs a migration: an older save keeps its fuel in `input`, so
  the read path sweeps it across for machines with a slot, or every existing
  Furnace would load stone cold. **v22 ended the need to bump for an enum
  change at all**: the file now carries the block and item KEY TABLES its
  ordinals refer to, so content that moved is translated instead of misread
  (see **Content identity** above). It also widened both ids on disk from one
  byte to two, which roughly DOUBLES a save (223 KB → 463 KB on a real world) —
  irrelevant at the current 6×6-chunk cap, and the fix if the world ever grows
  is a per-chunk palette, which would land it below the original. Pre-v22 saves
  migrate on read (identity map + 1-byte ids); v14/v18/v21 real saves were
  verified to load and re-save losslessly. The Aug 2026 content batch (the
  primitive tier, compost, grafted saplings) changed the format NOT AT ALL --
  new blocks and items ride v22's key tables, the sapling registry is still a
  plain `pos -> float`, and the retired `m_lootRng` was transient -- and a real
  399 KB **v25** save was checked the same way: load -> re-save -> reload, three
  generations, identical size and an identical world each time.

Commercial shell (main menu + save slots + logging/crash dumps — July 2026):
- **Main menu on launch** — the game boots into a NEW GAME / CONTINUE / SETTINGS /
  QUIT shell (`m_shellOpen`; `openMainMenu`/`updateMainMenu`/`drawMainMenu` in
  VoxelGameUi.cpp) over an *unbuilt* world (bare sky backdrop; nothing simulates —
  `setPaused(true)`). `onStart` no longer auto-loads; choosing a slot runs
  `startNewGame(slot)` (fresh island + kit) or `continueGame(slot)` (load), both
  ending in the shared `startPlaying()` tail (seed power/hums, spawn the creature,
  unpause, recapture the mouse). CONTINUE greys out when no slot has a save. Built
  on the pause-menu pattern (`beginPanel`/`menuNav`/`drawSimpleRow`); dispatched at
  the same three overlay points (onUpdate/onRender/onEscape). Settings is decoupled
  from `m_pauseOpen` so it opens from the menu too. `onExit` skips saving while
  `m_shellOpen` (never overwrite a slot with the empty backdrop).
- **Three save slots** — `save_<n>.vxf` per slot (`saveSlotPath`, `m_saveSlot` —
  distinct from the hotbar's `m_selectedSlot`); `kSaveSlots` in VoxelGameInternal.h.
  The **slot picker** (`openSlotPicker`/`updateSlotPicker`/`drawSlotPicker`, reached
  from NEW GAME to create/overwrite or CONTINUE to load) draws per-slot cards from a
  `SlotMeta` **sidecar** (`<save>.meta`; `SaveSystem::readMeta`/`metaPath`) so it
  lists playtime + timestamp + boss progress WITHOUT loading (or version-gating) the
  full save. Overwrite (ENTER-again) and RMB delete are two-step confirms
  (`m_slotConfirm`). Legacy single `save.vxf` is copy-migrated to `save_0.vxf` on
  first launch (`migrateLegacySave`, now two hops: old org → `save.vxf` → slot 0).
  Playtime (`m_playtime`, accrued in onUpdate only while `!paused()`) rides the save
  (v15 append) and the sidecar.
- **Logging + crash dumps** (`engine/Log.*`, `engine/CrashHandler.*`,
  `engine/Paths.*`) — `Log::init` installs an `SDL_SetLogOutputFunction` hook that
  tees every existing `SDL_Log` to a rotating `logs/game.log` (3 generations) — no
  call-site changes. `CrashHandler::install` writes a Windows minidump
  (`crashes/crash-<ts>.dmp` via `MiniDumpWriteDump`/DbgHelp — `dbghelp` linked in
  engine/CMakeLists.txt under `WIN32`) or, on POSIX, an async-signal-safe
  `backtrace()` text dump then re-raises. Both installed at the top of `main()`
  using `engine::prefDir(kOrgName, kAppName)` (the same pref dir as the saves).
