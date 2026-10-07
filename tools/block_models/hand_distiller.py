"""The Hand Distiller (hand-cranked tier), built with modelkit.

Taller and thinner than the Still: a stone footing, a narrow copper column
with two brass collecting rings and a cap, a spout near the base feeding a
small glass jar, and a hand wheel low on the front. The wheel is the `crank`
group and turns about Z once per turn.

    python tools/block_models/hand_distiller.py          # writes + previews
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from PIL import ImageEnhance
from modelkit import Model, material, atlas_tile

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

m = Model("hand_distiller", kind="block")
m.material("stone", atlas_tile(3))
m.material("copper", material("pixellab_copper"))
m.material("brass", material("pixellab_brass"))
m.material("glass", material("pixellab_glass"))
m.material("iron", ImageEnhance.Brightness(material("pixellab_wrought_iron")).enhance(1.6))

m.box("footing", (4, 0, 4), (12, 2, 12), "stone", faces={"down": None})
m.box("column", (6, 2, 6), (10, 15, 10), "copper")
m.box("ring_low", (5, 7, 5), (11, 8, 11), "brass")
m.box("ring_high", (5, 11, 5), (11, 12, 11), "brass")
m.box("cap", (6.75, 15, 6.75), (9.25, 16, 9.25), "brass")
m.box("spout", (10, 4, 7.5), (13.5, 5, 8.5), "copper")
m.box("jar", (11.5, 0, 6), (15, 3.5, 10), "glass")

PIVOT = (8, 5, 4)
m.group("crank", pivot=PIVOT)
m.box("spoke_h", (5, 4.5, 3.5), (11, 5.5, 4.5), "iron", group="crank")
m.box("spoke_v", (7.5, 2, 3.5), (8.5, 8, 4.5), "iron", group="crank")

m.move("crank", "Spin", axis=(0, 0, 1), rate=1.0, cranked=True)

if __name__ == "__main__":
    print(m.cpp_part_anims("HandDistiller"))
    m.preview(os.path.join(REPO, "models", "hand_distiller.bbmodel"))
