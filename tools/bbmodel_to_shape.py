#!/usr/bin/env python3
"""Bake Blockbench `java_block` models into the generated block-shape table.

Pure stdlib (hand-rolled PNG reader/writer), like make_atlas.py and
make_sounds.py. Reads one or more `.bbmodel` files, and writes:

  game/assets/shapes.png                      -- packed shaped-block textures
  game/include/game/generated/BlockShapes.inl -- the shape data (BlockShape.h
                                                 owns the types and registry)

Nothing includes the .inl yet -- this bake runs ahead of the renderer refactor
(see ROADMAP.md, "3D detailed blocks") so the data questions get answered while
the engine is still untouched. Rerun after editing a model:

    python tools/bbmodel_to_shape.py models/brewing_cauldron.bbmodel

Deliberately NOT touching game/assets/atlas.png: shaped-block textures are
128px regions, not 16px tiles, and the two want different packing. Merging the
sheets later is a bake-time decision, not a source change.

What it handles: cuboid elements, per-face UVs, box-UV projects (Blockbench
still writes explicit per-face rects, and this preserves their MIRRORING --
half the faces in a box-UV model have descending rects and a min/max normalize
would silently un-mirror them), embedded base64 textures, and Minecraft
animated-texture strips (a texture N*uv_height tall is N frames).

Rotated elements are supported by baking the rotation into the quad corners.
A rotated box is not an AABB, and the shape table backs collision as well as
the mesh, so the two split: geometry is EXACT, collision uses the rotated
box's bounding box. Slightly generous to walk into, and the reason quads and
boxes are separate arrays.

FLAT elements (zero extent on exactly one axis) are supported, which is what a
crossed-plane crop is made of: they bake to 2 quads rather than 6-with-slivers,
and collide as a thin slab so they can still be walked into and aimed at. Flat
on two axes -- a line, a point -- is still degenerate and skipped.

NAMED GROUPS become PARTS: each quad records which group it belongs to and its
corners as vectors from that group's pivot, which is what lets the game turn a
drill or rock a lid without a remesh (see uPartRot[] in voxel.vert). Both
outliner layouts are read -- Blockbench 5.0's flat `groups` table and a model
with no groups at all -- so every element lands on some part, part 0 being the
static root.

What it still rejects, loudly: non-cube (mesh) elements, geometry reaching
outside its own cell, and textures past the first.
"""

import argparse
import base64
import json
import math
import re
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Blockbench model space is 0..16 per cell; the game uses cell-local 0..1.
MODEL_UNITS = 16.0

# Half-texel inset on every baked face rect, in sheet pixels. The atlas samples
# nearest, and a face rect's edge lands exactly on a texel boundary; nudging
# inward keeps a face off its neighbour's first column. Applied with the rect's
# SIGN so mirrored faces inset the same way, and clamped to a quarter of the
# rect: box-UV models routinely produce sub-texel rects (a 0.5-unit-tall box has
# a half-pixel-tall side face), and a flat 0.5px inset would collapse or invert
# those instead of nudging them.
TEXEL_INSET = 0.5

# How far, in model units, geometry may poke out of its own cell before the
# bake refuses it. Rotation inflates a box's reach by arithmetic, so a strict
# zero would reject perfectly ordinary 45-degree elements; a whole unit is
# still far below "this model was designed to span two cells".
CELL_OVERHANG_TOLERANCE = 1.0

# A FLAT element -- zero extent on exactly one axis -- is a legitimate model: the
# crossed planes a crop is made of. Only two-or-more flat axes (a line, a point)
# are genuinely degenerate. Rendering handles a plane exactly, but collision
# cannot: `boxes` feeds boxOverlapsWorld and the raycast, and a zero-thickness
# AABB overlaps nothing, so a plane would be un-walkable-into AND un-aimable-at.
# So the COLLISION box alone is given a minimum thickness, centred on the plane;
# the drawn geometry stays exactly where it was authored.
MIN_COLLISION_UNITS = 1.0

# What one shaped vertex costs, for the size report only: position(3),
# normal(3), uv(2), emissive(1), animBank(1), partOff(3), partSlot(1). This had
# said 9 since before shaped vertices carried an animation bank, so every KB
# figure it has ever printed -- including the ones quoted in ROADMAP.md -- was
# an underestimate. Keep it in step with ChunkMesher.h.
SHAPED_VERTEX_FLOATS = 14

# Face order matches ChunkMesher's kFaces: +X, -X, +Y, -Y, +Z, -Z.
FACE_ORDER = ["east", "west", "up", "down", "south", "north"]

