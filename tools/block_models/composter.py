"""The Composter (powered tier), built with modelkit.

A slatted wooden drum lying on its side in an iron cradle: the drum is four
boxes, two turned 45 degrees about its axle (the Grinder's trick, so it reads
round), its staves painted with dark compost showing through the gaps and
iron hoops at either end, and a hinged hatch on its surface. The drum and
hatch are the `drum` group and roll slowly about the axle while powered.

    python tools/block_models/composter.py          # writes + previews
"""
import os, random, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from PIL import Image, ImageEnhance
from modelkit import Model, material

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
wood = material("pixellab_worn_wood_32").convert("RGBA")


def staves(size=16):
    """Planks running along the axle, a dark gap of compost every fourth row,
    and an iron hoop two texels in from each end."""
    im = wood.crop((0, 0, size, size)).copy(); px = im.load(); rnd = random.Random(3)
    for y in range(size):
        for x in range(size):
            if y % 4 == 3:
                px[x, y] = rnd.choice(((52, 36, 30, 255), (66, 46, 36, 255), (40, 30, 28, 255)))
            if x in (2, 13):
                px[x, y] = (70, 76, 86, 255)
    return im


iron = material("pixellab_wrought_iron").convert("RGBA")
m = Model("composter", kind="block")
m.material("staves", staves())
m.material("end", ImageEnhance.Brightness(wood).enhance(0.7))
m.material("iron", ImageEnhance.Brightness(iron).enhance(1.4))
m.material("dark", ImageEnhance.Brightness(iron).enhance(0.9))

m.box("rail_n", (0.75, 0, 3), (15.25, 1, 4.5), "dark", faces={"down": None})
m.box("rail_s", (0.75, 0, 11.5), (15.25, 1, 13), "dark", faces={"down": None})
m.box("cradle_w", (0.75, 1, 5), (2.25, 8, 11), "iron")
m.box("cradle_e", (13.75, 1, 5), (15.25, 8, 11), "iron")

C = (8, 7, 8)
ENDS = {"east": "end", "west": "end"}
m.group("drum", pivot=C)
m.box("axle", (2.25, 6.5, 7.5), (13.75, 7.5, 8.5), "dark", group="drum")
m.box("drum_a", (2.5, 1.5, 5.75), (13.5, 12.5, 10.25), "staves", faces=ENDS, group="drum")
m.box("drum_b", (2.6, 4.75, 2.5), (13.4, 9.25, 13.5), "staves", faces=ENDS, group="drum")
R = dict(group="drum", rotate=("x", 45), origin=C)
m.box("drum_c", (2.7, 1.5, 5.75), (13.3, 12.5, 10.25), "staves", faces=ENDS, **R)
m.box("drum_d", (2.8, 4.75, 2.5), (13.2, 9.25, 13.5), "staves", faces=ENDS, **R)
m.box("hatch", (6, 12.5, 6.25), (10, 13.25, 9.75), "iron", group="drum")

m.move("drum", "Spin", axis=(1, 0, 0), rate=0.2)

if __name__ == "__main__":
    print(m.cpp_part_anims("Composter"))
    m.preview(os.path.join(REPO, "models", "composter.bbmodel"))
