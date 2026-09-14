"""Read mono_sdf.fnt the way the engine will, and check it says what fontbake reported.

This is the consumer-side check: the tool validating its own header proves the struct is
self-consistent, not that an independent reader lands on the same bytes.
"""
import struct, sys

path = sys.argv[1] if len(sys.argv) > 1 else "shared_assets/fonts/mono_sdf.fnt"
raw = open(path, "rb").read()

# 4s + 20 x 4-byte fields, matching core/UIFont.h's ui_font_header.
fmt = "<4s I IIIIII II fff ff ff II II"
size = struct.calcsize(fmt)
print("header size: %d (UIFont.h static_asserts 84)" % size)

(magic, version,
 atlas_w, atlas_h, grid_w, grid_h, cell_w, cell_h,
 first_char, num_chars,
 em_px, advance_px, line_height_px,
 origin_x, origin_y,
 onedge, distance_range_px,
 solid_x, solid_y,
 pixel_offset, pixel_bytes) = struct.unpack(fmt, raw[:size])

fail = []
def check(name, cond, detail=""):
    print(("  OK   " if cond else "  FAIL ") + name + ("  " + detail if detail else ""))
    if not cond:
        fail.append(name)

print("\nheader:")
check("magic", magic == b"FNT1", str(magic))
check("version", version == 1, str(version))
check("atlas = grid * cell",
      grid_w * cell_w == atlas_w and grid_h * cell_h == atlas_h,
      "%dx%d = %dx%d cells of %dx%d" % (atlas_w, atlas_h, grid_w, grid_h, cell_w, cell_h))
check("printable ASCII", first_char == 0x20 and num_chars == 95,
      "first=0x%02X num=%d" % (first_char, num_chars))
check("spare cell for solid", num_chars < grid_w * grid_h,
      "%d glyphs in %d cells" % (num_chars, grid_w * grid_h))
check("pixel_bytes", pixel_bytes == atlas_w * atlas_h, str(pixel_bytes))
check("file length", len(raw) == pixel_offset + pixel_bytes,
      "%d == %d + %d" % (len(raw), pixel_offset, pixel_bytes))

print("\nmetrics: em=%.1f advance=%.3f line_height=%.3f origin=(%.1f,%.1f)"
      % (em_px, advance_px, line_height_px, origin_x, origin_y))
print("field:   onedge=%.4f (%.0f/255)  range=%.2f px" % (onedge, onedge * 255, distance_range_px))

px = raw[pixel_offset:]

def texel(x, y):
    return px[y * atlas_w + x]

def cell_of(ch):
    i = ord(ch) - first_char
    return (i % grid_w, i * 0 + i // grid_w)

def cell_peak(ch):
    """Brightest texel in a glyph's cell - its deepest 'inside' point."""
    cx, cy = cell_of(ch)
    best = 0
    for y in range(cell_h):
        row = px[(cy * cell_h + y) * atlas_w + cx * cell_w:][:cell_w]
        m = max(row)
        if m > best:
            best = m
    return best

print("\npixels:")
check("solid texel is fully inside", texel(solid_x, solid_y) == 255,
      "value %d at (%d,%d)" % (texel(solid_x, solid_y), solid_x, solid_y))
check("space has no ink", cell_peak(" ") < onedge * 255,
      "peak %d, edge %.0f" % (cell_peak(" "), onedge * 255))
for ch in "AMg!~0":
    check("'%s' has ink" % ch, cell_peak(ch) > onedge * 255,
          "peak %d" % cell_peak(ch))

# The pen origin must land on the baseline: 'A' sits on it, so ink should be ABOVE origin_y
# and essentially nothing below. A sign error here is the classic way text ends up shifted by
# a line, and it is invisible in a single-glyph screenshot.
cx, cy = cell_of("A")
below = 0
for y in range(int(origin_y) + 2, cell_h):
    row = px[(cy * cell_h + y) * atlas_w + cx * cell_w:][:cell_w]
    below = max(below, max(row))
check("'A' sits on the baseline", below < onedge * 255,
      "peak %d below origin_y=%.0f" % (below, origin_y))

# 'g' is a descender and MUST have ink below the baseline, or origin_y is wrong the other way.
cx, cy = cell_of("g")
below = 0
for y in range(int(origin_y) + 2, cell_h):
    row = px[(cy * cell_h + y) * atlas_w + cx * cell_w:][:cell_w]
    below = max(below, max(row))
check("'g' descends below the baseline", below > onedge * 255, "peak %d" % below)

print("\n%s" % ("ALL CHECKS PASSED" if not fail else "FAILED: " + ", ".join(fail)))
sys.exit(1 if fail else 0)
