# One prompt per block

Every block below is a **single, complete, copy-paste prompt**. Nothing needs
to be pasted with it and nothing needs filling in: the rules, the texture
size, the element budget and the file name are already inside each one.
Generate one block at a time.

`AUTHORING.md` is the authority on the pipeline; the short version of what to
do with a finished file is at the bottom of this page.

**42 blocks.** Terrain -- Grass, Dirt, Stone, Sand, Copper Ore, Log, Leaves,
Voidstone -- is deliberately absent: it is placed by the million and a cube is
the right answer. Wire, Conduit, Rune Core, Pedestal, Sapling, the Cauldron,
Alembic, Auger, Infuser and the four crop stages already have models.

Each prompt carries a **Status** line under its heading. A returned model
rarely keeps the stem its prompt asked for, so a done block names the file it
was actually baked from -- that, not the prompt's stem, is what to look for in
`models/` and on the bake command line.

## Why the budgets are so tight

Cost is per **placed block** and scales with how many of the thing exist, so
the budget is a property of the block rather than of the model. A quad costs
336 bytes and a cube element is 5-6 quads, so **one element is about 2 KB** of
chunk mesh against **1.3 KB for an entire plain block**.

The conduit is the cautionary tale. It came back at 57 elements, which is
61 KB for every conduit in the world -- roughly 47x a plain cube, for the
block laid in the largest numbers of anything in the game. The same silhouette
was available in about six elements with the rails and rivets painted on. That
is why each prompt states a hard cap and says twice over that detail belongs
in the texture.

## Animation plays only while a block is POWERED

A block that is not a power node parks on frame 0 forever, so an animation
strip on one is wasted sheet space. Only eleven blocks can animate at all:
Generator, Grinder, Distiller, Transmuter, Composter, Forge, Press, Sifter,
Glassblower, Compactor and Harvester. The prompts already account for this --
three of them ask for an 8-frame strip (Generator, Forge, Transmuter, whose
fire and runes should visibly move) and the other 39 forbid frames outright.

---

## A. Sources and nodes

These scatter across the island's whole outer band and are what the player
navigates by, so they are **bulk content on tiny budgets** - never
machine-sized. The six Sources share one silhouette and differ by colour and
crown: generate **Herb Source** first, then ask for each of the others as a
recolour of it, or the set will not hold together.

### Herb Bush

**Status: done** -- baked from `herb_bush.bbmodel`.

```
Output a Blockbench "Java Block/Item" project file (.bbmodel, model_format
"java_block") with the texture EMBEDDED in the file as a base64 PNG.

HARD RULES - a file breaking any of these is rejected by our importer:
- CUBE elements only. No mesh or poly elements of any kind.
- ALL geometry inside the 0..16 cell on every axis. It may touch a wall,
  never cross one.
- At most ONE rotation axis per element, and only the angles -45, -22.5, 0,
  22.5, 45. A rotated element collides as its bounding box, so do not rotate
  anything the player would stand on.
- The texture must be EXACTLY 16x16 pixels. Do NOT produce an
  animation strip or multiple frames - a texture a whole multiple
  taller is read as an animation and the extra frames are wasted.
- Alpha is BINARY: anything below 50% alpha is discarded, everything else is
  drawn fully opaque. Use alpha 0 or alpha 255 only. No anti-aliased edges, no
  semi-transparent glass, no soft shadows in the alpha channel. Transparency
  cuts holes; it does not fade.
- Box UV or per-face UV are both fine.

STYLE: chunky low-poly voxel-game block, flat shaded, pixel-art texture, no
gradients, no text, no logos. It sits on grass in daylight among other blocks
one metre across, and must read at 5-10 blocks distance.

DETAIL GOES IN THE TEXTURE, NOT IN GEOMETRY. Bolts, planks, panel lines,
grain, rust, dials, vents and engraving must be PAINTED. Model only what
changes the silhouette - the shape you would still recognise as a black
cut-out against the sky. This is the rule most often broken and the one that
matters most.

BUDGET: 4 cube elements MAXIMUM. This is a hard limit, not a target;
fewer is better. Exceeding it makes the model unusable.

MODEL: HERB BUSH

A low leafy herb bush growing from the ground. Build it from 3 or 4 FLAT
crossed planes (an element with zero thickness on one axis), not from boxes.
Dark green foliage with a few small pale flowers painted on. Cut the leaf
silhouette out of the texture with alpha 0 so it reads as leaves rather than
as flat cards.

Save the project as herb_bush.bbmodel
```

### Crystal Node

**Status: done** -- baked from `violet_crystal_cluster.bbmodel`.

```
Output a Blockbench "Java Block/Item" project file (.bbmodel, model_format
"java_block") with the texture EMBEDDED in the file as a base64 PNG.

HARD RULES - a file breaking any of these is rejected by our importer:
- CUBE elements only. No mesh or poly elements of any kind.
- ALL geometry inside the 0..16 cell on every axis. It may touch a wall,
  never cross one.
- At most ONE rotation axis per element, and only the angles -45, -22.5, 0,
  22.5, 45. A rotated element collides as its bounding box, so do not rotate
  anything the player would stand on.
- The texture must be EXACTLY 16x16 pixels. Do NOT produce an
  animation strip or multiple frames - a texture a whole multiple
  taller is read as an animation and the extra frames are wasted.
- Alpha is BINARY: anything below 50% alpha is discarded, everything else is
  drawn fully opaque. Use alpha 0 or alpha 255 only. No anti-aliased edges, no
  semi-transparent glass, no soft shadows in the alpha channel. Transparency
  cuts holes; it does not fade.
- Box UV or per-face UV are both fine.

STYLE: chunky low-poly voxel-game block, flat shaded, pixel-art texture, no
gradients, no text, no logos. It sits on grass in daylight among other blocks
one metre across, and must read at 5-10 blocks distance.

DETAIL GOES IN THE TEXTURE, NOT IN GEOMETRY. Bolts, planks, panel lines,
grain, rust, dials, vents and engraving must be PAINTED. Model only what
changes the silhouette - the shape you would still recognise as a black
cut-out against the sky. This is the rule most often broken and the one that
matters most.

BUDGET: 4 cube elements MAXIMUM. This is a hard limit, not a target;
fewer is better. Exceeding it makes the model unusable.

MODEL: CRYSTAL NODE

A cluster of 3 or 4 angular pale-violet crystal shards growing straight out
of the cell floor at slightly different heights and angles, the tallest about
10 units high. Facets painted as flat bands of lighter and darker violet.

Save the project as crystal_node.bbmodel
```

### Essence Vent

**Status: done** -- baked from `dark_stone_vent.bbmodel`.

```
Output a Blockbench "Java Block/Item" project file (.bbmodel, model_format
"java_block") with the texture EMBEDDED in the file as a base64 PNG.

HARD RULES - a file breaking any of these is rejected by our importer:
- CUBE elements only. No mesh or poly elements of any kind.
- ALL geometry inside the 0..16 cell on every axis. It may touch a wall,
  never cross one.
- At most ONE rotation axis per element, and only the angles -45, -22.5, 0,
  22.5, 45. A rotated element collides as its bounding box, so do not rotate
  anything the player would stand on.
- The texture must be EXACTLY 16x16 pixels. Do NOT produce an
  animation strip or multiple frames - a texture a whole multiple
  taller is read as an animation and the extra frames are wasted.
- Alpha is BINARY: anything below 50% alpha is discarded, everything else is
  drawn fully opaque. Use alpha 0 or alpha 255 only. No anti-aliased edges, no
  semi-transparent glass, no soft shadows in the alpha channel. Transparency
  cuts holes; it does not fade.
- Box UV or per-face UV are both fine.

STYLE: chunky low-poly voxel-game block, flat shaded, pixel-art texture, no
gradients, no text, no logos. It sits on grass in daylight among other blocks
one metre across, and must read at 5-10 blocks distance.

DETAIL GOES IN THE TEXTURE, NOT IN GEOMETRY. Bolts, planks, panel lines,
grain, rust, dials, vents and engraving must be PAINTED. Model only what
changes the silhouette - the shape you would still recognise as a black
cut-out against the sky. This is the rule most often broken and the one that
matters most.

BUDGET: 4 cube elements MAXIMUM. This is a hard limit, not a target;
fewer is better. Exceeding it makes the model unusable.

MODEL: ESSENCE VENT

A small dark stone vent in the ground: a low rough rim around a black
opening, with two short crooked spires leaning out of it. A faint blue glow
painted around the mouth.

Save the project as essence_vent.bbmodel
```

### Resonant Node

**Status: done** -- baked from `bicolour_crystal_node.bbmodel`.

```
Output a Blockbench "Java Block/Item" project file (.bbmodel, model_format
"java_block") with the texture EMBEDDED in the file as a base64 PNG.

HARD RULES - a file breaking any of these is rejected by our importer:
- CUBE elements only. No mesh or poly elements of any kind.
- ALL geometry inside the 0..16 cell on every axis. It may touch a wall,
  never cross one.
- At most ONE rotation axis per element, and only the angles -45, -22.5, 0,
  22.5, 45. A rotated element collides as its bounding box, so do not rotate
  anything the player would stand on.
- The texture must be EXACTLY 16x16 pixels. Do NOT produce an
  animation strip or multiple frames - a texture a whole multiple
  taller is read as an animation and the extra frames are wasted.
- Alpha is BINARY: anything below 50% alpha is discarded, everything else is
  drawn fully opaque. Use alpha 0 or alpha 255 only. No anti-aliased edges, no
  semi-transparent glass, no soft shadows in the alpha channel. Transparency
  cuts holes; it does not fade.
- Box UV or per-face UV are both fine.

STYLE: chunky low-poly voxel-game block, flat shaded, pixel-art texture, no
gradients, no text, no logos. It sits on grass in daylight among other blocks
one metre across, and must read at 5-10 blocks distance.

DETAIL GOES IN THE TEXTURE, NOT IN GEOMETRY. Bolts, planks, panel lines,
grain, rust, dials, vents and engraving must be PAINTED. Model only what
changes the silhouette - the shape you would still recognise as a black
cut-out against the sky. This is the rule most often broken and the one that
matters most.

BUDGET: 5 cube elements MAXIMUM. This is a hard limit, not a target;
fewer is better. Exceeding it makes the model unusable.

MODEL: RESONANT NODE

A cluster of angular crystal shards like a crystal node, but every shard is
banded in TWO colours - teal at the base turning to amber at the tip in hard
stripes - so it reads as two materials fused into one.

Save the project as resonant_node.bbmodel
```

### Herb Source

**Status: done** -- baked from `mossy_shrine_standing_stone.bbmodel`.

