#!/usr/bin/env python3
"""Generate tempest.bbmodel -- THE TEMPEST, boss #2.

Pure stdlib, structured exactly like make_test_model.py: cuboid elements on a
small bone tree, an embedded base64 PNG texture, and idle/walk keyframe
animations. The tempest is a slim storm-blue reaper with mismatched wing
blades (asymmetry doubles as the handedness cue) and a hot core, animated
fast.

Rerun after editing:  python tools/make_tempest_model.py

The output is a *starter* -- replace it with any hand-made Blockbench model of
the same name and the game loads that instead. It overwrites hand edits!

Boss #1 took exactly that offer, which is why this script no longer generates
it: the Void Warden ships as the authored, committed
game/assets/models/void_warden.bbmodel (see vg::kWardenModel). Its generated
starter lived on here for weeks referenced by nothing, so it is gone -- see
git history for the seven-cube version if the shape of a starter is ever
wanted again.
"""

import base64
import json
import struct
import uuid
import zlib
from pathlib import Path


W = H = 32


def write_png(w, h, rgba):
    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    raw = b"".join(b"\x00" + bytes(rgba[y * w * 4:(y + 1) * w * 4]) for y in range(h))
    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) +
            chunk(b"IEND", b""))


def faces(uv, north=None):
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


def key(channel, time, x, y, z, interpolation="linear"):
    return {"channel": channel, "time": time,
            "data_points": [{"x": x, "y": y, "z": z}],
            "interpolation": interpolation}


# --- THE TEMPEST (boss #2): slim, fast, storm-blue ---------------------------

def tuid(name):
    return str(uuid.uuid5(uuid.NAMESPACE_DNS, f"{name}.tempest.voxel-factory"))


T_BODY, T_HEAD_EL, T_CREST = tuid("body"), tuid("head-el"), tuid("crest")
T_WING_L, T_WING_R, T_TAIL = tuid("wing-l"), tuid("wing-r"), tuid("tail")
T_ROOT, T_HEAD, T_WINGS = tuid("root-bone"), tuid("head-bone"), tuid("wings-bone")
T_IDLE, T_WALK = tuid("anim-idle"), tuid("anim-walk")


def make_tempest_texture():
    px = bytearray(W * H * 4)

    def rect(x0, y0, x1, y1, c):
        for y in range(y0, y1):
            for x in range(x0, x1):
                i = (y * W + x) * 4
                px[i:i + 4] = bytes((*c, 255))

    rect(0, 0, 16, 16, (40, 58, 96))      # body: storm blue
    rect(16, 8, 32, 16, (52, 74, 118))    # head sides/top
    rect(16, 0, 24, 8, (46, 66, 108))     # head FRONT (eye region)
    rect(24, 0, 32, 8, (36, 52, 86))      # head back
    rect(0, 16, 8, 24, (196, 214, 240))   # wing blades: pale storm-light
    rect(8, 16, 16, 24, (30, 42, 70))     # tail: darkest
    rect(0, 24, 8, 32, (140, 220, 255))   # crest/core: electric cyan
    # A single cyclopean eye, off-center (the asymmetry cue).
    rect(18, 3, 21, 6, (170, 240, 255))
    rect(19, 4, 20, 5, (255, 255, 255))
    return px


TEMPEST_ELEMENTS = [
    # Slim tall body, feet plane y=0.
    element("body", T_BODY, (-4, 2, -4), (4, 16, 4), faces((2, 2, 14, 14))),
    # Narrow head high on the front.
    element("head", T_HEAD_EL, (-3, 14, -8), (3, 20, -2),
            faces((24, 0, 32, 8), north=(16, 0, 24, 8))),
    # Electric crest running up off the head.
    element("crest", T_CREST, (-1, 19, -6), (1, 24, -3), faces((0, 24, 8, 32))),
    # Mismatched wing blades: a long one on -X, a short one on +X.
    element("wing-long", T_WING_L, (-12, 8, -1), (-4, 18, 1), faces((0, 16, 8, 24))),
    element("wing-short", T_WING_R, (4, 10, -1), (9, 16, 1), faces((0, 16, 8, 24))),
    # Trailing tail spike, angled down.
    element("tail", T_TAIL, (-1, 8, 4), (1, 10, 12), faces((8, 16, 16, 24)),
            rotation=(-18, 0, 0), origin=(0, 9, 4)),
]

TEMPEST_OUTLINER = [{
    "name": "root", "uuid": T_ROOT, "origin": [0, 0, 0],
    "children": [
        T_BODY, T_TAIL,
        {"name": "head", "uuid": T_HEAD, "origin": [0, 16, -2],
         "children": [T_HEAD_EL, T_CREST]},
        {"name": "wings", "uuid": T_WINGS, "origin": [0, 13, 0],
         "children": [T_WING_L, T_WING_R]},
    ],
}]

TEMPEST_ANIMATIONS = [
    {
        "uuid": T_IDLE, "name": "idle", "loop": "loop", "length": 1.6,
        "snapping": 24, "animators": {
            T_ROOT: {"name": "root", "type": "bone", "keyframes": [
                key("position", 0.0, 0, 0, 0),
                key("position", 0.8, 0, 1.6, 0),   # restless hover-bob
                key("position", 1.6, 0, 0, 0),
            ]},
            T_WINGS: {"name": "wings", "type": "bone", "keyframes": [
                key("rotation", 0.0, 0, 0, 10),
                key("rotation", 0.8, 0, 0, -10),   # slow scissor
                key("rotation", 1.6, 0, 0, 10),
            ]},
        },
    },
    {
        "uuid": T_WALK, "name": "walk", "loop": "loop", "length": 0.5,
        "snapping": 24, "animators": {
            T_ROOT: {"name": "root", "type": "bone", "keyframes": [
                key("position", 0.0, 0, 0.4, 0),
                key("position", 0.25, 0, 1.8, 0),  # skimming dashes
                key("position", 0.5, 0, 0.4, 0),
            ]},
            T_WINGS: {"name": "wings", "type": "bone", "keyframes": [
                key("rotation", 0.0, 0, 0, 24),
                key("rotation", 0.25, 0, 0, -24),  # violent flapping
                key("rotation", 0.5, 0, 0, 24),
            ]},
            T_HEAD: {"name": "head", "type": "bone", "keyframes": [
                key("rotation", 0.0, 10, 0, 0),    # head lowered, diving in
                key("rotation", 0.5, 10, 0, 0),
            ]},
        },
    },
]


def write_model(filename, name, elements, outliner, animations, texture_png,
                texture_uuid):
    model = {
        "meta": {"format_version": "4.5", "model_format": "free", "box_uv": False},
        "name": name,
        "resolution": {"width": W, "height": H},
        "elements": elements,
        "outliner": outliner,
        "textures": [{
            "name": name + ".png", "uuid": texture_uuid, "id": "0",
            "mode": "bitmap", "saved": False, "particle": False,
            "source": "data:image/png;base64," + base64.b64encode(texture_png).decode(),
        }],
        "animations": animations,
    }
    out = Path(__file__).resolve().parent.parent / "game" / "assets" / "models"
    out.mkdir(parents=True, exist_ok=True)
    path = out / filename
    path.write_text(json.dumps(model, indent=1))
    print(f"wrote {path} ({path.stat().st_size} bytes)")


def main():
    write_model("tempest.bbmodel", "tempest", TEMPEST_ELEMENTS, TEMPEST_OUTLINER,
                TEMPEST_ANIMATIONS, write_png(W, H, make_tempest_texture()),
                tuid("texture"))


if __name__ == "__main__":
    main()
