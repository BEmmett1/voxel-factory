"""Software renderer for block shapes and creatures -- see them without the game.

Both loaders reproduce what the GAME draws, not a parallel idea of it:

* Blocks go through tools/bbmodel_to_shape.py's own `load_model` +
  `cull_interior`, so every quad, corner, UV and part offset is the baked one.
  Moving parts are posed exactly like VoxelGame::updatePartAnim + voxel.vert:
  rows naming one part compose (turns multiply, moves add), and a vertex lands
  at p + (M*d - d) + T with d its baked offset from the part's pivot. Which
  parts move, and how, is read from the real kPartAnims table in BlockShape.h.
* Creatures are loaded by a line-for-line replica of engine/src/BbModel.cpp:
  the same face corner table, ZYX element rotation, /16 unit funnel, bone DFS
  and evaluateBbPose (rest rotation + sampled tracks, step / linear /
  Catmull-Rom).

Lighting is voxel.frag's and entity.frag's: shade = 0.35 + 0.65 * max(0,
n . -L) with the game's kLightDir, alpha cut at 0.5, no back-face culling.
The projection is orthographic, so silhouettes and proportions read true.
Needs numpy + Pillow.
"""
import base64, io, json, math, re, sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import bbmodel_to_shape as bake  # noqa: E402  (the bake IS the block loader)

LIGHT_DIR = np.array([-0.4, -1.0, -0.3]); LIGHT_DIR /= np.linalg.norm(LIGHT_DIR)
BACKGROUND = (36, 40, 48, 255)


# ---- maths ------------------------------------------------------------------

def rot_axis(axis, radians):
    """glm::rotate's matrix: right-handed, counter-clockwise about `axis`."""
    a = np.asarray(axis, float); a = a / (np.linalg.norm(a) or 1.0)
    x, y, z = a; c, s = math.cos(radians), math.sin(radians); C = 1 - c
    return np.array([[c + x*x*C, x*y*C - z*s, x*z*C + y*s],
                     [y*x*C + z*s, c + y*y*C, y*z*C - x*s],
                     [z*x*C - y*s, z*y*C + x*s, c + z*z*C]])

def rot_zyx(deg):
    """BbModel.cpp rotZYX: rotate(Z) * rotate(Y) * rotate(X)."""
    return (rot_axis((0, 0, 1), math.radians(deg[2])) @
            rot_axis((0, 1, 0), math.radians(deg[1])) @
            rot_axis((1, 0, 0), math.radians(deg[0])))


# ---- a mesh the rasterizer eats ---------------------------------------------

class Mesh:
    """Triangles: pos (N,3,3) world, uv (N,3,2) in TEXEL coordinates, normal
    (N,3), plus the RGBA texture as an (H,W,4) uint8 array."""
    def __init__(self, pos, uv, normal, tex):
        self.pos, self.uv, self.normal, self.tex = pos, uv, normal, tex


def _quads_to_tris(corners, uvs, normals):
    """Quads (Q,4,...) wound 0-1-2-3 -> triangles (0,1,2) + (0,2,3)."""
    c, u = np.asarray(corners, float), np.asarray(uvs, float)
    tp = np.concatenate([c[:, [0, 1, 2]], c[:, [0, 2, 3]]])
    tu = np.concatenate([u[:, [0, 1, 2]], u[:, [0, 2, 3]]])
    n = np.asarray(normals, float)
    return tp, tu, np.concatenate([n, n])


# ---- blocks -----------------------------------------------------------------