```
Output a Blockbench "Java Block/Item" project file (.bbmodel, model_format
"java_block") with the texture EMBEDDED in the file as a base64 PNG.

HARD RULES - a file breaking any of these is rejected by our importer:
- CUBE elements only. No mesh or poly elements of any kind.
- ALL geometry inside the 0..16 cell on every axis. It may touch a wall,
  never cross one.
- At most ONE rotation axis per element, and only the angles -45, -22.5, 0,
  22.5, 45. A rotated element collides as its bounding box, so do not rotate
  anything the player would stand on.
- The texture must be EXACTLY 16x16 pixels. Do NOT produce an
  animation strip or multiple frames - a texture a whole multiple
  taller is read as an animation and the extra frames are wasted.
- Alpha is BINARY: anything below 50% alpha is discarded, everything else is
  drawn fully opaque. Use alpha 0 or alpha 255 only. No anti-aliased edges, no
  semi-transparent glass, no soft shadows in the alpha channel. Transparency
  cuts holes; it does not fade.
- Box UV or per-face UV are both fine.

STYLE: chunky low-poly voxel-game block, flat shaded, pixel-art texture, no
gradients, no text, no logos. It sits on grass in daylight among other blocks
one metre across, and must read at 5-10 blocks distance.

DETAIL GOES IN THE TEXTURE, NOT IN GEOMETRY. Bolts, planks, panel lines,
grain, rust, dials, vents and engraving must be PAINTED. Model only what
changes the silhouette - the shape you would still recognise as a black
cut-out against the sky. This is the rule most often broken and the one that
matters most.

BUDGET: 6 cube elements MAXIMUM. This is a hard limit, not a target;
fewer is better. Exceeding it makes the model unusable.

MODEL: HERB SOURCE

A weathered mossy standing stone, roughly 8 wide by 12 tall by 8 deep,
sitting on the ground. Its top is carved into a shallow bowl holding a pool of
glowing green light. Moss and carved spiral runes painted on the sides. It
should read as a small shrine, not as a machine.

Save the project as herb_source.bbmodel
```

### Crystal Source

**Status: done** -- baked from `runed_standing_stone.bbmodel`.

```
Output a Blockbench "Java Block/Item" project file (.bbmodel, model_format
"java_block") with the texture EMBEDDED in the file as a base64 PNG.

HARD RULES - a file breaking any of these is rejected by our importer:
- CUBE elements only. No mesh or poly elements of any kind.
- ALL geometry inside the 0..16 cell on every axis. It may touch a wall,
  never cross one.
- At most ONE rotation axis per element, and only the angles -45, -22.5, 0,
  22.5, 45. A rotated element collides as its bounding box, so do not rotate
  anything the player would stand on.
- The texture must be EXACTLY 16x16 pixels. Do NOT produce an
  animation strip or multiple frames - a texture a whole multiple
  taller is read as an animation and the extra frames are wasted.
- Alpha is BINARY: anything below 50% alpha is discarded, everything else is
  drawn fully opaque. Use alpha 0 or alpha 255 only. No anti-aliased edges, no
  semi-transparent glass, no soft shadows in the alpha channel. Transparency
  cuts holes; it does not fade.
- Box UV or per-face UV are both fine.

STYLE: chunky low-poly voxel-game block, flat shaded, pixel-art texture, no
gradients, no text, no logos. It sits on grass in daylight among other blocks
one metre across, and must read at 5-10 blocks distance.

DETAIL GOES IN THE TEXTURE, NOT IN GEOMETRY. Bolts, planks, panel lines,
grain, rust, dials, vents and engraving must be PAINTED. Model only what
changes the silhouette - the shape you would still recognise as a black
cut-out against the sky. This is the rule most often broken and the one that
matters most.

BUDGET: 6 cube elements MAXIMUM. This is a hard limit, not a target;
fewer is better. Exceeding it makes the model unusable.

MODEL: CRYSTAL SOURCE

A weathered standing stone, roughly 8 wide by 12 tall by 8 deep, in pale
grey-violet stone with carved runes painted on the sides. Instead of a bowl,
its top holds a small cluster of glowing violet crystal.

Save the project as crystal_source.bbmodel
```

### Copper Source

**Status: done** -- baked from `verdigris_standing_stone.bbmodel`.

```
Output a Blockbench "Java Block/Item" project file (.bbmodel, model_format
"java_block") with the texture EMBEDDED in the file as a base64 PNG.

HARD RULES - a file breaking any of these is rejected by our importer:
- CUBE elements only. No mesh or poly elements of any kind.
- ALL geometry inside the 0..16 cell on every axis. It may touch a wall,
  never cross one.
- At most ONE rotation axis per element, and only the angles -45, -22.5, 0,
  22.5, 45. A rotated element collides as its bounding box, so do not rotate
  anything the player would stand on.
- The texture must be EXACTLY 16x16 pixels. Do NOT produce an
  animation strip or multiple frames - a texture a whole multiple
  taller is read as an animation and the extra frames are wasted.
- Alpha is BINARY: anything below 50% alpha is discarded, everything else is
  drawn fully opaque. Use alpha 0 or alpha 255 only. No anti-aliased edges, no
  semi-transparent glass, no soft shadows in the alpha channel. Transparency
  cuts holes; it does not fade.
- Box UV or per-face UV are both fine.

STYLE: chunky low-poly voxel-game block, flat shaded, pixel-art texture, no
gradients, no text, no logos. It sits on grass in daylight among other blocks
one metre across, and must read at 5-10 blocks distance.

DETAIL GOES IN THE TEXTURE, NOT IN GEOMETRY. Bolts, planks, panel lines,
grain, rust, dials, vents and engraving must be PAINTED. Model only what
changes the silhouette - the shape you would still recognise as a black
cut-out against the sky. This is the rule most often broken and the one that
matters most.

BUDGET: 6 cube elements MAXIMUM. This is a hard limit, not a target;
fewer is better. Exceeding it makes the model unusable.

MODEL: COPPER SOURCE

A weathered standing stone, roughly 8 wide by 12 tall by 8 deep, in grey
stone streaked with green verdigris and orange copper. A hammered metal band
runs around its middle and its top holds a pool of glowing orange light.

Save the project as copper_source.bbmodel
```

### Sand Source

**Status: done** -- baked from `sand_source.bbmodel`.

```
Output a Blockbench "Java Block/Item" project file (.bbmodel, model_format
"java_block") with the texture EMBEDDED in the file as a base64 PNG.

HARD RULES - a file breaking any of these is rejected by our importer:
- CUBE elements only. No mesh or poly elements of any kind.
- ALL geometry inside the 0..16 cell on every axis. It may touch a wall,
  never cross one.
- At most ONE rotation axis per element, and only the angles -45, -22.5, 0,
  22.5, 45. A rotated element collides as its bounding box, so do not rotate
  anything the player would stand on.
- The texture must be EXACTLY 16x16 pixels. Do NOT produce an
  animation strip or multiple frames - a texture a whole multiple
  taller is read as an animation and the extra frames are wasted.
- Alpha is BINARY: anything below 50% alpha is discarded, everything else is
  drawn fully opaque. Use alpha 0 or alpha 255 only. No anti-aliased edges, no
  semi-transparent glass, no soft shadows in the alpha channel. Transparency
  cuts holes; it does not fade.
- Box UV or per-face UV are both fine.

STYLE: chunky low-poly voxel-game block, flat shaded, pixel-art texture, no
gradients, no text, no logos. It sits on grass in daylight among other blocks
one metre across, and must read at 5-10 blocks distance.

DETAIL GOES IN THE TEXTURE, NOT IN GEOMETRY. Bolts, planks, panel lines,
grain, rust, dials, vents and engraving must be PAINTED. Model only what
changes the silhouette - the shape you would still recognise as a black
cut-out against the sky. This is the rule most often broken and the one that
matters most.

BUDGET: 6 cube elements MAXIMUM. This is a hard limit, not a target;
fewer is better. Exceeding it makes the model unusable.

MODEL: SAND SOURCE

A weathered standing stone, roughly 8 wide by 12 tall by 8 deep, in pale gold
sandstone with carved runes. A crack runs down one face with sand spilling out
of it, and its top holds a pool of glowing pale-gold light.

Save the project as sand_source.bbmodel
```

### Essence Source

**Status: done** -- baked from `essence_source.bbmodel`.

```
Output a Blockbench "Java Block/Item" project file (.bbmodel, model_format
"java_block") with the texture EMBEDDED in the file as a base64 PNG.

HARD RULES - a file breaking any of these is rejected by our importer:
- CUBE elements only. No mesh or poly elements of any kind.
- ALL geometry inside the 0..16 cell on every axis. It may touch a wall,
  never cross one.
- At most ONE rotation axis per element, and only the angles -45, -22.5, 0,
  22.5, 45. A rotated element collides as its bounding box, so do not rotate
  anything the player would stand on.
- The texture must be EXACTLY 16x16 pixels. Do NOT produce an
  animation strip or multiple frames - a texture a whole multiple
  taller is read as an animation and the extra frames are wasted.
- Alpha is BINARY: anything below 50% alpha is discarded, everything else is
  drawn fully opaque. Use alpha 0 or alpha 255 only. No anti-aliased edges, no
  semi-transparent glass, no soft shadows in the alpha channel. Transparency
  cuts holes; it does not fade.
- Box UV or per-face UV are both fine.

STYLE: chunky low-poly voxel-game block, flat shaded, pixel-art texture, no
gradients, no text, no logos. It sits on grass in daylight among other blocks
one metre across, and must read at 5-10 blocks distance.

DETAIL GOES IN THE TEXTURE, NOT IN GEOMETRY. Bolts, planks, panel lines,
grain, rust, dials, vents and engraving must be PAINTED. Model only what
changes the silhouette - the shape you would still recognise as a black
cut-out against the sky. This is the rule most often broken and the one that
matters most.

BUDGET: 6 cube elements MAXIMUM. This is a hard limit, not a target;
fewer is better. Exceeding it makes the model unusable.

MODEL: ESSENCE SOURCE

A weathered standing stone, roughly 8 wide by 12 tall by 8 deep, in deep
blue-black stone with carved runes. Its top holds a pool of glowing pale blue
light with a single small mote floating just above it.

Save the project as essence_source.bbmodel
```

### Resonant Source

**Status: done** -- baked from `resonant_source.bbmodel`.

```
Output a Blockbench "Java Block/Item" project file (.bbmodel, model_format
"java_block") with the texture EMBEDDED in the file as a base64 PNG.

HARD RULES - a file breaking any of these is rejected by our importer:
- CUBE elements only. No mesh or poly elements of any kind.
- ALL geometry inside the 0..16 cell on every axis. It may touch a wall,
  never cross one.
- At most ONE rotation axis per element, and only the angles -45, -22.5, 0,
  22.5, 45. A rotated element collides as its bounding box, so do not rotate
  anything the player would stand on.
- The texture must be EXACTLY 16x16 pixels. Do NOT produce an
  animation strip or multiple frames - a texture a whole multiple
  taller is read as an animation and the extra frames are wasted.
- Alpha is BINARY: anything below 50% alpha is discarded, everything else is
  drawn fully opaque. Use alpha 0 or alpha 255 only. No anti-aliased edges, no
  semi-transparent glass, no soft shadows in the alpha channel. Transparency
  cuts holes; it does not fade.
- Box UV or per-face UV are both fine.

STYLE: chunky low-poly voxel-game block, flat shaded, pixel-art texture, no
gradients, no text, no logos. It sits on grass in daylight among other blocks
one metre across, and must read at 5-10 blocks distance.

DETAIL GOES IN THE TEXTURE, NOT IN GEOMETRY. Bolts, planks, panel lines,
grain, rust, dials, vents and engraving must be PAINTED. Model only what
changes the silhouette - the shape you would still recognise as a black
cut-out against the sky. This is the rule most often broken and the one that
matters most.

BUDGET: 7 cube elements MAXIMUM. This is a hard limit, not a target;
fewer is better. Exceeding it makes the model unusable.

MODEL: RESONANT SOURCE

A weathered standing stone, roughly 8 wide by 12 tall by 8 deep, visibly
FUSED from two different stones: teal on one side, amber on the other, with a
hard jagged seam running straight down the middle of every face. Its top holds
a pool of light that is teal on one half and amber on the other. This is the
rarest of the set and should be the brightest.

Save the project as resonant_source.bbmodel
```

