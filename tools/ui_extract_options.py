"""Cut reusable UI sprites out of art_source/bomber_ui_example_2.png.

That mockup is a finished options screen painted as one flat image: panels with
content in them, a close button, buttons, sliders, checkboxes. A UI framework
needs the pieces back out - a window frame it can draw at any size, and sprites
with alpha - so this recovers the two that scope the rest: the panel frame and
the close cross.

THE TWO IDEAS THIS RESTS ON, both of which took a wrong turn first:

  ALPHA BY FLOOD FILL, THEN PER-ROW SPAN FILL. A threshold alone cannot cut these
  out: the cave wall behind is not uniformly dark (it has lit stones and moss),
  and the frame is not uniformly light (its bars are separated by dark gaps that
  connect the outside to the interior, so a plain fill leaks straight through
  them and hollows the panel out). The fix is that both shapes are CONVEX - a
  chamfered rectangle and a bevelled square - so after flooding, taking each
  row's span between its first and last opaque pixel closes every gap and
  reproduces the silhouette exactly. Leaks stop mattering, because a leaked
  interior still leaves the frame bars as that row's first and last opaque pixel.

  THE INTERIOR IS REFILLED, NOT PRESERVED. A window frame has to be content-free,
  and the panel interior is flat neutral grey (median 47,47,47), so it can simply
  be repainted. A thin band just inside the frame is kept from the art, because
  that is where the inner bevel and its shadow live and they are part of the
  frame; everything deeper is repainted and cross-faded into that band. This is
  also what makes the 9-slice insets safe - the title text that used to sit
  within an inset of the top edge is gone before any slicing happens.

Scanning for the rects needs a search window that STARTS IN REAL BACKGROUND: the
panels sit about 18 px apart, so a scan begun far outside stops on the
neighbouring panel's frame instead. Hence the explicit windows below.

RUN IT WITH THE WINDOWS PYTHON, not MSYS's - only that one has Pillow.

    python tools/ui_extract_options.py [--out DIR] [--proof DIR]
"""
import os
import sys
from PIL import Image, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MOCKUP = os.path.join(ROOT, "art_source", "bomber_ui_example_2.png")
OUTDIR = os.path.join(ROOT, "apps", "bomber", "assets", "ui")

# Search windows, each starting inside real background. cx,cy is any point within
# the sprite; the scan meets its edges from outside.
#
# `inset` is the 9-slice inset, or None for a sprite that is always drawn at one
# size. It is a pinned constant, not a measurement, and deliberately so: what it
# has to cover is the corner ART - the chamfered wooden segment, which ends 28 px
# in - and nothing in the image marks where that ends. The silhouette's own
# chamfer is only ~14 px, so deriving the inset from the alpha (tried first)
# cuts through the middle of the corner segment and skews it when stretched.
# check_inset below asserts it at least clears the silhouette.
#
# `margin` is how much background to crop around the rect. It must exceed how far
# the sprite reaches beyond the detected rect (the scan stops at bright material,
# so it lands INSIDE the dark outline the art draws around each shape), but must
# not reach a neighbouring widget - the panels sit only ~18 px apart, and a
# margin that touches the next panel's frame lets the span fill bridge the two.
# Nothing sits near the close button, so it can afford a generous one.
SPRITES = {
    #  name             cx    cy    x window        y window      inset margin
    "window_frame": (1255,  780, (1010, 1500), (586, 978),  30,    8),   # the VIDEO panel
    "button_close": (1448,   86, (1380, 1524), (18, 160),   None, 22),
}
BEVEL_BAND = 8          # interior kept from the art, where the frame's inner bevel lives
OUTLINE = 3             # dark outline the art draws around each shape, in px


def solid(p):
    """Frame material: warm wood, or stone bright enough not to be cave wall."""
    r, g, b = p
    return (r - b) > 25 or (r + g + b) / 3.0 > 95


