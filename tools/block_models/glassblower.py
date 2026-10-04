"""The Glassblower (powered tier), built with modelkit.

A small round furnace of firebrick -- two crossed boxes and a dome, so it
reads round -- with a glowing circular port painted on the front (a decal),
an iron arm on a post at the back holding a blowpipe and a molten gather out
over the port, and a cooling rack of vials on the west side. The arm (beam,
pipe and gather) is the `arm` group, pivoting at its shoulder on the post,
and swings slowly to and fro while the glassblower is powered.

    python tools/block_models/glassblower.py          # writes + previews
"""
import math, os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from PIL import Image, ImageEnhance
from modelkit import Model, material

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def tint(img, k, b=(0, 0, 0)):
    rgb = img.convert("RGB").split()
    return Image.merge("RGBA", (*[c.point(lambda v, kk=kk, bb=bb: max(0, min(255, int(v * kk + bb))))
                                  for c, kk, bb in zip(rgb, k, b)], img.getchannel("A")))


brick = tint(material("pixellab_sandstone"), (0.72, 0.46, 0.4))


def port(w=6, h=8):
    """The furnace front: firebrick with a round glowing port."""
    im = brick.crop((0, 0, w, h)).copy(); px = im.load()
    cx, cy = w / 2, 3.5
    for y in range(h):
        for x in range(w):
            d = math.hypot(x + 0.5 - cx, y + 0.5 - cy)
            if d < 1.1:   px[x, y] = (255, 236, 160, 255)
            elif d < 2.0: px[x, y] = (255, 150, 52, 255)
            elif d < 2.6: px[x, y] = (46, 32, 30, 255)
    return im


iron = material("pixellab_wrought_iron").convert("RGBA")
glass = material("pixellab_glass")
m = Model("glassblower", kind="block")
m.material("brick", brick)
m.material("dark_brick", ImageEnhance.Brightness(brick).enhance(0.7))
m.material("iron", ImageEnhance.Brightness(iron).enhance(1.45))
m.material("brass", material("pixellab_brass"))
m.material("wood", material("pixellab_worn_wood_32"))
m.material("glass", glass)
m.material("hot_glass", tint(glass.convert("L").convert("RGBA"), (0.9, 0.55, 0.25), (110, 60, 10)))
m.material("port", port(), decal=True)

m.box("plinth", (2.5, 0, 3), (11.5, 1, 13), "dark_brick", faces={"down": None})
m.box("body_a", (3, 1, 4.5), (11, 9, 11.5), "brick")
m.box("body_b", (4, 1, 3.5), (10, 9, 12.5), "brick", faces={"north": "port"})
m.box("dome", (4.5, 9, 5), (9.5, 10.75, 11), "dark_brick")
m.box("post", (12, 0, 11), (13.5, 13, 12.5), "iron")
# The cooling rack, west side.
m.box("rack_post_n", (0.5, 0, 5), (1.25, 7, 5.75), "wood")
m.box("rack_post_s", (0.5, 0, 10.25), (1.25, 7, 11), "wood")
m.box("shelf_low", (0.5, 3, 5), (2.75, 3.75, 11), "wood")
m.box("shelf_high", (0.5, 6, 5), (2.75, 6.75, 11), "wood")
m.box("vial_a", (1.25, 3.75, 6.25), (2.25, 5.5, 7.25), "glass")
m.box("vial_b", (1.25, 3.75, 8.75), (2.25, 5.5, 9.75), "glass")

SHOULDER = (12.75, 12.75, 11.75)
m.group("arm", pivot=SHOULDER)
m.box("beam", (7.5, 12.25, 11), (13.5, 13.25, 12.5), "iron", group="arm")
m.box("pipe", (7.5, 11.5, 1.5), (8.5, 12.25, 12), "brass", group="arm")
m.box("gather", (7, 10.75, 1.25), (9, 12.5, 3.25), "hot_glass", group="arm")

m.move("arm", "Rock", axis=(0, 1, 0), rate=0.25, amount=12.0)

if __name__ == "__main__":
    print(m.cpp_part_anims("Glassblower"))
    m.preview(os.path.join(REPO, "models", "glassblower.bbmodel"))