## B. Cheap wins

Placed in bulk, so these carry the smallest budgets in the file. Tilled and
Rich Soil are one element each - the whole model is a slab a pixel shy of full
height and everything that tells them apart is paint.

### Scaffold

**Status: done** -- baked from `timber_scaffold_frame.bbmodel`.

```
Output a Blockbench "Java Block/Item" project file (.bbmodel, model_format
"java_block") with the texture EMBEDDED in the file as a base64 PNG.

HARD RULES - a file breaking any of these is rejected by our importer:
- CUBE elements only. No mesh or poly elements of any kind.
- ALL geometry inside the 0..16 cell on every axis. It may touch a wall,
  never cross one.
- At most ONE rotation axis per element, and only the angles -45, -22.5, 0,
  22.5, 45. A rotated element collides as its bounding box, so do not rotate
  anything the player would stand on.
- The texture must be EXACTLY 16x16 pixels. Do NOT produce an
  animation strip or multiple frames - a texture a whole multiple
  taller is read as an animation and the extra frames are wasted.
- Alpha is BINARY: anything below 50% alpha is discarded, everything else is
  drawn fully opaque. Use alpha 0 or alpha 255 only. No anti-aliased edges, no
  semi-transparent glass, no soft shadows in the alpha channel. Transparency
  cuts holes; it does not fade.
- Box UV or per-face UV are both fine.

STYLE: chunky low-poly voxel-game block, flat shaded, pixel-art texture, no
gradients, no text, no logos. It sits on grass in daylight among other blocks
one metre across, and must read at 5-10 blocks distance.

DETAIL GOES IN THE TEXTURE, NOT IN GEOMETRY. Bolts, planks, panel lines,
grain, rust, dials, vents and engraving must be PAINTED. Model only what
changes the silhouette - the shape you would still recognise as a black
cut-out against the sky. This is the rule most often broken and the one that
matters most.

BUDGET: 8 cube elements MAXIMUM. This is a hard limit, not a target;
fewer is better. Exceeding it makes the model unusable.

MODEL: SCAFFOLD

An open cubic climbing frame: four corner posts of bare timber plus a rail
around the top and another around the bottom, completely hollow in the middle
so you can see straight through it. Lashings of twine painted at the joints.
If the rails can be painted onto the posts convincingly, use 4 elements.

Save the project as scaffold.bbmodel
```

### Tilled Soil

**Status: done** -- baked from `tilled_soil.bbmodel` (furrows on the top face
repainted by hand; the generator left them out).

```
Output a Blockbench "Java Block/Item" project file (.bbmodel, model_format
"java_block") with the texture EMBEDDED in the file as a base64 PNG.

HARD RULES - a file breaking any of these is rejected by our importer:
- CUBE elements only. No mesh or poly elements of any kind.
- ALL geometry inside the 0..16 cell on every axis. It may touch a wall,
  never cross one.
- At most ONE rotation axis per element, and only the angles -45, -22.5, 0,
  22.5, 45. A rotated element collides as its bounding box, so do not rotate
  anything the player would stand on.
- The texture is 64x32 pixels with BOX UV: that is exactly the unwrap of a
  16x15x16 box (a 16x16 texture cannot hold it). Do NOT produce an
  animation strip or multiple frames - a texture a whole multiple
  taller is read as an animation and the extra frames are wasted.
- Alpha is BINARY: anything below 50% alpha is discarded, everything else is
  drawn fully opaque. Use alpha 0 or alpha 255 only. No anti-aliased edges, no
  semi-transparent glass, no soft shadows in the alpha channel. Transparency
  cuts holes; it does not fade.
- Use box UV (see the texture rule above).

STYLE: chunky low-poly voxel-game block, flat shaded, pixel-art texture, no
gradients, no text, no logos. It sits on grass in daylight among other blocks
one metre across, and must read at 5-10 blocks distance.

DETAIL GOES IN THE TEXTURE, NOT IN GEOMETRY. Bolts, planks, panel lines,
grain, rust, dials, vents and engraving must be PAINTED. Model only what
changes the silhouette - the shape you would still recognise as a black
cut-out against the sky. This is the rule most often broken and the one that
matters most.

BUDGET: EXACTLY ONE cube element. Not "about one", not "one main body plus
details" - ONE. An earlier soil attempt came back with 44 elements (separate
boxes for ridges, furrows, clods and crumbling edges) and was rejected
outright: this block is laid by the hundred across farm fields, and every
extra box is paid once per block in the field. If you feel the urge to add a
second element, paint it instead. Before you answer, check that the elements
array has length 1.

MODEL: TILLED SOIL

ONE box filling the cell from 0,0,0 to 16,15,16 - full width and depth, one
unit shy of full height, so worked ground sits slightly below the grass around
it. Dark brown crumbly earth, with a loose broken edge painted on the sides.

THE TOP FACE MUST SHOW FURROWS. Paint four straight parallel furrows running
the full length of the top face: each a dark trough with a lit ridge beside
it, strong enough to read from ten blocks away. The furrows are the ONLY thing
that tells tilled ground apart from dirt at a glance; an earlier attempt
painted plain speckled earth on top and had to be repainted by hand.

Save the project as tilled_soil.bbmodel
```

### Rich Soil

**Status: done** -- baked from `rich_soil.bbmodel` (texture rebuilt from atlas tiles
194/195/2; the generated one was near-black with no furrows).

```
Make a Blockbench .bbmodel file (File > New > Java Block/Item) with the
texture embedded as a base64 PNG.

The model is EXACTLY ONE cube: from [0,0,0] to [16,15,16], no rotation.
Nothing else. No extra boxes for detail - this block is placed by the
hundred, so every detail must be painted on the texture.

Texture: 64x32 pixels, box UV, fully opaque (alpha 255 everywhere), flat
pixel art, no gradients, no text, one frame only.

Look: rich farmland soil. Almost black-brown earth with flecks of pale straw
and compost. The TOP face must show four straight parallel furrows running
its full length - dark troughs with lit ridges beside them, clear from ten
blocks away. The sides are the same dark earth with a slightly broken,
lighter lip along the top edge.

Save as rich_soil.bbmodel
```

### Grafted Sapling

**Status: done** -- baked from `grafted_sapling.bbmodel`.

```
Make a Blockbench .bbmodel file (File > New > Java Block/Item) with the
texture embedded as a base64 PNG.

AT MOST 12 cube elements, all inside the 0..16 cell. Rotations only on one
axis per element, only -45/-22.5/22.5/45. Detail is painted, not modelled -
this block may be planted by the dozen.

Texture: 32x32 pixels, flat pixel art, no gradients, no text, one frame only.
Alpha is 0 or 255 only - no soft edges.

Look: a young grafted tree, sturdier and fuller than a wild sapling. A
straight brown trunk about 3 units thick and 14 tall, four short branches
angling out near the top, and a leaf cluster on each branch. Make each leaf
cluster from two flat crossed planes (zero thickness on one axis) with the
leaf shape cut out using alpha 0. Paint a pale grafting band around the
trunk near its base.

Save as grafted_sapling.bbmodel
```

## C. The hand-cranked tier

Twelve tools the player's own arm drives, and the first machines a new player
meets. They are **hand tools on a bench, not machinery**: low, wooden, worn,
human-scale.

Every one carries a handle or crank in a group named `crank`, because a
cranked machine only advances while you turn it. That is the entire identity
of the tier and it should be visible before you read a tooltip.

None are power nodes, so **none may animate**.

### Sieve

**Status: done** -- baked from `sieve.bbmodel` (group renamed `handle` -> `crank`;
the screen's mesh holes painted by hand, it came back solid; screen and braces
raised 3 units so the mesh shows above the frame instead of hiding inside it).

```
Make a Blockbench .bbmodel file (File > New > Java Block/Item) with the
texture embedded as a base64 PNG.

AT MOST 14 cube elements, all inside the 0..16 cell. Rotations only on one
axis per element, only -45/-22.5/22.5/45. Detail is painted, not modelled.

Texture: 64x64 pixels, flat pixel art, no gradients, no text, one frame only.
Alpha is 0 or 255 only - no soft edges.

Look: a hand tool on a bench - low, wooden, worn, human-scale. A square
wooden frame holding a woven mesh screen, standing on four short legs, with
a handle on one side for shaking it. The mesh is ONE thin element whose
texture is a grid of alpha-0 holes, so you can see through it. Sawdust and
grain painted on the frame.

The handle goes in its own outliner group named exactly "crank", with the
group's origin where the handle meets the frame. Everything else stays in
the root.

Save as sieve.bbmodel
```

### Mortar

**Status: done** -- built by hand, not from this prompt: `mortar.bbmodel`, from
`tools/block_models/mortar.py`. See `ART_PROGRESS.md`.

```
Make a Blockbench .bbmodel file (File > New > Java Block/Item) with the
texture embedded as a base64 PNG.

AT MOST 14 cube elements, all inside the 0..16 cell. Rotations only on one
axis per element, only -45/-22.5/22.5/45. Detail is painted, not modelled.

Texture: 64x64 pixels, flat pixel art, no gradients, no text, one frame only.
Alpha is 0 or 255 only - no soft edges.

Look: a hand tool on a bench - low, worn, human-scale. A heavy grey stone
bowl sitting in a low wooden cradle, with an upright stone pestle resting
inside it. Ground powder painted around the rim.

The pestle goes in its own outliner group named exactly "crank" (not
"pestle", not "handle" - exactly "crank"), with the group's origin at the
bottom of the pestle where it meets the bowl. Everything else stays in the
root.

Save as mortar.bbmodel
```

### Hand Press

**Status: done** -- built with modelkit, not from this prompt: `hand_press.bbmodel`,
from `tools/block_models/hand_press.py`.

```
Output a Blockbench "Java Block/Item" project file (.bbmodel, model_format
"java_block") with the texture EMBEDDED in the file as a base64 PNG.

HARD RULES - a file breaking any of these is rejected by our importer:
- CUBE elements only. No mesh or poly elements of any kind.
- ALL geometry inside the 0..16 cell on every axis. It may touch a wall,
  never cross one.
- At most ONE rotation axis per element, and only the angles -45, -22.5, 0,
  22.5, 45. A rotated element collides as its bounding box, so do not rotate
  anything the player would stand on.
- The texture must be EXACTLY 16x16 pixels. Do NOT produce an
  animation strip or multiple frames - a texture a whole multiple
  taller is read as an animation and the extra frames are wasted.
- Alpha is BINARY: anything below 50% alpha is discarded, everything else is
  drawn fully opaque. Use alpha 0 or alpha 255 only. No anti-aliased edges, no
  semi-transparent glass, no soft shadows in the alpha channel. Transparency
  cuts holes; it does not fade.
- Box UV or per-face UV are both fine.

STYLE: chunky low-poly voxel-game block, flat shaded, pixel-art texture, no
gradients, no text, no logos. It sits on grass in daylight among other blocks
one metre across, and must read at 5-10 blocks distance.

DETAIL GOES IN THE TEXTURE, NOT IN GEOMETRY. Bolts, planks, panel lines,
grain, rust, dials, vents and engraving must be PAINTED. Model only what
changes the silhouette - the shape you would still recognise as a black
cut-out against the sky. This is the rule most often broken and the one that
matters most.

BUDGET: 14 cube elements MAXIMUM. This is a hard limit, not a target;
fewer is better. Exceeding it makes the model unusable.

MODEL: HAND PRESS

A screw press: a solid wooden bench, two upright posts, a threaded screw
running down between them into a flat pressing plate, and a straight bar
handle across the top of the screw.

MOVING PART: put the handle, crank, pestle, hammer, lever or wheel
in its own outliner group named exactly "crank", with that group's
origin at the centre of its axle.
Everything else can sit in the root group, and the exact group name
is what our code looks for.

Save the project as hand_press.bbmodel
```

