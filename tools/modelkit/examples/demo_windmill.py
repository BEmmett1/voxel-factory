"""Demo block for modelkit: a little windmill whose sails spin on the clock and
whose hub lamp breathes. Not game content -- writes to the temp dir."""
import os, sys, tempfile
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
from modelkit import Model, atlas_tile, material

m = Model("demo_windmill", kind="block")
m.material("wood", material("pixellab_worn_wood_32"))
m.material("stone", atlas_tile(3))
m.material("brass", material("pixellab_brass"))
m.group("sails", pivot=(8, 11, 4))
m.group("hub", pivot=(8, 11, 3.5))
m.box("base", (3, 0, 3), (13, 2, 13), "stone")
m.box("tower", (5, 2, 5), (11, 12, 11), "wood")
m.box("cap", (4, 12, 4), (12, 14, 12), "stone")
m.box("hub", (7, 10, 3), (9, 12, 5), "brass", group="hub")
m.box("sail_v", (7.5, 6.5, 3.5), (8.5, 15.5, 4), "wood", group="sails")
m.box("sail_h", (3.5, 10.5, 3.5), (12.5, 11.5, 4), "wood", group="sails")
m.move("sails", "Spin", axis=(0, 0, 1), rate=0.5)
m.move("hub", "Pulse", rate=1.0, amount=0.08)
print(m.cpp_part_anims("DemoWindmill"))
m.preview(os.path.join(tempfile.gettempdir(), "modelkit_demo", "demo_windmill.bbmodel"))
