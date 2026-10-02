# Art progress: block models and item icons

A working log for the art pass: the 3D block models still missing from
`models/PROMPTS.md`, and new item icons for inventories, the hotbar, and
items on the ground. Update the tables and the log with every asset that
lands.

## How each kind of asset is made

**Block models (`models/*.bbmodel`).** Geometry is written by hand as cube
elements, so it is exactly what the prompt's budget allows: no generator
over-modelling to trim afterwards. Each model is a script in
`tools/block_models/` (`<block>.py`) built on `bbgen.py`, which writes a
Blockbench 5.0 `java_block` file: every face samples a sub-rect of a named
material region at one texel per unit (the game's density), per-face UV, with
named groups for moving parts. Materials are PixelLab swatches kept in
`models/materials/`, or the game's own atlas tiles where those are the right
look. The `.bbmodel` stays the real source and opens in Blockbench; the script
is how it was made and how to remake it. These tools need Pillow (the bake and
`make_atlas.py` do not).

Then the usual pipeline (`models/AUTHORING.md`): bake every model in one run,
the four C++ edits, check the bake's quad/KB report against the budget, and
look at it in the game. Hand-cranked machines keep the handle in a group named
`crank`.

PixelLab notes so far: `create_image_pixflux` at 32x32 with a palette taken
from the matching atlas tiles gives good wood and powder swatches (1
generation each). It kept drawing STONE as cobbles or bricks even when told
not to, twice, so stone uses the atlas Stone tile instead. Its freeform tools
reject 16x16 (the minimum area is 32x32).

**Item icons (`game/assets/atlas.png`, 16x16 tiles).** Generated in batches
with PixelLab's `create_1_direction_object` at size 16, which returns up to 64
candidates per call, one per description, in a single consistent style. The
existing icons go in as the style reference, so new ones sit beside the old.
Each pick is checked at 1x on the dark panel background before it replaces
its tile. Two things need code, not just art:

- **Placeable items have no icon of their own.** `iconTile()` borrows the
  block's side texture, so a Mortar in your pack is a flat stone square. They
  need `ItemInfo::atlasTile` honoured for placeables too, plus free atlas
  rows (the grid only grows in rows, see `game/assets/ATLAS.md`).
- **`tools/make_atlas.py` overwrites hand edits.** Once generated icons are
  in, it must not be rerun without first teaching it to keep them.

PixelLab budget: 2000 generations a cycle (refills on the 30th of each
month). A 16px batch costs 20-40.

## Block models: 16 of 42 done, 26 to go

Done before this pass (see the Status lines in `models/PROMPTS.md`): all ten
Sources and Nodes, Scaffold, Tilled Soil, Rich Soil, Grafted Sapling, Sieve.

| # | block | tier | budget | status |
|--:|---|---|--:|---|
| 1 | Mortar | hand-cranked | 14 | **done**: 12 elements, 63 quads, 20.7 KB |
| 2 | Hand Press | hand-cranked | 14 | not started |
| 3 | Anvil | hand-cranked | 14 | not started |
| 4 | Blowpipe | hand-cranked | 14 | not started |
| 5 | Tamper | hand-cranked | 14 | not started |
| 6 | Compost Heap | hand-cranked | 14 | not started |
| 7 | Mixing Bowl | hand-cranked | 14 | not started |
| 8 | Infusion Stand | hand-cranked | 14 | not started |
| 9 | Still | hand-cranked | 14 | not started |
| 10 | Hand Distiller | hand-cranked | 14 | not started |
| 11 | Hand Transmuter | hand-cranked | 14 | not started |
| 12 | Bloomery | fuel-fired | 16 | not started |
| 13 | Furnace | fuel-fired | 20 | not started |
| 14 | Generator | powered | 28 | not started |
| 15 | Grinder | powered | 28 | not started |
| 16 | Press | powered | 28 | not started |
| 17 | Forge | powered | 32 | not started |
| 18 | Sifter | powered | 28 | not started |
| 19 | Glassblower | powered | 28 | not started |
| 20 | Compactor | powered | 28 | not started |
| 21 | Composter | powered | 28 | not started |
| 22 | Distiller | powered | 32 | not started |
| 23 | Transmuter | powered | 32 | not started |
| 24 | Harvester | powered | 28 | not started |
| 25 | Rain Barrel | unpowered | 14 | not started |
| 26 | Storage Crate | unpowered | 8 | not started |
| 27 | Irrigator | unpowered | 16 | not started |

## Item icons: 0 of 118 done

| batch | items | count | status |
|---|---|--:|---|
| 1 | Raw materials and parts: Stone, Copper Ore, Sand, Herb, Crystal, Rain Water, Essence, Wood, Stick, Pebble, Plant Fiber, Twine, Compost, Bio Briquette, Charcoal, Copper/Iron Nugget, Copper/Iron Ingot, Copper/Iron Plate, Copper/Iron Rod, Gear, Machine Casing, Etched Plate, Machine Frame, Glass, Vial | 29 | not started |
| 2 | Alchemy: Ground Herb, Crystal Dust, Herbal Tincture, Mineral Solution, Healing Draught, Mana Vial, Elixir of Vigor, Refined Elixir, Philosopher's Catalyst, Philosopher's Stone, Resonance, Fusion Catalyst, Void Catalyst, Storm Core, Teleport Key, Storm Key | 16 | not started |
| 3 | Tools, weapons, armor: Wood/Stone/Copper/Iron Pickaxe and Axe; Stone/Copper/Iron Shovel; Copper Hoe; Copper/Iron Sword; Wrench, Bucket; Copper/Iron/Aegis Helm, Chestplate, Boots | 25 | not started |
| 4 | Placeables (needs the code change above): the 48 machines, sources, soils and saplings | 48 | not started |

## Log

- **2026-09-29.** Pass started. PixelLab MCP connected (Tier 1, 1908
  generations left this cycle). This file created.
- **2026-09-29. Mortar done.** Wooden cradle (two runners, two cheeks), a
  stone bowl (base and four walls, shaded inside, rim dusted with powder),
  lilac powder, and a pestle leaning on the rim in the `crank` group. 12 of 14
  elements, 20.7 KB. Baked, wired into `Block.cpp`, build clean, selftest
  passes, and checked in the game. Cost 4 generations (stone twice, wood,
  powder).
- **2026-09-29. Two earlier models checked in the game at last.** Tilled
  Soil, Rich Soil and the Grafted Sapling look right. The Sieve's mesh worked
  but sat 3.5 units down inside a 6-unit frame, so the walls hid it and the
  block read as a wooden box; the screen and braces now sit just under the
  rim, and the twine grid shows.
- **2026-09-29. Hand-cranked parts move.** Engine work (see CLAUDE.md, "Parts
  can MOVE, and a hand can drive them"): parts can now translate (`Bob`),
  several motions compose on one part, and a `cranked` motion follows the
  player's turns of the handle. The Mortar's pestle grinds round the bowl and
  mashes into the powder twice per turn while its crank panel is open.
  **For every hand-cranked model from here on:** give the `crank` group a
  pivot where it would bear, pose it sensibly at rest, and add its
  `kPartAnims` rows (with `cranked = true`) in the same change.
