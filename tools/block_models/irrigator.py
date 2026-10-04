"""The Irrigator (unpowered), built with modelkit.

A squat iron water tank -- two crossed boxes and a domed lid, so it reads
round -- on a low frame with four legs, a round water gauge painted on its
front (a decal), and four short sprinkler arms pointing out of its sides and
tipped 22.5 degrees downward, each with a brass nozzle. Static: it spends
water, not power, so nothing on it could animate.

    python tools/block_models/irrigator.py          # writes + previews
"""
import math, os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from PIL import ImageEnhance
from modelkit import Model, material

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
iron = material("pixellab_wrought_iron").convert("RGBA")
tank = ImageEnhance.Brightness(iron).enhance(1.35)


def gauge(w=7, h=8):
    """A round dial, half full of blue water behind its glass."""
    im = tank.crop((0, 0, w, h)).copy(); px = im.load()
    cx, cy = w / 2, 3.5
    for y in range(h):
        for x in range(w):
            d = math.hypot(x + 0.5 - cx, y + 0.5 - cy)
            if d < 2.2:
                px[x, y] = (70, 120, 160, 255) if y >= cy else (210, 214, 200, 255)
            elif d < 2.9:
                px[x, y] = (176, 140, 70, 255)   # brass bezel
    return im


m = Model("irrigator", kind="block")
m.material("tank", tank)
m.material("dark", ImageEnhance.Brightness(iron).enhance(0.85))
m.material("brass", material("pixellab_brass"))
m.material("gauge", gauge(), decal=True)

for i, (x, z) in enumerate(((3, 3), (11.5, 3), (3, 11.5), (11.5, 11.5))):
    m.box(f"leg_{i}", (x, 0, z), (x + 1.5, 2, z + 1.5), "dark")
m.box("frame", (2.5, 2, 2.5), (13.5, 3, 13.5), "dark")
m.box("tank_a", (3, 3, 4.5), (13, 11, 11.5), "tank")
m.box("tank_b", (4.5, 3.1, 3), (11.5, 10.9, 13), "tank", faces={"north": "gauge"})
m.box("lid", (5, 11, 5), (11, 12.25, 11), "dark")
m.box("cap", (7, 12.25, 7), (9, 13, 9), "brass")
# The sprinkler arms, out of each side and tipped downward.
ARMS = (((13, 8, 7.5), (15.5, 9, 8.5), ("z", -22.5), (13, 8.5, 8)),
        ((0.5, 8, 7.5), (3, 9, 8.5), ("z", 22.5), (3, 8.5, 8)),
        ((7.5, 8, 13), (8.5, 9, 15.5), ("x", 22.5), (8, 8.5, 13)),
        ((7.5, 8, 0.5), (8.5, 9, 3), ("x", -22.5), (8, 8.5, 3)))
for i, (a, b, rot, origin) in enumerate(ARMS):
    m.box(f"arm_{i}", a, b, "brass", rotate=rot, origin=origin)

if __name__ == "__main__":
    m.preview(os.path.join(REPO, "models", "irrigator.bbmodel"))