def find_rect(px, cx, cy, xlim, ylim, run=4):
    def scan(fixed, start, end, horizontal):
        step = 1 if end > start else -1
        hits = 0
        for i in range(start, end, step):
            if solid(px[i, fixed] if horizontal else px[fixed, i]):
                hits += 1
                if hits >= run:
                    return i - step * (run - 1)
            else:
                hits = 0
        return None

    def med(v):
        v = sorted(i for i in v if i is not None)
        return v[len(v) // 2] if v else None

    ys = list(range(ylim[0] + 40, ylim[1] - 40, 19))
    xs = list(range(xlim[0] + 40, xlim[1] - 40, 19))
    return (med([scan(y, xlim[0], cx, True) for y in ys]),
            med([scan(x, ylim[0], cy, False) for x in xs]),
            med([scan(y, xlim[1], cx, True) for y in ys]),
            med([scan(x, ylim[1], cy, False) for x in xs]))


def cut(im, rect, margin, grow=OUTLINE):
    """Crop rect with a margin and return the sprite as RGBA."""
    x0, y0, x1, y1 = rect
    crop = im.crop((x0 - margin, y0 - margin, x1 + margin + 1, y1 + margin + 1)).convert("RGB")
    w, h = crop.size
    px = crop.load()

    def is_bg(p):
        return not solid(p)

    bg = [[False] * w for _ in range(h)]
    stack = []
    for x in range(w):
        for y in (0, h - 1):
            if is_bg(px[x, y]) and not bg[y][x]:
                bg[y][x] = True; stack.append((x, y))
    for y in range(h):
        for x in (0, w - 1):
            if is_bg(px[x, y]) and not bg[y][x]:
                bg[y][x] = True; stack.append((x, y))
    while stack:
        x, y = stack.pop()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = x + dx, y + dy
            if 0 <= nx < w and 0 <= ny < h and not bg[ny][nx] and is_bg(px[nx, ny]):
                bg[ny][nx] = True; stack.append((nx, ny))

    # convex span fill: everything between a row's first and last opaque pixel is
    # inside the sprite, which closes the gaps the fill leaked through
    for y in range(h):
        on = [x for x in range(w) if not bg[y][x]]
        if on:
            for x in range(on[0], on[-1] + 1):
                bg[y][x] = False
    # and again down columns, which squares off the chamfers the row pass rounded
    for x in range(w):
        on = [y for y in range(h) if not bg[y][x]]
        if on:
            for y in range(on[0], on[-1] + 1):
                bg[y][x] = False

    # Grow the mask outward to take back the dark outline the art draws around
    # each shape. That outline is as dark as the cave wall, so the fill counts it
    # as background and the span fill cannot recover it - it lies OUTSIDE each
    # row's first and last opaque pixel. Without this the wood meets transparency
    # with no edge at all, which reads as a cut-out rather than a drawn widget.
    for _ in range(grow):
        edge = [(x, y) for y in range(h) for x in range(w)
                if bg[y][x] and any(0 <= x + dx < w and 0 <= y + dy < h and not bg[y + dy][x + dx]
                                    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))]
        for x, y in edge:
            bg[y][x] = False

    alpha = Image.new("L", (w, h), 0)
    ap = alpha.load()
    for y in range(h):
        for x in range(w):
            ap[x, y] = 0 if bg[y][x] else 255
    alpha = alpha.filter(ImageFilter.GaussianBlur(0.6))
    out = crop.convert("RGBA")
    out.putalpha(alpha)
    # Trim to what the alpha actually covers, NOT back to the rect: the rect is a
    # lower bound (it stops inside the outline), so cropping to it clips the
    # sprite - which is what sliced the stone border off the close button.
    return out.crop(out.getbbox())


def bar_thickness(sprite):
    """How thick the frame bar is at the middle of each edge.

    This is where the interior starts, so it is where the repaint starts - not at
    the corner inset, which is larger and would leave the content that sits along
    the middle of an edge untouched.
    """
    px = sprite.load()
    w, h = sprite.size
    my, mx = h // 2, w // 2

    def walk(get, n):
        i = 0
        while i < n and (get(i)[3] < 8 or solid(get(i)[:3])):
            i += 1
        return i

    return (walk(lambda i: px[i, my], w), walk(lambda i: px[mx, i], h),
            walk(lambda i: px[w - 1 - i, my], w), walk(lambda i: px[mx, h - 1 - i], h))


def check_inset(sprite, inset):
    """Warn if the inset would cut inside the silhouette's chamfer.

    A necessary condition, not a sufficient one - it catches an inset that is
    outright too small, but cannot see where the corner artwork ends.
    """
    px = sprite.load()
    w, h = sprite.size
    chamfer = 0
    for x in range(w):
        ys = [y for y in range(h) if px[x, y][3] > 128]
        if ys and ys[0] <= 1:
            chamfer = x
            break
    if inset < chamfer:
        print("  WARNING: inset %d cuts inside the %d px silhouette chamfer" % (inset, chamfer))
    return chamfer


