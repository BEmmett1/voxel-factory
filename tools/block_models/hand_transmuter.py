"""The Hand Transmuter (hand-cranked tier), built with modelkit.

A thick stone slab with a circle and runes carved into its top (painted: the
terrain's slate with a violet-inlaid ring and four rune marks), a small dull
crystal held over the centre on a bent iron arm, and a hand wheel on the
slab's front. The wheel is the `crank` group and turns about Z once per turn.

    python tools/block_models/hand_transmuter.py          # writes + previews
"""
import math, os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from PIL import ImageEnhance
from modelkit import Model, material, atlas_tile

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def carved_top():
    """The slate tile at 14x14 (the slab top, one texel per unit) with a
    violet ring and four rune notches at the compass points."""
    st = atlas_tile(3).convert("RGBA").resize((14, 14))
    px = st.load()
    inlay, deep = (150, 108, 178, 255), (96, 70, 118, 255)
    for y in range(14):
        for x in range(14):
            d = math.hypot(x + 0.5 - 7, y + 0.5 - 7)
            if 4.6 <= d <= 5.6:
                px[x, y] = inlay if (x + y) % 3 else deep
    for x, y in ((6, 0), (7, 0), (6, 13), (7, 13), (0, 6), (0, 7), (13, 6), (13, 7), (6, 6), (7, 7)):
        px[x, y] = deep
    return st


m = Model("hand_transmuter", kind="block")
m.material("stone", atlas_tile(3))
m.material("carved", carved_top())
m.material("iron", ImageEnhance.Brightness(material("pixellab_wrought_iron")).enhance(1.6))
m.material("crystal", material("pixellab_lilac_powder_32"))
m.material("brass", material("pixellab_brass"))

m.box("slab", (1, 0, 1), (15, 4, 15), "stone", faces={"up": "carved", "down": None})
m.box("arm_up", (12.5, 4, 7.5), (13.5, 12.5, 8.5), "iron")
m.box("arm_over", (8.5, 11.5, 7.5), (12.5, 12.5, 8.5), "iron")
m.box("clasp", (7.25, 10.5, 7.25), (8.75, 11.75, 8.75), "brass")
m.box("crystal", (6.75, 6.5, 6.75), (9.25, 10.5, 9.25), "crystal", rotate=("y", 45), origin=(8, 8, 8))

PIVOT = (8, 2.25, 0.5)
m.group("crank", pivot=PIVOT)
m.box("spoke_h", (5.75, 1.75, 0), (10.25, 2.75, 1), "brass", group="crank")
m.box("spoke_v", (7.5, 0, 0), (8.5, 4.5, 1), "brass", group="crank")

m.move("crank", "Spin", axis=(0, 0, 1), rate=1.0, cranked=True)

if __name__ == "__main__":
    print(m.cpp_part_anims("HandTransmuter"))
    m.preview(os.path.join(REPO, "models", "hand_transmuter.bbmodel"))
