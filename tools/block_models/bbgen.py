"""Tiny Blockbench 5.0 java_block writer for hand-authored block models.

A model is a list of Box(...) cubes and a texture sheet made of named material
REGIONS. Every face samples a sub-rect of its material region at exactly one
texel per model unit, so texel density matches the rest of the game, with an
offset hashed from the element + face so neighbouring faces do not repeat.
"""
import base64, io, json, uuid, hashlib
from PIL import Image

FACES = ("north", "east", "south", "west", "up", "down")


def face_dims(fr, to, face):
    dx, dy, dz = (to[0] - fr[0]), (to[1] - fr[1]), (to[2] - fr[2])
    if face in ("north", "south"): return dx, dy
    if face in ("east", "west"):   return dz, dy
    return dx, dz


class Box:
    def __init__(self, name, fr, to, mat, faces=None, rot=None, origin=None, group=None):
        """mat: default material for every face; faces: {face: material or None}
        (None = no face, i.e. not drawn). rot: (axis, angle) with origin."""
        self.name, self.fr, self.to = name, list(fr), list(to)
        self.mat, self.faces = mat, dict(faces or {})
        self.rot, self.origin, self.group = rot, origin, group


def _uid(*parts):
    return str(uuid.UUID(hashlib.md5("|".join(map(str, parts)).encode()).hexdigest()))


def build(name, boxes, sheet, regions, groups=None, out_path=None):
    """sheet: PIL RGBA image; regions: {mat: (x, y, w, h)} in texels.
    groups: {group_name: origin}. Elements with .group go into that group."""
    groups = groups or {}
    W, H = sheet.size
    elements, by_group = [], {g: [] for g in groups}
    root_children = []
    for i, b in enumerate(boxes):
        faces = {}
        for f in FACES:
            m = b.faces.get(f, b.mat)
            if m is None:
                continue
            rx, ry, rw, rh = regions[m]
            fw, fh = face_dims(b.fr, b.to, f)
            fw, fh = min(fw, rw), min(fh, rh)
            h = int(hashlib.md5(f"{name}{i}{f}".encode()).hexdigest(), 16)
            ox = (h % max(1, int(rw - fw) + 1))
            oy = ((h >> 8) % max(1, int(rh - fh) + 1))
            u1, v1 = rx + ox, ry + oy
            faces[f] = {"uv": [u1, v1, u1 + fw, v1 + fh], "texture": 0}
        rot = [0, 0, 0]
        if b.rot:
            rot["xyz".index(b.rot[0])] = b.rot[1]
        origin = b.origin or [(b.fr[k] + b.to[k]) / 2 for k in range(3)]
        uid = _uid(name, "el", i)
        elements.append({
            "uuid": uid, "type": "cube", "name": b.name, "box_uv": False,
            "rescale": False, "locked": False, "render_order": "default",
            "allow_mirror_modeling": True, "from": b.fr, "to": b.to,
            "autouv": 0, "color": 0, "origin": origin, "rotation": rot,
            "faces": faces,
        })
        (by_group[b.group] if b.group else root_children).append(uid)

    group_rows = [{"uuid": _uid(name, "grp", "root"), "name": "root",
                   "origin": [8, 0, 8], "rotation": [0, 0, 0], "color": 0,
                   "visibility": True, "export": True, "isOpen": True,
                   "locked": False, "autouv": 0}]
    for g, org in groups.items():
        group_rows.append({"uuid": _uid(name, "grp", g), "name": g, "origin": org,
                           "rotation": [0, 0, 0], "color": 0, "visibility": True,
                           "export": True, "isOpen": True, "locked": False, "autouv": 0})
        root_children.append({"uuid": _uid(name, "grp", g), "isOpen": True,
                              "children": by_group[g]})
    outliner = [{"uuid": _uid(name, "grp", "root"), "isOpen": True,
                 "children": root_children}]

    buf = io.BytesIO(); sheet.save(buf, "PNG")
    tex = {"uuid": _uid(name, "tex"), "name": "texture.png", "id": "0", "path": "",
           "folder": "", "namespace": "", "width": W, "height": H,
           "uv_width": W, "uv_height": H, "particle": False,
           "use_as_default": False, "layers_enabled": False, "internal": True,
           "visible": True, "saved": False, "render_mode": "default",
           "render_sides": "auto", "frame_time": 1, "frame_order_type": "loop",
           "frame_order": "", "frame_interpolate": False,
           "source": "data:image/png;base64," + base64.b64encode(buf.getvalue()).decode()}
    model = {"meta": {"format_version": "5.0", "model_format": "java_block",
                      "box_uv": False},
             "name": name, "model_identifier": "",
             "resolution": {"width": W, "height": H},
             "elements": elements, "groups": group_rows, "outliner": outliner,
             "textures": [tex]}
    if out_path:
        with open(out_path, "w", encoding="utf-8", newline="") as fh:
            json.dump(model, fh, separators=(",", ":"))
    return model
