"""The Generator (powered tier), built with modelkit.

An iron firebox on four stubby legs, riveted, with a warning-stripe band
round its foot, a barred grate on the front that FLICKERS across an 8-frame
texture strip, a short exhaust stack, and a flywheel on the east side. The
flywheel is the `flywheel` group and spins while the generator runs.

Both the strip and the spin play only while the block is energized -- for a
generator, while it is burning on a network that wants its power -- which is
exactly when a real one would be roaring.

The wheel is eight rim pieces (four square, four turned 45 degrees about the
axle) round two spokes and a hub, so it reads ROUND as it turns.

    python tools/block_models/generator.py          # writes + previews
"""
import os, random, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from PIL import Image, ImageEnhance
from modelkit import Model, material

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
FRAMES = 8
BODY = (2, 2, 3), (12, 11, 13)          # front face 10 wide, 9 tall
GRATE_X, GRATE_Y = (4, 10), (4.5, 9)

iron = ImageEnhance.Brightness(material("pixellab_wrought_iron").convert("RGBA")).enhance(1.4)


def riveted(base):
    im = base.copy(); px = im.load()
    for x in range(1, 16, 3):
        for y in (1, 14):
            px[x, y] = (150, 158, 168, 255); px[y, x] = (150, 158, 168, 255)
    return im


def stripes(size=16):
    im = Image.new("RGBA", (size, size)); px = im.load()
    for y in range(size):
        for x in range(size):
            px[x, y] = (206, 162, 52, 255) if (x + y) // 3 % 2 else (40, 36, 38, 255)
    return im


def front(frame):
    """The firebox front: riveted iron, the grate opening full of fire whose
    brightness rises and falls over the strip, soot above it."""
    w, h = BODY[1][0] - BODY[0][0], BODY[1][1] - BODY[0][1]
    im = riveted(iron).crop((0, 0, w, h)); px = im.load()
    rnd = random.Random(100 + frame)
    heat = 0.65 + 0.35 * abs(((frame * 3) % FRAMES) / (FRAMES / 2) - 1)   # flicker
    col = lambda wx: int(wx - BODY[0][0]); row = lambda wy: int(BODY[1][1] - wy)
    for y in range(row(GRATE_Y[1]), row(GRATE_Y[0])):
        for x in range(col(GRATE_X[0]), col(GRATE_X[1])):
            r = rnd.random() * heat + (1 - heat) * 0.35 + 0.1
            c = (255, 214, 110) if r > 0.75 else (250, 150, 52) if r > 0.45 else (196, 72, 30) if r > 0.2 else (90, 36, 28)
            px[x, y] = c + (255,)
    for x in range(col(GRATE_X[0]), col(GRATE_X[1])):        # soot over the grate
        for y in range(0, row(GRATE_Y[1])):
            if rnd.random() < 0.5 - 0.15 * (row(GRATE_Y[1]) - 1 - y):
                rr, g, b, a = px[x, y]; px[x, y] = (rr // 2, g // 2, b // 2, a)
    return im


m = Model("generator", kind="block", frames=FRAMES, frame_time=2)
m.material("iron", riveted(iron))
m.material("dark", ImageEnhance.Brightness(iron).enhance(0.7))
m.material("stripes", stripes())
m.material("brass", material("pixellab_brass"))
m.material("soot", Image.new("RGBA", (16, 16), (34, 30, 32, 255)))
m.material("front", [front(f) for f in range(FRAMES)], decal=True)

for i, (x, z) in enumerate(((2.5, 3.5), (9.5, 3.5), (2.5, 10.5), (9.5, 10.5))):
    m.box(f"leg_{i}", (x, 0, z), (x + 2, 2, z + 2), "dark")
m.box("body", *BODY, "iron", faces={"north": "front"})
m.box("band", (1.75, 2, 2.75), (12.25, 3.5, 13.25), "stripes")
for i, x in enumerate((5.75, 7.75)):
    m.box(f"bar_{i}", (x, GRATE_Y[0], 2.25), (x + 0.75, GRATE_Y[1], 3), "dark")
m.box("stack", (4, 11, 8), (7, 14.5, 11), "dark")
m.box("stack_cap", (3.5, 14.5, 7.5), (7.5, 15.5, 11.5), "iron", faces={"up": "soot"})
m.box("bearing", (12, 5.5, 7), (13.25, 7.5, 9), "dark")

# The flywheel, centred on its axle.
C = (14.25, 6.5, 8)
m.group("flywheel", pivot=C)
W = dict(group="flywheel")
X0, X1 = 13.5, 15
m.box("rim_top", (X0, 10.5, 5.5), (X1, 11.5, 10.5), "iron", **W)
m.box("rim_bottom", (X0, 1.5, 5.5), (X1, 2.5, 10.5), "iron", **W)
m.box("rim_front", (X0, 4, 3.5), (X1, 9, 4.5), "iron", **W)
m.box("rim_back", (X0, 4, 11.5), (X1, 9, 12.5), "iron", **W)
D = dict(group="flywheel", rotate=("x", 45), origin=C)
m.box("rim_d1", (X0, 10.5, 5.5), (X1, 11.5, 10.5), "iron", **D)
m.box("rim_d2", (X0, 1.5, 5.5), (X1, 2.5, 10.5), "iron", **D)
m.box("rim_d3", (X0, 4, 3.5), (X1, 9, 4.5), "iron", **D)
m.box("rim_d4", (X0, 4, 11.5), (X1, 9, 12.5), "iron", **D)
m.box("spoke_v", (13.75, 2.5, 7.5), (14.75, 10.5, 8.5), "dark", **W)
m.box("spoke_h", (13.75, 6, 4.5), (14.75, 7, 11.5), "dark", **W)
m.box("hub", (13.25, 5.5, 7), (15.25, 7.5, 9), "brass", **W)

m.move("flywheel", "Spin", axis=(1, 0, 0), rate=0.5)

if __name__ == "__main__":
    print(m.cpp_part_anims("Generator"))
    m.preview(os.path.join(REPO, "models", "generator.bbmodel"))
