# Testing Voxel Factory

A manual test script that exercises the whole automation loop
(mine → craft → power → process → transport). Each step lists an action and the
result to expect.

## Controls quick-reference

| Action | Input |
|---|---|
| Look | Mouse |
| Move (horizontal plane) | **W A S D** |
| Up / Down | **Space** / **Left Shift** |
| Sprint (~3×) | hold **Left Ctrl** |
| Mine block | **LMB** |
| Place selected item | **RMB** |
| Select hotbar slot | **1**–**8** |
| Open/close crafting menu | **E** |
| Menu: select / craft | **W/S** (or ↑/↓) / **Enter** |
| Machine: load inputs / take outputs | **F** / **G** (while aiming at it) |
| Close menu, or quit | **Esc** |

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
   **Expect:** a window titled `Voxel Factory  —  Holding: Conduit x0 …`, a sky-blue
   background over textured green ground, a centered crosshair, and an 8-slot hotbar along
   the bottom (all counts 0). No console errors.

## 1. Smoke test — watch the demo run (no input)

3. Look at the structure ahead: **generator** (orange) → glowing **wire** (yellow) →
   **grinder** (gray) → three dark **conduits** → **cauldron** (brass). Wait ~5 seconds.
   **Expect:** a green progress bar floats above the grinder and fills repeatedly; every
   couple of seconds a small item icon (ground herb) appears on the conduits and travels
   into the cauldron. This one view exercises power + machine processing + transport.

## 2. Camera & movement

4. Move the mouse to look around, then hold **W** while looking up and down.
   **Expect:** you move on the horizontal plane only — your altitude does not change.
5. **A/D** strafe; **Space** rises; **Left Shift** descends; holding **Left Ctrl** while
   moving is ~3× faster. (**Esc** with no menu open quits — don't press it yet.)

## 3. Mining (LMB) → inventory

6. Fly over the open ground. Resource nodes are scattered colored blocks: green (herb),
   copper-brown (copper ore), sandy (sand), violet (crystal), blue (spring), purple
   (essence).
7. Center the crosshair on a **copper ore** node and click **LMB**.
   **Expect:** the block is removed (mined). Mine a few nodes.
8. Confirm the drops entered your inventory: press **E** and check that the
   `COPPER INGOT ( COPPER ORE )` row is bright/white (affordable). Mining grass or dirt
   yields nothing. Press **E** to close.

## 4. Crafting menu (E)

9. Press **E**. **Expect:** the world dims and a `CRAFTING` panel lists recipes, each with
   its inputs, an affordability color (white = you have the inputs, gray = you don't), and
   `HAVE n` (how many you own). You start with some raw materials, so several rows are
   already white.
10. Use **W/S** (or ↑/↓) to move the yellow selection and **Enter** to craft. Craft
    `COPPER INGOT`, then select and craft `COPPER PLATE`, then `WIRE`.
    **Expect:** inputs decrement and `HAVE` increments live; dependent rows change color as
    you gain their inputs. Close with **E**.
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

## 7. Machines — load, process, take

15. Mine several **herb** bushes first, then aim at your powered grinder.
    **Expect:** a look-at panel shows `GRINDER`, an `IN:` line, an `OUT:` line, and
    `F LOAD   G TAKE`.
16. Press **F**. **Expect:** herb moves from your inventory into the grinder
    (`IN: HERB xN`); a progress bar appears above it and, after ~2s, `OUT: GROUND HERB xN`
    grows. An unpowered machine does not progress.
17. Press **G**. **Expect:** the outputs move to your inventory and the panel's `OUT:`
    clears.

## 8. Conduits — auto-transport

18. Face the direction you want items to flow, then place a **Conduit** directly in front of
    the grinder's output side. (A conduit takes items from the machine *behind* it and
    pushes to whatever is *ahead*; its direction is set by the way you were facing when you
    placed it.) Extend the line with more conduits and put a **Cauldron** at the end.
    **Expect:** ground herb the grinder produces rides the conduits (small floating icons)
    and is delivered into the cauldron with no manual loading.

## 9. Break & recover

19. Mine (**LMB**) a loaded machine or an occupied conduit.
    **Expect:** the block breaks, its placeable item returns to your inventory, and any
    buffered or carried items are returned too — nothing is lost.

## 10. Quit

20. With no menu open, press **Esc**. **Expect:** the game closes cleanly.