# The four corners of every quad, as (s, t) parameters into the face's u/v
# axes. This order is COUNTER-CLOCKWISE seen from outside the box, so the
# mesher emits (0,1,2) + (0,2,3) with no winding fix of its own.
CORNER_ST = [(0.0, 0.0), (0.0, 1.0), (1.0, 1.0), (1.0, 0.0)]

# How each face's UV rect parameterizes its quad, as (axis, direction) pairs for
# the u and v texture axes; axis 0/1/2 = X/Y/Z. This is Minecraft's convention:
# each face is textured as seen head-on from OUTSIDE the box, with texture v
# running DOWN. Blockbench's +Z is Minecraft's "south".
#
# ---- This table is the handedness knob. ----
# If a baked model comes out mirrored or upside down on one axis, the fix is a
# sign here, not in the mesher -- exactly like BbModel.cpp funnelling all unit
# conversion through geoToWorld. Corner positions are baked THROUGH it, so the
# runtime never sees it and there is no second copy to drift.
FACE_AXES = {
    #          u-axis, u-dir, v-axis, v-dir
    "east":  (2, -1, 1, -1),
    "west":  (2, +1, 1, -1),
    "up":    (0, +1, 2, +1),
    "down":  (0, +1, 2, -1),
    "south": (0, +1, 1, -1),
    "north": (0, -1, 1, -1),
}


# --- PNG ---------------------------------------------------------------------

