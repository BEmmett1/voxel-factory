# Model prompts for the blocks that are still painted cubes

Generator prompts for every block in the **Model coverage** backlog
(ROADMAP.md). Paste **THE CONTRACT** below, then one block's brief under it.
One model per prompt — the bake takes one file per shape.

`AUTHORING.md` is the authority on the rules and on what to do with the file
afterwards; this file only turns those rules into something a model generator
will obey. Read the **Budget** section before anything else: the single thing a
generator gets wrong every time is building detail out of boxes that should
have been painted into the texture.

**Fifty-one blocks have models or should stay cubes; 45 are listed here.**
Terrain (Grass, Dirt, Stone, Sand, Copper Ore, Log, Leaves, Voidstone) is
deliberately absent — it is placed by the million and a cube is correct.

---

## THE CONTRACT

Paste this above every brief, unchanged.

```
Output a Blockbench "Java Block/Item" project (.bbmodel, model_format
"java_block") with the texture EMBEDDED as base64 PNG inside the file.

HARD RULES - the importer rejects a file that breaks any of these:
- CUBE elements only. No mesh/poly elements; an all-mesh model bakes to
  nothing.
- All geometry inside the 0..16 cell on every axis. More than 1 unit outside
  is a hard error. Geometry may touch a wall but must not cross it.
- At most ONE rotation axis per element, and only the Minecraft angles
  (-45, -22.5, 0, 22.5, 45). A rotated element renders exactly but COLLIDES
  as its bounding box, so keep rotations off anything the player stands on.
- The texture must be EXACTLY as tall as the project's texture height. A
  texture a whole multiple taller is read as an animation strip and wastes
  that multiple of sheet space. Do not emit animation frames unless the brief
  asks for them.
- Alpha is BINARY. The renderer discards a texel below 50% alpha and draws
  everything else fully opaque. Paint alpha 0 or alpha 255 only - no
  anti-aliased or semi-transparent edges, no soft shadows in the alpha.
  Transparency is how you cut a window or a gap, not how you fade anything.
- Keep the project's texture size small: 16x16 or 32x32. 64x64 only if the
  brief says so. Unused texture area is wasted room in a shared sheet.
- Box UV is fine. Per-face UVs are fine. Either will bake.

STYLE: chunky low-poly voxel-game blocks that read at 5-10 blocks' distance,
flat shaded, pixel-art texture, no gradients, no text, no logos. Assume the
block sits on grass in daylight next to other blocks 1 metre across.

DETAIL BELONGS IN THE TEXTURE. Bolts, panel lines, grain, rust, rune
engraving, dials and vents must be PAINTED, never modelled. Geometry is only
for silhouette: the parts you would still recognise as a black shape against
the sky. This is the rule generators break most, and it is the one that
matters.

BUDGET: the brief gives a hard maximum number of cube elements. Treat it as a
limit, not a target - fewer is better. Going over it is a failure, not a
flourish.
```

---

## Budget

Cost is per **placed block**, and it scales with how many of the thing exist,
so the budget is a property of the block rather than of the model. A shaped
vertex is 14 floats, a quad is 6 vertices: **336 bytes per quad**, and a cube
element is 5-6 quads once hidden faces are dropped, so **≈2 KB per element**.

| | elements | quads | chunk mesh |
|---|---:|---:|---:|
| plain cube (the baseline) | 1 | 6 | 1.3 KB |
| herb crop | 2 flat | 4 | 1.3 KB |
| wire hub, straight run | 3 of 7 | 16 | 5.2 KB |
| tree sapling | 9 | 53 | 17 KB |
| Auger | 29 | 138 | 45 KB |
| Cauldron | 40 | 213 | 70 KB |
| **Conduit hub, straight run** | **~29 of 57** | **186** | **61 KB** |
| Infuser | 67 | 367 | 120 KB |

