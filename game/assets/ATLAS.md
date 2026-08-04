# atlas.png tile map

The atlas is **256x256**: a **16x16 grid of 16px tiles**. Tile index =
`row * 16 + col`. Repaint any tile in any pixel editor and rebuild
(`cmake --build out/build/x64-Debug` copies it next to the exe) — no code
changes needed unless you *move* a tile. World tiles must stay fully opaque;
item icons (rows 4-7, 11) may use transparency.

The sheet was 8 rows until the recipe overhaul. Because the index is
`row * 16 + col` and the **column count never changed**, growing downward left
every existing tile exactly where it was — which is why this grid may only
ever grow in rows, never in columns.

The mapping lives in code at `game/src/Block.cpp` (the `tiles` field on each
kBlocks row: per-block top/side/bottom, surfaced by the inline
`Atlas::tilesForBlock`) and `game/src/Item.cpp` (`atlasTile` per material item;
placeable items reuse their block's side tile automatically).
`tools/make_atlas.py` regenerates the whole file from scratch — run it only if
you want to *discard* hand edits and start over.

## Row 0 — terrain & trees (tiles 0-15)

| tile | col | content |
|-----:|----:|---------|
| 0 | 0 | grass top |
| 1 | 1 | grass side (turf lip on dirt) |
| 2 | 2 | dirt (also grass/log-adjacent bottoms) |
| 3 | 3 | stone |
| 4 | 4 | scaffold |
| 5 | 5 | sapling |
| 6 | 6 | log ends (rings; top + bottom) |
| 7 | 7 | log bark (sides) |
| 8 | 8 | leaves |
| 9-15 | | spare |

## Rows 1-2 — machines (tiles 16-47)

| tile | content | tile | content |
|-----:|---------|-----:|---------|
| 16 | generator top | 17 | generator side (vents) |
| 18 | wire | 19 | belt |
| 20 | **belt arrow** (points toward +v; the mesher rotates it) | 21 | grinder top (burr wheel) |
| 22 | grinder side | 23 | cauldron top (brew) |
| 24 | cauldron side | 25 | infuser top |
| 26 | infuser side (window) | 27 | alembic top (coil) |
| 28 | alembic side (retort) | 29 | distiller top (pipes) |
| 30 | distiller side (column) | 31 | transmuter top (alchemy circle) |
| 32 | transmuter side (gem) | 33 | miner top |
| 34 | miner side (drill) | 35 | composter top |
| 36 | composter side | 37 | forge top |
| 38 | forge side | 39 | press top (ram + die) |
| 40 | press side (screw + platen) | 41 | rune core top (sigil) |
| 42 | rune core side | 43 | pedestal top (socket) |
| 44 | pedestal side | 45-47 | spare |

## Row 3 — nodes & sources (tiles 48-63)

| tile | content | tile | content |
|-----:|---------|-----:|---------|
| 48 | herb bush | 49 | crystal node |
| 50 | copper ore node | 51 | sand node |
| 52 | spring | 53 | essence vent |
| 54 | herb source | 55 | crystal source |
| 56 | copper source | 57 | sand source |
| 58 | water source | 59 | essence source |
| 60 | voidstone (boss arena) | 61 | resonant node |
| 62 | resonant source | 63 | spare |

## Rows 4-5 — item icons (tiles 64-95; transparency welcome)

| tile | content | tile | content |
|-----:|---------|-----:|---------|
| 64 | stone | 65 | copper ore |
| 66 | sand | 67 | herb |
| 68 | crystal | 69 | spring water |
| 70 | essence | 71 | copper ingot |
| 72 | copper plate | 73 | glass |
| 74 | vial | 75 | machine frame |
| 76 | wood | 77 | bucket |
| 78 | wrench | 79 | copper sword |
| 80 | ground herb | 81 | crystal dust |
| 82 | herbal tincture | 83 | mineral solution |
| 84 | healing draught | 85 | mana vial |
| 86 | elixir of vigor | 87 | refined elixir |
| 88 | philosopher's catalyst | 89 | philosopher's stone |
| 90 | teleport key | 91 | void catalyst |
| 92 | storm key | 93 | storm core |
| 94 | resonance | 95 | fusion catalyst |

## Rows 6-7 — tools + spare (tiles 96-127)

| tile | content | tile | content |
|-----:|---------|-----:|---------|
| 96 | copper pickaxe | 97 | copper axe |
| 98 | copper shovel | 99 | stick |
| 100 | pebble | 101 | wood pickaxe |
| 102 | wood axe | 103 | stone pickaxe |
| 104 | stone axe | 105 | stone shovel |
| 106 | copper helm | 107 | copper chestplate |
| 108 | copper boots | 109 | aegis helm |
| 110 | aegis chestplate | 111 | aegis boots |
| 112 | copper rod | 113 | gear |
| 114 | machine casing | 115 | etched plate |
| 116-127 | spare | | |

## Row 8 — the smelting & sifting tier (tiles 128-135)

| tile | content | tile | content |
|-----:|---------|-----:|---------|
| 128 | furnace top (flue + fire) | 129 | furnace side (brick + mouth) |
| 130 | sifter top (wire mesh) | 131 | sifter side (hopper + tray) |
| 132 | glassblower top (gather) | 133 | glassblower side (pipe + bulb) |
| 134 | compactor top (mould) | 135 | compactor side (ram + bed) |

## Rows 8-10 — the manual tier (tiles 136-161)

Thirteen hand-cranked twins, painted from the `MANUAL_TIER` table in
`make_atlas.py` rather than one at a time: every top tile is a worn work
surface in the powered twin's accent color, every side tile carries the same
**hand crank**, so the whole tier reads as one tier.

| tile | content | tile | content |
|-----:|---------|-----:|---------|
| 136/137 | bloomery (← furnace) | 138/139 | sieve (← sifter) |
| 140/141 | blowpipe (← glassblower) | 142/143 | tamper (← compactor) |
| 144/145 | mortar (← grinder) | 146/147 | hand press (← press) |
| 148/149 | anvil (← forge) | 150/151 | compost heap (← composter) |
| 152/153 | mixing bowl (← cauldron) | 154/155 | infusion stand (← infuser) |
| 156/157 | still (← alembic) | 158/159 | hand distiller (← distiller) |
| 160/161 | hand transmuter (← transmuter) | 162-175 | spare |

## Row 11 — iron & charcoal (tiles 176-188)

| tile | content | tile | content |
|-----:|---------|-----:|---------|
| 176 | iron nugget | 177 | copper nugget |
| 178 | iron ingot | 179 | iron plate |
| 180 | iron rod | 181 | charcoal |
| 182 | iron pickaxe | 183 | iron axe |
| 184 | iron shovel | 185 | iron sword |
| 186 | iron helm | 187 | iron chestplate |
| 188 | iron boots | 189-191 | spare |

## Row 12 — bulk storage (tiles 192-193)

| tile | content | tile | content |
|-----:|---------|-----:|---------|
| 192 | storage crate (top/lid) | 193 | storage crate (side) |

## Rows 12-15 — spare (tiles 194-255)

Empty. This is the headroom the 8→16 row growth bought.

All tiles above are painted by `make_atlas.py`. The rest (9-15, 45-47, 63,
116-127, 162-175, 189-191, 194-255) are free for new blocks/items. Claim a tile here,
add a painter to `make_atlas.py`, and point the code at it (the `tiles` field
on the kBlocks row for blocks, `atlasTile` on the kItems row for material
items).

## shapes.png — the other sheet

**Not this file, and not on this grid.** `shapes.png` holds the textures of 3D
detailed blocks (`BlockShape.h`), packed as arbitrary REGIONS rather than 16px
tiles, because a Blockbench model's texture is typically 128px and may be an
animated strip. It is generated wholesale by
`python tools/bbmodel_to_shape.py models/<model>.bbmodel` alongside the shape
data — never hand-edited, and never repainted here.

A shaped block still keeps its `tiles` row above: those supply the item icon
and the fallback when `atlas.png` is missing. `BlockId::Cauldron` is the
first, and tiles 23/24 are still its.