def read_png(data):
    """Decode an 8-bit RGB/RGBA PNG to (w, h, bytearray RGBA)."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    w, h = struct.unpack(">II", data[16:24])
    depth, color, _, _, interlace = data[24:29]
    if depth != 8 or color not in (2, 6):
        raise ValueError(f"unsupported PNG (bitdepth {depth}, colortype {color}); "
                         "export 8-bit RGB or RGBA")
    if interlace:
        raise ValueError("interlaced PNG unsupported")

    idat = b""
    i = 8
    while i < len(data):
        ln = struct.unpack(">I", data[i:i + 4])[0]
        tag = data[i + 4:i + 8]
        if tag == b"IDAT":
            idat += data[i + 8:i + 8 + ln]
        i += 12 + ln

    src = zlib.decompress(idat)
    bpp = 4 if color == 6 else 3
    stride = w * bpp
    out = bytearray()
    prev = bytearray(stride)
    p = 0
    for _ in range(h):
        ft = src[p]
        p += 1
        line = bytearray(src[p:p + stride])
        p += stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if ft == 1:
                line[x] = (line[x] + a) & 255
            elif ft == 2:
                line[x] = (line[x] + b) & 255
            elif ft == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif ft == 4:
                pp = a + b - c
                pa, pb, pc = abs(pp - a), abs(pp - b), abs(pp - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        out += line
        prev = line

    if bpp == 4:
        return w, h, out
    rgba = bytearray(w * h * 4)
    for k in range(w * h):
        rgba[k * 4:k * 4 + 3] = out[k * 3:k * 3 + 3]
        rgba[k * 4 + 3] = 255
    return w, h, rgba


def write_png(path, w, h, rgba):
    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    raw = b"".join(b"\x00" + bytes(rgba[y * w * 4:(y + 1) * w * 4]) for y in range(h))
    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(raw, 9)) +
           chunk(b"IEND", b""))
    Path(path).write_bytes(png)


def blit(dst, dw, dst_x, dst_y, src, sw, sx, sy, w, h):
    for y in range(h):
        di = ((dst_y + y) * dw + dst_x) * 4
        si = ((sy + y) * sw + sx) * 4
        dst[di:di + w * 4] = src[si:si + w * 4]


# --- model loading -----------------------------------------------------------

def rotation_matrix(axis, degrees):
    """Rotation about one axis, as a 3x3 row-major tuple."""
    c = math.cos(math.radians(degrees))
    s = math.sin(math.radians(degrees))
    if axis == 0:
        return ((1, 0, 0), (0, c, -s), (0, s, c))
    if axis == 1:
        return ((c, 0, s), (0, 1, 0), (-s, 0, c))
    return ((c, -s, 0), (s, c, 0), (0, 0, 1))


def apply3(m, v):
    return tuple(m[i][0] * v[0] + m[i][1] * v[1] + m[i][2] * v[2] for i in range(3))


class Quad:
    """One textured face of one box, in cell-local space.

    Corners are BAKED, not derived at runtime: an element may be rotated, and a
    rotated face is not axis-aligned, so there is nothing for a lo/hi pair to
    describe. The mesher just copies these.
    """

    def __init__(self, pos, normal, face, uv, cull, blo, bhi, rotated, part, off):
        self.pos = pos        # 4 corners in 0..1, CCW seen from outside
        self.normal = normal  # rotated face normal
        self.face = face      # index into FACE_ORDER -- which neighbour `cull` consults
        self.uv = uv          # [u0, v0, u1, v1] in model uv-space px, sign kept
        self.cull = cull      # face is flush with a cell wall (never when rotated)
        self.blo = blo        # owning box's AABB, for the interior-face cull
        self.bhi = bhi
        self.rotated = rotated
        self.part = part      # index into the model's part table
        self.off = off        # 4 corners as vectors FROM that part's pivot


class Part:
    """One named group from the outliner -- a thing that can move.

    A part is addressed by NAME from the game side, because which parts actually
    animate is gameplay policy, not model data. `parent` is kept so animating a
    group can later carry its subgroups without a re-bake.
    """

    def __init__(self, name, parent, pivot):
        self.name = name
        self.parent = parent
        self.pivot = pivot    # cell-local (0..1)


class Model:
    def __init__(self, name):
        self.name = name
        self.quads = []
        self.boxes = []       # cell-local AABBs, for collision/rays
        self.box_rotated = []  # parallel: was the box's element rotated?
        self.parts = []       # Part rows; [0] is always the static root
        self.frames = 1
        self.frame_time = 0
        self.tex = None       # (w, h, rgba) cropped, frames stacked
        self.crop = None      # (x, y, w, h) in texture px, of one frame
        self.region = None    # (x, y) placement in the sheet
        self.raw_quads = 0    # before interior culling


def load_parts(doc):
    """Outliner -> (parts, element uuid -> part index).

    Two outliner shapes have to work, and both are shipped in this repo:

      * Blockbench 5.0 (every machine model here) strips the outliner to
        {uuid, children} and moves name/origin/rotation into a flat `groups`
        table. Without reading it, every group pivots about the model origin --
        the same quirk BbModel.cpp:198 handles for creature bones.
      * No groups at all (every herb_crop model): the outliner is a flat list of
        bare element-uuid strings. Those elements belong to part 0.

    Part 0 is the implicit static root, so a model with no groups still has a
    valid part table and every quad still names a part.
    """
    props = {}
    for g in doc.get("groups") or []:
        if isinstance(g, dict) and isinstance(g.get("uuid"), str):
            props[g["uuid"]] = g

    def field(node, key):
        """Read a group property inline (4.x) or from the `groups` table (5.0)."""
        if key in node:
            return node[key]
        g = props.get(node.get("uuid"))
        if isinstance(g, dict) and key in g:
            return g[key]
        return None

    parts = [Part("static", -1, (0.0, 0.0, 0.0))]
    by_element = {}

    def walk(node, parent):
        if isinstance(node, str):  # a leaf element uuid
            by_element[node] = parent
            return
        if not isinstance(node, dict):
            return
        name = field(node, "name")
        origin = field(node, "origin") or (8, 8, 8)
        pivot = tuple(float(v) / MODEL_UNITS for v in origin)
        index = len(parts)
        parts.append(Part(str(name) if name else f"part{index}", parent, pivot))
        for child in node.get("children") or []:
            walk(child, index)

    for node in doc.get("outliner") or []:
        walk(node, 0)
    return parts, by_element


def load_model(path):
    doc = json.loads(Path(path).read_text(encoding="utf-8"))
    name = path.stem
    fmt = doc.get("meta", {}).get("model_format", "?")
    if fmt not in ("java_block", "free", "bedrock", "bedrock_old"):
        print(f"  ! {name}: unfamiliar model_format '{fmt}' -- baking anyway")

    model = Model(name)
    model.parts, part_of_element = load_parts(doc)

    res = doc.get("resolution", {})
    textures = doc.get("textures", [])
    if not textures:
        raise ValueError(f"{name}: no textures")
    if len(textures) > 1:
        print(f"  ! {name}: {len(textures)} textures; only #0 is baked "
              "(multi-texture models need one region each)")
    tex = textures[0]
    uv_w = float(tex.get("uv_width") or res.get("width") or 16)
    uv_h = float(tex.get("uv_height") or res.get("height") or 16)

    src = tex.get("source", "")
    if not src.startswith("data:image"):
        raise ValueError(f"{name}: texture is not embedded (path='{tex.get('path')}'); "
                         "re-save with the texture embedded")
    tw, th, pixels = read_png(base64.b64decode(src.split(",", 1)[1]))

    # A texture N*uv_height tall is a Minecraft animation strip.
    frames = 1
    if uv_h > 0 and th > uv_h and th % int(uv_h) == 0:
        frames = th // int(uv_h)
    model.frames = frames
    model.frame_time = tex.get("frame_time", 0)

    meshes, blank, nrot, thin, planes = 0, 0, 0, 0, 0
    for el in doc.get("elements", []):
        if el.get("type", "cube") != "cube":
            meshes += 1
            continue
        if el.get("visible") is False:
            continue

        # Which group owns this element, and the point that group turns about.
        # The pivot is NOT put through `place`: element rotation is already baked
        # into the corners, while the part rotation is a separate, later transform
        # about the group's own origin.
        part = part_of_element.get(el.get("uuid"), 0)
        pivot = model.parts[part].pivot

        frm = [float(v) for v in el["from"]]
        to = [float(v) for v in el["to"]]
        inflate = float(el.get("inflate", 0.0))
        lo = [min(frm[i], to[i]) - inflate for i in range(3)]
        hi = [max(frm[i], to[i]) + inflate for i in range(3)]
        # Flat on ONE axis is a plane, which is real geometry (see
        # MIN_COLLISION_UNITS). Flat on two or more is a line or a point.
        flat_axes = [i for i in range(3) if hi[i] - lo[i] <= 0.0]
        if len(flat_axes) >= 2:
            blank += 1
            continue
        flat_axis = flat_axes[0] if flat_axes else -1
        if flat_axis >= 0:
            planes += 1

        # Element rotation. Minecraft allows one axis at a time; Blockbench
        # writes all three, so take the non-zero one and say so if there are
        # several (composing them would be an order the format does not define).
        rot = [float(v) for v in (el.get("rotation") or (0, 0, 0))]
        nonzero = [i for i in range(3) if abs(rot[i]) > 1e-6]
        rmat, rorigin = None, None
        if nonzero:
            if len(nonzero) > 1:
                print(f"  ! {name}: '{el.get('name')}' rotates on "
                      f"{len(nonzero)} axes; only the first is applied")
            if el.get("rescale"):
                print(f"  ! {name}: '{el.get('name')}' has rescale set "
                      "(ignored -- it only matters for MC's own renderer)")
            rmat = rotation_matrix(nonzero[0], rot[nonzero[0]])
            rorigin = [float(v) for v in el.get("origin", (8, 8, 8))]
            nrot += 1

        def place(p):
            """Model-space point -> cell-local, through the element rotation."""
            if rmat is not None:
                d = [p[i] - rorigin[i] for i in range(3)]
                r = apply3(rmat, d)
                p = [rorigin[i] + r[i] for i in range(3)]
            return tuple(v / MODEL_UNITS for v in p)

        # Collision stays axis-aligned: the AABB of the (possibly rotated) box.
        # Rendering is exact, collision is conservative -- a deliberate split,
        # and the reason quads and boxes are separate arrays.
        corners8 = [place((lo[0] if i & 1 else hi[0],
                           lo[1] if i & 2 else hi[1],
                           lo[2] if i & 4 else hi[2])) for i in range(8)]
        blo = tuple(min(c[a] for c in corners8) for a in range(3))
        bhi = tuple(max(c[a] for c in corners8) for a in range(3))
        # Give a collapsed COLLISION axis its minimum thickness, symmetrically.
        # Done on the AABB rather than on `lo`/`hi` so a ROTATED plane -- whose
        # flat axis is no longer a world axis -- is covered by the same line.
        # `blo`/`bhi` themselves stay exact: they also bound the quads, and
        # inflating them there would let a face look sealed inside the model.
        # Only a genuinely FLAT element gets this: a merely thin box (a trim
        # ring, a rim) already has extent on every axis and collides as
        # authored, and inflating those would quietly fatten four shipped
        # models' collision.
        clo, chi = list(blo), list(bhi)
        if flat_axis >= 0:
            thick = MIN_COLLISION_UNITS / MODEL_UNITS
            for a in range(3):
                if chi[a] - clo[a] < thick:
                    mid = 0.5 * (clo[a] + chi[a])
                    clo[a], chi[a] = mid - 0.5 * thick, mid + 0.5 * thick
        model.boxes.append((tuple(clo), tuple(chi)))
        model.box_rotated.append(rmat is not None)

        for fi, fname in enumerate(FACE_ORDER):
            face = el.get("faces", {}).get(fname)
            if not face or face.get("texture") is None:
                continue
            # On a flat element only the two faces PERPENDICULAR to the flat
            # axis have any area; the other four are zero-area slivers. Drop
            # them, so a plane costs exactly 2 quads instead of 6.
            if flat_axis >= 0 and fi // 2 != flat_axis:
                continue
            uv = [float(v) for v in face["uv"]]
            # A face whose uv rect is degenerate on one axis is NOT dropped:
            # box-UV floors a box's dimensions to whole texels, so every
            # element thinner than 1 unit (trim rings, banding, rims) gets
            # zero-height side rects while its geometry is perfectly real.
            # Dropping them punches holes in exactly the fine detail these
            # models are for. A collapsed rect samples a single texel line,
            # which is what Blockbench and Minecraft draw; the inset clamp
            # already leaves a zero-extent rect alone.
            if uv[0] == uv[2] or uv[1] == uv[3]:
                thin += 1
            if face.get("rotation"):
                # Face-level UV rotation needs a corner permutation, not a rect.
                print(f"  ! {name}: face rotation on '{el.get('name')}'.{fname} "
                      "ignored (author without face UV rotation)")

            axis = fi // 2
            outward_hi = (fi % 2) == 0
            fixed = hi[axis] if outward_hi else lo[axis]
            uA, uD, vA, vD = FACE_AXES[fname]

            pos = []
            for s, t in CORNER_ST:
                p = [0.0, 0.0, 0.0]
                p[axis] = fixed
                p[uA] = lo[uA] + (hi[uA] - lo[uA]) * (s if uD > 0 else 1.0 - s)
                p[vA] = lo[vA] + (hi[vA] - lo[vA]) * (t if vD > 0 else 1.0 - t)
                pos.append(place(p))

            normal = [0.0, 0.0, 0.0]
            normal[axis] = 1.0 if outward_hi else -1.0
            if rmat is not None:
                normal = list(apply3(rmat, normal))

            # A rotated face is never flush with a cell wall, so it can never
            # be hidden by a neighbour.
            wall = (rmat is None and
                    (hi[axis] >= MODEL_UNITS - 1e-4 if outward_hi else lo[axis] <= 1e-4))

            # Each corner as a VECTOR from the part's pivot. Baking the offset
            # rather than the pivot is what lets a part rotate in a world-space
            # chunk mesh at all: p = pivot + M*(p - pivot) rearranges to
            # p + (M*d - d), and d is translation-invariant, so the shader needs
            # no pivot, no cell origin, and no unsafe floor(aPos).
            off = [tuple(c[i] - pivot[i] for i in range(3)) for c in pos]

            model.quads.append(
                Quad(pos, normal, fi, uv, wall, blo, bhi, rmat is not None,
                     part, off))

    if len(model.parts) > 1:
        print("  parts: " + ", ".join(pt.name for pt in model.parts[1:]))
    else:
        print("  no named groups: every quad is on the static part")
    if nrot:
        print(f"  {nrot} rotated element(s): geometry exact, collision uses "
              "their bounding boxes")
    if meshes:
        print(f"  ! {name}: skipped {meshes} non-cube (mesh) element(s)")
    if blank:
        print(f"  ! {name}: skipped {blank} zero-area box(es)")
    if planes:
        print(f"  {planes} flat element(s): 2 quads each, collided as a "
              f"{MIN_COLLISION_UNITS:g}-unit slab")
    if thin:
        print(f"  {thin} face(s) on sub-unit-thick elements sample a single "
              "texel line (box-UV floors box size)")
    if not model.quads:
        raise ValueError(f"{name}: nothing to bake")

    # Geometry must stay inside its own cell. The engine's collision and ray
    # queries iterate the cells an AABB overlaps and test only THAT cell's
    # boxes (see Collision.h), so a box hanging into a neighbour would render
    # correctly and then be silently missed by physics -- the worst kind of
    # bug to chase. Minecraft allows from/to outside 0..16, so this is a real
    # thing a model can do, not a theoretical one.
    #
    # A SMALL overhang is tolerated and clamped, because 45-degree rotation
    # produces it by arithmetic rather than by intent: a 10-wide element spun
    # about its centre sweeps a 14.14-wide diagonal, and a corner can clear the
    # cell by a fraction of a unit without the author ever meaning to leave it.
    # Clamping costs a sliver of collision no player can feel. A model designed
    # to span cells overshoots by whole units and still fails.
    tol = CELL_OVERHANG_TOLERANCE / MODEL_UNITS
    over = [max(max(-lo[i], hi[i] - 1.0) for i in range(3)) for lo, hi in model.boxes]
    worst = max(over) if over else 0.0
    if worst > tol:
        i = over.index(worst)
        lo, hi = model.boxes[i]
        raise ValueError(
            f"{name}: geometry reaches {worst * MODEL_UNITS:.2f} units outside "
            f"the 0..16 cell, e.g. {tuple(round(v * MODEL_UNITS, 2) for v in lo)}"
            f"...{tuple(round(v * MODEL_UNITS, 2) for v in hi)}\n"
            "    Collision only tests a cell's own boxes, so overhanging\n"
            "    geometry would draw but not collide. Keep the model inside\n"
            "    the cell, or make it a multiblock.")

    nclamped = sum(1 for o in over if o > 1e-6)
    if nclamped:
        model.boxes = [(tuple(min(max(v, 0.0), 1.0) for v in lo),
                        tuple(min(max(v, 0.0), 1.0) for v in hi))
                       for lo, hi in model.boxes]
        print(f"  {nclamped} collision box(es) clamped to the cell "
              f"(max {worst * MODEL_UNITS:.2f} units over, from rotation)")

    model.raw_quads = len(model.quads)

    # Crop to the used UV region, in texture pixels, snapped outward.
    scale_x, scale_y = tw / uv_w, (th / frames) / uv_h
    us = [v for q in model.quads for v in (q.uv[0], q.uv[2])]
    vs = [v for q in model.quads for v in (q.uv[1], q.uv[3])]
    cx0 = max(0, math.floor(min(us) * scale_x))
    cy0 = max(0, math.floor(min(vs) * scale_y))
    cx1 = min(tw, math.ceil(max(us) * scale_x))
    cy1 = min(th // frames, math.ceil(max(vs) * scale_y))
    cw = max(1, cx1 - cx0)
    ch = max(1, cy1 - cy0)
    model.crop = (cx0, cy0, cw, ch, scale_x, scale_y)

    # Stack every frame's cropped band; the baked UVs address frame 0 and a
    # shader adds (frame * ch) to reach the rest without a remesh.
    stacked = bytearray(cw * ch * frames * 4)
    for f in range(frames):
        blit(stacked, cw, 0, f * ch, pixels, tw, cx0, f * (th // frames) + cy0, cw, ch)
    model.tex = (cw, ch * frames, stacked)
    return model


# --- interior face culling ---------------------------------------------------

def cull_interior(model, eps=1e-6):
    """Drop faces sealed against another box's opposing face.

    Bake-time only: a 40-box model with all six faces on every box carries a lot
    of geometry that is permanently inside the model. Neighbour occlusion in the
    mesher cannot help here (nothing is flush with a cell wall), so this is the
    only place the cost comes off.
    """
    # Rotated boxes sit out: their faces are not axis-aligned, so neither the
    # coplanarity test nor the coverage test means anything for them.
    def covered(q):
        if q.rotated:
            return False
        axis = q.face // 2
        outward_hi = (q.face % 2) == 0
        plane = q.bhi[axis] if outward_hi else q.blo[axis]
        others = [a for a in (0, 1, 2) if a != axis]
        for i, (lo, hi) in enumerate(model.boxes):
            if model.box_rotated[i]:
                continue
            touch = lo[axis] if outward_hi else hi[axis]
            if abs(touch - plane) > eps:
                continue
            if all(lo[a] <= q.blo[a] + eps and hi[a] >= q.bhi[a] - eps for a in others):
                return True
        return False

    model.quads = [q for q in model.quads if not covered(q)]


# --- sheet packing -----------------------------------------------------------

def pack(models, max_width=1024, pad=1):
    """Shelf-pack each model's texture into one sheet.

    Both dimensions end up powers of two, and the width shrinks to whatever the
    content actually needed -- one 128px model should not ship a 1024px sheet.
    """
    x, y, shelf = pad, pad, 0
    for m in sorted(models, key=lambda m: -m.tex[1]):
        w, h, _ = m.tex
        if x + w + pad > max_width and x > pad:
            x, y, shelf = pad, y + shelf + pad, 0
        m.region = (x, y)
        x += w + pad
        shelf = max(shelf, h)

    used_w = max(m.region[0] + m.tex[0] + pad for m in models)
    width = 1 << (used_w - 1).bit_length()
    height = 1 << (y + shelf + pad - 1).bit_length()

    sheet = bytearray(width * height * 4)
    for m in models:
        w, h, px = m.tex
        rx, ry = m.region
        if ry + h > height:
            raise ValueError("sheet overflow -- raise --sheet-width")
        blit(sheet, width, rx, ry, px, w, 0, 0, w, h)
    return width, height, sheet


# --- emit --------------------------------------------------------------------

def cpp_ident(name):
    """File stem -> CamelCase C++ identifier.

    Anything that is not alphanumeric becomes a word break, because model files
    arrive from a browser as `alchemical_alembic (1).bbmodel` and a stem pasted
    straight into a symbol name would emit code that does not compile.
    """
    words = [w for w in re.split(r"[^0-9A-Za-z]+", name) if w]
    ident = "".join(w[:1].upper() + w[1:] for w in words)
    if not ident or ident[0].isdigit():
        ident = "Shape" + ident
    return ident


def fmt_uv(q, m, sheet_w, sheet_h):
    cx0, cy0, cw, ch, sx, sy = m.crop
    rx, ry = m.region

    # Inset amounts, clamped so a sub-texel rect stays non-degenerate.
    iu = min(TEXEL_INSET, abs(q.uv[2] - q.uv[0]) * sx * 0.25)
    iv = min(TEXEL_INSET, abs(q.uv[3] - q.uv[1]) * sy * 0.25)

    def one(u, v, du, dv):
        px = u * sx - cx0 + rx + iu * du
        py = v * sy - cy0 + ry + iv * dv
        return px / sheet_w, py / sheet_h

    du = 1.0 if q.uv[2] > q.uv[0] else -1.0
    dv = 1.0 if q.uv[3] > q.uv[1] else -1.0
    u0, v0 = one(q.uv[0], q.uv[1], du, dv)
    u1, v1 = one(q.uv[2], q.uv[3], -du, -dv)
    return u0, v0, u1, v1


def emit(models, sheet_w, sheet_h, out_path, argv):
    L = []
    L.append("// GENERATED by tools/bbmodel_to_shape.py -- do not edit by hand.")
    L.append(f"//   {argv}")
    L.append("//")
    L.append("// Sub-cube block geometry baked from Blockbench java_block models.")
    L.append("// DATA ONLY: included by game/BlockShape.h, which owns the types")
    L.append("// (ShapeQuad / ShapeAabb / ShapeAnim) and the ShapeId registry.")
    L.append("// Texture: game/assets/shapes.png "
             f"({sheet_w}x{sheet_h}), sampled like the atlas.")
    L.append("//")
    L.append("// Each quad carries its four corners already in world-ish cell")
    L.append("// space (0..1), COUNTER-CLOCKWISE seen from outside, with the")
    L.append("// matching uv per corner. Everything -- the face axis mapping,")
    L.append("// UV mirroring, element rotation -- is resolved HERE, so the")
    L.append("// mesher copies vertices and never reconstructs geometry. If a")
    L.append("// model comes out mirrored or upside down, the fix is a sign in")
    L.append("// the BAKE; there is no second copy in the C++ to drift.")
    L.append("//")
    L.append("// `face` survives only to say which neighbour `cull` consults.")
    L.append("// Rotated faces are never flush with a cell wall, so they are")
    L.append("// never culled.")
    L.append("//")
    L.append("// `part` names a row in this shape's ShapePart table -- the")
    L.append("// Blockbench group the face belongs to -- and `partOff` carries")
    L.append("// its corners as vectors FROM that group's pivot. Rotating about")
    L.append("// a pivot is p + (M*d - d) with d = p - pivot, and d is the same")
    L.append("// in cell and world space, so a world-space chunk vertex can")
    L.append("// turn without the shader ever knowing where its cell is.")
    L.append("// Part 0 is the static root: it never moves, so its offsets are")
    L.append("// never read.")
    L.append("")
    L.append("// clang-format off")
    L.append("")

    for m in models:
        cw, chf, _ = m.tex
        ch = chf // m.frames
        ident = cpp_ident(m.name)
        L.append(f"// ---- {m.name} "
                 f"({len(m.boxes)} boxes, {len(m.quads)} quads, "
                 f"{len(m.parts)} parts, "
                 f"{m.frames} frame{'s' if m.frames != 1 else ''}) ----")
        L.append(f"inline constexpr ShapeQuad kShapeQuads{ident}[] = {{")
        for q in m.quads:
            u0, v0, u1, v1 = fmt_uv(q, m, sheet_w, sheet_h)
            pos = ", ".join(f"{{{p[0]:.6f}f, {p[1]:.6f}f, {p[2]:.6f}f}}"
                            for p in q.pos)
            uvs = ", ".join(
                f"{{{u0 + (u1 - u0) * s:.6f}f, {v0 + (v1 - v0) * t:.6f}f}}"
                for s, t in CORNER_ST)
            n = q.normal
            off = ", ".join(f"{{{o[0]:.6f}f, {o[1]:.6f}f, {o[2]:.6f}f}}"
                            for o in q.off)
            L.append(
                f"    {{ {{{pos}}}, {{{uvs}}},"
                f" {{{n[0]:.6f}f, {n[1]:.6f}f, {n[2]:.6f}f}},"
                f" {q.face}, {'true ' if q.cull else 'false'},"
                f" {{{off}}}, {q.part} }},")
        L.append("};")
        L.append(f"inline constexpr ShapePart kShapeParts{ident}[] = {{")
        for pt in m.parts:
            L.append(f'    {{ "{pt.name}", {pt.parent},'
                     f" {{{pt.pivot[0]:.6f}f, {pt.pivot[1]:.6f}f,"
                     f" {pt.pivot[2]:.6f}f}} }},")
        L.append("};")
        L.append(f"inline constexpr ShapeAabb kShapeBoxes{ident}[] = {{")
        for lo, hi in m.boxes:
            L.append(f"    {{ {{{lo[0]:.6f}f, {lo[1]:.6f}f, {lo[2]:.6f}f}},"
                     f" {{{hi[0]:.6f}f, {hi[1]:.6f}f, {hi[2]:.6f}f}} }},")
        L.append("};")
        blo = [min(b[0][i] for b in m.boxes) for i in range(3)]
        bhi = [max(b[1][i] for b in m.boxes) for i in range(3)]
        L.append(f"// Whole-model bounds -- a cheap stand-in if {len(m.boxes)} "
                 "collision boxes prove too many.")
        L.append(f"inline constexpr ShapeAabb kShapeBounds{ident} = "
                 f"{{ {{{blo[0]:.6f}f, {blo[1]:.6f}f, {blo[2]:.6f}f}},"
                 f" {{{bhi[0]:.6f}f, {bhi[1]:.6f}f, {bhi[2]:.6f}f}} }};")
        L.append(f"inline constexpr ShapeAnim kShapeAnim{ident} = "
                 f"{{ {m.frames}, {m.frame_time}, "
                 f"{ch / sheet_h:.6f}f }};   // += vStride*frame")
        L.append("")

    L.append("// clang-format on")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text("\n".join(L) + "\n", encoding="utf-8")


# --- main --------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("models", nargs="+", type=Path, help=".bbmodel file(s)")
    ap.add_argument("--out-png", type=Path,
                    default=ROOT / "game" / "assets" / "shapes.png")
    ap.add_argument("--out-header", type=Path,
                    default=ROOT / "game" / "include" / "game" / "generated" /
                            "BlockShapes.inl")
    ap.add_argument("--sheet-width", type=int, default=1024)
    ap.add_argument("--no-cull", action="store_true",
                    help="keep faces sealed inside the model (for comparison)")
    args = ap.parse_args()

    models = []
    for path in args.models:
        print(f"{path}")
        try:
            m = load_model(path)
        except (ValueError, KeyError) as e:
            print(f"  FAILED: {e}")
            return 1
        if not args.no_cull:
            cull_interior(m)
        models.append(m)

        kept, raw = len(m.quads), m.raw_quads
        cw, chf, _ = m.tex
        print(f"  {len(m.boxes)} boxes, {raw} faces -> {kept} quads "
              f"({raw - kept} sealed inside, {100.0 * (raw - kept) / raw:.0f}%)")
        print(f"  vertices/block: {raw * 6} raw -> {kept * 6} baked "
              f"({kept * 6 * SHAPED_VERTEX_FLOATS * 4 / 1024.0:.1f} KB of chunk "
              f"mesh at {SHAPED_VERTEX_FLOATS} floats)")
        print(f"  texture: {cw}x{chf // m.frames} used"
              f"{f' x {m.frames} frames' if m.frames > 1 else ''}"
              f"{f' (frame_time {m.frame_time})' if m.frames > 1 else ''}")
        mirrored = sum(1 for q in m.quads if q.uv[2] < q.uv[0] or q.uv[3] < q.uv[1])
        print(f"  {mirrored} of {kept} quads mirrored (box-UV); "
              f"{sum(1 for q in m.quads if q.cull)} flush with a cell wall")

    sheet_w, sheet_h, sheet = pack(models, args.sheet_width)
    write_png(args.out_png, sheet_w, sheet_h, sheet)
    print(f"wrote {args.out_png} ({sheet_w}x{sheet_h})")

    emit(models, sheet_w, sheet_h, args.out_header,
         "python tools/bbmodel_to_shape.py " +
         " ".join(str(p) for p in args.models))
    print(f"wrote {args.out_header}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