**The conduit is the cautionary tale.** It came back at 57 elements, of which
120 quads are frame bars and rivets drawn on *every* conduit in the world —
47× a plain cube for a block players place in hundreds. The same silhouette
was available in about six elements with the rails painted on. When a brief
below says "8 elements maximum", that number is the whole point of the brief.

Rough guide: **place-in-hundreds ≤ 8 elements, place-a-few-dozen ≤ 20,
place-one-or-two ≤ 45.**

---

## Animation and moving parts

Two effects exist, and both are **gated on POWER**. A block that is not a
power node parks on frame 0 and stands still forever, so authoring either one
for such a block is wasted work.

**Power nodes** (animation and motion play when the network is satisfied):
Generator, Grinder, Distiller, Transmuter, Composter, Forge, Press, Sifter,
Glassblower, Compactor, Harvester.

**Not power nodes** (never animate — Rain Barrel, Furnace and Bloomery run on
fuel or weather, the twelve hand-cranked tools run on your arm, and the rest
draw nothing): every other block in this file.

- **Texture animation**: author the texture as N stacked frames, N× the
  project's texture height, and say so. Only where a brief asks.
- **Moving parts**: put the moving piece in its **own named outliner group**
  with its origin at the point it should turn about. Naming it is all the
  model does; a C++ table decides what actually moves, and a group nothing
  names is free and stays still. Use the exact group name the brief gives.

---

# The blocks

## A. Sources and nodes — the island's landmarks

Sources glow and are what you navigate by; nodes are what grows around them.
Both are scattered across the whole outer band, so both are bulk.
**Never machine-sized.**

| block | brief |
|---|---|
| **Herb Bush** | A low leafy bush of 3-4 crossed flat planes, dark green with small pale leaves. Max **4 elements**, all flat planes. Cut the leaf silhouette with alpha 0. Texture 16x16. |
| **Crystal Node** | 3-4 angular pale-violet crystal shards of different heights growing from the cell floor, tallest ~10 units. Max **4 elements**. Texture 16x16. |
| **Essence Vent** | A small dark stone vent mouth on the ground with two short crooked spires. Max **4 elements**. Texture 16x16. |
| **Resonant Node** | Like Crystal Node but the shards are banded two colours, teal and amber, reading as a fused hybrid. Max **5 elements**. Texture 16x16. |
| **Herb Source** | A mossy standing stone, roughly 8x12x8, with a carved bowl on top holding glowing green light. Max **6 elements**. Texture 16x16. |
| **Crystal Source** | The same standing stone, violet, with a crystal cluster set into the top instead of a bowl. Max **6 elements**. Texture 16x16. |
| **Copper Source** | The same standing stone, weathered green-and-orange copper, with a metal band around it. Max **6 elements**. Texture 16x16. |
| **Sand Source** | The same standing stone, pale gold, with sand spilling from a crack down one face. Max **6 elements**. Texture 16x16. |
| **Essence Source** | The same standing stone, deep blue-black, with a hovering pale mote above the bowl. Max **6 elements**. Texture 16x16. |
| **Resonant Source** | The same standing stone but visibly fused from two halves of different stone, teal on one side and amber on the other, seam down the middle, brightest of the six. Max **7 elements**. Texture 16x16. |

The six sources should read as **one set**: same silhouette, different colour
and crown. Generate the Herb Source first and ask for the others as recolours
of it, so the family holds together.

## B. Cheap wins — placed in bulk, tiny budgets

| block | brief |
|---|---|
| **Scaffold** | An open cubic frame: four corner posts plus a top and bottom rail, hollow in the middle so you can see through it. Max **8 elements**, or 4 if the rails can be painted. Bare timber lashed with twine. Texture 16x16. |
| **Tilled Soil** | A slab filling the cell's bottom 15 of 16 units, so worked ground sits a touch below grass, with four parallel furrows painted across the top. **1 element**. Texture 16x16. |
| **Rich Soil** | Identical geometry to Tilled Soil, darker and crumblier, with flecks of compost painted in. **1 element**. Texture 16x16. |
| **Grafted Sapling** | A young tree like the existing tree_sapling but sturdier: a thicker trunk and four short branches instead of two. Max **12 elements**. Texture 16x16. |