### Anvil

**Status: done** -- built with modelkit, not from this prompt: `anvil.bbmodel`,
from `tools/block_models/anvil.py`.

```
Output a Blockbench "Java Block/Item" project file (.bbmodel, model_format
"java_block") with the texture EMBEDDED in the file as a base64 PNG.

HARD RULES - a file breaking any of these is rejected by our importer:
- CUBE elements only. No mesh or poly elements of any kind.
- ALL geometry inside the 0..16 cell on every axis. It may touch a wall,
  never cross one.
- At most ONE rotation axis per element, and only the angles -45, -22.5, 0,
  22.5, 45. A rotated element collides as its bounding box, so do not rotate
  anything the player would stand on.
- The texture must be EXACTLY 16x16 pixels. Do NOT produce an
  animation strip or multiple frames - a texture a whole multiple
  taller is read as an animation and the extra frames are wasted.
- Alpha is BINARY: anything below 50% alpha is discarded, everything else is
  drawn fully opaque. Use alpha 0 or alpha 255 only. No anti-aliased edges, no
  semi-transparent glass, no soft shadows in the alpha channel. Transparency
  cuts holes; it does not fade.
- Box UV or per-face UV are both fine.

STYLE: chunky low-poly voxel-game block, flat shaded, pixel-art texture, no
gradients, no text, no logos. It sits on grass in daylight among other blocks
one metre across, and must read at 5-10 blocks distance.

DETAIL GOES IN THE TEXTURE, NOT IN GEOMETRY. Bolts, planks, panel lines,
grain, rust, dials, vents and engraving must be PAINTED. Model only what
changes the silhouette - the shape you would still recognise as a black
cut-out against the sky. This is the rule most often broken and the one that
matters most.

BUDGET: 14 cube elements MAXIMUM. This is a hard limit, not a target;
fewer is better. Exceeding it makes the model unusable.

MODEL: ANVIL

A blackened iron anvil with the classic horn and waist, sitting on a scarred
wooden stump, with a hammer leaning against it. The hammer is the handle.
Scale and hammer marks painted on the face.

MOVING PART: put the handle, crank, pestle, hammer, lever or wheel
in its own outliner group named exactly "crank", with that group's
origin at the centre of its axle.
Everything else can sit in the root group, and the exact group name
is what our code looks for.

Save the project as anvil.bbmodel
```

### Blowpipe

**Status: done** -- built with modelkit, not from this prompt: `blowpipe.bbmodel`,
from `tools/block_models/blowpipe.py`.

```
Output a Blockbench "Java Block/Item" project file (.bbmodel, model_format
"java_block") with the texture EMBEDDED in the file as a base64 PNG.

HARD RULES - a file breaking any of these is rejected by our importer:
- CUBE elements only. No mesh or poly elements of any kind.
- ALL geometry inside the 0..16 cell on every axis. It may touch a wall,
  never cross one.
- At most ONE rotation axis per element, and only the angles -45, -22.5, 0,
  22.5, 45. A rotated element collides as its bounding box, so do not rotate
  anything the player would stand on.
- The texture must be EXACTLY 16x16 pixels. Do NOT produce an
  animation strip or multiple frames - a texture a whole multiple
  taller is read as an animation and the extra frames are wasted.
- Alpha is BINARY: anything below 50% alpha is discarded, everything else is
  drawn fully opaque. Use alpha 0 or alpha 255 only. No anti-aliased edges, no
  semi-transparent glass, no soft shadows in the alpha channel. Transparency
  cuts holes; it does not fade.
- Box UV or per-face UV are both fine.

STYLE: chunky low-poly voxel-game block, flat shaded, pixel-art texture, no
gradients, no text, no logos. It sits on grass in daylight among other blocks
one metre across, and must read at 5-10 blocks distance.

DETAIL GOES IN THE TEXTURE, NOT IN GEOMETRY. Bolts, planks, panel lines,
grain, rust, dials, vents and engraving must be PAINTED. Model only what
changes the silhouette - the shape you would still recognise as a black
cut-out against the sky. This is the rule most often broken and the one that
matters most.

BUDGET: 14 cube elements MAXIMUM. This is a hard limit, not a target;
fewer is better. Exceeding it makes the model unusable.

MODEL: BLOWPIPE

A small glassblower's bench: a low stand holding a long thin metal pipe
horizontally over a shallow bowl of glowing coals, with a bellows lever
sticking out of one side.

MOVING PART: put the handle, crank, pestle, hammer, lever or wheel
in its own outliner group named exactly "crank", with that group's
origin at the centre of its axle.
Everything else can sit in the root group, and the exact group name
is what our code looks for.

Save the project as blowpipe.bbmodel
```

### Tamper

**Status: done** -- built with modelkit, not from this prompt: `tamper.bbmodel`,
from `tools/block_models/tamper.py`.

```
Output a Blockbench "Java Block/Item" project file (.bbmodel, model_format
"java_block") with the texture EMBEDDED in the file as a base64 PNG.

HARD RULES - a file breaking any of these is rejected by our importer:
- CUBE elements only. No mesh or poly elements of any kind.
- ALL geometry inside the 0..16 cell on every axis. It may touch a wall,
  never cross one.
- At most ONE rotation axis per element, and only the angles -45, -22.5, 0,
  22.5, 45. A rotated element collides as its bounding box, so do not rotate
  anything the player would stand on.
- The texture must be EXACTLY 16x16 pixels. Do NOT produce an
  animation strip or multiple frames - a texture a whole multiple
  taller is read as an animation and the extra frames are wasted.
- Alpha is BINARY: anything below 50% alpha is discarded, everything else is
  drawn fully opaque. Use alpha 0 or alpha 255 only. No anti-aliased edges, no
  semi-transparent glass, no soft shadows in the alpha channel. Transparency
  cuts holes; it does not fade.
- Box UV or per-face UV are both fine.

STYLE: chunky low-poly voxel-game block, flat shaded, pixel-art texture, no
gradients, no text, no logos. It sits on grass in daylight among other blocks
one metre across, and must read at 5-10 blocks distance.

DETAIL GOES IN THE TEXTURE, NOT IN GEOMETRY. Bolts, planks, panel lines,
grain, rust, dials, vents and engraving must be PAINTED. Model only what
changes the silhouette - the shape you would still recognise as a black
cut-out against the sky. This is the rule most often broken and the one that
matters most.

BUDGET: 14 cube elements MAXIMUM. This is a hard limit, not a target;
fewer is better. Exceeding it makes the model unusable.

MODEL: TAMPER

A wide flat stone base plate with a heavy weighted iron rammer standing
upright on it and a T-shaped wooden handle across the top of the rammer.
Squat and obviously heavy.

MOVING PART: put the handle, crank, pestle, hammer, lever or wheel
in its own outliner group named exactly "crank", with that group's
origin at the centre of its axle.
Everything else can sit in the root group, and the exact group name
is what our code looks for.

Save the project as tamper.bbmodel
```

### Compost Heap

**Status: done** -- built with modelkit, not from this prompt: `compost_heap.bbmodel`,
from `tools/block_models/compost_heap.py`.

```
Output a Blockbench "Java Block/Item" project file (.bbmodel, model_format
"java_block") with the texture EMBEDDED in the file as a base64 PNG.

HARD RULES - a file breaking any of these is rejected by our importer:
- CUBE elements only. No mesh or poly elements of any kind.
- ALL geometry inside the 0..16 cell on every axis. It may touch a wall,
  never cross one.
- At most ONE rotation axis per element, and only the angles -45, -22.5, 0,
  22.5, 45. A rotated element collides as its bounding box, so do not rotate
  anything the player would stand on.
- The texture must be EXACTLY 16x16 pixels. Do NOT produce an
  animation strip or multiple frames - a texture a whole multiple
  taller is read as an animation and the extra frames are wasted.
- Alpha is BINARY: anything below 50% alpha is discarded, everything else is
  drawn fully opaque. Use alpha 0 or alpha 255 only. No anti-aliased edges, no
  semi-transparent glass, no soft shadows in the alpha channel. Transparency
  cuts holes; it does not fade.
- Box UV or per-face UV are both fine.

STYLE: chunky low-poly voxel-game block, flat shaded, pixel-art texture, no
gradients, no text, no logos. It sits on grass in daylight among other blocks
one metre across, and must read at 5-10 blocks distance.

DETAIL GOES IN THE TEXTURE, NOT IN GEOMETRY. Bolts, planks, panel lines,
grain, rust, dials, vents and engraving must be PAINTED. Model only what
changes the silhouette - the shape you would still recognise as a black
cut-out against the sky. This is the rule most often broken and the one that
matters most.

BUDGET: 14 cube elements MAXIMUM. This is a hard limit, not a target;
fewer is better. Exceeding it makes the model unusable.

MODEL: COMPOST HEAP

An open box of rough unplaned planks, half full of dark crumbling compost,
with a turning fork stuck upright into the heap. Straw and peelings painted
through the compost.

MOVING PART: put the handle, crank, pestle, hammer, lever or wheel
in its own outliner group named exactly "crank", with that group's
origin at the centre of its axle.
Everything else can sit in the root group, and the exact group name
is what our code looks for.

Save the project as compost_heap.bbmodel
```

### Mixing Bowl

**Status: done** -- built with modelkit, not from this prompt: `mixing_bowl.bbmodel`,
from `tools/block_models/mixing_bowl.py`.

```
Output a Blockbench "Java Block/Item" project file (.bbmodel, model_format
"java_block") with the texture EMBEDDED in the file as a base64 PNG.

HARD RULES - a file breaking any of these is rejected by our importer:
- CUBE elements only. No mesh or poly elements of any kind.
- ALL geometry inside the 0..16 cell on every axis. It may touch a wall,
  never cross one.
- At most ONE rotation axis per element, and only the angles -45, -22.5, 0,
  22.5, 45. A rotated element collides as its bounding box, so do not rotate
  anything the player would stand on.
- The texture must be EXACTLY 16x16 pixels. Do NOT produce an
  animation strip or multiple frames - a texture a whole multiple
  taller is read as an animation and the extra frames are wasted.
- Alpha is BINARY: anything below 50% alpha is discarded, everything else is
  drawn fully opaque. Use alpha 0 or alpha 255 only. No anti-aliased edges, no
  semi-transparent glass, no soft shadows in the alpha channel. Transparency
  cuts holes; it does not fade.
- Box UV or per-face UV are both fine.

STYLE: chunky low-poly voxel-game block, flat shaded, pixel-art texture, no
gradients, no text, no logos. It sits on grass in daylight among other blocks
one metre across, and must read at 5-10 blocks distance.

DETAIL GOES IN THE TEXTURE, NOT IN GEOMETRY. Bolts, planks, panel lines,
grain, rust, dials, vents and engraving must be PAINTED. Model only what
changes the silhouette - the shape you would still recognise as a black
cut-out against the sky. This is the rule most often broken and the one that
matters most.

BUDGET: 14 cube elements MAXIMUM. This is a hard limit, not a target;
fewer is better. Exceeding it makes the model unusable.

MODEL: MIXING BOWL

A wide shallow clay bowl resting on a three-legged wooden tripod, with a long
wooden stirring paddle laid across the rim. Greenish residue painted inside
the bowl.

MOVING PART: put the handle, crank, pestle, hammer, lever or wheel
in its own outliner group named exactly "crank", with that group's
origin at the centre of its axle.
Everything else can sit in the root group, and the exact group name
is what our code looks for.

Save the project as mixing_bowl.bbmodel
```

