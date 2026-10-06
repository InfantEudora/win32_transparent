"""Builds art_source/chasm/chasm_cliffs.blend - the chasm's cliff kit - exports it, and renders previews.

    blender.exe -b --python apps/chasm/tools/blender_chasm_cliffs.py -- [--force] [--render]

Writes the .blend, apps/chasm/assets/meshes/chasm_cliffs.glb (the "Export" collection only) and, with
--render, art_source/chasm/previews/cliffs_*.png. Refuses to overwrite the .blend without --force, as
blender_chasm_props.py does: once someone edits it by hand, a rebuild throws those edits away.

THE TARGET is the chasm in chasm_aigen_1.png, not its houses: blocky rock in faceted vertical columns,
broken by strata into steps; a grass cap with a band of soil under it that overhangs the face a
little; small grassy terraces stepping down the wall; a natural arch where the rim bridges an inlet;
waterfalls through notches in the rim, and springs coming out of the face itself.

ONE MESHER MAKES ALL OF IT (Relief). A path runs along the rim, the chasm on its right. Across it the
wall is cut into COLUMNS (irregular widths along the path) and down it into BANDS (strata, irregular
heights). Each cell is either empty or holds rock between a back depth and a front depth, measured
out from the path; each column has a chamfered front profile, each cell its own tilt, each band its
own lean. The mesh is the cells' front faces plus whatever their differences expose - a ledge where
a lower cell stands out, an overhang where a higher one does, a column's side where its neighbour
is set back or missing. Every other feature is an edit of that grid: an arch empties cells under a
two-sided lintel, a notch empties the cap and grooves the rock below it, a spring recesses one cell.
A closed path makes a free-standing island or terrace the same way. Because it is a grid of numbers,
not a sculpt, the same idea ports directly to TerrainMesh.cpp's walls if the look is wanted there.

COLOURS follow blender_chasm_props.py: flat faces, every face one palette cell (Palette.h), row 0 for
rock and grass, the effects row (15) for foam and mist. Water gets its own objects, so the game can
draw it with a moving shader without touching the rock.

UNITS are the game's: the plateau at z = 0, a balcony at -12, an island at -24, the mist from about
-34 down (Terrain.cpp's terrain_levels). Kit pieces face -Y, the rim along X at y = 0, origin on the
rim at the piece's middle.
"""

import math
import os
import random
import sys

import bmesh
import bpy
from mathutils import Vector
from mathutils.geometry import delaunay_2d_cdt

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
BLEND = os.path.join(REPO, "art_source", "chasm", "chasm_cliffs.blend")
PROPS_BLEND = os.path.join(REPO, "art_source", "chasm", "chasm_props.blend")
GLB = os.path.join(REPO, "apps", "chasm", "assets", "meshes", "chasm_cliffs.glb")
PALETTE = os.path.join(REPO, "apps", "chasm", "assets", "textures", "palette.png")
PREVIEWS = os.path.join(REPO, "art_source", "chasm", "previews")

#Palette.h - must agree with it.
PALETTE_COLS = 32
PALETTE_ROWS = 16
PAL_GRASS_0 = 0
PAL_LIP = 4
PAL_ROCK_0 = 5          #b08c68, the lightest
PAL_ROCK_1 = 6          #947054
PAL_ROCK_2 = 7          #785c48, the darkest: undersides and recesses
PAL_ROCK_3 = 8          #a47c5a
PAL_EARTH = 11
PAL_WATER = 14
PAL_HAZE = 15           #a pale blue-grey: the light streaks in a fall
PAL_EFFECTS = 15        #row
PAL_MIST_LIGHT = 0      #columns in the effects row
PAL_MIST = 1
PAL_FOAM = 3
PAL_VOID = 5            #black: the inside of a fresh crack

STRATA = [PAL_ROCK_0, PAL_ROCK_3, PAL_ROCK_1, PAL_ROCK_3, PAL_ROCK_0, PAL_ROCK_1]

CAP_GRASS = 0.35        #the turf band's height
CAP_SOIL = 0.80         #the soil band under it
TOP_STEP = 2.5          #how finely a plateau strip is cut, for its bumps
TOP_BUMP = 0.14         #bumps only go DOWN: a strip never rises above a face that meets it at z = 0
MIST_Z = -34.0


def pal_uv(col, row=0):
    #Blender's V runs up from the image's bottom; the engine's row 0 is the top row. The glTF exporter
    #flips V on the way out, so this lands on Palette.h's PaletteUV() in the file.
    return ((col + 0.5) / PALETTE_COLS, 1.0 - (row + 0.5) / PALETTE_ROWS)


def grass(rng):
    return PAL_GRASS_0 + rng.randrange(4)


def hash2(ix, iy, seed):
    h = (ix * 374761393 + iy * 668265263 + seed * 982451653) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((h ^ (h >> 16)) & 0xFFFF) / 65535.0


def vnoise(x, y, seed=0):
    """Value noise in 0..1. A function of position alone, so two meshes that share a vertex agree on
    its height without knowing about each other."""
    ix, iy = math.floor(x), math.floor(y)
    fx, fy = x - ix, y - iy
    fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    a, b = hash2(ix, iy, seed), hash2(ix + 1, iy, seed)
    c, d = hash2(ix, iy + 1, seed), hash2(ix + 1, iy + 1, seed)
    return (a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy


def ground_z(x, y):
    """The plateau's height well back from the rim: what bump() comes to once its fade is done."""
    return -TOP_BUMP * vnoise(x / 3.2, y / 3.2, 7)


def bump(p, w):
    #Fades out toward the rim, so the lip stays a crisp line.
    fade = max(0.0, min(1.0, (-w - 1.0) / 2.0))
    return -TOP_BUMP * vnoise(p.x / 3.2, p.y / 3.2, 7) * fade


# ---------------------------------------------------------------------------------------------
# Geometry accumulator: loose faces, welded when they become an object.
# ---------------------------------------------------------------------------------------------

def newell(pts):
    n = Vector((0, 0, 0))
    for a, b in zip(pts, pts[1:] + pts[:1]):
        n.x += (a.y - b.y) * (a.z + b.z)
        n.y += (a.z - b.z) * (a.x + b.x)
        n.z += (a.x - b.x) * (a.y + b.y)
    return n


class Geo:
    def __init__(self):
        self.faces = []

    def add(self, pts, pal, want=None, row=0):
        clean = []
        for p in pts:
            if not clean or (p - clean[-1]).length > 1e-5:
                clean.append(p.copy())
        if len(clean) > 2 and (clean[0] - clean[-1]).length <= 1e-5:
            clean.pop()
        if len(clean) < 3:
            return
        n = newell(clean)
        if n.length < 1e-7:
            return
        if want is not None and n.dot(want) < 0:
            clean.reverse()
        self.faces.append((clean, pal, row))

    def extend(self, other):
        self.faces += other.faces

    def transform(self, fn):
        """Moves every point by `fn`. Keeps winding, so only for maps that keep orientation."""
        self.faces = [([fn(p) for p in pts], pal, row) for pts, pal, row in self.faces]

    def hull(self, points, pal, row=0, drop_down=False):
        """A convex lump; `pal` is a palette cell, or a function of the face's normal."""
        bm = bmesh.new()
        verts = [bm.verts.new(p) for p in points]
        res = bmesh.ops.convex_hull(bm, input=verts, use_existing_faces=False)
        loose = list({g for g in res["geom_interior"] + res["geom_unused"] if isinstance(g, bmesh.types.BMVert)})
        bmesh.ops.delete(bm, geom=loose, context="VERTS")
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
        for f in bm.faces:
            f.normal_update()
            if drop_down and f.normal.z < -0.6:
                continue
            self.add([v.co.copy() for v in f.verts], pal(f.normal) if callable(pal) else pal, None, row)
        bm.free()


def bridge(geo, P, a0, a1, b0, b1, want, pal_of, row=0):
    """The face between two lines that share their ends' positions along an interface and differ in
    depth: a at depth a0..a1, b at b0..b1. Where a stands further out the face looks along `want`,
    where b does it looks the other way; if they cross, it is two triangles."""
    d0, d1 = a0 - b0, a1 - b1
    eps = 1e-4
    if abs(d0) < eps and abs(d1) < eps:
        return
    if d0 * d1 >= 0 or abs(d0) < eps or abs(d1) < eps:
        sign = 1.0 if d0 + d1 > 0 else -1.0
        geo.add([P(0, a0), P(1, a1), P(1, b1), P(0, b0)], pal_of(sign, max(abs(d0), abs(d1))), want * sign, row)
        return
    x = d0 / (d0 - d1)
    wc = a0 + (a1 - a0) * x
    s0 = 1.0 if d0 > 0 else -1.0
    geo.add([P(0, a0), P(x, wc), P(0, b0)], pal_of(s0, abs(d0)), want * s0, row)
    geo.add([P(x, wc), P(1, a1), P(1, b1)], pal_of(-s0, abs(d1)), want * -s0, row)


# ---------------------------------------------------------------------------------------------
# Paths: a smooth line along a rim, or a closed loop around an island.
# ---------------------------------------------------------------------------------------------

DEEP_FROM = 1.5         #behind the rim further than this, a point goes along the smoothed normal
DEEP_SMOOTH = 6.0


def across(src, dst, s):
    """The point on rim `dst` facing s on rim `src`, where one is the other's crack carried across the
    gap - the same control points, walked the other way. Columns, slots and features are matched
    through this."""
    k, f = src.param_at(s)
    return dst.s_at_param(len(src.ctrl_s) - 2 - k, 1.0 - f)


def catmull(p0, p1, p2, p3, t):
    t2, t3 = t * t, t * t * t
    return 0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3)


class Path:
    """Walked with the plateau on the left and the chasm on the right, so `normal` - out of the face -
    is the tangent turned clockwise. A closed loop walked counter-clockwise seen from above has its
    normal pointing out, the same rule."""

    def __init__(self, points, closed=False, step=0.2, sharp=False):
        """`sharp` runs straight from point to point: the corners of a fresh break, where a smooth
        curve would be ground worn down over ages."""
        ctrl = [Vector((p[0], p[1], 0.0)) for p in points]
        n = len(ctrl)
        self.closed = closed
        self.sharp = sharp
        self.ctrl = [(p[0], p[1]) for p in points]
        samples, self.ctrl_i = [], []
        for i in range(n if closed else n - 1):
            self.ctrl_i.append(len(samples))
            p0 = ctrl[(i - 1) % n] if closed else ctrl[max(i - 1, 0)]
            p1, p2 = ctrl[i % n], ctrl[(i + 1) % n]
            p3 = ctrl[(i + 2) % n] if closed else ctrl[min(i + 2, n - 1)]
            k = max(2, int((p2 - p1).length / step))
            for m in range(k):
                samples.append(p1.lerp(p2, m / k) if sharp else catmull(p0, p1, p2, p3, m / k))
        samples.append(samples[0].copy() if closed else ctrl[-1])
        self.pts = samples
        self.s = [0.0]
        for a, b in zip(samples, samples[1:]):
            self.s.append(self.s[-1] + (b - a).length)
        self.length = self.s[-1]
        self.ctrl_s = [self.s[k] for k in self.ctrl_i] + [self.length]

    def point(self, s):
        if self.closed:
            s %= self.length
        else:
            s = max(0.0, min(self.length, s))
        lo, hi = 0, len(self.s) - 1
        while hi - lo > 1:
            mid = (lo + hi) // 2
            if self.s[mid] <= s:
                lo = mid
            else:
                hi = mid
        span = self.s[hi] - self.s[lo]
        t = 0.0 if span <= 0 else (s - self.s[lo]) / span
        return self.pts[lo].lerp(self.pts[hi], t)

    def tangent(self, s, h=0.6):
        if not self.closed:
            a, b = max(0.0, s - h), min(self.length, s + h)
        else:
            a, b = s - h, s + h
        return (self.point(b) - self.point(a)).normalized()

    def normal(self, s):
        t = self.tangent(s)
        return Vector((t.y, -t.x, 0.0))

    def deep_normal(self, s):
        """The normal smoothed over DEEP_SMOOTH either way: what a point far behind the rim is placed
        along, so that two neighbouring columns' lines back from a sharp corner do not cross."""
        t = self.tangent(s, DEEP_SMOOTH)
        return Vector((t.y, -t.x, 0.0))

    def place(self, s, n, w):
        """The point `w` out from the rim at s: along `n` for the first DEEP_FROM behind the rim, then
        along the smoothed normal. Everything that stands on a rim's frame places itself through this,
        so the walls, their plateau strips, the rivers and the ground's hole all agree."""
        p = self.point(s)
        if w >= -DEEP_FROM:
            return Vector((p.x + n.x * w, p.y + n.y * w, 0.0))
        d = self.deep_normal(s)
        return Vector((p.x - n.x * DEEP_FROM + d.x * (w + DEEP_FROM), p.y - n.y * DEEP_FROM + d.y * (w + DEEP_FROM), 0.0))

    def at(self, s, w, z):
        q = self.place(s, self.normal(s), w)
        q.z = z
        return q

    def param_at(self, s):
        """Which control segment s falls in, and how far along it."""
        k = 0
        while k < len(self.ctrl_s) - 2 and self.ctrl_s[k + 1] <= s:
            k += 1
        span = self.ctrl_s[k + 1] - self.ctrl_s[k]
        return k, (0.0 if span <= 0 else (s - self.ctrl_s[k]) / span)

    def s_at_param(self, k, f):
        return self.ctrl_s[k] + f * (self.ctrl_s[k + 1] - self.ctrl_s[k])

    def s_at_x(self, x):
        best = min(range(len(self.pts)), key=lambda i: abs(self.pts[i].x - x))
        return self.s[best]


