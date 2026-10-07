"""The Mixing Bowl (hand-cranked tier), built with modelkit.

A wide, shallow clay bowl on a three-legged wooden stand, a murky green
mixture inside, and a wooden paddle standing in it. The paddle is the
`crank` group: it leans 22.5 degrees from the surface of the mixture and
Spins about the vertical, so it stirs round the bowl once per turn.

(The prompt laid the paddle across the rim. Standing in the mix, it can
stir: the motion is the point of the crank.)

    python tools/block_models/mixing_bowl.py          # writes + previews
"""
import os, random, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from PIL import Image
from modelkit import Model, material

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def tint(img, k, b=(0, 0, 0)):
    rgb = img.convert("RGB").split()
    return Image.merge("RGBA", (*[c.point(lambda v, k=kk, b=bb: max(0, min(255, int(v * k + b))))
                                  for c, kk, bb in zip(rgb, k, b)], img.getchannel("A")))


def mixture(size=16, seed=5):
    """Murky green, with paler swirls of residue."""
    rnd = random.Random(seed)
    im = Image.new("RGBA", (size, size))
    for y in range(size):
        for x in range(size):
            r = rnd.random()
            c = ((112, 140, 92) if r < 0.1 else (74, 100, 66) if r < 0.4
                 else (54, 78, 54) if r < 0.8 else (38, 56, 44))
            im.putpixel((x, y), c + (255,))
    return im


m = Model("mixing_bowl", kind="block")
m.material("clay", tint(material("pixellab_sandstone"), (0.82, 0.54, 0.44)))
m.material("wood", material("pixellab_worn_wood_32"))
m.material("mix", mixture())

# The tripod: three legs under the bowl.
m.box("leg_n", (7.25, 0, 4), (8.75, 6, 5.5), "wood")
m.box("leg_sw", (4, 0, 10), (5.5, 6, 11.5), "wood")
m.box("leg_se", (10.5, 0, 10), (12, 6, 11.5), "wood")
# The bowl, stepped so it reads round: a narrow body under a wide rim, the
# mixture filling the rim.
m.box("body", (4, 6, 4), (12, 9, 12), "clay")
m.box("rim_n", (2, 9, 2), (14, 11, 3.5), "clay")
m.box("rim_s", (2, 9, 12.5), (14, 11, 14), "clay")
m.box("rim_w", (2, 9, 3.5), (3.5, 11, 12.5), "clay")
m.box("rim_e", (12.5, 9, 3.5), (14, 11, 12.5), "clay")
m.box("mix", (3.5, 9, 3.5), (12.5, 10.25, 12.5), "clay", faces={"up": "mix", "down": None})

# The paddle, leaning from the surface of the mixture.
PIVOT = (8, 10.25, 8)
LEAN = dict(group="crank", rotate=("z", 22.5), origin=PIVOT)
m.group("crank", pivot=PIVOT)
m.box("blade", (7.5, 8.75, 6.75), (8.5, 11.5, 9.25), "wood", **LEAN)
m.box("shaft", (7.6, 11.5, 7.6), (8.4, 16, 8.4), "wood", **LEAN)

m.move("crank", "Spin", axis=(0, 1, 0), rate=1.0, cranked=True)

if __name__ == "__main__":
    print(m.cpp_part_anims("MixingBowl"))
    m.preview(os.path.join(REPO, "models", "mixing_bowl.bbmodel"))
