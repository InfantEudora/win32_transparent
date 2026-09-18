"""Cut reusable UI sprites out of the bomber splash mockup.

The splash is a flat JPEG mockup: the buttons are painted into the artwork, over
a dark cave background, with their labels baked in. A UI framework needs the
opposite - a blank widget with an alpha channel, that can be stretched to any
size and have real text drawn over it. This recovers that.

WHY THIS WORKS AT ALL, rather than needing a paint-over by hand:

  A gradient scan along x shows the button is CONSTANT along x everywhere except
  its two end caps and the lettering between them. Plain plank therefore carries
  no information along x, only along y. So the lettered span can be rebuilt by
  interpolating between one clean column taken from each side of it, and every
  horizontal feature - the rim, the two grooves, the three plank faces and their
  bevels - comes back exactly, because each is copied along the axis it is
  constant on. Nothing is inpainted and nothing is guessed.

  An earlier attempt mirror-tiled a wider strip and dragged the end-cap bevels
  into the middle, giving a repeating chocolate-bar pattern. Hence SRC_L/SRC_R
  are pinned strictly inside the measured flat runs; widen them and that returns.

  Alpha comes from a flood fill inward from the border, not a colour threshold.
  Warmth alone separates plank from cave floor, but it also matches the brick and
  terrain that touch the outer buttons - only a fill distinguishes "outside" from
  merely "warm".

  Plank colour is then pushed a few pixels outwards underneath the feathered
  edge. The soft rim then fades plank-to-transparent instead of toward cave-blue,
  which is what stops the dark halo when the sprite is drawn over light UI.

RUN IT WITH THE WINDOWS PYTHON, not MSYS's - only that one has Pillow. See the
"Two pythons" note in CLAUDE.md.

    python tools/ui_extract_splash.py

Coordinates below are measured, not eyeballed; tools/ui_extract_probe.py is the
scan that produced them, and is the thing to re-run if the mockup is ever
redrawn.
"""
import os
import sys
from PIL import Image, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPLASH = os.path.join(ROOT, "apps", "bomber", "assets", "images", "splash.jpg")
OUTDIR = os.path.join(ROOT, "apps", "bomber", "assets", "ui")

# --- measured geometry ------------------------------------------------------
# The four buttons sit on a 276 px pitch; OPTIONS is the one worth cutting: it is
# the only one clear of the brick and terrain, and its single short label leaves
# the widest clean plank on both sides.
BUTTON = (625, 771, 866, 874)       # x0,y0,x1,y1 inclusive, the plank itself
FILL0, FILL1 = 660, 830             # lettered span to rebuild
SRC_L, SRC_R = 656, 834             # clean columns, inside the flat runs 640..663 / 826..853
MARGIN = 6                          # background kept around the crop for the flood fill to start in

# 9-slice insets for the result. Horizontal cuts land in the flat plank either
# side; vertical cuts land inside the middle plank, clear of both grooves.
SLICE = dict(left=20, right=20, top=42, bottom=40)


