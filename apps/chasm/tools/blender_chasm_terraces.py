"""Builds art_source/chasm/chasm_terraces.blend - the chasm as COLUMNS on the game's grid, all quads - and
renders previews.

    blender.exe -b --python apps/chasm/tools/blender_chasm_terraces.py -- [--force] [--render [names]] [--quick]
    blender.exe -b --python apps/chasm/tools/blender_chasm_terraces.py -- --render-only [names] [--quick]

Inside Blender, with chasm_terraces.blend open: edit `chasm_plots`, then run the `rebuild_terraces` text
block (Text Editor, Run Script). The terrain, the rivers' water and the falls are rebuilt from the plots;
nothing else in the file is touched.

WHY COLUMNS. chasm_cliffs.blend (blender_chasm_cliffs.py) has the right look, but it is loose faces welded
and triangulated, so nobody can edit it, and nothing in it is the game's own data. This file is built from
what the game already has: the fine grid (Townscaper's method, as Grid.cpp makes it) and ONE HEIGHT PER
FINE VERTEX. In the game a plot IS a vertex, drawn as the polygon round it through its quads' centres and
edge midpoints (grid_plan.md section 2). So here a column is a plot, standing at its vertex's height, and
the chasm is nothing but plots at different heights. That is the blocky, stacked look of chasm_aigen_1.png,
and it is the same data the game's levels already are - only more of them.

`chasm_plots` is that data: the fine grid as a mesh, one vertex per plot, z = the top of its column. It is
the ONLY input the terrain is built from (a fresh build generates it first, then builds from it like a
rebuild does), so whatever is edited there - heights, or the grid itself - comes out the same way.
Heights are snapped to 0.5 and are meant to sit on the 3-unit strata steps (plateau 0, balcony -12,
island -24, floor -72); a river bed is -1.

THE MESHER (build_terrain), everything a quad:
  - TOPS: every grid face is cut at its centre and edge midpoints into one corner quad per corner, and
    each corner quad is its corner's column top. A plot's top is the 3-6 corner quads round its vertex.
  - WALLS: along every plot boundary segment (edge midpoint to face centre) whose two plots differ in
    height, from the higher top down to the lower. Walls are cut into RINGS at a global set of heights
    (every column top, every top's turf and soil lines, every stratum), so where walls meet on a vertical
    line they share its vertices: each edge has two faces, and loop-select picks out a whole stratum.
  - SADDLES (two high plots diagonal across a quad, two low) would put four walls on one vertical edge.
    The higher of the two low plots is raised instead, which joins the high pair as the game's marching
    squares do (TerrainMesh.cpp). A rebuild writes the change back to chasm_plots and says how many.
  - THE LOOK is all in where ring vertices go, never in the topology: each ring vertex moves along the
    wall's outward normal by its column's offset for that stratum (columns stand out or sit back, some
    strata are soft and recessed), plus a little at an edge midpoint (the middle of a column's face) and
    minus a little at a face centre between two columns (the joint between them). Under each top the
    turf band overhangs and the soil band tucks in. Faces get their palette cell from what they are:
    turf, soil, a stratum, a ledge (upward facing: rock or moss) or an underside.
  - The map's outline gets a SKIRT down to BASE_Z, as TerrainMesh.cpp's does.

COLOURS: one palette cell per face, row 0 for rock and grass, the effects row (15) for mist and foam
(Palette.h). Flat shaded. Not exported to the game yet - nothing would load it; the port is the mesher.
"""

import json
import math
import os
import random
import sys
import time

import bmesh
import bpy
import numpy as np
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
BLEND = os.path.join(REPO, "art_source", "chasm", "chasm_terraces.blend")
PROPS_BLEND = os.path.join(REPO, "art_source", "chasm", "chasm_props.blend")
PALETTE = os.path.join(REPO, "apps", "chasm", "assets", "textures", "palette.png")
PREVIEWS = os.path.join(REPO, "art_source", "chasm", "previews")

#Palette.h - must agree with it.
PALETTE_COLS = 32
PALETTE_ROWS = 16
PAL_GRASS_0 = 0
PAL_LIP = 4
PAL_ROCK_0 = 5          #the lightest
PAL_ROCK_1 = 6
PAL_ROCK_2 = 7          #the darkest: undersides and the deep
PAL_ROCK_3 = 8
PAL_EARTH = 11
PAL_PATH = 13          #a river's shore: the bed showing past the water
PAL_WATER = 14
PAL_HAZE = 15           #pale blue: streaks in a fall
PAL_EFFECTS = 15        #row
PAL_MIST_LIGHT = 0      #columns in the effects row
PAL_MIST = 1
PAL_FOAM = 3

#Heights. The game's levels are plateau 0, balcony -12, island -24, floor -70 (Terrain.cpp); here they are
#steps of one stratum, so a level is just a height that happens to be a multiple of STEP.
STEP = 3.0              #a stratum, and what column tops are cut to
FLOOR_N = 24            #the floor, in steps: -72
RIVER_BED = -1.0
WATER_Z = -0.4          #a river's surface: the bank's soil line, so the water meets the banks at a ring
MIST_TOP = -38.0        #the cloud deck's top; islands at -24 stand well out of it
BASE_Z = -80.0          #the skirt's bottom
CAP_DEEPEST = -40.0     #tops deeper than this get no turf or soil: nobody sees them
STRATA_FINE_TO = -42.0  #a stratum every STEP down to here, every other one below

#The cap: turf, then soil, under every top.
TURF = 0.30             #the turf's side
TURF_UNDER = 0.40       #the step under the overhang
SOIL = 1.00             #where the rock starts
LIP = 0.22              #how far the turf overhangs the rock under it
CHAMFER = 0.30          #between one stratum and the next: a short slope, not a sliver
BULGE = 0.10            #a column's face stands out at its middle...
GROOVE = 0.14           #...and its joints with its neighbours sit back
OFFSET_MAX = 0.45       #a stratum's own in/out
TOP_BUMP = 0.08         #tops sag a little away from their rims, for facets; never up
GRASS_BY_LIGHT = (2, 0, 1, 3)   #palette row 0's grass columns, darkest first
EPS = 0.02

#The section: the chasm runs along Y (north), home (east) is +X. Game units.
X0, X1 = -110.0, 110.0
Y0, Y1 = -125.0, 125.0
LATTICE = 8.0           #GridSettings::triangle_side: fine cells come out about 1.8 across
RELAX_PASSES = 40
RELAX_STRENGTH = 0.5

STUB = '''import bpy, sys
#Rebuilds chasm_terrain, chasm_water and chasm_falls from chasm_plots. Nothing else is touched.
path = bpy.path.abspath("//../../apps/chasm/tools/blender_chasm_terraces.py")
sys.argv = [sys.argv[0], "--", "--rebuild"]
exec(compile(open(path).read(), path, "exec"), {"__name__": "__main__", "__file__": path})
'''


def pal_uv(col, row=0):
    #Blender's V runs up from the image's bottom; the engine's row 0 is the top row. The glTF exporter
    #flips V on the way out, so this lands on Palette.h's PaletteUV() in a file.
    return ((col + 0.5) / PALETTE_COLS, 1.0 - (row + 0.5) / PALETTE_ROWS)


def hash2(ix, iy, seed):
    h = (ix * 374761393 + iy * 668265263 + seed * 982451653) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((h ^ (h >> 16)) & 0xFFFF) / 65535.0


