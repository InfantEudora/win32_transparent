"""
Writes the FIRST version of apps/chasm/assets/textures/palette.png.

After that the PNG is the source: edit it in any paint program. This script refuses to overwrite it
unless given --force, so a hand-tuned palette is never lost to a re-run.

The layout is fixed by apps/chasm/Palette.h: 32 x 16 cells, CELL pixels square. A ROW is a biome
(row 0 temperate, then desert, frozen, swamp; row 15 the effects row; the rest spare); a COLUMN is a material within it.
The engine samples the centre of a cell with nearest filtering and no mipmaps, so a cell is
one flat colour - paint every pixel of a cell the same.

Colours are sRGB-ish values straight to the screen (the engine has no tone mapping), chosen
against the A Little Age screenshot in art_source/chasm/ under the default lighting.

    python apps/chasm/tools/make_palette.py [--force | --effects]
"""
import os
import sys
from PIL import Image

CELL = 8
COLS = 32
ROWS = 16

# Column meanings - keep in step with Palette.h.
GRASS_0, GRASS_1, GRASS_2, GRASS_3 = 0, 1, 2, 3
LIP = 4
ROCK_0, ROCK_1, ROCK_2, ROCK_3 = 5, 6, 7, 8
FLOOR, FLOOR_DARK = 9, 10
EARTH = 11
FIELD, PATH, WATER, HAZE = 12, 13, 14, 15

def row(grass, lip, rock, floor, floor_dark, earth, field, path, water, haze):
    r = [None] * COLS
    for i, g in enumerate(grass):
        r[GRASS_0 + i] = g
    r[LIP] = lip
    for i, k in enumerate(rock):
        r[ROCK_0 + i] = k
    r[FLOOR] = floor
    r[FLOOR_DARK] = floor_dark
    r[EARTH] = earth
    r[FIELD] = field
    r[PATH] = path
    r[WATER] = water
    r[HAZE] = haze
    return r

ROWS_DEF = [
    # 0 temperate: A Little Age's muted plains green (measured ~160,188,125 on screen, lit), warm
    # sandstone strata, a mossy chasm floor.
    # The four grass shades are a step or two apart, no more: one per coarse cell, so a wider
    # spread draws the grid on the ground as a patchwork.
    row(grass=[(150, 178, 110), (152, 180, 112), (148, 176, 108), (153, 181, 113)],
        lip=(168, 182, 118),
        rock=[(176, 140, 104), (148, 112, 84), (120, 92, 72), (164, 124, 90)],
        floor=(92, 108, 84), floor_dark=(72, 86, 68),
        earth=(112, 84, 62),
        field=(222, 196, 110), path=(156, 118, 78), water=(56, 104, 206), haze=(178, 204, 220)),
    # 1 desert
    row(grass=[(222, 196, 140), (228, 202, 146), (214, 188, 132), (232, 208, 152)],
        lip=(206, 172, 120),
        rock=[(196, 132, 92), (170, 110, 78), (140, 90, 66), (186, 122, 84)],
        floor=(150, 118, 86), floor_dark=(120, 94, 70),
        earth=(150, 104, 72),
        field=(214, 180, 96), path=(176, 140, 96), water=(60, 130, 200), haze=(232, 214, 186)),
    # 2 frozen
    row(grass=[(232, 238, 244), (238, 242, 248), (224, 232, 240), (242, 246, 250)],
        lip=(210, 220, 232),
        rock=[(132, 140, 152), (110, 118, 132), (90, 98, 112), (124, 130, 144)],
        floor=(180, 192, 206), floor_dark=(150, 162, 178),
        earth=(104, 108, 118),
        field=(200, 206, 196), path=(170, 176, 186), water=(120, 170, 220), haze=(214, 226, 238)),
    # 3 swamp
    row(grass=[(96, 122, 62), (102, 128, 66), (90, 116, 58), (108, 132, 70)],
        lip=(112, 128, 74),
        rock=[(116, 104, 84), (96, 86, 70), (78, 70, 58), (108, 96, 76)],
        floor=(66, 80, 52), floor_dark=(50, 62, 42),
        earth=(82, 70, 54),
        field=(170, 160, 92), path=(110, 96, 70), water=(70, 96, 84), haze=(170, 184, 168)),
]

