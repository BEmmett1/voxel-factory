"""The Compost Heap (hand-cranked tier), built with modelkit.

An open bin of rough planks, heaped with dark compost (straw and peelings
painted through it), and a turning fork leaning out of the middle. The fork
is the `crank` group: it leans 22.5 degrees from a pivot at the heap's
surface, so spinning it about the vertical sweeps a cone -- the fork stirring
round the bin -- while a small Bob digs it in twice per turn (the Mortar's
pestle rig, one bin over).

    python tools/block_models/compost_heap.py          # writes + previews
"""
import os, random, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from PIL import Image, ImageEnhance
from modelkit import Model, material

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def compost(size=16, seed=11):
    """Dark crumb with straw flecks and the odd green peeling."""
    rnd = random.Random(seed)
    im = Image.new("RGBA", (size, size))
    for y in range(size):
        for x in range(size):
            r = rnd.random()
            c = ((176, 150, 82) if r < 0.08 else (98, 120, 62) if r < 0.13
                 else (88, 62, 44) if r < 0.45 else (60, 42, 34) if r < 0.8 else (40, 30, 28))
            im.putpixel((x, y), c + (255,))
    # Straw lies in short strokes, not single pixels.
    for _ in range(3):
        x, y = rnd.randrange(size - 3), rnd.randrange(size)
        for k in range(3):
            im.putpixel((x + k, y), (164, 140, 84, 255))
    return im


m = Model("compost_heap", kind="block")
m.material("wood", ImageEnhance.Brightness(material("pixellab_worn_wood_32")).enhance(0.9))
m.material("handle", material("pixellab_worn_wood_32"))
m.material("compost", compost())
m.material("iron", ImageEnhance.Brightness(material("pixellab_wrought_iron")).enhance(1.6))

# The bin: four plank walls round a heap of compost.
m.box("wall_n", (1, 0, 1), (15, 8, 2.5), "wood")
m.box("wall_s", (1, 0, 13.5), (15, 8, 15), "wood")
m.box("wall_w", (1, 0, 2.5), (2.5, 8, 13.5), "wood")
m.box("wall_e", (13.5, 0, 2.5), (15, 8, 13.5), "wood")
m.box("fill", (2.5, 0, 2.5), (13.5, 6, 13.5), "compost", faces={"down": None})
m.box("mound", (4.5, 6, 4.5), (11.5, 7, 11.5), "compost", faces={"down": None})

# The fork, leaning from where it enters the heap.
PIVOT = (8, 7, 8)
LEAN = dict(group="crank", rotate=("z", 22.5), origin=PIVOT)
m.group("crank", pivot=PIVOT)
m.box("shaft", (7.5, 8.25, 7.5), (8.5, 15, 8.5), "handle", **LEAN)
m.box("grip", (6.5, 14.25, 7.5), (9.5, 15.25, 8.5), "handle", **LEAN)
m.box("ferrule", (6.25, 7.5, 7.75), (9.75, 8.25, 8.25), "iron", **LEAN)
for i, x in enumerate((6.25, 7.75, 9.25)):
    m.box(f"tine_{i}", (x, 5, 7.75), (x + 0.5, 7.5, 8.25), "iron", **LEAN)

m.move("crank", "Spin", axis=(0, 1, 0), rate=1.0, cranked=True)
m.move("crank", "Bob", axis=(0, -1, 0), rate=2.0, amount=1.0 / 16.0, cranked=True)

if __name__ == "__main__":
    print(m.cpp_part_anims("CompostHeap"))
    m.preview(os.path.join(REPO, "models", "compost_heap.bbmodel"))
