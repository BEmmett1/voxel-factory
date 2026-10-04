"""The Grinder (powered tier), built with modelkit.

A heavy iron frame -- plinth, two uprights, a crossbar -- holding a wide
grinding wheel of grey stone on a horizontal axle, a wooden hopper above it
heaped with ore, and a chute at the front letting the powder out. The wheel
(and its axle) is the `wheel` group and turns while the grinder is powered.

The wheel is four boxes, two turned 45 degrees about the axle, so its rim is
a sixteen-sided ring and it reads ROUND as it turns. Each box is inset a hair from the last along the
axle, or their end faces would share two planes and z-fight.

    python tools/block_models/grinder.py          # writes + previews
"""
import os, random, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from PIL import Image, ImageEnhance
from modelkit import Model, material, atlas_tile

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def powder(size=16, seed=8):
    rnd = random.Random(seed); im = Image.new("RGBA", (size, size))
    for y in range(size):
        for x in range(size):
            v = rnd.randint(108, 146)
            im.putpixel((x, y), (v, v - 6, v + 8, 255))
    return im


iron = material("pixellab_wrought_iron").convert("RGBA")
m = Model("grinder", kind="block")
m.material("iron", ImageEnhance.Brightness(iron).enhance(1.4))
m.material("dark", ImageEnhance.Brightness(iron).enhance(0.9))
m.material("stone", ImageEnhance.Brightness(atlas_tile(3)).enhance(0.9))
m.material("millface", ImageEnhance.Brightness(atlas_tile(3)).enhance(1.15))
m.material("rim", ImageEnhance.Brightness(atlas_tile(3)).enhance(0.7))
m.material("wood", material("pixellab_worn_wood_32"))
m.material("ore", atlas_tile(50))
m.material("powder", powder())

m.box("plinth", (1, 0, 2), (15, 2, 14), "dark", faces={"down": None})
m.box("upright_w", (1.5, 2, 7), (3, 13, 9), "iron")
m.box("upright_e", (13, 2, 7), (14.5, 13, 9), "iron")
m.box("crossbar", (1.5, 13, 7), (14.5, 14, 9), "iron")
m.box("hopper_low", (5.5, 12.5, 5.5), (10.5, 14, 10.5), "wood")
m.box("hopper_high", (4.5, 14, 4.5), (11.5, 16, 11.5), "wood", faces={"up": "ore"})
m.box("chute", (5.5, 2, 0.5), (10.5, 3, 4), "dark", faces={"up": "powder"})

C = (8, 7, 8)
FACE = {"east": "millface", "west": "millface"}   # the round faces, lighter than the rim
m.group("wheel", pivot=C)
m.box("axle", (3, 6.5, 7.5), (13, 7.5, 8.5), "dark", group="wheel")
m.box("wheel_a", (5.5, 2.5, 5.25), (10.5, 11.5, 10.75), "rim", faces=FACE, group="wheel")
m.box("wheel_b", (5.6, 4.25, 3.5), (10.4, 9.75, 12.5), "rim", faces=FACE, group="wheel")
R = dict(group="wheel", rotate=("x", 45), origin=C)
m.box("wheel_c", (5.7, 2.5, 5.25), (10.3, 11.5, 10.75), "rim", faces=FACE, **R)
m.box("wheel_d", (5.8, 4.25, 3.5), (10.2, 9.75, 12.5), "rim", faces=FACE, **R)

m.move("wheel", "Spin", axis=(1, 0, 0), rate=0.75)

if __name__ == "__main__":
    print(m.cpp_part_anims("Grinder"))
    m.preview(os.path.join(REPO, "models", "grinder.bbmodel"))
