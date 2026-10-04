"""The Distiller (powered tier), built with modelkit.

A tall copper distillation column: three bulbs stacked up it, each two
crossed boxes so it reads round and each smaller than the one below, joined
by short necks; a pressure gauge painted on the lowest (a decal); a top tube
feeding a condenser that runs across and down the east side through four
coil rings into a glass collecting vessel at the base. Static -- the prompt
names no moving part.

    python tools/block_models/distiller.py          # writes + previews
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from PIL import ImageEnhance
from modelkit import Model, material

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
copper = material("pixellab_copper").convert("RGBA")


def gauge(w=7, h=4):
    im = copper.crop((0, 0, w, h)).copy(); px = im.load()
    for x in range(2, 5):
        for y in range(0, 3):
            px[x, y] = (226, 214, 180, 255)
    px[3, 1] = (150, 40, 34, 255); px[4, 0] = (150, 40, 34, 255)
    for x in range(2, 5):
        px[x, 3] = (60, 50, 44, 255)
    return im


m = Model("distiller", kind="block")
m.material("copper", copper)
m.material("dark_copper", ImageEnhance.Brightness(copper).enhance(0.7))
m.material("iron", ImageEnhance.Brightness(material("pixellab_wrought_iron")).enhance(1.2))
m.material("glass", material("pixellab_glass"))
m.material("gauge", gauge(), decal=True)


def bulb(name, y0, y1, r, rc, faces=None):
    """Two crossed boxes, the second inset a hair in y so no faces coincide."""
    m.box(name + "_a", (8 - r, y0, 8 - rc), (8 + r, y1, 8 + rc), "copper", faces=faces)
    m.box(name + "_b", (8 - rc, y0 + 0.1, 8 - r), (8 + rc, y1 - 0.1, 8 + r), "copper")


m.box("plinth", (2.5, 0, 2.5), (11, 1, 13.5), "iron", faces={"down": None})
bulb("bulb_low", 1, 5, 3.5, 2.25, faces={"north": "gauge"})
m.box("neck_low", (7, 5, 7), (9, 6, 9), "dark_copper")
bulb("bulb_mid", 6, 9, 3, 1.75)
m.box("neck_mid", (7.25, 9, 7.25), (8.75, 10, 8.75), "dark_copper")
bulb("bulb_top", 10, 12.75, 2.5, 1.5)
m.box("top_tube", (7.4, 12.75, 7.4), (8.6, 15.5, 8.6), "dark_copper")
m.box("condenser_over", (8.6, 14.5, 7.4), (13.6, 15.5, 8.6), "dark_copper")
m.box("condenser_down", (12.4, 4, 7.4), (13.6, 14.5, 8.6), "dark_copper")
for i, y in enumerate((12, 10, 8, 6)):
    m.box(f"coil_{i}", (11.5, y, 6.5), (14.5, y + 0.75, 9.5), "copper")
m.box("vessel", (11, 0, 5.5), (15, 3.25, 10.5), "glass")
m.box("vessel_neck", (12, 3.25, 6.75), (14, 4, 9.25), "glass")

if __name__ == "__main__":
    m.preview(os.path.join(REPO, "models", "distiller.bbmodel"))
