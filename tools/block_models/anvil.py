"""The Anvil (hand-cranked tier), built with modelkit.

A blackened iron anvil -- horn, waist, heel -- on a squat log stump, and a
hammer lying across the face. The hammer is the `crank` group: it pivots at
the butt of its handle and strikes the face twice per turn of the crank.

The hammer is authored RAISED (22.5 degrees, an element rotation about its
pivot) so that the Rock motion, which sways the same amount either side,
spans "resting on the face" to "lifted 45 degrees" instead of swinging down
through the iron. The anvil sits low (face at 9) so the lifted head still
clears the top of the cell.

    python tools/block_models/anvil.py          # writes + previews
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from PIL import ImageEnhance
from modelkit import Model, material, atlas_tile

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

iron = material("pixellab_wrought_iron").convert("RGBA")
m = Model("anvil", kind="block")
m.material("iron", ImageEnhance.Brightness(iron).enhance(0.95))       # blackened body
m.material("face", ImageEnhance.Brightness(iron).enhance(1.75))       # worked face
m.material("bark", atlas_tile(7))
m.material("rings", atlas_tile(6))
m.material("wood", material("pixellab_worn_wood_32"))

# The stump: bark sides, ring top.
m.box("stump", (3, 0, 3), (13, 3, 13), "bark", faces={"up": "rings", "down": None})
# The anvil, horn to -X, heel to +X.
m.box("foot", (4, 3, 5), (12, 4, 11), "iron", faces={"down": None})
m.box("waist", (6, 4, 6.5), (10, 6, 9.5), "iron")
m.box("body", (3.5, 6, 5.5), (12.5, 9, 10.5), "iron", faces={"up": "face"})
m.box("horn", (1, 7, 6.5), (3.5, 9, 9.5), "iron", faces={"up": "face"})
m.box("horn_tip", (0, 7.75, 7.25), (1, 9, 8.75), "iron")
m.box("heel", (12.5, 7, 6.5), (14, 9, 9.5), "iron", faces={"up": "face"})

# The hammer, pivoting at its butt.
PIVOT = (10, 10, 13)
m.group("crank", pivot=PIVOT)
m.box("head", (8, 9, 6), (12, 11, 8), "face", group="crank",
      rotate=("x", 22.5), origin=PIVOT)
m.box("handle", (9.5, 9.5, 8), (10.5, 10.5, 13.5), "wood", group="crank",
      rotate=("x", 22.5), origin=PIVOT)

# Two blows per turn of the crank.
m.move("crank", "Rock", axis=(1, 0, 0), rate=2.0, amount=22.5, cranked=True)

if __name__ == "__main__":
    print(m.cpp_part_anims("Anvil"))
    m.preview(os.path.join(REPO, "models", "anvil.bbmodel"))
