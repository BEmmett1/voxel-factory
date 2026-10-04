"""The Rain Barrel (unpowered), built with modelkit.

An open-topped barrel of curved staves -- two crossed boxes, the second inset
a hair, so it reads round -- painted with vertical staves, two iron hoops and
damp water stains, a rim standing a unit proud of the water so the top reads
OPEN, and dark rainwater filling it. Static: a rain barrel is never a power
node, so neither a strip nor a part could ever play.

    python tools/block_models/rain_barrel.py          # writes + previews
"""
import os, random, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from PIL import Image, ImageEnhance
from modelkit import Model, material

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
wood = material("pixellab_worn_wood_32").convert("RGBA").rotate(90)   # grain runs UP the staves


def staves(size=16):
    """Vertical staves with a dark seam every fourth texel, hoops at rows 3
    and 11, and damp darkening toward the foot."""
    im = wood.crop((0, 0, size, size)).copy(); px = im.load()
    for y in range(size):
        for x in range(size):
            r, g, b, a = px[x, y]
            if x % 4 == 3:
                r, g, b = r * 0.55, g * 0.55, b * 0.55
            damp = 1 - 0.3 * max(0, (y - 9) / 6)
            r, g, b = r * damp, g * damp, b * (damp + 0.05)
            if y in (3, 11):
                r, g, b = 70, 76, 86
            px[x, y] = (int(r), int(g), int(min(255, b)), a)
    return im


def water(size=16, seed=12):
    rnd = random.Random(seed); im = Image.new("RGBA", (size, size))
    for y in range(size):
        for x in range(size):
            v = rnd.random()
            im.putpixel((x, y), ((58, 84, 104) if v < 0.15 else (36, 56, 74) if v < 0.7 else (28, 44, 60)) + (255,))
    return im


m = Model("rain_barrel", kind="block")
m.material("staves", staves())
m.material("rim", ImageEnhance.Brightness(wood).enhance(0.8))
m.material("water", water())

m.box("body_a", (3, 0, 4.5), (13, 12.5, 11.5), "staves", faces={"up": "water", "down": None})
m.box("body_b", (4.5, 0, 3), (11.5, 12.4, 13), "staves", faces={"up": "water", "down": None})
# The rim, a unit proud of the water, round both boxes' outlines.
m.box("rim_a_n", (3, 12.5, 4.5), (13, 13.5, 5.5), "rim")
m.box("rim_a_s", (3, 12.5, 10.5), (13, 13.5, 11.5), "rim")
m.box("rim_a_w", (3, 12.5, 5.5), (4, 13.5, 10.5), "rim")
m.box("rim_a_e", (12, 12.5, 5.5), (13, 13.5, 10.5), "rim")
m.box("rim_b_n", (4.5, 12.5, 3), (11.5, 13.5, 4.5), "rim")
m.box("rim_b_s", (4.5, 12.5, 11.5), (11.5, 13.5, 13), "rim")

if __name__ == "__main__":
    m.preview(os.path.join(REPO, "models", "rain_barrel.bbmodel"))
