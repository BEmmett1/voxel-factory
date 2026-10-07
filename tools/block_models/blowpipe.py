"""The Blowpipe (hand-cranked tier), built with modelkit.

A glassblower's bench: two trestles and a plank top, a stone bowl of embers,
a long pipe resting over it on two forked iron rests with a gather of glass
in the middle, and a pair of bellows at the back. The bellows' top board and
handle are the `crank` group, hinged at the back edge: each turn of the crank
pumps them once.

    python tools/block_models/blowpipe.py          # writes + previews
"""
import os, random, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from PIL import Image, ImageEnhance
from modelkit import Model, material, atlas_tile

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def embers(size=16, seed=7):
    """Coals: dark char with orange and ember-red specks, painted not modelled."""
    rnd = random.Random(seed)
    im = Image.new("RGBA", (size, size))
    for y in range(size):
        for x in range(size):
            r = rnd.random()
            c = ((255, 170, 60) if r < 0.12 else (214, 84, 34) if r < 0.34
                 else (96, 40, 30) if r < 0.6 else (46, 34, 36))
            im.putpixel((x, y), c + (255,))
    return im


wood = material("pixellab_worn_wood_32")
m = Model("blowpipe", kind="block")
m.material("wood", wood)
# Leather: the wood's grain, recoloured oxblood so the bag reads apart from the boards.
m.material("leather", Image.merge("RGBA", (*[ch.point(lambda v, k=k: int(v * k)) for ch, k in
          zip(wood.convert("RGB").split(), (0.75, 0.38, 0.36))], wood.getchannel("A"))))
m.material("iron", ImageEnhance.Brightness(material("pixellab_wrought_iron")).enhance(1.6))
m.material("brass", material("pixellab_brass"))
# The gather is molten: the glass swatch's shading, pushed to a hot amber.
glass = material("pixellab_glass")
m.material("hot_glass", Image.merge("RGBA", (*[ch.point(lambda v, k=k, b=b: min(255, int(v * k + b))) for ch, k, b in
           zip(glass.convert("L").convert("RGB").split(), (0.9, 0.55, 0.25), (110, 60, 10))], glass.getchannel("A"))))
m.material("stone", atlas_tile(3))
m.material("embers", embers())

# The bench.
m.box("trestle_w", (2, 0, 4), (4, 5, 12), "wood")
m.box("trestle_e", (12, 0, 4), (14, 5, 12), "wood")
m.box("top", (1, 5, 2), (15, 6.5, 14.5), "wood")
# The ember bowl, its coals painted on the top face.
m.box("bowl", (5, 6.5, 4.5), (11, 8, 10.5), "stone", faces={"up": "embers"})
# The pipe on its two rests, a gather of glass hanging over the coals.
m.box("rest_w", (2.5, 6.5, 7), (3.5, 10, 8), "iron")
m.box("rest_e", (12.5, 6.5, 7), (13.5, 10, 8), "iron")
m.box("pipe", (1, 10, 7.1), (15, 10.8, 7.9), "brass")
m.box("gather", (7.25, 9.25, 6.5), (9.25, 11.25, 8.5), "hot_glass")
# The bellows: bottom board and leather, then the hinged top board + handle.
m.box("bellows_base", (5, 6.5, 11), (11, 7.25, 14.5), "wood")
m.box("bellows_bag", (5.5, 7.25, 11.25), (10.5, 8.75, 14.25), "leather")
PIVOT = (8, 9.1, 11)
m.group("crank", pivot=PIVOT)
m.box("bellows_lid", (5, 8.75, 11), (11, 9.5, 14.5), "wood", group="crank")
m.box("handle", (7.5, 8.9, 14.5), (8.5, 9.6, 16), "wood", group="crank")

# One pump per turn: the lid lifts at the front and presses back down.
m.move("crank", "Rock", axis=(1, 0, 0), rate=1.0, amount=10.0, cranked=True)

if __name__ == "__main__":
    print(m.cpp_part_anims("Blowpipe"))
    m.preview(os.path.join(REPO, "models", "blowpipe.bbmodel"))
