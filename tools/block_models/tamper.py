"""The Tamper (hand-cranked tier), built with modelkit.

A squat rammed-earth press: a wide stone base plate, a wooden mould packed
with earth, two guide posts and a crossbar keeping the rammer straight, and
the rammer itself -- a heavy iron weight on a wooden shaft with a T-handle.
The rammer is the `crank` group and drops onto the mould twice per turn.

It is authored LIFTED: Bob plunges along its axis from rest, so the weight
hangs 2 above the mould and lands on it at the bottom of each stroke.

    python tools/block_models/tamper.py          # writes + previews
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from PIL import ImageEnhance
from modelkit import Model, material, atlas_tile

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

m = Model("tamper", kind="block")
m.material("stone", atlas_tile(3))
m.material("earth", atlas_tile(2))
m.material("wood", material("pixellab_worn_wood_32"))
m.material("iron", ImageEnhance.Brightness(material("pixellab_wrought_iron")).enhance(1.45))

m.box("base", (1, 0, 1), (15, 2, 15), "stone", faces={"down": None})
m.box("mould", (4, 2, 4), (12, 4, 12), "wood", faces={"up": "earth"})
m.box("post_w", (2, 2, 7), (3.5, 12, 9), "wood")
m.box("post_e", (12.5, 2, 7), (14, 12, 9), "wood")
m.box("crossbar", (2, 10.5, 7), (14, 12, 9), "wood")

m.group("crank", pivot=(8, 8, 8))
m.box("weight", (5, 6, 5), (11, 9.5, 11), "iron", group="crank")
m.box("shaft", (7.25, 9.5, 7.25), (8.75, 15, 8.75), "wood", group="crank")
m.box("handle", (7.5, 15, 4), (8.5, 16, 12), "wood", group="crank")

# Two blows per turn, each dropping the weight onto the packed earth.
m.move("crank", "Bob", axis=(0, -1, 0), rate=2.0, amount=2.0 / 16.0, cranked=True)

if __name__ == "__main__":
    print(m.cpp_part_anims("Tamper"))
    m.preview(os.path.join(REPO, "models", "tamper.bbmodel"))
