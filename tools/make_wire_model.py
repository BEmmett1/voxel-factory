"""Generate the placeholder Wire block-shape model.

    python tools/make_wire_model.py

Writes `models/wire_hub.bbmodel`: a small copper node with six stub arms, one
per face, each its own named outliner group. **The group names are the
contract** -- `kConnectParts` in game/include/game/BlockShape.h names
`arm_north`/`arm_south`/`arm_west`/`arm_east`/`arm_up`/`arm_down`, and the
mesher shows each one only when that neighbour is a power node. A hand-authored
replacement may look like anything at all as long as it keeps those six names,
one group per direction, with the geometry for a direction inside the group
that names it.

Deliberately near the bottom of the quad budget (AUTHORING.md): wire is placed
in long runs, so it gets the crop's treatment, not a machine's. A straight run
is the core plus two arms.

Sibling of make_crop_models.py: pure stdlib, overwrites hand edits, and the
generated half should be DELETED once a real model replaces it (the
make_tempest_model.py lesson -- otherwise the next run quietly reinstates the
placeholder the game no longer loads).
"""

import base64
import importlib.util
import json
from pathlib import Path

_spec = importlib.util.spec_from_file_location(
    "bbmodel_to_shape", Path(__file__).with_name("bbmodel_to_shape.py"))
_bake = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_bake)

OUT = Path(__file__).resolve().parent.parent / "models" / "wire_hub.bbmodel"

TEX = 16                      # texture is 16x16: one frame, two 4x4 patches
CORE_UV = (0, 0, 4, 4)        # bright copper, for the centre node
ARM_UV = (0, 4, 4, 8)         # a shade darker, so an arm reads as separate

# Copper, matching kBlocks' Wire colour {0.82, 0.72, 0.20} closely enough that
# the generated-atlas fallback and the model agree.
CORE_LIT = (214, 176, 60, 255)
CORE_DIM = (168, 132, 40, 255)
ARM_LIT = (176, 140, 46, 255)
ARM_DIM = (132, 102, 32, 255)

# The centre node, then one stub per face. Each stub is 2x2 in cross-section and
# runs from the node's face to the cell wall, so two opposite stubs plus the
# node read as one continuous wire through the cell.
CORE = (6, 6, 6, 10, 10, 10)
ARMS = [
    # name,        from,             to,               pivot (at the wall)
    ("arm_north", (7, 7, 0), (9, 9, 6), (8, 8, 0)),    # -Z
    ("arm_south", (7, 7, 10), (9, 9, 16), (8, 8, 16)),  # +Z
    ("arm_west", (0, 7, 7), (6, 9, 9), (0, 8, 8)),     # -X
    ("arm_east", (10, 7, 7), (16, 9, 9), (16, 8, 8)),   # +X
    ("arm_down", (7, 0, 7), (9, 6, 9), (8, 0, 8)),     # -Y
    ("arm_up", (7, 10, 7), (9, 16, 9), (8, 16, 8)),    # +Y
]

FACES = ["north", "east", "south", "west", "up", "down"]


def uid(n):
    """Stable pseudo-uuids: a regenerated file must diff empty."""
    h = f"{n:012x}"
    return f"{h[:8]}-{h[8:12]}-4000-8000-{h[:12]}"


def texture():
    px = bytearray(TEX * TEX * 4)

    def patch(x0, y0, x1, y1, lit, dim):
        for y in range(y0, y1):
            for x in range(x0, x1):
                # A one-pixel dim border reads as a bevel at play distance;
                # detail on a bulk block belongs in the texture, not in boxes.
                edge = x in (x0, x1 - 1) or y in (y0, y1 - 1)
                px[(y * TEX + x) * 4:(y * TEX + x) * 4 + 4] = bytes(dim if edge else lit)

    patch(*CORE_UV, CORE_LIT, CORE_DIM)
    patch(*ARM_UV, ARM_LIT, ARM_DIM)
    return px


def element(name, frm, to, uv, n):
    return {
        "name": name,
        "box_uv": False,
        "type": "cube",
        "uuid": uid(n),
        "from": list(frm),
        "to": list(to),
        "origin": list(frm),
        "faces": {f: {"uv": list(uv), "texture": 0} for f in FACES},
    }


def main():
    elements = [element("core", CORE[:3], CORE[3:], CORE_UV, 1)]
    outliner = [{
        "name": "core", "uuid": uid(101), "origin": [8, 8, 8],
        "children": [uid(1)],
    }]
    for i, (name, frm, to, pivot) in enumerate(ARMS):
        elements.append(element(name, frm, to, ARM_UV, i + 2))
        outliner.append({
            "name": name, "uuid": uid(102 + i), "origin": list(pivot),
            "children": [uid(i + 2)],
        })

    doc = {
        "meta": {"format_version": "4.5", "model_format": "java_block",
                 "box_uv": False},
        "name": "wire_hub",
        "parent": "",
        "ambientocclusion": True,
        "resolution": {"width": TEX, "height": TEX},
        "elements": elements,
        "outliner": outliner,
        "textures": [{
            "name": "wire", "folder": "block", "namespace": "", "id": "0",
            "particle": True, "render_mode": "normal", "visible": True,
            "internal": True, "uuid": uid(200),
            "uv_width": TEX, "uv_height": TEX,
            "source": "data:image/png;base64," + base64.b64encode(
                _bake.encode_png(TEX, TEX, texture())).decode("ascii"),
        }],
    }
    OUT.write_text(json.dumps(doc, separators=(",", ":")), encoding="utf-8")
    print(f"wrote {OUT} ({len(elements)} elements, {TEX}x{TEX} texture, 1 frame)")
    print("groups: " + ", ".join(["core"] + [a[0] for a in ARMS]))


if __name__ == "__main__":
    main()
