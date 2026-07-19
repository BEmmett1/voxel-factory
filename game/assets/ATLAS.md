# atlas.png tile map

The atlas is **256x128**: a **16x8 grid of 16px tiles**. Tile index =
`row * 16 + col`. Repaint any tile in any pixel editor and rebuild
(`cmake --build out/build/x64-Debug` copies it next to the exe) — no code
changes needed unless you *move* a tile. World tiles must stay fully opaque;
item icons (rows 4-5) may use transparency.

The mapping lives in code at `game/src/Atlas.cpp` (`kBlockTiles`: per-block
top/side/bottom) and `game/src/Item.cpp` (`atlasTile` per material item;
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
| 34 | miner side (drill) | 35-47 | spare |

## Row 3 — nodes & sources (tiles 48-63)

| tile | content | tile | content |
|-----:|---------|-----:|---------|
| 48 | herb bush | 49 | crystal node |
| 50 | copper ore node | 51 | sand node |
| 52 | spring | 53 | essence vent |
| 54 | herb source | 55 | crystal source |
| 56 | copper source | 57 | sand source |
| 58 | water source | 59 | essence source |
| 60 | voidstone (boss arena) | 61-63 | spare |

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
| 92-95 | spare | | |

## Rows 6-7 — spare (tiles 96-127)

Free for new blocks/items. Claim a tile here, then point the code at it
(the `tiles` field on the kBlocks row for blocks, `atlasTile` on the kItems
row for material items).
