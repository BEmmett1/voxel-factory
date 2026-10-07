"""The Hand Press (hand-cranked tier), built with modelkit.

A screw press: a wooden bench, two posts and a crossbeam, an iron screw down
through the beam into a pressing plate, and a bar handle across the top. The
handle, screw and plate are the `crank` group: as the player turns, they spin
about the screw's axis and the plate presses down onto the bed.

    python tools/block_models/hand_press.py          # writes + previews
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from PIL import ImageEnhance
from modelkit import Model, material

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

m = Model("hand_press", kind="block")
m.material("wood", material("pixellab_worn_wood_32"))
# The swatch is near-black (mean RGB 40,56,68): lifted so it reads as iron,
# not a hole, under the game's lighting.
m.material("iron", ImageEnhance.Brightness(material("pixellab_wrought_iron").convert("RGBA")).enhance(1.7))
m.material("brass", material("pixellab_brass"))

# The bench: a thick plank on two feet, and the iron bed the plate presses on.
m.box("foot_w", (1, 0, 4), (4, 2, 12), "wood")
m.box("foot_e", (12, 0, 4), (15, 2, 12), "wood")
m.box("bench", (1, 2, 3), (15, 4, 13), "wood")
m.box("bed", (5, 4, 5), (11, 5, 11), "iron")
# The frame: two posts and the crossbeam the screw threads through.
m.box("post_w", (2, 4, 6), (4, 11, 10), "wood")
m.box("post_e", (12, 4, 6), (14, 11, 10), "wood")
m.box("beam", (2, 11, 6), (14, 13, 10), "wood")
# The moving assembly, about the screw's axis. At rest the plate hangs 1.5
# above the bed and the handle 2 above the beam; the stroke is 1.5, so the
# plate lands on the bed and the handle never touches the beam.
m.group("crank", pivot=(8, 9, 8))
m.box("screw", (7.25, 7.5, 7.25), (8.75, 15, 8.75), "iron", group="crank")
m.box("plate", (5.5, 6.5, 5.5), (10.5, 7.5, 10.5), "iron", group="crank")
m.box("handle", (3, 15, 7.5), (13, 16, 8.5), "iron", group="crank")
m.box("knob_w", (2, 14.75, 7), (3.5, 16, 9), "brass", group="crank")
m.box("knob_e", (12.5, 14.75, 7), (14, 16, 9), "brass", group="crank")

# One full turn of the handle turns the screw once and drives the plate down
# onto the bed and back: a press stroke per turn.
m.move("crank", "Spin", axis=(0, 1, 0), rate=1.0, cranked=True)
m.move("crank", "Bob", axis=(0, -1, 0), rate=1.0, amount=1.5 / 16.0, cranked=True)

if __name__ == "__main__":
    print(m.cpp_part_anims("HandPress"))
    m.preview(os.path.join(REPO, "models", "hand_press.bbmodel"))
