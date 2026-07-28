# Testing Voxel Factory

A manual test script that exercises the whole game loop
(mine → craft → power → process → transport → weather → combat). Each step lists
an action and the result to expect.

## Controls quick-reference

| Action | Input |
|---|---|
| Look | Mouse |
| Walk | **W A S D** (no flying!) |
| Jump | **Space** |
| Sprint | hold **Left Ctrl** |
| Mine block / swing sword | **LMB** (a held sword swings first; a miss mines) |
| Place item / open machine / drink draught | **RMB** |
| Select hotbar slot | **1**–**9**, **0** (first ten) · **mouse wheel** cycles all |
| Open/close crafting menu | **E** |
| Help screen | **F1** (F1/Esc closes) |
| Perf overlay (dev) | **F3** |
| Force rain on/off (dev) | **F4** |
| Quick-save | **F5** (quitting also auto-saves) |
| Re-aim a conduit | **R** while aiming at it (requires a crafted Wrench) |
| Menu: select / craft | **W/S** (or ↑/↓) / **Enter** |
| Open a machine's panel | **RMB** on it (Shift+RMB places against it instead) |
| In the panel: choose / act / close | hover or **W/S** · click or **Enter** · **Esc/E/RMB** |
| Close menu / pause | **Esc** (with nothing open, Esc opens the **pause menu**) |
| Quit | pause menu → **SAVE AND QUIT** (or close the window; both save) |

**Keybinds note:** movement, jump, sprint, craft menu, inventory, wrench,
quick-save, and help are the **defaults** — all rebindable in the pause menu's
SETTINGS → KEYBINDS. Esc, mouse buttons, hotbar digits, menu navigation, and
the F3/F4 dev keys are fixed.

**Observability note:** the hotbar is ten player-assigned slots (fresh games
seed the ten machine placeables; assign anything else via the **Tab** inventory
overlay). Raw-material counts are on the Tab overlay's grid, the crafting menu
(a recipe row turns white when you can afford it, and shows `HAVE n`), or a
machine panel's INVENTORY grid. The window title always names the held item and
its count.

## Build & launch

1. From `voxel-factory/`, load the MSVC environment and build (the **first** build compiles
   SDL3 from source — expect a few minutes; later builds are fast):
   ```powershell
   & "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat"
   cmake --build out/build/x64-Debug
   ```
   On macOS (universal, 10.15+): `cmake --preset mac-debug && cmake --build --preset
   mac-debug`, then run `out/build/mac-Debug/bin/voxel-factory` (saves land in
   `~/Library/Application Support/BennyThompson/voxel-factory/`).
2. Run `out/build/x64-Debug/bin/voxel-factory.exe`.
   **Expect:** a window titled `Voxel Factory v<version>  —  Holding: Conduit x0 …`,
   a centered crosshair, a hotbar along the bottom (all counts 0), and a row of ten
   hearts above the hotbar's left end. You spawn on a grassy plateau at the center of a
   **floating island** in a blue sky, with **one grown tree** somewhere on the plateau.
   No console errors. The island layout is randomized each launch.

## 1. Smoke test — watch the demo run (no input)

- Look at the structure ahead: **generator** (orange) → glowing **wire** (yellow) →
  **grinder** (gray) → three dark **conduits** → **cauldron** (brass). Wait ~10 seconds.
  **Expect:** a green progress bar floats above the grinder and fills repeatedly; every
  couple of seconds a small item icon (ground herb) appears on the conduits and travels
  into the cauldron. Standing near the line you hear a **machine hum**. This one view
  exercises power + fuel + machine processing + transport. (Demo generators come
  preloaded with Wood; a generator's own bar is its **fuel gauge** — burn time left on
  the current piece.)
- Turn around toward the plateau's south side: a second demo — glowing **herb source** →
  **miner** (gray) + **generator** (orange) → two conduits.
  **Expect:** the miner harvests nodes that grow around the source (watch one vanish and
  its progress bar fill, one node per ~4 s within radius 4) and herb rides its conduits —
  fully automated mining, bounded by the patch's regrowth. Right-click the miner:
  its panel reads `MINES NEARBY RESOURCE NODES ( RADIUS 4 )` and `OUT:` accumulates herb.
- Walk toward the coast (mind the edge — falling off costs your whole pack) and look
  around. **Expect:** a roughly circular island with an irregular coastline floating in
  open sky. The resource-node **clusters** sit in the island's **outer band**, away from
  the plateau — each cluster grows around one brighter, **glowing source block**.
  Logistics distance is deliberate.

## 2. Camera & movement (walking physics, fall damage)

