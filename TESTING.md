# Testing Voxel Factory

A manual test script that exercises the whole automation loop
(mine → craft → power → process → transport). Each step lists an action and the
result to expect.

## Controls quick-reference

| Action | Input |
|---|---|
| Look | Mouse |
| Walk | **W A S D** (no flying!) |
| Jump | **Space** |
| Sprint | hold **Left Ctrl** |
| Mine block | **LMB** |
| Place selected item | **RMB** |
| Select hotbar slot | **1**–**9**, **0** (first ten) · **mouse wheel** cycles all |
| Open/close crafting menu | **E** |
| Help screen | **F1** (F1/Esc closes) |
| Quick-save | **F5** (quitting also auto-saves) |
| Re-aim a conduit | **R** while aiming at it (requires a crafted Wrench) |
| Menu: select / craft | **W/S** (or ↑/↓) / **Enter** |
| Open a machine's panel | **RMB** on it (Shift+RMB places against it instead) |
| In the panel: choose / act / close | hover or **W/S** · click or **Enter** · **Esc/E/RMB** |
| Close menu / pause | **Esc** (with nothing open, Esc opens the **pause menu**) |
| Quit | pause menu → **SAVE AND QUIT** (or close the window; both save) |

**Observability note:** the hotbar shows only *placeable* items (conduit, wire, machines).
Raw-material counts (ore, herb, …) are not on the hotbar — confirm them via the crafting
menu (a recipe row turns white when you can afford it, and shows `HAVE n`) or a machine's
look-at panel.

## Build & launch

1. From `voxel-factory/`, load the MSVC environment and build (the **first** build compiles
   SDL3 from source — expect a few minutes; later builds are fast):
   ```powershell
   & "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat"
   cmake --build out/build/x64-Debug
   ```
2. Run `out/build/x64-Debug/bin/voxel-factory.exe`.
   **Expect:** a window titled `Voxel Factory  —  Holding: Conduit x0 …`, a centered
   crosshair, and a 16-slot hotbar along the bottom (all counts 0). You spawn on a grassy
   plateau at the center of a **floating island** in a blue sky. No console errors.
   The island layout is randomized each launch.

## 1. Smoke test — watch the demo run (no input)

3. Look at the structure ahead: **generator** (orange) → glowing **wire** (yellow) →
   **grinder** (gray) → three dark **conduits** → **cauldron** (brass). Wait ~5 seconds.
   **Expect:** a green progress bar floats above the grinder and fills repeatedly; every
   couple of seconds a small item icon (ground herb) appears on the conduits and travels
   into the cauldron. This one view exercises power + machine processing + transport.
   Then fly up (**Space**) and look down.
   **Expect:** a roughly circular island with an irregular coastline floating in open sky,
   dotted with **clusters** of colored resource nodes, each cluster around one brighter,
   **glowing source block**. Fly past the coast and look under the island: the stone
   underside tapers toward the middle.
4. Turn around toward the plateau's south side: a second demo — glowing **herb source** →
   **miner** (gray) + **generator** (orange) → two conduits.
   **Expect:** the miner harvests nodes that grow around the source (watch one vanish and
   its progress bar fill, one node per ~4s within radius 4) and herb rides its conduits —
   fully automated mining, bounded by the patch's regrowth. Right-click the miner:
   its panel reads `MINES NEARBY RESOURCE NODES ( RADIUS 4 )` and `OUT:` accumulates herb.

## 2. Camera & movement (walking physics)

4. Move the mouse to look around; **WASD** walks, **Space** jumps (~1.3 blocks),
   **Left Ctrl** sprints. **Expect:** gravity holds you to the ground, one-block walls
   stop you, and you can jump onto single blocks. There is **no flying** — build with
   cheap **Scaffold** (Stone ×1 → Scaffold ×4) to climb; mine the block under your feet
   and you fall into the hole.