### Infusion Stand

**Status: done** -- built with modelkit, not from this prompt: `infusion_stand.bbmodel`,
from `tools/block_models/infusion_stand.py`.

```
Output a Blockbench "Java Block/Item" project file (.bbmodel, model_format
"java_block") with the texture EMBEDDED in the file as a base64 PNG.

HARD RULES - a file breaking any of these is rejected by our importer:
- CUBE elements only. No mesh or poly elements of any kind.
- ALL geometry inside the 0..16 cell on every axis. It may touch a wall,
  never cross one.
- At most ONE rotation axis per element, and only the angles -45, -22.5, 0,
  22.5, 45. A rotated element collides as its bounding box, so do not rotate
  anything the player would stand on.
- The texture must be EXACTLY 32x32 pixels. Do NOT produce an
  animation strip or multiple frames - a texture a whole multiple
  taller is read as an animation and the extra frames are wasted.
- Alpha is BINARY: anything below 50% alpha is discarded, everything else is
  drawn fully opaque. Use alpha 0 or alpha 255 only. No anti-aliased edges, no
  semi-transparent glass, no soft shadows in the alpha channel. Transparency
  cuts holes; it does not fade.
- Box UV or per-face UV are both fine.

STYLE: chunky low-poly voxel-game block, flat shaded, pixel-art texture, no
gradients, no text, no logos. It sits on grass in daylight among other blocks
one metre across, and must read at 5-10 blocks distance.

DETAIL GOES IN THE TEXTURE, NOT IN GEOMETRY. Bolts, planks, panel lines,
grain, rust, dials, vents and engraving must be PAINTED. Model only what
changes the silhouette - the shape you would still recognise as a black
cut-out against the sky. This is the rule most often broken and the one that
matters most.

BUDGET: 14 cube elements MAXIMUM. This is a hard limit, not a target;
fewer is better. Exceeding it makes the model unusable.

MODEL: INFUSION STAND

A slender wooden stand holding a small glass phial upright above an unlit
brass burner, with a little turning wheel on the side of the frame. Delicate
and apothecary-like.

MOVING PART: put the handle, crank, pestle, hammer, lever or wheel
in its own outliner group named exactly "crank", with that group's
origin at the centre of its axle.
Everything else can sit in the root group, and the exact group name
is what our code looks for.

Save the project as infusion_stand.bbmodel
```

### Still

**Status: done** -- built with modelkit, not from this prompt: `still.bbmodel`,
from `tools/block_models/still.py`.

```
Output a Blockbench "Java Block/Item" project file (.bbmodel, model_format
"java_block") with the texture EMBEDDED in the file as a base64 PNG.

HARD RULES - a file breaking any of these is rejected by our importer:
- CUBE elements only. No mesh or poly elements of any kind.
- ALL geometry inside the 0..16 cell on every axis. It may touch a wall,
  never cross one.
- At most ONE rotation axis per element, and only the angles -45, -22.5, 0,
  22.5, 45. A rotated element collides as its bounding box, so do not rotate
  anything the player would stand on.
- The texture must be EXACTLY 32x32 pixels. Do NOT produce an
  animation strip or multiple frames - a texture a whole multiple
  taller is read as an animation and the extra frames are wasted.
- Alpha is BINARY: anything below 50% alpha is discarded, everything else is
  drawn fully opaque. Use alpha 0 or alpha 255 only. No anti-aliased edges, no
  semi-transparent glass, no soft shadows in the alpha channel. Transparency
  cuts holes; it does not fade.
- Box UV or per-face UV are both fine.

STYLE: chunky low-poly voxel-game block, flat shaded, pixel-art texture, no
gradients, no text, no logos. It sits on grass in daylight among other blocks
one metre across, and must read at 5-10 blocks distance.

DETAIL GOES IN THE TEXTURE, NOT IN GEOMETRY. Bolts, planks, panel lines,
grain, rust, dials, vents and engraving must be PAINTED. Model only what
changes the silhouette - the shape you would still recognise as a black
cut-out against the sky. This is the rule most often broken and the one that
matters most.

BUDGET: 14 cube elements MAXIMUM. This is a hard limit, not a target;
fewer is better. Exceeding it makes the model unusable.

MODEL: STILL

A squat round copper pot with a domed lid, a curled copper arm running from
the lid down into a small collecting jar beside it, and a valve wheel on the
pot's side. Verdigris and hammer marks painted on the copper.

MOVING PART: put the handle, crank, pestle, hammer, lever or wheel
in its own outliner group named exactly "crank", with that group's
origin at the centre of its axle.
Everything else can sit in the root group, and the exact group name
is what our code looks for.

Save the project as still.bbmodel
```

### Hand Distiller

**Status: done** -- built with modelkit, not from this prompt: `hand_distiller.bbmodel`,
from `tools/block_models/hand_distiller.py`.

```
Output a Blockbench "Java Block/Item" project file (.bbmodel, model_format
"java_block") with the texture EMBEDDED in the file as a base64 PNG.

HARD RULES - a file breaking any of these is rejected by our importer:
- CUBE elements only. No mesh or poly elements of any kind.
- ALL geometry inside the 0..16 cell on every axis. It may touch a wall,
  never cross one.
- At most ONE rotation axis per element, and only the angles -45, -22.5, 0,
  22.5, 45. A rotated element collides as its bounding box, so do not rotate
  anything the player would stand on.
- The texture must be EXACTLY 32x32 pixels. Do NOT produce an
  animation strip or multiple frames - a texture a whole multiple
  taller is read as an animation and the extra frames are wasted.
- Alpha is BINARY: anything below 50% alpha is discarded, everything else is
  drawn fully opaque. Use alpha 0 or alpha 255 only. No anti-aliased edges, no
  semi-transparent glass, no soft shadows in the alpha channel. Transparency
  cuts holes; it does not fade.
- Box UV or per-face UV are both fine.

STYLE: chunky low-poly voxel-game block, flat shaded, pixel-art texture, no
gradients, no text, no logos. It sits on grass in daylight among other blocks
one metre across, and must read at 5-10 blocks distance.

DETAIL GOES IN THE TEXTURE, NOT IN GEOMETRY. Bolts, planks, panel lines,
grain, rust, dials, vents and engraving must be PAINTED. Model only what
changes the silhouette - the shape you would still recognise as a black
cut-out against the sky. This is the rule most often broken and the one that
matters most.

BUDGET: 14 cube elements MAXIMUM. This is a hard limit, not a target;
fewer is better. Exceeding it makes the model unusable.

MODEL: HAND DISTILLER

Taller and thinner than a still: a narrow upright copper column with two
collecting rings partway up it, a spout near the base feeding a small jar, and
a hand wheel at the bottom.

MOVING PART: put the handle, crank, pestle, hammer, lever or wheel
in its own outliner group named exactly "crank", with that group's
origin at the centre of its axle.
Everything else can sit in the root group, and the exact group name
is what our code looks for.

Save the project as hand_distiller.bbmodel
```

### Hand Transmuter

**Status: done** -- built with modelkit, not from this prompt: `hand_transmuter.bbmodel`,
from `tools/block_models/hand_transmuter.py`.

```
Output a Blockbench "Java Block/Item" project file (.bbmodel, model_format
"java_block") with the texture EMBEDDED in the file as a base64 PNG.

HARD RULES - a file breaking any of these is rejected by our importer:
- CUBE elements only. No mesh or poly elements of any kind.
- ALL geometry inside the 0..16 cell on every axis. It may touch a wall,
  never cross one.
- At most ONE rotation axis per element, and only the angles -45, -22.5, 0,
  22.5, 45. A rotated element collides as its bounding box, so do not rotate
  anything the player would stand on.
- The texture must be EXACTLY 32x32 pixels. Do NOT produce an
  animation strip or multiple frames - a texture a whole multiple
  taller is read as an animation and the extra frames are wasted.
- Alpha is BINARY: anything below 50% alpha is discarded, everything else is
  drawn fully opaque. Use alpha 0 or alpha 255 only. No anti-aliased edges, no
  semi-transparent glass, no soft shadows in the alpha channel. Transparency
  cuts holes; it does not fade.
- Box UV or per-face UV are both fine.

STYLE: chunky low-poly voxel-game block, flat shaded, pixel-art texture, no
gradients, no text, no logos. It sits on grass in daylight among other blocks
one metre across, and must read at 5-10 blocks distance.

DETAIL GOES IN THE TEXTURE, NOT IN GEOMETRY. Bolts, planks, panel lines,
grain, rust, dials, vents and engraving must be PAINTED. Model only what
changes the silhouette - the shape you would still recognise as a black
cut-out against the sky. This is the rule most often broken and the one that
matters most.

BUDGET: 14 cube elements MAXIMUM. This is a hard limit, not a target;
fewer is better. Exceeding it makes the model unusable.

MODEL: HAND TRANSMUTER

A thick stone slab carved with a circle and runes, a small dull crystal held
above its centre on a bent iron arm, and a hand wheel at the front of the
slab.

MOVING PART: put the handle, crank, pestle, hammer, lever or wheel
in its own outliner group named exactly "crank", with that group's
origin at the centre of its axle.
Everything else can sit in the root group, and the exact group name
is what our code looks for.

Save the project as hand_transmuter.bbmodel
```

## D. Fuel-fired

Heat rather than electricity. Not power nodes, so **no animation** - paint the
fire glow into the texture instead of trying to make it flicker.

### Bloomery

**Status: done** -- built with modelkit, not from this prompt: `bloomery.bbmodel`,
from `tools/block_models/bloomery.py`.

```
Output a Blockbench "Java Block/Item" project file (.bbmodel, model_format
"java_block") with the texture EMBEDDED in the file as a base64 PNG.

HARD RULES - a file breaking any of these is rejected by our importer:
- CUBE elements only. No mesh or poly elements of any kind.
- ALL geometry inside the 0..16 cell on every axis. It may touch a wall,
  never cross one.
- At most ONE rotation axis per element, and only the angles -45, -22.5, 0,
  22.5, 45. A rotated element collides as its bounding box, so do not rotate
  anything the player would stand on.
- The texture must be EXACTLY 32x32 pixels. Do NOT produce an
  animation strip or multiple frames - a texture a whole multiple
  taller is read as an animation and the extra frames are wasted.
- Alpha is BINARY: anything below 50% alpha is discarded, everything else is
  drawn fully opaque. Use alpha 0 or alpha 255 only. No anti-aliased edges, no
  semi-transparent glass, no soft shadows in the alpha channel. Transparency
  cuts holes; it does not fade.
- Box UV or per-face UV are both fine.

STYLE: chunky low-poly voxel-game block, flat shaded, pixel-art texture, no
gradients, no text, no logos. It sits on grass in daylight among other blocks
one metre across, and must read at 5-10 blocks distance.

DETAIL GOES IN THE TEXTURE, NOT IN GEOMETRY. Bolts, planks, panel lines,
grain, rust, dials, vents and engraving must be PAINTED. Model only what
changes the silhouette - the shape you would still recognise as a black
cut-out against the sky. This is the rule most often broken and the one that
matters most.

BUDGET: 16 cube elements MAXIMUM. This is a hard limit, not a target;
fewer is better. Exceeding it makes the model unusable.

MODEL: BLOOMERY

A waist-high smelting chimney of packed clay and stone, wider at the base and
narrowing toward the top, with an arched opening at the front glowing orange
and a dark slag scar running down one side. Cracked clay and soot painted on.
Primitive and hand-built, not manufactured.

Save the project as bloomery.bbmodel
```