class BlockShape:
    """A baked block model plus the kPartAnims rows that move it in game."""

    def __init__(self, path, part_anims=None):
        self.path = Path(path)
        buf = io.StringIO(); old = sys.stdout; sys.stdout = buf
        try:
            m = bake.load_model(self.path)
            bake.cull_interior(m)
        finally:
            sys.stdout = old
        self.bake_log = buf.getvalue()
        self.model = m
        cw, chf, rgba = m.tex
        self.tex = np.frombuffer(bytes(rgba), np.uint8).reshape(chf, cw, 4)
        self.frame_h = chf // m.frames
        self.anims = part_anims if part_anims is not None else game_part_anims(self.path.stem)

    @property
    def quads(self): return len(self.model.quads)
    @property
    def kb(self): return self.quads * 6 * bake.SHAPED_VERTEX_FLOATS * 4 / 1024.0

    def part_pose(self, t_clock, t_turns):
        """part index -> (M, T): composed exactly like updatePartAnim."""
        pose = {}
        names = {p.name: i for i, p in enumerate(self.model.parts)}
        for a in self.anims:
            i = names.get(a["part"])
            if i is None:
                continue
            t = t_turns if a.get("cranked") else t_clock
            phase = 2 * math.pi * a["rate"] * t
            M, T = pose.get(i, (np.eye(3), np.zeros(3)))
            m = np.eye(3)
            if a["motion"] == "Spin":
                m = rot_axis(a["axis"], phase)
            elif a["motion"] == "Rock":
                m = rot_axis(a["axis"], math.radians(a["amount"]) * math.sin(phase))
            elif a["motion"] == "Pulse":
                m = np.eye(3) * (1.0 + a["amount"] * math.sin(phase))
            elif a["motion"] == "Bob":
                T = T + np.asarray(a["axis"], float) * (a["amount"] * 0.5 * (1 - math.cos(phase)))
            pose[i] = (m @ M, T)
        return pose

    def mesh(self, t_clock=0.0, t_turns=0.0, animate=True):
        m = self.model
        cx0, cy0, cw, ch, sx, sy = m.crop
        pose = self.part_pose(t_clock, t_turns) if animate else {}
        frame = 0
        if animate and m.frames > 1 and m.frame_time:
            frame = int(t_clock / (m.frame_time * 0.05)) % m.frames
        corners, uvs, normals = [], [], []
        for q in m.quads:
            M, T = pose.get(q.part, (np.eye(3), np.zeros(3)))
            pts = []
            for k in range(4):
                p = np.asarray(q.pos[k]); d = np.asarray(q.off[k])
                pts.append(p + (M @ d - d) + T)
            corners.append(pts)
            uu = []
            for s, tt in bake.CORNER_ST:
                u = q.uv[0] + (q.uv[2] - q.uv[0]) * s
                v = q.uv[1] + (q.uv[3] - q.uv[1]) * tt
                uu.append((u * sx - cx0, v * sy - cy0 + frame * ch))
            uvs.append(uu)
            n = M @ np.asarray(q.normal); n = n / (np.linalg.norm(n) or 1.0)
            normals.append(n)
        return Mesh(*_quads_to_tris(corners, uvs, normals), self.tex)


def _cpp_float(s):
    s = s.strip().replace("f", "")
    return float(eval(s, {"__builtins__": {}}, {}))

def game_part_anims(stem):
    """The kPartAnims rows for the model whose kShapeNames entry is `stem`."""
    h = (ROOT / "game" / "include" / "game" / "BlockShape.h").read_text(encoding="utf-8")
    enum = re.search(r"enum class ShapeId[^{]*\{(.*?)\};", h, re.S).group(1)
    ids = [x.strip() for x in re.sub(r"//.*", "", enum).split(",") if x.strip() and x.strip() != "Count"]
    names = re.findall(r'"(\w+)"', re.search(r"kShapeNames\[\] = \{(.*?)\};", h, re.S).group(1))
    shape = dict(zip(names, ids)).get(stem)
    table = re.search(r"kPartAnims\[\] = \{(.*?)\n\};", h, re.S).group(1)
    rows = []
    for m in re.finditer(r'\{ShapeId::(\w+),\s*"([^"]+)",\s*PartMotion::(\w+),\s*\{([^}]*)\},'
                         r'\s*([^,]+),\s*([^,}]+?)(?:,\s*(true|false))?\}', table):
        if m.group(1) != shape:
            continue
        rows.append({"part": m.group(2), "motion": m.group(3),
                     "axis": [_cpp_float(v) for v in m.group(4).split(",")],
                     "rate": _cpp_float(m.group(5)), "amount": _cpp_float(m.group(6)),
                     "cranked": m.group(7) == "true"})
    return rows


# ---- creatures --------------------------------------------------------------