def wiggle_line(rng, x0, x1, y, amp, step, direction=1):
    xs = [x0 + (x1 - x0) * k / max(1, round(abs(x1 - x0) / step)) for k in range(round(abs(x1 - x0) / step) + 1)]
    pts = [(x, y + rng.uniform(-amp, amp)) for x in xs]
    return pts if direction > 0 else list(reversed(pts))


def jagged_line(rng, x0, x1, y, amp, lo=4.0, hi=9.0, direction=1):
    """A rim as a fresh break leaves it: straight runs between sharp corners, zig-zagging - each
    corner on the other side of the line from the last, more often than not."""
    pts, x, side = [(x0, y)], x0, 1.0
    while x1 - x > hi:
        x += rng.uniform(lo, hi)
        side = -side if rng.random() < 0.75 else side
        pts.append((x, y + side * amp * rng.uniform(0.3, 1.0)))
    pts.append((x1, y))
    return pts if direction > 0 else list(reversed(pts))


def blob(rng, cx, cy, r, sx=1.0, sy=1.0, rot=0.0, count=9, jitter=0.16, sharp=False):
    pts = []
    for k in range(count):
        a = 2 * math.pi * k / count + rng.uniform(-0.15, 0.15)
        rr = r * (1 + rng.uniform(-jitter, jitter))
        x, y = rr * math.cos(a) * sx, rr * math.sin(a) * sy
        pts.append((cx + x * math.cos(rot) - y * math.sin(rot), cy + x * math.sin(rot) + y * math.cos(rot)))
    return Path(pts, closed=True, sharp=sharp)


def block(rng, cx, cy, r, sx=1.0, sy=1.0, rot=0.0):
    """A broken-off block's outline: five to seven straight sides, no two alike."""
    return blob(rng, cx, cy, r, sx, sy, rot, rng.randint(5, 7), 0.22, sharp=True)


# ---------------------------------------------------------------------------------------------
# The relief.
# ---------------------------------------------------------------------------------------------

class Cell:
    __slots__ = ("on", "cut", "plug", "w", "tilt", "ds", "back", "pal", "two")

    def __init__(self):
        self.on = False
        self.cut = False        #a cap cell a feature removed; finish() leaves it off
        self.plug = False       #empty, with the plateau behind it: show the face at its back depth
        self.w = 0.0
        self.tilt = 0.0
        self.ds = 1.0           #how much of the column's profile this cell takes: a crack closing with depth
        self.back = -3.0
        self.pal = PAL_ROCK_0
        self.two = False        #free-standing: its back is a face too (a lintel)


def make_cols(rng, s0, s1, lo=2.2, hi=4.8, slots=()):
    """Column boundaries from s0 to s1. Each of `slots` - (start, end) - is made exactly one column,
    for a feature that needs its own: a crack."""
    cols, pending = [s0], sorted(slots)
    while True:
        nxt = cols[-1] + rng.uniform(lo, hi)
        if pending and nxt > pending[0][0] - 0.5 * lo:
            start, end = pending.pop(0)
            if start - cols[-1] < 0.8 and len(cols) > 1:
                cols[-1] = start
            elif start - cols[-1] >= 0.8:
                cols.append(start)
            cols.append(end)
            continue
        if s1 - cols[-1] <= hi:
            break
        cols.append(nxt)
    if s1 - cols[-1] < 0.8 and len(cols) > 1:
        cols.pop()
    cols.append(s1)
    return cols


def make_bands(rng, z_top, z_bottom, cap=True, lo=3.5, hi=8.0):
    zs, kinds = [z_top], []
    if cap:
        zs += [z_top - CAP_GRASS, z_top - CAP_GRASS - CAP_SOIL]
        kinds += ["grass", "soil"]
    while zs[-1] > z_bottom + 0.01:
        z = zs[-1] - rng.uniform(lo, hi)
        if z - z_bottom < lo * 0.6:
            z = z_bottom
        zs.append(z)
        kinds.append("rock")
    return zs, kinds


class Relief:
    def __init__(self, rng, path, cols, zs, kinds, closed=False):
        self.rng = rng
        self.path, self.cols, self.zs, self.kinds, self.closed = path, cols, zs, kinds, closed
        self.n, self.m = len(cols) - 1, len(zs) - 1
        self.prof = [[(0.0, 0.0), (1.0, 0.0)] for _ in range(self.n)]
        self.lean = [0.0] * self.m
        self.cells = [[Cell() for _ in range(self.m)] for _ in range(self.n)]
        self.bn = [path.normal(s) for s in cols]
        self.bt = [path.tangent(s) for s in cols]
        self.cap_ends = False   #an open path's two ends get side faces (a kit piece seen on its own)
        self.top = True         #the plateau strips on top; an island gets a fan instead (top_fan)
        self.bottom = False
        self.overhang = 0.35

    # -- shape ---------------------------------------------------------------------------------

    def first_rock(self):
        return self.kinds.index("rock")

    def fill_rock(self, depth, roughness=1.0, trend=0.6, recess_chance=0.05):
        """Rock in every cell. Columns hold their offset down several bands and then break, which
        is what makes the face read as columns rather than as a wall of bricks. `trend` stands the
        top out further than the bottom: a cliff that leans over the void."""
        rng = self.rng
        for i in range(self.n):
            #Fractured, not worn: two or three flat facets meeting at a hard ridge or a notch, never
            #the rounded chamfer of a column ground smooth - that reads as scallops, as flowers.
            width = self.cols[i + 1] - self.cols[i]
            k = min(1.0, width / 3.0)
            if rng.random() < 0.55:
                self.prof[i] = [(0.0, rng.uniform(-0.6, 0.1) * k), (rng.uniform(0.25, 0.75), rng.uniform(-0.3, 0.7) * k),
                                (1.0, rng.uniform(-0.6, 0.1) * k)]
            else:
                a = rng.uniform(0.2, 0.42)
                self.prof[i] = [(0.0, rng.uniform(-0.6, 0.1) * k), (a, rng.uniform(-0.2, 0.6) * k),
                                (rng.uniform(a + 0.15, 0.85), rng.uniform(-0.5, 0.5) * k), (1.0, rng.uniform(-0.6, 0.1) * k)]
        rock = [j for j in range(self.m) if self.kinds[j] == "rock"]
        band_w = {j: rng.gauss(0.0, 0.2 * roughness) for j in rock}
        offset = rng.randrange(len(STRATA))
        band_pal = {j: STRATA[(offset + n) % len(STRATA)] for n, j in enumerate(rock)}
        for j in rock:
            self.lean[j] = rng.uniform(-0.06, 0.06)
        height = self.zs[0] - self.zs[-1]
        for i in range(self.n):
            col_w = rng.gauss(0.0, 0.9 * roughness)
            for j in rock:
                if rng.random() < 0.25:
                    col_w = rng.gauss(0.0, 0.9 * roughness)
                f = (self.zs[0] - 0.5 * (self.zs[j] + self.zs[j + 1])) / height
                c = self.cells[i][j]
                c.on = True
                c.w = col_w + band_w[j] + rng.gauss(0.0, 0.12 * roughness) + trend * (1.0 - f)
                c.tilt = rng.uniform(-0.22, 0.22)
                c.back = -depth
                c.pal = band_pal[j]
                if rng.random() < recess_chance:
                    c.w -= rng.uniform(0.9, 1.6)
                    c.pal = PAL_ROCK_2

    def finish(self):
        """Lays the cap on each column's top rock: turf, then soil, both standing out past it."""
        j0 = self.first_rock() if "rock" in self.kinds else None
        for i in range(self.n):
            top = self.cells[i][j0] if j0 is not None else None
            #Torn turf: some columns' sod hangs out past the break, some is flush with it.
            overhang = self.overhang * self.rng.uniform(0.15, 1.4)
            for j in range(self.m):
                kind = self.kinds[j]
                if kind == "rock":
                    continue
                c = self.cells[i][j]
                if c.cut or top is None or not top.on:
                    c.on = False
                    c.back = top.back if top else c.back
                    continue
                zmid = 0.5 * (self.zs[j0] + self.zs[j0 + 1])
                c.on = True
                c.w = top.w + overhang - (0.14 if kind == "soil" else 0.0) + self.lean[j0] * (self.zs[j0] - zmid)
                c.tilt = top.tilt
                c.ds = top.ds
                c.back = top.back
                c.two = top.two
                c.pal = PAL_LIP if kind == "grass" else PAL_EARTH

    # -- reading ---------------------------------------------------------------------------------

    def cell(self, i, j):
        if self.closed:
            i %= self.n
        elif i < 0 or i >= self.n:
            return None
        if j < 0 or j >= self.m:
            return None
        c = self.cells[i][j]
        return c if c.on else None

    def front(self, i, j, k, z):
        c = self.cells[i][j]
        t, d = self.prof[i][k]
        width = self.cols[i + 1] - self.cols[i]
        zmid = 0.5 * (self.zs[j] + self.zs[j + 1])
        return c.w + d * c.ds + c.tilt * (t - 0.5) * width + self.lean[j] * (z - zmid)

    def pos(self, i, t, w, z):
        s = self.cols[i] + t * (self.cols[i + 1] - self.cols[i])
        q = self.path.place(s, (self.bn[i] * (1 - t) + self.bn[i + 1] * t).normalized(), w)
        q.z = z
        return q

    def pos_b(self, ib, w, z):
        q = self.path.place(self.cols[ib], self.bn[ib], w)
        q.z = z
        return q

    def column_at(self, s):
        for i in range(self.n):
            if self.cols[i] <= s < self.cols[i + 1]:
                return i
        return self.n - 1

    def band_at(self, z):
        for j in range(self.m):
            if self.zs[j + 1] <= z <= self.zs[j]:
                return j
        return self.m - 1

    def max_front(self, i, j, z):
        return max(self.front(i, j, k, z) for k in range(len(self.prof[i])))

    # -- colours ---------------------------------------------------------------------------------

    def ledge_pal(self, j, depth):
        """The top of whatever is below boundary j. Turf on the plateau and on any ledge wide enough
        to hold soil - the green tufts the reference has on its steps - bare light rock otherwise."""
        if j < self.m and self.kinds[j] != "rock":
            return grass(self.rng)
        if depth > 1.0 and self.rng.random() < 0.45:
            return grass(self.rng)
        return PAL_ROCK_0

    # -- the mesh --------------------------------------------------------------------------------

    def emit(self, geo):
        rng = self.rng
        up = Vector((0, 0, 1))
        n, m, zs = self.n, self.m, self.zs

        for i in range(n):
            nm = (self.bn[i] + self.bn[i + 1]).normalized()
            P = self.prof[i]
            for j in range(m):
                c = self.cells[i][j]
                zt, zb = zs[j], zs[j + 1]
                if not c.on:
                    if c.plug:
                        geo.add([self.pos(i, 0, c.back, zt), self.pos(i, 1, c.back, zt),
                                 self.pos(i, 1, c.back, zb), self.pos(i, 0, c.back, zb)], PAL_ROCK_1, nm)
                    continue
                for k in range(len(P) - 1):
                    t0, t1 = P[k][0], P[k + 1][0]
                    geo.add([self.pos(i, t0, self.front(i, j, k, zt), zt), self.pos(i, t1, self.front(i, j, k + 1, zt), zt),
                             self.pos(i, t1, self.front(i, j, k + 1, zb), zb), self.pos(i, t0, self.front(i, j, k, zb), zb)],
                            c.pal, nm)
                if c.two:
                    geo.add([self.pos(i, 0, c.back, zt), self.pos(i, 1, c.back, zt),
                             self.pos(i, 1, c.back, zb), self.pos(i, 0, c.back, zb)], c.pal, -nm)

        #Between bands: ledges, overhangs, the plateau on top, an arch's soffit.
        for i in range(n):
            P = self.prof[i]
            for jb in range(m + 1):
                if jb == m and not self.bottom:
                    continue
                z = zs[jb]
                U, L = self.cell(i, jb - 1), self.cell(i, jb)
                if not U and not L:
                    continue
                if jb == 0 and not self.top:
                    continue
                if not U and jb == 0:
                    self.emit_top(geo, i, L)
                    continue
                under = lambda s, d: PAL_ROCK_2
                for k in range(len(P) - 1):
                    t0, t1 = P[k][0], P[k + 1][0]
                    Pf = lambda x, w, t0=t0, t1=t1, z=z: self.pos(i, t0 + (t1 - t0) * x, w, z)
                    if U and L:
                        pal = lambda s, d, jb=jb: PAL_ROCK_2 if s > 0 else self.ledge_pal(jb, d)
                        bridge(geo, Pf, self.front(i, jb - 1, k, z), self.front(i, jb - 1, k + 1, z),
                               self.front(i, jb, k, z), self.front(i, jb, k + 1, z), -up, pal)
                        if (U.two or L.two) and abs(U.back - L.back) > 1e-4:
                            bridge(geo, Pf, L.back, L.back, U.back, U.back, -up, pal)
                    elif U:
                        bridge(geo, Pf, self.front(i, jb - 1, k, z), self.front(i, jb - 1, k + 1, z),
                               U.back, U.back, -up, under)
                    else:
                        floor = self.cells[i][jb - 1].cut          #a notch's floor: the river's bed
                        top = lambda s, d, jb=jb, floor=floor: PAL_ROCK_1 if floor else self.ledge_pal(jb, d)
                        bridge(geo, Pf, self.front(i, jb, k, z), self.front(i, jb, k + 1, z), L.back, L.back, up, top)

        #Between columns: the sides of whatever stands out further than its neighbour.
        for ib in range(n if self.closed else n + 1):
            if not self.closed and ib in (0, n) and not self.cap_ends:
                continue
            T = self.bt[ib]
            ia, ic = (ib - 1) % n, ib % n
            for j in range(m):
                A, B = self.cell(ib - 1, j), self.cell(ib, j)
                if not A and not B:
                    continue
                zt, zb = zs[j], zs[j + 1]
                Pf = lambda x, w, ib=ib, zt=zt, zb=zb: self.pos_b(ib, w, zt + (zb - zt) * x)
                pal = lambda s, d, A=A, B=B: (A.pal if s > 0 else B.pal) if A and B else (A or B).pal
                if A:
                    ka = len(self.prof[ia]) - 1
                    a0, a1 = self.front(ia, j, ka, zt), self.front(ia, j, ka, zb)
                if B:
                    b0, b1 = self.front(ic, j, 0, zt), self.front(ic, j, 0, zb)
                if A and B:
                    bridge(geo, Pf, a0, a1, b0, b1, T, pal)
                    if (A.two or B.two) and abs(A.back - B.back) > 1e-4:
                        bridge(geo, Pf, B.back, B.back, A.back, A.back, T, pal)
                elif A:
                    bridge(geo, Pf, a0, a1, A.back, A.back, T, pal)
                else:
                    bridge(geo, Pf, b0, b1, B.back, B.back, -T, pal)

    def emit_top(self, geo, i, L):
        """The plateau behind a column, cut into rows so it can take the same bumps as its neighbours:
        rows at fixed depths, so two columns meet vertex for vertex."""
        P = self.prof[i]
        z = self.zs[0]
        for k in range(len(P) - 1):
            t0, t1 = P[k][0], P[k + 1][0]
            f0, f1 = self.front(i, 0, k, z), self.front(i, 0, k + 1, z)
            ws = [L.back]
            r = math.ceil(-L.back / TOP_STEP) - 1
            while r >= 0 and -r * TOP_STEP < min(f0, f1) - 0.4:
                if -r * TOP_STEP > L.back + 0.3:
                    ws.append(-r * TOP_STEP)
                r -= 1
            rows = [(w, w) for w in ws] + [(f0, f1)]

            def at(t, w):
                p = self.pos(i, t, w, z)
                if not L.two:
                    p.z += bump(p, w)
                return p
            for (a0, a1), (b0, b1) in zip(rows, rows[1:]):
                q = [at(t0, a0), at(t1, a1), at(t1, b1), at(t0, b0)]
                pal = grass(self.rng)
                #Two triangles, each its own colour: the plateau's facets.
                geo.add([q[0], q[1], q[2]], pal, Vector((0, 0, 1)))
                geo.add([q[0], q[2], q[3]], grass(self.rng), Vector((0, 0, 1)))

    def top_fan(self, geo, dome=0.06, step=2.2):
        """An island's top: the cap's edge and a scatter of points inside it at their own small heights,
        triangulated - irregular facets like the plateau's. A fan out from the middle reads as a
        flower from above, however angular its edge."""
        rng = self.rng
        z = self.zs[0]
        up = Vector((0, 0, 1))
        rim = []
        for i in range(self.n):
            for k in range(len(self.prof[i])):
                if self.cells[i][0].on:
                    p = self.pos(i, self.prof[i][k][0], self.front(i, 0, k, z), z)
                    if not rim or (p - rim[-1]).length > 0.05:
                        rim.append(p)
        centre = sum(rim, Vector((0, 0, 0))) / len(rim)
        centre.z = z + dome
        outline = Outline([(p.x, p.y) for p in rim], 2.0)
        inner = []
        x0, x1 = min(p.x for p in rim), max(p.x for p in rim)
        y0, y1 = min(p.y for p in rim), max(p.y for p in rim)
        gx = x0 + 0.5 * step
        while gx < x1:
            gy = y0 + 0.5 * step
            while gy < y1:
                x, y = gx + rng.uniform(-0.35, 0.35) * step, gy + rng.uniform(-0.35, 0.35) * step
                if outline.inside(x, y) and all((x - q.x) ** 2 + (y - q.y) ** 2 > 1.0 for q in rim):
                    inner.append(Vector((x, y, z + rng.uniform(0.0, 0.14))))
                gy += step
            gx += step
        pts = rim + inner
        verts, _, faces, orig, _, _ = delaunay_2d_cdt([Vector((q.x, q.y)) for q in pts], [], [list(range(len(rim)))], 1, 1e-4)
        height = [pts[o[0]].z if o else z for o in orig]
        for f in faces:
            geo.add([Vector((verts[k].x, verts[k].y, height[k])) for k in f], grass(rng), up)
        return centre


