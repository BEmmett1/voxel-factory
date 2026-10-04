"""The Storage Crate (unpowered), built with modelkit.

A sturdy wooden crate and its lid: TWO elements, because crates get stacked
in rows and every element is paid for many times over. Everything else --
the planks, the iron corner brackets, the nails, the rope handle -- is
painted, as decals: one picture for the four sides and one for the lid.

    python tools/block_models/storage_crate.py          # writes + previews
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from modelkit import Model, material

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
wood = material("pixellab_worn_wood_32").convert("RGBA")
IRON, NAIL, ROPE = (64, 70, 80, 255), (150, 156, 166, 255), (176, 150, 96, 255)


def planks(w, h, rope=False):
    """Horizontal planks with dark seams, iron brackets on the four corners
    with a nail in each (on faces tall enough to hold one), and (on the sides)
    a rope handle."""
    im = wood.resize((32, 32)).crop((0, 0, w, h)).copy(); px = im.load()
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if y % 4 == 3:
                px[x, y] = (int(r * 0.5), int(g * 0.5), int(b * 0.5), a)
    for cx, cy in (((0, 0), (w - 3, 0), (0, h - 3), (w - 3, h - 3)) if h >= 6 else ()):
        for dx in range(3):
            for dy in range(3):
                if dx == 0 or dy == 0 or cx and dx == 2 or cy and dy == 2:
                    px[cx + dx, cy + dy] = IRON
        px[cx + 1, cy + 1] = NAIL
    if rope:
        mid = w // 2
        for x in range(mid - 2, mid + 2):
            px[x, 4] = ROPE
        px[mid - 3, 5] = ROPE; px[mid + 2, 5] = ROPE
    return im


m = Model("storage_crate", kind="block")
m.material("side", planks(14, 12, rope=True), decal=True)
m.material("lid_side", planks(15, 2), decal=True)
m.material("lid_top", planks(15, 15), decal=True)

SIDES = {f: "side" for f in ("north", "south", "east", "west")}
m.box("crate", (1, 0, 1), (15, 12, 15), "side", faces=dict(SIDES, up=None, down=None))
m.box("lid", (0.5, 12, 0.5), (15.5, 14, 15.5), "lid_side", faces={"up": "lid_top", "down": "lid_top"})

if __name__ == "__main__":
    m.preview(os.path.join(REPO, "models", "storage_crate.bbmodel"))