### Furnace

**Status: done** -- built with modelkit, not from this prompt: `furnace.bbmodel`,
from `tools/block_models/furnace.py`.

```
Output a Blockbench "Java Block/Item" project file (.bbmodel, model_format
"java_block") with the texture EMBEDDED in the file as a base64 PNG.

HARD RULES - a file breaking any of these is rejected by our importer:
- CUBE elements only. No mesh or poly elements of any kind.
- ALL geometry inside the 0..16 cell on every axis. It may touch a wall,
  never cross one.
- At most ONE rotation axis per element, and only the angles -45, -22.5, 0,
  22.5, 45. A rotated element collides as its bounding box, so do not rotate
  anything the player would stand on.
- The texture must be EXACTLY 32x32 pixels. Do NOT produce an
  animation strip or multiple frames - a texture a whole multiple
  taller is read as an animation and the extra frames are wasted.
- Alpha is BINARY: anything below 50% alpha is discarded, everything else is
  drawn fully opaque. Use alpha 0 or alpha 255 only. No anti-aliased edges, no
  semi-transparent glass, no soft shadows in the alpha channel. Transparency
  cuts holes; it does not fade.
- Box UV or per-face UV are both fine.

STYLE: chunky low-poly voxel-game block, flat shaded, pixel-art texture, no
gradients, no text, no logos. It sits on grass in daylight among other blocks
one metre across, and must read at 5-10 blocks distance.

DETAIL GOES IN THE TEXTURE, NOT IN GEOMETRY. Bolts, planks, panel lines,
grain, rust, dials, vents and engraving must be PAINTED. Model only what
changes the silhouette - the shape you would still recognise as a black
cut-out against the sky. This is the rule most often broken and the one that
matters most.

BUDGET: 20 cube elements MAXIMUM. This is a hard limit, not a target;
fewer is better. Exceeding it makes the model unusable.

MODEL: FURNACE

A squat stone-and-iron furnace: a heavy rectangular body, an iron-barred door
at the front with orange fire glowing between the bars, a short chimney rising
from the back, and a riveted iron band around the body. Soot painted above the
door.

Save the project as furnace.bbmodel
```

## E. Powered machines

All are power nodes, so animation and moving parts both work, and both play
only while the network is satisfied. These carry the largest budgets here.

### Generator

**Status: done** -- built with modelkit, not from this prompt: `generator.bbmodel`,
from `tools/block_models/generator.py`.

```
Output a Blockbench "Java Block/Item" project file (.bbmodel, model_format
"java_block") with the texture EMBEDDED in the file as a base64 PNG.

HARD RULES - a file breaking any of these is rejected by our importer:
- CUBE elements only. No mesh or poly elements of any kind.
- ALL geometry inside the 0..16 cell on every axis. It may touch a wall,
  never cross one.
- At most ONE rotation axis per element, and only the angles -45, -22.5, 0,
  22.5, 45. A rotated element collides as its bounding box, so do not rotate
  anything the player would stand on.
- Each animation frame must be EXACTLY 32x32 pixels. Stack 8
  frames vertically so the whole image is 32x256, forming a
  looping animation strip.
- Alpha is BINARY: anything below 50% alpha is discarded, everything else is
  drawn fully opaque. Use alpha 0 or alpha 255 only. No anti-aliased edges, no
  semi-transparent glass, no soft shadows in the alpha channel. Transparency
  cuts holes; it does not fade.
- Box UV or per-face UV are both fine.

STYLE: chunky low-poly voxel-game block, flat shaded, pixel-art texture, no
gradients, no text, no logos. It sits on grass in daylight among other blocks
one metre across, and must read at 5-10 blocks distance.

DETAIL GOES IN THE TEXTURE, NOT IN GEOMETRY. Bolts, planks, panel lines,
grain, rust, dials, vents and engraving must be PAINTED. Model only what
changes the silhouette - the shape you would still recognise as a black
cut-out against the sky. This is the rule most often broken and the one that
matters most.

BUDGET: 28 cube elements MAXIMUM. This is a hard limit, not a target;
fewer is better. Exceeding it makes the model unusable.

MODEL: GENERATOR

An iron firebox on stubby legs with a glowing grate across the front, a large
flywheel mounted on one side, and a short exhaust stack on top. Rivets, soot
and warning stripes painted on. Across the 8 frames the grate should flicker
and brighten as if burning.

MOVING PART: put the flywheel
in its own outliner group named exactly "flywheel", with that group's
origin at the centre of the wheel.
Everything else can sit in the root group, and the exact group name
is what our code looks for.

Save the project as generator.bbmodel
```

### Grinder

**Status: done** -- built with modelkit, not from this prompt: `grinder.bbmodel`,
from `tools/block_models/grinder.py`.

```
Output a Blockbench "Java Block/Item" project file (.bbmodel, model_format
"java_block") with the texture EMBEDDED in the file as a base64 PNG.

HARD RULES - a file breaking any of these is rejected by our importer:
- CUBE elements only. No mesh or poly elements of any kind.
- ALL geometry inside the 0..16 cell on every axis. It may touch a wall,
  never cross one.
- At most ONE rotation axis per element, and only the angles -45, -22.5, 0,
  22.5, 45. A rotated element collides as its bounding box, so do not rotate
  anything the player would stand on.
- The texture must be EXACTLY 32x32 pixels. Do NOT produce an
  animation strip or multiple frames - a texture a whole multiple
  taller is read as an animation and the extra frames are wasted.
- Alpha is BINARY: anything below 50% alpha is discarded, everything else is
  drawn fully opaque. Use alpha 0 or alpha 255 only. No anti-aliased edges, no
  semi-transparent glass, no soft shadows in the alpha channel. Transparency
  cuts holes; it does not fade.
- Box UV or per-face UV are both fine.

STYLE: chunky low-poly voxel-game block, flat shaded, pixel-art texture, no
gradients, no text, no logos. It sits on grass in daylight among other blocks
one metre across, and must read at 5-10 blocks distance.

DETAIL GOES IN THE TEXTURE, NOT IN GEOMETRY. Bolts, planks, panel lines,
grain, rust, dials, vents and engraving must be PAINTED. Model only what
changes the silhouette - the shape you would still recognise as a black
cut-out against the sky. This is the rule most often broken and the one that
matters most.

BUDGET: 28 cube elements MAXIMUM. This is a hard limit, not a target;
fewer is better. Exceeding it makes the model unusable.

MODEL: GRINDER

A heavy iron frame holding a wide grinding wheel of grey stone on a horizontal
axle, with a hopper above feeding into it and a chute at the front letting
powder out.

MOVING PART: put the grinding wheel
in its own outliner group named exactly "wheel", with that group's
origin at the centre of the wheel's axle.
Everything else can sit in the root group, and the exact group name
is what our code looks for.

Save the project as grinder.bbmodel
```

### Press

**Status: not started.**

```
Output a Blockbench "Java Block/Item" project file (.bbmodel, model_format
"java_block") with the texture EMBEDDED in the file as a base64 PNG.

HARD RULES - a file breaking any of these is rejected by our importer:
- CUBE elements only. No mesh or poly elements of any kind.
- ALL geometry inside the 0..16 cell on every axis. It may touch a wall,
  never cross one.
- At most ONE rotation axis per element, and only the angles -45, -22.5, 0,
  22.5, 45. A rotated element collides as its bounding box, so do not rotate
  anything the player would stand on.
- The texture must be EXACTLY 32x32 pixels. Do NOT produce an
  animation strip or multiple frames - a texture a whole multiple
  taller is read as an animation and the extra frames are wasted.
- Alpha is BINARY: anything below 50% alpha is discarded, everything else is
  drawn fully opaque. Use alpha 0 or alpha 255 only. No anti-aliased edges, no
  semi-transparent glass, no soft shadows in the alpha channel. Transparency
  cuts holes; it does not fade.
- Box UV or per-face UV are both fine.

STYLE: chunky low-poly voxel-game block, flat shaded, pixel-art texture, no
gradients, no text, no logos. It sits on grass in daylight among other blocks
one metre across, and must read at 5-10 blocks distance.

DETAIL GOES IN THE TEXTURE, NOT IN GEOMETRY. Bolts, planks, panel lines,
grain, rust, dials, vents and engraving must be PAINTED. Model only what
changes the silhouette - the shape you would still recognise as a black
cut-out against the sky. This is the rule most often broken and the one that
matters most.

BUDGET: 28 cube elements MAXIMUM. This is a hard limit, not a target;
fewer is better. Exceeding it makes the model unusable.

MODEL: PRESS

A blocky iron press: a thick base plate, two heavy uprights at the sides, and
a broad flat ram head suspended between them above the plate. Hydraulic lines
and pressure dials painted on the uprights.

MOVING PART: put the ram head
in its own outliner group named exactly "ram", with that group's
origin at the TOP face of the ram, so it can drive downward.
Everything else can sit in the root group, and the exact group name
is what our code looks for.

Save the project as press.bbmodel
```

### Forge

**Status: not started.**

```
Output a Blockbench "Java Block/Item" project file (.bbmodel, model_format
"java_block") with the texture EMBEDDED in the file as a base64 PNG.

HARD RULES - a file breaking any of these is rejected by our importer:
- CUBE elements only. No mesh or poly elements of any kind.
- ALL geometry inside the 0..16 cell on every axis. It may touch a wall,
  never cross one.
- At most ONE rotation axis per element, and only the angles -45, -22.5, 0,
  22.5, 45. A rotated element collides as its bounding box, so do not rotate
  anything the player would stand on.
- Each animation frame must be EXACTLY 32x32 pixels. Stack 8
  frames vertically so the whole image is 32x256, forming a
  looping animation strip.
- Alpha is BINARY: anything below 50% alpha is discarded, everything else is
  drawn fully opaque. Use alpha 0 or alpha 255 only. No anti-aliased edges, no
  semi-transparent glass, no soft shadows in the alpha channel. Transparency
  cuts holes; it does not fade.
- Box UV or per-face UV are both fine.

STYLE: chunky low-poly voxel-game block, flat shaded, pixel-art texture, no
gradients, no text, no logos. It sits on grass in daylight among other blocks
one metre across, and must read at 5-10 blocks distance.

DETAIL GOES IN THE TEXTURE, NOT IN GEOMETRY. Bolts, planks, panel lines,
grain, rust, dials, vents and engraving must be PAINTED. Model only what
changes the silhouette - the shape you would still recognise as a black
cut-out against the sky. This is the rule most often broken and the one that
matters most.

BUDGET: 32 cube elements MAXIMUM. This is a hard limit, not a target;
fewer is better. Exceeding it makes the model unusable.

MODEL: FORGE

An armourer's forge: a stone hearth on an iron frame with a glowing bed of
coals, a metal hood above it, and a small anvil on a shelf at one side. Tongs
and a hammer painted on the frame. Across the 8 frames the coal bed should
pulse from dull red to bright orange.

Save the project as forge.bbmodel
```