5. Walk off the island's edge (empty your pockets first if you value them!).
   **Expect:** you fall past the underside, your **entire inventory is wiped**, and you
   respawn on the plateau. The edge is the game's first real danger.

## 2b. Pause menu (Esc) — time stops

5b. With no menu open, press **Esc**. **Expect:** the world dims and a `PAUSED` panel
    opens (RESUME / SAVE GAME / SAVE AND QUIT; hover/click or W/S + Enter; the version
    shows top-right). Watch a working machine's floating progress bar first: while
    paused it does **not** advance — machines, patch growth, and weather are frozen.
    **Esc** (or RESUME) resumes exactly where things left off, with no burst of
    catch-up activity. `SAVE GAME` quick-saves (title flashes SAVED).

## 3. Mining (LMB) → inventory + patch regrowth

6. Fly to a resource cluster. Nodes are colored blocks: green (herb), copper-brown
   (copper ore), sandy (sand), violet (crystal), blue (spring), purple (essence) — each
   cluster grows around a glowing **source** block of the same hue.
7. Center the crosshair on a **copper ore** node and click **LMB**.
   **Expect:** the block is removed (mined). Mine the whole patch bare (but leave the
   glowing source), hover nearby for ~15–30 seconds, and **expect new nodes to grow back**
   near the source. Mining the source itself drops a re-placeable source item instead.
8. Confirm the drops entered your inventory: press **E** and check that the
   `COPPER INGOT ( COPPER ORE )` row is bright/white (affordable). Mining grass or dirt
   yields nothing. Press **E** to close.

## 4. Crafting menu (E)

