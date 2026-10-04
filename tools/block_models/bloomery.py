"""The Bloomery (fuel-fired manual tier), built with modelkit.

A waist-high smelting chimney of packed clay on a stone footing: four tiers
narrowing toward the top, a glowing arched mouth painted on the front (a
DECAL, so the arch is drawn whole rather than sampled as grain), embers
glowing down the flue, a clay tuyere pipe at the side, and a dark slag scar
run down the east flank as three drips. Soot darkens the upper tiers.

No moving part: a Bloomery is fire-driven, not cranked, and never a power
node, so a part could never be animated anyway.

    python tools/block_models/bloomery.py          # writes + previews
"""
import os, random, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from PIL import Image, ImageEnhance
from modelkit import Model, material, atlas_tile

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def tint(img, k):
    rgb = img.convert("RGB").split()
    return Image.merge("RGBA", (*[c.point(lambda v, kk=kk: max(0, min(255, int(v * kk))))
                                  for c, kk in zip(rgb, k)], img.getchannel("A")))


def cracked(img, seed):
    """Hairline cracks: a few short dark random walks."""
    im = img.copy(); px = im.load(); rnd = random.Random(seed)
    for _ in range(4):
        x, y = rnd.randrange(im.width), rnd.randrange(im.height)
        for _ in range(6):
            px[x % im.width, y % im.height] = (54, 38, 32, 255)
            x += rnd.choice((-1, 0, 1)); y += 1
    return im


def embers(size=16, seed=3):
    rnd = random.Random(seed); im = Image.new("RGBA", (size, size))
    for y in range(size):
        for x in range(size):
            r = rnd.random()
            im.putpixel((x, y), ((255, 186, 72) if r < 0.15 else (226, 96, 36) if r < 0.45
                                 else (120, 44, 30) if r < 0.75 else (52, 30, 30)) + (255,))
    return im


def arch(clay, w=12, h=5):
    """The front of the footing tier: clay, with a 6-wide arched mouth full of
    fire and a dark rim round it, its top corners rounded off."""
    im = clay.crop((0, 0, w, h)).copy(); px = im.load()
    rnd = random.Random(9)
    x0, x1 = 3, 9                                  # the mouth, [x0, x1)
    for y in range(h):
        for x in range(x0 - 1, x1 + 1):
            corner = x in (x0 - 1, x1) and y == 0 or x in (x0, x1 - 1) and y == 0
            if x in (x0 - 1, x1) and y == 0:
                continue                           # clay above the shoulders
            inside = x0 <= x < x1 and y >= 1 and not (x in (x0, x1 - 1) and y == 1)
            px[x, y] = (rnd.choice(((255, 200, 90), (255, 160, 60), (240, 120, 40)) if y < 3
                                    else ((255, 220, 120), (255, 186, 72))) + (255,)
                         if inside else (40, 28, 26, 255))
    return im


clay = cracked(tint(material("pixellab_sandstone"), (0.95, 0.72, 0.58)), 1)
m = Model("bloomery", kind="block")
m.material("clay", clay)
m.material("sooty", cracked(tint(material("pixellab_sandstone"), (0.62, 0.5, 0.44)), 2))
m.material("stone", atlas_tile(3))
m.material("slag", ImageEnhance.Brightness(material("pixellab_wrought_iron")).enhance(0.8))
m.material("embers", embers())
m.material("mouth", arch(clay), decal=True)

m.box("footing", (1.5, 0, 1.5), (14.5, 1, 14.5), "stone", faces={"down": None})
m.box("tier_base", (2, 1, 2), (14, 6, 14), "clay", faces={"north": "mouth"})
m.box("tier_mid", (3, 6, 3), (13, 9.5, 13), "clay")
m.box("tier_upper", (4, 9.5, 4), (12, 12.5, 12), "sooty")
m.box("flue", (5, 12.5, 5), (11, 15, 11), "sooty", faces={"up": "embers"})
m.box("tuyere", (0.5, 2, 7), (2, 3.5, 9), "clay")
m.box("slag_low", (14, 1, 6.5), (14.75, 5, 8.25), "slag")
m.box("slag_mid", (13, 5, 6.75), (13.6, 8.5, 8), "slag")
m.box("slag_high", (12, 8.5, 7), (12.5, 11, 7.75), "slag")

if __name__ == "__main__":
    m.preview(os.path.join(REPO, "models", "bloomery.bbmodel"))
