# Recipes

**Generated** by `voxel-factory --dump-recipes` from the tables in
`game/src/Recipes.cpp`. Do not hand-edit: edit the recipe and
regenerate. The `key` column is the recipe's stable identity -- it is
what a save stores for a machine locked to a MAKE row, which is why
the tables can be reordered and edited freely.

## Hand-craft (the survival tier)

Instant and free, so it deliberately cannot build the factory.

| key | inputs | output |
|---|---|---|
| `hand/wood-pickaxe` | Stick x2 + Pebble x3 | Wood Pickaxe |
| `hand/wood-axe` | Stick x2 + Pebble x3 | Wood Axe |
| `hand/stone-pickaxe` | Stone x3 + Stick x2 | Stone Pickaxe |
| `hand/stone-axe` | Stone x3 + Stick x2 | Stone Axe |
| `hand/stone-shovel` | Stone x2 + Stick x2 | Stone Shovel |
| `hand/scaffold` | Stone | Scaffold x4 |
| `hand/bucket` | Wood x3 | Bucket |
| `hand/storage-crate` | Wood x8 | Storage Crate |
| `hand/bloomery` | Stone x8 | Bloomery |
| `hand/sieve` | Wood x4 + Stick x4 | Sieve |
| `hand/pedestal` | Stone x4 + Copper Ingot | Pedestal |
| `hand/rune-core` | Stone x6 + Copper Ingot x2 + Crystal | Rune Core |

## Machines


### Generator

Burns fuel. 

_No recipes -- its behavior is code, not a table._

### Grinder

Draws 5 power. Hand tier: **Mortar** (3x slower). 

| key | inputs | output | seconds |
|---|---|---|---|
| `grinder/ground-herb` | Herb | Ground Herb | 2.0 |
| `grinder/crystal-dust` | Crystal | Crystal Dust | 2.0 |
| `grinder/sand` | Stone | Sand x2 | 2.0 |

### Cauldron

Draws 5 power. Hand tier: **Mixing Bowl** (3x slower). 

| key | inputs | output | seconds |
|---|---|---|---|
| `cauldron/herbal-tincture` | Ground Herb + Rain Water | Herbal Tincture | 3.0 |
| `cauldron/mineral-solution` | Crystal Dust + Rain Water | Mineral Solution | 3.0 |

### Infuser

Draws 5 power. Hand tier: **Infusion Stand** (3x slower). 

| key | inputs | output | seconds |
|---|---|---|---|
| `infuser/healing-draught` | Herbal Tincture + Vial | Healing Draught | 4.0 |
| `infuser/mana-vial` | Mineral Solution + Essence | Mana Vial | 4.0 |

### Alembic

Draws 5 power. Hand tier: **Still** (3x slower). 

| key | inputs | output | seconds |
|---|---|---|---|
| `alembic/elixir-of-vigor` | Healing Draught + Mana Vial | Elixir of Vigor | 5.0 |

### Distiller

Draws 5 power. Hand tier: **Hand Distiller** (3x slower). 

| key | inputs | output | seconds |
|---|---|---|---|
| `distiller/refined-elixir` | Elixir of Vigor + Essence | Refined Elixir | 6.0 |

### Transmuter

Draws 5 power. Hand tier: **Hand Transmuter** (3x slower). 

| key | inputs | output | seconds |
|---|---|---|---|
| `transmuter/philosophers-catalyst` | Refined Elixir + Crystal Dust | Philosopher's Catalyst | 8.0 |
| `transmuter/philosophers-stone` | Philosopher's Catalyst + Elixir of Vigor | Philosopher's Stone | 10.0 |

### Miner

Draws 5 power. 

_No recipes -- its behavior is code, not a table._

### Rain Barrel

Runs unpowered. 

_No recipes -- its behavior is code, not a table._

### Composter

Draws 5 power. Hand tier: **Compost Heap** (3x slower). 

| key | inputs | output | seconds |
|---|---|---|---|
| `composter/dirt-from-sticks` | Stick x3 | Dirt x2 | 2.5 |
| `composter/dirt-from-sapling` | Sapling | Dirt x3 | 3.0 |

### Forge

Draws 5 power. Hand tier: **Anvil** (3x slower). 

| key | inputs | output | seconds |
|---|---|---|---|
| `forge/copper-helm` | Copper Plate x3 | Copper Helm | 5.0 |
| `forge/copper-chest` | Copper Plate x5 | Copper Chestplate | 7.0 |
| `forge/copper-boots` | Copper Plate x3 | Copper Boots | 5.0 |
| `forge/aegis-helm` | Machine Frame + Void Catalyst | Aegis Helm | 8.0 |
| `forge/aegis-chest` | Machine Frame x2 + Void Catalyst + Storm Core | Aegis Chestplate | 12.0 |
| `forge/aegis-boots` | Machine Frame + Storm Core | Aegis Boots | 8.0 |
| `forge/iron-helm` | Iron Plate x3 | Iron Helm | 6.0 |
| `forge/iron-chest` | Iron Plate x5 | Iron Chestplate | 8.0 |
| `forge/iron-boots` | Iron Plate x3 | Iron Boots | 6.0 |