# BbModel.cpp kFaces: corner selectors (from=0 / to=1) TL, TR, BR, BL.
CREATURE_FACES = [
    ("north", (0, 0, -1), ((1, 1, 0), (0, 1, 0), (0, 0, 0), (1, 0, 0))),
    ("south", (0, 0, 1),  ((0, 1, 1), (1, 1, 1), (1, 0, 1), (0, 0, 1))),
    ("east",  (1, 0, 0),  ((1, 1, 1), (1, 1, 0), (1, 0, 0), (1, 0, 1))),
    ("west",  (-1, 0, 0), ((0, 1, 0), (0, 1, 1), (0, 0, 1), (0, 0, 0))),
    ("up",    (0, 1, 0),  ((0, 1, 0), (1, 1, 0), (1, 1, 1), (0, 1, 1))),
    ("down",  (0, -1, 0), ((0, 0, 1), (1, 0, 1), (1, 0, 0), (0, 0, 0))),
]


def _num(v, d=0.0):
    try: return float(v)
    except (TypeError, ValueError): return d

def _vec(v, d=(0.0, 0.0, 0.0)):
    if not isinstance(v, (list, tuple)) or len(v) < 3: return np.array(d, float)
    return np.array([_num(v[0], d[0]), _num(v[1], d[1]), _num(v[2], d[2])])