# ---------------------------------------------------------------------------------------------
# Features: edits of a wall's grid.
# ---------------------------------------------------------------------------------------------

def cols_between(R, sa, sb):
    return [i for i in range(R.n) if sa <= 0.5 * (R.cols[i] + R.cols[i + 1]) <= sb]


def cut_notch(R, sa, sb, groove=1.0):
    """Where a river leaves the plateau: the cap goes, leaving the top rock as the lip, and the rock
    below is worn back into a chute, so the fall clears it. Returns (lip z, lip depth, and where along
    the rim the channel actually starts and ends - its columns', which is what the water must follow)."""
    cols = cols_between(R, sa, sb)
    j0 = R.first_rock()
    lip = max(R.max_front(i, j0, R.zs[j0]) for i in cols)
    for i in cols:
        for j in range(R.m):
            c = R.cells[i][j]
            if R.kinds[j] != "rock":
                c.cut = True            #no plug: the river's channel carries on behind the piece
            elif j > j0:
                c.w = min(c.w, lip - groove - R.rng.uniform(0.0, 0.6))
                c.pal = PAL_ROCK_2 if R.rng.random() < 0.4 else c.pal
    return R.zs[j0], lip, R.cols[cols[0]], R.cols[cols[-1] + 1]


def cut_spring(R, s, z):
    """A dark mouth in the face where water comes out of the rock. Returns (floor z, recess depth,
    lip depth, mouth width, the column's middle along the path)."""
    i, j = R.column_at(s), R.band_at(z)
    c = R.cells[i][j]
    c.w = min(c.w, -1.6) - 0.6
    c.tilt = 0.0
    c.pal = PAL_ROCK_2
    floor = R.zs[j + 1]
    lip = R.max_front(i, j + 1, floor) if j + 1 < R.m else c.w + 1.0
    return floor, c.w + 0.3, lip, 0.55 * (R.cols[i + 1] - R.cols[i]), 0.5 * (R.cols[i] + R.cols[i + 1])


def cut_crack(R, i, depth, cover=6.5):
    """A fresh fissure running inland from the face, in column i: a V in plan, `depth` deep at the
    top and closing as it goes down, its sides zig-zagging - the line the next block will come away
    along. Returns the depth of its tip, where a crack across the plateau carries on."""
    rng = R.rng
    t2 = rng.uniform(0.42, 0.58)
    R.prof[i] = [(0.0, 0.0), (rng.uniform(0.15, t2 - 0.1), -depth * rng.uniform(0.3, 0.55)), (t2, -depth),
                 (rng.uniform(t2 + 0.1, 0.85), -depth * rng.uniform(0.3, 0.6)), (1.0, 0.0)]
    height = R.zs[0] - R.zs[-1]
    for j in range(R.m):
        if R.kinds[j] != "rock":
            continue
        c = R.cells[i][j]
        beside = [R.cells[k][j] for k in (i - 1, i + 1) if 0 <= k < R.n and R.cells[k][j].on]
        if beside:
            c.w = sum(x.w for x in beside) / len(beside)
        c.tilt = 0.0
        f = (R.zs[0] - 0.5 * (R.zs[j] + R.zs[j + 1])) / height
        c.ds = max(0.1, 1.0 - 1.6 * f)
        if f > 0.12:
            c.pal = PAL_ROCK_2
    #The V reaches far behind the rim; its column's strips must reach further, and so must its
    #neighbours' for `cover` either side - more than the ragged hole the ground leaves for it, which a
    #narrow neighbour alone does not span.
    reach = [i]
    for step in (-1, 1):
        k, width = i + step, 0.0
        while 0 <= k < R.n and width < cover:
            reach.append(k)
            width += R.cols[k + 1] - R.cols[k]
            k += step
    for k in reach:
        for c in R.cells[k]:
            c.back = min(c.back, -(depth + 4.0))
    return -depth


def matching_wall(rng, F, path, trend_f, trend_n, erosion=0.2):
    """The other side of a fresh crack: F's rim moved across the gap by `offset` and walked the other
    way, every cell's depth negated - so each spur on one side faces the notch it broke out of, and
    the strata line up across the gap. That fit is what makes the chasm look recent. Call it before
    F's features are cut: a river or a later crack is younger than the split. `path` is the other
    rim - F's control points carried across, walked the other way - and columns are matched through
    across(), so the gap may be wider in one place than another. `erosion` keeps the fit from being
    perfect."""
    cols = [across(F.path, path, c) for c in reversed(F.cols)]
    cols[0], cols[-1] = 0.0, path.length
    N = Relief(rng, path, cols, list(F.zs), list(F.kinds))
    height = F.zs[0] - F.zs[-1]
    for i in range(N.n):
        src = F.n - 1 - i
        N.prof[i] = [(1.0 - t, -d) for t, d in reversed(F.prof[src])]
        for j in range(F.m):
            a, b = F.cells[src][j], N.cells[i][j]
            if F.kinds[j] != "rock" or not a.on:
                continue
            f = (F.zs[0] - 0.5 * (F.zs[j] + F.zs[j + 1])) / height
            b.on = True
            b.w = -(a.w - trend_f * (1.0 - f)) + trend_n * (1.0 - f) + rng.gauss(0.0, erosion)
            b.tilt, b.ds, b.back, b.pal = a.tilt, a.ds, a.back, a.pal
    N.lean = [-x for x in F.lean]
    return N


def island(rng, geo, path, z_top, z_bottom, trend=0.5, roughness=0.9, tilt=0.0):
    """A free-standing block: an island, a terrace against a wall, a column. `tilt` leans the whole
    block - strata, top and all - by that slope, in a random direction: a block that dropped and
    settled crooked. Returns the top's middle, and a function giving the top's height at (x, y)."""
    part = Geo()
    zs, kinds = make_bands(rng, z_top, z_bottom)
    R = Relief(rng, path, make_cols(rng, 0.0, path.length, 1.8, 3.6), zs, kinds, closed=True)
    R.top = False
    R.overhang = 0.3
    R.fill_rock(2.0, roughness, trend, recess_chance=0.03)
    R.finish()
    R.emit(part)
    centre = R.top_fan(part)
    a = rng.uniform(0.0, 2.0 * math.pi)
    gx, gy = tilt * math.cos(a), tilt * math.sin(a)
    cx, cy = centre.x, centre.y
    part.transform(lambda p: Vector((p.x, p.y, p.z + gx * (p.x - cx) + gy * (p.y - cy))))
    geo.extend(part)
    return centre, (lambda x, y: z_top + gx * (x - cx) + gy * (y - cy))


def natural_bridge(rng, geo, a, b, z_top, half, crown, spring, z_bottom, span=0.7):
    """A natural bridge: a fin of rock from a to b - from a rim out to a block standing off it, or
    between two blocks - turf on top, its underside arched over `span` of its length, from `crown`
    under the middle down to `spring` at the opening's ends. From above, the chasm shows on both sides
    of it and through it; an arch across a bay in the wall never managed that. Two-sided throughout,
    `half` either side of its line; its ends are meant to be buried in whatever it joins."""
    d = Vector((b.x - a.x, b.y - a.y, 0.0))
    side = Vector((-d.y, d.x, 0.0)).normalized()
    mid = a.lerp(b, 0.5) + side * rng.uniform(-0.12, 0.12) * d.length
    path = Path([(a.x, a.y), (mid.x, mid.y), (b.x, b.y)], sharp=True)
    zs, kinds = make_bands(rng, z_top, z_bottom, lo=2.5, hi=5.0)
    R = Relief(rng, path, make_cols(rng, 0.0, path.length, 1.6, 3.2), zs, kinds)
    R.fill_rock(half, roughness=0.5, trend=0.0, recess_chance=0.0)
    length = path.length
    for i in range(R.n):
        u = (0.5 * (R.cols[i] + R.cols[i + 1]) - 0.5 * length) / (0.5 * length * span)
        bottom = crown + (spring - crown) * (1.0 - math.sqrt(max(0.0, 1.0 - u * u))) if abs(u) < 1.0 else None
        for j in range(R.m):
            c = R.cells[i][j]
            if R.kinds[j] != "rock":
                continue
            c.two = True
            c.back = -half * rng.uniform(0.8, 1.15)
            c.w = half + 0.4 * c.w
            if bottom is not None and 0.5 * (R.zs[j] + R.zs[j + 1]) < bottom:
                c.on = False
    R.finish()
    R.emit(geo)