### Press

Draws 5 power. Hand tier: **Hand Press** (3x slower). 

| key | inputs | output | seconds |
|---|---|---|---|
| `press/copper-plate` | Copper Ingot | Copper Plate | 3.0 |
| `press/iron-plate` | Iron Ingot | Iron Plate | 3.0 |
| `press/iron-rod` | Iron Ingot | Iron Rod x2 | 2.0 |
| `press/gear` | Iron Rod x2 | Gear | 3.0 |
| `press/machine-casing` | Iron Plate x4 | Machine Casing | 5.0 |
| `press/etched-plate` | Copper Plate + Crystal Dust x2 | Etched Plate | 4.0 |
| `press/machine-frame` | Machine Casing + Gear x2 + Etched Plate | Machine Frame | 8.0 |
| `press/copper-rod` | Copper Ingot | Copper Rod x2 | 2.0 |

### Rune Core

Draws 8 power. 

_No recipes -- its behavior is code, not a table._

### Pedestal

Runs unpowered. 

_No recipes -- its behavior is code, not a table._

### Furnace

Burns fuel. Hand tier: **Bloomery** (3x slower). 

| key | inputs | output | seconds |
|---|---|---|---|
| `furnace/copper-ingot` | Copper Ore x2 | Copper Ingot | 4.0 |
| `furnace/iron-ingot` | Iron Nugget x4 | Iron Ingot | 4.0 |
| `furnace/copper-from-nuggets` | Copper Nugget x4 | Copper Ingot | 4.0 |
| `furnace/glass` | Sand | Glass | 3.0 |
| `furnace/charcoal` | Wood x2 | Charcoal | 6.0 |

### Sifter

Draws 5 power. Hand tier: **Sieve** (3x slower). 

| key | inputs | output | seconds |
|---|---|---|---|
| `sifter/sand` | Sand x4 | Iron Nugget (30%), Copper Nugget (20%), Pebble (20%), Crystal (5%), nothing (25%) | 4.0 |
| `sifter/soil` | Dirt x4 | Pebble x2 (45%), Sand (30%), Stick (15%), nothing (10%) | 3.0 |

### Glassblower

Draws 5 power. Hand tier: **Blowpipe** (3x slower). 

| key | inputs | output | seconds |
|---|---|---|---|
| `glassblower/vial` | Glass | Vial | 3.0 |

### Compactor

Draws 5 power. Hand tier: **Tamper** (3x slower). 

| key | inputs | output | seconds |
|---|---|---|---|
| `compactor/stone` | Dirt x4 + Sand x4 | Stone x2 | 5.0 |

### Storage Crate

Runs unpowered. 

_No recipes -- its behavior is code, not a table._

## Alchemy Circle

A `ring` of 4 entries is the CARDINAL pedestals clockwise from north
(a Lesser circle can run it); 8 entries is the full ring and needs a
powered Greater circle. `-` is a slot that must be EMPTY. Matching is
rotation-invariant and each slot matches "holds at least this many",
so one pattern can shadow another -- order is what disambiguates, and
`--selftest` lays every pattern to prove none is unreachable.