class Creature:
    """A creature .bbmodel, loaded and posed the way BbModel.cpp does it."""

    def __init__(self, path):
        doc = json.loads(Path(path).read_text(encoding="utf-8"))
        self.path = Path(path)
        res = doc.get("resolution") or {}
        self.resW = max(1.0, _num(res.get("width", 16), 16)); self.resH = max(1.0, _num(res.get("height", 16), 16))
        src = (doc.get("textures") or [{}])[0].get("source", "")
        img = Image.open(io.BytesIO(base64.b64decode(src.split(",", 1)[1]))).convert("RGBA") if src.startswith("data:image") \
            else Image.new("RGBA", (16, 16), (255, 0, 255, 255))
        self.tex = np.array(img)
        groups = {g["uuid"]: g for g in doc.get("groups") or [] if isinstance(g, dict) and "uuid" in g}
        def field(node, k):
            if k in node: return node[k]
            return groups.get(node.get("uuid"), {}).get(k)
        self.bones, self.bone_by_uuid, by_el = [], {}, {}
        def walk(node, parent):
            if isinstance(node, str): by_el[node] = parent; return
            if not isinstance(node, dict): return
            i = len(self.bones)
            self.bones.append({"name": str(field(node, "name") or f"bone{i}"), "parent": parent,
                               "pivot": _vec(field(node, "origin")) / 16.0,
                               "rest": _vec(field(node, "rotation"))})
            if "uuid" in node: self.bone_by_uuid[node["uuid"]] = i
            for c in node.get("children") or []: walk(c, i)
        for n in doc.get("outliner") or []: walk(n, -1)
        root = None
        def synthetic():
            nonlocal root
            if root is None:
                root = len(self.bones)
                self.bones.append({"name": "root", "parent": -1, "pivot": np.zeros(3), "rest": np.zeros(3)})
            return root
        tris = []   # (bone, corners(4,3), uv(4,2), normal)
        for el in doc.get("elements") or []:
            if "from" not in el or "to" not in el: continue
            inf = _num(el.get("inflate", 0))
            frm = _vec(el["from"]) - inf; to = _vec(el["to"]) + inf
            R, origin = np.eye(3), np.zeros(3)
            if "rotation" in el:
                origin = _vec(el.get("origin")) / 16.0; R = rot_zyx(_vec(el["rotation"]))
            bone = by_el.get(el.get("uuid"))
            if bone is None: bone = synthetic()
            faces = el.get("faces") or {}
            for name, normal, sel in CREATURE_FACES:
                f = faces.get(name)
                if not isinstance(f, dict) or ("texture" in f and f["texture"] is None) or len(f.get("uv") or []) < 4:
                    continue
                u0, v0, u1, v1 = (_num(f["uv"][0]) / self.resW, _num(f["uv"][1]) / self.resH,
                                  _num(f["uv"][2]) / self.resW, _num(f["uv"][3]) / self.resH)
                cs = []
                for c in sel:
                    p = np.array([to[0] if c[0] else frm[0], to[1] if c[1] else frm[1], to[2] if c[2] else frm[2]]) / 16.0
                    if "rotation" in el: p = origin + R @ (p - origin)
                    cs.append(p)
                n = R @ np.asarray(normal, float); n = n / (np.linalg.norm(n) or 1)
                tris.append((bone, np.array(cs), np.array([(u0, v0), (u1, v0), (u1, v1), (u0, v1)]), n))
        if not self.bones: synthetic()
        self.faces = tris
        self.anims = {}
        for an in doc.get("animations") or []:
            tracks = {}
            for uid, a in (an.get("animators") or {}).items():
                b = self.bone_by_uuid.get(uid)
                if b is None: continue
                tr = {"rotation": [], "position": []}
                for kf in a.get("keyframes") or []:
                    ch = kf.get("channel")
                    if ch not in tr: continue
                    dp = (kf.get("data_points") or [{}])[0]
                    v = np.array([_num(dp.get("x", 0)), _num(dp.get("y", 0)), _num(dp.get("z", 0))])
                    if ch == "position": v = v / 16.0
                    tr[ch].append((_num(kf.get("time", 0)), v, kf.get("interpolation", "linear")))
                for ch in tr: tr[ch].sort(key=lambda k: k[0])
                tracks[b] = tr
            self.anims[an.get("name", "animation")] = {"length": _num(an.get("length", 0)),
                                                        "loop": an.get("loop", "once") == "loop", "tracks": tracks}

    def find(self, name):
        """BbModel::findAnimation: exact, else a Bedrock name's last segment."""
        if name in self.anims: return name
        for n in self.anims:
            if n.rsplit(".", 1)[-1] == name: return n
        return None

    @staticmethod
    def _sample(keys, t, rest):
        if not keys: return rest
        if t <= keys[0][0]: return keys[0][1]
        if t >= keys[-1][0]: return keys[-1][1]
        hi = 1
        while keys[hi][0] < t: hi += 1
        a, b = keys[hi - 1], keys[hi]
        if a[2] == "step": return a[1]
        span = b[0] - a[0]; f = (t - a[0]) / span if span > 0 else 0.0
        if "catmullrom" in (a[2], b[2]):
            p0 = keys[hi - 2][1] if hi >= 2 else a[1]; p3 = keys[hi + 1][1] if hi + 1 < len(keys) else b[1]
            p1, p2 = a[1], b[1]; t2, t3 = f * f, f * f * f
            return 0.5 * (2 * p1 + (p2 - p0) * f + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (3 * p1 - p0 - 3 * p2 + p3) * t3)
        return a[1] + (b[1] - a[1]) * f

    def pose(self, anim=None, time=0.0):
        """evaluateBbPose: per-bone 4x4 (as (R, t) pairs) composed down the tree."""
        a = self.anims.get(self.find(anim)) if anim else None
        t = 0.0
        if a and a["length"] > 0:
            t = math.fmod(time, a["length"]) if a["loop"] else min(time, a["length"])
        out = []
        for i, b in enumerate(self.bones):
            rot, pos = b["rest"].copy(), np.zeros(3)
            if a and i in a["tracks"]:
                tr = a["tracks"][i]
                rot = b["rest"] + self._sample(tr["rotation"], t, np.zeros(3))
                pos = self._sample(tr["position"], t, np.zeros(3))
            R = rot_zyx(rot)
            # local = T(pivot + pos) * R * T(-pivot)
            lt = b["pivot"] + pos - R @ b["pivot"]
            if b["parent"] >= 0:
                PR, Pt = out[b["parent"]]
                out.append((PR @ R, PR @ lt + Pt))
            else:
                out.append((R, lt))
        return out

    def mesh(self, anim=None, time=0.0):
        P = self.pose(anim, time)
        th, tw = self.tex.shape[:2]
        corners, uvs, normals = [], [], []
        for bone, cs, uv, n in self.faces:
            R, t = P[bone]
            # Triangles TL-BL-BR, TL-BR-TR like BbModel.cpp; as a quad that is
            # corners (TL, BL, BR, TR) wound 0-1-2-3.
            order = [0, 3, 2, 1]
            corners.append([R @ cs[k] + t for k in order])
            uvs.append([(uv[k][0] * tw, uv[k][1] * th) for k in order])
            normals.append(R @ n)
        return Mesh(*_quads_to_tris(corners, uvs, normals), self.tex)


