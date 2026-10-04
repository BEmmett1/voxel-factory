# Art progress: block models and item icons

A working log for the art pass: the 3D block models still missing from
`models/PROMPTS.md`, and new item icons for inventories, the hotbar, and
items on the ground. Update the tables and the log with every asset that
lands.

## Art direction (decided 2026-10-02)

**Moodier and more alchemical**, applied to everything: atlas tiles, item
icons, and the 3D models' textures (keeping the models that are already good).
Concretely, from the approved terrain pilot:

- **Mid-tone and readable, never near-black.** The first pilot was too dark:
  under the game's lighting the moss read as asphalt and the canopy as a black
  blob. Ask for MEDIUM brightness and check it on blocks in the game, not on
  the tile alone.
- **Dusky, desaturated, with faint verdigris and violet undertones.** Moss
  green, grooved warm bark, slate stone, ashen sand, peat; violet flecks in the
  leaves, verdigris in the copper.
- **A dusk sky to match** (`kSkyClear` twilight teal, `kSkyStorm` bruised
  slate, in VoxelGameInternal.h). A dusky world under a noon-blue sky reads as
  a texture swap, not a mood.
- **Style references for later batches are the approved atlas tiles**: 0, 1,
  2, 3, 6, 7, 8, 50, 51, 60.

**Tile lessons.** `create_tiles_pro` draws a dark outline round every tile by
default, and on a block face that tiles into a visible grid across the whole
island. `outline_mode: segmentation` usually prevents it -- but not always
(the voidstone came back rimmed anyway), so measure every pick: the mean edge
luminance minus the interior's should be within about +-6. A rimmed tile with
good texture is rescued by copying the row/column inside the border onto it;
the grass also needed its grey patches pulled toward moss and its contrast cut
to 60% so the 16px repeat stopped reading as a pattern. Ask for numbered tiles;
each comes back as two candidates.

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
- **`tools/make_atlas.py` overwrites hand edits.** It now refuses to run
  without `--overwrite-art`: the committed atlas.png is the source.

PixelLab budget: 2000 generations a cycle (refills on the 30th of each
month). A 16px batch costs 20-40.

## Block models: 17 of 42 done, 25 to go

Done before this pass (see the Status lines in `models/PROMPTS.md`): all ten
Sources and Nodes, Scaffold, Tilled Soil, Rich Soil, Grafted Sapling, Sieve.

| # | block | tier | budget | status |
|--:|---|---|--:|---|
| 1 | Mortar | hand-cranked | 14 | **done**: 12 elements, 63 quads, 20.7 KB |
| 2 | Hand Press | hand-cranked | 14 | **done**: 12 elements, 64 quads, 21.0 KB |
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

## Atlas terrain: done

Every non-machine block still drawn from the atlas: grass (top, side), dirt,
stone, log (end, bark), leaves, copper ore, sand node, voidstone -- tiles 0,
1, 2, 3, 6, 7, 8, 50, 51, 60. The 26 machines still on atlas tiles are
deliberately skipped: each is waiting for a 3D model, and a new flat texture
for it would be thrown away.

## 3D model textures

