"""The moody/alchemical retexture of six generated models (Oct 2026).

Geometry is untouched; each texel is repainted from a material by what its
original colour depicted (see retexture.py). Run once over the ORIGINAL
textures -- it is not idempotent, since a second run would classify the new
colours -- then re-bake every model. Materials: PixelLab swatches in
models/materials/ plus the approved terrain tiles from the atlas (slate 3,
bark 7, leaves 8), so the models share the terrain's palette.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image
from retexture import retexture, hsl

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MAT = os.path.join(REPO, "models", "materials")
atlas = Image.open(os.path.join(REPO, "game", "assets", "atlas.png")).convert("RGBA")
def tile(t): return atlas.crop(((t % 16) * 16, (t // 16) * 16, (t % 16) * 16 + 16, (t // 16) * 16 + 16))
def mat(n): return Image.open(os.path.join(MAT, f"pixellab_{n}.png")).convert("RGBA")

M = {"slate": tile(3), "bark": tile(7), "leaves": tile(8), "bronze": mat("bronze"),
     "brass": mat("brass"), "copper": mat("copper"), "glass": mat("glass"),
     "sandstone": mat("sandstone"),
     # The wire's whole texture is 16x16, so it samples one corner of a swatch:
     # this is the copper swatch's one 16x16 window with no verdigris in it.
     "copper_clean": mat("copper").crop((7, 2, 23, 18))}
G = {"amber": (0.09, 0.55), "verdigris": (0.45, 0.40), "violet": (0.78, 0.50)}
def model(n): return os.path.join(REPO, "models", n + ".bbmodel")

def hue12(h): return int(h * 12) % 12

def conduit(r, g, b):
    h, s, l = hsl(r, g, b)
    if s > 0.15 and hue12(h) in (5, 6, 7): return "glass"
    return "bronze"

def wire(r, g, b):
    return "copper_clean"

def herb_bush(r, g, b):
    h, s, l = hsl(r, g, b)
    if s > 0.15 and hue12(h) in (2, 3, 4, 5): return "leaves"
    if s > 0.15 and hue12(h) in (0, 1): return "bark"
    return None   # pale petals and specks keep their colour

def sand_source(r, g, b):
    h, s, l = hsl(r, g, b)
    if s > 0.6 and l > 0.45: return "amber"
    return "sandstone"

def copper_source(r, g, b):
    h, s, l = hsl(r, g, b)
    if s <= 0.15: return "slate"
    if hue12(h) in (0, 1, 2): return "copper"
    if hue12(h) in (4, 5, 6): return "verdigris"
    return "slate"

def rune_core(r, g, b):
    h, s, l = hsl(r, g, b)
    if s <= 0.15: return "slate"
    k = hue12(h)
    if k in (0, 1, 2): return "brass"
    if k in (5, 6, 7): return "glass"
    if k in (8, 9, 10, 11): return "violet"
    return "slate"

JOBS = [("conduit_hub", conduit), ("wire_hub", wire), ("herb_bush", herb_bush),
        ("sand_source", sand_source), ("verdigris_standing_stone", copper_source),
        ("rune_core", rune_core)]

if __name__ == "__main__":
    only = set(sys.argv[1:])            # optionally: just these model names
    for name, fn in JOBS:
        if only and name not in only: continue
        _, counts = retexture(model(name), fn, M, G)
        print(name, counts)