- Move the mouse to look around; **WASD** walks, **Space** jumps (~1.3 blocks),
  **Left Ctrl** sprints. **Expect:** gravity holds you to the ground, one-block walls
  stop you, and you can jump onto single blocks. There is **no flying** — build with
  cheap **Scaffold** (Stone ×1 → Scaffold ×4) to climb; mine the block under your feet
  and you fall into the hole.
- Scaffold up ~6 blocks and step off. **Expect:** the landing **hurts** — a grunt plays
  and the hearts row drops. Short hops (up to ~3 blocks) are free.
- Walk off the island's edge (empty your pockets first if you value them!).
  **Expect:** you fall past the underside, your **entire inventory is wiped**, and you
  respawn on the plateau at **full health**. Reaching 0 hearts triggers the exact same
  rule — one hardcore penalty everywhere. Machines and belts keep their buffers.

## 2b. Pause menu (Esc) — time stops

- With no menu open, press **Esc**. **Expect:** the world dims and a `PAUSED` panel
  opens (RESUME / SETTINGS / SAVE GAME / SAVE AND QUIT; hover/click or W/S + Enter;
  the version shows top-right). Watch a working machine's floating progress bar first:
  while paused it does **not** advance — machines, patch growth, weather, and the creature are
  frozen (machine hums go quiet; rain, if any, keeps sounding). **Esc** (or RESUME)
  resumes exactly where things left off, with no burst of catch-up activity.
  `SAVE GAME` quick-saves (title flashes SAVED).

## 2c. Settings & keybinds (pause menu → SETTINGS)

- Open SETTINGS from the pause menu. **Expect:** FULLSCREEN / VSYNC / SENSITIVITY /
  VOLUME / KEYBINDS... / BACK, values right-aligned (`< OFF >`, `< 0.12 >`, `< 80% >`).
  The sim stays frozen throughout (check a progress bar).
- **FULLSCREEN** (A/D or Enter): the window goes borderless fullscreen **live**, UI
  re-centers; toggle back. **VSYNC** off: the F3 frame average drops well below the
  refresh period; on restores it. **SENSITIVITY** up + resume: look is visibly faster.
  **VOLUME** to 0%: everything silent, live.
- **KEYBINDS...**: rows show current key names. Enter on JUMP → `PRESS A KEY` (red);
  press **W**. **Expect:** JUMP=W **and** MOVE FORWARD shows `---` (a key lives on one
  action). Esc mid-capture only cancels the capture; a reserved key (Enter, a digit)
  plays the deny sound and keeps capturing. RESET DEFAULTS restores everything.
- Esc backs out one level per press: capture → keybinds → settings → pause. Closing
  SETTINGS writes `settings.cfg` next to the save. Relaunch: **expect** every changed
  value and bind (including a `---` unbound) to survive. Hand-edit the file with
  `SENSITIVITY=99` and a junk line: **expect** a clean launch, value clamped to 0.40.

## 3. Mining (LMB) → inventory + patch regrowth

- Walk out to a resource cluster. Nodes are colored blocks: green (herb), copper-brown
  (copper ore), sandy (sand), violet (crystal), purple (essence) — five kinds, each
  cluster grown around a glowing **source** block of the same hue.
- Center the crosshair on a **copper ore** node and click **LMB**.
  **Expect:** the block breaks with a thock (mined). Mine the whole patch bare (but
  leave the glowing source), wait nearby for ~15–30 seconds, and **expect new nodes to
  grow back** near the source (one per ~7 s, up to 5 per patch). Mining the source
  itself drops a re-placeable source item instead.
- Confirm the drops entered your inventory: press **E** and check that the
  `COPPER INGOT ( COPPER ORE x2 )` row is bright/white (affordable). Press **E** to close.

## 4. Forestry — the tree is your fuel line

