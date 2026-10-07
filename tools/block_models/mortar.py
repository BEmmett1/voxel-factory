"""The Mortar (hand-cranked tier). Rebuilds models/mortar.bbmodel.

Stone is the game's own Stone tile (atlas 3); wood and powder are PixelLab
swatches in models/materials/. Run it, then re-bake every model.
"""
import os, random, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image
from bbgen import Box, build

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MAT = os.path.join(REPO, "models", "materials")

atlas = Image.open(os.path.join(REPO, "game", "assets", "atlas.png")).convert("RGBA")
stone_tile = atlas.crop((3 * 16, 0, 4 * 16, 16))  # the game's own Stone
wood = Image.open(os.path.join(MAT, "pixellab_worn_wood_32.png")).convert("RGBA")
powder = Image.open(os.path.join(MAT, "pixellab_lilac_powder_32.png")).convert("RGBA")

def tiled(tile, w, h):
    im = Image.new("RGBA", (w, h))
    for y in range(0, h, tile.height):
        for x in range(0, w, tile.width):
            im.paste(tile, (x, y))
    return im

def shade(im, k):
    px = im.copy(); p = px.load()
    for y in range(px.height):
        for x in range(px.width):
            r, g, b, a = p[x, y]
            p[x, y] = (int(r * k), int(g * k), int(b * k), 255)
    return px

stone = tiled(stone_tile, 32, 32)
inner = shade(stone, 0.62)            # the bowl's shadowed inside
rim = stone.copy()                    # the rim, dusted with powder
rng = random.Random(7)
rp, pp = rim.load(), powder.load()
for y in range(32):
    for x in range(32):
        if rng.random() < 0.28:
            rp[x, y] = pp[x % 32, y % 32]

sheet = Image.new("RGBA", (64, 64))
sheet.paste(stone, (0, 0)); sheet.paste(rim, (32, 0))
sheet.paste(wood, (0, 32)); sheet.paste(inner.crop((0, 0, 16, 32)), (32, 32))
sheet.paste(powder.crop((0, 0, 16, 32)), (48, 32))
regions = {"stone": (0, 0, 32, 32), "rim": (32, 0, 32, 32), "wood": (0, 32, 32, 32),
           "inner": (32, 32, 16, 32), "powder": (48, 32, 16, 32)}

boxes = [
    # The low wooden cradle: two runners on the ground, two cheeks hugging the bowl.
    Box("runner_n", (2, 0, 3), (14, 2, 5), "wood"),
    Box("runner_s", (2, 0, 11), (14, 2, 13), "wood"),
    Box("cheek_w", (2, 2, 5), (3, 6, 11), "wood"),
    Box("cheek_e", (13, 2, 5), (14, 6, 11), "wood"),
    # The heavy stone bowl: a base and four thick walls, dark inside, dusty rim.
    Box("bowl_base", (4, 2, 4), (12, 4, 12), "stone"),
    Box("wall_n", (3, 4, 3), (13, 10, 5), "stone", {"south": "inner", "up": "rim"}),
    Box("wall_s", (3, 4, 11), (13, 10, 13), "stone", {"north": "inner", "up": "rim"}),
    Box("wall_w", (3, 4, 5), (5, 10, 11), "stone", {"east": "inner", "up": "rim"}),
    Box("wall_e", (11, 4, 5), (13, 10, 11), "stone", {"west": "inner", "up": "rim"}),
    # Ground powder heaped in the bowl.
    Box("powder", (5, 4, 5), (11, 7, 11), "powder"),
    # The pestle: rests in the powder, leaning on the rim. Its own group, pivoting
    # where it meets the bowl.
    Box("pestle_head", (6.5, 6, 6.5), (9.5, 8.5, 9.5), "stone",
        rot=("z", -22.5), origin=[8, 6, 8], group="crank"),
    Box("pestle_shaft", (7, 8.5, 7), (9, 16, 9), "stone",
        rot=("z", -22.5), origin=[8, 6, 8], group="crank"),
]

build("mortar", boxes, sheet, regions, groups={"crank": [8, 6, 8]},
      out_path=os.path.join(REPO, "models", "mortar.bbmodel"))
print("wrote mortar.bbmodel with", len(boxes), "elements")