def refill_interior(sprite, inset, band=BEVEL_BAND):
    """Repaint everything deeper than `band` inside the frame with the panel's own
    colour, cross-faded into the kept band so there is no seam."""
    px = sprite.load()
    w, h = sprite.size
    l, t, r, b = inset
    x0, y0 = l + band, t + band
    x1, y1 = w - r - band, h - b - band
    if x1 - x0 < 8 or y1 - y0 < 8:
        return sprite, None

    samples = sorted((px[x, y][:3] for y in range(y0, y1, 3) for x in range(x0, x1, 3)),
                     key=lambda c: sum(c))
    fill = samples[len(samples) // 2]

    fade = 5.0
    for y in range(y0, y1):
        for x in range(x0, x1):
            # 0 at the band edge, 1 once clear of it, so the repaint eases in
            d = min(x - x0, y - y0, x1 - 1 - x, y1 - 1 - y) / fade
            k = min(1.0, max(0.0, d))
            k = k * k * (3 - 2 * k)
            c = px[x, y]
            px[x, y] = (int(c[0] + (fill[0] - c[0]) * k),
                        int(c[1] + (fill[1] - c[1]) * k),
                        int(c[2] + (fill[2] - c[2]) * k), c[3])
    return sprite, fill


def nine_slice(img, ins, w, h):
    l, t, r, b = ins
    iw, ih = img.size
    w, h = max(w, l + r + 1), max(h, t + b + 1)
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    for sx0, sx1, dx0, dx1 in ((0, l, 0, l), (l, iw - r, l, w - r), (iw - r, iw, w - r, w)):
        for sy0, sy1, dy0, dy1 in ((0, t, 0, t), (t, ih - b, t, h - b), (ih - b, ih, h - b, h)):
            piece = img.crop((sx0, sy0, sx1, sy1))
            if (dx1 - dx0, dy1 - dy0) != piece.size:
                piece = piece.resize((dx1 - dx0, dy1 - dy0), Image.BICUBIC)
            out.paste(piece, (dx0, dy0))
    return out


def checker(w, h):
    s = Image.new("RGB", (w, h))
    sp = s.load()
    for y in range(h):
        for x in range(w):
            sp[x, y] = (208, 208, 208) if ((x // 8) + (y // 8)) % 2 == 0 else (154, 154, 154)
    return s


def main():
    out_dir = OUTDIR
    if "--out" in sys.argv:
        out_dir = sys.argv[sys.argv.index("--out") + 1]
    if not os.path.isdir(out_dir):
        os.makedirs(out_dir)

    im = Image.open(MOCKUP).convert("RGB")
    px = im.load()
    results = {}

    for name, (cx, cy, xlim, ylim, inset, margin) in SPRITES.items():
        rect = find_rect(px, cx, cy, xlim, ylim)
        sprite = cut(im, rect, margin)
        note = ""
        if name == "window_frame":
            # Repaint from the frame's own BAR THICKNESS inwards, not from the
            # 9-slice inset: the inset is larger, and starting there would leave
            # the content that sits along the middle of each edge untouched.
            sprite, fill = refill_interior(sprite, bar_thickness(sprite))
            note = "  interior refilled rgb%s" % (fill,)
        ins = (inset,) * 4 if inset else None
        if ins:
            check_inset(sprite, inset)
        path = os.path.join(out_dir, name + ".png")
        sprite.save(path)
        results[name] = (sprite, ins)
        print("%-14s rect=%s  %dx%d  9-slice=%s%s"
              % (name, rect, sprite.width, sprite.height,
                 ("%d all round" % inset) if inset else "fixed size", note))

    if "--proof" not in sys.argv:
        return
    proof_dir = sys.argv[sys.argv.index("--proof") + 1]

    # proof sheet: each sprite at native size, and the frame stretched about
    sheet_w, rows = 1000, []
    frame, fins = results["window_frame"]
    close, _ = results["button_close"]
    rows.append([(frame, None), (close, None)])
    rows.append([(nine_slice(frame, fins, 300, 200), None),
                 (nine_slice(frame, fins, 220, 330), None)])
    rows.append([(nine_slice(frame, fins, 640, 150), None)])

    pad = 14
    hgt = sum(max(i.height for i, _ in row) + pad for row in rows) + pad
    sheet = checker(sheet_w, hgt)
    y = pad
    for row in rows:
        x = pad
        for img, _ in row:
            sheet.paste(img, (x, y), img)
            x += img.width + pad
        y += max(i.height for i, _ in row) + pad
    proof = os.path.join(proof_dir, "_proof.png")
    sheet.save(proof)
    print("wrote %s" % proof)


if __name__ == "__main__":
    main()
