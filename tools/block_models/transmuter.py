"""The Transmuter (powered tier), built with modelkit.

A dark stone pedestal -- a plinth and a carved top whose concentric rings are
a decal -- with runes round its sides that brighten and dim over an 8-frame
strip, three iron arms rising at 120 degrees with brass claws turned in at
their tips, and a faceted violet crystal floating in the gap between them,
touching nothing. The crystal is the `crystal` group: it turns slowly and
bobs while powered.

    python tools/block_models/transmuter.py          # writes + previews
"""
import math, os, random, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from PIL import ImageEnhance
from modelkit import Model, material, atlas_tile

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
FRAMES = 8
stone = ImageEnhance.Brightness(atlas_tile(3)).enhance(0.55).convert("RGBA")


def runes(frame, size=16):
    """The same runes every frame; only their glow changes."""
    glow = 0.5 - 0.5 * math.cos(2 * math.pi * frame / FRAMES)
    im = stone.copy(); px = im.load(); rnd = random.Random(31)
    lo, hi = (70, 52, 92), (196, 150, 240)
    c = tuple(int(a + (b - a) * glow) for a, b in zip(lo, hi)) + (255,)
    for x0 in range(1, size - 1, 4):              # a rune every four texels
        for dx, dy in rnd.sample([(0, 0), (1, 0), (0, 1), (1, 1), (0, 2), (1, 2)], 4):
            px[x0 + dx, 1 + dy] = c
    return im


def rings(w=10, h=10):
    im = stone.crop((0, 0, w, h)).copy(); px = im.load()
    for y in range(h):
        for x in range(w):
            d = math.hypot(x + 0.5 - w / 2, y + 0.5 - h / 2)
            if 4.0 <= d < 4.7 or 2.2 <= d < 2.9:
                px[x, y] = (40, 34, 46, 255)
            elif 2.9 <= d < 3.3:
                px[x, y] = (128, 96, 160, 255)
    return im


m = Model("transmuter", kind="block", frames=FRAMES, frame_time=3)
m.material("stone", stone)
m.material("runes", [runes(f) for f in range(FRAMES)])
m.material("rings", rings(), decal=True)
m.material("iron", ImageEnhance.Brightness(material("pixellab_wrought_iron")).enhance(1.4))
m.material("brass", material("pixellab_brass"))
m.material("crystal", ImageEnhance.Brightness(material("pixellab_lilac_powder_32")).enhance(1.15))

SIDES = {f: "runes" for f in ("north", "south", "east", "west")}
m.box("plinth", (2, 0, 2), (14, 3, 14), "stone", faces=dict(SIDES, down=None))
m.box("top", (3, 3, 3), (13, 4.5, 13), "stone", faces={"up": "rings"})
R = 5.0
for i, ang in enumerate((math.pi / 2, math.pi / 2 + 2 * math.pi / 3, math.pi / 2 + 4 * math.pi / 3)):
    x, z = 8 + R * math.cos(ang), 8 - R * math.sin(ang)
    m.box(f"arm_{i}", (x - 0.75, 4.5, z - 0.75), (x + 0.75, 12, z + 0.75), "iron")
    tx, tz = 8 + (R - 1.25) * math.cos(ang), 8 - (R - 1.25) * math.sin(ang)
    m.box(f"claw_{i}", (tx - 0.6, 11.5, tz - 0.6), (tx + 0.6, 13, tz + 0.6), "brass")

C = (8, 9.5, 8)
TURN = dict(rotate=("y", 45), origin=C, group="crystal")
m.group("crystal", pivot=C)
m.box("crystal", (6.75, 8, 6.75), (9.25, 11, 9.25), "crystal", **TURN)
m.box("tip_up", (7.4, 11, 7.4), (8.6, 12.25, 8.6), "crystal", **TURN)
m.box("tip_down", (7.4, 6.75, 7.4), (8.6, 8, 8.6), "crystal", **TURN)

m.move("crystal", "Spin", axis=(0, 1, 0), rate=0.3)
m.move("crystal", "Bob", axis=(0, 1, 0), rate=0.5, amount=0.75 / 16.0)

if __name__ == "__main__":
    print(m.cpp_part_anims("Transmuter"))
    m.preview(os.path.join(REPO, "models", "transmuter.bbmodel"))
