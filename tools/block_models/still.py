"""The Still (hand-cranked tier), built with modelkit.

A squat copper pot (foot, belly, shoulder, domed lid -- stepped so it reads
round), a copper arm running from the dome across and down into a glass
collecting jar, and a valve wheel on the pot's front. The wheel is the
`crank` group and turns about Z once per turn. The copper swatch already
carries the verdigris the prompt asks for.

    python tools/block_models/still.py          # writes + previews
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from PIL import ImageEnhance
from modelkit import Model, material

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

m = Model("still", kind="block")
m.material("copper", material("pixellab_copper"))
m.material("dark_copper", ImageEnhance.Brightness(material("pixellab_copper")).enhance(0.7))
m.material("glass", material("pixellab_glass"))
m.material("brass", material("pixellab_brass"))

m.box("foot", (2.5, 0, 5), (9.5, 1, 12), "dark_copper", faces={"down": None})
m.box("belly", (1.5, 1, 4), (10.5, 7, 13), "copper")
m.box("shoulder", (2.5, 7, 5), (9.5, 8.5, 12), "copper")
m.box("dome", (4, 8.5, 6.5), (8, 10, 10.5), "copper")
m.box("arm_across", (5.5, 10, 8), (13.5, 11, 9), "dark_copper")
m.box("arm_down", (12.5, 5.5, 8), (13.5, 10, 9), "dark_copper")
m.box("jar", (11, 0, 6.5), (15, 4, 10.5), "glass")
m.box("jar_neck", (12, 4, 7.5), (14, 5.5, 9.5), "glass")

PIVOT = (6, 4, 3.5)
m.group("crank", pivot=PIVOT)
m.box("spoke_h", (3.5, 3.5, 3), (8.5, 4.5, 4), "brass", group="crank")
m.box("spoke_v", (5.5, 1.5, 3), (6.5, 6.5, 4), "brass", group="crank")

m.move("crank", "Spin", axis=(0, 0, 1), rate=1.0, cranked=True)

if __name__ == "__main__":
    print(m.cpp_part_anims("Still"))
    m.preview(os.path.join(REPO, "models", "still.bbmodel"))