# ---------------------------------------------------------------------------------------------
# Water.
# ---------------------------------------------------------------------------------------------

def falls(geo, rng, path, sc, width, z_lip, w_from, w_lip, z_end, out=2.2, spread=0.10, strips=3, foam=0.3):
    """A fall as a few strips side by side, each with its own small offset - faceted water. It runs
    level from `w_from` to the lip, curls over and drops, widening as it goes."""
    drops = [0.0, 0.3, 0.9, 1.8, 3.2, 5.2, 8.0, 12.0, 17.0, 23.0, 30.0, 38.0, 47.0]
    zl = [z_lip - d for d in drops if z_lip - d > z_end + 0.6] + [z_end]
    lines = []
    for q in range(strips + 1):
        f = q / strips - 0.5
        jw = rng.uniform(-0.15, 0.15)
        line = [(sc + f * width, w_from, z_lip + 0.05)]
        for z in zl:
            d = z_lip - z
            w = w_lip + 0.05 + (0.3 + out * (1 - math.exp(-d / 2.5))) * min(1.0, d / 0.3) + jw * min(1.0, d / 1.5)
            line.append((sc + f * (width + spread * d), w, z + (0.05 if d == 0 else 0.0)))
        lines.append(line)
    up = Vector((0, 0, 1))
    #Each strip keeps one colour most of the way down, so the fall reads as streaks, not a checkerboard.
    shades = [(PAL_FOAM, PAL_EFFECTS), (PAL_HAZE, 0), (PAL_FOAM, PAL_EFFECTS), (PAL_HAZE, 0), (PAL_WATER, 0)]
    #White in the middle, blue at the edges, as a fall seen from the front is.
    streak = [(PAL_WATER, 0) if q in (0, strips - 1) and strips > 2 else rng.choice(shades[:4]) for q in range(strips)]
    for q in range(strips):
        for r in range(len(lines[q]) - 1):
            a, b = lines[q][r], lines[q][r + 1]
            c, d = lines[q + 1][r + 1], lines[q + 1][r]
            quad = [path.at(*a), path.at(*d), path.at(*c), path.at(*b)]
            nrm = path.normal(a[0])
            want = up if r <= 1 else (nrm + up * 0.3)
            if r == 0:
                col, row = PAL_WATER, 0
            elif r == 1 or r == len(lines[q]) - 2:
                col, row = PAL_FOAM, PAL_EFFECTS
            elif rng.random() < foam * 0.4:
                col, row = rng.choice(shades)
            else:
                col, row = streak[q]
            geo.add(quad, col, want, row)
    bottom = path.at(sc, lines[strips // 2][-1][1], z_end)
    return bottom


def splash(geo, rng, centre, radius, lumps=5, row=PAL_EFFECTS, col=PAL_FOAM, flat=0.55):
    for _ in range(lumps):
        c = centre + Vector((rng.uniform(-radius, radius), rng.uniform(-radius, radius), rng.uniform(-0.2, 0.4)))
        r = radius * rng.uniform(0.35, 0.7)
        pts = []
        golden = math.pi * (3 - math.sqrt(5))
        for i in range(14):
            z = 1 - 2 * (i + 0.5) / 14
            rr = math.sqrt(1 - z * z)
            a = golden * i
            k = 1 + rng.uniform(-0.2, 0.2)
            pts.append(c + Vector((r * rr * math.cos(a) * k, r * rr * math.sin(a) * k, r * flat * z * k)))
        geo.hull(pts, col, row, drop_down=True)


def stream(geo, rng, path, sc, width, w_from, w_to, z):
    """A river's last stretch along its channel to the lip: flat, straight, as the channel is."""
    steps = max(2, int(abs(w_to - w_from) / 2.5))
    prev = None
    for k in range(steps + 1):
        w = w_from + (w_to - w_from) * k / steps
        row = [path.at(sc + f * width, w, z) for f in (-0.5, 0.5)]
        if prev:
            geo.add([prev[0], prev[1], row[1], row[0]], PAL_WATER, Vector((0, 0, 1)))
        prev = row


# ---------------------------------------------------------------------------------------------
# Rocks: long spiked shards, standing, clustered, fallen, and the big spires in the chasm.
# ---------------------------------------------------------------------------------------------

def rock_by_facing(n, band):
    """Lit tops, dark undersides, and the stratum's colour on the sides - one shard's strata line up
    with nothing in particular, but they say it is the same rock as the walls."""
    if n.z > 0.55:
        return PAL_ROCK_0
    if n.z < -0.35:
        return PAL_ROCK_2
    return band


def spike(rng, height, radius, sides=5, segs=3, lean=(0.0, 0.0), twist=0.2, sink=0.4, tip_cut=0.0, flat=0.65):
    """A long shard of rock standing on the origin: a blade more than a cone - its section squashed
    by `flat` - staying thick most of the way up, each ring kinked off the line of the one below,
    and the point pushed off to one side, as a slab splits. `tip_cut` breaks the point off that far
    down, leaving a flat top: a fallen shard's broken end. A smooth taper reads as a horn."""
    geo = Geo()
    a0 = rng.uniform(0.0, 2.0 * math.pi)
    ca, sa = math.cos(a0), math.sin(a0)
    top_t = 1.0 - tip_cut
    ts = [top_t * k / (segs - 1) for k in range(segs)] if tip_cut > 0 else [k / segs for k in range(segs)]
    axis, rings = [], []
    for k, t in enumerate(ts):
        r = radius * max(0.06, 1.0 - t) ** 0.55 * rng.uniform(0.85, 1.1)
        kink = Vector((rng.uniform(-0.18, 0.18), rng.uniform(-0.18, 0.18), 0.0)) * radius * (k > 0)
        c = Vector((lean[0] * t, lean[1] * t, -sink + (height + sink) * t)) + kink
        ring = []
        for q in range(sides):
            a = twist * k + 2.0 * math.pi * q / sides + rng.uniform(-0.25, 0.25)
            rr = r * rng.uniform(0.8, 1.15)
            x, y = rr * math.cos(a), rr * math.sin(a) * flat
            ring.append(c + Vector((x * ca - y * sa, x * sa + y * ca, rng.uniform(-0.12, 0.12) * radius)))
        axis.append(c)
        rings.append(ring)
    sides_pal = [PAL_ROCK_1, PAL_ROCK_3, PAL_ROCK_1, PAL_ROCK_2]

    def face(pts, k, want):
        n = newell(pts)
        if n.length < 1e-9:
            return
        n.normalize()
        if n.dot(want) < 0:
            n = -n
        geo.add(pts, rock_by_facing(n, rng.choice(sides_pal)), want)

    for k in range(len(rings) - 1):
        mid = (axis[k] + axis[k + 1]) * 0.5
        for q in range(sides):
            quad = [rings[k][q], rings[k][(q + 1) % sides], rings[k + 1][(q + 1) % sides], rings[k + 1][q]]
            face(quad, k, sum(quad, Vector((0, 0, 0))) / 4 - mid)
    if tip_cut > 0:
        face(list(rings[-1]), len(rings), Vector((0, 0, 1)))
    else:
        side = rng.uniform(0.25, 0.55) * radius * rng.choice([-1.0, 1.0])
        tip = Vector((lean[0] + side * ca, lean[1] + side * sa, height))
        for q in range(sides):
            tri = [rings[-1][q], rings[-1][(q + 1) % sides], tip]
            face(tri, len(rings), sum(tri, Vector((0, 0, 0))) / 3 - axis[-1])
    return geo


def placed(src, at, yaw=0.0, scale=1.0, pitch=0.0):
    """A copy of `src` tipped over by `pitch` about X (to lay a shard down), turned by `yaw`, scaled
    and moved to `at`."""
    cp, sp, cy, sy = math.cos(pitch), math.sin(pitch), math.cos(yaw), math.sin(yaw)
    at = Vector(at)

    def f(p):
        x, y, z = p.x, p.y * cp - p.z * sp, p.y * sp + p.z * cp
        return at + Vector((x * cy - y * sy, x * sy + y * cy, z)) * scale
    out = Geo()
    out.faces = [([f(p) for p in pts], pal, row) for pts, pal, row in src.faces]
    return out


def rock_base(geo, rng, radius, height):
    pts = []
    for k in range(9):
        a = 2.0 * math.pi * k / 9 + rng.uniform(-0.25, 0.25)
        r = radius * rng.uniform(0.7, 1.1)
        pts.append(Vector((r * math.cos(a), r * math.sin(a), -0.3)))
        pts.append(Vector((r * 0.6 * math.cos(a + 0.3), r * 0.6 * math.sin(a + 0.3), height * rng.uniform(0.6, 1.0))))
    geo.hull(pts, lambda n: rock_by_facing(n, PAL_ROCK_1), drop_down=True)


def spike_cluster(rng, count=4, size=1.0):
    """Shards splaying out of one broken base, the tallest near the middle."""
    geo = Geo()
    rock_base(geo, rng, 1.2 * size, 0.7 * size)
    for k in range(count):
        a = 2.0 * math.pi * k / count + rng.uniform(-0.4, 0.4)
        d = rng.uniform(0.2, 0.8) * size
        h = rng.uniform(1.4, 3.4) * size * (1.25 if k == 0 else 1.0)
        r = h * rng.uniform(0.3, 0.42)
        out = h * rng.uniform(0.15, 0.4)
        s = spike(rng, h, r, rng.choice([4, 5, 5]), 3, (math.cos(a) * out, math.sin(a) * out),
                  tip_cut=rng.uniform(0.15, 0.3) if rng.random() < 0.5 else 0.0)
        geo.extend(placed(s, (math.cos(a) * d, math.sin(a) * d, 0.0), rng.uniform(0, 6.28)))
    return geo


def fallen_shard(rng, length, radius):
    """A shard lying where it fell, its point snapped off, one end dug into the ground."""
    s = spike(rng, length, radius, rng.choice([5, 6]), 4, twist=0.25, sink=0.0, tip_cut=rng.uniform(0.2, 0.35))
    return placed(s, (-0.5 * length, 0.0, radius * 0.45), 0.5 * math.pi, 1.0, -math.radians(rng.uniform(78, 86)))


# ---------------------------------------------------------------------------------------------
# Cracks across the plateau.
# ---------------------------------------------------------------------------------------------

def ground_crack(geo, rng, pts, width, z_at, open_end=False):
    """A crack across the ground: a dark slot, with the turf along both lips torn and tipped up into
    it. `pts` is its line (xy), `z_at(x, y)` the ground under it. With `open_end` it is widest at its
    first point - where it leaves a fissure in the face - and closes to nothing at the other; otherwise
    it closes at both. It sits ON the ground, as a decal with relief, so it needs no hole cut."""
    up = Vector((0, 0, 1))
    n = len(pts)
    P = [Vector((x, y, 0.0)) for x, y in pts]
    side = []
    for k in range(n):
        d = (P[min(k + 1, n - 1)] - P[max(k - 1, 0)]).normalized()
        side.append(Vector((-d.y, d.x, 0.0)))
    rows = []
    for k in range(n):
        t = k / (n - 1)
        h = 0.5 * width * ((1.0 - t) ** 0.7 if open_end else math.sin(math.pi * t) ** 0.6)
        h *= rng.uniform(0.75, 1.2) if 0 < k < n - 1 else 1.0
        lip = (0.3 + 1.3 * h) * rng.uniform(0.7, 1.3)
        rise = (0.04 + 0.3 * h) * rng.uniform(0.5, 1.2)
        rows.append((h, lip, rise))

    def at(k, off, dz):
        p = P[k] + side[k] * off
        p.z = z_at(p.x, p.y) + dz
        return p
    for k in range(n - 1):
        slot = [at(k, rows[k][0], 0.03), at(k + 1, rows[k + 1][0], 0.03),
                at(k + 1, -rows[k + 1][0], 0.03), at(k, -rows[k][0], 0.03)]
        geo.add(slot, PAL_VOID, up, PAL_EFFECTS)
        for sg in (1.0, -1.0):
            inner = [at(k, sg * rows[k][0], rows[k][2] + 0.03), at(k + 1, sg * rows[k + 1][0], rows[k + 1][2] + 0.03)]
            outer = [at(k, sg * (rows[k][0] + rows[k][1]), 0.01), at(k + 1, sg * (rows[k + 1][0] + rows[k + 1][1]), 0.01)]
            base = [at(k, sg * rows[k][0], 0.03), at(k + 1, sg * rows[k + 1][0], 0.03)]
            geo.add([outer[0], outer[1], inner[1], inner[0]], grass(rng), up)
            towards = -side[k] * sg + up * 0.2
            geo.add([inner[0], inner[1], base[1], base[0]], PAL_EARTH, towards)


def crack_line(rng, path, s, w, length, heading, step=1.3, wander=0.35):
    """A crack's line in a wall's (s, w) frame - along the rim and in from it - as world xy. `heading`
    is the angle in that frame: -pi/2 runs straight inland, 0 along the rim."""
    pts, a = [], heading
    for _ in range(max(3, int(length / step)) + 1):
        p = path.at(s, w, 0.0)
        pts.append((p.x, p.y, s, w))
        a += rng.uniform(-wander, wander)
        a = heading + max(-0.9, min(0.9, a - heading))
        s += math.cos(a) * step
        w += math.sin(a) * step
    return pts


# ---------------------------------------------------------------------------------------------
# The plateau beyond the walls, and the rivers across it.
# ---------------------------------------------------------------------------------------------

class Outline:
    """A closed polygon that answers "is this point inside?" quickly: its edges bucketed by height."""

    def __init__(self, pts, size=4.0):
        self.size = size
        self.bins = {}
        for k in range(len(pts)):
            a, b = pts[k], pts[(k + 1) % len(pts)]
            if a[1] == b[1]:
                continue
            for j in range(int(math.floor(min(a[1], b[1]) / size)), int(math.floor(max(a[1], b[1]) / size)) + 1):
                self.bins.setdefault(j, []).append((a, b))

    def inside(self, x, y):
        c = False
        for a, b in self.bins.get(int(math.floor(y / self.size)), ()):
            if (a[1] > y) != (b[1] > y) and x < a[0] + (y - a[1]) * (b[0] - a[0]) / (b[1] - a[1]):
                c = not c
        return c


class Near:
    """Lines that answer "is this point within reach of one?" quickly: segments bucketed by cell, each
    with its own reach."""

    def __init__(self, size=8.0):
        self.size = size
        self.cells = {}

    def add(self, pts, reach):
        for a, b in zip(pts, pts[1:]):
            for i in range(int(math.floor((min(a[0], b[0]) - reach) / self.size)),
                           int(math.floor((max(a[0], b[0]) + reach) / self.size)) + 1):
                for j in range(int(math.floor((min(a[1], b[1]) - reach) / self.size)),
                               int(math.floor((max(a[1], b[1]) + reach) / self.size)) + 1):
                    self.cells.setdefault((i, j), []).append((a, b, reach))

    def near(self, x, y):
        for a, b, r in self.cells.get((int(math.floor(x / self.size)), int(math.floor(y / self.size))), ()):
            dx, dy = b[0] - a[0], b[1] - a[1]
            t = max(0.0, min(1.0, ((x - a[0]) * dx + (y - a[1]) * dy) / (dx * dx + dy * dy + 1e-12)))
            ex, ey = x - a[0] - t * dx, y - a[1] - t * dy
            if ex * ex + ey * ey < r * r:
                return True
        return False


def chasm_outline(rims, w):
    """The chasm as one closed polygon, `w` behind every rim: the rims in order, each walked with the
    chasm on its right, so one's end leads to the next one's start."""
    pts = []
    for path in rims:
        steps = max(2, int(path.length / 1.0))
        for k in range(steps + 1):
            p = path.at(path.length * k / steps, w, 0.0)
            pts.append((p.x, p.y))
    return Outline(pts)


GROUND_SINK = 0.05      #the ground mesh sits this far under the strips, so where both are, the strips win


def ground(geo, rng, x0, x1, y0, y1, step, blocked):
    """The plateau beyond the walls' strips: a jittered triangle grid at the ground's height, every
    triangle with a corner in the chasm, a river or a crack left out. The strips, the river banks and
    the walls' own cracks cover what that leaves ragged - each reaches past the ragged edge by more
    than a triangle. Separate from the walls on purpose: a wall's strips run back along its own normal,
    and at a sharp corner of a fresh rim those cross each other a few units back."""
    nx, ny = int((x1 - x0) / step) + 1, int((y1 - y0) / step) + 1
    grid = {}
    for i in range(nx):
        for j in range(ny):
            x = x0 + i * step + (rng.uniform(-0.3, 0.3) * step if 0 < i < nx - 1 else 0.0)
            y = y0 + j * step + (rng.uniform(-0.3, 0.3) * step if 0 < j < ny - 1 else 0.0)
            grid[i, j] = (Vector((x, y, ground_z(x, y) - GROUND_SINK)), blocked(x, y))
    up = Vector((0, 0, 1))
    for i in range(nx - 1):
        for j in range(ny - 1):
            a, b, c, d = grid[i, j], grid[i + 1, j], grid[i + 1, j + 1], grid[i, j + 1]
            for tri in ((a, b, c), (a, c, d)) if (i + j) % 2 == 0 else ((a, b, d), (b, c, d)):
                if not any(t[1] for t in tri):
                    geo.add([t[0] for t in tri], grass(rng), up)


def line_sides(P):
    out = []
    for k in range(len(P)):
        d = (P[min(k + 1, len(P) - 1)] - P[max(k - 1, 0)]).normalized()
        out.append(Vector((-d.y, d.x, 0.0)))
    return out


def river_channel(geo, rng, pts, width, bank, z_floor=-1.15):
    """A river's channel across the plateau, from just behind its notch back to where it comes from:
    water on the floor, earth walls, and a strip of bank either side, a hair above the strips, that
    covers the ragged edge of the hole the ground leaves for it."""
    up = Vector((0, 0, 1))
    P = [Vector((x, y, 0.0)) for x, y in pts]
    sides = line_sides(P)
    half = 0.5 * width

    def at(k, off, z=None):
        p = P[k] + sides[k] * off
        p.z = ground_z(p.x, p.y) + 0.01 if z is None else z
        return p
    for k in range(len(P) - 1):
        geo.add([at(k, half, z_floor + 0.05), at(k + 1, half, z_floor + 0.05),
                 at(k + 1, -half, z_floor + 0.05), at(k, -half, z_floor + 0.05)], PAL_WATER, up)
        for sg in (1.0, -1.0):
            e0, e1 = at(k, sg * half), at(k + 1, sg * half)
            geo.add([e0, e1, at(k + 1, sg * half, z_floor), at(k, sg * half, z_floor)], PAL_EARTH, -sides[k] * sg)
            geo.add([at(k, sg * (half + bank)), at(k + 1, sg * (half + bank)), e1, e0], grass(rng), up)


def river_line(rng, path, s, length, step=4.0, wander=0.22, edge=None):
    """A river from its notch at s back inland: square to the rim for the first stretch - the
    notch's walls run straight back - then meandering. With `edge` - (x, y, half_x, half_y), the
    map's middle and half sizes - it turns by degrees towards the nearer side of the map and runs
    until it is past it, which is where the game's rivers come from."""
    p = path.at(s, -4.5, 0.0)
    d = -path.deep_normal(s)
    a0 = a = math.atan2(d.y, d.x)
    pts = [(p.x, p.y)]
    if edge:
        target = math.pi if d.x < 0.0 else 0.0     #the side it already runs towards, never back across
    for k in range(int(length / step)):
        if k > 1:
            if edge:
                turn = math.atan2(math.sin(target - a), math.cos(target - a))
                a += 0.08 * turn + rng.uniform(-0.6, 0.6) * wander
            else:
                a += rng.uniform(-wander, wander)
                a = a0 + max(-0.8, min(0.8, a - a0))
        p = p + Vector((math.cos(a), math.sin(a), 0.0)) * step
        pts.append((p.x, p.y))
        if edge and (abs(p.x - edge[0]) > edge[2] + 8.0 or abs(p.y - edge[1]) > edge[3] + 8.0):
            break
    return pts


def notch_river(rng, geo_rock, geo_water, R, sa, sb, length, blocked, trees_block, step, z_end=MIST_Z - 1.0,
                strips=3, edge=None):
    """Everything one river is, from the map to the mist: the notch it cut in wall R between sa and
    sb (one column, a slot made for it), its channel back inland, the water along both and the fall.
    Its line goes into `blocked` and `trees_block`, so the ground leaves room for it; its banks are
    wide enough to cover the ragged edge a ground of triangles `step` across leaves. With `edge`, the
    river comes from that side of the map - see river_line."""
    z_lip, lip, sa, sb = cut_notch(R, sa, sb)
    sc, width = 0.5 * (sa + sb), sb - sa
    line = river_line(rng, R.path, sc, length, edge=edge)
    river_channel(geo_water, rng, line, width, 1.5 * step + 0.5)
    blocked.add(line, 0.5 * width + 0.2)
    trees_block.add(line, 0.5 * width + 4.0)
    stream(geo_water, rng, R.path, sc, width, -4.6, -2.0, z_lip + 0.05)
    return lambda: falls(geo_water, rng, R.path, sc, width - 0.6, z_lip, -2.0, lip, z_end, strips=strips)


# ---------------------------------------------------------------------------------------------
# Kit pieces: each a straight stretch of rim, facing -Y, origin on the rim at its middle.
# ---------------------------------------------------------------------------------------------

KIT_BOTTOM = -48.0
KIT_DEPTH = 3.0


def kit_path(rng, length):
    return Path(jagged_line(rng, -0.5 * length, 0.5 * length, 0.0, 0.9, 3.0, 6.0), sharp=True)


def kit_wall(rng, length, depth=KIT_DEPTH, slots=()):
    """`slots` are (from, to) along the rim, measured from its middle - each made one column."""
    path = kit_path(rng, length)
    mid = 0.5 * path.length
    zs, kinds = make_bands(rng, 0.0, KIT_BOTTOM)
    R = Relief(rng, path, make_cols(rng, 0.0, path.length, slots=[(mid + a, mid + b) for a, b in slots]), zs, kinds)
    R.cap_ends = True
    R.fill_rock(depth, trend=0.4)
    return path, R


def kit_cliff_wall(rng):
    geo = Geo()
    _, R = kit_wall(rng, 16.0)
    R.finish()
    R.emit(geo)
    return {"cliff_wall_a": geo}


def kit_cliff_crack(rng):
    """A wall with a fissure opening into it, and the crack it carries on as across the plateau."""
    geo = Geo()
    path, R = kit_wall(rng, 18.0, depth=14.0, slots=[(-1.0, 1.0)])
    i = R.column_at(0.5 * path.length)
    tip = cut_crack(R, i, 9.0)
    R.finish()
    R.emit(geo)
    s_mid = 0.5 * (R.cols[i] + R.cols[i + 1])
    line = crack_line(rng, path, s_mid, tip + 0.6, 12.0, -0.5 * math.pi)
    ground_crack(geo, rng, [(x, y) for x, y, _, _ in line], 1.0, ground_z, open_end=True)
    return {"cliff_crack_a": geo}


def kit_cliff_notch(rng):
    """A rim notch: a slot the width of the game's river channels, its floor at the channel's depth."""
    rock, water = Geo(), Geo()
    path, R = kit_wall(rng, 14.0, slots=[(-2.2, 2.2)])
    z_lip, lip, sa, sb = cut_notch(R, 0.5 * path.length - 2.2, 0.5 * path.length + 2.2)
    R.finish()
    R.emit(rock)
    sc = 0.5 * (sa + sb)
    end = falls(water, rng, path, sc, sb - sa - 0.6, z_lip, -KIT_DEPTH, lip, MIST_Z - 2.0)
    splash(water, rng, end, 2.6, 6, PAL_EFFECTS, PAL_MIST_LIGHT)
    return {"cliff_notch_a": rock, "falls_notch_a": water}


def kit_cliff_spring(rng):
    rock, water = Geo(), Geo()
    path, R = kit_wall(rng, 12.0)
    sc = 0.5 * path.length
    floor, w_from, lip, width, s_mid = cut_spring(R, sc, -9.0)
    R.finish()
    R.emit(rock)
    #It lands on a terrace of its own, ten below - level, so the water meets it where it should.
    t_centre = path.at(s_mid, 3.5, 0)
    ctop, _ = island(rng, rock, block(rng, t_centre.x, t_centre.y, 4.2, 1.3, 0.9), floor - 10.0, KIT_BOTTOM)
    end = falls(water, rng, path, s_mid, width, floor, w_from, lip, ctop.z - 0.1, out=1.4, spread=0.05, strips=2)
    splash(water, rng, end, 1.3, 4)
    return {"cliff_spring_a": rock, "falls_spring_a": water}


def kit_cliff_bridge(rng):
    """A natural bridge from the rim out to a pillar of plateau standing off it. Its rim end goes a
    little into whatever wall it is put against."""
    geo = Geo()
    far = Vector((0.0, -16.0, 0.0))
    island(rng, geo, block(rng, far.x, far.y, 5.5, 1.1, 1.0, rng.uniform(0, 3)), 0.0, KIT_BOTTOM)
    natural_bridge(rng, geo, Vector((0.0, 1.5, 0.0)), far, -0.02, 2.2, -5.0, -20.0, KIT_BOTTOM)
    return {"cliff_bridge_a": geo}


def kit_cliff_terraces(rng):
    """Three blocks that slumped down a wall and out from it, each settled a little crooked. Placed
    half into the wall, so they stand against whichever wall they are put on."""
    geo = Geo()
    for (x, out, r, top) in [(-3.0, 1.0, 5.0, -3.0), (2.5, 4.0, 4.4, -7.5), (-1.0, 8.0, 3.6, -13.0)]:
        island(rng, geo, block(rng, x, -out, r, 1.25, 0.85, rng.uniform(-0.3, 0.3)), top, KIT_BOTTOM,
               tilt=rng.uniform(0.03, 0.07))
    return {"cliff_terraces_a": geo}


def kit_island(rng):
    geo = Geo()
    island(rng, geo, block(rng, 0, 0, 7.0, 1.2, 0.9, 0.3), 0.0, -40.0, tilt=0.08)
    return {"cliff_island_a": geo}


def kit_column(rng):
    geo = Geo()
    island(rng, geo, block(rng, 0, 0, 2.8, 1.0, 1.0, 0.0), 0.0, -40.0, trend=0.2, roughness=0.6, tilt=0.1)
    return {"cliff_column_a": geo}


def any_spike(rng):
    """A standing shard for scattering: about three times as tall as it is thick, leaning, and more
    often than not with its point broken off - a slab that split and was pushed up, not a thorn. A
    sharp point on every one of them read as a field of spikes."""
    h = rng.uniform(2.5, 5.5)
    lean = h * rng.uniform(0.05, 0.3)
    a = rng.uniform(0.0, 2.0 * math.pi)
    return spike(rng, h, h * rng.uniform(0.3, 0.42), rng.choice([4, 5, 5, 6]), 3,
                 (math.cos(a) * lean, math.sin(a) * lean), twist=rng.uniform(0.1, 0.3),
                 tip_cut=rng.uniform(0.15, 0.35) if rng.random() < 0.6 else 0.0, flat=rng.uniform(0.55, 0.85))


def crack_piece(rng, length, width, wander):
    geo = Geo()
    line = crack_line(rng, Path([(-0.5 * length, 0.0), (0.5 * length, 0.0)]), 0.0, 0.0, length, 0.0, wander=wander)
    ground_crack(geo, rng, [(x - 0.5 * length, y) for x, y, _, _ in line], width, lambda x, y: 0.0)
    return geo


#(name, seed, builder, row): row 0 the walls and blocks, row 1 the props, row 2 the spires. A builder
#returns {object name: Geo}.
KIT = [
    ("wall", 101, kit_cliff_wall, 0),
    ("crack", 108, kit_cliff_crack, 0),
    ("notch", 102, kit_cliff_notch, 0),
    ("spring", 103, kit_cliff_spring, 0),
    ("bridge", 104, kit_cliff_bridge, 0),
    ("terraces", 105, kit_cliff_terraces, 0),
    ("island", 106, kit_island, 0),
    ("column", 107, kit_column, 0),
] + [("spike_%s" % c, 210 + k, (lambda c: lambda r: {"rock_spike_%s" % c: any_spike(r)})(c), 1)
     for k, c in enumerate("abcd")] \
  + [("cluster_%s" % c, 220 + k, (lambda c, n, size: lambda r: {"rock_spike_cluster_%s" % c: spike_cluster(r, n, size)})(c, n, size), 1)
     for k, (c, n, size) in enumerate([("a", 5, 1.0), ("b", 3, 0.8), ("c", 6, 1.3)])] \
  + [("fallen_%s" % c, 230 + k, (lambda c, l, r_: lambda r: {"rock_shard_fallen_%s" % c: fallen_shard(r, l, r_)})(c, l, r_), 1)
     for k, (c, l, r_) in enumerate([("a", 7.0, 0.85), ("b", 4.5, 0.7), ("c", 9.0, 1.1)])] \
  + [("crack_%s" % c, 240 + k, (lambda c, l, w, wa: lambda r: {"ground_crack_%s" % c: crack_piece(r, l, w, wa)})(c, l, w, wa), 1)
     for k, (c, l, w, wa) in enumerate([("a", 12.0, 0.9, 0.35), ("b", 7.0, 0.6, 0.5), ("c", 18.0, 1.2, 0.3), ("d", 5.0, 0.45, 0.6)])] \
  + [("spire_%s" % c, 250 + k, (lambda c, h, r_: lambda r: {"rock_spire_%s" % c: spike(r, h, r_, 6, 5, (r.uniform(-3, 3), r.uniform(-3, 3)),
                                                                                      twist=0.25, sink=2.0, flat=0.75, tip_cut=tc)})(c, h, r_), 2)
     for k, (c, h, r_, tc) in enumerate([("a", 40.0, 5.0, 0.0), ("b", 30.0, 4.0, 0.0), ("c", 36.0, 5.5, 0.2)])]
KIT_ROWS = {0: (32.0, -200.0, 0.0, 0.0), 1: (12.0, -140.0, 0.0, 0.0), 2: (18.0, -200.0, 8 * 32.0, -48.0)}     #spacing, y, first x, z


# ---------------------------------------------------------------------------------------------
# The diorama: a stretch of chasm with every feature in it, close up, to judge the look. Not exported.
# ---------------------------------------------------------------------------------------------

DIO_STRIP = 6.0         #how far the walls' strips reach behind the rim; the ground mesh does the rest
DIO_STEP = 1.6          #the ground's triangle size
OUTLINE_W = -2.5        #where the ground's hole is, behind the rim: well inside the strips
FAR_Y, NEAR_Y = 20.0, -20.0
RIFT_SHEAR = 7.0        #the rift opened a little sideways as well as apart


def plateau_props(rng, props, path, count, avoid, marks, z_of=ground_z):
    """Heaps of broken rock just behind a rim, where the ground broke: `count` heaps of two to four,
    mostly fallen shards and low clusters, the odd standing shard among them, all leaning away from
    the edge as if the break had heaved them up. Scattered one by one across the grass they read as
    a field of spikes, which nothing explains."""
    for _ in range(count):
        s0, w0 = rng.uniform(5, path.length - 5), rng.uniform(-7.0, -3.0)
        if any(abs(s0 - a) < r for a, r in avoid):
            continue
        out = path.deep_normal(s0)
        away = math.atan2(-out.y, -out.x)
        for _ in range(rng.randint(2, 4)):
            s, w = s0 + rng.uniform(-3.5, 3.5), w0 + rng.uniform(-2.0, 1.0)
            p = path.at(s, w, 0.0)
            kind = rng.random()
            if kind < 0.45:
                g = fallen_shard(rng, rng.uniform(2.5, 5.5), rng.uniform(0.45, 0.8))
                yaw = rng.uniform(0, 6.28)
            elif kind < 0.85:
                g = spike_cluster(rng, rng.randint(2, 4), rng.uniform(0.5, 0.9))
                yaw = rng.uniform(0, 6.28)
            else:
                g = any_spike(rng)
                yaw = away - 0.5 * math.pi + rng.uniform(-0.6, 0.6)
            props.extend(placed(g, (p.x, p.y, z_of(p.x, p.y) - 0.15), yaw))
            marks.add([(p.x, p.y), (p.x + 0.01, p.y)], 2.5)


def plateau_cracks(rng, props, path, count, avoid, marks):
    """Cracks along behind a rim, where the next slabs will part."""
    made = 0
    while made < count:
        s, w = rng.uniform(8, path.length - 8), rng.uniform(-14.0, -5.0)
        if any(abs(s - a) < r for a, r in avoid):
            continue
        line = crack_line(rng, path, s, w, rng.uniform(7.0, 18.0), rng.choice([0.0, math.pi]) + rng.uniform(-0.3, 0.3))
        xy = [(x, y) for x, y, _, _ in line]
        ground_crack(props, rng, xy, rng.uniform(0.4, 0.9), ground_z)
        marks.add(xy, 1.5)
        made += 1


def fissure(rng, props, R, i, depth, blocked, marks):
    """A crack in wall R's column i, and the crack it carries on as across the plateau."""
    tip = cut_crack(R, i, depth)
    s = 0.5 * (R.cols[i] + R.cols[i + 1])
    width = 0.7 * (R.cols[i + 1] - R.cols[i])
    line = crack_line(rng, R.path, s, tip + 0.5, rng.uniform(16.0, 34.0), -0.5 * math.pi)
    xy = [(x, y) for x, y, _, _ in line]
    ground_crack(props, rng, xy, width, ground_z, open_end=True)
    marks.add(xy, 1.5)
    #A branch off it part way along, narrower, splaying off to one side, closed at both ends.
    k = int(len(line) * rng.uniform(0.25, 0.5))
    _, _, bs, bw = line[k]
    heading = -0.5 * math.pi + rng.choice([-1.0, 1.0]) * rng.uniform(0.5, 0.9)
    branch = crack_line(rng, R.path, bs, bw, rng.uniform(6.0, 14.0), heading)
    bxy = [(x, y) for x, y, _, _ in branch]
    ground_crack(props, rng, bxy, 0.45 * width, ground_z, open_end=True)
    marks.add(bxy, 1.5)
    #The ground's hole for the V itself; the crack's strips reach far enough to cover its ragged edge.
    v = [R.path.at(R.cols[i] + t * (R.cols[i + 1] - R.cols[i]), d - 0.5, 0.0) for t, d in R.prof[i]]
    blocked.add([(p.x, p.y) for p in v], 0.6)
    marks.add([(p.x, p.y) for p in v], 2.0)


def forest(rng, trees, paths, clumps, blocked):
    """Clumps of pines with an oak among them, on the plateau, clear of everything in `blocked`."""
    for path in paths:
        for _ in range(clumps):
            cs, cw = rng.uniform(5, path.length - 5), rng.uniform(-8, -60)
            for _ in range(rng.randint(10, 35)):
                p = path.at(cs + rng.gauss(0, 5), cw + rng.gauss(0, 4), 0)
                if not blocked(p.x, p.y):
                    trees.append((p.x, p.y, ground_z(p.x, p.y), "pine" if rng.random() < 0.85 else "oak"))


def island_trees(rng, trees, ctop, zf, r, count, kind="pine"):
    for _ in range(count):
        tx, ty = ctop.x + rng.uniform(-0.45, 0.45) * r, ctop.y + rng.uniform(-0.45, 0.45) * r
        trees.append((tx, ty, zf(tx, ty), kind))


def diorama(rng):
    out = {"dio_rock": Geo(), "dio_water": Geo(), "dio_mist": Geo(), "dio_props": Geo(), "dio_ground": Geo()}
    rock, water, mist, props = out["dio_rock"], out["dio_water"], out["dio_mist"], out["dio_props"]
    trees = []
    blocked, marks = Near(), Near()

    far = Path(jagged_line(rng, -95, 95, FAR_Y, 2.6, 4.0, 9.0), sharp=True)
    near = Path(list(reversed([(x + RIFT_SHEAR, y + NEAR_Y - FAR_Y) for x, y in far.ctrl])), sharp=True)
    crack_x = [-62.0, -22.0, 19.0, 37.0, 66.0]
    s_notch, s_bridge = far.s_at_x(6.0), far.s_at_x(-38.0)
    s_nn_far = across(near, far, near.s_at_x(-20.0))
    slots = [(s_notch - 2.2, s_notch + 2.2), (s_nn_far - 1.8, s_nn_far + 1.8)]
    for x in crack_x:
        s, half = far.s_at_x(x), rng.uniform(1.2, 2.0)
        slots.append((s - half, s + half))

    zs, kinds = make_bands(rng, 0.0, -50.0)
    F = Relief(rng, far, make_cols(rng, 0.0, far.length, slots=slots), zs, kinds)
    F.fill_rock(DIO_STRIP, roughness=1.1, trend=0.4)
    N = matching_wall(rng, F, near, 0.4, 0.4)

    #Younger than the split: the cracks that will be the next blocks to go, the rivers.
    for x in crack_x:
        fissure(rng, props, F, F.column_at(far.s_at_x(x)), rng.uniform(8.0, 14.0), blocked, marks)
    for x in [-40.0, 12.0, 50.0]:
        fissure(rng, props, N, N.column_at(near.s_at_x(x)), rng.uniform(4.0, 8.0), blocked, marks)
    pour = [notch_river(rng, rock, water, F, s_notch - 2.2, s_notch + 2.2, 70.0, blocked, marks, DIO_STEP, strips=4)]
    s_nn = near.s_at_x(-20.0)
    pour.append(notch_river(rng, rock, water, N, across(far, near, s_nn_far + 1.8), across(far, near, s_nn_far - 1.8),
                            70.0, blocked, marks, DIO_STEP, strips=2))
    springs = [cut_spring(F, far.s_at_x(29.0), -9.0), cut_spring(F, far.s_at_x(-10.0), -4.5)]
    F.finish()
    F.emit(rock)
    N.finish()
    N.emit(rock)
    for fall in pour:
        splash(mist, rng, fall(), 4.0, 7, PAL_EFFECTS, PAL_MIST_LIGHT)

    #A natural bridge from the rim out to a pillar of plateau, where the arch was: from above you see
    #the mist on both sides of it, which no arch in a bay managed.
    pillar = far.at(s_bridge, 15.0, 0.0)
    ctop, zf = island(rng, rock, block(rng, pillar.x, pillar.y, 6.0, 1.1, 1.0, rng.uniform(0, 3)), 0.0, -50.0)
    natural_bridge(rng, rock, far.at(s_bridge, -1.0, 0.0), ctop, -0.02, 2.2, -5.0, -20.0, -50.0)
    island_trees(rng, trees, ctop, zf, 6.0, 4)

    #Slumped blocks down the far wall left of the notch, and a level one under the first spring.
    for (x, outw, r, top) in [(-16.0, 1.5, 5.5, -3.5), (-10.0, 5.0, 4.8, -8.5), (-15.0, 9.0, 4.0, -14.0)]:
        c = far.at(far.s_at_x(x), outw, 0)
        ctop, zf = island(rng, rock, block(rng, c.x, c.y, r, 1.25, 0.85, rng.uniform(-0.4, 0.4)), top, -50.0,
                          tilt=rng.uniform(0.03, 0.07))
        island_trees(rng, trees, ctop, zf, r, rng.randint(1, 3))
    floor, w_from, slip, width, s_mid = springs[0]
    c = far.at(s_mid, 4.0, 0)
    ctop, _ = island(rng, rock, block(rng, c.x, c.y, 4.8, 1.3, 0.9), floor - 11.0, -50.0)
    end = falls(water, rng, far, s_mid, width, floor, w_from, slip, ctop.z - 0.15, out=1.5, spread=0.05, strips=2)
    splash(water, rng, end, 1.4, 4)
    floor, w_from, slip, width, s_mid = springs[1]
    falls(water, rng, far, s_mid, width, floor, w_from, slip, MIST_Z - 1.0, out=1.2, spread=0.04, strips=2)
    #A slab standing off the wall right of the springs, just below the rim: a balcony for a winch.
    c = far.at(far.s_at_x(50.0), 2.0, 0)
    ctop, zf = island(rng, rock, block(rng, c.x, c.y, 5.0, 1.4, 0.8), -12.0, -50.0, tilt=0.05)
    island_trees(rng, trees, ctop, zf, 5.0, 2, "oak")

    #Blocks that fell into the middle and stuck, tilted.
    for (x, y, r, top, sx, sy) in [(-8.0, -1.0, 7.0, -24.0, 1.3, 0.9), (24.0, -5.0, 3.4, -16.0, 1.0, 1.0),
                                   (-58.0, 2.0, 5.5, -20.0, 1.0, 1.3), (54.0, -6.0, 6.0, -26.0, 1.4, 0.8)]:
        ctop, zf = island(rng, rock, block(rng, x, y, r, sx, sy, rng.uniform(0, 3)), top, -50.0,
                          tilt=rng.uniform(0.05, 0.11))
        island_trees(rng, trees, ctop, zf, r, int(r * 0.7))
        if r > 5:
            props.extend(placed(spike_cluster(rng, 3, 0.8), (ctop.x, ctop.y, zf(ctop.x, ctop.y)), rng.uniform(0, 6.28)))

    #Spires: shards the size of the walls, standing in the mist.
    for (x, y) in [(-28.0, -3.0), (12.0, 3.0), (38.0, -10.0), (-70.0, -6.0), (72.0, 0.0), (-44.0, -10.0)]:
        s = spike(rng, rng.uniform(30.0, 44.0), rng.uniform(3.8, 5.5), 6, 5, (rng.uniform(-3, 3), rng.uniform(-3, 3)),
                  twist=0.25, sink=2.0, flat=0.75, tip_cut=rng.choice([0.0, 0.0, 0.2]))
        rock.extend(placed(s, (x, y, -48.0), rng.uniform(0, 6.28)))

    plateau_props(rng, props, far, 4, [(s_notch, 6), (s_bridge, 8)], marks)
    plateau_props(rng, props, near, 3, [(s_nn, 6)], marks)
    plateau_cracks(rng, props, far, 7, [(s_notch, 12), (s_bridge, 12)], marks)
    plateau_cracks(rng, props, near, 4, [(s_nn, 12)], marks)

    hole = chasm_outline([far, near], OUTLINE_W)
    ground(out["dio_ground"], rng, -95.0, 95.0, -85.0, 85.0, DIO_STEP, lambda x, y: hole.inside(x, y) or blocked.near(x, y))
    m = [Vector((-100, -40, MIST_Z)), Vector((100, -40, MIST_Z)), Vector((100, 40, MIST_Z)), Vector((-100, 40, MIST_Z))]
    mist.add(m, PAL_MIST_LIGHT, Vector((0, 0, 1)), PAL_EFFECTS)

    keep_out = chasm_outline([far, near], -4.5)
    forest(rng, trees, [far, near], 16, lambda x, y: keep_out.inside(x, y) or marks.near(x, y) or abs(x) > 94 or abs(y) > 84)
    return out, trees


# ---------------------------------------------------------------------------------------------
# The rift: the whole of a game-sized chasm, as the game's map would have it - one crack up the
# middle of the valley, closing at the north end, both walls cut from it. What the terrain's walls
# could be; built here first, so it can be judged before any of it goes into TerrainMesh.cpp.
# ---------------------------------------------------------------------------------------------

RIFT_AT = Vector((0.0, 1500.0, 0.0))    #far from the diorama and the kit: no camera sees another's
MAP_X, MAP_Y = 400.0, 225.0             #the game's map, 100,000 fine cells at 16:9
RIFT_STRIP = 7.5                        #the ground is coarser here, so the strips reach further
RIFT_STEP = 2.5


def rift_rims(rng):
    """The crack, then the two rims it opened into. The crack zig-zags at the scale of the walls; the
    rims are the crack carried out either side by half the opening, and a little along it as well, so
    each wall is the other's cast. The opening tapers to a slot at the north end."""
    axis = Path([(rng.uniform(-20, 20), -MAP_Y - 6.0), (rng.uniform(-35, 35), -100.0), (rng.uniform(-35, 35), 10.0),
                 (rng.uniform(-30, 30), 110.0), (rng.uniform(-15, 15), 185.0)])
    length = axis.length
    crack, s, side = [], 0.0, 1.0
    while True:
        p, n = axis.point(s), axis.normal(s)
        crack.append((p + n * side * rng.uniform(0.3, 1.0) * 4.5 * (0.0 if s == 0.0 else 1.0), s))
        if s >= length:
            break
        s = min(length, s + rng.uniform(5.0, 11.0))
        side = -side if rng.random() < 0.75 else side
    ph1, ph2 = rng.uniform(0, 6), rng.uniform(0, 6)

    def half(s):
        u = s / length
        taper = max(0.0, min(1.0, (length - s) / 130.0))
        taper = taper * taper * (3 - 2 * taper)
        return 0.7 + (36.0 + 7.0 * math.sin(u * 6.0 + ph1) + 4.0 * math.sin(u * 17.0 + ph2)) * taper
    west, east = [], []
    for p, s in crack:
        t, n, h = axis.tangent(s, 6.0), axis.normal(s), half(s)
        west.append(tuple((p - n * h + t * h * 0.1)[:2]))
        east.append(tuple((p + n * h - t * h * 0.1)[:2]))
    return Path(west, sharp=True), Path(list(reversed(east)), sharp=True), axis, half


def end_wall(rng, geo, a, b, depth):
    """The few units of wall that close a rift's slot end, across from one rim's end to the other's."""
    path = Path([a, b], sharp=True)
    zs, kinds = make_bands(rng, 0.0, -50.0)
    R = Relief(rng, path, [0.0, path.length], zs, kinds)
    R.fill_rock(depth, roughness=0.5, trend=0.2)
    R.finish()
    R.emit(geo)


def rift(rng):
    out = {"rift_rock": Geo(), "rift_water": Geo(), "rift_mist": Geo(), "rift_props": Geo(), "rift_ground": Geo()}
    rock, water, mist, props = out["rift_rock"], out["rift_water"], out["rift_mist"], out["rift_props"]
    trees = []
    blocked, marks = Near(), Near()
    west, east, axis, half = rift_rims(rng)
    rims = [west, east]
    views = {}

    #Where things go, as fractions along the west rim; an east rim feature is chosen on the west's
    #parameter too and carried across, so its slot can be made in both walls' shared columns.
    def ws(u):
        return u * west.length
    rivers = [(west, ws(0.30)), (west, ws(0.66)), (east, across(west, east, ws(0.47)))]
    crack_w = [ws(u) for u in (0.12, 0.22, 0.41, 0.55, 0.76, 0.86)]
    crack_e = [ws(u) for u in (0.17, 0.35, 0.60, 0.70, 0.81)]
    slots = []
    for path, s in rivers:
        sw = s if path is west else across(east, west, s)
        slots.append((sw - 2.2, sw + 2.2))
    for s in crack_w + crack_e:
        h = rng.uniform(1.4, 2.4)
        slots.append((s - h, s + h))

    zs, kinds = make_bands(rng, 0.0, -50.0)
    W = Relief(rng, west, make_cols(rng, 0.0, west.length, slots=slots), zs, kinds)
    W.fill_rock(RIFT_STRIP, roughness=1.1, trend=0.4)
    E = matching_wall(rng, W, east, 0.4, 0.4)

    for s in crack_w:
        fissure(rng, props, W, W.column_at(s), rng.uniform(6.0, 14.0), blocked, marks)
    for s in crack_e:
        fissure(rng, props, E, E.column_at(across(west, east, s)), rng.uniform(5.0, 12.0), blocked, marks)
    pour = []
    for path, s in rivers:
        R = W if path is west else E
        if path is west:
            sa, sb = s - 2.2, s + 2.2
        else:
            sw = across(east, west, s)
            sa, sb = across(west, east, sw + 2.2), across(west, east, sw - 2.2)
        pour.append(notch_river(rng, rock, water, R, sa, sb, 900.0, blocked, marks, RIFT_STEP, strips=4,
                                edge=(0.0, 0.0, MAP_X, MAP_Y)))
        p = path.at(0.5 * (sa + sb), 0.0, 0.0)
        views.setdefault("rift_falls", (p + path.normal(0.5 * (sa + sb)) * 22.0, path))
    springs = []
    for R, us in [(W, (0.18, 0.5, 0.8)), (E, (0.26, 0.64))]:
        for u in us:
            s = ws(u) if R is W else across(west, east, ws(u))
            springs.append((R, cut_spring(R, s + 3.0, rng.uniform(-12.0, -4.0))))
    W.finish()
    W.emit(rock)
    E.finish()
    E.emit(rock)
    end_wall(rng, rock, west.ctrl[-1], east.ctrl[0], RIFT_STRIP)
    for fall in pour:
        splash(mist, rng, fall(), 5.0, 8, PAL_EFFECTS, PAL_MIST_LIGHT)
    for R, (floor, w_from, slip, width, s_mid) in springs:
        falls(water, rng, R.path, s_mid, width, floor, w_from, slip, MIST_Z - 1.0, out=1.3, spread=0.04, strips=2)

    #Inside the rift. Each placement is tried against what is already there and against the walls.
    taken = []

    def free(x, y, r, wall_gap):
        if any((x - a) ** 2 + (y - b) ** 2 < (r + c + 6.0) ** 2 for a, b, c in taken):
            return False
        for path in rims:
            for s in range(0, int(path.length), 6):
                q = path.point(float(s))
                if (q.x - x) ** 2 + (q.y - y) ** 2 < (r + wall_gap) ** 2:
                    return False
        return True

    def in_rift(u, frac):
        s = u * axis.length
        p, n = axis.point(s), axis.normal(s)
        return p + n * frac * half(s)

    #Shards: the blocks that broke loose and dropped, tops at the game's island level.
    shard_tops = []
    for _ in range(60):
        if len(shard_tops) >= 7:
            break
        u, r = rng.uniform(0.06, 0.74), rng.uniform(8.0, 15.0)
        c = in_rift(u, rng.uniform(-0.55, 0.55))
        if not free(c.x, c.y, r, 8.0):
            continue
        taken.append((c.x, c.y, r))
        ctop, zf = island(rng, rock, block(rng, c.x, c.y, r, rng.uniform(0.9, 1.4), rng.uniform(0.7, 1.1), rng.uniform(0, 3)),
                          -24.0, -50.0, tilt=rng.uniform(0.03, 0.08))
        shard_tops.append((ctop, zf, r))
        island_trees(rng, trees, ctop, zf, r, int(r * 0.9))
        if rng.random() < 0.5:
            props.extend(placed(spike_cluster(rng, 4, 1.0), (ctop.x, ctop.y, zf(ctop.x, ctop.y)), rng.uniform(0, 6.28)))
    #A natural bridge between the two nearest shards, and one from the west rim to a pillar.
    best = None
    for a in range(len(shard_tops)):
        for b in range(a + 1, len(shard_tops)):
            d = (shard_tops[a][0] - shard_tops[b][0]).length - shard_tops[a][2] - shard_tops[b][2]
            if 6.0 < d < 40.0 and (best is None or d < best[0]):
                best = (d, a, b)
    if best:
        _, a, b = best
        natural_bridge(rng, rock, shard_tops[a][0], shard_tops[b][0], -24.02, 2.4, -29.0, -40.0, -50.0, span=0.5)
    s_b = ws(0.38)
    pillar = west.at(s_b, 16.0, 0.0)
    if free(pillar.x, pillar.y, 6.5, 4.0):
        ctop, zf = island(rng, rock, block(rng, pillar.x, pillar.y, 6.5, 1.1, 1.0, rng.uniform(0, 3)), 0.0, -50.0)
        natural_bridge(rng, rock, west.at(s_b, -1.0, 0.0), ctop, -0.02, 2.2, -5.0, -20.0, -50.0)
        island_trees(rng, trees, ctop, zf, 6.5, 4)
        taken.append((pillar.x, pillar.y, 6.5))
        views["rift_bridge"] = (pillar, west)
    #Balconies: blocks slumped against the walls a step below the rim, where a winch reaches them.
    for R, us in [(W, (0.08, 0.27, 0.58, 0.72)), (E, (0.2, 0.45, 0.78))]:
        for u in us:
            s = ws(u) if R is W else across(west, east, ws(u))
            r = rng.uniform(6.0, 11.0)
            c = R.path.at(s, r * 0.55, 0.0)
            ctop, zf = island(rng, rock, block(rng, c.x, c.y, r, 1.4, 0.8, rng.uniform(-0.4, 0.4)), -12.0, -50.0,
                              tilt=rng.uniform(0.02, 0.05))
            island_trees(rng, trees, ctop, zf, r, int(r * 0.5))
    #Columns and spires.
    for k in range(40):
        u = rng.uniform(0.04, 0.85)
        c = in_rift(u, rng.uniform(-0.8, 0.8))
        tall = k % 2 == 0
        r = rng.uniform(3.5, 6.0) if tall else rng.uniform(3.5, 5.5)
        if not free(c.x, c.y, r, 5.0):
            continue
        taken.append((c.x, c.y, r))
        if tall:
            island(rng, rock, block(rng, c.x, c.y, r, 1.0, 1.0, 0.0), rng.uniform(-30.0, -6.0), -50.0,
                   trend=0.2, roughness=0.6, tilt=rng.uniform(0.04, 0.12))
        else:
            s = spike(rng, rng.uniform(26.0, 44.0), r, 6, 5, (rng.uniform(-3, 3), rng.uniform(-3, 3)),
                      twist=0.25, sink=2.0, flat=0.75, tip_cut=rng.choice([0.0, 0.0, 0.2]))
            rock.extend(placed(s, (c.x, c.y, -48.0), rng.uniform(0, 6.28)))

    avoid_w = [(across(east, west, s) if p is east else s, 10.0) for p, s in rivers]
    plateau_props(rng, props, west, 12, avoid_w, marks)
    plateau_props(rng, props, east, 12, [(across(west, east, a), r) for a, r in avoid_w], marks)
    plateau_cracks(rng, props, west, 10, avoid_w, marks)
    plateau_cracks(rng, props, east, 10, [(across(west, east, a), r) for a, r in avoid_w], marks)

    hole = chasm_outline(rims, OUTLINE_W)
    ground(out["rift_ground"], rng, -MAP_X, MAP_X, -MAP_Y, MAP_Y, RIFT_STEP, lambda x, y: hole.inside(x, y) or blocked.near(x, y))
    m = [Vector((-MAP_X, -MAP_Y, MIST_Z)), Vector((MAP_X, -MAP_Y, MIST_Z)), Vector((MAP_X, MAP_Y, MIST_Z)), Vector((-MAP_X, MAP_Y, MIST_Z))]
    mist.add(m, PAL_MIST_LIGHT, Vector((0, 0, 1)), PAL_EFFECTS)
    keep_out = chasm_outline(rims, -4.5)
    forest(rng, trees, rims, 45, lambda x, y: keep_out.inside(x, y) or marks.near(x, y) or abs(x) > MAP_X - 1 or abs(y) > MAP_Y - 1)
    views["rift_tip"] = (Vector(west.ctrl[-1] + (0.0,)) + Vector((0.0, -30.0, 0.0)), west)
    return out, trees, views


# ---------------------------------------------------------------------------------------------
# Objects, staging, export.
# ---------------------------------------------------------------------------------------------

def build_object(name, geo, material, collection, location=(0, 0, 0)):
    bm = bmesh.new()
    pal = bm.faces.layers.int.new("pal")
    row = bm.faces.layers.int.new("row")
    for pts, col, r in geo.faces:
        verts = [bm.verts.new(p) for p in pts]
        try:
            f = bm.faces.new(verts)
        except ValueError:
            continue
        f[pal] = col
        f[row] = r
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-4)
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    bm.normal_update()
    uv = bm.loops.layers.uv.new("UVMap")
    for f in bm.faces:
        u = pal_uv(f[pal], f[row])
        for loop in f.loops:
            loop[uv].uv = u
        f.smooth = False
    bm.faces.layers.int.remove(pal)
    bm.faces.layers.int.remove(row)
    tris = len(bm.faces)
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    mesh.materials.append(material)
    obj = bpy.data.objects.new(name, mesh)
    obj.location = location
    collection.objects.link(obj)
    return obj, tris


def palette_material():
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


EXPORT_SETTINGS = dict(
    export_format="GLB",
    export_yup=True,
    export_apply=True,
    export_texcoords=True,
    export_normals=True,
    export_tangents=False,
    export_materials="EXPORT",
    export_image_format="NONE",
    export_vertex_color="NONE",
    export_animations=False,
    export_skins=False,
    export_morph=False,
    export_cameras=False,
    export_lights=False,
    export_extras=False,
)


def setup_export(collection):
    with bpy.context.temp_override(collection=collection):
        bpy.ops.collection.exporter_add(name="IO_FH_gltf2")
    props = collection.exporters[-1].export_properties
    for key, value in EXPORT_SETTINGS.items():
        setattr(props, key, value)
    props.filepath = "//" + os.path.relpath(GLB, os.path.dirname(BLEND)).replace("\\", "/")
    bpy.context.scene["glTF2ExportSettings"] = dict(EXPORT_SETTINGS, use_active_collection=True, will_save_settings=True)


def place_trees(trees, collection, prefix="tree"):
    """Pines and oaks from chasm_props.blend, linked duplicates - only for judging scale."""
    if not os.path.exists(PROPS_BLEND):
        return 0
    want = ["tree_pine_a", "tree_pine_b", "tree_pine_c", "tree_pine_d", "tree_oak_a", "tree_oak_b", "tree_oak_c"]
    with bpy.data.libraries.load(PROPS_BLEND, link=False) as (src, dst):
        dst.meshes = [n for n in want if n in src.meshes]
    meshes = {m.name: m for m in dst.meshes if m}
    pines = [meshes[n] for n in want if n.startswith("tree_pine") and n in meshes]
    oaks = [meshes[n] for n in want if n.startswith("tree_oak") and n in meshes]
    rng = random.Random(5)
    for k, (x, y, z, kind) in enumerate(trees):
        pool = pines if kind == "pine" else oaks
        if not pool:
            continue
        obj = bpy.data.objects.new("%s_%04d" % (prefix, k), rng.choice(pool))
        obj.location = (x, y, z)
        obj.rotation_euler = (0, 0, rng.uniform(0, 6.28))
        s = rng.uniform(0.85, 1.2)
        obj.scale = (s, s, s)
        collection.objects.link(obj)
    return len(trees)


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


def depth_fog(coll, name, centre, x, y, z_top=-2.0, z_bottom=-56.0, density=0.11):
    """The diorama's abyss filling with mist, thicker the deeper it goes - what the game's Mist does
    for the chasm, and what makes an arch read: through it you see haze, not more of the same rock.
    Preview only: a volume box over one scene's chasm, `x` and `y` its half sizes, clear of the rest."""
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=2.0)
    bm.to_mesh(me)
    bm.free()
    obj = bpy.data.objects.new(name, me)
    obj.location = (centre[0], centre[1], 0.5 * (z_top + z_bottom))
    obj.scale = (x, y, 0.5 * (z_top - z_bottom))
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
    ramp.inputs["From Min"].default_value = 1.0          #the box's top: clear
    ramp.inputs["From Max"].default_value = -1.0         #its bottom: thickest
    power = nodes.new("ShaderNodeMath")
    power.operation = "POWER"
    power.inputs[1].default_value = 1.3
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


