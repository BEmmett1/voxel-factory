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

What it rejects, loudly: rotated elements. A rotated box is not an AABB, and
the shape table backs collision and raycasts as well as the mesh -- supporting
it means splitting render geometry from collision geometry, which is a design
decision, not a script fix.
"""

import argparse
import base64
import json
import math
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

# Face order matches ChunkMesher's kFaces: +X, -X, +Y, -Y, +Z, -Z.
FACE_ORDER = ["east", "west", "up", "down", "south", "north"]

# How each face's UV rect parameterizes its quad, as (axis, direction) pairs for
# the u and v texture axes; axis 0/1/2 = X/Y/Z. This is Minecraft's convention:
# each face is textured as seen head-on from OUTSIDE the box, with texture v
# running DOWN. Blockbench's +Z is Minecraft's "south".
#
# ---- This table is the handedness knob. ----
# If a baked model comes out mirrored or upside down on one axis, the fix is a
# sign here, not in the mesher -- exactly like BbModel.cpp funnelling all unit
# conversion through geoToWorld. It is emitted into the .inl so the C++ mesher
# reads the same numbers rather than keeping a second copy.
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

class Quad:
    """One textured face of one box, in cell-local space."""

    def __init__(self, lo, hi, face, uv, cull):
        self.lo = lo          # (x, y, z) in 0..1
        self.hi = hi
        self.face = face      # index into FACE_ORDER
        self.uv = uv          # [u0, v0, u1, v1] in model uv-space px, sign kept
        self.cull = cull      # face is flush with a cell wall


class Model:
    def __init__(self, name):
        self.name = name
        self.quads = []
        self.boxes = []       # cell-local AABBs, for collision/rays
        self.frames = 1
        self.frame_time = 0
        self.tex = None       # (w, h, rgba) cropped, frames stacked
        self.crop = None      # (x, y, w, h) in texture px, of one frame
        self.region = None    # (x, y) placement in the sheet
        self.raw_quads = 0    # before interior culling


def load_model(path):
    doc = json.loads(Path(path).read_text(encoding="utf-8"))
    name = path.stem
    fmt = doc.get("meta", {}).get("model_format", "?")
    if fmt not in ("java_block", "free", "bedrock", "bedrock_old"):
        print(f"  ! {name}: unfamiliar model_format '{fmt}' -- baking anyway")

    model = Model(name)

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

    rotated, meshes, blank = [], 0, 0
    for el in doc.get("elements", []):
        if el.get("type", "cube") != "cube":
            meshes += 1
            continue
        if el.get("rotation"):
            rotated.append(el.get("name", "?"))
            continue
        if el.get("visible") is False:
            continue

        frm = [float(v) for v in el["from"]]
        to = [float(v) for v in el["to"]]
        inflate = float(el.get("inflate", 0.0))
        lo = [min(frm[i], to[i]) - inflate for i in range(3)]
        hi = [max(frm[i], to[i]) + inflate for i in range(3)]
        if any(hi[i] - lo[i] <= 0.0 for i in range(3)):
            blank += 1
            continue

        lo_c = tuple(v / MODEL_UNITS for v in lo)
        hi_c = tuple(v / MODEL_UNITS for v in hi)
        model.boxes.append((lo_c, hi_c))

        for fi, fname in enumerate(FACE_ORDER):
            face = el.get("faces", {}).get(fname)
            if not face or face.get("texture") is None:
                continue
            uv = [float(v) for v in face["uv"]]
            if uv[0] == uv[2] or uv[1] == uv[3]:
                blank += 1
                continue
            if face.get("rotation"):
                # Face-level UV rotation needs a corner permutation, not a rect.
                print(f"  ! {name}: face rotation on '{el.get('name')}'.{fname} "
                      "ignored (author without face UV rotation)")
            axis = fi // 2
            outward_hi = (fi % 2) == 0
            wall = hi[axis] >= MODEL_UNITS - 1e-4 if outward_hi else lo[axis] <= 1e-4
            model.quads.append(Quad(lo_c, hi_c, fi, uv, wall))

    if rotated:
        raise ValueError(
            f"{name}: {len(rotated)} rotated element(s) -- {', '.join(rotated[:4])}"
            f"{' ...' if len(rotated) > 4 else ''}\n"
            "    A rotated box is not an AABB, and this table also backs collision\n"
            "    and raycasts. Either un-rotate them in Blockbench, or decide to\n"
            "    split render geometry from collision geometry first.")
    if meshes:
        print(f"  ! {name}: skipped {meshes} non-cube (mesh) element(s)")
    if blank:
        print(f"  ! {name}: skipped {blank} zero-area box/face(s)")
    if not model.quads:
        raise ValueError(f"{name}: nothing to bake")

    # Geometry must stay inside its own cell. The engine's collision and ray
    # queries iterate the cells an AABB overlaps and test only THAT cell's
    # boxes (see Collision.h), so a box hanging into a neighbour would render
    # correctly and then be silently missed by physics -- the worst kind of
    # bug to chase. Minecraft allows from/to outside 0..16, so this is a real
    # thing a model can do, not a theoretical one.
    outside = [(lo, hi) for lo, hi in model.boxes
               if any(lo[i] < -1e-6 or hi[i] > 1.0 + 1e-6 for i in range(3))]
    if outside:
        lo, hi = outside[0]
        raise ValueError(
            f"{name}: {len(outside)} box(es) reach outside the 0..16 cell, "
            f"e.g. {tuple(round(v * MODEL_UNITS, 2) for v in lo)}..."
            f"{tuple(round(v * MODEL_UNITS, 2) for v in hi)}\n"
            "    Collision only tests a cell's own boxes, so overhanging\n"
            "    geometry would draw but not collide. Keep the model inside\n"
            "    the cell, or make it a multiblock.")

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
    def covered(q):
        axis = q.face // 2
        outward_hi = (q.face % 2) == 0
        plane = q.hi[axis] if outward_hi else q.lo[axis]
        others = [a for a in (0, 1, 2) if a != axis]
        for lo, hi in model.boxes:
            touch = lo[axis] if outward_hi else hi[axis]
            if abs(touch - plane) > eps:
                continue
            if all(lo[a] <= q.lo[a] + eps and hi[a] >= q.hi[a] - eps for a in others):
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
    L.append("// `uv` is {u0,v0,u1,v1} in absolute sheet UV, and u0 > u1 (or")
    L.append("// v0 > v1) means the face is MIRRORED -- interpolate with mix()")
    L.append("// and the sign takes care of itself; do not sort the rect.")
    L.append("//")
    L.append("// kShapeFaceAxes is the corner parameterization the bake used --")
    L.append("// for face f, position along axis uAxis runs with parameter s")
    L.append("// (reversed when uDir < 0) and vAxis with t, while uv runs")
    L.append("// mix(u0,u1,s), mix(v0,v1,t). Read it here rather than keeping a")
    L.append("// second copy in the mesher: if a baked model comes out mirrored")
    L.append("// or upside down, the fix is a sign in the BAKE, not in the C++.")
    L.append("")
    L.append("// clang-format off")
    L.append("")
    L.append("// { uAxis, uDir, vAxis, vDir } per face, in kFaces order "
             "(+X,-X,+Y,-Y,+Z,-Z).")
    L.append("inline constexpr int kShapeFaceAxes[6][4] = {")
    for fname in FACE_ORDER:
        ua, ud, va, vd = FACE_AXES[fname]
        L.append(f"    {{ {ua}, {ud:+d}, {va}, {vd:+d} }},   // {fname}")
    L.append("};")
    L.append("")

    for m in models:
        cw, chf, _ = m.tex
        ch = chf // m.frames
        ident = "".join(p.capitalize() for p in m.name.replace("-", "_").split("_"))
        L.append(f"// ---- {m.name} "
                 f"({len(m.boxes)} boxes, {len(m.quads)} quads, "
                 f"{m.frames} frame{'s' if m.frames != 1 else ''}) ----")
        L.append(f"inline constexpr ShapeQuad kShapeQuads{ident}[] = {{")
        for q in m.quads:
            u0, v0, u1, v1 = fmt_uv(q, m, sheet_w, sheet_h)
            L.append(
                f"    {{ {{{q.lo[0]:.6f}f, {q.lo[1]:.6f}f, {q.lo[2]:.6f}f}},"
                f" {{{q.hi[0]:.6f}f, {q.hi[1]:.6f}f, {q.hi[2]:.6f}f}},"
                f" {q.face}, {'true ' if q.cull else 'false'},"
                f" {{{u0:.6f}f, {v0:.6f}f, {u1:.6f}f, {v1:.6f}f}} }},")
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
              f"({kept * 6 * 9 * 4 / 1024.0:.1f} KB of chunk mesh at 9 floats)")
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
