# Scalability Audit

*July 2026. Scope: content scalability (cost of adding blocks/items/machines/recipes)
and code architecture (the `VoxelGame` class). Findings first, recommended refactor
sequence at the end — each step there is independently green-lightable; nothing is
committed by this document.*

*Out of scope by design: the performance architecture (dirty-driven meshing,
edit-only power solves) is deliberate and measured — don't touch it. At ~8k first-party
lines this is a structured cleanup target, not a rewrite candidate.*

---

## Part 1 — Content scalability: "half data-driven"

Everything is keyed off central `enum class BlockId` / `ItemId`, whose ordinal is
**both** the index into parallel positional registries **and** the on-disk save
encoding. Real data tables exist (`Block.cpp`, `Item.cpp`, `Atlas.cpp`, `Recipes.cpp`),
but they stay aligned with enum order by comment only, and they're surrounded by
hand-written switch/if ladders that must be extended in lockstep. Nothing fails to
compile when a site is missed — the defaults swallow it.

### Cost of adding one of each, today

**Recipe — best in class.** One brace-initialized row in `kRecipes` /
`kMachineRecipes` (`game/src/Recipes.cpp:10-70`). The crafting menu, machine panel,
and sim all iterate the tables; nothing else changes. One caveat:
`Machine::selectedRecipe` is a **saved index** into `kMachineRecipes`
(`SaveSystem.cpp:106,216`), so recipe rows are append-only — reordering silently
repoints saved machine selections.

**Plain block / material item — near-data-driven.** Enum append (append-only — the
ordinal is the save byte) + a positional `BlockInfo` / `ItemInfo` / `BlockTiles` row
+ cases in the `blockName()` and `blockDrop()` switches. The mesher and all
inventory/crafting/hotbar UI are fully generic. The switches have silent defaults:
a forgotten block compiles fine, names itself `?`, and drops nothing.

**Machine with custom behavior — the cliff.** A "standard recipe machine"
(Cauldron-style) is cheap: the block checklist + `isMachine()` + `machineAccepts()`
+ power entries, and the generic tick/panel handle the rest. But each machine with
bespoke behavior (Generator, Rain Barrel, Miner) spreads across **~14
compiler-unchecked special-case sites**:

- 6 branches in the 20 Hz tick — `VoxelGameSim.cpp:28,41,114,414,420`; the Miner is
  a ~58-line inline special case wedged before the generic recipe engine.
- 5 in the panel UI — `VoxelGameUi.cpp:213-265`: status-string and action-row
  if/else ladders on `mac.type`, plus the brittle `recipes.empty()` proxy for
  "recipe-less machine".
- 3 in helpers — `machineAccepts` (`VoxelGameInternal.h:195-211`),
  `isPowerNode`/`demand` (`PowerSystem.cpp:21-32`), `isMachine` (`Block.cpp:45-51`).

The Rain Barrel — a machine that ignores power — must be *explicitly excluded* in
three separate places to stay out of the power system.

### Hard-coded hotspots, ranked by silent-failure risk

1. **`Block.cpp:53-59`** — `isSource()` / `isResourceNode()` are enum-**range**
   checks (`id >= X && id <= Y`). Combined with the positional tables, inserting a
   block mid-enum silently misclassifies blocks and misaligns every registry — and
   corrupts saves, since block bytes and inventory slots are raw ordinals
   (`SaveSystem.cpp:41-56,93`). Wrong content placement is a data-corruption
   hazard, not a build error.
2. **`VoxelGameSim.cpp:414-478`** — the onTick machine loop. Every non-recipe
   machine grows this function.
3. **`VoxelGameUi.cpp:213-265`** — machine "personality" ladder in the panel.
4. **`Item.cpp:75-125`** — `blockDrop()` / `nodeForRaw()`: large switches whose
   defaults return nothing, so new resources are un-mineable until both are
   manually extended.
5. **`machineAccepts` + the power ladders** — three separate `== BlockId::X` chains
   that must agree with each other and with `isMachine()`.

---

## Part 2 — Architecture: one class, ~60 members, 13 concerns

`VoxelGame` (`game/include/game/VoxelGame.h`) holds ~60 data members spanning render
handles, world/power, inventory/hotbar, machine/belt/growth registries, entities,
audio handles, weather, save meta, and five clusters of UI state. The per-concern
`.cpp` split is organizational only — every file sees the entire private surface.

### Worst couplings

- **UI mutates sim state directly.** `updateMachineUi` (`VoxelGameUi.cpp:397-532`)
  rewrites `Machine::selectedRecipe` / `progress` and moves items between
  `m_inventory` and machine buffers. The UI layer is the machine sim's editor, with
  no API between them.
- **The player edit path is the central knot.** One branch of `onUpdate`
  (`VoxelGamePlayer.cpp:297-375`) touches inventory, machine/belt/source/sapling
  registration, the power re-solve, audio, and the world. Any extraction of those
  systems has to pass through this function.
- **`solvePowerAndMarkDirty`** (`VoxelGameSim.cpp:84-97`) fuses three concerns:
  power solve → chunk dirtying → audio hum-loop lifecycle (`updateHums`
  creates/destroys loops through `audio()`).
- **`SaveData`** (`SaveSystem.h:19-41`) is a 17-field reference bundle that pins
  VoxelGame's exact member layout; every ownership change must migrate it in
  lockstep (append-only / `kOldestLoadable` rules).