### Sifter

**Status: not started.**

```
Output a Blockbench "Java Block/Item" project file (.bbmodel, model_format
"java_block") with the texture EMBEDDED in the file as a base64 PNG.

HARD RULES - a file breaking any of these is rejected by our importer:
- CUBE elements only. No mesh or poly elements of any kind.
- ALL geometry inside the 0..16 cell on every axis. It may touch a wall,
  never cross one.
- At most ONE rotation axis per element, and only the angles -45, -22.5, 0,
  22.5, 45. A rotated element collides as its bounding box, so do not rotate
  anything the player would stand on.
- The texture must be EXACTLY 32x32 pixels. Do NOT produce an
  animation strip or multiple frames - a texture a whole multiple
  taller is read as an animation and the extra frames are wasted.
- Alpha is BINARY: anything below 50% alpha is discarded, everything else is
  drawn fully opaque. Use alpha 0 or alpha 255 only. No anti-aliased edges, no
  semi-transparent glass, no soft shadows in the alpha channel. Transparency
  cuts holes; it does not fade.
- Box UV or per-face UV are both fine.

STYLE: chunky low-poly voxel-game block, flat shaded, pixel-art texture, no
gradients, no text, no logos. It sits on grass in daylight among other blocks
one metre across, and must read at 5-10 blocks distance.

DETAIL GOES IN THE TEXTURE, NOT IN GEOMETRY. Bolts, planks, panel lines,
grain, rust, dials, vents and engraving must be PAINTED. Model only what
changes the silhouette - the shape you would still recognise as a black
cut-out against the sky. This is the rule most often broken and the one that
matters most.

BUDGET: 28 cube elements MAXIMUM. This is a hard limit, not a target;
fewer is better. Exceeding it makes the model unusable.

MODEL: SIFTER

A boxy iron frame holding a wide sieve tray slung at a slight angle, with a
hopper above it and a collecting pan below. Springs painted where the tray
meets the frame.

MOVING PART: put the sieve tray
in its own outliner group named exactly "tray", with that group's
origin at the centre of the tray.
Everything else can sit in the root group, and the exact group name
is what our code looks for.

Save the project as sifter.bbmodel
```

### Glassblower

**Status: not started.**

```
Output a Blockbench "Java Block/Item" project file (.bbmodel, model_format
"java_block") with the texture EMBEDDED in the file as a base64 PNG.

HARD RULES - a file breaking any of these is rejected by our importer:
- CUBE elements only. No mesh or poly elements of any kind.
- ALL geometry inside the 0..16 cell on every axis. It may touch a wall,
  never cross one.
- At most ONE rotation axis per element, and only the angles -45, -22.5, 0,
  22.5, 45. A rotated element collides as its bounding box, so do not rotate
  anything the player would stand on.
- The texture must be EXACTLY 32x32 pixels. Do NOT produce an
  animation strip or multiple frames - a texture a whole multiple
  taller is read as an animation and the extra frames are wasted.
- Alpha is BINARY: anything below 50% alpha is discarded, everything else is
  drawn fully opaque. Use alpha 0 or alpha 255 only. No anti-aliased edges, no
  semi-transparent glass, no soft shadows in the alpha channel. Transparency
  cuts holes; it does not fade.
- Box UV or per-face UV are both fine.

STYLE: chunky low-poly voxel-game block, flat shaded, pixel-art texture, no
gradients, no text, no logos. It sits on grass in daylight among other blocks
one metre across, and must read at 5-10 blocks distance.

DETAIL GOES IN THE TEXTURE, NOT IN GEOMETRY. Bolts, planks, panel lines,
grain, rust, dials, vents and engraving must be PAINTED. Model only what
changes the silhouette - the shape you would still recognise as a black
cut-out against the sky. This is the rule most often broken and the one that
matters most.

BUDGET: 28 cube elements MAXIMUM. This is a hard limit, not a target;
fewer is better. Exceeding it makes the model unusable.

MODEL: GLASSBLOWER

A small round furnace with a glowing circular port at the front, a swinging
arm above it holding a blowpipe out over the port, and a cooling rack at one
side.

MOVING PART: put the swing arm holding the blowpipe
in its own outliner group named exactly "arm", with that group's
origin at the arm's shoulder joint.
Everything else can sit in the root group, and the exact group name
is what our code looks for.

Save the project as glassblower.bbmodel
```

### Compactor

**Status: not started.**

```
Output a Blockbench "Java Block/Item" project file (.bbmodel, model_format
"java_block") with the texture EMBEDDED in the file as a base64 PNG.

HARD RULES - a file breaking any of these is rejected by our importer:
- CUBE elements only. No mesh or poly elements of any kind.
- ALL geometry inside the 0..16 cell on every axis. It may touch a wall,
  never cross one.
- At most ONE rotation axis per element, and only the angles -45, -22.5, 0,
  22.5, 45. A rotated element collides as its bounding box, so do not rotate
  anything the player would stand on.
- The texture must be EXACTLY 32x32 pixels. Do NOT produce an
  animation strip or multiple frames - a texture a whole multiple
  taller is read as an animation and the extra frames are wasted.
- Alpha is BINARY: anything below 50% alpha is discarded, everything else is
  drawn fully opaque. Use alpha 0 or alpha 255 only. No anti-aliased edges, no
  semi-transparent glass, no soft shadows in the alpha channel. Transparency
  cuts holes; it does not fade.
- Box UV or per-face UV are both fine.

STYLE: chunky low-poly voxel-game block, flat shaded, pixel-art texture, no
gradients, no text, no logos. It sits on grass in daylight among other blocks
one metre across, and must read at 5-10 blocks distance.

DETAIL GOES IN THE TEXTURE, NOT IN GEOMETRY. Bolts, planks, panel lines,
grain, rust, dials, vents and engraving must be PAINTED. Model only what
changes the silhouette - the shape you would still recognise as a black
cut-out against the sky. This is the rule most often broken and the one that
matters most.

BUDGET: 28 cube elements MAXIMUM. This is a hard limit, not a target;
fewer is better. Exceeding it makes the model unusable.

MODEL: COMPACTOR

A squat, obviously very heavy iron machine: a thick body with a recessed
square pressing plate on top and four thick guide posts at the corners. Hazard
stripes and dents painted on.

MOVING PART: put the pressing plate
in its own outliner group named exactly "plate", with that group's
origin at the TOP face of the plate, so it can press down.
Everything else can sit in the root group, and the exact group name
is what our code looks for.

Save the project as compactor.bbmodel
```

### Composter

**Status: not started.**

```
Output a Blockbench "Java Block/Item" project file (.bbmodel, model_format
"java_block") with the texture EMBEDDED in the file as a base64 PNG.

HARD RULES - a file breaking any of these is rejected by our importer:
- CUBE elements only. No mesh or poly elements of any kind.
- ALL geometry inside the 0..16 cell on every axis. It may touch a wall,
  never cross one.
- At most ONE rotation axis per element, and only the angles -45, -22.5, 0,
  22.5, 45. A rotated element collides as its bounding box, so do not rotate
  anything the player would stand on.
- The texture must be EXACTLY 32x32 pixels. Do NOT produce an
  animation strip or multiple frames - a texture a whole multiple
  taller is read as an animation and the extra frames are wasted.
- Alpha is BINARY: anything below 50% alpha is discarded, everything else is
  drawn fully opaque. Use alpha 0 or alpha 255 only. No anti-aliased edges, no
  semi-transparent glass, no soft shadows in the alpha channel. Transparency
  cuts holes; it does not fade.
- Box UV or per-face UV are both fine.

STYLE: chunky low-poly voxel-game block, flat shaded, pixel-art texture, no
gradients, no text, no logos. It sits on grass in daylight among other blocks
one metre across, and must read at 5-10 blocks distance.

DETAIL GOES IN THE TEXTURE, NOT IN GEOMETRY. Bolts, planks, panel lines,
grain, rust, dials, vents and engraving must be PAINTED. Model only what
changes the silhouette - the shape you would still recognise as a black
cut-out against the sky. This is the rule most often broken and the one that
matters most.

BUDGET: 28 cube elements MAXIMUM. This is a hard limit, not a target;
fewer is better. Exceeding it makes the model unusable.

MODEL: COMPOSTER

A slatted wooden drum lying on its side in an iron cradle, with a hinged hatch
on the drum's surface and dark compost visible through gaps between the slats.
Iron hoops painted around the drum.

MOVING PART: put the drum
in its own outliner group named exactly "drum", with that group's
origin at the centre line of the drum, so it can roll.
Everything else can sit in the root group, and the exact group name
is what our code looks for.

Save the project as composter.bbmodel
```

### Distiller

**Status: not started.**

```
Output a Blockbench "Java Block/Item" project file (.bbmodel, model_format
"java_block") with the texture EMBEDDED in the file as a base64 PNG.

HARD RULES - a file breaking any of these is rejected by our importer:
- CUBE elements only. No mesh or poly elements of any kind.
- ALL geometry inside the 0..16 cell on every axis. It may touch a wall,
  never cross one.
- At most ONE rotation axis per element, and only the angles -45, -22.5, 0,
  22.5, 45. A rotated element collides as its bounding box, so do not rotate
  anything the player would stand on.
- The texture must be EXACTLY 32x32 pixels. Do NOT produce an
  animation strip or multiple frames - a texture a whole multiple
  taller is read as an animation and the extra frames are wasted.
- Alpha is BINARY: anything below 50% alpha is discarded, everything else is
  drawn fully opaque. Use alpha 0 or alpha 255 only. No anti-aliased edges, no
  semi-transparent glass, no soft shadows in the alpha channel. Transparency
  cuts holes; it does not fade.
- Box UV or per-face UV are both fine.

STYLE: chunky low-poly voxel-game block, flat shaded, pixel-art texture, no
gradients, no text, no logos. It sits on grass in daylight among other blocks
one metre across, and must read at 5-10 blocks distance.

DETAIL GOES IN THE TEXTURE, NOT IN GEOMETRY. Bolts, planks, panel lines,
grain, rust, dials, vents and engraving must be PAINTED. Model only what
changes the silhouette - the shape you would still recognise as a black
cut-out against the sky. This is the rule most often broken and the one that
matters most.

BUDGET: 32 cube elements MAXIMUM. This is a hard limit, not a target;
fewer is better. Exceeding it makes the model unusable.

MODEL: DISTILLER

A tall copper distillation column with three bulbs stacked up it, a coiled
condenser spiralling down one side into a collecting vessel at the base, and a
pressure gauge painted on the lowest bulb.

Save the project as distiller.bbmodel
```

### Transmuter

**Status: not started.**

