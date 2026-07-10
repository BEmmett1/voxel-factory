#!/usr/bin/env python3
"""Generate game/assets/atlas.png -- the starter texture atlas.

Pure stdlib (hand-rolled PNG writer). The atlas is a 16x8 grid of 16px tiles
(256x128); the tile map is documented in game/assets/ATLAS.md and must stay in
sync with Atlas.cpp / Item.cpp. Rerun after editing:  python tools/make_atlas.py

The output is a *starter* -- every tile can be repainted by hand in any editor;
this script just guarantees a complete, coherent set to start from.
"""

import struct
import zlib
from pathlib import Path

COLS, ROWS, T = 16, 8, 16
W, H = COLS * T, ROWS * T

buf = bytearray(W * H * 4)  # RGBA, transparent black


# --- tiny drawing kit -------------------------------------------------------

def n2(x, y, seed=0):
    """Deterministic hash noise in [0, 1)."""
    h = (x * 374761393 + y * 668265263 + seed * 2654435761) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((h ^ (h >> 16)) % 1024) / 1024.0


def clamp8(v):
    return max(0, min(255, int(v)))


def shade(c, m):
    return (clamp8(c[0] * m), clamp8(c[1] * m), clamp8(c[2] * m))


class Tile:
    """Draws into one 16x16 tile of the atlas."""

    def __init__(self, index):
        self.ox = (index % COLS) * T
        self.oy = (index // COLS) * T
        self.seed = index * 7919

    def px(self, x, y, c, a=255):
        if not (0 <= x < T and 0 <= y < T):
            return
        i = ((self.oy + y) * W + self.ox + x) * 4
        buf[i:i + 4] = bytes((c[0], c[1], c[2], a))

    def fill(self, c, noise=0.12, edge=0.0):
        """Noisy flat fill; edge > 0 darkens the 1px border by that factor."""
        for y in range(T):
            for x in range(T):
                m = 1.0 + (n2(x, y, self.seed) - 0.5) * 2.0 * noise
                if edge and (x in (0, T - 1) or y in (0, T - 1)):
                    m *= 1.0 - edge
                self.px(x, y, shade(c, m))

    def rect(self, x0, y0, x1, y1, c, a=255):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.px(x, y, c, a)

    def hline(self, y, x0, x1, c, a=255):
        self.rect(x0, y, x1, y, c, a)

    def vline(self, x, y0, y1, c, a=255):
        self.rect(x, y0, x, y1, c, a)

    def outline(self, c, a=255):
        self.hline(0, 0, T - 1, c, a)
        self.hline(T - 1, 0, T - 1, c, a)
        self.vline(0, 0, T - 1, c, a)
        self.vline(T - 1, 0, T - 1, c, a)

    def speckle(self, c, count, seed=1, size=1):
        for k in range(count):
            x = int(n2(k, 3, self.seed + seed) * (T - size))
            y = int(n2(7, k, self.seed + seed) * (T - size))
            self.rect(x, y, x + size - 1, y + size - 1, c)

    def disc(self, cx, cy, r, c, a=255):
        for y in range(T):
            for x in range(T):
                if (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                    self.px(x, y, c, a)

    def ring(self, cx, cy, r, c, a=255):
        for y in range(T):
            for x in range(T):
                d = (x - cx) ** 2 + (y - cy) ** 2
                if (r - 0.7) ** 2 <= d <= (r + 0.7) ** 2:
                    self.px(x, y, c, a)

    def rivets(self, c):
        for x, y in ((1, 1), (T - 2, 1), (1, T - 2), (T - 2, T - 2)):
            self.px(x, y, c)


# --- palette (anchored to Block.cpp colors, 0-255) --------------------------

GRASS = (77, 158, 66)
DIRT = (115, 79, 46)
STONE = (128, 128, 135)
GENERATOR = (219, 115, 31)
WIRE = (209, 184, 51)
BELT = (56, 56, 66)
GRINDER = (115, 115, 122)
CAULDRON = (46, 46, 56)
INFUSER = (102, 158, 153)
ALEMBIC = (184, 148, 71)
DISTILLER = (148, 77, 140)
TRANSMUTER = (217, 191, 89)
MINER = (89, 102, 117)
SCAFFOLD = (173, 158, 122)
SAPLING = (115, 184, 71)
LOG = (115, 84, 46)
LEAVES = (46, 128, 41)

HERB = (51, 140, 56)
CRYSTAL = (140, 115, 217)
COPPER = (179, 115, 77)
SAND = (217, 199, 140)
WATER = (64, 128, 217)
ESSENCE = (153, 71, 184)

SOURCES = {  # tile -> glow color (matches the Source* block colors)
    54: (77, 242, 77), 55: (191, 140, 255), 56: (255, 140, 64),
    57: (255, 235, 140), 59: (217, 89, 255),
}

BARREL = (102, 71, 38)  # rain-barrel staves

GOLD = (255, 214, 51)
DARK = (25, 25, 30)
GLASS_EDGE = (170, 200, 215)
GLASS_SHINE = (230, 245, 255)


# --- shared painters --------------------------------------------------------

def plate(t, base):
    """Machine housing: noisy plate, dark frame, corner rivets."""
    t.fill(base, noise=0.08)
    t.outline(shade(base, 0.55))
    t.rivets(shade(base, 1.45))


def node(t, resource, seed=5):
    """Resource node: stony base with resource-colored lumps."""
    t.fill(shade(STONE, 0.85), noise=0.10, edge=0.25)
    for k in range(7):
        x = 2 + int(n2(k, 11, t.seed + seed) * (T - 5))
        y = 2 + int(n2(13, k, t.seed + seed) * (T - 5))
        t.rect(x, y, x + 1, y + 1, resource)
        t.px(x, y, shade(resource, 1.35))


def source(t, glow):
    """Source block: dark shell around a radiant core."""
    t.fill(shade(glow, 0.35), noise=0.10, edge=0.30)
    c = T // 2
    for k, r in ((0.55, 5), (0.8, 3)):
        t.disc(c, c, r, shade(glow, k + 0.4))
    t.disc(c, c, 1, (255, 255, 245))
    for d in (-6, 6):  # cross rays
        t.px(c + d, c, shade(glow, 1.2))
        t.px(c, c + d, shade(glow, 1.2))


def vial(t, liquid=None):
    """Item icon: corked vial, optionally filled."""
    t.vline(5, 4, 13, GLASS_EDGE)
    t.vline(10, 4, 13, GLASS_EDGE)
    t.hline(13, 5, 10, GLASS_EDGE)
    t.hline(4, 5, 10, GLASS_EDGE)
    t.rect(6, 2, 9, 3, (150, 105, 60))          # cork
    if liquid:
        t.rect(6, 8, 9, 12, liquid)
        t.hline(8, 6, 9, shade(liquid, 1.3))
    t.vline(6, 5, 12, GLASS_SHINE, 120)          # shine


def potion(t, liquid):
    """Item icon: round-bellied flask with liquid."""
    t.ring(8, 10, 4.5, GLASS_EDGE)
    t.vline(6, 2, 5, GLASS_EDGE)
    t.vline(9, 2, 5, GLASS_EDGE)
    t.rect(6, 1, 9, 1, (150, 105, 60))
    t.disc(8, 11, 3.6, liquid)
    t.px(6, 9, GLASS_SHINE)
    t.px(7, 8, GLASS_SHINE)


def gem(t, c, big=False):
    """Item icon: cut gem, diamond silhouette."""
    r = 6 if big else 4
    cx, cy = 8, 8
    for y in range(-r, r + 1):
        half = r - abs(y)
        for x in range(-half, half + 1):
            m = 1.25 if (x + y) < 0 else (0.75 if (x - y) > 0 else 1.0)
            t.px(cx + x, cy + y, shade(c, m))
    t.px(cx - r // 2, cy - r // 2, (255, 255, 255))


# --- tile painters ----------------------------------------------------------

def paint(index):
    return Tile(index)


def terrain():
    t = paint(0)                                     # grass top
    t.fill(GRASS, noise=0.14)
    t.speckle(shade(GRASS, 1.35), 9, seed=2)         # bright blades
    t.speckle(shade(GRASS, 0.7), 7, seed=4)

    t = paint(1)                                     # grass side
    t.fill(DIRT, noise=0.14)
    t.speckle(shade(DIRT, 0.65), 6, seed=3)
    for x in range(T):                               # ragged turf lip
        depth = 2 + int(n2(x, 0, 77) * 3)
        for y in range(depth):
            t.px(x, y, shade(GRASS, 0.9 + n2(x, y, 78) * 0.3))
        if n2(x, 5, 79) > 0.72:                      # hanging root strand
            t.px(x, depth, shade(GRASS, 0.75))
            t.px(x, depth + 1, shade(GRASS, 0.6))

    t = paint(2)                                     # dirt
    t.fill(DIRT, noise=0.16)
    t.speckle(shade(DIRT, 0.6), 8, seed=5)
    t.speckle(shade(DIRT, 1.3), 5, seed=6)
    t.speckle(shade(STONE, 0.8), 3, seed=7)          # buried pebbles

    t = paint(3)                                     # stone
    t.fill(STONE, noise=0.10)
    x, y = 3, 0                                      # meandering crack
    for step in range(14):
        t.px(x, y, shade(STONE, 0.55))
        y += 1
        x += (1 if n2(step, 1, 41) > 0.6 else 0) - (1 if n2(step, 2, 42) > 0.6 else 0)
        x = max(1, min(T - 2, x))
    t.speckle(shade(STONE, 1.25), 5, seed=8)
    t.speckle(shade(STONE, 0.7), 4, seed=9)

    t = paint(4)                                     # scaffold
    t.fill(SCAFFOLD, noise=0.10)
    frame = shade(SCAFFOLD, 0.6)
    t.outline(frame)
    for d in range(T):                               # X brace
        t.px(d, d, frame)
        t.px(T - 1 - d, d, frame)
    t.rivets(shade(SCAFFOLD, 1.3))

    t = paint(5)                                     # sapling block
    t.fill(shade(SAPLING, 0.75), noise=0.10, edge=0.2)
    t.vline(8, 9, 13, shade(LOG, 1.1))               # young stem
    t.disc(8, 6, 3, shade(LEAVES, 1.25))             # leaf puff
    t.disc(7, 5, 1, shade(SAPLING, 1.45))
    t.px(6, 8, shade(LEAVES, 1.1))
    t.px(10, 7, shade(LEAVES, 1.1))

    t = paint(6)                                     # log top: rings
    t.fill(shade(LOG, 0.8), noise=0.08)
    t.outline(shade(LOG, 0.55))
    for r, m in ((6, 1.25), (4, 0.8), (2, 1.3)):
        t.ring(8, 8, r, shade((166, 127, 77), m))
    t.px(8, 8, shade(LOG, 0.6))

    t = paint(7)                                     # log side: bark
    t.fill(LOG, noise=0.10)
    for x in (1, 4, 7, 10, 13):
        for y in range(T):
            if n2(x, y, 91) > 0.25:
                t.px(x, y, shade(LOG, 0.7))
    t.speckle(shade(LOG, 1.35), 5, seed=10)
    t.disc(11, 5, 1, shade(LOG, 0.55))               # knot

    t = paint(8)                                     # leaves
    t.fill(LEAVES, noise=0.16)
    t.speckle(shade(LEAVES, 0.5), 7, seed=11)        # depth holes
    t.speckle(shade(LEAVES, 1.4), 8, seed=12)        # lit clusters
    t.speckle(shade(SAPLING, 1.1), 3, seed=13)


def machines():
    t = paint(16)                                    # generator top
    plate(t, GENERATOR)
    t.disc(8, 8, 4, shade(GENERATOR, 0.5))
    t.disc(8, 8, 3, GOLD)
    t.disc(8, 8, 1, (255, 255, 230))
    for dx, dy in ((-5, 0), (5, 0), (0, -5), (0, 5)):
        t.px(8 + dx, 8 + dy, shade(GENERATOR, 1.4))

    t = paint(17)                                    # generator side
    plate(t, GENERATOR)
    for y in (5, 8, 11):                             # vents
        t.hline(y, 3, 12, shade(GENERATOR, 0.45))
        t.hline(y + 1, 3, 12, shade(GENERATOR, 0.75))

    t = paint(18)                                    # wire
    t.fill(shade(WIRE, 0.5), noise=0.08, edge=0.25)
    t.rect(6, 0, 9, 15, shade(WIRE, 1.0))            # conduit cross
    t.rect(0, 6, 15, 9, shade(WIRE, 1.0))
    t.rect(7, 0, 8, 15, shade(WIRE, 1.35))
    t.rect(0, 7, 15, 8, shade(WIRE, 1.35))
    t.rect(6, 6, 9, 9, shade(GOLD, 1.05))            # junction

    t = paint(19)                                    # belt base
    t.fill(BELT, noise=0.08)
    t.vline(0, 0, 15, shade(BELT, 1.5))              # rails
    t.vline(15, 0, 15, shade(BELT, 1.5))
    for y in (2, 6, 10, 14):                         # rollers
        t.hline(y, 2, 13, shade(BELT, 1.8))

    t = paint(20)                                    # belt arrow (over base look)
    t.fill(BELT, noise=0.08)
    t.vline(0, 0, 15, shade(BELT, 1.5))
    t.vline(15, 0, 15, shade(BELT, 1.5))
    for y in range(2, 9):                            # shaft, pointing +v
        t.px(7, y, GOLD)
        t.px(8, y, GOLD)
    for k in range(5):                               # chevron head, tip at y=13
        t.hline(9 + k, 3 + k, 12 - k, GOLD)

    t = paint(21)                                    # grinder top: burr wheel
    plate(t, GRINDER)
    t.ring(8, 8, 5, shade(GRINDER, 0.5))
    for a in range(8):                               # teeth
        dx = (5, 4, 0, -4, -5, -4, 0, 4)[a]
        dy = (0, 4, 5, 4, 0, -4, -5, -4)[a]
        t.px(8 + dx, 8 + dy, DARK)
    t.disc(8, 8, 2, shade(GRINDER, 1.3))
    t.px(8, 8, DARK)

    t = paint(22)                                    # grinder side
    plate(t, GRINDER)
    t.hline(7, 2, 13, DARK)                          # gear band
    t.hline(8, 2, 13, shade(GRINDER, 0.6))
    for x in range(2, 14, 3):
        t.px(x, 6, DARK)
        t.px(x, 9, DARK)

    t = paint(23)                                    # cauldron top: brew
    t.fill(CAULDRON, noise=0.08)
    t.outline(shade(CAULDRON, 1.6))
    t.rect(2, 2, 13, 13, shade(HERB, 0.9))           # liquid
    for k in range(5):                               # bubbles
        x = 3 + int(n2(k, 21, 7) * 10)
        y = 3 + int(n2(23, k, 7) * 10)
        t.px(x, y, shade(HERB, 1.5))

    t = paint(24)                                    # cauldron side
    t.fill(shade(CAULDRON, 1.4), noise=0.10)
    t.hline(0, 0, 15, shade(CAULDRON, 2.6))          # bright rim
    t.hline(1, 0, 15, shade(CAULDRON, 2.1))
    t.rect(3, 4, 4, 11, shade(CAULDRON, 2.2))        # belly highlight
    t.hline(13, 2, 13, shade(CAULDRON, 0.8))         # under-curve shadow
    t.rect(2, 14, 4, 15, shade(CAULDRON, 0.6))       # feet
    t.rect(11, 14, 13, 15, shade(CAULDRON, 0.6))
    t.hline(14, 6, 9, shade(CAULDRON, 0.7))

    t = paint(25)                                    # infuser top
    plate(t, INFUSER)
    t.ring(8, 8, 4, shade(INFUSER, 0.5))
    t.disc(8, 8, 2, shade(WATER, 1.1))               # intake funnel
    t.px(7, 7, (240, 250, 255))

    t = paint(26)                                    # infuser side
    plate(t, INFUSER)
    t.rect(4, 4, 11, 12, GLASS_EDGE)                 # window
    t.rect(5, 5, 10, 11, shade(INFUSER, 0.45))
    t.rect(5, 8, 10, 11, shade(WATER, 0.9))          # liquid level
    t.px(5, 5, GLASS_SHINE)

    t = paint(27)                                    # alembic top: coil
    plate(t, ALEMBIC)
    for r, m in ((5, 0.55), (3, 1.3), (1, 0.55)):
        t.ring(8, 8, r, shade(ALEMBIC, m))

    t = paint(28)                                    # alembic side: retort
    plate(t, ALEMBIC)
    t.disc(6, 10, 3, shade(ALEMBIC, 0.5))            # bulb
    t.disc(6, 10, 2, shade(DISTILLER, 1.1))
    t.hline(6, 7, 12, shade(ALEMBIC, 0.5))           # neck
    t.px(12, 7, shade(ALEMBIC, 0.5))
    t.px(12, 8, shade(DISTILLER, 1.1))

    t = paint(29)                                    # distiller top: pipes
    plate(t, DISTILLER)
    t.rect(3, 3, 5, 12, shade(DISTILLER, 0.55))
    t.rect(10, 3, 12, 12, shade(DISTILLER, 0.55))
    t.vline(4, 3, 12, shade(DISTILLER, 1.35))
    t.vline(11, 3, 12, shade(DISTILLER, 1.35))

    t = paint(30)                                    # distiller side: column
    plate(t, DISTILLER)
    t.rect(6, 2, 9, 13, GLASS_EDGE)
    t.rect(7, 3, 8, 12, shade(DISTILLER, 0.4))
    for y in (11, 8, 5):                             # rising bubbles
        t.px(7 + (y % 2), y, shade(GOLD, 1.0))

    t = paint(31)                                    # transmuter top: circle
    plate(t, TRANSMUTER)
    t.ring(8, 8, 6, shade(DISTILLER, 0.9))
    for k in range(9):                               # inscribed triangle
        x = 8 - 4 + k
        t.px(x, 12, DARK)                            # base
        half = k // 2
        t.px(8 - 4 + half, 12 - k, DARK)             # left edge
        t.px(8 + 4 - half, 12 - k, DARK)             # right edge
    t.disc(8, 9, 1, (255, 250, 220))

    t = paint(32)                                    # transmuter side
    plate(t, TRANSMUTER)
    gem_c = shade(DISTILLER, 1.15)
    for y in range(-3, 4):                           # inset gem
        half = 3 - abs(y)
        t.hline(8 + y, 8 - half, 8 + half, gem_c)
    t.px(7, 7, (255, 255, 255))

    t = paint(33)                                    # miner top
    plate(t, MINER)
    for k in range(0, 16, 4):                        # hazard corners
        t.px(k, 0, GOLD)
        t.px(15 - k, 15, GOLD)
    t.ring(8, 8, 4, shade(MINER, 0.5))
    t.disc(8, 8, 2, shade(MINER, 1.4))

    t = paint(34)                                    # miner side: drill
    plate(t, MINER)
    for k in range(5):                               # drill bit pointing down
        t.hline(8 + k, 4 + k, 11 - k, shade(STONE, 1.1))
    t.rect(5, 4, 10, 7, shade(MINER, 0.55))
    t.hline(5, 5, 10, shade(MINER, 1.3))


def nodes_and_sources():
    t = paint(48)                                    # herb bush
    t.fill(shade(HERB, 0.55), noise=0.12)
    t.speckle(shade(HERB, 1.25), 10, seed=14, size=2)
    t.speckle(shade(HERB, 1.6), 5, seed=15)
    t.speckle((200, 80, 90), 3, seed=16)             # berries

    t = paint(49)                                    # crystal node
    t.fill(shade(STONE, 0.6), noise=0.10, edge=0.2)
    for cx, cy, h in ((5, 9, 5), (10, 10, 6), (8, 7, 4)):
        for k in range(h):                           # shard columns
            t.px(cx, cy - k, shade(CRYSTAL, 0.9 + k * 0.12))
            t.px(cx + 1, cy - k + 1, shade(CRYSTAL, 0.7))
        t.px(cx, cy - h, (240, 235, 255))

    node(paint(50), COPPER)                          # copper ore
    t = paint(51)                                    # sand node: ripples
    t.fill(SAND, noise=0.10)
    for y in (3, 7, 11):
        for x in range(T):
            yy = y + (1 if n2(x, y, 55) > 0.5 else 0)
            t.px(x, yy, shade(SAND, 0.8))
    t.speckle(shade(SAND, 1.25), 6, seed=17)

    t = paint(52)                                    # rain-barrel top: open water
    t.fill(BARREL, noise=0.10)
    t.outline(shade(BARREL, 0.55))
    t.rect(2, 2, 13, 13, shade(WATER, 0.85))         # water surface inside the rim
    t.ring(8, 8, 3, shade(WATER, 1.3))               # ripple
    t.px(5, 4, (225, 240, 255))
    t.speckle(shade(WATER, 1.15), 4, seed=18)

    t = paint(58)                                    # rain-barrel sides: staves
    t.fill(BARREL, noise=0.10)
    for x in (3, 7, 11):                             # stave seams
        t.vline(x, 0, 15, shade(BARREL, 0.65))
    for y in (3, 12):                                # iron hoops
        t.hline(y, 0, 15, (140, 140, 150))
        t.hline(y + 1, 0, 15, (90, 90, 100))
    t.speckle(shade(BARREL, 1.3), 4, seed=19)

    t = paint(53)                                    # essence vent
    t.fill(shade(ESSENCE, 0.5), noise=0.12, edge=0.25)
    for k in range(10):                              # rising wisp
        y = 13 - k
        x = 8 + int(2.2 * (n2(k, 61, 3) - 0.5) * 2)
        t.px(x, y, shade(ESSENCE, 1.0 + k * 0.06))
    t.disc(8, 12, 2, shade(ESSENCE, 1.3))
    t.px(8, 3, (245, 220, 255))

    for tile, glow in SOURCES.items():
        source(paint(tile), glow)


def items():
    t = paint(64)                                    # stone lump
    t.disc(8, 9, 5, STONE)
    t.disc(7, 8, 5, shade(STONE, 1.1))
    t.px(5, 6, shade(STONE, 1.35))
    t.rect(6, 12, 11, 13, shade(STONE, 0.7))

    t = paint(65)                                    # copper ore chunk
    t.disc(8, 9, 5, shade(STONE, 0.9))
    t.speckle(COPPER, 6, seed=19)
    t.px(7, 7, shade(COPPER, 1.4))

    t = paint(66)                                    # sand pile
    for k in range(5):                               # mound
        t.hline(13 - k, 3 + k, 12 - k, shade(SAND, 1.0 + k * 0.06))
    t.speckle(shade(SAND, 0.8), 4, seed=20)

    t = paint(67)                                    # herb sprig
    t.vline(8, 4, 13, shade(HERB, 0.8))
    for dy, dx in ((5, -2), (7, 2), (9, -3), (11, 3)):
        t.hline(dy, 8 + min(0, dx), 8 + max(0, dx), shade(HERB, 1.2))
        t.px(8 + dx, dy - 1, shade(HERB, 1.45))

    gem(paint(68), CRYSTAL)                          # crystal

    t = paint(69)                                    # water droplet
    t.disc(8, 9, 4, WATER)
    t.px(8, 4, WATER)
    t.rect(7, 5, 9, 6, WATER)
    t.px(6, 8, (230, 242, 255))

    t = paint(70)                                    # essence orb
    t.disc(8, 8, 4, shade(ESSENCE, 0.9))
    t.ring(8, 8, 5, shade(ESSENCE, 1.25))
    t.px(6, 6, (245, 225, 255))
    t.px(11, 3, shade(ESSENCE, 1.4))
    t.px(4, 12, shade(ESSENCE, 1.4))

    t = paint(71)                                    # copper ingot
    for y, m in ((7, 1.3), (8, 1.05), (9, 0.9), (10, 0.7)):
        x0 = 3 + (10 - y) // 3
        t.hline(y, x0, 15 - x0, shade(COPPER, m))
    t.hline(6, 5, 10, shade(COPPER, 1.5))

    t = paint(72)                                    # copper plate
    t.rect(3, 4, 12, 12, COPPER)
    t.rect(3, 4, 12, 5, shade(COPPER, 1.3))
    for x, y in ((4, 6), (11, 6), (4, 11), (11, 11)):
        t.px(x, y, shade(COPPER, 0.55))

    t = paint(73)                                    # glass pane
    t.rect(4, 3, 11, 12, GLASS_EDGE, 200)
    t.rect(5, 4, 10, 11, (200, 225, 240), 140)
    for d in range(4):
        t.px(9 - d, 5 + d, GLASS_SHINE, 220)

    vial(paint(74))                                  # empty vial

    t = paint(75)                                    # machine frame
    t.rect(3, 3, 12, 12, shade(STONE, 0.9))
    t.rect(5, 5, 10, 10, (0, 0, 0), 0)               # hollow center
    t.rect(6, 6, 9, 9, shade(COPPER, 1.1))           # inner armature
    t.rivets(shade(STONE, 1.3))

    t = paint(76)                                    # wood plank
    t.rect(2, 4, 13, 7, shade(LOG, 1.25))
    t.rect(2, 9, 13, 12, shade(LOG, 1.05))
    for x in (5, 9, 12):
        t.px(x, 5, shade(LOG, 0.8))
        t.px(x - 1, 11, shade(LOG, 0.75))

    t = paint(77)                                    # bucket
    for y in range(2, 7):                            # handle arc above the rim
        for x in range(T):
            d = (x - 8) ** 2 + (y - 6) ** 2
            if 12 <= d <= 20:
                t.px(x, y, shade(STONE, 0.85))
    t.hline(6, 4, 11, shade(STONE, 1.4))             # rim
    for y in range(7, 14):                           # tapering pail
        inset = (y - 7) // 3
        t.hline(y, 4 + inset, 11 - inset, shade(STONE, 1.05 - (y - 7) * 0.04))
    t.vline(5, 7, 12, shade(STONE, 1.25))            # side sheen

    t = paint(78)                                    # wrench
    for d in range(8):                               # diagonal shaft
        t.px(4 + d, 12 - d, shade(STONE, 1.15))
        t.px(5 + d, 12 - d, shade(STONE, 0.9))
    t.disc(12, 4, 2, shade(STONE, 1.15))             # head
    t.px(13, 3, (0, 0, 0), 0)                        # jaw notch
    t.px(14, 2, (0, 0, 0), 0)
    t.disc(4, 12, 1, shade(STONE, 0.9))

    t = paint(79)                                    # copper sword
    for d in range(8):                               # diagonal blade
        t.px(5 + d, 10 - d, shade(COPPER, 1.25))
        t.px(6 + d, 10 - d, shade(COPPER, 0.95))
    t.px(13, 2, (255, 235, 210))                     # gleaming tip
    for d in range(-1, 3):                           # cross-guard
        t.px(4 + d, 12 - d, GOLD)
    t.px(3, 13, shade(LOG, 1.2))                     # grip
    t.px(2, 14, shade(LOG, 1.0))
    t.px(1, 15, shade(LOG, 0.8))                     # pommel

    t = paint(80)                                    # ground herb powder
    for k in range(4):
        t.hline(13 - k, 4 + k, 11 - k, shade(HERB, 1.1))
    t.speckle(shade(HERB, 1.5), 5, seed=21)

    t = paint(81)                                    # crystal dust
    for k in range(3):
        t.hline(13 - k, 5 + k, 10 - k, shade(CRYSTAL, 1.05))
    t.px(6, 9, shade(CRYSTAL, 1.5))
    t.px(9, 8, (240, 235, 255))
    t.px(4, 11, shade(CRYSTAL, 1.3))

    vial(paint(82), shade(HERB, 1.1))                # herbal tincture
    vial(paint(83), shade(WATER, 1.0))               # mineral solution
    potion(paint(84), (222, 76, 98))                 # healing draught
    potion(paint(85), (72, 110, 235))                # mana vial
    potion(paint(86), (235, 186, 56))                # elixir of vigor

    t = paint(87)                                    # refined elixir
    potion(t, (250, 214, 92))
    t.px(3, 3, (255, 255, 220))
    t.px(12, 5, (255, 255, 220))

    t = paint(88)                                    # philosopher's catalyst
    gem(t, (222, 96, 60))
    t.px(12, 3, (255, 230, 200))

    t = paint(89)                                    # philosopher's stone
    gem(t, (200, 40, 60), big=True)
    for dx, dy in ((-7, 0), (7, 0), (0, -7), (0, 7)):
        t.px(8 + dx, 8 + dy, (255, 210, 220))


# --- PNG writer -------------------------------------------------------------

def write_png(path, w, h, rgba):
    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    raw = b"".join(b"\x00" + bytes(rgba[y * w * 4:(y + 1) * w * 4]) for y in range(h))
    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(raw, 9)) +
           chunk(b"IEND", b""))
    Path(path).write_bytes(png)


def main():
    terrain()
    machines()
    nodes_and_sources()
    items()

    out = Path(__file__).resolve().parent.parent / "game" / "assets" / "atlas.png"
    write_png(out, W, H, buf)
    print(f"wrote {out} ({W}x{H})")

    # An 8x nearest-neighbor preview for eyeballing, next to the atlas.
    S = 8
    big = bytearray(W * S * H * S * 4)
    for y in range(H * S):
        for x in range(W * S):
            i = ((y // S) * W + (x // S)) * 4
            j = (y * W * S + x) * 4
            big[j:j + 4] = buf[i:i + 4]
    write_png(out.with_name("atlas_preview.png"), W * S, H * S, big)
    print("wrote preview (not needed by the game; do not commit)")


if __name__ == "__main__":
    main()