9. Press **E**. **Expect:** the world dims, the cursor is released, and a `CRAFTING`
   panel lists recipes, each with its inputs, an affordability color (white = you have
   the inputs, gray = you don't), and `HAVE n` (how many you own). An `INVENTORY` grid
   of your materials sits at the bottom (hovering a cell names it). You start with some
   raw materials, so several rows are already white.
10. **Click** a recipe row to craft it (hovering highlights; the mouse wheel or **W/S**
    also move the selection, **Enter** crafts it). Craft `COPPER INGOT`, then
    `COPPER PLATE`, then `WIRE`.
    **Expect:** inputs decrement, `HAVE` increments, and the inventory grid updates live;
    dependent rows change color as you gain their inputs. Close with **E**, **Esc**, or
    **RMB**.
11. **Expect:** the **Wire** hotbar slot (slot 2) now shows a non-zero count.

## 5. Placement (RMB consumes inventory)

12. Open the menu and craft a few `CONDUIT`, plus one `GENERATOR` and one `GRINDER`. Close
    the menu.
13. Press **1**–**8** to select a hotbar slot (the selected slot is highlighted). Choose a
    slot whose count is > 0, aim at the ground, and click **RMB**.
    **Expect:** the block is placed and the hotbar count drops by 1. With a count of 0, RMB
    places nothing.

## 6. Power networks

14. Place a **Generator**, then a line of **Wire** leading from it to a **Grinder** (each
    block adjacent to the previous one).
    **Expect:** the generator, wires, and grinder glow (energized). Mine a middle wire
    (**LMB**) and the now-disconnected grinder stops glowing; replace the wire and it glows
    again.

## 7. Machines — open, load, process, take

15. Mine several **herb** bushes first, then aim at your powered grinder.
    **Expect:** a small look-at panel shows `GRINDER`, `IN:`/`OUT:` lines, and `RMB OPEN`.
16. **Right-click the grinder.** **Expect:** the cursor is released and a panel opens:
    machine name, `POWERED`/`NO POWER` status, an `AUTO ( FIRST READY RECIPE )` row, one
    `MAKE <output> ( inputs )` row per recipe (white when you can contribute an input),
    a `TAKE OUTPUTS` row, `IN:`/`OUT:` buffers shown as item cells, a live progress bar,
    and an `INVENTORY` grid of everything you own (icon + count; hovering a cell names
    it). A `>` marker shows the active recipe mode (AUTO by default).
16b. **Drag and drop:** LMB-drag an inventory cell onto the `IN:` band to move the whole
    stack (the band tints green when the machine accepts that item, red when it doesn't);
    RMB-drag moves a single item. Dragging a cell *out* of `IN:`/`OUT:` onto the
    inventory grid returns it to you. Releasing anywhere else (or Esc) returns the
    payload where it came from — items are never lost.
17. Click (or W/S + Enter) the `MAKE GROUND HERB` row. **Expect:** the `>` marker moves to
    it (the machine is now locked to that recipe — it also refuses belt deliveries of
    other ingredients) and your herb moves into `IN:`; if powered, the progress bar fills
    and `OUT: GROUND HERB xN` grows every ~2s. Activate `TAKE OUTPUTS` to collect; the
    `AUTO` row returns the machine to first-ready-recipe mode. Close with **Esc**, **E**,
    or **RMB**. (An unpowered machine loads but does not run.)

## 8. Conduits — auto-transport

18. Face the direction you want items to flow, then place a **Conduit** directly in front of
    the grinder's output side. (A conduit takes items from the machine *behind* it and
    pushes to whatever is *ahead*; its direction is set by the way you were facing when you
    placed it.) Extend the line with more conduits and put a **Cauldron** at the end.
    **Expect:** ground herb the grinder produces rides the conduits (small floating icons)
    and is delivered into the cauldron with no manual loading.

## 8b. Vertical conduits, the wrench, and terrain collection

18b. Mine a **grass** block (LMB). **Expect:** the hotbar's Grass slot (last of 18)
    gains 1; Dirt collects the same way. Both place back with RMB — terrain holes are
    repairable with the material you collected.
18c. Look **steeply down** (or up) and place a Conduit. **Expect:** it faces vertically —
    no arrow on its top; its exposed *side* faces show the arrow pointing down (or up).
    Vertical conduits move items between floors exactly like horizontal ones.
18d. Craft a **Wrench** (Copper Plate x2), aim at any conduit, and press **R**.
    **Expect:** each press re-aims it through six directions (+x, +z, -x, -z, up, down),
    the arrow updating instantly. Without a wrench in inventory, R does nothing.

## 9. Break & recover

19. Mine (**LMB**) a loaded machine or an occupied conduit.
    **Expect:** the block breaks, its placeable item returns to your inventory, and any
    buffered or carried items are returned too — nothing is lost.

## 10. End-game — the closed loop (long play or spot-check)

20. The full chain: **Distiller** (Elixir of Vigor + Essence → Refined Elixir) and
    **Transmuter** (Refined Elixir + Crystal Dust → Philosopher's Catalyst; Catalyst +
    Elixir → Philosopher's Stone) appear in the crafting menu and process like the other
    machines when powered.
21. With a Catalyst in inventory, the crafting menu's last six rows craft **new source
    blocks** (Catalyst + 8 of the raw). Craft one, wheel-select it (it sits past slot 10),
    and place it on grass.
    **Expect:** it glows and, within ~15 seconds, begins growing its own node patch —
    resource production itself is craftable, closing the economy loop.

## 11. Save / load

22. Craft something distinctive (e.g. two Copper Ingots), fly somewhere memorable, then
    quit via **Esc → SAVE AND QUIT**.
    **Expect:** the game closes cleanly and writes
    `%APPDATA%\benny\voxel-factory\save.vxf`.
23. Relaunch. **Expect:** the *same* island (identical coastline and source layout), your
    camera exactly where you left it, machines/belts resuming their work, and the
    crafting menu showing your ingots (`HAVE 2`). **F5** saves at any time without
    quitting. Delete `save.vxf` to start a fresh island.

## 12. Quit

24. Press **Esc** and activate **SAVE AND QUIT** (or just close the window).
    **Expect:** the game closes cleanly (and saves either way).
