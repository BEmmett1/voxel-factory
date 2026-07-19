#!/usr/bin/env python3
"""Generate game/assets/models/boss.bbmodel -- the VOID WARDEN (boss #1).

Pure stdlib, structured exactly like make_test_model.py: cuboid elements on a
small bone tree, an embedded base64 PNG texture, and idle/walk keyframe
animations. The warden is a hulking void-purple brute: broad body, low-slung
head with a glowing eye band, two mismatched horns (asymmetry doubles as the
handedness cue), and heavy arm slabs that swing while it stalks.

Rerun after editing:  python tools/make_boss_model.py

The output is a *starter* -- replace it with any hand-made Blockbench model of
the same name (Free format, per-face UVs, embedded texture). It overwrites
hand edits!
"""

import base64
import json
import struct
import uuid
import zlib
from pathlib import Path


def uid(name):
    return str(uuid.uuid5(uuid.NAMESPACE_DNS, f"{name}.boss.voxel-factory"))


U_BODY, U_HEAD_EL, U_HORN_L, U_HORN_R = uid("body"), uid("head-el"), uid("horn-l"), uid("horn-r")
U_ARM_L, U_ARM_R, U_CREST = uid("arm-l"), uid("arm-r"), uid("crest")
U_ROOT, U_HEAD, U_ARMS = uid("root-bone"), uid("head-bone"), uid("arms-bone")
U_IDLE, U_WALK = uid("anim-idle"), uid("anim-walk")

W = H = 32


def make_texture():
    px = bytearray(W * H * 4)

    def rect(x0, y0, x1, y1, c):
        for y in range(y0, y1):
            for x in range(x0, x1):
                i = (y * W + x) * 4
                px[i:i + 4] = bytes((*c, 255))

    rect(0, 0, 16, 16, (52, 36, 82))      # body: deep void purple
    rect(16, 8, 32, 16, (66, 46, 104))    # head sides/top
    rect(16, 0, 24, 8, (58, 40, 92))      # head FRONT (eye band region)
    rect(24, 0, 32, 8, (48, 33, 76))      # head back
    rect(0, 16, 8, 24, (226, 214, 245))   # horns: pale bone-violet
    rect(8, 16, 16, 24, (40, 27, 64))     # arms: darker slabs
    rect(0, 24, 8, 32, (140, 90, 220))    # crest: bright violet
    # The eye band: a hot violet strip across the head's front region.
    rect(17, 3, 23, 5, (200, 150, 255))
    rect(18, 3, 19, 5, (255, 240, 255))   # hot core, off-center (asymmetry)
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


ELEMENTS = [
    # Massive body: 14 wide, 12 tall, 12 long, riding low (feet plane y=0).
    element("body", U_BODY, (-7, 3, -6), (7, 15, 6), faces((2, 2, 14, 14))),
    # Head jutting forward and low at the -Z front; eye band on its north face.
    element("head", U_HEAD_EL, (-4, 8, -12), (4, 15, -5),
            faces((24, 0, 32, 8), north=(16, 0, 24, 8))),
    # Mismatched horns -- the big one on +X, a stub on -X (handedness cue).
    element("horn-big", U_HORN_L, (4, 13, -11), (7, 19, -8), faces((0, 16, 8, 24))),
    element("horn-stub", U_HORN_R, (-6, 13, -10), (-4, 16, -8), faces((0, 16, 8, 24))),
    # Arm slabs hanging at the flanks, on their own swing bone.
    element("arm-l", U_ARM_L, (7, 0, -3), (10, 12, 3), faces((8, 16, 16, 24))),
    element("arm-r", U_ARM_R, (-10, 0, -3), (-7, 12, 3), faces((8, 16, 16, 24))),
    # A bright crest ridge along the spine, tilted back.
    element("crest", U_CREST, (-1, 14, -3), (1, 19, 5), faces((0, 24, 8, 32)),
            rotation=(-20, 0, 0), origin=(0, 15, 0)),
]

OUTLINER = [{
    "name": "root", "uuid": U_ROOT, "origin": [0, 0, 0],
    "children": [
        U_BODY, U_CREST,
        {"name": "head", "uuid": U_HEAD, "origin": [0, 12, -5],  # neck pivot
         "children": [U_HEAD_EL, U_HORN_L, U_HORN_R]},
        {"name": "arms", "uuid": U_ARMS, "origin": [0, 12, 0],   # shoulder pivot
         "children": [U_ARM_L, U_ARM_R]},
    ],
}]


def key(channel, time, x, y, z, interpolation="linear"):
    return {"channel": channel, "time": time,
            "data_points": [{"x": x, "y": y, "z": z}],
            "interpolation": interpolation}


ANIMATIONS = [
    {
        "uuid": U_IDLE, "name": "idle", "loop": "loop", "length": 2.4,
        "snapping": 24, "animators": {
            U_ROOT: {"name": "root", "type": "bone", "keyframes": [
                key("position", 0.0, 0, 0, 0),
                key("position", 1.2, 0, 0.6, 0),   # slow menacing heave
                key("position", 2.4, 0, 0, 0),
            ]},
            U_HEAD: {"name": "head", "type": "bone", "keyframes": [
                key("rotation", 0.0, 0, -8, 0),
                key("rotation", 1.2, 4, 8, 0),     # scanning sweep
                key("rotation", 2.4, 0, -8, 0),
            ]},
        },
    },
    {
        "uuid": U_WALK, "name": "walk", "loop": "loop", "length": 0.9,
        "snapping": 24, "animators": {
            U_ROOT: {"name": "root", "type": "bone", "keyframes": [
                key("position", 0.0, 0, 0, 0),
                key("position", 0.225, 0, 1.2, 0), # heavy two-beat stomp
                key("position", 0.45, 0, 0, 0),
                key("position", 0.675, 0, 1.2, 0),
                key("position", 0.9, 0, 0, 0),
            ]},
            U_ARMS: {"name": "arms", "type": "bone", "keyframes": [
                key("rotation", 0.0, 14, 0, 0),
                key("rotation", 0.45, -14, 0, 0),  # opposed arm swing
                key("rotation", 0.9, 14, 0, 0),
            ]},
            U_HEAD: {"name": "head", "type": "bone", "keyframes": [
                key("rotation", 0.0, 6, 0, 0),     # head down, charging
                key("rotation", 0.9, 6, 0, 0),
            ]},
        },
    },
]


def main():
    png = write_png(W, H, make_texture())
    model = {
        "meta": {"format_version": "4.5", "model_format": "free", "box_uv": False},
        "name": "boss",
        "resolution": {"width": W, "height": H},
        "elements": ELEMENTS,
        "outliner": OUTLINER,
        "textures": [{
            "name": "boss.png", "uuid": uid("texture"), "id": "0",
            "mode": "bitmap", "saved": False, "particle": False,
            "source": "data:image/png;base64," + base64.b64encode(png).decode(),
        }],
        "animations": ANIMATIONS,
    }
    out = Path(__file__).resolve().parent.parent / "game" / "assets" / "models"
    out.mkdir(parents=True, exist_ok=True)
    path = out / "boss.bbmodel"
    path.write_text(json.dumps(model, indent=1))
    print(f"wrote {path} ({path.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