def stage(coll, rift_views):
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
    depth_fog(coll, "fog_dio", (0.0, 0.0), 100.0, 45.0)
    depth_fog(coll, "fog_rift", (RIFT_AT.x, RIFT_AT.y), MAP_X, MAP_Y)
    cams = {
        "game": add_camera(coll, "cam_game", (0, 6, -6), 55, 0, 125, 50),
        "oblique": add_camera(coll, "cam_oblique", (-8, 12, -10), 32, -28, 95, 40),
        "bridge": add_camera(coll, "cam_bridge", (-40, 26, -8), 48, 10, 70, 40),
        "kit": add_camera(coll, "cam_kit", (5.0 * 32.0, -204.0, -20), 18, 0, 400, 40),
        "props": add_camera(coll, "cam_props", (6.5 * 12.0, -140.0, 1.0), 40, 0, 190, 40),
        "rift_overview": add_camera(coll, "cam_rift_overview", RIFT_AT + Vector((0.0, 20.0, -10.0)), 62, 0, 900, 35),
    }
    for name, (target, _) in rift_views.items():
        cams[name] = add_camera(coll, "cam_" + name, RIFT_AT + Vector((target.x, target.y, -8.0)), 55, 0, 125, 50)
    return cams


def render(cams):
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
    #Occlusion in the crevices - the game has SSAO - and the depth fog's range.
    for key, value in [("use_raytracing", True), ("ray_tracing_method", "SCREEN"), ("use_fast_gi", True),
                       ("fast_gi_method", "AMBIENT_OCCLUSION_ONLY"), ("fast_gi_distance", 6.0),
                       ("volumetric_end", 1400.0), ("volumetric_tile_size", "4"), ("volumetric_samples", 64)]:
        try:
            setattr(scene.eevee, key, value)
        except (AttributeError, TypeError):
            print("eevee has no %s here" % key)
    scene.render.image_settings.file_format = "PNG"
    try:
        scene.eevee.taa_render_samples = 32
        scene.eevee.shadow_resolution_scale = 1.0
    except AttributeError:
        pass
    os.makedirs(PREVIEWS, exist_ok=True)
    dio, rift_coll = bpy.data.collections["Diorama"], bpy.data.collections["Rift"]
    for name, cam in cams.items():
        scene.camera = cam
        in_rift = name.startswith("rift")
        dio.hide_render = in_rift or name in ("kit", "props")
        rift_coll.hide_render = not in_rift
        bpy.data.objects["fog_dio"].hide_render = dio.hide_render
        bpy.data.objects["fog_rift"].hide_render = not in_rift
        for obj in bpy.data.collections["Export"].objects:
            row = obj.get("kit_row", 0)
            obj.hide_render = (name == "kit" and row == 1) or (name == "props" and row != 1) or in_rift
        scene.render.filepath = os.path.join(PREVIEWS, "cliffs_%s.png" % name)
        bpy.ops.render.render(write_still=True)
        print("rendered %s" % scene.render.filepath)


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if os.path.exists(BLEND) and "--force" not in argv:
        print("\n%s exists and may be edited by hand; pass --force to rebuild it from scratch." % BLEND)
        sys.exit(1)

    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    material = palette_material()

    export = bpy.data.collections.new("Export")
    scene.collection.children.link(export)
    dio = bpy.data.collections.new("Diorama")
    scene.collection.children.link(dio)
    rift_coll = bpy.data.collections.new("Rift")
    scene.collection.children.link(rift_coll)
    stage_coll = bpy.data.collections.new("Stage")
    scene.collection.children.link(stage_coll)

    print("\n%-20s %6s" % ("kit piece", "tris"))
    slots = {}
    for _, seed, builder, row in KIT:
        slot = slots.get(row, 0)
        slots[row] = slot + 1
        spacing, y, x0, z = KIT_ROWS[row]
        at = (x0 + slot * spacing, y, z)
        for name, geo in builder(random.Random(seed)).items():
            obj, tris = build_object(name, geo, material, export, at)
            obj["kit_row"] = row
            print("%-20s %6d" % (name, tris))

    parts, trees = diorama(random.Random(2026))
    for name, geo in parts.items():
        _, tris = build_object(name, geo, material, dio)
        print("%-20s %6d" % (name, tris))
    print("trees: %d" % place_trees(trees, dio))

    parts, trees, rift_views = rift(random.Random(95))
    for name, geo in parts.items():
        _, tris = build_object(name, geo, material, rift_coll, RIFT_AT)
        print("%-20s %6d" % (name, tris))
    print("trees: %d" % place_trees([(x + RIFT_AT.x, y + RIFT_AT.y, z, k) for x, y, z, k in trees], rift_coll, "rift_tree"))

    cams = stage(stage_coll, rift_views)
    setup_export(export)

    os.makedirs(os.path.dirname(BLEND), exist_ok=True)
    bpy.data.images["palette.png"].filepath = "//" + os.path.relpath(PALETTE, os.path.dirname(BLEND)).replace("\\", "/")
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=BLEND, relative_remap=False)
    with bpy.context.temp_override(collection=export):
        bpy.ops.collection.export_all()
    print("wrote %s (%d bytes)" % (GLB, os.path.getsize(GLB)))

    if "--render" in argv:
        render(cams)


main()
