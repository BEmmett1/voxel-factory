#!/usr/bin/env python3
"""Generate models/herb_crop_<n>.bbmodel -- the four herb growth stages.

Pure stdlib, like make_atlas.py / make_sounds.py / make_test_model.py. These are
the SOURCE models for tools/bbmodel_to_shape.py, so after running this, re-bake
(over every model at once -- the bake packs one sheet):

    python tools/make_crop_models.py
    python tools/bbmodel_to_shape.py models/*.bbmodel

A crop is the classic crossed-plane model: two vertical planes at the cell
centre, one flat on X and one flat on Z, forming a "+" seen from above. Two
axis-aligned planes need no element rotation at all. Each plane bakes to 2
quads, so a whole crop is 4 quads -- roughly a hundredth of the Infuser, which
is the point: farming is the first feature to place shaped blocks in BULK, and
the quad budget is the real constraint on the model.

The texture is TRANSPARENT outside the plant. That is what makes this the first
content to actually exercise the alpha cutout in voxel.frag, which has shipped
inert since July 2026 waiting for exactly this.

Unlike the four machine models these are generated rather than hand-authored in
Blockbench, because they are four near-identical silhouettes that differ only by
height -- and because a stage set wants to be re-tunable in one place. Replace
any of them with a hand-made model of the same name if you want to paint one.
It overwrites hand edits!
"""

import base64
import json
import struct
import uuid
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
W = H = 16  # texture size, and the cell is 16 model units too

STEM = (74, 124, 52)
LEAF = (108, 166, 72)
LEAF_HI = (146, 200, 100)
FLOWER = (186, 142, 214)
SEED = (232, 206, 128)

# Height in model units per stage. Stage 3 is ripe -- the Harvester takes only
# that one, and it is the only stage tall enough to read as "done" at a glance.
STAGE_HEIGHTS = [4, 8, 12, 16]


def uid(name):
    return str(uuid.uuid5(uuid.NAMESPACE_DNS, f"{name}.voxel-factory"))


def write_png(w, h, rgba):
    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    raw = b"".join(b"\x00" + bytes(rgba[y * w * 4:(y + 1) * w * 4]) for y in range(h))
    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) +
            chunk(b"IEND", b""))


def make_texture(stage):
    """Plant silhouette on transparent ground, bottom-aligned in the sheet.

    Texture row 15 is ground level and row (16 - height) is the plant's top,
    which is exactly the band the face's uv rect addresses (v runs DOWN).
    """
    px = bytearray(W * H * 4)  # all zero = fully transparent

    def put(x, row, c):
        if 0 <= x < W and 0 <= row < H:
            i = (row * W + x) * 4
            px[i:i + 4] = bytes((*c, 255))

    def run(x0, x1, row, c):
        for x in range(x0, x1):
            put(x, row, c)

    h = STAGE_HEIGHTS[stage]
    top = H - h  # texture row of the plant's tip

    # Stem: two texels wide, ground up to the tip.
    for row in range(top, H):
        run(7, 9, row, STEM)

    # Leaves: pairs stepping out from the stem as the plant grows. Each pair sits
    # a little below the tip so the silhouette tapers.
    tiers = [(0.30, 3), (0.55, 4), (0.80, 5)][:max(1, stage + 1)]
    for frac, reach in tiers:
        row = int(top + (h - 1) * frac)
        if row >= H - 1:
            continue
        run(7 - reach, 7, row, LEAF)
        run(9, 9 + reach, row, LEAF)
        run(7 - reach, 7 - reach + 1, row - 1, LEAF_HI)
        run(8 + reach, 9 + reach, row - 1, LEAF_HI)

    if stage == 3:
        # Ripe: flower heads and a seed cluster at the crown.
        run(5, 11, top, FLOWER)
        run(6, 10, top + 1, FLOWER)
        run(7, 9, top + 2, SEED)
    elif stage == 2:
        run(6, 10, top, LEAF_HI)

    return px, h


def plane(name, el_uuid, height, flat_axis):
    """One vertical plane through the cell centre, flat on X (0) or Z (2).

    Only the two faces perpendicular to the flat axis carry any area; the bake
    drops the other four itself, so listing all six here costs nothing and keeps
    the model editable in Blockbench.
    """
    frm = [0.0, 0.0, 0.0]
    to = [16.0, float(height), 16.0]
    frm[flat_axis] = to[flat_axis] = 8.0
    uv = [0, H - height, W, H]
    return {
        "name": name, "type": "cube", "uuid": el_uuid, "color": 0,
        "from": frm, "to": to, "origin": [8, 0, 8],
        "faces": {f: {"uv": list(uv), "texture": 0}
                  for f in ("north", "south", "east", "west", "up", "down")},
    }


def main():
    out = ROOT / "models"
    out.mkdir(parents=True, exist_ok=True)
    for stage in range(len(STAGE_HEIGHTS)):
        pixels, h = make_texture(stage)
        png = write_png(W, H, pixels)
        name = f"herb_crop_{stage}"
        model = {
            "meta": {"format_version": "4.5", "model_format": "java_block",
                     "box_uv": False},
            "name": name,
            "resolution": {"width": W, "height": H},
            "elements": [
                plane("blade_x", uid(f"{name}-x"), h, 0),
                plane("blade_z", uid(f"{name}-z"), h, 2),
            ],
            "outliner": [uid(f"{name}-x"), uid(f"{name}-z")],
            "textures": [{
                "name": f"{name}.png", "uuid": uid(f"{name}-tex"), "id": "0",
                "mode": "bitmap", "saved": False, "particle": False,
                "source": "data:image/png;base64," + base64.b64encode(png).decode(),
            }],
        }
        path = out / f"{name}.bbmodel"
        path.write_text(json.dumps(model, indent=1))
        print(f"wrote {path} ({path.stat().st_size} bytes, {h} units tall)")


if __name__ == "__main__":
    main()