# Columns 16-31: props (trees, rocks, bushes - art_source/chasm/chasm_props.blend) and buildings.
# One list of 16 per biome row, in Palette.h's order: pine dark/mid/light, leaf dark/mid/light,
# bark, bark dark, stone light/mid/dark, bush, accent, wall, roof, timber.
PROPS_DEF = [
    # 0 temperate: A Little Age's dark conifers and warm roofs.
    [(30, 92, 50), (44, 118, 62), (70, 142, 76), (78, 128, 52), (98, 150, 62), (128, 172, 80),
     (110, 78, 52), (78, 56, 40), (178, 172, 160), (146, 140, 130), (110, 106, 100),
     (88, 130, 60), (220, 90, 80), (232, 224, 206), (186, 84, 62), (120, 84, 58)],
    # 1 desert: palms and scrub, sandstone, adobe.
    [(70, 110, 52), (96, 136, 64), (126, 158, 80), (120, 132, 70), (146, 156, 84), (172, 176, 104),
     (140, 104, 70), (108, 78, 54), (220, 196, 160), (196, 168, 130), (160, 132, 100),
     (140, 146, 82), (226, 160, 60), (226, 200, 160), (196, 120, 76), (150, 106, 70)],
    # 2 frozen: snow-dusted conifers, grey stone.
    [(52, 88, 76), (74, 110, 96), (196, 214, 222), (110, 124, 116), (150, 164, 160), (214, 226, 232),
     (96, 80, 70), (70, 60, 54), (196, 204, 214), (156, 164, 176), (118, 126, 140),
     (120, 140, 130), (170, 70, 90), (226, 230, 236), (110, 118, 140), (100, 84, 74)],
    # 3 swamp: dark willows and mossy wood.
    [(40, 74, 44), (56, 92, 52), (80, 112, 62), (70, 96, 48), (90, 116, 58), (116, 138, 72),
     (90, 72, 52), (64, 52, 40), (140, 140, 120), (112, 112, 96), (84, 86, 74),
     (76, 104, 52), (200, 190, 90), (190, 184, 160), (110, 96, 70), (96, 76, 54)],
]

# Row 15 is not a biome: the EFFECTS row, colours that are the same in every biome. Columns 0-3
# are the chasm's mist - light, mid, dark - and the foam at the foot of a fall (Mist.h).
EFFECTS_ROW = 15
EFFECTS_DEF = [(126, 128, 134), (102, 104, 112), (80, 82, 90), (226, 234, 238)]

def paint_effects(img):
    for c, colour in enumerate(EFFECTS_DEF):
        for y in range(CELL):
            for x in range(CELL):
                img.putpixel((c * CELL + x, EFFECTS_ROW * CELL + y), colour + (255,))

def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.normpath(os.path.join(here, "..", "assets", "textures", "palette.png"))
    # --effects paints only the effects row into the existing PNG, leaving every other cell as it is.
    if "--effects" in sys.argv:
        img = Image.open(out).convert("RGBA")
        paint_effects(img)
        img.save(out)
        print("painted the effects row into", out)
        return 0
    if os.path.exists(out) and "--force" not in sys.argv:
        print("refusing to overwrite %s - it is the source now; pass --force to start over" % out)
        return 1
    img = Image.new("RGBA", (COLS * CELL, ROWS * CELL), (255, 0, 255, 255))   # magenta = unused
    for r, props in enumerate(PROPS_DEF):
        for i, colour in enumerate(props):
            ROWS_DEF[r][16 + i] = colour
    for r, cells in enumerate(ROWS_DEF):
        for c, colour in enumerate(cells):
            if colour is None:
                continue
            for y in range(CELL):
                for x in range(CELL):
                    img.putpixel((c * CELL + x, r * CELL + y), colour + (255,))
    paint_effects(img)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    img.save(out)
    print("wrote", out)
    return 0

if __name__ == "__main__":
    sys.exit(main())
