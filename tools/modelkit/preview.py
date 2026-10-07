"""Preview + check a block shape or creature .bbmodel without the game.

    python tools/modelkit/preview.py models/mortar.bbmodel
    python tools/modelkit/preview.py game/assets/models/void_warden.bbmodel --anim attack
    python tools/modelkit/preview.py models/x.bbmodel --out some/dir --frames 16

Writes, next to nothing in the repo (default: a `modelkit_preview/` folder in
the system temp dir, or --out):
  <stem>_views.png   rest pose from four sides (front = the model's -Z face)
  <stem>_<anim>.png  a strip of animation frames, front-left view
  <stem>_<anim>.gif  the same animation, all four views, looping
and prints the checks:
  blocks     quads + KB of chunk mesh (the bake's own numbers), and whether any
             animated part leaves the cell at ANY sampled moment -- which the
             bake cannot see, since it only checks the rest pose
  creatures  bone count (the engine's cap is 32), the idle/walk clips the game
             requires, and an attack clip if the species telegraphs a swing

A block's motion comes from the real kPartAnims rows in BlockShape.h, so an
existing model previews exactly as it moves in game; a spec can pass its own.
"""
import argparse, json, math, os, sys, tempfile
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from render import BlockShape, Creature, rasterize, fit, VIEWS  # noqa: E402

LABEL_H = 18


def views_image(mesh, center, scale, size, title=""):
    tiles = [Image.fromarray(rasterize(mesh, y, p, center, scale, size)) for y, p, _ in VIEWS]
    W = Image.new("RGBA", (len(tiles) * (size + 4), size + LABEL_H), (20, 20, 24, 255))
    d = ImageDraw.Draw(W)
    for i, (im, (_, _, lbl)) in enumerate(zip(tiles, VIEWS)):
        W.paste(im, (i * (size + 4), 0))
        d.text((i * (size + 4) + 4, size + 3), f"{lbl} {title}", fill=(210, 210, 210, 255))
    return W


def strip_image(meshes, center, scale, size, labels):
    yaw, pitch, _ = VIEWS[0]
    W = Image.new("RGBA", (len(meshes) * (size + 4), size + LABEL_H), (20, 20, 24, 255))
    d = ImageDraw.Draw(W)
    for i, m in enumerate(meshes):
        W.paste(Image.fromarray(rasterize(m, yaw, pitch, center, scale, size)), (i * (size + 4), 0))
        d.text((i * (size + 4) + 4, size + 3), labels[i], fill=(210, 210, 210, 255))
    return W


def save_gif(meshes, center, scale, size, path, seconds):
    frames = [views_image(m, center, scale, size).convert("P", palette=Image.ADAPTIVE) for m in meshes]
    frames[0].save(path, save_all=True, append_images=frames[1:], loop=0,
                   duration=max(20, int(1000 * seconds / len(frames))))


def is_block(path):
    fmt = json.loads(Path(path).read_text(encoding="utf-8")).get("meta", {}).get("model_format")
    return fmt == "java_block"


