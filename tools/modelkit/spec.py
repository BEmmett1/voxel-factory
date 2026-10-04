"""Describe a block shape or a creature in Python; write the .bbmodel.

    from modelkit import Model, atlas_tile, material
    m = Model("hand_press", kind="block")
    m.material("wood", material("pixellab_worn_wood_32"))
    m.material("iron", atlas_tile(3))
    m.group("crank", pivot=(8, 12, 8))
    m.box("base", (2, 0, 2), (14, 3, 14), "wood")
    m.box("screw", (7, 3, 7), (9, 13, 9), "iron", group="crank")
    m.move("crank", "Bob", axis=(0, -1, 0), rate=1, amount=2 / 16, cranked=True)
    m.save("models/hand_press.bbmodel")        # then: preview, bake, wire
    print(m.cpp_part_anims("HandPress"))        # the kPartAnims rows to paste

Creatures are the same object with kind="creature": groups become BONES
(`parent=` for the hierarchy, `rotation=` for a rest pose), coordinates are
model units with the feet at y = 0 and the model facing -Z, and clips replace
moves:

    walk = m.clip("walk", length=0.8)
    walk.rot("leg_l", 0.0, (30, 0, 0)); walk.rot("leg_l", 0.4, (-30, 0, 0))
    walk.pos("root", 0.2, (0, 1, 0), interp="catmullrom")

Texture: every material becomes a region of one sheet, tiled to fit, and each
face samples a sub-rect of its material at ONE TEXEL PER UNIT (the game's
density) with per-face UVs -- which the creature loader requires and the block
bake handles. Groups/bones are written inline in the outliner (the 4.x shape),
which both loaders and Blockbench read. Needs Pillow.
"""
import base64, hashlib, io, json, math, os, uuid
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
FACES = ("north", "east", "south", "west", "up", "down")


def _uid(*parts):
    return str(uuid.UUID(hashlib.md5("|".join(map(str, parts)).encode()).hexdigest()))


