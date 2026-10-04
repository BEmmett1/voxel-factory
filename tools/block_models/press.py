"""The Press (powered tier), built with modelkit.

A blocky iron press: a thick base plate with a copper die on it, two heavy
uprights (a hydraulic line and a pressure dial painted on each front, as a
decal), a crossbeam, a fixed hydraulic cylinder hanging from it with copper
lines out to the uprights, and the ram -- a broad head on a rod -- which is
the `ram` group and strokes down onto the die while powered.

The rod sits inside the cylinder at rest and is long enough that at the
bottom of the stroke its top still meets the cylinder's mouth: a ram that
dropped away from its own piston would read as a falling block.

    python tools/block_models/press.py          # writes + previews
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from PIL import Image, ImageEnhance
from modelkit import Model, material

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
STROKE = 4.0

iron = material("pixellab_wrought_iron").convert("RGBA")
light = ImageEnhance.Brightness(iron).enhance(1.45)


def upright_front(w=3, h=12):
    """A copper hydraulic line down the upright and a dial near its top."""
    im = light.crop((0, 0, w, h)).copy(); px = im.load()
    for y in range(h):
        px[w - 1, y] = (176, 104, 66, 255)                # the line
    for (x, y) in ((0, 1), (1, 1), (0, 2), (1, 2)):
        px[x, y] = (226, 214, 180, 255)                   # dial face
    px[1, 1] = (150, 40, 34, 255)                         # needle
    return im


m = Model("press", kind="block")
m.material("iron", light)
m.material("dark", ImageEnhance.Brightness(iron).enhance(0.9))
m.material("copper", material("pixellab_copper"))
m.material("gauge", upright_front(), decal=True)

m.box("base", (0.5, 0, 1), (15.5, 2, 15), "dark", faces={"down": None})
m.box("die", (4.5, 2, 4.5), (11.5, 3.5, 11.5), "copper")
m.box("upright_w", (1, 2, 5), (4, 14, 11), "iron", faces={"north": "gauge"})
m.box("upright_e", (12, 2, 5), (15, 14, 11), "iron", faces={"north": "gauge"})
m.box("beam", (1, 14, 5), (15, 16, 11), "dark")
m.box("cylinder", (6.25, 10, 6.25), (9.75, 14, 9.75), "dark")
m.box("line_w", (4, 12, 7.5), (6.25, 12.75, 8.5), "copper")
m.box("line_e", (9.75, 12, 7.5), (12, 12.75, 8.5), "copper")

m.group("ram", pivot=(8, 9.5, 8))
m.box("head", (4.25, 7.5, 4.5), (11.75, 9.5, 11.5), "iron", group="ram")
m.box("rod", (7.25, 9.5, 7.25), (8.75, 14, 8.75), "copper", group="ram")

m.move("ram", "Bob", axis=(0, -1, 0), rate=0.5, amount=STROKE / 16.0)

if __name__ == "__main__":
    print(m.cpp_part_anims("Press"))
    m.preview(os.path.join(REPO, "models", "press.bbmodel"))