# ---- the rasterizer ---------------------------------------------------------

def view_basis(yaw_deg, pitch_deg):
    """Camera looking DOWN `pitch` degrees along compass heading `yaw`:
    0 looks toward -Z (so it sees the model's +Z/back face), 180 looks toward +Z
    (sees the -Z front), 90 looks toward +X."""
    y, p = math.radians(yaw_deg), math.radians(pitch_deg)
    fwd = np.array([math.sin(y) * math.cos(p), -math.sin(p), -math.cos(y) * math.cos(p)])
    right = np.cross(fwd, [0, 1, 0]); right /= np.linalg.norm(right)
    up = np.cross(right, fwd)
    return fwd, right, up


def rasterize(mesh, yaw, pitch, center, scale, size):
    """One orthographic view. Returns an RGBA uint8 (size, size, 4) image."""
    W = H = size
    fwd, right, up = view_basis(yaw, pitch)
    rel = mesh.pos - center
    sx = W / 2 + (rel @ right) * scale
    sy = H / 2 - (rel @ up) * scale
    depth = rel @ fwd
    shade = 0.35 + 0.65 * np.clip(mesh.normal @ -LIGHT_DIR, 0, None)
    img = np.empty((H, W, 4), np.uint8); img[:] = BACKGROUND
    zbuf = np.full((H, W), np.inf)
    tex = mesh.tex; th, tw = tex.shape[:2]
    for i in range(len(mesh.pos)):
        x, y, z = sx[i], sy[i], depth[i]
        x0, x1 = max(int(math.floor(x.min())), 0), min(int(math.ceil(x.max())), W - 1)
        y0, y1 = max(int(math.floor(y.min())), 0), min(int(math.ceil(y.max())), H - 1)
        if x0 > x1 or y0 > y1: continue
        X, Y = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        d = (y[1] - y[2]) * (x[0] - x[2]) + (x[2] - x[1]) * (y[0] - y[2])
        if abs(d) < 1e-12: continue
        w0 = ((y[1] - y[2]) * (X - x[2]) + (x[2] - x[1]) * (Y - y[2])) / d
        w1 = ((y[2] - y[0]) * (X - x[2]) + (x[0] - x[2]) * (Y - y[2])) / d
        w2 = 1 - w0 - w1
        inside = (w0 >= -1e-6) & (w1 >= -1e-6) & (w2 >= -1e-6)
        if not inside.any(): continue
        zz = w0 * z[0] + w1 * z[1] + w2 * z[2]
        uv = mesh.uv[i]
        u = w0 * uv[0, 0] + w1 * uv[1, 0] + w2 * uv[2, 0]
        v = w0 * uv[0, 1] + w1 * uv[1, 1] + w2 * uv[2, 1]
        tx = np.clip(np.floor(u).astype(int), 0, tw - 1); ty = np.clip(np.floor(v).astype(int), 0, th - 1)
        texel = tex[ty, tx]
        sub = zbuf[y0:y1 + 1, x0:x1 + 1]
        ok = inside & (zz < sub) & (texel[..., 3] >= 128)
        if not ok.any(): continue
        sub[ok] = zz[ok]
        rgb = np.clip(texel[..., :3].astype(float) * shade[i], 0, 255).astype(np.uint8)
        region = img[y0:y1 + 1, x0:x1 + 1]
        region[ok, :3] = rgb[ok]; region[ok, 3] = 255
    return img


# Models face north (-Z), so "front" is the -Z face; right is +X (east).
VIEWS = [(135, 30, "front-left"), (225, 30, "front-right"), (45, 30, "back-left"), (315, 30, "back-right")]


def fit(meshes, views=VIEWS, size=240, margin=0.12):
    """Centre + scale that frames every mesh (every animation pose) in every
    view, so a model never jumps or shrinks between frames of a strip."""
    pts = np.concatenate([m.pos.reshape(-1, 3) for m in meshes])
    lo, hi = pts.min(0), pts.max(0); center = (lo + hi) / 2
    ext = 1e-6
    for yaw, pitch, _ in views:
        _, r, u = view_basis(yaw, pitch)
        rel = pts - center
        ext = max(ext, np.abs(rel @ r).max(), np.abs(rel @ u).max())
    return center, (size / 2) * (1 - margin) / ext
