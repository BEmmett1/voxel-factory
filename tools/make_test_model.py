#!/usr/bin/env python3
"""Generate game/assets/models/creature.bbmodel -- the starter test creature.

Pure stdlib. A deliberately ASYMMETRIC two-bone Blockbench model used to
verify the .bbmodel import pipeline end to end: body + rotated tail on the
root bone, head + one horn (on the +X side only) on a child bone, eyes on the
front (-Z) face, an embedded base64 PNG texture, and idle/walk keyframe
animations (including one step-interpolation key and one value written as the
string "0" to exercise the loader's lenient paths).

Rerun after editing:  python tools/make_test_model.py

The output is a *starter* -- replace it with any hand-made Blockbench model
of the same name (Free format, per-face UVs, embedded texture); this script
just guarantees a working asset so the pipeline never depends on hand art.
It overwrites hand edits!
"""

import base64
import json
import struct
import uuid
import zlib
from pathlib import Path

# Deterministic uuids (Blockbench wants uuid-shaped strings).
def uid(name):
    return str(uuid.uuid5(uuid.NAMESPACE_DNS, f"{name}.voxel-factory"))

U_BODY, U_HEAD_EL, U_HORN, U_TAIL = uid("body"), uid("head-el"), uid("horn"), uid("tail")
U_ROOT, U_HEAD = uid("root-bone"), uid("head-bone")
U_IDLE, U_WALK = uid("anim-idle"), uid("anim-walk")

W = H = 32  # texture size (matches "resolution")


# --- texture: distinct flat colors per part + eyes on the face region --------

def make_texture():
    px = bytearray(W * H * 4)

    def rect(x0, y0, x1, y1, c):
        for y in range(y0, y1):
            for x in range(x0, x1):
                i = (y * W + x) * 4
                px[i:i + 4] = bytes((*c, 255))

    rect(0, 0, 16, 16, (86, 140, 70))     # body: moss green
    rect(16, 8, 32, 16, (196, 164, 120))  # head sides/top: tan
    rect(16, 0, 24, 8, (208, 178, 132))   # head FRONT (eyes region)
    rect(24, 0, 32, 8, (186, 152, 108))   # head back
    rect(0, 16, 8, 24, (235, 226, 180))   # horn: bone white
    rect(8, 16, 16, 24, (60, 104, 52))    # tail: dark green
    # Eyes: two dark pixels on the front region -- the orientation cue.
    for ex in (18, 21):
        rect(ex, 3, ex + 1, 5, (25, 20, 20))
    return px


def write_png(w, h, rgba):
    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    raw = b"".join(b"\x00" + bytes(rgba[y * w * 4:(y + 1) * w * 4]) for y in range(h))
    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) +
            chunk(b"IEND", b""))


# --- geometry: cuboids in Blockbench units (16 = 1 block), feet at y=0 -------

def faces(uv, north=None):
    """All six faces mapped to `uv` ([x0,y0,x1,y1]); north overridable."""
    f = {name: {"uv": list(uv), "texture": 0}
         for name in ("north", "south", "east", "west", "up", "down")}
    if north:
        f["north"] = {"uv": list(north), "texture": 0}
    return f


def element(name, el_uuid, frm, to, fcs, rotation=None, origin=None):
    el = {
        "name": name, "type": "cube", "uuid": el_uuid, "color": 0,
        "from": list(frm), "to": list(to),
        "origin": list(origin or (0, 0, 0)), "faces": fcs,
    }
    if rotation:
        el["rotation"] = list(rotation)
    return el


ELEMENTS = [
    # Body: 8 wide, 6 tall, 10 long, standing on stubby air (feet plane y=0).
    element("body", U_BODY, (-4, 2, -5), (4, 8, 5), faces((2, 2, 14, 14))),
    # Head at the -Z (north/front) end; eyes live on its north face.
    element("head", U_HEAD_EL, (-3, 5, -10), (3, 11, -4),
            faces((24, 0, 32, 8), north=(16, 0, 24, 8))),
    # Horn on the head's +X side ONLY -- the handedness cue.
    element("horn", U_HORN, (3, 9, -8), (5, 11, -6), faces((0, 16, 8, 24))),
    # Tail with a static 25-degree X rotation -- exercises the rotation bake.
    element("tail", U_TAIL, (-1, 4, 5), (1, 6, 10), faces((8, 16, 16, 24)),
            rotation=(25, 0, 0), origin=(0, 5, 5)),
]

OUTLINER = [{
    "name": "root", "uuid": U_ROOT, "origin": [0, 0, 0],
    "children": [
        U_BODY, U_TAIL,
        {"name": "head", "uuid": U_HEAD, "origin": [0, 6, -5],  # neck pivot
         "children": [U_HEAD_EL, U_HORN]},
    ],
}]


# --- animations ---------------------------------------------------------------

def key(channel, time, x, y, z, interpolation="linear"):
    return {"channel": channel, "time": time,
            "data_points": [{"x": x, "y": y, "z": z}],
            "interpolation": interpolation}


ANIMATIONS = [
    {
        "uuid": U_IDLE, "name": "idle", "loop": "loop", "length": 2.0,
        "snapping": 24, "animators": {
            U_HEAD: {"name": "head", "type": "bone", "keyframes": [
                # One value as the string "0" -- proves the lenient parser.
                key("rotation", 0.0, "0", 0, 0),
                key("rotation", 1.0, 5, 0, 0),
                key("rotation", 2.0, 0, 0, 0),
            ]},
        },
    },
    {
        "uuid": U_WALK, "name": "walk", "loop": "loop", "length": 0.8,
        "snapping": 24, "animators": {
            U_ROOT: {"name": "root", "type": "bone", "keyframes": [
                key("position", 0.0, 0, 0, 0),
                key("position", 0.2, 0, 1, 0),
                key("position", 0.4, 0, 0, 0),
                key("position", 0.6, 0, 1, 0, interpolation="step"),
                key("position", 0.8, 0, 0, 0),
            ]},
            U_HEAD: {"name": "head", "type": "bone", "keyframes": [
                key("rotation", 0.0, 0, 0, 6),
                key("rotation", 0.4, 0, 0, -6),
                key("rotation", 0.8, 0, 0, 6),
            ]},
        },
    },
]


def main():
    png = write_png(W, H, make_texture())
    model = {
        "meta": {"format_version": "4.5", "model_format": "free", "box_uv": False},
        "name": "creature",
        "resolution": {"width": W, "height": H},
        "elements": ELEMENTS,
        "outliner": OUTLINER,
        "textures": [{
            "name": "creature.png", "uuid": uid("texture"), "id": "0",
            "mode": "bitmap", "saved": False, "particle": False,
            "source": "data:image/png;base64," + base64.b64encode(png).decode(),
        }],
        "animations": ANIMATIONS,
    }
    out = Path(__file__).resolve().parent.parent / "game" / "assets" / "models"
    out.mkdir(parents=True, exist_ok=True)
    path = out / "creature.bbmodel"
    path.write_text(json.dumps(model, indent=1))
    print(f"wrote {path} ({path.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