def atlas_tile(t):
    """A 16px tile of game/assets/atlas.png -- the approved terrain palette."""
    a = Image.open(ROOT / "game" / "assets" / "atlas.png").convert("RGBA")
    return a.crop(((t % 16) * 16, (t // 16) * 16, (t % 16) * 16 + 16, (t // 16) * 16 + 16))


def material(name):
    """A swatch from models/materials/ (with or without the .png)."""
    n = name if name.endswith(".png") else name + ".png"
    return Image.open(ROOT / "models" / "materials" / n).convert("RGBA")


def solid(rgba, size=16):
    return Image.new("RGBA", (size, size), tuple(rgba) + ((255,) if len(rgba) == 3 else ()))


class Clip:
    def __init__(self, name, length, loop):
        self.name, self.length, self.loop, self.keys = name, length, loop, []

    def rot(self, bone, time, deg, interp="linear"):
        self.keys.append((bone, "rotation", time, tuple(deg), interp)); return self

    def pos(self, bone, time, units, interp="linear"):
        self.keys.append((bone, "position", time, tuple(units), interp)); return self


class Model:
    def __init__(self, name, kind="block"):
        assert kind in ("block", "creature")
        self.name, self.kind = name, kind
        self.mats, self.groups, self.boxes, self.decals = {}, {}, [], set()
        self.moves, self.clips = [], []

    # ---- content -------------------------------------------------------------
    def material(self, key, img, decal=False):
        """A texture to sample. An ordinary material TILES: each face takes its
        own window of it, so a box never shows the same grain twice. A DECAL is
        a picture -- a glowing arch, a dial -- and each face using it shows the
        WHOLE image, stretched to fit, so paint it at the face's size."""
        self.mats[key] = img.convert("RGBA")
        if decal:
            self.decals.add(key)
        return key

    def group(self, name, pivot=(8, 8, 8), parent=None, rotation=(0, 0, 0)):
        """A block PART (something kPartAnims can move) or a creature BONE."""
        assert parent is None or parent in self.groups, f"unknown parent {parent}"
        self.groups[name] = {"pivot": tuple(pivot), "parent": parent, "rotation": tuple(rotation)}
        return name

    def box(self, name, frm, to, mat, faces=None, rotate=None, origin=None, group=None, inflate=0.0):
        """faces: {face: material key, or None for no face}. rotate: (axis, deg),
        one axis, +-22.5 / +-45 for blocks (the bake's rule)."""
        assert mat in self.mats, f"unknown material {mat}"
        assert group is None or group in self.groups, f"unknown group {group}"
        self.boxes.append(dict(name=name, frm=tuple(frm), to=tuple(to), mat=mat,
                               faces=dict(faces or {}), rotate=rotate, origin=origin,
                               group=group, inflate=inflate))
        return self

    def move(self, part, motion, axis=(0, 1, 0), rate=1.0, amount=0.0, cranked=False):
        """A kPartAnims row (blocks): Spin, Rock (amount = degrees), Pulse
        (amount = scale delta) or Bob (amount = blocks). Rows on one part compose."""
        assert self.kind == "block" and part in self.groups
        assert motion in ("Spin", "Rock", "Pulse", "Bob")
        self.moves.append({"part": part, "motion": motion, "axis": list(axis), "rate": rate,
                           "amount": amount, "cranked": cranked})
        return self

    def clip(self, name, length, loop=True):
        assert self.kind == "creature"
        c = Clip(name, length, loop); self.clips.append(c); return c

    # ---- the texture sheet -------------------------------------------------
    def _face_dims(self, b, face):
        d = [abs(b["to"][i] - b["frm"][i]) for i in range(3)]
        return {"north": (d[0], d[1]), "south": (d[0], d[1]), "east": (d[2], d[1]),
                "west": (d[2], d[1]), "up": (d[0], d[2]), "down": (d[0], d[2])}[face]

    def _sheet(self):
        """Tile each USED material into a square region big enough for its
        largest face, and pack the regions into one sheet."""
        need = {}
        for b in self.boxes:
            for f in FACES:
                k = b["faces"].get(f, b["mat"])
                if k is None: continue
                assert k in self.mats, f"unknown material {k}"
                fw, fh = self._face_dims(b, f)
                need[k] = max(need.get(k, 16), math.ceil(max(fw, fh)))
                if k in self.decals:
                    need[k] = max(need[k], *self.mats[k].size)
        regions, x, y, row_h, W = {}, 0, 0, 0, 128
        for k in sorted(need, key=lambda k: -need[k]):
            r = 16 if need[k] <= 16 else 32 if need[k] <= 32 else 64
            if x + r > W:
                x, y, row_h = 0, y + row_h, 0
            regions[k] = (x, y, r); x += r; row_h = max(row_h, r)
        H = 1 << max(4, math.ceil(math.log2(max(16, y + row_h))))
        sheet = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        for k, (rx, ry, r) in regions.items():
            src = self.mats[k]
            for ty in range(0, r, src.height):
                for tx in range(0, r, src.width):
                    sheet.paste(src.crop((0, 0, min(src.width, r - tx), min(src.height, r - ty))), (rx + tx, ry + ty))
        return sheet, regions

    # ---- writing ------------------------------------------------------------
    def to_json(self):
        sheet, regions = self._sheet()
        W, H = sheet.size
        elements, members = [], {g: [] for g in self.groups}
        root_children = []
        for i, b in enumerate(self.boxes):
            faces = {}
            for f in FACES:
                k = b["faces"].get(f, b["mat"])
                if k is None: continue
                rx, ry, r = regions[k]
                if k in self.decals:
                    iw, ih = self.mats[k].size
                    faces[f] = {"uv": [rx, ry, rx + iw, ry + ih], "texture": 0}
                    continue
                fw, fh = self._face_dims(b, f)
                fw, fh = min(fw, r), min(fh, r)
                h = int(hashlib.md5(f"{self.name}{i}{f}".encode()).hexdigest(), 16)
                ox = h % max(1, int(r - fw) + 1); oy = (h >> 8) % max(1, int(r - fh) + 1)
                faces[f] = {"uv": [rx + ox, ry + oy, rx + ox + fw, ry + oy + fh], "texture": 0}
            rot = [0, 0, 0]
            if b["rotate"]:
                rot["xyz".index(b["rotate"][0])] = b["rotate"][1]
            uid = _uid(self.name, "el", i)
            el = {"uuid": uid, "type": "cube", "name": b["name"], "box_uv": False, "rescale": False,
                  "locked": False, "from": list(b["frm"]), "to": list(b["to"]),
                  "origin": list(b["origin"] or [(b["frm"][k] + b["to"][k]) / 2 for k in range(3)]),
                  "faces": faces, "color": 0}
            if b["inflate"]: el["inflate"] = b["inflate"]
            if b["rotate"]: el["rotation"] = rot
            elements.append(el)
            (members[b["group"]] if b["group"] else root_children).append(uid)

        def node(g):
            info = self.groups[g]
            kids = list(members[g]) + [node(c) for c, ci in self.groups.items() if ci["parent"] == g]
            return {"name": g, "uuid": _uid(self.name, "grp", g), "origin": list(info["pivot"]),
                    "rotation": list(info["rotation"]), "isOpen": True, "children": kids}

        tops = [node(g) for g, gi in self.groups.items() if gi["parent"] is None]
        if self.kind == "block":
            outliner = [{"name": "root", "uuid": _uid(self.name, "grp", "__root"), "origin": [8, 0, 8],
                         "rotation": [0, 0, 0], "isOpen": True, "children": root_children + tops}]
        else:
            assert not root_children, "creature boxes must belong to a bone (group=...)"
            outliner = tops

        buf = io.BytesIO(); sheet.save(buf, "PNG")
        doc = {"meta": {"format_version": "4.5",
                        "model_format": "java_block" if self.kind == "block" else "free",
                        "box_uv": False},
               "name": self.name, "model_identifier": "", "resolution": {"width": W, "height": H},
               "elements": elements, "outliner": outliner,
               "textures": [{"name": "texture.png", "uuid": _uid(self.name, "tex"), "id": "0",
                             "width": W, "height": H, "uv_width": W, "uv_height": H,
                             "mode": "bitmap", "saved": False, "particle": False,
                             "source": "data:image/png;base64," + base64.b64encode(buf.getvalue()).decode()}]}
        if self.kind == "creature":
            anims = []
            for c in self.clips:
                animators = {}
                for bone, ch, t, v, interp in c.keys:
                    assert bone in self.groups, f"clip {c.name} names unknown bone {bone}"
                    a = animators.setdefault(_uid(self.name, "grp", bone),
                                             {"name": bone, "type": "bone", "keyframes": []})
                    a["keyframes"].append({"channel": ch, "time": t, "interpolation": interp,
                                           "uuid": _uid(self.name, c.name, bone, ch, t),
                                           "data_points": [{"x": v[0], "y": v[1], "z": v[2]}]})
                anims.append({"uuid": _uid(self.name, "anim", c.name), "name": c.name,
                              "loop": "loop" if c.loop else "once", "length": c.length,
                              "snapping": 24, "animators": animators})
            doc["animations"] = anims
        return doc

    def save(self, path):
        path = Path(path); path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(self.to_json(), separators=(",", ":")), encoding="utf-8", newline="")
        return path

    def cpp_part_anims(self, shape_id):
        """The kPartAnims rows for BlockShape.h, ready to paste."""
        def f(x): return f"{float(x):g}f" if float(x) != int(x) else f"{int(x)}.0f"
        out = []
        for m in self.moves:
            ax = ", ".join(f(v) for v in m["axis"])
            row = (f'{{ShapeId::{shape_id}, "{m["part"]}", PartMotion::{m["motion"]}, {{{ax}}}, '
                   f'{f(m["rate"])}, {f(m["amount"])}{", true" if m["cranked"] else ""}}},')
            out.append("    " + row)
        return "\n".join(out)

    def preview(self, path, out=None, frames=12, size=240):
        """Save, then render + check it with preview.py (the spec's own moves,
        not BlockShape.h's -- the shape is not in the game yet)."""
        import tempfile, preview as pv
        p = self.save(path)
        out = Path(out or Path(tempfile.gettempdir()) / "modelkit_preview"); out.mkdir(parents=True, exist_ok=True)
        issues = (pv.preview_block(p, out, frames, size, self.moves) if self.kind == "block"
                  else pv.preview_creature(p, out, frames, size))
        for i in issues: print(f"  PROBLEM: {i}")
        print(f"  previews in {out}")
        return issues
