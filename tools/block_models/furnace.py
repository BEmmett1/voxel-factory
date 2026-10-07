"""The Furnace (fuel-fired tier), built with modelkit.

A squat stone body on a plinth, a riveted iron band round it, a short
chimney with an iron cap at the back, and the door: the front face is a
DECAL (fire in an iron-framed opening, soot climbing the stone above it),
with three real iron bars and a lintel standing proud of it so the door
reads in silhouette, not just in paint.

No moving part: fuel-fired, never a power node, so nothing could animate.

    python tools/block_models/furnace.py          # writes + previews
"""
import os, random, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from PIL import Image, ImageEnhance
from modelkit import Model, material, atlas_tile

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

BODY = (1, 1, 2), (15, 10, 15)          # the front face is 14 wide, 9 tall
DOOR_X = (4, 11)                         # world x of the opening
DOOR_Y = (1.5, 6)                        # world y of the opening


def stone_tiled(w, h):
    t = atlas_tile(3).convert("RGBA"); im = Image.new("RGBA", (w, h))
    for y in range(0, h, 16):
        for x in range(0, w, 16):
            im.paste(t, (x, y))
    return im


def front():
    w, h = BODY[1][0] - BODY[0][0], BODY[1][1] - BODY[0][1]
    im = stone_tiled(w, h); px = im.load(); rnd = random.Random(4)
    col = lambda wx: int(wx - BODY[0][0])
    row = lambda wy: int(BODY[1][1] - wy)                 # image rows run down
    x0, x1 = col(DOOR_X[0]), col(DOOR_X[1])
    r0, r1 = row(DOOR_Y[1]), row(DOOR_Y[0])
    iron = (58, 64, 74, 255)
    for y in range(h):
        for x in range(w):
            if x0 <= x < x1 and r0 <= y <= r1:
                px[x, y] = (rnd.choice(((255, 196, 86), (250, 150, 56), (228, 104, 38)))
                            + (255,)) if x0 < x < x1 - 1 and y > r0 else iron
            elif y < r0 and x0 - 1 <= x <= x1:            # soot climbing from the door
                if rnd.random() < 0.75 - 0.18 * (r0 - y):
                    r, g, b, a = px[x, y]; px[x, y] = (r // 3, g // 3, b // 3, a)
    return im


m = Model("furnace", kind="block")
m.material("stone", atlas_tile(3))
m.material("dark", ImageEnhance.Brightness(atlas_tile(3)).enhance(0.65))
m.material("iron", ImageEnhance.Brightness(material("pixellab_wrought_iron")).enhance(1.5))
m.material("soot", Image.new("RGBA", (16, 16), (36, 32, 34, 255)))
m.material("front", front(), decal=True)

m.box("plinth", (0.5, 0, 1.5), (15.5, 1, 15.5), "dark", faces={"down": None})
m.box("body", *BODY, "stone", faces={"north": "front"})
m.box("band", (0.75, 7, 1.75), (15.25, 8, 15.25), "iron")
m.box("lintel", (3.5, 6, 1.25), (11.5, 7, 2), "iron")
for i, x in enumerate((5.25, 7.125, 9.0)):
    m.box(f"bar_{i}", (x, DOOR_Y[0], 1.25), (x + 0.75, DOOR_Y[1], 2), "iron")
m.box("chimney", (10, 10, 10), (14, 14.5, 14), "stone")
m.box("chimney_cap", (9.5, 14.5, 9.5), (14.5, 15.5, 14.5), "iron", faces={"up": "soot"})

if __name__ == "__main__":
    m.preview(os.path.join(REPO, "models", "furnace.bbmodel"))