| key | centre | ring (clockwise from N) | output | seconds |
|---|---|---|---|---|
| `circle/grinder` | - | Copper Ingot x3, -, Stone x4, - | Grinder | 6.0 |
| `circle/generator` | - | Copper Ingot x2, Stone x2, Wood x2, - | Generator | 6.0 |
| `circle/composter` | - | Wood x6, -, Stick x4, - | Composter | 5.0 |
| `circle/rain-barrel` | - | Wood x6, -, Bucket, - | Rain Barrel | 5.0 |
| `circle/wire` | - | Copper Ingot, -, -, - | Wire x2 | 2.0 |
| `circle/press` | - | Copper Ingot x2, Stone x2, Copper Ingot x2, Stone x2 | Press | 8.0 |
| `circle/furnace` | - | Stone x6, Charcoal x2, Stone x6, Charcoal x2 | Furnace | 8.0 |
| `circle/mortar` | - | Stone x6, -, -, - | Mortar | 3.0 |
| `circle/compost-heap` | - | Wood x4, -, -, - | Compost Heap | 3.0 |
| `circle/hand-press` | - | Stone x4, Wood x4, -, - | Hand Press | 4.0 |
| `circle/anvil` | - | Copper Ingot x4, Stone x4, -, - | Anvil | 4.0 |
| `circle/tamper` | - | Stone x6, Wood x2, -, - | Tamper | 4.0 |
| `circle/blowpipe` | - | Stone x4, -, Wood x2, - | Blowpipe | 4.0 |
| `circle/mixing-bowl` | - | Stone x4, -, Glass x2, - | Mixing Bowl | 4.0 |
| `circle/infusion-stand` | - | Wood x4, -, Glass, - | Infusion Stand | 4.0 |
| `circle/still` | - | Wood x4, -, Vial x2, - | Still | 4.0 |
| `circle/hand-distiller` | - | Stone x4, -, Vial x2, - | Hand Distiller | 4.0 |
| `circle/hand-transmuter` | - | Stone x4, -, Crystal x2, - | Hand Transmuter | 4.0 |
| `circle/copper-axe` | - | Copper Plate x3, Wood x2, -, - | Copper Axe | 4.0 |
| `circle/copper-pickaxe` | - | Copper Plate x3, -, Wood x2, - | Copper Pickaxe | 4.0 |
| `circle/copper-shovel` | - | Copper Plate x2, -, Wood x2, - | Copper Shovel | 4.0 |
| `circle/copper-sword` | - | Copper Plate x2, -, Wood, - | Copper Sword | 4.0 |
| `circle/conduit` | - | Copper Plate x2, -, -, - | Conduit x2 | 3.0 |
| `circle/wrench` | - | Copper Plate, -, Copper Plate, - | Wrench | 3.0 |
| `circle/iron-axe` | - | Iron Plate x3, Wood x2, -, - | Iron Axe | 5.0 |
| `circle/iron-pickaxe` | - | Iron Plate x3, -, Wood x2, - | Iron Pickaxe | 5.0 |
| `circle/iron-shovel` | - | Iron Plate x2, -, Wood x2, - | Iron Shovel | 5.0 |
| `circle/iron-sword` | - | Iron Plate x2, -, Wood, - | Iron Sword | 5.0 |
| `circle/miner` | - | Machine Frame, Copper Plate x3, Stone x4, - | Miner | 8.0 |
| `circle/distiller` | - | Machine Frame, Glass x2, Crystal, - | Distiller | 8.0 |
| `circle/transmuter` | - | Machine Frame, Crystal x2, Essence, - | Transmuter | 8.0 |
| `circle/infuser` | - | Machine Frame, Glass, Vial, - | Infuser | 6.0 |
| `circle/cauldron` | - | Machine Frame, -, Glass x2, - | Cauldron | 6.0 |
| `circle/forge` | - | Machine Frame, -, Copper Plate x2, - | Forge | 6.0 |
| `circle/alembic` | - | Machine Frame, -, Crystal, - | Alembic | 6.0 |
| `circle/sifter` | - | Machine Frame, Wood x4, Stone x4, - | Sifter | 8.0 |
| `circle/glassblower` | - | Machine Frame, Glass x4, -, - | Glassblower | 6.0 |
| `circle/compactor` | - | Machine Frame, Stone x8, -, - | Compactor | 6.0 |
| `circle/herb-source` | Philosopher's Catalyst | Herb x8, -, -, - | Herb Source | 10.0 |
| `circle/crystal-source` | Philosopher's Catalyst | Crystal x8, -, -, - | Crystal Source | 10.0 |
| `circle/copper-source` | Philosopher's Catalyst | Copper Ore x8, -, -, - | Copper Source | 10.0 |
| `circle/sand-source` | Philosopher's Catalyst | Sand x8, -, -, - | Sand Source | 10.0 |
| `circle/essence-source` | Philosopher's Catalyst | Essence x8, -, -, - | Essence Source | 10.0 |
| `circle/fusion-catalyst-from-resonance` | - | Resonance x2, -, -, - | Fusion Catalyst | 5.0 |
| `circle/philosophers-catalyst-from-resonance` | - | Resonance, -, -, - | Philosopher's Catalyst | 5.0 |
| `circle/teleport-key` | Philosopher's Catalyst x100 | Crystal, Essence, Crystal, -, Crystal, Essence, Crystal, - | Teleport Key | 12.0 |
| `circle/storm-key` | Void Catalyst x100 | Crystal, Rain Water, Crystal, Rain Water, Crystal, Rain Water, Crystal, Rain Water | Storm Key | 12.0 |
| `circle/fusion-catalyst-from-stone` | Philosopher's Stone | Crystal, -, Essence, -, Crystal, -, Essence, - | Fusion Catalyst | 15.0 |

## Fuels

| item | seconds |
|---|---|
| Stick | 5 |
| Sapling | 5 |
| Wood | 20 |
| Charcoal | 60 |

A machine that both burns fuel and runs recipes has a FUEL buffer of its
own, so a Furnace can char wood while burning wood -- which pile an
arriving belt item joins is inferred (ingredient wins), and a hand-drag
lands in the cell you dropped it on.