def extract(src, box, fill, src_cols, margin):
    bx0, by0, bx1, by1 = box
    f0, f1 = fill
    sl, sr = src_cols
    crop = src.crop((bx0 - margin, by0 - margin, bx1 + margin + 1, by1 + margin + 1))
    w, h = crop.size
    px = crop.load()

    # -- text removal: interpolate the span between two clean columns
    col_l = [px[sl - bx0 + margin, y] for y in range(h)]
    col_r = [px[sr - bx0 + margin, y] for y in range(h)]
    for x in range(f0, f1):
        t = (x - f0) / float(f1 - 1 - f0)
        t = t * t * (3 - 2 * t)         # smoothstep, so each join matches its neighbour
        cx = x - bx0 + margin
        for y in range(h):
            a, b = col_l[y], col_r[y]
            px[cx, y] = (int(a[0] + (b[0] - a[0]) * t + 0.5),
                         int(a[1] + (b[1] - a[1]) * t + 0.5),
                         int(a[2] + (b[2] - a[2]) * t + 0.5))

    # -- alpha: flood the background in from every border pixel
    def is_bg(p):
        return (p[0] - p[2]) < 22

    mask = [[False] * w for _ in range(h)]
    stack = []
    for x in range(w):
        for y in (0, h - 1):
            if is_bg(px[x, y]) and not mask[y][x]:
                mask[y][x] = True; stack.append((x, y))
    for y in range(h):
        for x in (0, w - 1):
            if is_bg(px[x, y]) and not mask[y][x]:
                mask[y][x] = True; stack.append((x, y))
    while stack:
        x, y = stack.pop()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = x + dx, y + dy
            if 0 <= nx < w and 0 <= ny < h and not mask[ny][nx] and is_bg(px[nx, ny]):
                mask[ny][nx] = True; stack.append((nx, ny))

    # -- decontaminate: grow plank colour outwards under the feather
    solid = [[not mask[y][x] for x in range(w)] for y in range(h)]
    for _ in range(3):
        grown = []
        for y in range(h):
            for x in range(w):
                if solid[y][x]:
                    continue
                acc, n = [0, 0, 0], 0
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < w and 0 <= ny < h and solid[ny][nx]:
                        c = px[nx, ny]
                        acc[0] += c[0]; acc[1] += c[1]; acc[2] += c[2]; n += 1
                if n:
                    grown.append((x, y, (acc[0] // n, acc[1] // n, acc[2] // n)))
        for x, y, c in grown:
            px[x, y] = c
            solid[y][x] = True

    alpha = Image.new("L", (w, h), 0)
    ap = alpha.load()
    for y in range(h):
        for x in range(w):
            ap[x, y] = 0 if mask[y][x] else 255
    # a touch of blur restores the anti-aliasing the hard mask threw away
    alpha = alpha.filter(ImageFilter.GaussianBlur(0.6))

    out = crop.convert("RGBA")
    out.putalpha(alpha)
    return out.crop((margin, margin, margin + (bx1 - bx0 + 1), margin + (by1 - by0 + 1)))


def nine_slice(img, s, w, h):
    """Stretch img to w x h, keeping the corners unscaled. The reference for what
    the engine-side widget has to do; also the proof that the cut points are sane."""
    l, r, t, b = s["left"], s["right"], s["top"], s["bottom"]
    iw, ih = img.size
    w = max(w, l + r + 1)
    h = max(h, t + b + 1)
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    cols = [(0, l, 0, l), (l, iw - r, l, w - r), (iw - r, iw, w - r, w)]
    rows = [(0, t, 0, t), (t, ih - b, t, h - b), (ih - b, ih, h - b, h)]
    for sx0, sx1, dx0, dx1 in cols:
        for sy0, sy1, dy0, dy1 in rows:
            piece = img.crop((sx0, sy0, sx1, sy1))
            if (dx1 - dx0, dy1 - dy0) != piece.size:
                piece = piece.resize((dx1 - dx0, dy1 - dy0), Image.BICUBIC)
            out.paste(piece, (dx0, dy0))
    return out


def main():
    if not os.path.exists(SPLASH):
        sys.exit("splash not found: %s" % SPLASH)
    src = Image.open(SPLASH).convert("RGB")

    btn = extract(src, BUTTON, (FILL0, FILL1), (SRC_L, SRC_R), MARGIN)
    if not os.path.isdir(OUTDIR):
        os.makedirs(OUTDIR)
    path = os.path.join(OUTDIR, "button_blank.png")
    btn.save(path)
    print("wrote %s  %dx%d  9-slice l/r/t/b = %d/%d/%d/%d"
          % (path, btn.width, btn.height,
             SLICE["left"], SLICE["right"], SLICE["top"], SLICE["bottom"]))

    if "--demo" in sys.argv:
        demo_dir = sys.argv[sys.argv.index("--demo") + 1]
        sizes = [(btn.width, btn.height), (420, 104), (150, 104), (300, 170)]
        pad = 14
        width = max(s[0] for s in sizes) + pad * 2
        height = sum(s[1] + pad for s in sizes) + pad
        sheet = Image.new("RGB", (width, height))
        sp = sheet.load()
        for yy in range(height):
            for xx in range(width):
                sp[xx, yy] = (208, 208, 208) if ((xx // 8) + (yy // 8)) % 2 == 0 else (154, 154, 154)
        y = pad
        for (w, h) in sizes:
            sheet.paste(nine_slice(btn, SLICE, w, h), (pad, y), nine_slice(btn, SLICE, w, h))
            y += h + pad
        out = os.path.join(demo_dir, "nine_slice_demo.png")
        sheet.save(out)
        print("wrote %s" % out)


if __name__ == "__main__":
    main()