def vnoise(x, y, seed=0):
    """Value noise in 0..1, smooth."""
    ix, iy = math.floor(x), math.floor(y)
    fx, fy = x - ix, y - iy
    fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    a, b = hash2(ix, iy, seed), hash2(ix + 1, iy, seed)
    c, d = hash2(ix, iy + 1, seed), hash2(ix + 1, iy + 1, seed)
    return (a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy


def lnoise(t, seed, wrap=0):
    """1D value noise in 0..1 joined by straight lines: kinks, not curves - a fresh crack is angular."""
    i = math.floor(t)
    f = t - i
    a, b = i, i + 1
    if wrap:
        a, b = a % wrap, b % wrap
    ha, hb = hash2(a, 0, seed), hash2(b, 0, seed)
    return ha + (hb - ha) * f


def signed(n):
    return 2.0 * n - 1.0


def smoothstep(a, b, x):
    t = max(0.0, min(1.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


# ---------------------------------------------------------------------------------------------
# The grid: Grid.cpp's method (grid_plan.md section 1), in miniature.
# ---------------------------------------------------------------------------------------------

def lattice():
    """Triangles over the section in rows along X. Odd rows are offset half a side, and their end points
    are snapped onto the section's sides, so the outline is straight all round."""
    h = LATTICE * math.sqrt(3.0) / 2.0
    nr = max(1, round((Y1 - Y0) / h))
    nc = max(2, round((X1 - X0) / LATTICE))
    s = (X1 - X0) / nc
    pts, rows = [], []
    for r in range(nr + 1):
        y = Y0 + (Y1 - Y0) * r / nr
        if r % 2 == 0:
            xs = [X0 + c * s for c in range(nc + 1)]
        else:
            xs = [X0] + [X0 + (c + 0.5) * s for c in range(1, nc - 1)] + [X1]
        rows.append(list(range(len(pts), len(pts) + len(xs))))
        pts.extend((x, y) for x in xs)
    tris = []
    for r in range(nr):
        A, B = rows[r], rows[r + 1]
        i = j = 0
        #Zip the two rows together, always advancing the one whose next point is further left.
        while i < len(A) - 1 or j < len(B) - 1:
            if j == len(B) - 1 or (i < len(A) - 1 and pts[A[i + 1]][0] < pts[B[j + 1]][0]):
                tris.append((A[i], A[i + 1], B[j]))
                i += 1
            else:
                tris.append((A[i], B[j + 1], B[j]))
                j += 1
    return pts, tris


def merge(rng, tris):
    """Random neighbouring triangles into quads; whatever is left over stays a triangle."""
    edge_tris = {}
    for t, (a, b, c) in enumerate(tris):
        for u, v in ((a, b), (b, c), (c, a)):
            edge_tris.setdefault((min(u, v), max(u, v)), []).append(t)
    order = list(range(len(tris)))
    rng.shuffle(order)
    used = [False] * len(tris)
    polys = []
    for t in order:
        if used[t]:
            continue
        tri = tris[t]
        options = []
        for k in range(3):
            u, v = tri[k], tri[(k + 1) % 3]
            for o in edge_tris[(min(u, v), max(u, v))]:
                if o != t and not used[o]:
                    options.append((k, o))
        used[t] = True
        if not options:
            polys.append(tri)
            continue
        k, o = rng.choice(options)
        used[o] = True
        r = (tri[(k + 2) % 3], tri[k], tri[(k + 1) % 3])     #the shared edge is now r[1] -> r[2]
        d = [x for x in tris[o] if x not in (r[1], r[2])][0]
        polys.append((r[0], r[1], d, r[2]))
    return polys


def subdivide(pts, polys):
    """Every polygon into one quad per corner through its centroid and its edge midpoints."""
    pts = list(pts)
    mids = {}

    def mid(a, b):
        key = (min(a, b), max(a, b))
        if key not in mids:
            mids[key] = len(pts)
            pts.append(((pts[a][0] + pts[b][0]) * 0.5, (pts[a][1] + pts[b][1]) * 0.5))
        return mids[key]

    quads = []
    for poly in polys:
        n = len(poly)
        c = len(pts)
        pts.append((sum(pts[i][0] for i in poly) / n, sum(pts[i][1] for i in poly) / n))
        m = [mid(poly[i], poly[(i + 1) % n]) for i in range(n)]
        for i in range(n):
            quads.append((poly[i], m[i], c, m[i - 1]))
    return pts, quads


def relax(pts, quads):
    """Each quad pulls its corners toward the square that best fits them (grid_plan.md section 1 step 4);
    the pulls are averaged per vertex and applied together. The outline slides along itself."""
    P = np.array(pts, dtype=np.float64)
    Q = np.array(quads, dtype=np.int64)
    fx = (np.abs(P[:, 0] - X0) < 1e-6) | (np.abs(P[:, 0] - X1) < 1e-6)
    fy = (np.abs(P[:, 1] - Y0) < 1e-6) | (np.abs(P[:, 1] - Y1) < 1e-6)
    keep_x, keep_y = P[:, 0].copy(), P[:, 1].copy()
    count = np.bincount(Q.ravel(), minlength=len(P)).astype(np.float64)
    count[count == 0] = 1.0
    for _ in range(RELAX_PASSES):
        p = P[Q]
        c = p.mean(axis=1)
        d = p - c[:, None, :]
        #Corner i turned back by i quarter turns: all four agree for a square.
        v = (d[:, 0] + np.stack([d[:, 1, 1], -d[:, 1, 0]], -1) - d[:, 2]
             + np.stack([-d[:, 3, 1], d[:, 3, 0]], -1)) * 0.25
        t1 = np.stack([-v[:, 1], v[:, 0]], -1)
        target = np.stack([v, t1, -v, -t1], 1) + c[:, None, :]
        f = target - p
        acc = np.zeros_like(P)
        np.add.at(acc[:, 0], Q.ravel(), f[:, :, 0].ravel())
        np.add.at(acc[:, 1], Q.ravel(), f[:, :, 1].ravel())
        P += RELAX_STRENGTH * acc / count[:, None]
        P[fx, 0] = keep_x[fx]
        P[fy, 1] = keep_y[fy]
    return [tuple(p) for p in P]


def make_grid(rng):
    pts, tris = lattice()
    polys = merge(rng, tris)
    pts, quads = subdivide(pts, polys)
    pts = relax(pts, quads)                 #the coarse level
    pts, quads = subdivide(pts, quads)
    pts = relax(pts, quads)                 #the fine level: one vertex per plot
    #subdivide() leaves the lattice's points behind as well; keep only what a quad uses.
    used = sorted(set(i for q in quads for i in q))
    remap = {old: new for new, old in enumerate(used)}
    return [pts[i] for i in used], [tuple(remap[i] for i in q) for q in quads]


# ---------------------------------------------------------------------------------------------
# Heights: the chasm as a layout. The game's ChasmLayout makes its own; this one is the section's.
# ---------------------------------------------------------------------------------------------

SHEAR = 7.0             #the rift opened a little sideways as well as apart: the walls match, offset

def spine(y):
    return 4.0 * math.sin(y / 58.0 + 0.6) + 2.5 * math.sin(y / 21.0 + 2.0)


def half_width(y):
    return 38.0 + 7.0 * math.sin(y / 45.0 + 1.2) + 3.0 * math.sin(y / 17.0 + 0.3)


def jag(y):
    """How far the west rim stands into the chasm. The east rim has the same, SHEAR further north: a
    promontory on one side is a bay on the other, as if the two walls had just come apart."""
    return (8.0 * signed(lnoise(y / 26.0, 1)) + 3.5 * signed(lnoise(y / 9.5, 2))
            + 1.4 * signed(lnoise(y / 3.7, 3)))


def rim_west(y):
    return spine(y) - half_width(y) + jag(y)


def rim_east(y):
    return spine(y) + half_width(y) + jag(y - SHEAR)


def fringe_width(y, side):
    """How far out from the wall the broken columns reach. Nothing, in places: there the wall is sheer."""
    return 13.0 * smoothstep(0.40, 0.75, vnoise(y / 24.0 + side * 31.7, 0.5, 40 + side))


#(x from the spine, y, x radius, y radius, turn, steps down, seed). Islands (-24) are the game's shards;
#the tall narrow ones its columns.
ISLANDS = [
    (2.0, 8.0, 15.0, 10.0, 0.5, 8, 1),
    (-6.0, -80.0, 8.0, 6.0, -0.3, 6, 2),
    (9.0, 64.0, 4.5, 3.5, 0.2, 3, 3),
    (-13.0, -30.0, 3.2, 2.6, 0.0, 5, 4),
    (11.0, -52.0, 2.6, 2.0, 0.0, 9, 5),
    (-10.0, 96.0, 6.0, 4.0, 1.0, 7, 6),
]
#(side 0 west / 1 east, y, half its length along the wall, how far it reaches out, steps down, seed).
#The first two are the game's balconies (-12); the rest are the small low patches between.
BALCONIES = [
    (1, -36.0, 12.0, 10.0, 4, 1),
    (0, 50.0, 9.0, 8.0, 4, 2),
    (1, 86.0, 5.0, 5.0, 2, 3),
    (0, -62.0, 6.0, 5.0, 3, 4),
    (0, -8.0, 4.0, 4.0, 2, 5),
    (1, 14.0, 4.5, 4.0, 3, 6),
]
#(start x on the map's east edge, start y, the y where it reaches the rim, seed). Home is east, and only
#home has rivers (grid_plan.md).
RIVERS = [
    (X1 + 2.0, 82.0, 40.0, 1),
    (X1 + 2.0, -106.0, -78.0, 2),
]
RIVER_HALF = 2.6        #the water, either side of the line
RIVER_SHORE = 1.4       #bed plots reach this much further: plots are blocky, the water's edge is not
RIVER_WET = 3.0         #past the bank: no crumbling, no cracks, no trees
#(side, y at the rim, heading away from the chasm in degrees, length, seed)
FISSURES = [
    (0, -96.0, 20.0, 18.0, 1),
    (0, -22.0, -25.0, 14.0, 2),
    (0, 28.0, 10.0, 22.0, 3),
    (0, 86.0, -15.0, 11.0, 4),
    (1, -6.0, 15.0, 16.0, 5),
    (1, 62.0, -20.0, 12.0, 6),
    (1, -100.0, 25.0, 10.0, 7),
]


def island_steps(x, y):
    for dx, yc, rx, ry, rot, n, seed in ISLANDS:
        cx = spine(yc) + dx
        px, py = x - cx, y - yc
        c, s = math.cos(rot), math.sin(rot)
        u, v = (px * c + py * s) / rx, (-px * s + py * c) / ry
        r = math.hypot(u, v)
        if r > 2.0:
            continue
        a = (math.atan2(v, u) / (2.0 * math.pi) + 0.5)
        edge = 1.0 + 0.16 * signed(lnoise(a * 9.0, seed, 9)) + 0.07 * signed(lnoise(a * 23.0, seed + 9, 23))
        scale = min(rx, ry)
        inner = (edge - r) * scale           #about how far in from the island's edge
        if inner > 0.0:
            if inner < 1.8 and vnoise(x / 2.4, y / 2.4, 60 + seed) > 0.68:
                return n + 1                 #crumbling at its own edge
            return n
        if inner > -3.5:
            return n + 1 + int(3.99 * vnoise(x / 2.2, y / 2.2, 70 + seed))   #its broken skirt
    return None


def balcony_steps(x, y, side, inside):
    for s, yc, hl, depth, n, seed in BALCONIES:
        if s != side:
            continue
        u = (y - yc) / hl
        if abs(u) >= 1.25:
            continue
        reach = depth * math.sqrt(max(0.0, 1.0 - u * u)) * (0.8 + 0.4 * lnoise(y / 3.3, 80 + seed))
        if inside < reach:
            return n
        if inside < reach + 3.0 and abs(u) < 1.15:
            return n + 1 + int(2.99 * vnoise(x / 2.2, y / 2.2, 90 + seed))
    return None


def make_pillars():
    """Lone pillars of a plot or a few, standing free in the chasm with grass on top: the game's columns,
    small. In the heights, not props, because a pillar is plots like everything else."""
    rng = random.Random(11)
    out = []
    tries = 0
    while len(out) < 12 and tries < 800:
        tries += 1
        y = rng.uniform(Y0 + 8, Y1 - 8)
        xl, xr = rim_west(y), rim_east(y)
        x = rng.uniform(xl, xr)
        side = 0 if x - xl < xr - x else 1
        if min(x - xl, xr - x) < fringe_width(y, side) + 3.0:
            continue
        if any(math.hypot(x - spine(yc) - dx, y - yc) < max(rx, ry) + 6 for dx, yc, rx, ry, *_ in ISLANDS):
            continue
        if any(math.hypot(x - px, y - py) < 9 for px, py, _, _ in out):
            continue
        out.append((x, y, rng.uniform(1.5, 2.9), rng.randint(2, 10)))
    return out


PILLARS = make_pillars()


def polyline_nearest(pts, x, y):
    """Distance from (x, y) to a polyline, how far along it the nearest point is, and that point."""
    best, best_s, foot, s = 1e9, 0.0, (x, y), 0.0
    for (ax, ay), (bx, by) in zip(pts, pts[1:]):
        dx, dy = bx - ax, by - ay
        L2 = dx * dx + dy * dy
        t = 0.0 if L2 == 0 else max(0.0, min(1.0, ((x - ax) * dx + (y - ay) * dy) / L2))
        d = math.hypot(x - ax - dx * t, y - ay - dy * t)
        if d < best:
            best, best_s, foot = d, s + t * math.sqrt(L2), (ax + dx * t, ay + dy * t)
        s += math.sqrt(L2)
    return best, best_s, foot


def polyline_dist(pts, x, y):
    d, s, _ = polyline_nearest(pts, x, y)
    return d, s


def onto_river(rivers, x, y):
    """(x, y) pulled in to the water's edge if it lies beyond it: the water is smooth, the bed is plots."""
    best = None
    for r in rivers:
        d, _, foot = polyline_nearest(r, x, y)
        if best is None or d < best[0]:
            best = (d, foot)
    if best is None or best[0] <= RIVER_HALF:
        return x, y, best[0] if best else 0.0
    d, (fx, fy) = best
    return fx + (x - fx) * RIVER_HALF / d, fy + (y - fy) * RIVER_HALF / d, d


def river_line(sx, sy, ey, seed):
    """From the map's edge to past the rim, meandering; ends 5 inside the chasm so the bed cuts the lip."""
    ex = rim_east(ey) - 5.0
    rng = random.Random(seed)
    phase = rng.uniform(0, 6.28)
    dx, dy = ex - sx, ey - sy
    L = math.hypot(dx, dy)
    nx, ny = -dy / L, dx / L
    pts = []
    steps = int(L / 2.5)
    for k in range(steps + 1):
        t = k / steps
        swing = 7.0 * math.sin(t * 2.3 * math.pi + phase) * math.sin(math.pi * min(1.0, t * 1.15)) ** 0.5
        swing += 1.2 * signed(lnoise(t * 9.0, seed + 50))
        pts.append((sx + dx * t + nx * swing, sy + dy * t + ny * swing))
    return pts


def fissure_line(side, y, heading, length, seed):
    rng = random.Random(100 + seed)
    x = rim_west(y) if side == 0 else rim_east(y)
    a = math.radians(heading)
    pts = [(x, y)]
    s = 0.0
    while s < length:
        a += rng.uniform(-0.35, 0.35)
        dx = -math.cos(a) if side == 0 else math.cos(a)
        x, y = x + dx * 1.4, y + math.sin(a) * 1.4
        pts.append((x, y))
        s += 1.4
    return pts, length


def fix_saddles(faces, top):
    """Two high corners diagonal across a face and two low ones would stand four walls on one line. Raise
    the higher low corner to the lower high one: the high pair joins, as TerrainMesh.cpp's saddle does."""
    changed = 0
    for _ in range(30):
        again = False
        for f in faces:
            hs = sorted(set(top[c] for c in f))
            for lo, hi in zip(hs, hs[1:]):
                z = 0.5 * (lo + hi)
                up = [top[c] > z for c in f]
                runs = sum(1 for i in range(len(f)) if up[i] and not up[i - 1])
                if runs > 1:
                    lows = [c for c in f if top[c] < z]
                    c = max(lows, key=lambda k: top[k])
                    top[c] = min(top[k] for k in f if top[k] > z)
                    changed += 1
                    again = True
                    break
        if not again:
            break
    return changed


def neighbours(n, quads):
    nbr = [set() for _ in range(n)]
    for q in quads:
        for i in range(len(q)):
            a, b = q[i], q[(i + 1) % len(q)]
            nbr[a].add(b)
            nbr[b].add(a)
    return nbr


def generate_tops(P, quads):
    rivers = [river_line(*spec) for spec in RIVERS]
    lips = [(rim_east(ey), ey) for _, _, ey, _ in RIVERS]
    fissures = [fissure_line(*spec) for spec in FISSURES]
    tops, wet = [], []
    for x, y in P:
        dw, de = x - rim_west(y), rim_east(y) - x
        inside = min(dw, de)
        side = 0 if dw < de else 1
        river_d = min(polyline_dist(r, x, y)[0] for r in rivers) if x > 0 else 1e9
        wet.append(river_d < RIVER_HALF + RIVER_SHORE + RIVER_WET)
        if inside > 0.0:
            n = chasm_steps(x, y, inside, side, lips)
        elif river_d < RIVER_HALF + RIVER_SHORE:
            tops.append(RIVER_BED)
            continue
        else:
            n = 0
            if not wet[-1]:
                for pts, length in fissures:
                    if abs(pts[0][1] - y) > length + 3 or abs(pts[0][0] - x) > length + 3:
                        continue
                    d, s = polyline_dist(pts, x, y)
                    f = min(1.0, s / length)
                    #Wide enough to always catch a plot (they are ~1.8 apart): narrower, a crack
                    #came out as a row of separate pits.
                    if d < 2.3 - 1.2 * f:
                        n = max(n, int(round(9.0 - 8.0 * f)))
        tops.append(-n * STEP)

    #The rim crumbling: plateau plots that stand over a real drop break a step or two down, in clumps;
    #then, more rarely, the plots behind those. Only ever next to a drop - never a pit in the meadow.
    nbr = neighbours(len(P), quads)
    for ring, (lo, hi, need) in enumerate(((0.60, 0.76, -6.0), (0.70, 2.0, -3.0))):
        drops = []
        for i, (x, y) in enumerate(P):
            if tops[i] != 0.0 or wet[i] or not any(tops[j] <= need for j in nbr[i]):
                continue
            d = vnoise(x / 2.8, y / 2.8, 50 + ring)
            if d > hi:
                drops.append((i, -2 * STEP))
            elif d > lo:
                drops.append((i, -STEP))
        for i, t in drops:
            tops[i] = t
    return tops, wet, rivers, lips


def chasm_steps(x, y, inside, side, lips):
    for lx, ly in lips:
        if math.hypot(x - lx, y - ly) < 8.0:
            return FLOOR_N                  #a clear drop in front of every fall
    n = island_steps(x, y)
    if n is not None:
        return n
    for px, py, pr, pn in PILLARS:
        if math.hypot(x - px, y - py) < pr:
            return pn
    n = balcony_steps(x, y, side, inside)
    if n is not None:
        return n
    F = fringe_width(y, side)
    if inside < F:
        if vnoise(x / 5.0, y / 5.0, 21) > 0.76:
            return FLOOR_N                  #gaps, so the broken columns stand apart
        #Blocks a few plots across rather than plot-by-plot noise: the stepping is in the noise's
        #levels, so neighbours mostly agree and the edges between blocks are clean steps.
        t = inside / F
        n = (1.5 + 7.0 * t + (vnoise(x / 6.0, y / 6.0, 11 + side) - 0.5) * 5.0
             + (hash2(round(x * 7), round(y * 7), 12) - 0.5) * 0.8)
        return max(1, min(int(round(n)), 12))
    return FLOOR_N


# ---------------------------------------------------------------------------------------------
# The mesher: chasm_plots -> chasm_terrain (+ water, falls). See the module docstring.
# ---------------------------------------------------------------------------------------------

def band(z):
    """The stratum a height is in. A stratum's top ring belongs to it; the ring CHAMFER above belongs to
    the one above."""
    return int(math.floor((-z + CHAMFER * 0.5) / STEP))


BLOCK_LEVELS = (-0.28, -0.08, 0.10, 0.28)


def rock_offset(h, x, y, k):
    """How far plot h's stratum k stands out (+) or sits back (-) from the plot boundary. The main part
    is position noise cut into a few levels, so a block of rock is two or three plots wide and two strata
    tall, flush across itself and stepped against the next - the stacked blocks of chasm_aigen_1."""
    lvl = BLOCK_LEVELS[min(3, int(vnoise(x / 4.5, y / 4.5, 71 + k // 2) * 4.0))]
    block = 0.07 * signed(hash2(h, k, 72))
    soft = -0.20 if hash2(k, 0, 73) < 0.30 else 0.0          #a soft stratum, eroded back all along
    return max(-OFFSET_MAX, min(OFFSET_MAX, lvl + block + soft))


def strata_colour(k, h):
    if k < 5:
        seq = (PAL_ROCK_3, PAL_ROCK_0, PAL_ROCK_3, PAL_ROCK_1)
    elif k < 10:
        seq = (PAL_ROCK_1, PAL_ROCK_3, PAL_ROCK_1, PAL_ROCK_2)
    else:
        seq = (PAL_ROCK_1, PAL_ROCK_2)
    c = seq[k % len(seq)]
    if hash2(h, k, 81) < 0.12:
        c = seq[(k + 1) % len(seq)]
    return c


def newell(pts):
    n = Vector((0.0, 0.0, 0.0))
    for a, b in zip(pts, pts[1:] + pts[:1]):
        n.x += (a[1] - b[1]) * (a[2] + b[2])
        n.y += (a[2] - b[2]) * (a[0] + b[0])
        n.z += (a[0] - b[0]) * (a[1] + b[1])
    return n.normalized() if n.length > 1e-12 else n


def build_terrain(plots, coll, material):
    t0 = time.time()
    me = plots.data
    N = len(me.vertices)
    xy = [(v.co.x, v.co.y) for v in me.vertices]
    top = [round(v.co.z * 2.0) / 2.0 for v in me.vertices]
    faces = []
    for p in me.polygons:
        f = list(p.vertices)
        area = sum(xy[f[i - 1]][0] * xy[f[i]][1] - xy[f[i]][0] * xy[f[i - 1]][1] for i in range(len(f)))
        if area < 0:
            f.reverse()
        faces.append(f)
    joined = fix_saddles(faces, top)
    moved = sum(1 for i, v in enumerate(me.vertices) if abs(v.co.z - top[i]) > 1e-6)
    if moved:
        for i, v in enumerate(me.vertices):
            v.co.z = top[i]
        me.update()
        print("chasm_plots: %d heights snapped or raised (%d saddles joined), written back" % (moved, joined))

    #Points: plots (0..N), edge midpoints (E0..), face centres (F0..).
    edge_id, edge_faces = {}, []
    for fi, f in enumerate(faces):
        for i in range(len(f)):
            a, b = f[i], f[(i + 1) % len(f)]
            k = (a, b) if a < b else (b, a)
            if k not in edge_id:
                edge_id[k] = len(edge_faces)
                edge_faces.append([])
            edge_faces[edge_id[k]].append(fi)
    NE = len(edge_faces)
    E0, F0 = N, N + len(edge_faces)
    pos = list(xy) + [None] * (NE + len(faces))
    adj = [[i] for i in range(N)] + [None] * (NE + len(faces))
    outline = [False] * len(pos)
    for (a, b), e in edge_id.items():
        pos[E0 + e] = ((xy[a][0] + xy[b][0]) * 0.5, (xy[a][1] + xy[b][1]) * 0.5)
        adj[E0 + e] = [a, b]
        if len(edge_faces[e]) == 1:
            outline[a] = outline[b] = outline[E0 + e] = True
    for fi, f in enumerate(faces):
        pos[F0 + fi] = (sum(xy[c][0] for c in f) / len(f), sum(xy[c][1] for c in f) / len(f))
        adj[F0 + fi] = f

    #The rings: every height any wall is cut at. Shared by all walls, so they share vertices.
    tops = sorted(set(top), reverse=True)
    zs = set()
    for t in tops:
        zs.add(t)
        if t > CAP_DEEPEST:
            zs.update((t - TURF, t - TURF_UNDER, t - SOIL))
    k = 1
    while -k * STEP > BASE_Z + 0.5:
        z = -k * STEP
        if z >= STRATA_FINE_TO or k % 2 == 0:
            zs.update((z, z + CHAMFER))
        k += 1
    zs.add(BASE_Z)
    exact = set(tops) | {BASE_Z}
    ring_z = []
    for z in sorted(zs, reverse=True):
        if ring_z and ring_z[-1] - z < 0.08:
            if z in exact and ring_z[-1] not in exact:
                ring_z[-1] = z
            continue
        ring_z.append(z)
    ri = {z: i for i, z in enumerate(ring_z)}
    R = len(ring_z)

    def key(pid, r):
        return pid * R + r

    quads = []          #(vertex keys, kind)
    seg_n = {}          #vertex key -> {segment: its wall's outward normal}
    falls = []          #(point a, point b, normal): wall segments a river goes over

    def strip(A, B, r0, r1, nrm, kind):
        sx, sy = pos[B][0] - pos[A][0], pos[B][1] - pos[A][1]
        flip = (-sy * nrm[0] + sx * nrm[1]) < 0
        seg = (A, B) if A < B else (B, A)
        for r in range(r0, r1):
            a0, b0, a1, b1 = key(A, r), key(B, r), key(A, r + 1), key(B, r + 1)
            quads.append(((b0, a0, a1, b1) if flip else (a0, b0, b1, a1), kind + (r,)))
        for r in range(r0, r1 + 1):
            seg_n.setdefault(key(A, r), {})[seg] = nrm
            seg_n.setdefault(key(B, r), {})[seg] = nrm

    for fi, f in enumerate(faces):
        n = len(f)
        es = [edge_id[(min(f[i], f[(i + 1) % n]), max(f[i], f[(i + 1) % n]))] for i in range(n)]
        for i in range(n):
            c = f[i]
            r = ri[top[c]]
            quads.append(((key(c, r), key(E0 + es[i], r), key(F0 + fi, r), key(E0 + es[i - 1], r)),
                          ("top", c, fi * 8 + i)))
        for i in range(n):
            a, b = f[i], f[(i + 1) % n]
            if top[a] == top[b]:
                continue
            H, L = (a, b) if top[a] > top[b] else (b, a)
            A, B = E0 + es[i], F0 + fi
            sx, sy = pos[B][0] - pos[A][0], pos[B][1] - pos[A][1]
            ln = math.hypot(sx, sy) or 1.0
            nrm = (-sy / ln, sx / ln)
            if nrm[0] * (xy[L][0] - xy[H][0]) + nrm[1] * (xy[L][1] - xy[H][1]) < 0:
                nrm = (-nrm[0], -nrm[1])
            strip(A, B, ri[top[H]], ri[top[L]], nrm, ("wall", H, L))
            if abs(top[H] - RIVER_BED) < EPS and top[L] < RIVER_BED - 4.0:
                falls.append((A, B, nrm))
    for (a, b), e in edge_id.items():
        if len(edge_faces[e]) != 1:
            continue
        f = faces[edge_faces[e][0]]
        i = f.index(a)
        if f[(i + 1) % len(f)] != b:
            a, b = b, a
        dx, dy = xy[b][0] - xy[a][0], xy[b][1] - xy[a][1]
        ln = math.hypot(dx, dy) or 1.0
        out = (dy / ln, -dx / ln)
        strip(a, E0 + e, ri[top[a]], ri[BASE_Z], out, ("skirt", a, -1))
        strip(E0 + e, b, ri[top[b]], ri[BASE_Z], out, ("skirt", b, -1))

    def column_offset(h, z):
        t = top[h]
        ob = rock_offset(h, xy[h][0], xy[h][1], band(t - SOIL - 0.05))
        dz = t - z
        if dz < TURF + EPS:
            return ob + LIP
        if dz < SOIL + EPS:
            return ob
        return rock_offset(h, xy[h][0], xy[h][1], band(z))

    def offset_at(pid, z, segs):
        cols = adj[pid]
        above = [c for c in cols if top[c] > z + EPS]
        at = [c for c in cols if abs(top[c] - z) <= EPS]
        below = [c for c in cols if top[c] < z - EPS]
        if not below or (at and above):
            return 0.0, (0.0, 0.0)          #no wall here, or a wall's foot on a lower top: stays put
        hs = at if at else above
        o = sum(column_offset(h, z) for h in hs) / len(hs)
        if pid < F0:
            o += BULGE
        elif len(hs) > 1:
            o -= GROOVE
        drop = min(top[h] for h in hs) - max(top[c] for c in below)
        if drop < 2.5:
            o *= drop / 2.5                 #a river bank, not a cliff
        o = max(-0.6, min(0.75, o))
        ns = list(segs.values())
        sx, sy = sum(n[0] for n in ns), sum(n[1] for n in ns)
        ln = math.hypot(sx, sy)
        if ln < 1e-6:
            return 0.0, (0.0, 0.0)
        nx, ny = sx / ln, sy / ln
        miter = min(nx * n[0] + ny * n[1] for n in ns)
        return o / max(miter, 0.66), (nx, ny)

    keys = sorted(set(k for q, _ in quads for k in q))
    vindex = {}
    verts = []
    for k in keys:
        pid, r = divmod(k, R)
        z = ring_z[r]
        x, y = pos[pid]
        segs = seg_n.get(k)
        if outline[pid]:
            pass
        elif segs:
            o, (nx, ny) = offset_at(pid, z, segs)
            x, y = x + nx * o, y + ny * o
        else:
            z -= TOP_BUMP * vnoise(x / 3.2, y / 3.2, 7)
        vindex[k] = len(verts)
        verts.append((x, y, z))

    out_faces, out_cols = [], []
    for q, kind in quads:
        idx = tuple(vindex[k] for k in q)
        out_faces.append(idx)
        if kind[0] == "top":
            _, c, salt = kind
            t = top[c]
            if abs(t - RIVER_BED) < EPS:
                col = PAL_PATH
            elif t > CAP_DEEPEST:
                #One shade per plot, in patches: per corner quad it read as a checkerboard.
                g = 0.75 * vnoise(xy[c][0] / 9.0, xy[c][1] / 9.0, 31) + 0.35 * hash2(c, 0, 32)
                col = PAL_GRASS_0 + GRASS_BY_LIGHT[min(3, int(g * 3.6))]
            else:
                col = PAL_ROCK_2
        else:
            _, H, _, r = kind
            zm = 0.5 * (ring_z[r] + ring_z[r + 1])
            d = top[H] - zm
            if d < TURF and top[H] > CAP_DEEPEST:
                col = PAL_LIP
            elif d < SOIL and top[H] > CAP_DEEPEST:
                col = PAL_EARTH
            else:
                nz = newell([verts[i] for i in idx]).z
                if nz > 0.4:
                    col = PAL_GRASS_0 + 2 if hash2(H, r, 33) < 0.12 else PAL_ROCK_0     #moss on a ledge
                elif nz < -0.4:
                    col = PAL_ROCK_2
                else:
                    col = strata_colour(band(zm), H)
        out_cols.append((col, 0))

    terrain = replace_object("chasm_terrain", verts, out_faces, out_cols, material, coll)

    #The rivers' water: each bed plot's top, lifted to WATER_Z and pushed under the banks and to the lip -
    #then, if chasm_plots carries the rivers' lines, pulled in to RIVER_HALF from them, so the water's
    #edge is smooth and the blocky bed shows past it as a muddy shore.
    rivers = json.loads(plots.get("rivers", "[]"))
    bed = [abs(t - RIVER_BED) < EPS for t in top]
    wverts, wfaces, wkeys = [], [], {}

    def wkey(pid):
        if pid not in wkeys:
            x, y = pos[pid]
            beds = [c for c in adj[pid] if bed[c]]
            if pid >= N and len(beds) < len(adj[pid]):
                cx = sum(xy[c][0] for c in beds) / len(beds)
                cy = sum(xy[c][1] for c in beds) / len(beds)
                ln = math.hypot(x - cx, y - cy) or 1.0
                x, y = x + (x - cx) / ln * 0.37, y + (y - cy) / ln * 0.37
            x, y, _ = onto_river(rivers, x, y)
            wkeys[pid] = len(wverts)
            wverts.append((x, y, WATER_Z))
        return wkeys[pid]

    for fi, f in enumerate(faces):
        n = len(f)
        for i, c in enumerate(f):
            if bed[c]:
                ea = E0 + edge_id[(min(c, f[(i + 1) % n]), max(c, f[(i + 1) % n]))]
                eb = E0 + edge_id[(min(c, f[i - 1]), max(c, f[i - 1]))]
                wfaces.append((wkey(c), wkey(ea), wkey(F0 + fi), wkey(eb)))
    replace_object("chasm_water", wverts, wfaces, [(PAL_WATER, 0)] * len(wfaces), material, coll)

    #The falls: a sheet from every lip segment, bowing out as it drops into the cloud deck. As wide as the
    #water, not the bed: segments wholly on the shore are left dry.
    if rivers:
        falls = [s for s in falls if min(onto_river(rivers, *pos[s[0]])[2], onto_river(rivers, *pos[s[1]])[2])
                 < RIVER_HALF + 0.2]
    fn = {}
    for a, b, nrm in falls:
        for p in (a, b):
            sx, sy = fn.get(p, (0.0, 0.0))
            fn[p] = (sx + nrm[0], sy + nrm[1])
    J = 14
    drop = WATER_Z - (MIST_TOP - 2.0)
    fverts, fkeys = [], {}
    for p, (sx, sy) in fn.items():
        ln = math.hypot(sx, sy) or 1.0
        nx, ny = sx / ln, sy / ln
        bx, by, _ = onto_river(rivers, *pos[p])
        for j in range(J + 1):
            d = drop * (j / J) ** 1.5
            out = LIP + 0.15 + 2.4 * (d / 30.0) ** 0.7
            fkeys[(p, j)] = len(fverts)
            fverts.append((bx + nx * out, by + ny * out, WATER_Z - d))
    ffaces, fcols = [], []
    for a, b, nrm in falls:
        sx, sy = pos[b][0] - pos[a][0], pos[b][1] - pos[a][1]
        flip = (-sy * nrm[0] + sx * nrm[1]) < 0
        streak = hash2(min(a, b), max(a, b), 41) < 0.3
        for j in range(J):
            q = (fkeys[(a, j)], fkeys[(b, j)], fkeys[(b, j + 1)], fkeys[(a, j + 1)])
            ffaces.append(q[::-1] if flip else q)
            if j == 0:
                fcols.append((PAL_FOAM, PAL_EFFECTS))
            else:
                fcols.append((PAL_HAZE, 0) if streak else (PAL_WATER, 0))
    replace_object("chasm_falls", fverts, ffaces, fcols, material, coll)

    bm = bmesh.new()
    bm.from_mesh(terrain.data)
    bad = sum(1 for e in bm.edges if len(e.link_faces) > 2)
    open_edges = sum(1 for e in bm.edges if len(e.link_faces) == 1)
    not_quads = sum(1 for f in bm.faces if len(f.verts) != 4)
    bm.free()
    print("terrain: %d quads (%d not quads), %d verts, %d rings, %d edges with >2 faces, %d open edges "
          "(the skirt's foot), %d fall segments, %.1f s"
          % (len(out_faces), not_quads, len(verts), R, bad, open_edges, len(falls), time.time() - t0))
    lip_points = [(pos[a][0], pos[a][1]) for a, _, _ in falls]
    return terrain, lip_points


def replace_object(name, verts, faces, cols, material, coll):
    """Builds `name` from quads with one palette cell per face, replacing any object of that name."""
    old = bpy.data.objects.get(name)
    if old is not None:
        old_mesh = old.data
        bpy.data.objects.remove(old, do_unlink=True)
        if old_mesh.users == 0:
            bpy.data.meshes.remove(old_mesh)
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], faces)
    uv = me.uv_layers.new(name="UVMap")
    flat = []
    for f, (c, r) in zip(faces, cols):
        flat.extend(pal_uv(c, r) * len(f))
    uv.data.foreach_set("uv", flat)
    me.polygons.foreach_set("use_smooth", [False] * len(faces))
    me.validate()
    me.update()
    me.materials.append(material)
    obj = bpy.data.objects.new(name, me)
    coll.objects.link(obj)
    return obj


# ---------------------------------------------------------------------------------------------
# Props and the stage: only for judging the look. The game places its own.
# ---------------------------------------------------------------------------------------------

def palette_material():
    mat = bpy.data.materials.get("palette")
    if mat:
        return mat
    mat = bpy.data.materials.new("palette")
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    bsdf = nodes["Principled BSDF"]
    bsdf.inputs["Roughness"].default_value = 1.0
    bsdf.inputs["Specular IOR Level"].default_value = 0.0
    tex = nodes.new("ShaderNodeTexImage")
    tex.image = bpy.data.images.load(PALETTE)
    tex.interpolation = "Closest"
    tex.location = (-350, 250)
    mat.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    return mat


def volume_nodes(name):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    nodes.remove(nodes["Principled BSDF"])
    vol = nodes.new("ShaderNodeVolumePrincipled")
    vol.inputs["Color"].default_value = (0.95, 0.96, 0.98, 1.0)
    mat.node_tree.links.new(vol.outputs["Volume"], nodes["Material Output"].inputs["Volume"])
    return mat, nodes, mat.node_tree.links, vol


def set_density(nodes, links, vol, density, glow):
    """Density, and emission in proportion to it. Deep in the chasm the walls shade the volume from the
    sun, and lit by that alone a cloud reads as grey smoke. Emission is per length, not per density, so
    a flat strength would fill a long view with white; tied to density, only the cloud itself glows."""
    links.new(density, vol.inputs["Density"])
    vol.inputs["Emission Color"].default_value = (0.86, 0.91, 1.0, 1.0)
    links.new(math_node(nodes, links, "MULTIPLY", density, value=glow), vol.inputs["Emission Strength"])


def math_node(nodes, links, op, a, b=None, value=None):
    n = nodes.new("ShaderNodeMath")
    n.operation = op
    links.new(a, n.inputs[0])
    if b is not None:
        links.new(b, n.inputs[1])
    elif value is not None:
        n.inputs[1].default_value = value
    return n.outputs[0]


def map_range(nodes, links, a, lo, hi, to_lo=0.0, to_hi=1.0):
    n = nodes.new("ShaderNodeMapRange")
    n.clamp = True
    n.inputs["From Min"].default_value = lo
    n.inputs["From Max"].default_value = hi
    n.inputs["To Min"].default_value = to_lo
    n.inputs["To Max"].default_value = to_hi
    links.new(a, n.inputs["Value"])
    return n.outputs["Result"]


def noise(nodes, links, vector, scale, detail=3.0):
    n = nodes.new("ShaderNodeTexNoise")
    n.inputs["Scale"].default_value = scale
    n.inputs["Detail"].default_value = detail
    links.new(vector, n.inputs["Vector"])
    return n.outputs["Fac"]


def steam_material():
    """A puff of steam: densest at its middle, fraying at its edge, and as thick as its object's colour
    alpha says - so one material serves a plume from its dense foot to its thin top."""
    mat, nodes, links, vol = volume_nodes("steam")
    coords = nodes.new("ShaderNodeTexCoord")
    info = nodes.new("ShaderNodeObjectInfo")
    r = nodes.new("ShaderNodeVectorMath")
    r.operation = "LENGTH"
    links.new(coords.outputs["Object"], r.inputs[0])
    core = map_range(nodes, links, r.outputs["Value"], 1.0, 0.2)
    fray = map_range(nodes, links, noise(nodes, links, coords.outputs["Object"], 2.2), 0.35, 0.65, 0.2, 1.0)
    d = math_node(nodes, links, "MULTIPLY", core, fray)
    d = math_node(nodes, links, "MULTIPLY", d, info.outputs["Alpha"])
    d = math_node(nodes, links, "MULTIPLY", d, value=1.3)
    set_density(nodes, links, vol, d, 0.9)
    return mat


def cloud_deck(coll, z_top=MIST_TOP + 2.0, z_bottom=MIST_TOP - 16.0, density=0.9):
    """The deck the chasm drowns in: a slab of volume whose density is noise cut into billows, clear at
    its top and solid toward its bottom."""
    me = bpy.data.meshes.new("cloud_deck")
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=2.0)
    bm.to_mesh(me)
    bm.free()
    obj = bpy.data.objects.new("cloud_deck", me)
    obj.location = (0.0, 0.5 * (Y0 + Y1), 0.5 * (z_top + z_bottom))
    obj.scale = (85.0, 0.5 * (Y1 - Y0), 0.5 * (z_top - z_bottom))
    coll.objects.link(obj)
    mat, nodes, links, vol = volume_nodes("cloud_deck")
    coords = nodes.new("ShaderNodeTexCoord")
    geo = nodes.new("ShaderNodeNewGeometry")
    split = nodes.new("ShaderNodeSeparateXYZ")
    links.new(coords.outputs["Object"], split.inputs[0])
    depth = map_range(nodes, links, split.outputs["Z"], 1.0, -0.3)
    billow = map_range(nodes, links, noise(nodes, links, geo.outputs["Position"], 0.055, 4.0), 0.44, 0.60)
    #The billows reach higher where the noise is thick: the deck's top is lumpy, not a ceiling.
    lumpy = math_node(nodes, links, "MULTIPLY", depth, billow)
    lumpy = math_node(nodes, links, "POWER", lumpy, value=0.7)
    d = math_node(nodes, links, "MULTIPLY", lumpy, value=density)
    set_density(nodes, links, vol, d, 1.2)
    me.materials.append(mat)
    obj.hide_select = True
    return obj


def spire_mesh(name, rng, height, radius, material):
    """A spire of rock standing out of the deck, in the walls' own idiom: a stack of five-sided blocks,
    each narrower than the one under it and set off-centre on it, the step between them a ledge (or an
    overhang), the top broken off on a slant. All quads but the top. A smooth taper read as a carrot."""
    sides = 5
    blocks = rng.randint(4, 6)
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new("UVMap")
    flat = rng.uniform(0.6, 0.85)
    angles = [rng.uniform(0, 6.28) + 2 * math.pi * i / sides + rng.uniform(-0.2, 0.2) for i in range(sides)]
    spokes = [rng.uniform(0.85, 1.12) for _ in range(sides)]
    tilt = rng.uniform(0.3, 0.7) * radius
    heights = [rng.uniform(0.75, 1.25) for _ in range(blocks)]
    heights = [h * height / sum(heights) for h in heights]

    def ring(cx, cy, r, z, slant=0.0):
        return [bm.verts.new((cx + math.cos(a) * r * s, cy + math.sin(a) * r * s * flat, z + math.cos(a) * slant))
                for a, s in zip(angles, spokes)]

    sides_faces, steps = [], []
    cx = cy = z = 0.0
    prev = None
    for j in range(blocks):
        r = radius * (1.0 - 0.5 * j / blocks) * rng.uniform(0.9, 1.04)
        if j > 0:
            cx += rng.uniform(-0.16, 0.16) * radius
            cy += rng.uniform(-0.16, 0.16) * radius
        bottom = ring(cx, cy, r, z + (0.3 if j > 0 else 0.0))
        z += heights[j]
        top = ring(cx + rng.uniform(-0.05, 0.05) * r, cy, r * 0.93, z, tilt if j == blocks - 1 else 0.0)
        for i in range(sides):
            k = (i + 1) % sides
            sides_faces.append((bm.faces.new((bottom[i], bottom[k], top[k], top[i])), j))
            if prev is not None:
                steps.append(bm.faces.new((prev[i], prev[k], bottom[k], bottom[i])))
        prev = top
    cap = bm.faces.new(prev)
    bm.normal_update()
    shades = (PAL_ROCK_3, PAL_ROCK_1, PAL_ROCK_0, PAL_ROCK_3, PAL_ROCK_1, PAL_ROCK_0)
    for f, j in sides_faces:
        for loop in f.loops:
            loop[uv].uv = pal_uv(shades[j])
    for f in steps:
        col = PAL_ROCK_0 if f.normal.z > 0.2 else PAL_ROCK_2
        for loop in f.loops:
            loop[uv].uv = pal_uv(col)
    for loop in cap.loops:
        loop[uv].uv = pal_uv(PAL_GRASS_0 + 2 if rng.random() < 0.5 else PAL_ROCK_0)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    me.polygons.foreach_set("use_smooth", [False] * len(me.polygons))
    me.materials.append(material)
    return me


def load_props(names):
    if not os.path.exists(PROPS_BLEND):
        return {}
    with bpy.data.libraries.load(PROPS_BLEND, link=False) as (src, dst):
        dst.meshes = [n for n in names if n in src.meshes]
    return {m.name: m for m in dst.meshes if m}


def place(coll, name, mesh, at, yaw=0.0, scale=1.0, sz=None):
    obj = bpy.data.objects.new(name, mesh)
    obj.location = at
    obj.rotation_euler = (0.0, 0.0, yaw)
    obj.scale = (scale, scale, scale if sz is None else sz)
    coll.objects.link(obj)
    return obj


def inside_chasm(x, y):
    return min(x - rim_west(y), rim_east(y) - x)


def props(rng, coll, material, P, quads, tops, wet, lip_points):
    #Neighbours, to keep trees off rims.
    nbr = [set() for _ in P]
    for q in quads:
        for i in range(4):
            a, b = q[i], q[(i + 1) % 4]
            nbr[a].add(b)
            nbr[b].add(a)

    meshes = load_props(["tree_pine_a", "tree_pine_b", "tree_pine_c", "tree_pine_d", "tree_oak_a",
                         "tree_oak_b", "tree_oak_c", "rock_a", "rock_b", "rock_c"])
    pines = [m for n, m in meshes.items() if n.startswith("tree_pine")]
    oaks = [m for n, m in meshes.items() if n.startswith("tree_oak")]
    rocks = [m for n, m in meshes.items() if n.startswith("rock")]
    trees = stones = 0
    for i, (x, y) in enumerate(P):
        t = tops[i]
        if wet[i] or t < -24.5 or abs(t - RIVER_BED) < EPS or any(tops[j] != t for j in nbr[i]):
            continue
        if t == 0.0:
            forest = vnoise(x / 38.0, y / 38.0, 90)
            p = 0.03 + 0.55 * smoothstep(0.42, 0.68, forest)
        else:
            p = 0.22
        if pines and rng.random() < p:
            pool = oaks if (oaks and rng.random() < 0.18) else pines
            place(coll, "tree_%05d" % trees, rng.choice(pool),
                  (x + rng.uniform(-0.3, 0.3), y + rng.uniform(-0.3, 0.3), t),
                  rng.uniform(0, 6.28), 0.7 * rng.uniform(0.85, 1.2))
            trees += 1
        elif rocks and -16.0 < t < -2.0 and rng.random() < 0.10:
            #Floatstone, as a hint: pale stones on the low patches and balconies.
            place(coll, "floatstone_%04d" % stones, rng.choice(rocks), (x, y, t), rng.uniform(0, 6.28),
                  rng.uniform(0.45, 0.8))
            stones += 1

    #The cloud deck and the steam are VOLUMES here: the preview is for judging the depth, and opaque lumps
    #read as boulders from above. The game draws its own (Mist.cpp).
    deck = cloud_deck(coll)
    sphere = bpy.data.meshes.new("steam_puff")
    bm = bmesh.new()
    bmesh.ops.create_icosphere(bm, subdivisions=2, radius=1.0)
    bm.to_mesh(sphere)
    bm.free()
    sphere.materials.append(steam_material())

    #Plumes rising out of the deck along the walls and round the islands, leaning on the wind, thinning.
    plumes = []
    for k in range(12):
        side = k % 2
        y = Y0 + 15.0 + (Y1 - Y0 - 30.0) * (k + rng.uniform(0.1, 0.9)) / 12.0
        x = rim_west(y) + rng.uniform(4, 9) if side == 0 else rim_east(y) - rng.uniform(4, 9)
        plumes.append((x, y, rng.randint(6, 10)))
    for dx, yc, rx, ry, *_ in ISLANDS[:2]:
        plumes.append((spine(yc) + dx + rx + 3.0, yc - 2.0, 7))
    for x, y in lip_points[::4]:
        plumes.append((x + (-3.0 if x > 0 else 3.0), y, 4))       #spray where a fall lands
    puffs = 0
    for p, (x, y, height) in enumerate(plumes):
        height = int(height * 1.4)
        for k in range(height):
            #Narrow and tall at the foot, spreading and thinning as it climbs: a wisp, not a ball.
            r = 1.5 + 0.42 * k + rng.uniform(-0.25, 0.25)
            obj = place(coll, "steam_%02d_%02d" % (p, k), sphere,
                        (x + 0.6 * k + rng.uniform(-0.5, 0.5), y + 0.25 * k + rng.uniform(-0.5, 0.5),
                         MIST_TOP - 1.0 + 2.6 * k), rng.uniform(0, 6.28), r, r * 1.5)
            obj.color = (1.0, 1.0, 1.0, 0.75 * max(0.1, 1.0 - k / (height + 1.0)) ** 1.3)
            obj.hide_select = True
            puffs += 1

    #Spires out of the deck, clear of the islands and the walls' broken columns.
    spires = []
    tries = 0
    while len(spires) < 9 and tries < 400:
        tries += 1
        y = rng.uniform(Y0 + 10, Y1 - 10)
        x = rng.uniform(rim_west(y), rim_east(y))
        side = 0 if x - rim_west(y) < rim_east(y) - x else 1
        if inside_chasm(x, y) < fringe_width(y, side) + 5.0 or island_steps(x, y) is not None:
            continue
        if any(math.hypot(x - sx, y - sy) < 14 for sx, sy in spires):
            continue
        if any(math.hypot(x - px, y - py) < 8 for px, py, _, _ in PILLARS):
            continue
        if any(math.hypot(x - spine(yc) - dx, y - yc) < max(rx, ry) + 8 for dx, yc, rx, ry, *_ in ISLANDS):
            continue
        spires.append((x, y))
        h = rng.uniform(26, 44)
        me = spire_mesh("spire_%d" % len(spires), rng, h, rng.uniform(2.6, 4.2), material)
        place(coll, "spire_%d" % len(spires), me, (x, y, -56.0), rng.uniform(0, 6.28))
    print("props: %d trees, %d floatstones, %d plumes of %d puffs, %d spires" % (trees, stones, len(plumes), puffs, len(spires)))


#Sunlight from the north-west (Blender's -X +Y), about 55 degrees up - the README's lighting.
SUN_TRAVEL = Vector((1.0, -1.0, -2.0)).normalized()


def add_camera(coll, name, target, elev_deg, yaw_deg, dist, lens):
    e, y = math.radians(elev_deg), math.radians(yaw_deg)
    off = Vector((math.sin(y) * math.cos(e), -math.cos(y) * math.cos(e), math.sin(e))) * dist
    cam = bpy.data.objects.new(name, bpy.data.cameras.new(name))
    cam.data.lens = lens
    cam.data.clip_end = 3000.0
    cam.location = Vector(target) + off
    cam.rotation_euler = (-off).to_track_quat("-Z", "Y").to_euler()
    coll.objects.link(cam)
    return cam


def depth_fog(coll, name, x0, x1, y0, y1, z_top=-3.0, z_bottom=-60.0, density=0.10):
    """The abyss filling with mist, thicker the deeper it goes - what the game's Mist does for the chasm.
    Preview only."""
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=2.0)
    bm.to_mesh(me)
    bm.free()
    obj = bpy.data.objects.new(name, me)
    obj.location = (0.5 * (x0 + x1), 0.5 * (y0 + y1), 0.5 * (z_top + z_bottom))
    obj.scale = (0.5 * (x1 - x0), 0.5 * (y1 - y0), 0.5 * (z_top - z_bottom))
    coll.objects.link(obj)
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    nodes.remove(nodes["Principled BSDF"])
    vol = nodes.new("ShaderNodeVolumePrincipled")
    vol.inputs["Color"].default_value = (0.86, 0.90, 0.95, 1.0)
    coords = nodes.new("ShaderNodeTexCoord")
    split = nodes.new("ShaderNodeSeparateXYZ")
    ramp = nodes.new("ShaderNodeMapRange")
    ramp.inputs["From Min"].default_value = 1.0
    ramp.inputs["From Max"].default_value = -1.0
    power = nodes.new("ShaderNodeMath")
    power.operation = "POWER"
    power.inputs[1].default_value = 1.6
    scale = nodes.new("ShaderNodeMath")
    scale.operation = "MULTIPLY"
    scale.inputs[1].default_value = density
    links.new(coords.outputs["Object"], split.inputs[0])
    links.new(split.outputs["Z"], ramp.inputs["Value"])
    links.new(ramp.outputs["Result"], power.inputs[0])
    links.new(power.outputs["Value"], scale.inputs[0])
    links.new(scale.outputs["Value"], vol.inputs["Density"])
    links.new(vol.outputs["Volume"], nodes["Material Output"].inputs["Volume"])
    me.materials.append(mat)
    obj.hide_select = True
    return obj


def wire_overlay(coll, terrain, centre, half):
    """The terrain's edges round `centre`, as thin dark bars - for the topology preview only."""
    bm = bmesh.new()
    bm.from_mesh(terrain.data)
    far = [f for f in bm.faces
           if abs(f.calc_center_median().x - centre[0]) > half or abs(f.calc_center_median().y - centre[1]) > half]
    bmesh.ops.delete(bm, geom=far, context="FACES")
    me = bpy.data.meshes.new("wire_overlay")
    bm.to_mesh(me)
    bm.free()
    obj = bpy.data.objects.new("wire_overlay", me)
    coll.objects.link(obj)
    mod = obj.modifiers.new("wire", "WIREFRAME")
    mod.thickness = 0.045
    mod.use_even_offset = True
    mod.use_relative_offset = False
    mod.offset = 0.0
    mat = bpy.data.materials.new("wire")
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (0.02, 0.02, 0.03, 1.0)
    bsdf.inputs["Roughness"].default_value = 1.0
    me.materials.append(mat)
    obj.hide_render = True
    obj.hide_set(True)
    return obj


def stage(coll):
    sun = bpy.data.objects.new("sun_nw", bpy.data.lights.new("sun_nw", "SUN"))
    sun.data.energy = 2.6
    sun.data.angle = math.radians(1.5)
    sun.rotation_euler = SUN_TRAVEL.to_track_quat("-Z", "Y").to_euler()
    coll.objects.link(sun)
    world = bpy.data.worlds.new("sky")
    world.use_nodes = True
    bg = world.node_tree.nodes["Background"]
    bg.inputs["Color"].default_value = (0.62, 0.74, 0.86, 1.0)
    bg.inputs["Strength"].default_value = 0.85
    bpy.context.scene.world = world
    depth_fog(coll, "depth_fog", -75.0, 75.0, Y0, Y1)
    yb = -36.0
    yw = 10.0
    return {
        "game": add_camera(coll, "cam_game", (0.0, -5.0, -10.0), 55, 0, 165, 50),
        "overview": add_camera(coll, "cam_overview", (0.0, 0.0, -12.0), 58, 0, 420, 40),
        "oblique": add_camera(coll, "cam_oblique", (0.0, 22.0, -16.0), 22, -12, 140, 30),
        "close": add_camera(coll, "cam_close", (rim_east(yb) - 6.0, yb, -10.0), 30, -62, 55, 32),
        "wire": add_camera(coll, "cam_wire", (rim_west(yw) + 2.0, yw, -5.0), 38, 68, 34, 32),
    }


def render(names, quick):
    scene = bpy.context.scene
    for engine in ("BLENDER_EEVEE_NEXT", "BLENDER_EEVEE"):
        try:
            scene.render.engine = engine
            break
        except TypeError:
            continue
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    scene.render.resolution_x, scene.render.resolution_y = 1600, 900
    for key, value in [("use_raytracing", True), ("ray_tracing_method", "SCREEN"), ("use_fast_gi", True),
                       ("fast_gi_method", "AMBIENT_OCCLUSION_ONLY"), ("fast_gi_distance", 6.0),
                       ("volumetric_end", 1400.0), ("volumetric_tile_size", "4" if not quick else "8"),
                       ("volumetric_samples", 64 if not quick else 24)]:
        try:
            setattr(scene.eevee, key, value)
        except (AttributeError, TypeError):
            print("eevee has no %s here" % key)
    scene.render.image_settings.file_format = "PNG"
    try:
        scene.eevee.taa_render_samples = 8 if quick else 32
        scene.eevee.shadow_resolution_scale = 1.0
    except AttributeError:
        pass
    os.makedirs(PREVIEWS, exist_ok=True)
    wire = bpy.data.objects.get("wire_overlay")
    for name in names:
        cam = bpy.data.objects.get("cam_" + name)
        if cam is None:
            print("no camera cam_%s" % name)
            continue
        scene.camera = cam
        if wire:
            wire.hide_render = name != "wire"
        scene.render.filepath = os.path.join(PREVIEWS, "terraces_%s.png" % name)
        t0 = time.time()
        bpy.ops.render.render(write_still=True)
        print("rendered %s in %.0f s" % (scene.render.filepath, time.time() - t0))


ALL_VIEWS = ["game", "overview", "oblique", "close", "wire"]


def views_from(argv, flag):
    i = argv.index(flag)
    if i + 1 < len(argv) and not argv[i + 1].startswith("--"):
        return argv[i + 1].split(",")
    return ALL_VIEWS


def rebuild():
    plots = bpy.data.objects.get("chasm_plots")
    if plots is None:
        print("no chasm_plots in this file")
        return
    coll = bpy.data.collections.get("Terrain") or bpy.context.scene.collection
    build_terrain(plots, coll, palette_material())


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if "--rebuild" in argv or (not bpy.app.background and bpy.data.objects.get("chasm_plots")):
        rebuild()
        return
    if "--render-only" in argv:
        bpy.ops.wm.open_mainfile(filepath=BLEND)
        render(views_from(argv, "--render-only"), "--quick" in argv)
        return
    if os.path.exists(BLEND) and "--force" not in argv:
        print("\n%s exists and may be edited by hand; pass --force to rebuild it from scratch." % BLEND)
        sys.exit(1)

    t0 = time.time()
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    material = palette_material()
    colls = {}
    for name in ("Plots", "Terrain", "Props", "Stage"):
        colls[name] = bpy.data.collections.new(name)
        scene.collection.children.link(colls[name])

    rng = random.Random(2026)
    P, quads = make_grid(rng)
    tops, wet, rivers, lips = generate_tops(P, quads)
    joined = fix_saddles(quads, tops)
    print("grid: %d plots, %d fine quads, %d saddles joined, %.1f s" % (len(P), len(quads), joined, time.time() - t0))
    counts = {}
    for t in tops:
        counts[t] = counts.get(t, 0) + 1
    print("plots by height: " + ", ".join("%g:%d" % (t, counts[t]) for t in sorted(counts, reverse=True)))

    me = bpy.data.meshes.new("chasm_plots")
    me.from_pydata([(x, y, t) for (x, y), t in zip(P, tops)], [], quads)
    me.update()
    plots = bpy.data.objects.new("chasm_plots", me)
    plots.display_type = "WIRE"
    plots["about"] = ("One vertex per plot, z = the top of its column (0 plateau, -12 balcony, -24 island, "
                      "-72 floor, -1 river bed; steps of 3). Edit, then run the rebuild_terraces text block.")
    plots["rivers"] = json.dumps([[(round(x, 3), round(y, 3)) for x, y in r] for r in rivers])
    colls["Plots"].objects.link(plots)
    plots.hide_render = True

    terrain, lip_points = build_terrain(plots, colls["Terrain"], material)
    props(random.Random(7), colls["Props"], material, P, quads, tops, wet, lip_points)
    stage(colls["Stage"])
    wire_overlay(colls["Stage"], terrain, (rim_west(10.0) + 2.0, 10.0), 16.0)
    plots.hide_set(True)

    text = bpy.data.texts.new("rebuild_terraces")
    text.write(STUB)

    os.makedirs(os.path.dirname(BLEND), exist_ok=True)
    bpy.data.images["palette.png"].filepath = "//" + os.path.relpath(PALETTE, os.path.dirname(BLEND)).replace("\\", "/")
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=BLEND, relative_remap=False)
    print("wrote %s (%d bytes) in %.1f s" % (BLEND, os.path.getsize(BLEND), time.time() - t0))

    if "--render" in argv:
        render(views_from(argv, "--render"), "--quick" in argv)


main()
