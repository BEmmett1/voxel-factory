"""Crop an accidental animation strip out of a Blockbench shape model.

    python tools/normalize_shape_texture.py models/conduit_hub.bbmodel [...]

bbmodel_to_shape.py auto-detects a texture a whole multiple of `uv_height` tall
as a Minecraft animation strip and bakes it with a `vStride`, stacking every
frame into shapes.png. That is right for a machine authored as an animation and
wrong for everything else: a model generator that emits an 8-frame strip by
habit costs 8x the sheet region for frames that will never play, since animation
is gated on POWER (see AUTHORING.md rule 10) and a block that is not a power
node parks on frame 0 forever.

This keeps frame 0 and nothing else -- it rewrites the embedded PNG in place and
drops the `frame_*` keys, so the file stays a valid Blockbench project you can
reopen and keep editing. Idempotent: a texture already one frame tall is left
untouched, so it is safe to re-run over a whole folder.

Pure stdlib, and it borrows the bake's own PNG codec rather than carrying a
second one, so a decoder fix lands in both at once.
"""

import argparse
import base64
import importlib.util
import json
import sys
from pathlib import Path

_spec = importlib.util.spec_from_file_location(
    "bbmodel_to_shape", Path(__file__).with_name("bbmodel_to_shape.py"))
_bake = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_bake)

DATA_URI = "data:image/png;base64,"

# Blockbench's animation settings. Left behind, they would make the file look
# animated in the editor even though the strip is gone.
FRAME_KEYS = ("frame_time", "frame_order", "frame_order_type", "frame_interpolate")


def normalize_texture(tex, index, name):
    """Crop tex to its first frame. Returns a one-line report, or None."""
    src = tex.get("source", "")
    if not src.startswith(DATA_URI):
        print(f"! {name}: texture {index} is not embedded -- skipped "
              "(re-save from Blockbench with the texture embedded)")
        return None

    png = base64.b64decode(src[len(DATA_URI):])
    w, h, rgba = _bake.read_png(png)

    # uv_height is the height of ONE frame; the bake reads the same fields.
    uv_h = int(tex.get("uv_height") or h)
    uv_w = int(tex.get("uv_width") or w)
    if uv_h <= 0 or h <= uv_h:
        return None                      # already a single frame
    if h % uv_h:
        print(f"! {name}: texture {index} is {h}px tall, not a whole multiple "
              f"of uv_height {uv_h} -- left alone")
        return None

    frames = h // uv_h
    cropped = rgba[:w * uv_h * 4]
    tex["source"] = DATA_URI + base64.b64encode(
        _bake.encode_png(w, uv_h, cropped)).decode("ascii")
    for k in FRAME_KEYS:
        tex.pop(k, None)
    return (f"  texture {index}: {w}x{h} ({frames} frames) -> {w}x{uv_h} "
            f"(1 frame), uv {uv_w}x{uv_h}")


def normalize(path):
    doc = json.loads(path.read_text(encoding="utf-8"))
    reports = [r for i, t in enumerate(doc.get("textures", []))
               if (r := normalize_texture(t, i, path.name))]
    if not reports:
        print(f"{path.name}: already one frame per texture, unchanged")
        return False
    # Blockbench writes compact JSON; match it so the diff is the texture alone.
    path.write_text(json.dumps(doc, separators=(",", ":")), encoding="utf-8")
    print(path.name)
    for r in reports:
        print(r)
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("models", nargs="+", type=Path, help="one or more .bbmodel files")
    args = ap.parse_args()

    changed = 0
    for p in args.models:
        if not p.exists():
            print(f"! {p}: no such file")
            return 1
        changed += normalize(p)
    print(f"\n{changed} file(s) rewritten. Re-bake to pick the change up.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