- Find the plateau's grown tree and chop it: **LMB** the trunk logs.
  **Expect:** each Log yields **Wood ×2** (check the crafting menu's `HAVE`).
- Chop the leaves too. **Expect:** some leaves drop a **Sapling** (about one in four,
  with a pity guarantee — never more than 4 dry leaves in a row). Leaves you leave
  orphaned (no log nearby) **decay on their own** within seconds and roll the same
  sapling chance, so trunk-first felling doesn't strand you.
- Select the Sapling in the hotbar (wheel) and place it on grass or dirt.
  **Expect:** it plants; after ~45 s with open space above it grows into a full tree.
  Wood is both **structure and fuel** — the renewable loop that feeds the generators.

## 5. Hand-crafting the bootstrap pair (E)

Economy v2: **Copper Plates are machine-made** (a **Press** recipe — it was a
Grinder recipe before July 2026), and nothing you can craft by hand costs a plate.
The tech tree bootstraps: tree → wood + starting ore → Generator + Press → fueled
Press presses plates → everything else. You start with a lean kit of raws (ore,
stone, sand, crystal, herb, essence) — exactly enough slack for this.

- Press **E**. **Expect:** the world dims, the cursor is released, and a `CRAFTING`
  panel lists recipes, each with its inputs, an affordability color (white = you have
  the inputs, gray = you don't), and `HAVE n`. An `INVENTORY` grid of your materials
  sits at the bottom (hovering a cell names it).
- **Click** a recipe row to craft it (hover highlights; the mouse wheel or **W/S**
  move the selection, **Enter** crafts). Craft `COPPER INGOT` a few times, then
  `WIRE`, then the pair: `GRINDER` (ingots + stone) and `GENERATOR` (ingots + stone +
  wood). **Expect:** inputs decrement, `HAVE` increments, the grid updates live, and
  dependent rows change color as you gain their inputs. A deny buzz plays if you
  activate a gray row. Close with **E**, **Esc**, or **RMB**.

## 6. Placement (RMB consumes inventory)

- Select a hotbar slot with a non-zero count (**1**–**9**/**0** or wheel), aim at the
  ground, and click **RMB**. **Expect:** the block is placed with a thunk and the count
  drops by 1. With a count of 0, RMB places nothing. Blocks refuse to place overlapping
  your own body.

## 7. Power is fueled — generators burn wood

- Place your **Generator**, a line of **Wire**, and the **Grinder** at the end (each
  block adjacent to the previous one). **Expect:** nothing glows yet — a generator with
  no fuel is a dark network.
- **RMB** the generator and drag some **Wood** from the INVENTORY grid onto its `IN:`
  band (LMB-drag = stack, RMB-drag = one). **Expect:** as soon as the network has work
  to do, the generator lights: it burns **one Wood per ~20 s**, its bar counts the burn
  down (a fuel gauge), and the generator, wires, and grinder all **glow** (energized) —
  with a hum audible nearby. Generators only light a new piece while something on the
  network **demands** power, so idle networks don't waste fuel.
- Mine a middle wire (**LMB**). **Expect:** the disconnected grinder stops glowing;
  replace the wire and it glows again.

## 8. Machines — open, load, process, take

- Mine several **herb** bushes first, then aim at your powered grinder.
  **Expect:** a small look-at panel shows `GRINDER`, `IN:`/`OUT:` lines, and `RMB OPEN`.
- **Right-click the grinder.** **Expect:** the cursor is released and a panel opens:
  machine name, `POWERED`/`NO POWER` status, an `AUTO ( FIRST READY RECIPE )` row, one
  `MAKE <output> ( inputs )` row per recipe, a `TAKE OUTPUTS` row, `IN:`/`OUT:` buffers
  as item cells, a live progress bar, and an `INVENTORY` grid of everything you own.
  A `>` marker shows the active recipe mode (AUTO by default).
- **Drag and drop:** LMB-drag an inventory cell onto the `IN:` band to move the whole
  stack (the band tints green when the machine accepts that item, red when it doesn't);
  RMB-drag moves a single item. Dragging a cell *out* of `IN:`/`OUT:` onto the
  inventory grid returns it to you. Releasing anywhere else (or Esc) returns the
  payload where it came from — items are never lost.
- Open a powered **Press** and click the `MAKE COPPER PLATE ( COPPER INGOT )` row.
  **Expect:** the `>` marker moves to it (the machine locks to that recipe and refuses
  belt deliveries of other ingredients), your ingots move into `IN:`, the bar fills
  (~3 s per plate), and `OUT: COPPER PLATE xN` grows. Activate `TAKE OUTPUTS` to
  collect. **Plates now unlock the rest of the table**: Conduit, Wrench, Copper Sword,
  Machine Frame, and every alchemy machine. (An unpowered machine loads but does not
  run.) On `AUTO` a Press fed plain ingots also makes plates — plate is the first row,
  ahead of `COPPER ROD`, which costs the same single ingot; lock `MAKE COPPER ROD` when
  you want rods.

## 9. Conduits — auto-transport

- Face the direction you want items to flow, then place a **Conduit** (Copper Plate ×2
  → Conduit ×2) directly in front of the grinder's output side. (A conduit takes items
  from the machine *behind* it and pushes to whatever is *ahead*; its direction is set
  by the way you were facing when you placed it.) Extend the line and put a machine at
  the end. **Expect:** the grinder's products ride the conduits (small floating icons)
  and are delivered with no manual loading.

## 9b. Vertical conduits, the wrench, and terrain collection

- Mine a **grass** block (LMB). **Expect:** the hotbar's Grass slot gains 1; Dirt
  collects the same way. Both place back with RMB — terrain holes are repairable.
- Look **steeply down** (or up) and place a Conduit. **Expect:** it faces vertically —
  its exposed *side* faces show the arrow pointing down (or up). Vertical conduits move
  items between floors exactly like horizontal ones.
- Craft a **Wrench** (Copper Plate ×2), aim at any conduit, and press **R**.
  **Expect:** each press re-aims it through six directions (+x, +z, -x, -z, up, down),
  the arrow updating instantly. Without a wrench in inventory, R does nothing.

## 10. Weather — rain is the ONLY water

- Wait for a rain front (clear phases run ~1.5–4 min; rain ~40–100 s), or press the
  hidden dev key **F4** to force rain on/off for testing.
  **Expect:** the sky eases to storm-grey, world-space streaks fall around you (skipping
  covered spots — stand under a roof and the rain stops overhead), lit surfaces dim
  while **energized machines stay bright** (beacons in the murk), and a rain ambience
  swells. Source patches and saplings grow ~3× faster in rain.
- **Bucket** (Wood ×3): select it in the hotbar and stand under open sky in the rain.
  **Expect:** a fill bar appears over the hotbar; after ~8 s you collect one
  **Rain Water**. Under a roof (or holding anything else) the bar resets.
- **Rain Barrel** (Wood ×6 + Bucket): place it under open sky.
  **Expect:** during rain it fills itself — one Rain Water per ~12 s, up to 10 buffered —
  with **no power needed**; it doesn't join power networks. Belts pull from it like any
  machine. The **Cauldron** recipes consume Rain Water, so the alchemy chain runs on
  the weather.

## 11. Combat & health

- Find the **creature** wandering near the plateau (one spawns fresh each launch; it is
  deliberately **not** saved). **Expect:** it walks a few blocks, idles, turns — with
  gravity and block collision like the player.
- Craft a **Copper Sword** (Copper Plate ×2 + Wood ×1), select it, aim at the creature,
  and click **LMB**. **Expect:** a swing whoosh; on a hit, a thunk — the creature
  flashes red, is knocked back with a little pop into the air, and **flees**. Three
  hits fell it (it vanishes; no drops yet — boss loot answers "why fight" later).
  A swing that misses falls through to normal mining; the sword is never a placement.
- Hurt yourself (a tall drop), then select the **Healing Draught** (the Infuser makes
  them) and click **RMB**. **Expect:** a heal sound, +4 hearts, one draught consumed,
  and the click places/opens nothing. RMB with full hearts drinks nothing.

## 12. End-game — the closed loop (long play or spot-check)

- The full chain: **Distiller** (Elixir of Vigor + Essence → Refined Elixir) and
  **Transmuter** (Refined Elixir + Crystal Dust → Philosopher's Catalyst; Catalyst +
  Elixir → Philosopher's Stone) appear in the crafting menu and process like the other
  machines when powered.
- With a Catalyst in inventory, the crafting menu's **last five rows** craft new
  **source blocks** (Catalyst + 8 of the raw: herb, crystal, copper, sand, essence).
  Craft one, wheel-select it, and place it on grass.
  **Expect:** it glows and, within ~15 seconds, begins growing its own node patch —
  resource production itself is craftable, closing the economy loop.

## 13. Save / load

- Craft something distinctive (e.g. two Copper Ingots), walk somewhere memorable, then
  quit via **Esc → SAVE AND QUIT**.
  **Expect:** the game closes cleanly and writes
  `%APPDATA%\BennyThompson\voxel-factory\save.vxf` (a legacy save under `benny\` is
  migrated on first launch).
- Relaunch. **Expect:** the *same* island (identical coastline and source layout), your
  camera exactly where you left it, machines/belts/weather/health resuming exactly
  (fuel gauges included), and the crafting menu showing your ingots (`HAVE 2`). The
  creature is new each launch by design. **F5** saves at any time without quitting.
  Delete `save.vxf` to start a fresh island.
- Headless check: `voxel-factory.exe --selftest` runs a save/load round-trip and exits
  0 on success (this is what CI runs).

## 14. Perf overlay (F3) — dev

- Press **F3**. **Expect:** frame avg/worst ms, last remesh/power-solve costs, and
  per-second counts. On a settled island the worst frame should sit in the single-digit
  milliseconds; editing one block should remesh only the chunk(s) it touches.

## 15. Quit

- Press **Esc** and activate **SAVE AND QUIT** (or just close the window).
  **Expect:** the game closes cleanly (and saves either way).
