"""The Forge (powered tier), built with modelkit.

An armourer's forge: a stone hearth on four iron legs, a raised rim round a
recessed bed of coals that pulses from dull red to bright orange across an
8-frame strip, a stepped iron hood on two back posts, and a small anvil on a
wooden shelf at the east side. No moving part; the strip plays while the
forge is powered.

    python tools/block_models/forge.py          # writes + previews
"""
import math, os, random, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from PIL import Image, ImageEnhance
from modelkit import Model, material, atlas_tile

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
FRAMES = 8


def coals(frame, size=16):
    """The same coal pattern every frame; only the heat changes, so the bed
    breathes rather than crawls."""
    heat = 0.5 - 0.5 * math.cos(2 * math.pi * frame / FRAMES)        # 0 .. 1 .. 0
    rnd = random.Random(21); im = Image.new("RGBA", (size, size))
    for y in range(size):
        for x in range(size):
            r = rnd.random()
            if r < 0.35:   # a glowing coal
                lo, hi = (120, 34, 24), (255, 176, 64)
            elif r < 0.75: # a warm one
                lo, hi = (74, 26, 22), (220, 92, 34)
            else:          # ash and char
                lo, hi = (40, 30, 30), (70, 40, 32)
            im.putpixel((x, y), tuple(int(a + (b - a) * heat) for a, b in zip(lo, hi)) + (255,))
    return im


iron = material("pixellab_wrought_iron").convert("RGBA")
m = Model("forge", kind="block", frames=FRAMES, frame_time=3)
m.material("iron", ImageEnhance.Brightness(iron).enhance(1.4))
m.material("dark", ImageEnhance.Brightness(iron).enhance(0.9))
m.material("stone", atlas_tile(3))
m.material("wood", material("pixellab_worn_wood_32"))
m.material("coals", [coals(f) for f in range(FRAMES)])

for i, (x, z) in enumerate(((1.5, 3.5), (9.5, 3.5), (1.5, 11), (9.5, 11))):
    m.box(f"leg_{i}", (x, 0, z), (x + 1.5, 5, z + 1.5), "dark")
m.box("hearth", (1, 5, 3), (12, 8, 13), "stone")
m.box("rim_n", (1, 8, 3), (12, 9.5, 4.5), "stone")
m.box("rim_s", (1, 8, 11.5), (12, 9.5, 13), "stone")
m.box("rim_w", (1, 8, 4.5), (2.5, 9.5, 11.5), "stone")
m.box("rim_e", (10.5, 8, 4.5), (12, 9.5, 11.5), "stone")
m.box("coal_bed", (2.5, 8, 4.5), (10.5, 8.75, 11.5), "stone", faces={"up": "coals"})
m.box("post_w", (1.5, 9.5, 11.5), (2.5, 12, 12.5), "dark")
m.box("post_e", (10.5, 9.5, 11.5), (11.5, 12, 12.5), "dark")
m.box("hood", (1.5, 12, 4), (11.5, 13.5, 12.5), "iron")
m.box("hood_top", (3.5, 13.5, 6), (9.5, 15, 10.5), "iron")
m.box("flue", (5.5, 15, 7.25), (7.5, 16, 9.25), "dark")
m.box("shelf", (12, 5, 5), (15.5, 6, 11), "wood")
m.box("anvil_foot", (12.75, 6, 6.5), (14.75, 7, 9.5), "dark")
m.box("anvil_waist", (13.25, 7, 7), (14.25, 8, 9), "dark")
m.box("anvil_face", (12.5, 8, 6), (15, 9, 10), "iron")

if __name__ == "__main__":
    m.preview(os.path.join(REPO, "models", "forge.bbmodel"))
