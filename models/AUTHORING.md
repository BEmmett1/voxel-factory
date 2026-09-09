# Authoring block-shape models

This folder holds **block-shape bake sources only** — Blockbench `.bbmodel`
files that `tools/bbmodel_to_shape.py` turns into
`game/include/game/generated/BlockShapes.inl` + `game/assets/shapes.png`.
Nothing here is loaded at runtime, and nothing here is shipped by
`copy-assets`.

Creature models are the other kind and live **only** in
`game/assets/models/`. A creature `.bbmodel` placed here is a copy that will
drift from the one the game loads, and the bake ignores it silently — which is
how boss #1 went missing from fresh clones for weeks.

The loop is two commands: save the project here, then re-bake **every** model
in one run (see step 14).

## Creating the project

**1. File → New → Java Block/Item.** This writes `model_format: "java_block"`
and gives you the 0–16 cell grid the bake expects. It also accepts `free`,
`bedrock` and `bedrock_old`, and warns on anything else:

    ! name: unfamiliar model_format 'x' -- baking anyway

There is no format *version* to choose. Blockbench stamps `meta.format_version`
from its own build, and the bake never reads it — it reads `meta.model_format`
and otherwise adapts to the outliner's structure. Both shapes shipped here
work: **5.0** (a flat `groups` table, every machine model) and **4.5** (bare
element-uuid strings and no groups at all, the herb crops).

That adaptation is the one place a Blockbench upgrade could bite, and it would
do it **silently**. 5.0 moved a group's `name`/`origin`/`rotation` out of the
outliner into the `groups` table; `load_parts` reads both, but a layout it does
not recognise is not an error — every part simply pivots about the model
origin, the same quirk `BbModel.cpp` handles for creature bones. The tell is a
trial bake in which every `kShapeParts...` pivot reads `{0.5, 0.5, 0.5}`
instead of the distinct pivots you authored.

**2. Set the texture size before you model** (Project panel → Texture Size).
This is the region packed into the shared `shapes.png`, so keep it small — the
crops live on 16×16 and 32×32 is generous for a machine. A 128×128 project
whose art only occupies 128×44 wastes the difference in the sheet.

**3. Create the texture inside Blockbench** (Textures panel → `+` → blank)
rather than linking a PNG from disk. The bake refuses a linked texture:

    name: texture is not embedded (path='...'); re-save with the texture embedded

To check a saved file without running the bake — it must print
`data:image/png;base64`:

```bash
python -c "import json,sys;print(json.load(open(sys.argv[1]))['textures'][0]['source'][:22])" models/x.bbmodel
```

## Modelling rules the bake enforces

**4. Cubes only.** Blockbench's Mesh tool is off limits: elements whose `type`
is not `cube` are skipped with a warning, so an all-mesh model bakes to nothing
at all (`name: nothing to bake`). Everything is boxes.

**5. Stay inside the 0–16 cell**, on every axis. More than
`CELL_OVERHANG_TOLERANCE` (**1 unit**) outside is a hard error. This is not
fussiness: every collision and ray query iterates the cells an AABB overlaps
and tests only *that* cell's boxes, so geometry hanging into a neighbour would
draw correctly and then be silently missed by physics. Under a unit is clamped
with a warning, because a 45° rotation produces a small overhang by arithmetic
rather than by intent (a 10-wide element sweeps a 14.14-wide diagonal).

**6. One rotation axis per element.** Minecraft allows one, Blockbench writes
all three; if two are non-zero only the first is applied and you get a warning.
`rescale` is ignored — it only matters to Minecraft's own renderer. A rotated
element renders exactly but **collides as its bounding box**, which is why
`quads` and `boxes` are separate arrays.

**7. Flat elements are legal, and they are the cheap detail.** Zero extent on
exactly one axis is a plane: **2 quads**, with the other four zero-area faces
dropped. Its *collision* box alone is inflated to `MIN_COLLISION_UNITS`
(1 unit), centred on the plane, because a zero-thickness AABB overlaps nothing
and the plane would be neither walk-into-able nor aimable-at; the drawn
geometry stays where you put it. Zero extent on two or more axes is a line or a
point and gets skipped. The herb crops are four quads each because of this.

**8. Faces you remove stay removed.** Clearing a face's texture in the Faces
panel makes the bake skip it. The bake *also* drops faces sealed inside the
model automatically, but that only catches fully covered ones — an interior box
still costs whatever of it peeks out.

## Texturing

**9. Alpha is binary.** `voxel.frag` does alpha **cutout** at 0.5 and writes an
opaque fragment; there is no blending in the world pass, deliberately, because
cutout is order-independent and lets chunks draw in hash-map order with no
depth sort. So paint fully opaque or fully transparent, with **no
anti-aliased alpha edges** — anything between snaps to one side of the
threshold. This is how a tube gets see-through windows: holes in the texture,
not translucency.