## C. The hand-cranked tier — twelve tools your arm drives

These are the first machines a new player meets, and rendering them as
identical boxes teaches nothing about what each does. They are **hand tools on
a bench, not machinery**: low, wooden, worn.

**Every one of them has a crank or a handle**, because a cranked machine only
advances while the player turns it — that is the tier's whole identity. Put
the handle in its own outliner group named exactly **`crank`**, with the
group's origin at the centre of its axle so it can be made to turn later.

None are power nodes. **No animation frames.** Max **14 elements** each,
texture 16x16 (32x32 only where noted).

| block | brief |
|---|---|
| **Sieve** | A square wooden frame holding a woven mesh, on short legs, with a side handle. Mesh painted with alpha-0 holes. Group: `crank`. |
| **Mortar** | A heavy stone bowl on a low wooden stand with an upright pestle resting in it. The pestle is the handle. Group: `crank`. |
| **Hand Press** | A screw press: a wooden bench, two upright posts, a threaded screw down the middle and a bar handle across the top. Group: `crank`. |
| **Anvil** | A blackened iron anvil on a scarred wooden stump, hammer leaning against it. The hammer is the handle. Group: `crank`. |
| **Blowpipe** | A small glassblower's bench: a stand holding a long thin pipe over a shallow bowl of coals, with a bellows lever at the side. Group: `crank`. |
| **Tamper** | A wide flat stone base with a heavy weighted rammer standing on it and a T-shaped handle on top. Group: `crank`. |
| **Compost Heap** | An open box of rough planks half full of dark compost, with a turning fork stuck upright in it. Group: `crank`. |
| **Mixing Bowl** | A wide clay bowl on a tripod with a long stirring paddle laid across it. Group: `crank`. |
| **Infusion Stand** | A slender wooden stand holding a glass phial over an unlit burner, with a small side wheel. Texture 32x32. Group: `crank`. |
| **Still** | A squat copper pot with a curled arm running down to a small collecting jar, and a valve wheel on the side. Texture 32x32. Group: `crank`. |
| **Hand Distiller** | Taller and thinner than the Still: a narrow copper column with two collecting rings and a wheel at the base. Texture 32x32. Group: `crank`. |
| **Hand Transmuter** | A stone slab carved with a circle, a small crystal held above it on a bent arm, and a hand wheel at the front. Texture 32x32. Group: `crank`. |

## D. Fuel-fired — heat, not electricity

Not power nodes. **No animation frames**, so paint the fire glow into the
texture rather than trying to make it flicker.

| block | brief |
|---|---|
| **Bloomery** | A waist-high chimney of clay and stone, narrowing toward the top, with a glowing arched opening at the front and a slag scar down one side. Max **16 elements**. Texture 32x32. |
| **Furnace** | A squat stone-and-iron furnace: a heavy body, an iron-barred door glowing at the front, a short chimney at the back. Max **20 elements**. Texture 32x32. |

## E. Powered machines — these may animate and may move

All are power nodes, so both effects work. Max **28 elements** each unless
noted, texture 32x32.

Where a brief names a group, author that piece as its own outliner group with
its origin at the axis it turns about.

