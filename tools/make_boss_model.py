#!/usr/bin/env python3
"""Generate the boss models: boss.bbmodel (the VOID WARDEN, boss #1) and
tempest.bbmodel (THE TEMPEST, boss #2).

Pure stdlib, structured exactly like make_test_model.py: cuboid elements on a
small bone tree, an embedded base64 PNG texture, and idle/walk keyframe
animations. The warden is a hulking void-purple brute: broad body, low-slung
head with a glowing eye band, two mismatched horns (asymmetry doubles as the
handedness cue), and heavy arm slabs that swing while it stalks. The tempest
is its opposite: a slim storm-blue reaper with mismatched wing blades and a
hot core, animated fast.

Rerun after editing:  python tools/make_boss_model.py

The outputs are *starters* -- replace either with any hand-made Blockbench
model of the same name (Free format, per-face UVs, embedded texture). It
overwrites hand edits!
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
    write_model("boss.bbmodel", "boss", ELEMENTS, OUTLINER, ANIMATIONS,
                write_png(W, H, make_texture()), uid("texture"))
    write_model("tempest.bbmodel", "tempest", TEMPEST_ELEMENTS, TEMPEST_OUTLINER,
                TEMPEST_ANIMATIONS, write_png(W, H, make_tempest_texture()),
                tuid("texture"))


if __name__ == "__main__":
    main()
