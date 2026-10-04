"""The Infusion Stand (hand-cranked tier), built with modelkit.

A slender apothecary stand: a wooden base, an unlit brass burner, two thin
uprights and a crossbar clasping a glass phial half full of a violet
infusion, and a little spoked wheel on the side of the frame. The wheel is
the `crank` group and turns once per turn of the crank.

    python tools/block_models/infusion_stand.py          # writes + previews
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from PIL import Image, ImageEnhance
from modelkit import Model, material

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

m = Model("infusion_stand", kind="block")
m.material("wood", material("pixellab_worn_wood_32"))
m.material("brass", material("pixellab_brass"))
m.material("glass", material("pixellab_glass"))
m.material("infusion", ImageEnhance.Brightness(material("pixellab_lilac_powder_32")).enhance(0.75))
m.material("soot", Image.new("RGBA", (16, 16), (40, 34, 36, 255)))

m.box("base", (3, 0, 3), (13, 1.5, 13), "wood", faces={"down": None})
m.box("burner", (6, 1.5, 6), (10, 4, 10), "brass")
m.box("wick", (7.5, 4, 7.5), (8.5, 4.75, 8.5), "soot")
m.box("post_w", (3.5, 1.5, 7.5), (4.5, 14, 8.5), "wood")
m.box("post_e", (11.5, 1.5, 7.5), (12.5, 14, 8.5), "wood")
m.box("crossbar", (4.5, 10, 7.5), (11.5, 11, 8.5), "brass")
# The phial: infusion below, clear glass above, a neck and a cork.
m.box("phial_fill", (6.5, 6, 6.5), (9.5, 8.5, 9.5), "infusion")
m.box("phial_glass", (6.5, 8.5, 6.5), (9.5, 10, 9.5), "glass")
m.box("neck", (7.25, 10, 7.25), (8.75, 12.5, 8.75), "glass")
m.box("cork", (7.5, 12.5, 7.5), (8.5, 13.5, 8.5), "wood")

# The side wheel, turning about X on the east upright.
PIVOT = (13, 9.5, 8)
m.group("crank", pivot=PIVOT)
m.box("spoke_h", (12.5, 9, 5), (13.5, 10, 11), "brass", group="crank")
m.box("spoke_v", (12.5, 6.5, 7.5), (13.5, 12.5, 8.5), "brass", group="crank")
m.box("knob", (13.5, 11.5, 7.5), (14.75, 12.5, 8.5), "wood", group="crank")

m.move("crank", "Spin", axis=(1, 0, 0), rate=1.0, cranked=True)

if __name__ == "__main__":
    print(m.cpp_part_anims("InfusionStand"))
    m.preview(os.path.join(REPO, "models", "infusion_stand.bbmodel"))