| block | brief |
|---|---|
| **Generator** | An iron firebox on legs with a glowing grate at the front, a flywheel on one side and a stubby exhaust stack. Group: **`flywheel`**, origin at the wheel's centre. Texture as **8 stacked frames** so the grate can flicker. |
| **Grinder** | A heavy iron hopper over a stone grinding wheel set in a frame, with a chute at the front. Group: **`wheel`**, origin at the wheel's axle. |
| **Press** | A blocky iron press: a base plate, two thick uprights, and a broad ram head between them. Group: **`ram`**, origin at the TOP of the ram so it can drive down. |
| **Forge** | An anvil-and-hearth on an iron frame with a glowing bed and a hood over it. Texture as **8 stacked frames** so the bed pulses. Max **32 elements**. |
| **Sifter** | A boxy frame holding a slung sieve tray at a slight angle, with a hopper above it. Group: **`tray`**, origin at the tray's centre so it can shake. |
| **Glassblower** | A small round furnace with a glowing port, a swing-arm holding a blowpipe over it, and a cooling rack at the side. Group: **`arm`**, origin at the arm's shoulder joint. |
| **Compactor** | A squat, very heavy iron block with a recessed plate on top and thick guide posts at the corners. Group: **`plate`**, origin at the top of the plate. |
| **Composter** | A slatted wooden drum lying on its side in an iron cradle, with a hatch on the drum and dark compost visible through the slats. Group: **`drum`**, origin at the drum's centre line so it can roll. |
| **Distiller** | A tall copper column with three bulbs stacked up it, a coiled condenser running down the side into a collecting vessel. Max **32 elements**. |
| **Transmuter** | A dark stone pedestal carved with rings, holding a floating crystal between three curved arms that do not touch it. Group: **`crystal`**, origin at the crystal's centre. Texture as **8 stacked frames**. Max **32 elements**. |
| **Harvester** | A low wheeled frame with a horizontal cutting reel of thin blades at the front and a collecting box behind. Group: **`reel`**, origin at the reel's axle. |

## F. Unpowered utility — no animation, no motion

| block | brief |
|---|---|
| **Rain Barrel** | An open-topped barrel of curved staves bound with two iron hoops, water visible near the top. Max **14 elements**. Texture 16x16. |
| **Storage Crate** | A sturdy wooden crate with corner brackets and a lid, planks and iron corners painted rather than modelled. Max **8 elements** — this one gets placed in rows. Texture 16x16. |
| **Irrigator** | A squat tank on a low frame with four short sprinkler arms pointing outward and down, and a water gauge on the front. Max **16 elements**. Texture 16x16. |

---

## After the model comes back

1. Save it into `models/` with a lower_snake_case stem — the stem becomes the
   C++ identifier, so `rain_barrel.bbmodel` → `RainBarrel`.
2. If the generator sent an animation strip you did not ask for:
   `python tools/normalize_shape_texture.py models/<file>.bbmodel`
3. **Re-bake every model in one command.** The bake packs one sheet, so a
   partial run silently drops every model it was not given. The last full
   command line lives in the header comment of
   `game/include/game/generated/BlockShapes.inl`; append yours and update that
   comment in the same commit.
4. Four C++ edits, all `static_assert`ed (AUTHORING.md step 15): a `ShapeId`
   value, a `kBlockShapes` row, a `kShapeNames` entry, and `fullCube = false`
   plus `.shape` on the block's `kBlocks` row.
5. Check the bake's printed quad and KB figures against the budget above
   before committing. If a model came back over budget, the fix is almost
   always to delete elements and paint them instead.

Setting `fullCube = false` carries three consequences every time: the block
stops occluding its neighbours, stops keeping rain out (`skyVisible`), and
stops blocking grass spread. All three are right for anything you can see
past, which is nearly every model here.

A shaped block **may** keep `fullCube = true`, and `content::validate()`
states the exact condition: its shape's collision bounds must fill the cell
edge to edge. That is a claim about the union of the collision boxes, not
about the drawn surface, so it is only safe when the model is genuinely
watertight — any recess, chamfer or gap and neighbours will cull faces against
a block you can see through, leaving holes in the world. Prefer
`fullCube = false` and accept that rain falls past a furnace; it is cosmetic,
and the alternative fails loudly in a way that is tedious to trace.
