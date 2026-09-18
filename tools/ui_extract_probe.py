"""Measure a UI mockup, so ui_extract_splash.py's coordinates are derived and not guessed.

Run this when the mockup is redrawn, or when cutting a new widget out of a new
one. It answers the three questions the extractor needs answered, and every
number it prints is one that is otherwise tempting to eyeball wrong:

  1. WHERE ARE THE WIDGETS?  Warmth (r-b) separates painted wood from a cool cave
     background far more cleanly than brightness does - the plank rim is dark but
     still warm, so brightness thresholds keep slicing the rim off.

  2. WHERE IS THE WIDGET FLAT ALONG X?  The per-column gradient is near zero over
     plain plank and jumps at the end caps and at lettering. Only the near-zero
     runs are safe to source a stretched middle from. This is the measurement the
     whole text-removal trick depends on.

  3. WHERE MAY A 9-SLICE CUT?  Same scan along y: a cut through a busy row smears
     a groove or a rim when the widget is stretched.

    python tools/ui_extract_probe.py                         # the bomber splash
    python tools/ui_extract_probe.py path/to/other.png       # some other mockup

Windows Python only - MSYS's has no Pillow. See CLAUDE.md.
"""
import os
import sys
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT = os.path.join(ROOT, "apps", "bomber", "assets", "images", "splash.jpg")

# The band the widgets sit in, as a fraction of height. Buttons along the bottom
# is the common mockup layout; override on the command line for anything else.
BAND = (0.86, 0.99)


def warm(p):
    return (p[0] - p[2]) > 25


def find_widgets(px, w, h, band):
    y0, y1 = int(h * band[0]), int(h * band[1])
    bh = y1 - y0
    hist = [sum(1 for y in range(y0, y1) if warm(px[x, y])) for x in range(w)]
    runs, start = [], None
    for x in range(w):
        on = hist[x] > bh * 0.55
        if on and start is None:
            start = x
        elif not on and start is not None:
            if x - start > 120:
                runs.append((start, x - 1))
            start = None
    if start is not None and w - start > 120:
        runs.append((start, w - 1))

    boxes = []
    for (xa, xb) in runs:
        span = xb - xa + 1
        ys = [y for y in range(max(0, y0 - 40), min(h, y1 + 20))
              if sum(1 for x in range(xa, xb + 1) if warm(px[x, y])) > span * 0.55]
        if ys:
            boxes.append((xa, min(ys), xb, max(ys)))
    return boxes


def gradient_x(px, box, skip=3):
    """Mean per-column change along x. Low = flat plank, safe to source from."""
    x0, y0, x1, y1 = box
    out = []
    for x in range(x0, x1):
        tot = sum(sum(abs(px[x, y][c] - px[x + 1, y][c]) for c in range(3))
                  for y in range(y0 + skip, y1 - skip))
        out.append((x, tot / float(max(1, y1 - y0 - 2 * skip))))
    return out


def gradient_y(px, box, cols):
    """Mean per-row change along y, sampled ONLY over the given columns.

    Sampling the whole width would read the lettering, and a two-line label makes
    every row look busy - which says nothing about where the widget's own
    horizontal features are. Feeding in the flat runs from the x-scan restricts
    this to text-free plank, so what is left really is rim, groove and bevel.
    """
    x0, y0, x1, y1 = box
    xs = [x for a, b in cols for x in range(a, b + 1)]
    if not xs:
        xs = list(range(x0 + 40, x1 - 40))
    out = []
    for y in range(y0, y1):
        tot = sum(sum(abs(px[x, y][c] - px[x, y + 1][c]) for c in range(3)) for x in xs)
        out.append((y, tot / float(len(xs))))
    return out


def runs_below(profile, limit):
    """Contiguous stretches of the profile under limit - the usable ones."""
    out, start, prev = [], None, None
    for i, v in profile:
        if v < limit and start is None:
            start = i
        elif v >= limit and start is not None:
            out.append((start, prev))
            start = None
        prev = i
    if start is not None:
        out.append((start, prev))
    return [r for r in out if r[1] - r[0] >= 4]


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT
    im = Image.open(path).convert("RGB")
    w, h = im.size
    px = im.load()
    print("%s  %dx%d" % (path, w, h))

    boxes = find_widgets(px, w, h, BAND)
    print("\n%d widget(s) found in the bottom band:" % len(boxes))
    for i, (x0, y0, x1, y1) in enumerate(boxes):
        print("  [%d] x %4d..%4d (w=%3d)   y %4d..%4d (h=%3d)"
              % (i, x0, x1, x1 - x0 + 1, y0, y1, y1 - y0 + 1))
    if not boxes:
        return

    # Pick a widget to profile by the SMALLEST height, not the average one: warm
    # scenery touching a widget can only ever inflate its box (the brick above the
    # outer two buttons does exactly that), never shrink it. So the shortest box
    # is the one whose bounds are really the widget's own.
    clean = min(b[3] - b[1] for b in boxes)
    box = next(b for b in boxes if (b[3] - b[1]) - clean <= 2)
    print("\nprofiling widget x %d..%d y %d..%d" % (box[0], box[2], box[1], box[3]))

    gx = gradient_x(px, box)
    flat = runs_below(gx, 8.0)
    print("\n  flat along x (source a stretched middle only from these):")
    for a, b in flat:
        print("    x %4d..%4d  (%d px)" % (a, b, b - a + 1))
    print("  busiest columns (end caps and lettering):")
    hot = sorted(gx, key=lambda t: -t[1])[:8]
    print("    " + ", ".join("%d:%.0f" % t for t in sorted(hot)))

    gy = gradient_y(px, box, flat)
    quiet = runs_below(gy, 12.0)
    print("\n  quiet along y (a 9-slice may cut here):")
    for a, b in quiet:
        print("    y %4d..%4d  -> top inset %d, bottom inset %d"
              % (a, b, a - box[1], box[3] - b))


if __name__ == "__main__":
    main()
