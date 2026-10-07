"""The Compactor (powered tier), built with modelkit.

A squat, very heavy iron machine: a thick body with a square recess on top
(packed earth in its floor), a hazard-striped rim round the recess, four
thick guide posts at the corners, and the pressing plate riding between
them -- the `plate` group, which presses down into the recess while powered.

    python tools/block_models/compactor.py          # writes + previews
"""
import os, random, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from PIL import Image, ImageEnhance
from modelkit import Model, material, atlas_tile

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def dented(base, seed=2):
    im = base.copy(); px = im.load(); rnd = random.Random(seed)
    for _ in range(5):
        x, y = rnd.randrange(1, 15), rnd.randrange(1, 15)
        r, g, b, a = px[x, y]; px[x, y] = (r // 2, g // 2, b // 2, a)
        r, g, b, a = px[x + 1, y + 1]; px[x + 1, y + 1] = (min(255, r + 40), min(255, g + 40), min(255, b + 40), a)
    return im


def stripes(size=16):
    im = Image.new("RGBA", (size, size)); px = im.load()
    for y in range(size):
        for x in range(size):
            px[x, y] = (206, 162, 52, 255) if (x + y) // 3 % 2 else (40, 36, 38, 255)
    return im


iron = material("pixellab_wrought_iron").convert("RGBA")
m = Model("compactor", kind="block")
m.material("iron", dented(ImageEnhance.Brightness(iron).enhance(1.4)))
m.material("dark", ImageEnhance.Brightness(iron).enhance(0.9))
m.material("stripes", stripes())
m.material("packed", ImageEnhance.Brightness(atlas_tile(2)).enhance(0.85))

m.box("body", (1, 0, 1), (15, 6, 15), "iron", faces={"up": "packed", "down": None})
m.box("rim_n", (1, 6, 1), (15, 8, 3.5), "stripes")
m.box("rim_s", (1, 6, 12.5), (15, 8, 15), "stripes")
m.box("rim_w", (1, 6, 3.5), (3.5, 8, 12.5), "stripes")
m.box("rim_e", (12.5, 6, 3.5), (15, 8, 12.5), "stripes")
for i, (x, z) in enumerate(((1, 1), (12.5, 1), (1, 12.5), (12.5, 12.5))):
    m.box(f"post_{i}", (x, 8, z), (x + 2.5, 14.5, z + 2.5), "dark")

m.group("plate", pivot=(8, 11, 8))
m.box("plate", (3.5, 9, 3.5), (12.5, 11, 12.5), "iron", group="plate")
m.box("boss", (6, 11, 6), (10, 12.5, 10), "dark", group="plate")

m.move("plate", "Bob", axis=(0, -1, 0), rate=0.6, amount=2.5 / 16.0)

if __name__ == "__main__":
    print(m.cpp_part_anims("Compactor"))
    m.preview(os.path.join(REPO, "models", "compactor.bbmodel"))
