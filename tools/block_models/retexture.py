"""Retexture an existing Blockbench model in place, keeping its geometry.

Every opaque texel is CLASSIFIED by what its original colour says it depicts
(stone, glass, glow, leaf...) and repainted from a material swatch -- or, for a
glow, recoloured -- scaled by the texel's original lightness relative to the
average of its class. So the model keeps all the shading and edge cues its
author painted, and only the material and colour change. Works on box-UV and
per-face layouts alike, because it never needs to know which face a texel is
on, and on animation strips, because the swatch is sampled at y modulo the
frame height so every frame is repainted the same way.

    from retexture import retexture, hsl
    retexture("models/x.bbmodel", classify, materials, glows)

`classify(r, g, b) -> key` returns a material key, a glow key, or None to keep
the texel as it is. `materials[key]` is an RGBA PIL image (tiled); `glows[key]`
is an (h, s) target hue/saturation in 0..1 that keeps the texel's lightness.
Needs Pillow.
"""
import base64, colorsys, io, json
from PIL import Image


def hsl(r, g, b):
    h, l, s = colorsys.rgb_to_hls(r / 255, g / 255, b / 255)
    return h, s, l


def _lum(r, g, b):
    return 0.299 * r + 0.587 * g + 0.114 * b


def retexture(path, classify, materials, glows=None, contrast=1.0, out=None):
    glows = glows or {}
    raw = open(path, encoding="utf-8").read()
    d = json.loads(raw)
    tex = d["textures"][0]
    src = tex["source"]
    im = Image.open(io.BytesIO(base64.b64decode(src.split(",", 1)[1]))).convert("RGBA")
    W, H = im.size
    frame_h = int(tex.get("uv_height") or H)
    p = im.load()

    # Pass 1: classify, and each class's mean lightness -- the reference the
    # shading is measured against.
    cls = {}
    sums = {}
    for y in range(H):
        for x in range(W):
            r, g, b, a = p[x, y]
            if a == 0:
                continue
            k = classify(r, g, b)
            if k is None:
                continue
            cls[(x, y)] = k
            s = sums.setdefault(k, [0.0, 0])
            s[0] += _lum(r, g, b); s[1] += 1
    mean = {k: (v[0] / v[1] if v[1] else 1.0) for k, v in sums.items()}

    # Material means, so a swatch texel at the class mean comes out at the
    # swatch's own average brightness.
    mats = {}
    for k, m in materials.items():
        m = m.convert("RGBA")
        px = [q for q in m.getdata() if q[3]]
        mats[k] = (m, m.load(), sum(_lum(*q[:3]) for q in px) / max(1, len(px)))

    out_im = im.copy(); q = out_im.load()
    for (x, y), k in cls.items():
        r, g, b, a = p[x, y]
        rel = (_lum(r, g, b) / mean[k]) if mean[k] > 0 else 1.0
        rel = 1.0 + (rel - 1.0) * contrast
        if k in glows:
            th, ts = glows[k]
            _, _, l = hsl(r, g, b)
            nr, ng, nb = colorsys.hls_to_rgb(th, l, ts)
            q[x, y] = (int(nr * 255), int(ng * 255), int(nb * 255), a)
            continue
        m, mp, _ = mats[k]
        sr, sg, sb, _ = mp[x % m.width, (y % frame_h) % m.height]
        q[x, y] = (max(0, min(255, int(sr * rel))), max(0, min(255, int(sg * rel))),
                   max(0, min(255, int(sb * rel))), a)

    buf = io.BytesIO(); out_im.save(buf, "PNG")
    new = "data:image/png;base64," + base64.b64encode(buf.getvalue()).decode()
    assert raw.count(src) == 1
    open(out or path, "w", encoding="utf-8", newline="").write(raw.replace(src, new))
    counts = {k: sums[k][1] for k in sums}
    return out_im, counts
