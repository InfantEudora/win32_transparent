"""Validate the overlay shader against the maths it is supposed to implement.

Rather than asserting "there should be a soft edge somewhere", this recomputes the rounded-box
distance field on the CPU for each sampled pixel, predicts the colour, and compares. If the shader
and this agree to a couple of levels across a curved edge, then the distance field, the pixel-space
antialiasing ramp and the outline band are all correct together.

Panel: min (24,544) max (300,710), r=12, fill rgba(20,24,34,210), outline 2px rgb(90,190,255).
"""
import math, sys
from PIL import Image

im = Image.open(sys.argv[1]).convert("RGB")
px = im.load()

X0, Y0, X1, Y1 = 24.0, 544.0, 300.0, 710.0
R = 12.0
FILL, FILL_A = (20, 24, 34), 210 / 255.0
LINE, LINE_HW = (90, 190, 255), 1.0          # half-width: AddRectOutline passes thickness*0.5
BG = (0, 0, 0)

cx, cy = (X0 + X1) / 2, (Y0 + Y1) / 2
hx, hy = (X1 - X0) / 2, (Y1 - Y0) / 2

def round_box(px_, py_):
    qx = abs(px_ - cx) - hx + R
    qy = abs(py_ - cy) - hy + R
    return min(max(qx, qy), 0.0) + math.hypot(max(qx, 0.0), max(qy, 0.0)) - R

def coverage(d):
    return min(max(0.5 - d, 0.0), 1.0)

def predict(ix, iy):
    """Fill quad then outline quad, straight alpha over the background - draw order matters."""
    x, y = ix + 0.5, iy + 0.5      # pixel centre, which is where GL samples
    d = round_box(x, y)
    out = list(BG)
    a = coverage(d) * FILL_A
    for i in range(3):
        out[i] = FILL[i] * a + out[i] * (1 - a)
    a = coverage(abs(d) - LINE_HW)
    for i in range(3):
        out[i] = LINE[i] * a + out[i] * (1 - a)
    return tuple(int(round(v)) for v in out)

fail = []
def check(name, cond, detail=""):
    print(("  OK   " if cond else "  FAIL ") + name + ("  " + detail if detail else ""))
    if not cond:
        fail.append(name)

# Walk the diagonal through the top-left rounded corner. This crosses the curve at 45 degrees, so
# it cannot land on the ramp endpoints the way an axis-aligned edge does, and every kind of pixel
# (outside / partial / outline / partial / fill) appears along it.
print("diagonal across the top-left rounded corner, predicted vs measured:")
worst = 0
partials = 0
for k in range(0, 22):
    ix, iy = int(X0) + k, int(Y0) + k
    got, want = px[ix, iy], predict(ix, iy)
    err = max(abs(g - w) for g, w in zip(got, want))
    worst = max(worst, err)
    d = round_box(ix + 0.5, iy + 0.5)
    # A pixel counts as partial if EITHER term is fractionally covered - the outline band has
    # its own edges, and ignoring them undercounts the very thing being measured.
    cov_fill, cov_line = coverage(d), coverage(abs(d) - LINE_HW)
    if (0.02 < cov_fill < 0.98) or (0.02 < cov_line < 0.98):
        partials += 1
    flag = "" if err <= 2 else "   <-- off by %d" % err
    print("    (%3d,%3d) d=%+6.2f  got %-16s want %-16s%s" % (ix, iy, d, str(got), str(want), flag))

check("shader matches the CPU distance field along a curve", worst <= 2,
      "worst channel error %d" % worst)
check("the curve is antialiased (partially covered pixels exist)", partials >= 2,
      "%d partial pixels on the diagonal" % partials)

# Roundness: at the very corner of the bounding box the shape must be absent. Far enough out that
# neither fill nor outline reaches - d there is ~5px, well past the 0.5px ramp and the 1px band.
d_corner = round_box(X0 + 0.5, Y0 + 0.5)
check("bbox corner is outside the rounded shape", d_corner > 2.0,
      "d=%.2f at the bbox corner, so a square would have filled it" % d_corner)
check("bbox corner really is background", max(px[int(X0), int(Y0)]) <= 2,
      "%s" % (px[int(X0), int(Y0)],))

# ...while the same offset into a radius-0 rect IS filled. That rect is the first of the sweep,
# (40,630)-(90,670), drawn white with radius 0 - the control case proving the corner above is
# missing because of the radius and not because the overlay is offset.
sq = px[41, 631]
check("a radius-0 rect DOES fill its bbox corner", min(sq) > 200, "%s at (41,631)" % (sq,))

print("\n%s" % ("ALL CHECKS PASSED" if not fail else "FAILED: " + ", ".join(fail)))
sys.exit(1 if fail else 0)
