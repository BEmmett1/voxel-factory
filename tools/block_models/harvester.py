"""The Harvester (powered tier), built with modelkit.

A low wheeled iron chassis with a wooden collecting box behind heaped with
cut herbs, and across the front a horizontal cutting reel: four thin blades
(green-stained) at a fixed radius round an axle between two end plates. The
reel is the `reel` group and turns while the harvester is powered.

    python tools/block_models/harvester.py          # writes + previews
"""
import os, random, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from PIL import ImageEnhance
from modelkit import Model, material, atlas_tile

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
iron = material("pixellab_wrought_iron").convert("RGBA")


def stained(base, seed=4):
    im = base.copy(); px = im.load(); rnd = random.Random(seed)
    for _ in range(40):
        x, y = rnd.randrange(16), rnd.randrange(16)
        px[x, y] = rnd.choice(((86, 120, 64, 255), (64, 96, 52, 255)))
    return im


m = Model("harvester", kind="block")
m.material("iron", ImageEnhance.Brightness(iron).enhance(1.4))
m.material("blade", stained(ImageEnhance.Brightness(iron).enhance(1.7)))
m.material("dark", ImageEnhance.Brightness(iron).enhance(0.8))
m.material("wood", material("pixellab_worn_wood_32"))
m.material("herbs", ImageEnhance.Brightness(atlas_tile(48)).enhance(0.75))

m.box("chassis", (2, 3, 5.5), (14, 5, 15), "iron")
for i, (x, z) in enumerate(((0.5, 6), (14, 6), (0.5, 11), (14, 11))):
    m.box(f"wheel_{i}", (x, 0, z), (x + 1.5, 4, z + 4), "dark")
m.box("bin", (3, 5, 8), (13, 9.5, 15), "wood", faces={"up": "herbs"})

C = (8, 5, 3)
m.group("reel", pivot=C)
m.box("axle", (1.5, 4.5, 2.5), (14.5, 5.5, 3.5), "dark", group="reel")
m.box("plate_w", (1.5, 3, 1), (2, 7, 5), "iron", group="reel")
m.box("plate_e", (14, 3, 1), (14.5, 7, 5), "iron", group="reel")
m.box("blade_top", (2, 7, 2.75), (14, 7.5, 3.25), "blade", group="reel")
m.box("blade_bottom", (2, 2.5, 2.75), (14, 3, 3.25), "blade", group="reel")
m.box("blade_front", (2, 4.75, 0.5), (14, 5.25, 1), "blade", group="reel")
m.box("blade_back", (2, 4.75, 5), (14, 5.25, 5.5), "blade", group="reel")

m.move("reel", "Spin", axis=(1, 0, 0), rate=1.0)

if __name__ == "__main__":
    print(m.cpp_part_anims("Harvester"))
    m.preview(os.path.join(REPO, "models", "harvester.bbmodel"))