- **`VoxelGameUi.cpp` is 1365 lines of hand-rolled widgets.** The same list-menu
  idiom (W/S wrap-nav + hover-picks-row + click/Enter activate + click sound) is
  duplicated ~4× (`:418`, `:565`, `:1007`, `:1134`); the item-grid draw loop 3×
  (`:322`, `:681`, `:913`). Layout is recomputed independently in update and draw
  (e.g. `panelLayout` at `:204` and `:412`) and silently disagrees if the inputs
  ever diverge.

### What's already healthy — the template to follow

`PowerSystem`, `SaveSystem`, `Recipes`, `Settings`, and `ChunkMesher` are extracted
as free functions / small value types taking `World&` + plain maps — never
`VoxelGame&`. `Machine` / `Belt` are plain data structs. `PowerSystem::solve` already
takes the machines map by reference: that's the interface precedent for extracting
the rest of the sim. The engine/game boundary is clean (hooks + protected accessors
only), with one noted leak: the game issues raw GL itself
(`VoxelGameRender.cpp:250-306`) — acceptable for now.

### Explicitly not problems

- `VoxelGameInternal.h` holds no mutable global state — all `inline constexpr`
  constants and pure helpers. It's a catch-all, not a hazard. Only
  `machineAccepts` / `minerFilter` (gameplay *rules*, not tuning) are misplaced.
- The seven-file `VoxelGame` split, panel-layout factories ("one source of truth
  for hit-test and draw"), and the data-table style of `Recipes.cpp` are all worth
  keeping and extending.

---

## Part 3 — Recommended refactor sequence

Each step leaves the game playable, saves loadable, and `--selftest` green. None of
them change the save format (enum ordinals stay put). Green-light individually.

### Content track

- **C1 — Compiler-enforced registries.** ✅ **Done (July 2026).** The positional
  arrays, enum-range checks, and name/drop switches are now designated tables in
  enum order, `static_assert`ed against `Count` (a missing/misplaced row is a
  compile error). `isMachine` / `isSource` / `isResourceNode` are flags on
  `BlockInfo`; `blockName`, `blockDrop`, `sourceSpawnsNode`, and the atlas tiles
  are `BlockInfo` fields (the separate Atlas table is gone); `nodeForRaw` is an
  `ItemInfo` field. Adding a plain block or item is now: append the enum value +
  one registry row (+ icon art). Killed hotspots 1 and 4 and the `isMachine`
  ladder. No save-format change.
- **C2 — Machine traits table.** ✅ **Done (July 2026).** `kMachineTraits` in
  Machine.h: one row per machine type (`MachineKind` Processor/Generator/
  Collector/Miner + demand, power output, fuel + burn time, collected item +
  cap/cadence), cross-`static_assert`ed in Block.cpp against the `BlockInfo`
  machine flags. The sim tick, `machineAccepts`, the power solve, and the panel
  header/row-0 each dispatch on the kind exactly once; the bespoke ticks are
  named functions (`tickGenerator`/`tickCollector`/`tickMiner` in
  VoxelGameSim.cpp). The Generator/Barrel knobs moved into their rows —
  per-machine data (a higher-tier generator is now just another row). A new
  standard recipe machine is pure data; killed hotspots 2, 3, and 5. No
  save-format change.

### Architecture track (ordered by cost/value)

- **A1 — UI toolkit.** ✅ **Done (July 2026).** File-local widgets in
  VoxelGameUi.cpp's anonymous namespace: `menuNav()` (the one implementation of
  W/S + arrows wrap-nav, wheel, hover-picks-row, Enter/LMB activation — used by
  the machine panel, crafting menu, pause menu, and settings/keybinds),
  `beginPanel()` (dim + slab + title framing), `drawItemGrid()` /
  `hoveredItemIn()` (grids, strips, tooltips), `drawSimpleRow()` and a shared
  panel palette. Net −39 lines while adding the toolkit; a new panel now costs
  a layout struct + a row switch instead of re-rolling the idiom.
- **A2 — CreatureSystem.** ✅ **Done (July 2026).** `CreatureSystem`
  (game/include/game/CreatureSystem.h + game/src/CreatureSystem.cpp) owns the
  Creature struct, model/GPU assets, instances, and the render-lerp clock;
  `VoxelGameEntities.cpp` is gone and VoxelGame keeps a single member. API
  follows the PowerSystem precedent — `update(World&)`,
  `render(Camera&, rainDim)`, `tryMeleeAttack(World&, Audio&, origin, dir)` —
  engine services passed in, never `VoxelGame&`. Eight members + a nested
  struct left the god class.
- **A3 — MachineSystem / BeltSim.** Move the onTick machine loop and `beltStep`
  into a system taking `(World&, machines, belts, PowerState&)`, following the
  `PowerSystem::solve` precedent. Prerequisite: unfuse `solvePowerAndMarkDirty`
  (return a changed-set; the caller does dirtying and hums) and decide hum-loop
  ownership. Best done *after* C2, which shrinks what has to move.
- **A4 — Weather module.** Small, cohesive (`m_weather*`, `m_rainIntensity`,
  `m_bucketFill`); the rain mesh stays render-side.

### Deferred (prerequisites missing)

- **PlayerController extraction** — needs a `WorldEdit` service that owns
  place/break side effects (registration + power re-solve + sounds) first;
  until then the edit path pins everything together.
- **SaveData reshaping** — migrates in lockstep with each ownership extraction;
  not a standalone step.
- **Engine Renderer abstraction** (game currently issues raw GL) — real but low
  urgency; revisit if a second rendering backend or render-graph need appears.
