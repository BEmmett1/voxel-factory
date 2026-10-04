"""Demo creature for modelkit: a six-legged beetle with idle and walk clips.
Not game content (no kSpecies row) -- writes to the temp dir."""
import os, sys, tempfile
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
from modelkit import Model, atlas_tile, material, solid

m = Model("demo_beetle", kind="creature")
m.material("shell", material("pixellab_bronze"))
m.material("belly", solid((40, 34, 44)))
m.material("glass", material("pixellab_glass"))
m.group("root", pivot=(0, 0, 0))
m.group("body", pivot=(0, 4, 0), parent="root")
m.group("head", pivot=(0, 5, -5), parent="body")
m.box("shell", (-4, 3, -5), (4, 8, 5), "shell", faces={"down": "belly"}, group="body")
m.box("head", (-2.5, 3.5, -8), (2.5, 7, -5), "belly", faces={"north": "glass"}, group="head")
legs = []
for side, x in (("l", -1), ("r", 1)):
    for i, z in enumerate((-3, 0, 3)):
        n = f"leg_{side}{i}"
        m.group(n, pivot=(x * 4, 4, z), parent="body")
        m.box(n, (x * 4 - 0.5 + x * 0.5, 0, z - 0.5), (x * 4 + 0.5 + x * 2.5, 4, z + 0.5), "belly", group=n)
        legs.append((n, side, i))
idle = m.clip("idle", 2.0)
idle.pos("body", 0.0, (0, 0, 0)); idle.pos("body", 1.0, (0, 0.4, 0), "catmullrom"); idle.pos("body", 2.0, (0, 0, 0))
walk = m.clip("walk", 0.6)
for n, side, i in legs:
    a = 25 if (i % 2 == 0) == (side == "l") else -25          # alternating tripod gait
    walk.rot(n, 0.0, (a, 0, 0)); walk.rot(n, 0.3, (-a, 0, 0)); walk.rot(n, 0.6, (a, 0, 0))
walk.rot("head", 0.0, (0, 6, 0)); walk.rot("head", 0.3, (0, -6, 0)); walk.rot("head", 0.6, (0, 6, 0))
m.preview(os.path.join(tempfile.gettempdir(), "modelkit_demo", "demo_beetle.bbmodel"))