def preview_block(path, out, nframes, size, part_anims=None):
    b = BlockShape(path, part_anims)
    stem = Path(path).stem
    print(f"{stem}: block, {len(b.model.boxes)} boxes, {b.quads} quads, {b.kb:.1f} KB of chunk mesh"
          f"{f', {b.model.frames}-frame texture strip' if b.model.frames > 1 else ''}")
    parts = [p.name for p in b.model.parts[1:]]
    print(f"  parts: {', '.join(parts) if parts else '(none)'}")
    for line in b.bake_log.splitlines():
        if line.strip().startswith("!"):
            print("  bake:" + line.strip()[1:])
    rest = b.mesh(animate=False)
    issues = []

    if not b.anims:
        print("  no kPartAnims rows: nothing moves")
        c, s = fit([rest])
        views_image(rest, c, s, size, "(rest)").save(out / f"{stem}_views.png")
        return issues

    cranked = any(a.get("cranked") for a in b.anims)
    clock_rates = [a["rate"] for a in b.anims if not a.get("cranked") and a["rate"] > 0]
    # One full cycle of the slowest motion: a turn of the handle, or the
    # longest clock period (capped so a slow drift still previews).
    span = 1.0 if cranked else min(4.0, max(1.0 / r for r in clock_rates)) if clock_rates else 1.0
    unit = "turn" if cranked else "s"
    for a in b.anims:
        print(f"  moves: {a['part']} {a['motion']} axis {a['axis']} rate {a['rate']:g}/"
              f"{'turn' if a.get('cranked') else 's'} amount {a['amount']:g}")

    ts = [span * i / nframes for i in range(nframes)]
    meshes = [b.mesh(t_clock=t, t_turns=t) for t in ts]
    # Bounds across the WHOLE motion: the bake only ever sees the rest pose.
    tol = bake_tol = 1.0 / 16.0
    worst = 0.0
    for t, m in zip(ts, meshes):
        p = m.pos.reshape(-1, 3)
        over = max((-p).max(), (p - 1.0).max())
        worst = max(worst, over)
    if worst > bake_tol:
        issues.append(f"an animated part reaches {worst * 16:.2f} units outside the cell "
                      "at some point in its motion (the bake allows 1)")
    else:
        print(f"  bounds: inside the cell for the whole motion (worst {max(worst, 0) * 16:.2f} units out)")

    c, s = fit([rest] + meshes)
    views_image(rest, c, s, size, "(rest)").save(out / f"{stem}_views.png")
    strip_image(meshes, c, s, size // 2 + 40, [f"{t:.2f} {unit}" for t in ts]).save(out / f"{stem}_motion.png")
    save_gif(meshes, c, s, size // 2 + 40, out / f"{stem}_motion.gif", span if not cranked else 1.2)
    return issues


def engine_crosscheck(path, bones, vertices):
    """Ask the GAME's own loader (voxel-factory --check-bbmodel, headless: no
    window, no save touched) and compare with this replica's counts, so the
    two can never drift apart unnoticed. Skipped if the game is not built."""
    import re, subprocess
    root = Path(__file__).resolve().parents[2]
    exe = next((p for p in (root / "out/build/x64-release/bin/voxel-factory.exe",
                            root / "out/build/x64-release/bin/voxel-factory",
                            root / "out/build/linux-release/bin/voxel-factory",
                            root / "out/build/mac-release/bin/voxel-factory") if p.exists()), None)
    if exe is None:
        print("  engine check: skipped (game not built)")
        return []
    r = subprocess.run([str(exe), "--check-bbmodel", str(Path(path).resolve())],
                       capture_output=True, text=True, timeout=60)
    m = re.search(r": (\d+) bones, (\d+) vertices", r.stdout)
    if r.returncode != 0 or not m:
        return [f"the game's loader rejected it:\n    " + (r.stdout + r.stderr).strip().replace("\n", "\n    ")]
    eb, ev = int(m.group(1)), int(m.group(2))
    if (eb, ev) != (bones, vertices):
        return [f"replica drift: the game sees {eb} bones / {ev} vertices, the preview {bones} / {vertices}"]
    print(f"  engine check: the game's loader agrees ({eb} bones, {ev} vertices)")
    return []


def preview_creature(path, out, nframes, size, only=None):
    cr = Creature(path)
    stem = Path(path).stem
    print(f"{stem}: creature, {len(cr.bones)} bones, {len(cr.faces)} faces, clips {list(cr.anims)}")
    issues = []
    if len(cr.bones) > 32:
        issues.append(f"{len(cr.bones)} bones; the engine's cap (kMaxEntityBones) is 32")
    for need in ("idle", "walk"):
        if not cr.find(need):
            issues.append(f"no '{need}' clip -- CreatureSystem::checkModels requires it")
    if not cr.find("attack"):
        print("  no 'attack' clip (fine unless its kSpecies row sets swingImpact > 0)")
    issues += engine_crosscheck(path, len(cr.bones), len(cr.faces) * 6)
    names = [n for n in cr.anims if only in (None, "all") or cr.find(only) == n]
    all_meshes, per = [cr.mesh()], {}
    for n in names:
        a = cr.anims[n]
        L = a["length"] or 1.0
        ts = [L * i / nframes for i in range(nframes)]
        per[n] = (ts, [cr.mesh(n, t) for t in ts], L)
        all_meshes += per[n][1]
        if a["length"] <= 0:
            issues.append(f"clip '{n}' has no length")
    c, s = fit(all_meshes)
    views_image(all_meshes[0], c, s, size, "(rest)").save(out / f"{stem}_views.png")
    for n, (ts, ms, L) in per.items():
        short = n.rsplit(".", 1)[-1]
        strip_image(ms, c, s, size // 2 + 40, [f"{t:.2f}s" for t in ts]).save(out / f"{stem}_{short}.png")
        save_gif(ms, c, s, size // 2 + 40, out / f"{stem}_{short}.gif", L)
        print(f"  clip {short}: {L:g}s, {'loops' if cr.anims[n]['loop'] else 'plays once'}")
    return issues


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("model", type=Path)
    ap.add_argument("--out", type=Path, default=Path(tempfile.gettempdir()) / "modelkit_preview")
    ap.add_argument("--anim", default="all", help="creature clip to render (default: all)")
    ap.add_argument("--frames", type=int, default=12)
    ap.add_argument("--size", type=int, default=240)
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    issues = (preview_block(a.model, a.out, a.frames, a.size) if is_block(a.model)
              else preview_creature(a.model, a.out, a.frames, a.size, a.anim))
    for i in issues:
        print(f"  PROBLEM: {i}")
    print(f"  wrote previews to {a.out}")
    return 1 if issues else 0


if __name__ == "__main__":
    sys.exit(main())
