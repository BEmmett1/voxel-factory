"""The Sifter (powered tier), built with modelkit.

A boxy iron frame -- four corner posts and two floor rails -- with a hopper
of sand above, a collecting pan below with nuggets in it, and between them
the sieve tray: a wooden frame with an iron mesh over sand. The tray is the
`tray` group and shakes side to side while the sifter runs; the shake stops
short of the posts.

    python tools/block_models/sifter.py          # writes + previews
"""
import os, random, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from PIL import Image, ImageEnhance
from modelkit import Model, material, atlas_tile

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def mesh(size=16):
    """Iron wire every other texel over sand: the screen."""
    sand = atlas_tile(51).convert("RGBA").load()
    im = Image.new("RGBA", (size, size)); px = im.load()
    for y in range(size):
        for x in range(size):
            px[x, y] = (64, 70, 80, 255) if x % 3 == 0 or y % 3 == 0 else sand[x, y]
    return im


def nuggets(size=16, seed=6):
    rnd = random.Random(seed)
    sand = ImageEnhance.Brightness(atlas_tile(51)).enhance(0.8).convert("RGBA")
    px = sand.load()
    for _ in range(10):
        x, y = rnd.randrange(size - 1), rnd.randrange(size - 1)
        c = rnd.choice(((196, 122, 70), (150, 152, 160), (212, 176, 90)))
        for dx, dy in ((0, 0), (1, 0), (0, 1)):
            px[x + dx, y + dy] = c + (255,)
    return sand


iron = material("pixellab_wrought_iron").convert("RGBA")
m = Model("sifter", kind="block")
m.material("iron", ImageEnhance.Brightness(iron).enhance(1.4))
m.material("dark", ImageEnhance.Brightness(iron).enhance(0.9))
m.material("wood", material("pixellab_worn_wood_32"))
m.material("sand", atlas_tile(51))
m.material("mesh", mesh())
m.material("nuggets", nuggets())

for i, (x, z) in enumerate(((1, 1), (13.5, 1), (1, 13.5), (13.5, 13.5))):
    m.box(f"post_{i}", (x, 0, z), (x + 1.5, 13, z + 1.5), "iron")
m.box("rail_w", (1, 1, 2.5), (2.5, 2, 13.5), "dark")
m.box("rail_e", (13.5, 1, 2.5), (15, 2, 13.5), "dark")
m.box("pan", (3, 2, 3), (13, 3.5, 13), "dark", faces={"up": "nuggets"})
m.box("hopper_low", (5, 11, 5), (11, 13, 11), "iron")
m.box("hopper_high", (2.5, 13, 2.5), (13.5, 15.5, 13.5), "iron", faces={"up": "sand"})

m.group("tray", pivot=(8, 7.5, 8))
m.box("tray", (3, 6.5, 2.75), (12.75, 8, 13.25), "wood", faces={"up": "mesh"}, group="tray")

m.move("tray", "Bob", axis=(1, 0, 0), rate=3.0, amount=0.75 / 16.0)

if __name__ == "__main__":
    print(m.cpp_part_anims("Sifter"))
    m.preview(os.path.join(REPO, "models", "sifter.bbmodel"))