```
Output a Blockbench "Java Block/Item" project file (.bbmodel, model_format
"java_block") with the texture EMBEDDED in the file as a base64 PNG.

HARD RULES - a file breaking any of these is rejected by our importer:
- CUBE elements only. No mesh or poly elements of any kind.
- ALL geometry inside the 0..16 cell on every axis. It may touch a wall,
  never cross one.
- At most ONE rotation axis per element, and only the angles -45, -22.5, 0,
  22.5, 45. A rotated element collides as its bounding box, so do not rotate
  anything the player would stand on.
- Each animation frame must be EXACTLY 32x32 pixels. Stack 8
  frames vertically so the whole image is 32x256, forming a
  looping animation strip.
- Alpha is BINARY: anything below 50% alpha is discarded, everything else is
  drawn fully opaque. Use alpha 0 or alpha 255 only. No anti-aliased edges, no
  semi-transparent glass, no soft shadows in the alpha channel. Transparency
  cuts holes; it does not fade.
- Box UV or per-face UV are both fine.

STYLE: chunky low-poly voxel-game block, flat shaded, pixel-art texture, no
gradients, no text, no logos. It sits on grass in daylight among other blocks
one metre across, and must read at 5-10 blocks distance.

DETAIL GOES IN THE TEXTURE, NOT IN GEOMETRY. Bolts, planks, panel lines,
grain, rust, dials, vents and engraving must be PAINTED. Model only what
changes the silhouette - the shape you would still recognise as a black
cut-out against the sky. This is the rule most often broken and the one that
matters most.

BUDGET: 32 cube elements MAXIMUM. This is a hard limit, not a target;
fewer is better. Exceeding it makes the model unusable.

MODEL: TRANSMUTER

A dark stone pedestal carved with concentric rings, three curved iron arms
rising from it, and a faceted crystal floating in the gap between their tips,
touching nothing. Glowing runes painted on the pedestal. Across the 8 frames
the runes should brighten and dim.

MOVING PART: put the floating crystal
in its own outliner group named exactly "crystal", with that group's
origin at the centre of the crystal.
Everything else can sit in the root group, and the exact group name
is what our code looks for.

Save the project as transmuter.bbmodel
```

### Harvester

**Status: not started.**

```
Output a Blockbench "Java Block/Item" project file (.bbmodel, model_format
"java_block") with the texture EMBEDDED in the file as a base64 PNG.

HARD RULES - a file breaking any of these is rejected by our importer:
- CUBE elements only. No mesh or poly elements of any kind.
- ALL geometry inside the 0..16 cell on every axis. It may touch a wall,
  never cross one.
- At most ONE rotation axis per element, and only the angles -45, -22.5, 0,
  22.5, 45. A rotated element collides as its bounding box, so do not rotate
  anything the player would stand on.
- The texture must be EXACTLY 32x32 pixels. Do NOT produce an
  animation strip or multiple frames - a texture a whole multiple
  taller is read as an animation and the extra frames are wasted.
- Alpha is BINARY: anything below 50% alpha is discarded, everything else is
  drawn fully opaque. Use alpha 0 or alpha 255 only. No anti-aliased edges, no
  semi-transparent glass, no soft shadows in the alpha channel. Transparency
  cuts holes; it does not fade.
- Box UV or per-face UV are both fine.

STYLE: chunky low-poly voxel-game block, flat shaded, pixel-art texture, no
gradients, no text, no logos. It sits on grass in daylight among other blocks
one metre across, and must read at 5-10 blocks distance.

DETAIL GOES IN THE TEXTURE, NOT IN GEOMETRY. Bolts, planks, panel lines,
grain, rust, dials, vents and engraving must be PAINTED. Model only what
changes the silhouette - the shape you would still recognise as a black
cut-out against the sky. This is the rule most often broken and the one that
matters most.

BUDGET: 28 cube elements MAXIMUM. This is a hard limit, not a target;
fewer is better. Exceeding it makes the model unusable.

MODEL: HARVESTER

A low wheeled iron frame with a horizontal cutting reel of thin blades across
the front and a collecting box behind it. Cut stalks and green stains painted
on the blades.

MOVING PART: put the cutting reel
in its own outliner group named exactly "reel", with that group's
origin at the centre of the reel's axle.
Everything else can sit in the root group, and the exact group name
is what our code looks for.

Save the project as harvester.bbmodel
```

## F. Unpowered utility

No power, so no animation and no motion. The Storage Crate gets the smallest
budget of the three because it is the one that gets stacked in rows.

### Rain Barrel

**Status: not started.**

```
Output a Blockbench "Java Block/Item" project file (.bbmodel, model_format
"java_block") with the texture EMBEDDED in the file as a base64 PNG.

HARD RULES - a file breaking any of these is rejected by our importer:
- CUBE elements only. No mesh or poly elements of any kind.
- ALL geometry inside the 0..16 cell on every axis. It may touch a wall,
  never cross one.
- At most ONE rotation axis per element, and only the angles -45, -22.5, 0,
  22.5, 45. A rotated element collides as its bounding box, so do not rotate
  anything the player would stand on.
- The texture must be EXACTLY 16x16 pixels. Do NOT produce an
  animation strip or multiple frames - a texture a whole multiple
  taller is read as an animation and the extra frames are wasted.
- Alpha is BINARY: anything below 50% alpha is discarded, everything else is
  drawn fully opaque. Use alpha 0 or alpha 255 only. No anti-aliased edges, no
  semi-transparent glass, no soft shadows in the alpha channel. Transparency
  cuts holes; it does not fade.
- Box UV or per-face UV are both fine.

STYLE: chunky low-poly voxel-game block, flat shaded, pixel-art texture, no
gradients, no text, no logos. It sits on grass in daylight among other blocks
one metre across, and must read at 5-10 blocks distance.

DETAIL GOES IN THE TEXTURE, NOT IN GEOMETRY. Bolts, planks, panel lines,
grain, rust, dials, vents and engraving must be PAINTED. Model only what
changes the silhouette - the shape you would still recognise as a black
cut-out against the sky. This is the rule most often broken and the one that
matters most.

BUDGET: 14 cube elements MAXIMUM. This is a hard limit, not a target;
fewer is better. Exceeding it makes the model unusable.

MODEL: RAIN BARREL

An open-topped wooden barrel made of curved staves bound with two iron hoops,
with dark water visible near the top rim. Water stains and damp wood painted
on the staves.

Save the project as rain_barrel.bbmodel
```

### Storage Crate

**Status: not started.**

```
Output a Blockbench "Java Block/Item" project file (.bbmodel, model_format
"java_block") with the texture EMBEDDED in the file as a base64 PNG.

HARD RULES - a file breaking any of these is rejected by our importer:
- CUBE elements only. No mesh or poly elements of any kind.
- ALL geometry inside the 0..16 cell on every axis. It may touch a wall,
  never cross one.
- At most ONE rotation axis per element, and only the angles -45, -22.5, 0,
  22.5, 45. A rotated element collides as its bounding box, so do not rotate
  anything the player would stand on.
- The texture must be EXACTLY 16x16 pixels. Do NOT produce an
  animation strip or multiple frames - a texture a whole multiple
  taller is read as an animation and the extra frames are wasted.
- Alpha is BINARY: anything below 50% alpha is discarded, everything else is
  drawn fully opaque. Use alpha 0 or alpha 255 only. No anti-aliased edges, no
  semi-transparent glass, no soft shadows in the alpha channel. Transparency
  cuts holes; it does not fade.
- Box UV or per-face UV are both fine.

STYLE: chunky low-poly voxel-game block, flat shaded, pixel-art texture, no
gradients, no text, no logos. It sits on grass in daylight among other blocks
one metre across, and must read at 5-10 blocks distance.

DETAIL GOES IN THE TEXTURE, NOT IN GEOMETRY. Bolts, planks, panel lines,
grain, rust, dials, vents and engraving must be PAINTED. Model only what
changes the silhouette - the shape you would still recognise as a black
cut-out against the sky. This is the rule most often broken and the one that
matters most.

BUDGET: 8 cube elements MAXIMUM. This is a hard limit, not a target;
fewer is better. Exceeding it makes the model unusable.

MODEL: STORAGE CRATE

A sturdy wooden shipping crate with a lid. Model the box and its lid only -
the planks, iron corner brackets, nails and rope handle must all be PAINTED,
because these get stacked in rows and every element is paid for many times
over.

Save the project as storage_crate.bbmodel
```

### Irrigator

**Status: not started.**

```
Output a Blockbench "Java Block/Item" project file (.bbmodel, model_format
"java_block") with the texture EMBEDDED in the file as a base64 PNG.

HARD RULES - a file breaking any of these is rejected by our importer:
- CUBE elements only. No mesh or poly elements of any kind.
- ALL geometry inside the 0..16 cell on every axis. It may touch a wall,
  never cross one.
- At most ONE rotation axis per element, and only the angles -45, -22.5, 0,
  22.5, 45. A rotated element collides as its bounding box, so do not rotate
  anything the player would stand on.
- The texture must be EXACTLY 16x16 pixels. Do NOT produce an
  animation strip or multiple frames - a texture a whole multiple
  taller is read as an animation and the extra frames are wasted.
- Alpha is BINARY: anything below 50% alpha is discarded, everything else is
  drawn fully opaque. Use alpha 0 or alpha 255 only. No anti-aliased edges, no
  semi-transparent glass, no soft shadows in the alpha channel. Transparency
  cuts holes; it does not fade.
- Box UV or per-face UV are both fine.

STYLE: chunky low-poly voxel-game block, flat shaded, pixel-art texture, no
gradients, no text, no logos. It sits on grass in daylight among other blocks
one metre across, and must read at 5-10 blocks distance.

DETAIL GOES IN THE TEXTURE, NOT IN GEOMETRY. Bolts, planks, panel lines,
grain, rust, dials, vents and engraving must be PAINTED. Model only what
changes the silhouette - the shape you would still recognise as a black
cut-out against the sky. This is the rule most often broken and the one that
matters most.

BUDGET: 16 cube elements MAXIMUM. This is a hard limit, not a target;
fewer is better. Exceeding it makes the model unusable.

MODEL: IRRIGATOR

A squat metal water tank on a low frame, with four short sprinkler arms
pointing outward and slightly downward from its sides, and a round water gauge
on the front face.

Save the project as irrigator.bbmodel
```

---

## After a model comes back

1. Save it into `models/` under the stem its prompt named.
2. If it arrived as an animation strip you did not ask for:
   `python tools/normalize_shape_texture.py models/<file>.bbmodel`
3. **Re-bake every model in ONE command.** The bake packs a single sheet, so a
   partial run silently drops every model it was not given. The last full
   command line lives in the header comment of
   `game/include/game/generated/BlockShapes.inl`; append yours and update that
   comment in the same commit.
4. Four C++ edits, each `static_assert`ed so a mismatch is a compile error: a
   `ShapeId` value, a `kBlockShapes` row, a `kShapeNames` entry, and
   `fullCube = false` plus `.shape` on the block's `kBlocks` row.
5. Check the bake's printed quad and KB figures against the budget. Over
   budget is almost always fixed by deleting elements and painting them.
6. Flip the block's **Status** line above to done, naming the file it was
   baked from, in the same commit.

`fullCube = false` costs three things every time: the block stops occluding
its neighbours, stops keeping rain out, and stops blocking grass spread. All
three are right for anything you can see past, which is nearly everything
here. A shaped block may keep `fullCube = true`, but only if its collision
bounds fill the cell edge to edge -- and that is a claim about collision
boxes, not about the drawn surface, so any recess or gap will let neighbours
cull faces against a block you can in fact see through.