**10. Don't make an animation strip by accident.** A texture a whole multiple
of `uv_height` tall is auto-detected as a Minecraft animation strip and baked
with a `vStride`. **Keep the texture exactly `uv_height` tall** unless you want
animation, or an 8× taller sheet region is all you buy.

Animation plays **only while the block is powered** — the mesher passes bank 0
(`ShapeId::FullCube`, offset zero) for anything unenergized, so a block that is
not a power node parks on frame 0 forever. Check `PowerSystem::isPowerNode`
before authoring frames.

**11. Box UV is supported here.** (The *creature* loader in
`engine/src/BbModel.cpp` needs per-face UVs; this bake does not — it carries
mirrored rects with their sign, which is what keeps a third of the cauldron
from silently un-mirroring.) One caveat: box UV floors a box's dimensions to
whole texels, so an element thinner than 1 unit gets a zero-height side rect
that samples a single texel line. That is tolerated, not dropped — dropping it
would punch holes in exactly the fine trim these models are for — but if you
want crisp sub-unit trim, give those elements per-face UVs.

## Groups and moving parts

**12. Name your groups.** Every named outliner group becomes a `ShapePart`, and
that costs nothing at bake time — it only becomes a shader slot if
`kPartAnims` in `game/include/game/BlockShape.h` names it. Set each group's
origin where the part should actually turn; that pivot is what the bake stores
offsets against.

Which parts move is **C++ policy, not model data**, so re-authoring a model can
never silently animate a different lump of it, and a typo'd part name is a
`static_assert`. Nested groups do not yet inherit a parent's motion — the
parent is baked but unread, so it can be added later with no re-bake.

## Saving and baking

**13. File → Save Project** as `.bbmodel` into `models/`. Not the Export menu —
that writes Java JSON or OBJ, neither of which the bake reads. The file stem
becomes the C++ identifier (`copper_conduit_hub` → `CopperConduitHub`), with
every non-alphanumeric run treated as a word break, so avoid names like
`model (1).bbmodel`.

**14. Bake every model in one command.** The bake packs **one sheet**, so
baking a single model drops all the others out of `shapes.png`. The last full
command line lives in the header comment of
`game/include/game/generated/BlockShapes.inl` — append yours to it and update
that comment in the same commit.

**A model in this folder is not automatically baked.** The command line above
is the whole list; a file missing from it is tracked but unused. That is a
legitimate state -- `conduit_arm.bbmodel` is a straight-segment variant kept for
later, since the conduit draws its arms as parts of the hub instead -- but it is
also how a model silently goes missing, so a file left off the list should say
why in its commit message. Do not "fix" it by leaving the file untracked: an
untracked model is the bug that lost boss #1 for weeks.

To try a model without touching the committed outputs:

```bash
python tools/bbmodel_to_shape.py models/*.bbmodel \
  --out-png /tmp/trial.png --out-header /tmp/trial.inl
```

**15. Then four C++ edits**, all `static_assert`ed so a mismatch is a compile
error rather than a silent one:

- a `ShapeId` value before `Count` (`BlockShape.h`)
- a `kBlockShapes` row, in enum order
- a `kShapeNames` entry
- `fullCube = false` and `.shape = ShapeId::X` on the block's `kBlocks` row

Setting `fullCube = false` is what lets the shape be seen through, and it
carries three consequences every time: the block stops occluding its
neighbours, stops keeping rain out (`skyVisible`), and stops blocking grass
spread.

## Budget

The bake prints quads and KB per model — that is the live readout, so re-run it
early rather than at the end. A shaped vertex is 14 floats (position, normal,
uv, emissive, animation bank, part offset, part slot) = 56 bytes, and a quad is
6 vertices, so **336 bytes per quad**.

| | quads | chunk mesh |
|---|---:|---:|
| plain block (the baseline) | 6 | 1.3 KB |
| herb crop | 4 | 1.3 KB |
| Auger | 138 | 45 KB |
| Cauldron | 213 | 70 KB |
| Alembic | 232 | 76 KB |
| Infuser | 367 | 120 KB |

While modelling, the running arithmetic is **a box ≈ 5–6 quads ≈ 2 KB** and
**a flat plane = 2 quads ≈ 0.7 KB**.

The Infuser's 120 KB is the ceiling, and it is affordable only because you
place one. **Cost scales with how many of the thing exist**, so the budget is a
property of the block, not of the model: detailed shapes belong on machines,
and anything placed in bulk — a conduit, a wire, a crop — has to be authored
near the crop end of that table. Detail that reads at play distance almost
always wants to be texture; geometry should be buying you silhouette.