All 29 shaped blocks were photographed in a gallery under the new sky (a
temporary new-game hook stands them in three stepped rows). Most already
fit the mood -- the generator painted them dark -- and keep their textures.
Six stood out and were **retextured with their geometry untouched**:
Conduit (glaring white -> verdigris glass in bronze), Wire (gold blob ->
tarnished copper), Rune Core (neon cyan -> verdigris glass, brass, violet),
Sand Source (cream -> ashen sandstone), Copper Source (pale -> slate banded
with copper), Herb Bush (flat green -> the terrain's violet-flecked leaves).

How: `tools/block_models/retexture.py` classifies every texel by what its
ORIGINAL colour depicted and repaints it from a material swatch (or recolours
a glow), scaled by the texel's lightness relative to its class's average --
so the author's shading, edges and silhouette cues survive and only material
and colour change. Box UV, per-face UV and animation strips (the Rune Core's
8 frames) all work, since it never needs to know which face a texel is on.
`retexture_moody.py` holds the six classifications; it must run on ORIGINAL
textures (a second pass would reclassify the new colours) and takes model
names to redo just those. Materials: PixelLab bronze, verdigris glass,
copper, sandstone and brass in models/materials/, plus the terrain's slate,
bark and leaves straight from the atlas, so models and terrain share a
palette. Lesson: a model whose whole texture is tiny (the Wire's 16x16)
samples one corner of a swatch, and the copper swatch's corner was
verdigris-green -- it needed a clean 16x16 window of the swatch instead.

## Item icons: 118 of 118 done

| batch | items | count | status |
|---|---|--:|---|
| 1 | Raw materials and parts | 29 | **done** |
| 2 | Alchemy: potions, catalysts, cores, keys (+ rerolls of pebble, iron nugget, casing, essence) | 16 | **done** |
| 3 | Tools, weapons, armor, wrench, bucket | 25 | **done** |
| 4 | Placeables: the 48 machines, sources, soils and saplings (atlas rows 13-15, tiles 208-255) | 48 | **done** |

`iconTile()` now prefers an item's own `atlasTile`, placeable or not, falling
back to the block's side; the 48 placeable rows in Item.cpp carry tiles
208-255 (the map is in game/assets/ATLAS.md). Seen in game: the Tab inventory
and the crafting menu with the F6 kit.

**Icon lessons.** `create_1_direction_object` at size 16 returns 64
candidates per call (20 generations); give each item two description slots
and pick by eye, since a reroll's frames do not reliably follow their slot
order. Batch 1 ran on description alone and its best picks then went in as
`style_images` for every later batch, which is what keeps 118 icons in one
style. Recurring fixes, all free: dark iron pieces need lightness lifted
(x1.2-1.45) to read on the dark panel; two keys came back on an opaque slate
panel that needed flood-filling out from the transparent border; a few
"extras" (the slots past the described items) were better than the named
attempt -- the bucket, the mortar and pestle, the crate. Small hand tools are
the hard case at 16px: ten of the 24 hand-tier placeables needed a reroll
with more concrete descriptions ("classic black iron blacksmith anvil, side
view").

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
- **2026-10-02. Art direction set; terrain retextured.** User asked for a
  full retexture, moodier and more alchemical, models included. Three pilot
  rounds: too dark (moss as asphalt), then right but gridded (tile outlines),
  then right. The sky changed to dusk in code to match. Terrain written into
  atlas.png (tiles 0-3, 6-8, 50, 51, 60); the leaves lightened after review.
  make_atlas.py now refuses to overwrite the art. About 100 generations used.
- **2026-10-02. All 118 item icons replaced**, including the 48 placeables,
  which needed `iconTile()` to honour a placeable's own atlasTile. Six icon
  calls (two of them the 48 placeables, one a reroll of ten hand tools); 200
  generations used in total so far, 1800 left this cycle.
- **2026-10-04. modelkit built** (`tools/modelkit/`): preview any block or
  creature in seconds with its real animation, check bounds across the whole
  motion, and build new models from Python. The remaining 26 block models
  should be authored with it: spec -> preview -> iterate -> one in-game look
  at the end, instead of a game launch per iteration.
- **2026-10-04. Six model textures redone** (Conduit, Wire, Rune Core, Sand
  Source, Copper Source, Herb Bush) after a gallery review; the user chose
  all six. 5 generations for materials. Checked in the gallery in game.
- **2026-10-04. Hand Press done**, the first model built entirely with
  modelkit: a wooden bench and frame, an iron bed, and the `crank` group
  (screw, pressing plate, bar handle with brass knobs) that spins once and
  presses once per turn. The first preview caught the stroke driving the
  handle into the crossbeam, before any game launch; the beam came down and
  the stroke was sized so the plate just meets the bed. One generation for a
  wrought-iron swatch (lifted x1.7: as delivered it read as a hole). Checked
  in the game while cranking.
