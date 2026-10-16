"""Builds the FIRST art_source/chasm/chasm_props.blend - Chasm's low-poly props - and exports it.

    blender.exe -b --python apps/chasm/tools/blender_chasm_props.py -- [--add [--replace NAMES | --merge] | --force]

Writes the .blend and apps/chasm/assets/meshes/chasm_props.glb. The .blend is the source from then
on and is edited by hand, so this refuses to overwrite it without --force - re-running it throws
away every edit made in Blender since. To re-export after an edit, use the "Export" collection's
exporter (Collection properties > Exporters > Export), which this sets up; no script needed.

--add is for a new asset in ASSETS: it opens the existing .blend, builds only the assets that have
no object of that name yet, saves and exports - with the settings File > Export remembers in the file,
as it is exported by hand (export_as_last). Nothing already in the file is touched - but an asset
renamed in Blender counts as missing and is built again under its old name, and a builder may edit the
asset it builds on (plant_fruit moves a plant's own fruit off it). --replace NAME[,NAME] with --add
deletes those assets first, to rebuild them after a change here; their edits are lost. The export is
the whole file's: check its clips against the last GLB (2026-10-12: the saved file had lost Idle).

--merge with --add exports ONLY the assets it built and appends them to the GLB as it stands, which
keeps every byte already in it (save_and_merge) - for while the file and the GLB disagree. 2026-10-14
they did: the woman's rig and mesh and the Idle clip had been edited in the file since the last export,
so a whole export would have shipped those with the new props.

Everything is generated from fixed seeds, so a forced rebuild gives the same props.

THE LOOK (README "No textures: one palette"): flat-shaded facets, every face one colour, picked by
pointing all of the face's UVs at the centre of one palette cell (Palette.h). Row 0, but for the few
cells that are the same in every biome and season (cell()). A face's colour comes partly from which way it faces (lighter on
top, darker underneath) - a little baked occlusion, on top of what the sun does in game.

BUDGET: conifers are the forests, drawn instanced by the ten thousand, so they stay near 50
triangles; nothing here is over about 110. Faces nobody can see - trunk and rock bottoms, which sit
in the ground - are not built at all.
"""

import json
import math
import os
import random
import struct
import sys
import tempfile

import bmesh
import bpy
from mathutils import Euler, Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
BLEND = os.path.join(REPO, "art_source", "chasm", "chasm_props.blend")
GLB = os.path.join(REPO, "apps", "chasm", "assets", "meshes", "chasm_props.glb")
PALETTE = os.path.join(REPO, "apps", "chasm", "assets", "textures", "palette.png")

#Palette.h - must agree with it.
PALETTE_COLS = 32
PALETTE_ROWS = 16
PAL_GRASS_0 = 0         #four close shades, 0-3
PAL_PATH = 13           #borrowed for cut wood (stump tops, log ends): there is no free column for one
PAL_PINE_DARK = 16
PAL_PINE = 17
PAL_PINE_LIGHT = 18
PAL_LEAF_DARK = 19
PAL_LEAF = 20
PAL_LEAF_LIGHT = 21
PAL_BARK = 22
PAL_BARK_DARK = 23
PAL_STONE_LIGHT = 24
PAL_STONE = 25
PAL_STONE_DARK = 26
PAL_BUSH = 27
PAL_ACCENT = 28
PAL_FIELD = 12          #ripe wheat: straw
PAL_WATER = 14
PAL_WALL = 29           #plaster: a pale white
PAL_TIMBER = 31
PAL_ROCK_0 = 5          #the walls' strata (TerrainMesh.cpp): their lightest and their near-rim pair,
PAL_ROCK_1 = 6          #for the mine portal's rock, which sinks into a wall
PAL_ROCK_3 = 8


def cell(col, row):
    """A cell off row 0, kept in the same face layer as a column: one of the few colours the same in
    every biome and season, which no row shift may move."""
    return col + row * PALETTE_COLS


PAL_ROPE = cell(2, 9)               #PAL_CROPS' hemp rope: the crane's lines
PAL_VOID = cell(5, 15)              #PAL_EFFECTS' black: the mine's mouth
PAL_FLOATSTONE_LIGHT = cell(0, 10)  #PAL_MINERALS: floatstone's crystal, light to dark, and its glint
PAL_FLOATSTONE = cell(1, 10)
PAL_FLOATSTONE_DARK = cell(2, 10)
PAL_FLOATSTONE_GLINT = cell(3, 10)

GROUND_SINK = 0.08      #how far trunks and rock bases reach below z = 0, so a slope does not show a gap


def pal_uv(col, row=0):
    #Blender's V runs up from the image's BOTTOM; the engine's row 0 is the image's TOP row. The glTF
    #exporter flips V on the way out, so this lands on Palette.h's PaletteUV() in the file.
    return ((col + 0.5) / PALETTE_COLS, 1.0 - (row + 0.5) / PALETTE_ROWS)


# ---------------------------------------------------------------------------------------------
# Parts. Each adds geometry to the asset's bmesh and returns the faces it made, so the caller can
# colour exactly those.
# ---------------------------------------------------------------------------------------------

def ring(bm, centre, radius, sides, rot, rng, jitter):
    """A horizontal ring of verts, counter-clockwise seen from above. Only the radius is jittered,
    so the ring stays flat and a cap on it stays a planar n-gon."""
    verts = []
    for i in range(sides):
        a = rot + 2.0 * math.pi * i / sides
        r = radius * (1.0 + rng.uniform(-jitter, jitter))
        verts.append(bm.verts.new((centre.x + r * math.cos(a), centre.y + r * math.sin(a), centre.z)))
    return verts


def add_cone(bm, centre, radius, height, sides, rot, rng, jitter=0.0, tip=(0.0, 0.0), bottom=True):
    """A cone tier: sides, plus a flat bottom - which the camera sees between the tiers of a pine,
    so it is kept, and coloured dark."""
    base = ring(bm, centre, radius, sides, rot, rng, jitter)
    apex = bm.verts.new((centre.x + tip[0], centre.y + tip[1], centre.z + height))
    side = [bm.faces.new((base[i], base[(i + 1) % sides], apex)) for i in range(sides)]
    cap = [bm.faces.new(list(reversed(base)))] if bottom else []
    return side, cap


def add_prism(bm, centre, radius, height, sides, rot, rng, top_radius=None, jitter=0.0,
              top=False, top_tilt=(0.0, 0.0)):
    """An upright prism (a trunk, a stump), open at the bottom. `top_tilt` slopes the top ring, for a
    stump's slanted cut."""
    lo = ring(bm, centre, radius, sides, rot, rng, jitter)
    hi = ring(bm, centre + Vector((0, 0, height)), top_radius if top_radius else radius, sides, rot,
              rng, jitter)
    for v in hi:
        v.co.z += (v.co.x - centre.x) * top_tilt[0] + (v.co.y - centre.y) * top_tilt[1]
    side = [bm.faces.new((lo[i], lo[(i + 1) % sides], hi[(i + 1) % sides], hi[i])) for i in range(sides)]
    cap = [bm.faces.new(hi)] if top else []
    return side, cap


def add_hull(bm, points, drop_below=None):
    """The convex hull of `points`: the faceted lump every rock, bush and broadleaf crown is made of.
    Coplanar triangles are merged, and with `drop_below` the faces lying entirely below that height
    - a base resting in the ground - are deleted."""
    verts = [bm.verts.new(p) for p in points]
    res = bmesh.ops.convex_hull(bm, input=verts, use_existing_faces=False)
    #A set: a point can be listed as both interior and unused, and delete refuses a repeat.
    loose = list({g for g in res["geom_interior"] + res["geom_unused"] if isinstance(g, bmesh.types.BMVert)})
    bmesh.ops.delete(bm, geom=loose, context="VERTS")
    faces = [g for g in res["geom"] if isinstance(g, bmesh.types.BMFace) and g.is_valid]
    bmesh.ops.recalc_face_normals(bm, faces=faces)
    hull_verts = list({v for f in faces for v in f.verts})
    edges = list({e for f in faces for e in f.edges})
    bmesh.ops.dissolve_limit(bm, angle_limit=math.radians(1.0), verts=[], edges=edges)
    #Not dissolve_limit's "region": that holds only the faces it merged, not the ones it left alone.
    faces = list({f for v in hull_verts if v.is_valid for f in v.link_faces})
    if drop_below is not None:
        under = [f for f in faces if all(v.co.z <= drop_below for v in f.verts)]
        bmesh.ops.delete(bm, geom=under, context="FACES_ONLY")
        faces = [f for f in faces if f.is_valid]
    return faces


def ellipsoid_points(rng, centre, rx, ry, rz, count, jitter, z_floor=None):
    """Points scattered over an ellipsoid's surface, pushed in and out by `jitter`. Fibonacci-spaced
    rather than random, so no two bunch up into a sliver of a facet."""
    pts = []
    golden = math.pi * (3.0 - math.sqrt(5.0))
    turn = rng.uniform(0, 2 * math.pi)
    for i in range(count):
        z = 1.0 - 2.0 * (i + 0.5) / count
        r = math.sqrt(max(0.0, 1.0 - z * z))
        a = turn + golden * i
        k = 1.0 + rng.uniform(-jitter, jitter)
        p = Vector((centre.x + rx * r * math.cos(a) * k, centre.y + ry * r * math.sin(a) * k,
                    centre.z + rz * z * k))
        if z_floor is not None:
            p.z = max(p.z, z_floor)
        pts.append(p)
    return pts


# ---------------------------------------------------------------------------------------------
# Colouring
# ---------------------------------------------------------------------------------------------

def colour(bm, faces, col):
    layer = bm.faces.layers.int["pal"]
    for f in faces:
        f[layer] = col


def colour_by_facing(bm, faces, top, side, under, up=0.55, down=-0.30):
    """Lighter where a face looks at the sky, darker where it looks at the ground."""
    layer = bm.faces.layers.int["pal"]
    for f in faces:
        f.normal_update()
        n = f.normal.z
        f[layer] = top if n > up else (under if n < down else side)


def lean(bm, height, dx, dy, power=1.6):
    """Bends the whole asset over: nothing moves at the ground, the top moves by (dx, dy)."""
    for v in bm.verts:
        t = max(0.0, v.co.z) / height
        f = t ** power
        v.co.x += dx * f
        v.co.y += dy * f


# ---------------------------------------------------------------------------------------------
# Assets
# ---------------------------------------------------------------------------------------------

def conifer(bm, rng, height, tiers, width, sides, trunk_show, lean_xy, dark=False):
    """Stacked cones on a stub of trunk. Each tier starts a little over halfway up the one below and
    is turned and nudged off-centre, so the tiers do not line up into one smooth cone."""
    top_scale = 1.10                       #the top tier a little taller, a point but not a spike
    overlap = 0.55                         #where the next tier starts, as a fraction of this one
    tier_h = (height - trunk_show) / ((tiers - 1) * overlap + top_scale)
    trunk_r = 0.09 + 0.025 * width
    side, _ = add_prism(bm, Vector((0, 0, -GROUND_SINK)), trunk_r, trunk_show + GROUND_SINK + tier_h * 0.3,
                        5, rng.uniform(0, 6.3), rng, top_radius=trunk_r * 0.8)
    colour(bm, side, PAL_BARK_DARK if dark else PAL_BARK)

    z = trunk_show
    for i in range(tiers):
        last = i == tiers - 1
        t = i / max(1, tiers - 1)
        r = width * 0.5 * (1.0 - 0.38 * t) * rng.uniform(0.93, 1.07)
        h = tier_h * (top_scale if last else 1.0)
        off = Vector((rng.uniform(-0.04, 0.04), rng.uniform(-0.04, 0.04), 0)) * width
        tip = (rng.uniform(-0.05, 0.05) * width, rng.uniform(-0.05, 0.05) * width)
        side, cap = add_cone(bm, Vector((0, 0, z)) + off, r, h, sides, rng.uniform(0, 6.3), rng,
                             jitter=0.10, tip=tip)
        if dark:
            colour(bm, side, PAL_PINE_DARK if i < tiers - 1 else PAL_PINE)
        else:
            colour(bm, side, PAL_PINE_LIGHT if last else PAL_PINE)
        colour(bm, cap, PAL_PINE_DARK)
        z += tier_h * overlap
    lean(bm, height, lean_xy[0], lean_xy[1])


def trunk_with_fork(bm, rng, height, radius, dark=False):
    """A broadleaf trunk: a tapering prism that disappears into the crown."""
    side, _ = add_prism(bm, Vector((0, 0, -GROUND_SINK)), radius, height + GROUND_SINK, 5,
                        rng.uniform(0, 6.3), rng, top_radius=radius * 0.65)
    colour(bm, side, PAL_BARK_DARK if dark else PAL_BARK)


def broadleaf_round(bm, rng, height, crown_r):
    """One big round crown - a jittered icosphere, squashed a little - on a short trunk."""
    trunk_h = height - crown_r * 1.7
    trunk_with_fork(bm, rng, trunk_h + crown_r * 0.4, 0.16)
    centre = Vector((0, 0, height - crown_r * 0.88))
    res = bmesh.ops.create_icosphere(bm, subdivisions=2, radius=crown_r)
    verts = res["verts"]
    for v in verts:
        k = 1.0 + rng.uniform(-0.09, 0.09)
        v.co = Vector((v.co.x * k, v.co.y * k, v.co.z * k * 0.88)) + centre
    faces = list({f for v in verts for f in v.link_faces})
    colour_by_facing(bm, faces, PAL_LEAF_LIGHT, PAL_LEAF, PAL_LEAF_DARK)


def broadleaf_lumps(bm, rng, height, lumps, colours=(PAL_LEAF_LIGHT, PAL_LEAF, PAL_LEAF_DARK),
                    trunk_r=0.15, points=20, dark_trunk=False):
    """A crown of overlapping hull lumps, for a lumpier, broader tree. `lumps` is a list of
    (x, y, z-from-top, rx, rz) in tree units."""
    low = min(height - zt - rz * 0.8 for (_, _, zt, _, rz) in lumps)
    trunk_with_fork(bm, rng, low + 0.35, trunk_r, dark_trunk)
    for (x, y, zt, rx, rz) in lumps:
        pts = ellipsoid_points(rng, Vector((x, y, height - zt)), rx, rx * rng.uniform(0.9, 1.1), rz,
                               points, 0.10)
        faces = add_hull(bm, pts)
        colour_by_facing(bm, faces, *colours)


def rock(bm, rng, rx, ry, rz, points, centre=Vector((0, 0, 0)), jitter=0.18):
    """A boulder: the hull of points on a squashed, sunken ellipsoid, its base cut flat and dropped."""
    base_z = centre.z - GROUND_SINK
    pts = ellipsoid_points(rng, Vector((centre.x, centre.y, base_z + rz * 0.35)), rx, ry, rz,
                           points, jitter, z_floor=base_z)
    faces = add_hull(bm, pts, drop_below=base_z + 1e-4)
    colour_by_facing(bm, faces, PAL_STONE_LIGHT, PAL_STONE, PAL_STONE_DARK, up=0.6, down=-0.05)


def stump(bm, rng):
    """A cut stump with a slanted top and three root flares."""
    r = 0.30
    h = 0.42
    side, cap = add_prism(bm, Vector((0, 0, -GROUND_SINK)), r * 1.12, h + GROUND_SINK, 7,
                          rng.uniform(0, 6.3), rng, top_radius=r, jitter=0.06, top=True,
                          top_tilt=(0.18, 0.08))
    colour(bm, side, PAL_BARK)
    colour(bm, cap, PAL_PATH)
    for i in range(3):
        a = i * 2.0 * math.pi / 3 + rng.uniform(-0.4, 0.4)
        d = Vector((math.cos(a), math.sin(a), 0))
        s = Vector((-d.y, d.x, 0))
        inner = d * (r * 0.85)
        #Low and broad: a tall narrow flare reads as a thorn from above.
        a0 = bm.verts.new(inner + s * 0.15 - Vector((0, 0, GROUND_SINK)))
        a1 = bm.verts.new(inner - s * 0.15 - Vector((0, 0, GROUND_SINK)))
        up = bm.verts.new(inner + Vector((0, 0, h * 0.40)))
        out = bm.verts.new(d * (r + 0.20) - Vector((0, 0, GROUND_SINK)))
        faces = [bm.faces.new((a0, out, up)), bm.faces.new((out, a1, up))]
        colour(bm, faces, PAL_BARK_DARK)


def log(bm, rng):
    """A fallen log lying along X, half a radius into the ground, with one broken-off branch."""
    sides, r, length = 6, 0.22, 2.1
    rot = rng.uniform(0, 6.3)
    zc = r * 0.75
    #One radius per corner, shared by both ends, so every side face stays a flat quad - a twisted
    #one would be split into two triangles that disagree with the quad's single flat normal.
    radii = [r * rng.uniform(0.9, 1.06) for _ in range(sides)]
    a, b = [[bm.verts.new((x, radii[i] * math.cos(rot + 2 * math.pi * i / sides),
                           zc + radii[i] * math.sin(rot + 2 * math.pi * i / sides)))
             for i in range(sides)] for x in (-length * 0.5, length * 0.5)]
    side = [bm.faces.new((a[i], b[i], b[(i + 1) % sides], a[(i + 1) % sides])) for i in range(sides)]
    for f in side:
        f.normal_update()
    colour(bm, side, PAL_BARK)
    colour(bm, [f for f in side if f.normal.z < -0.3], PAL_BARK_DARK)
    caps = [bm.faces.new(list(reversed(a))), bm.faces.new(b)]
    colour(bm, caps, PAL_PATH)
    #The broken branch: a stubby square cone off the top, leaning back along the log.
    side, _ = add_cone(bm, Vector((0.25, 0.0, zc + r * 0.55)), 0.10, 0.24, 4, 0.4, rng,
                       tip=(-0.08, 0.06), bottom=False)
    colour(bm, side, PAL_BARK_DARK)


def bush(bm, rng, lumps, berries=0):
    """A few low hull lumps, optionally with berries poking out of the top."""
    all_faces = []
    for (x, y, rx, rz) in lumps:
        pts = ellipsoid_points(rng, Vector((x, y, rz * 0.55 - GROUND_SINK)), rx, rx * rng.uniform(0.85, 1.1),
                               rz, 14, 0.14, z_floor=-GROUND_SINK)
        faces = add_hull(bm, pts, drop_below=-GROUND_SINK + 1e-4)
        colour_by_facing(bm, faces, PAL_LEAF, PAL_BUSH, PAL_LEAF_DARK, up=0.6, down=-0.2)
        all_faces += faces
    #Berries: tiny three-sided pyramids stood on upward-facing faces, so they show from above.
    tops = [f for f in all_faces if f.is_valid and f.normal.z > 0.35]
    rng.shuffle(tops)
    for f in tops[:berries]:
        c = f.calc_center_median()
        n = f.normal
        side, _ = add_cone(bm, c - n * 0.02, 0.07, 0.11, 3, rng.uniform(0, 6.3), rng,
                           tip=(n.x * 0.06, n.y * 0.06), bottom=False)
        colour(bm, side, PAL_ACCENT)


# ---------------------------------------------------------------------------------------------
# Ground cover: scattered in their thousands between the trees, so every one is a handful of
# triangles. Spikes are three-sided and open-bottomed - visible from every side whatever the
# renderer culls, at three triangles each, where a flat blade would vanish edge-on or from behind.
# ---------------------------------------------------------------------------------------------

COVER_SINK = 0.03       #ground cover is too small to need GROUND_SINK; this just hides the seam


def spike(bm, rng, at, radius, height, lean_out):
    """A grass blade or a leaf: a thin three-sided spike, leaning `lean_out` away from the origin."""
    out = Vector((at.x, at.y, 0))
    out = out.normalized() * lean_out if out.length > 1e-6 else Vector((0, 0, 0))
    side, _ = add_cone(bm, Vector((at.x, at.y, -COVER_SINK)), radius, height + COVER_SINK, 3,
                       rng.uniform(0, 6.3), rng, tip=(out.x, out.y), bottom=False)
    return side


def tris_of_quad(bm, a, b, c, d):
    """A quad as two triangles: on a bent strip the quad is not planar, and one flat normal for a
    twisted quad is wrong for both of its halves."""
    return [bm.faces.new((a, b, c)), bm.faces.new((a, c, d))]


def frond(bm, spine, widths, fold):
    """A V-folded leaf along `spine`, the edges raised by `fold` times the width, so the leaf's top -
    the side the camera sees - is the inside of the V. A width of 0 makes that point a single vert:
    the tip, or a root that costs two triangles instead of four."""
    up = Vector((0, 0, 1))
    faces = []
    rails = []
    for i, p in enumerate(spine):
        d = (spine[min(i + 1, len(spine) - 1)] - spine[max(i - 1, 0)]).normalized()
        s = d.cross(up).normalized()
        w = widths[i]
        rails.append((bm.verts.new(p - s * w + up * fold * w), bm.verts.new(p),
                      bm.verts.new(p + s * w + up * fold * w)) if w > 0 else (None, bm.verts.new(p), None))
    for i in range(len(spine) - 1):
        (l0, p0, r0), (l1, p1, r1) = rails[i], rails[i + 1]
        if l0 is None:                  #a pointed root
            faces += [bm.faces.new((p0, p1, l1)), bm.faces.new((p0, r1, p1))]
        elif l1 is None:                #the tip
            faces += [bm.faces.new((l0, p0, p1)), bm.faces.new((p0, r0, p1))]
        else:
            faces += tris_of_quad(bm, l0, p0, p1, l1) + tris_of_quad(bm, p0, r0, r1, p1)
    return faces


def grass(bm, rng, blades, colours):
    """A tuft: `blades` spikes as (x, y, height), splayed outward."""
    for i, (x, y, h) in enumerate(blades):
        colour(bm, spike(bm, rng, Vector((x, y, 0)), 0.045, h, h * 0.35), colours[i % len(colours)])


def reeds(bm, rng, blades, heads):
    """A clump of reeds for the swamp's shallows and the rivers' banks: tall thin blades, leaning a
    little out of one root, and on some clumps cattails - a short brown head on a straight stem.
    The heads are what read as reed rather than long grass at the game's zoom; the blades alone,
    for a plain clump of rushes beside them. Blades are (x, y, height), heads (x, y, stem height)."""
    for i, (x, y, h) in enumerate(blades):
        colour(bm, spike(bm, rng, Vector((x, y, 0)), 0.035, h, h * 0.18), (PAL_LEAF, PAL_LEAF_DARK)[i % 2])
    for (x, y, h) in heads:
        colour(bm, spike(bm, rng, Vector((x, y, 0)), 0.025, h, 0.0), PAL_LEAF_DARK)
        side, _ = add_prism(bm, Vector((x, y, h - 0.24)), 0.055, 0.17, 3, rng.uniform(0, 6.3), rng,
                            top_radius=0.045)
        colour(bm, side, PAL_BARK_DARK)


def flowers(bm, rng, leaves, blooms):
    """Leaf spikes with squat blooms among them: the blooms are what reads, so they are wide, low and
    face the sky. No stalks - invisible at this size, and they doubled the count."""
    for (x, y, h) in leaves:
        colour(bm, spike(bm, rng, Vector((x, y, 0)), 0.05, h, h * 0.3), PAL_LEAF)
    for (x, y, z) in blooms:
        side, _ = add_cone(bm, Vector((x, y, z)), 0.085, 0.06, 3, rng.uniform(0, 6.3), rng, bottom=False)
        colour(bm, side, PAL_ACCENT)


def fern(bm, rng, count, length):
    """Fronds arching out of one root: up, then over and down."""
    for i in range(count):
        a = 2 * math.pi * i / count + rng.uniform(-0.3, 0.3)
        d = Vector((math.cos(a), math.sin(a), 0))
        L = length * rng.uniform(0.8, 1.1)
        spine = [Vector((0, 0, -COVER_SINK)), d * L * 0.45 + Vector((0, 0, L * 0.38)), d * L + Vector((0, 0, L * 0.12))]
        faces = frond(bm, spine, [0.0, L * 0.17, 0.0], 0.45)
        colour_by_facing(bm, faces, PAL_LEAF, PAL_LEAF_DARK, PAL_LEAF_DARK, up=0.75)


def mushrooms(bm, rng, caps):
    """A few caps sitting in the grass: (x, y, height, cap radius, colour). Four-sided cones, no
    stalks and no undersides - at the closest zoom a stalk is two pixels, and both together were
    two thirds of the triangles."""
    for (x, y, h, r, col) in caps:
        side, _ = add_cone(bm, Vector((x, y, -COVER_SINK)), r, h + COVER_SINK, 4, rng.uniform(0, 6.3), rng,
                           bottom=False)
        colour(bm, side, col)


def twig(bm, rng):
    """A fallen branch with one fork: two thin three-sided sticks lying in the grass, open-ended -
    the ends are a few pixels across at the closest zoom."""
    def stick(a, b, r):
        d = (b - a).normalized()
        s = d.cross(Vector((0, 0, 1))).normalized()
        u = s.cross(d)
        ra = [bm.verts.new(a + (s * math.cos(k * 2.094) + u * math.sin(k * 2.094)) * r) for k in range(3)]
        rb = [bm.verts.new(b + (s * math.cos(k * 2.094) + u * math.sin(k * 2.094)) * r * 0.7) for k in range(3)]
        faces = []
        for k in range(3):
            faces.append(bm.faces.new((ra[k], ra[(k + 1) % 3], rb[(k + 1) % 3], rb[k])))
        return faces
    r = 0.045
    main = stick(Vector((-0.45, 0, r * 0.5)), Vector((0.45, 0.05, r * 0.4)), r)
    fork = stick(Vector((0.05, 0.01, r * 0.5)), Vector((0.32, 0.30, r * 0.35)), r * 0.7)
    for f in main + fork:
        f.normal_update()
    colour_by_facing(bm, main + fork, PAL_BARK, PAL_BARK, PAL_BARK_DARK, up=0.3, down=-0.3)


# ---------------------------------------------------------------------------------------------
# Biome trees. Still UV'd into row 0: the game moves a biome's forest to its own row, and the row
# is what turns these into desert, snow or swamp colours.
# ---------------------------------------------------------------------------------------------

def banded_cone(bm, centre, radius, height, sides, rot, rng, split, jag):
    """A pine tier in two bands: a skirt below `split` (a fraction of the height) and a cap above it,
    the line between them zig-zagging by `jag` so the snow line drips. The cap is what shows
    between tiers, so it is the part that takes the snow."""
    base = ring(bm, centre, radius, sides, rot, rng, 0.08)
    mid = []
    for i, v in enumerate(base):
        t = split + (jag if i % 2 else -jag) * rng.uniform(0.6, 1.0)
        p = centre.lerp(v.co, 1.0 - t)
        mid.append(bm.verts.new((p.x, p.y, centre.z + height * t)))
    apex = bm.verts.new(centre + Vector((0, 0, height)))
    skirt = []
    for i in range(sides):
        j = (i + 1) % sides
        skirt += tris_of_quad(bm, base[i], base[j], mid[j], mid[i])
    cap = [bm.faces.new((mid[i], mid[(i + 1) % sides], apex)) for i in range(sides)]
    bottom = [bm.faces.new(list(reversed(base)))]
    return skirt, cap, bottom


def snow_conifer(bm, rng, height, tiers, width, sides, trunk_show, lean_xy):
    """The frozen biome's pine: the stacked cones of conifer(), each tier's upper band in PINE_LIGHT -
    which the frozen row makes snow - over a PINE_DARK skirt. In row 0 it is a two-tone pine."""
    top_scale, overlap = 1.10, 0.55
    tier_h = (height - trunk_show) / ((tiers - 1) * overlap + top_scale)
    trunk_r = 0.09 + 0.025 * width
    side, _ = add_prism(bm, Vector((0, 0, -GROUND_SINK)), trunk_r, trunk_show + GROUND_SINK + tier_h * 0.3,
                        5, rng.uniform(0, 6.3), rng, top_radius=trunk_r * 0.8)
    colour(bm, side, PAL_BARK_DARK)
    z = trunk_show
    for i in range(tiers):
        last = i == tiers - 1
        r = width * 0.5 * (1.0 - 0.38 * i / max(1, tiers - 1)) * rng.uniform(0.93, 1.07)
        h = tier_h * (top_scale if last else 1.0)
        #The next tier starts at `overlap`, so the visible band of this one is below that; the snow
        #line sits about halfway up it, a little higher on the top tier.
        skirt, cap, bottom = banded_cone(bm, Vector((0, 0, z)), r, h, sides, rng.uniform(0, 6.3), rng,
                                         0.40 if last else 0.28, 0.07)
        colour(bm, skirt, PAL_PINE_DARK)
        colour(bm, cap, PAL_PINE_LIGHT)
        colour(bm, bottom, PAL_PINE_DARK)
        z += tier_h * overlap
    lean(bm, height, lean_xy[0], lean_xy[1])


def palm(bm, rng, height, bend, fronds):
    """A desert palm: a curving trunk of stacked tapering segments, each sunk into the one below so
    the trunk shows its rings, and a crown of arching V-folded fronds."""
    segs = 4
    def centre(t):
        return Vector((bend * t * t, 0, t * height))
    for i in range(segs):
        t0, t1 = i / segs, (i + 1) / segs
        r = 0.16 - 0.035 * t0
        lo = centre(t0) - Vector((0, 0, GROUND_SINK if i == 0 else 0.06))
        side, _ = add_prism(bm, lo, r * 0.78, (centre(t1) - lo).z, 4, 0.3 + i * 0.4, rng, top_radius=r)
        dx = centre(t1).x - lo.x        #slide the segment's top ring along the curve, so the trunk bends
        for v in {v for f in side for v in f.verts}:
            if v.co.z > lo.z + 0.01:
                v.co.x += dx
        colour(bm, side, PAL_BARK)
    crown = centre(1.0)
    for i in range(fronds):
        a = 2 * math.pi * i / fronds + rng.uniform(-0.25, 0.25)
        d = Vector((math.cos(a), math.sin(a), 0))
        L = rng.uniform(1.5, 1.9)
        droop = rng.uniform(0.35, 0.6)
        spine = [crown + Vector((0, 0, 0.05)),
                 crown + d * L * 0.40 + Vector((0, 0, 0.28)),
                 crown + d * L * 0.75 + Vector((0, 0, 0.12)),
                 crown + d * L + Vector((0, 0, -droop))]
        faces = frond(bm, spine, [0.06, 0.30, 0.22, 0.0], 0.35)
        colour_by_facing(bm, faces, PAL_LEAF_LIGHT, PAL_LEAF, PAL_LEAF_DARK, up=0.8, down=-0.2)


def willow(bm, rng, height, crown_r, strands):
    """A swamp willow: a low dome on a leaning trunk, ringed by a hanging curtain whose bottom edge
    alternates long and short strands."""
    dome_base = height - crown_r * 0.55
    side, _ = add_prism(bm, Vector((0, 0, -GROUND_SINK)), 0.2, dome_base + GROUND_SINK, 5,
                        rng.uniform(0, 6.3), rng, top_radius=0.13)
    colour(bm, side, PAL_BARK_DARK)
    pts = ellipsoid_points(rng, Vector((0, 0, dome_base)), crown_r * 0.95, crown_r * 0.95, crown_r * 0.42,
                           16, 0.10, z_floor=dome_base - crown_r * 0.12)
    dome = add_hull(bm, pts)
    colour_by_facing(bm, dome, PAL_LEAF_LIGHT, PAL_LEAF, PAL_LEAF_DARK)

    rot = rng.uniform(0, 6.3)
    top_z = dome_base + crown_r * 0.05
    tops = [bm.verts.new((crown_r * 0.9 * math.cos(rot + 2 * math.pi * i / strands),
                          crown_r * 0.9 * math.sin(rot + 2 * math.pi * i / strands), top_z))
            for i in range(strands)]
    bottoms = []
    for k in range(strands * 2):
        a = rot + math.pi * k / strands
        long = k % 2 == 1
        #Long enough to hang well below the dome and splayed past its rim, or from above the willow
        #is just another round tree.
        drop = (crown_r * rng.uniform(1.45, 1.7)) if long else (crown_r * rng.uniform(0.8, 1.0))
        rr = crown_r * (1.18 if long else 1.08)
        #Kept clear of the ground, which also keeps the strands out of the origin's base sample.
        bottoms.append(bm.verts.new((rr * math.cos(a), rr * math.sin(a), max(0.4, top_z - drop))))
    for i in range(strands):
        t0, t1 = tops[i], tops[(i + 1) % strands]
        b0, b1, b2 = bottoms[2 * i], bottoms[2 * i + 1], bottoms[(2 * i + 2) % (2 * strands)]
        colour(bm, [bm.faces.new((t0, b0, b1))], PAL_LEAF)
        colour(bm, [bm.faces.new((t0, b1, t1))], PAL_LEAF_LIGHT)
        colour(bm, [bm.faces.new((t1, b1, b2))], PAL_LEAF_DARK)
    lean(bm, height, 0.35, -0.15, power=1.2)


# ---------------------------------------------------------------------------------------------
# Bare broadleaves (docs/seasons_plan.md, step 3): an oak in winter. The game swaps a tree's mesh
# for its bare one when its leaves have fallen, so the TRUNK must be the leafy oak's exactly - it is
# copied from that object as it stands in the .blend, edits and all - and only the crown changes:
# limbs reaching up and out into where the canopy was, a little short of its edge, and twigs.
# ---------------------------------------------------------------------------------------------

def frame(axis):
    """Two unit vectors square to `axis` and to each other."""
    axis = axis.normalized()
    ref = Vector((0, 0, 1)) if abs(axis.z) < 0.9 else Vector((1, 0, 0))
    u = axis.cross(ref).normalized()
    return u, axis.cross(u).normalized()


def limb(bm, points, radii, sides, rot):
    """A branch along `points`: a tube of `sides` tapering through `radii` (one per point but the
    last), ending in a point at the last one. Open at its base, which is inside what it grows from."""
    rings = []
    for i in range(len(points) - 1):
        axis = points[i + 1] - points[max(0, i - 1)]
        u, v = frame(axis)
        rings.append([bm.verts.new(points[i] + (u * math.cos(rot + 2 * math.pi * k / sides) +
                                               v * math.sin(rot + 2 * math.pi * k / sides)) * radii[i])
                      for k in range(sides)])
    tip = bm.verts.new(points[-1])
    faces = []
    def outward(f, on_axis):
        f.normal_update()
        if f.normal.dot(f.calc_center_median() - on_axis) < 0:
            f.normal_flip()
        faces.append(f)
    #Triangles, not quads: two rings square to different axes do not make planar quads (tris_of_quad).
    for i in range(len(rings) - 1):
        a, b = rings[i], rings[i + 1]
        mid = (points[i] + points[i + 1]) * 0.5
        for k in range(sides):
            for f in tris_of_quad(bm, a[k], a[(k + 1) % sides], b[(k + 1) % sides], b[k]):
                outward(f, mid)
    last = rings[-1]
    mid = (points[-2] + points[-1]) * 0.5
    for k in range(sides):
        outward(bm.faces.new((last[k], last[(k + 1) % sides], tip)), mid)
    return faces


def bare_broadleaf(leafy, limbs=4):
    """A builder for the bare form of the oak named `leafy`: its trunk as it is in the .blend, and
    `limbs` limbs, each with a side branch and a twig, in place of the crown. Needs `leafy` built or loaded first."""
    def build(bm, rng):
        if leafy not in bpy.data.objects:
            raise RuntimeError("%s needs %s in the file to copy its trunk from" % (leafy + "_bare", leafy))
        mesh = bpy.data.objects[leafy].data
        uvs = mesh.uv_layers.active.data
        pal = bm.faces.layers.int["pal"]
        #The leafy oak's faces by colour: bark is the trunk, copied; leaves give the crown's extent.
        made = {}
        bark_col = PAL_BARK
        crown = []
        for poly in mesh.polygons:
            col = int(uvs[poly.loop_start].uv[0] * PALETTE_COLS)
            if col in (PAL_BARK, PAL_BARK_DARK):
                bark_col = col
                vs = []
                for i in poly.vertices:
                    if i not in made:
                        made[i] = bm.verts.new(mesh.vertices[i].co)
                    vs.append(made[i])
                f = bm.faces.new(vs)
                f[pal] = col
            elif col in (PAL_LEAF_DARK, PAL_LEAF, PAL_LEAF_LIGHT):
                crown.extend(mesh.vertices[i].co.copy() for i in poly.vertices)
        trunk = [v.co for v in made.values()]
        top_z = max(c.z for c in trunk)
        top = [c for c in trunk if c.z > top_z - 0.02]
        fork = sum(top, Vector()) / len(top)
        fork_r = max((Vector((c.x, c.y, 0)) - Vector((fork.x, fork.y, 0))).length for c in top)
        lo = Vector((min(c.x for c in crown), min(c.y for c in crown), min(c.z for c in crown)))
        hi = Vector((max(c.x for c in crown), max(c.y for c in crown), max(c.z for c in crown)))
        centre = (lo + hi) * 0.5
        half = (hi - lo) * 0.5

        #An oak's winter shape is wide and spreading, not a broom: few heavy limbs leaving the trunk at
        #different heights, reaching out low to about the canopy's edge and turning up at their ends.
        faces = []
        turn = rng.uniform(0, 2 * math.pi)
        for i in range(limbs):
            a = turn + 2 * math.pi * i / limbs + rng.uniform(-0.3, 0.3)
            up = math.radians(rng.uniform(15, 50))
            reach = rng.uniform(0.85, 1.0)
            tip = centre + Vector((math.cos(up) * math.cos(a) * half.x, math.cos(up) * math.sin(a) * half.y,
                                   math.sin(up) * half.z)) * reach
            base = fork - Vector((0, 0, rng.uniform(0.10, 0.55)))
            length = (tip - base).length
            bend = base + (tip - base) * 0.40 + Vector((0, 0, 0.10 * length))
            r0 = fork_r * rng.uniform(0.85, 1.0)
            r1 = r0 * 0.55
            sides = rng.uniform(0, 6.3)
            faces += limb(bm, [base, bend, tip], [r0, r1], 4, sides)
            #A side branch from the bend, forking off one way and up.
            outer = tip - bend
            side_a = a + rng.choice((-1, 1)) * rng.uniform(0.5, 0.9)
            side_dir = Vector((math.cos(side_a), math.sin(side_a), 0)) * outer.xy.length + Vector((0, 0, outer.z + 0.3))
            side_len = outer.length * rng.uniform(0.55, 0.8)
            side_tip = bend + side_dir.normalized() * side_len
            side_mid = bend + (side_tip - bend) * 0.5 + Vector((0, 0, 0.08 * side_len))
            faces += limb(bm, [bend, side_mid, side_tip], [r1 * 0.8, r1 * 0.45], 3, rng.uniform(0, 6.3))
            #And a twig off the limb's outer half, up and out.
            at = bend + outer * rng.uniform(0.35, 0.65)
            out = (Vector((at.x - fork.x, at.y - fork.y, 0)).normalized() * rng.uniform(0.4, 0.8) +
                   Vector((rng.uniform(-0.4, 0.4), rng.uniform(-0.4, 0.4), 1.0)))
            faces += limb(bm, [at, at + out.normalized() * rng.uniform(0.35, 0.55)], [r1 * 0.5], 3,
                          rng.uniform(0, 6.3))
        colour_by_facing(bm, faces, bark_col, bark_col, PAL_BARK_DARK, up=0.8, down=-0.5)
    return build


def gem(bm, centre, r, rot):
    """A fruit: eight faces round `centre`, a little taller above than below, every face outward."""
    ring = [bm.verts.new(centre + Vector((r * math.cos(rot + k * math.pi / 2), r * math.sin(rot + k * math.pi / 2), 0)))
            for k in range(4)]
    top = bm.verts.new(centre + Vector((0, 0, r * 1.1)))
    bottom = bm.verts.new(centre - Vector((0, 0, r * 0.9)))
    faces = []
    for k in range(4):
        a, b = ring[k], ring[(k + 1) % 4]
        faces.append(bm.faces.new((a, b, top)))
        faces.append(bm.faces.new((b, a, bottom)))
    return faces


def plant_fruit(leafy, count, size, apples=False):
    """A builder for the FRUIT of the plant named `leafy` (foraging_plan.md): a mesh of its own that the game draws over
    a ripe plant and leaves off a picked one, so it is built in the plant's own space - keep_origin - and placed on it
    in the file. Fruit stands on the canopy's faces that look up (berries) or out (apples), spread over it. A plant
    modelled with its fruit on - bush_b's berries - has them MOVED here, out of the plant, so a picked bush is bare.
    Needs `leafy` built or loaded first."""
    def build(bm, rng):
        if leafy not in bpy.data.objects:
            raise RuntimeError("%s needs %s in the file to put its fruit on" % (leafy + "_fruit", leafy))
        mesh = bpy.data.objects[leafy].data
        uvs = mesh.uv_layers.active.data
        pal = bm.faces.layers.int["pal"]

        def column(poly):
            return int(uvs[poly.loop_start].uv[0] * PALETTE_COLS)

        own = [poly for poly in mesh.polygons if column(poly) == PAL_ACCENT]
        if own:
            made = {}
            for poly in own:
                vs = []
                for i in poly.vertices:
                    if i not in made:
                        made[i] = bm.verts.new(mesh.vertices[i].co)
                    vs.append(made[i])
                bm.faces.new(vs)[pal] = PAL_ACCENT
            #...and out of the plant, whatever else was edited on it left as it is.
            plant = bmesh.new()
            plant.from_mesh(mesh)
            uv = plant.loops.layers.uv.active
            gone = [f for f in plant.faces if int(f.loops[0][uv].uv[0] * PALETTE_COLS) == PAL_ACCENT]
            bmesh.ops.delete(plant, geom=gone, context="FACES")
            plant.to_mesh(mesh)
            plant.free()
            mesh.update()
            return
        leaves = (PAL_LEAF_DARK, PAL_LEAF, PAL_LEAF_LIGHT, PAL_BUSH)
        facing = -0.15 if apples else 0.35      #apples hang on the sides too; berries show from above
        spots = [(poly.center.copy(), poly.normal.copy()) for poly in mesh.polygons
                 if column(poly) in leaves and poly.normal.z > facing]
        rng.shuffle(spots)
        taken = []
        for c, n in spots:
            if len(taken) < count and all((c - t).length > size * 3.5 for t, _ in taken):
                taken.append((c, n))
        for c, n in taken:
            if apples:
                faces = gem(bm, c + n * size * 0.45, size, rng.uniform(0, 6.3))
            else:
                faces, _ = add_cone(bm, c - n * 0.02, size, size * 1.6, 3, rng.uniform(0, 6.3), rng,
                                    tip=(n.x * 0.06, n.y * 0.06), bottom=False)
            colour(bm, faces, PAL_ACCENT)
    build.keep_origin = True
    build.place_at = leafy
    return build


#name, seed, builder, slot (column, row) in the authoring layout
# ---------------------------------------------------------------------------------------------
# The archery range (range_plan.md). Each FACES -Y - south once exported (glTF's +Z), toward the
# shooters and the game's camera - as ApplicationChasmRange.cpp places them.
# ---------------------------------------------------------------------------------------------

def disc_ring(bm, y, z, radius, sides):
    """A ring of verts in the XZ plane at depth y, round height z: a disc that faces along Y."""
    return [bm.verts.new((radius * math.cos(2 * math.pi * (i + 0.5) / sides), y,
                          z + radius * math.sin(2 * math.pi * (i + 0.5) / sides))) for i in range(sides)]


def range_target(bm, rng):
    """A straw boss on two legs, its face ringed white, blue, red and gold, the gold in the middle."""
    #Wound by hand (a ring in order faces -Y), not by recalc_face_normals, which guesses on open shapes.
    sides, r, zc, depth = 8, 0.34, 0.74, 0.06
    back = disc_ring(bm, depth, zc, r, sides)
    rims = [disc_ring(bm, -depth, zc, rr, sides) for rr in (r, 0.25, 0.16, 0.07)]
    side = [bm.faces.new((back[i], back[(i + 1) % sides], rims[0][(i + 1) % sides], rims[0][i]))
            for i in range(sides)]
    colour(bm, side, PAL_FIELD)
    colour(bm, [bm.faces.new(list(reversed(back)))], PAL_FIELD)
    for band, col in enumerate((PAL_WALL, PAL_WATER, PAL_ACCENT)):
        outer, inner = rims[band], rims[band + 1]
        colour(bm, [bm.faces.new((outer[i], outer[(i + 1) % sides], inner[(i + 1) % sides], inner[i]))
                    for i in range(sides)], col)
    colour(bm, [bm.faces.new(rims[3])], PAL_FIELD)
    #The legs: square posts behind the boss, leaning back a little under its weight.
    for x in (-0.22, 0.22):
        legs, cap = add_prism(bm, Vector((x, 0.10, 0.0)), 0.04, zc + 0.05, 4, math.pi / 4, rng, top=True,
                              top_tilt=(0.0, 0.0))
        for v in {v for f in legs for v in f.verts}:
            v.co.y -= v.co.z * 0.10
        colour(bm, legs + cap, PAL_TIMBER)


def hay_bale(bm, rng):
    """A block of straw lying on its side, bound by two bands of twine."""
    hx, hy, h = 0.28, 0.18, 0.34
    box = [bm.verts.new((sx * hx, sy * hy, z)) for z in (0.0, h) for sy in (-1, 1) for sx in (-1, 1)]
    quads = [(4, 5, 7, 6), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5)]
    colour(bm, [bm.faces.new([box[i] for i in q]) for q in quads], PAL_FIELD)
    #Each band a strip round the top and both long sides, standing just proud of the straw.
    for x in (-0.12, 0.12):
        a, b, o = x - 0.018, x + 0.018, 0.006
        lo = [bm.verts.new((u, s * (hy + o), 0.0)) for s in (-1, 1) for u in (a, b)]
        hi = [bm.verts.new((u, s * (hy + o), h + o)) for s in (-1, 1) for u in (a, b)]
        strip = [bm.faces.new((lo[0], lo[1], hi[1], hi[0])), bm.faces.new((hi[0], hi[1], hi[3], hi[2])),
                 bm.faces.new((hi[2], hi[3], lo[3], lo[2]))]
        colour(bm, strip, PAL_TIMBER)


def arrow(bm, rng):
    """A shaft along Y, lying level, its point to +Y (into the target) and its fletching to -Y."""
    sides, r, half = 3, 0.014, 0.16
    a = [bm.verts.new((r * math.cos(2 * math.pi * i / sides), -half, r * math.sin(2 * math.pi * i / sides)))
         for i in range(sides)]
    b = [bm.verts.new((v.co.x, half, v.co.z)) for v in a]
    shaft = [bm.faces.new((a[i], b[i], b[(i + 1) % sides], a[(i + 1) % sides])) for i in range(sides)]
    shaft.append(bm.faces.new(a))       #the nock end, toward the archer: the end that is seen
    colour(bm, shaft, PAL_BARK_DARK)
    #Two crossed fins at the back, each two quads back to back a hair apart, so it is seen from either side
    #and remove_doubles keeps both.
    for horizontal in (True, False):
        w, gap = 0.035, 0.002
        p = [(-w, -half), (w, -half), (w, -half + 0.07), (-w, -half + 0.07)]
        for side, order in ((gap, p), (-gap, list(reversed(p)))):
            pts = [bm.verts.new((u, y, side) if horizontal else (side, y, u)) for u, y in order]
            colour(bm, [bm.faces.new(pts)], PAL_WALL)


# ---------------------------------------------------------------------------------------------
# Stone (stone_plan.md, 2026-10-14): rubble, a boulder, cut blocks, floatstone, the mine's portal and
# cart, the quarry's crane and the outcrop it stands on. Broken stone is angular where the rocks above
# are rounded lumps, so a pile of it is not taken for a rock; floatstone is a purple-blue crystal on a
# row of its own (cell()), so it is never taken for stone.
# ---------------------------------------------------------------------------------------------

def outward(face, direction):
    """Turns `face` round if it does not look along `direction` - for open shapes, where
    recalc_face_normals would have to guess."""
    face.normal_update()
    if face.normal.dot(direction) < 0.0:
        face.normal_flip()
    return face


def block(bm, at, size, yaw=0.0, tilt=(0.0, 0.0), bottom=False):
    """A box of `size` (x, y, z) standing on `at`, tipped by `tilt` (about x, then y) and turned `yaw`.
    Returns its faces by side: "top", "ends" (square to x), "sides" (square to y), and "bottom" if asked."""
    rot = Matrix.Rotation(yaw, 3, "Z") @ Matrix.Rotation(tilt[1], 3, "Y") @ Matrix.Rotation(tilt[0], 3, "X")
    hx, hy, h = size[0] / 2, size[1] / 2, size[2]
    v = {(sx, sy, sz): bm.verts.new(at + rot @ Vector((sx * hx, sy * hy, sz * h)))
         for sx in (-1, 1) for sy in (-1, 1) for sz in (0, 1)}

    def quad(keys, normal):
        return outward(bm.faces.new([v[k] for k in keys]), rot @ Vector(normal))
    faces = {"top": [quad([(-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1)], (0, 0, 1))],
             "ends": [quad([(s, -1, 0), (s, 1, 0), (s, 1, 1), (s, -1, 1)], (s, 0, 0)) for s in (-1, 1)],
             "sides": [quad([(-1, s, 0), (1, s, 0), (1, s, 1), (-1, s, 1)], (0, s, 0)) for s in (-1, 1)]}
    if bottom:
        faces["bottom"] = [quad([(-1, -1, 0), (1, -1, 0), (1, 1, 0), (-1, 1, 0)], (0, 0, -1))]
    return faces


def rod(bm, a, b, radius, sides, rot=math.pi / 4, caps=(True, True)):
    """A straight tube of `sides` from `a` to `b`: a timber (four sides, `radius` half its diagonal), a line
    (three) or a drum. Returns (sides, ends)."""
    axis = (b - a).normalized()
    u, w = frame(axis)
    offs = [(u * math.cos(rot + 2 * math.pi * k / sides) + w * math.sin(rot + 2 * math.pi * k / sides)) * radius
            for k in range(sides)]
    ra = [bm.verts.new(a + o) for o in offs]
    rb = [bm.verts.new(b + o) for o in offs]
    side = [outward(bm.faces.new((ra[k], ra[(k + 1) % sides], rb[(k + 1) % sides], rb[k])),
                    offs[k] + offs[(k + 1) % sides]) for k in range(sides)]
    ends = []
    if caps[0]:
        ends.append(outward(bm.faces.new(ra), -axis))
    if caps[1]:
        ends.append(outward(bm.faces.new(rb), axis))
    return side, ends


def chunk(bm, rng, at, half, cut=1, keep=8, tilt=0.30, sink=0.02, shades=None):
    """A broken stone: a jittered box with `cut` of its corners knocked off - flat broken faces and sharp
    edges, where a rock is a rounded lump - or, with `keep` 6 or 5, a wedge or a chip: its four bottom
    corners and two tops along one edge, or one top. Rests on height `at.z`, `sink` into whatever is under
    it."""
    hx, hy, hz = half
    corners = [(sx, sy, sz) for sx in (-1, 1) for sy in (-1, 1) for sz in (-1, 1)]
    if keep < 8:
        tops = [c for c in corners if c[2] > 0]
        if keep == 6:
            axis, sign = rng.randrange(2), rng.choice((-1, 1))
            tops = [c for c in tops if c[axis] == sign]
        else:
            tops = [rng.choice(tops)]
        corners = [c for c in corners if c[2] < 0] + tops
        cut = 0
    knocked = set(rng.sample(range(len(corners)), cut))
    pts = []
    for i, (sx, sy, sz) in enumerate(corners):
        p = Vector((sx * hx * rng.uniform(0.75, 1.15), sy * hy * rng.uniform(0.75, 1.15),
                    sz * hz * rng.uniform(0.80, 1.10)))
        if i in knocked:
            for k in range(3):          #the corner gone: a point part way along each of its edges instead
                q = p.copy()
                q[k] *= rng.uniform(0.30, 0.55)
                pts.append(q)
        else:
            pts.append(p)
    rot = Euler((rng.uniform(-tilt, tilt), rng.uniform(-tilt, tilt), rng.uniform(0, 2 * math.pi))).to_matrix()
    pts = [rot @ p for p in pts]
    low = min(p.z for p in pts)
    pts = [p + Vector((at.x, at.y, at.z - sink - low)) for p in pts]
    faces = add_hull(bm, pts)
    #What it lies on: the faces looking down from its lowest few centimetres, never seen.
    for f in faces:
        f.normal_update()
    under = [f for f in faces if f.normal.z < -0.5 and all(v.co.z < at.z - sink + 0.04 for v in f.verts)]
    bmesh.ops.delete(bm, geom=under, context="FACES_ONLY")
    faces = [f for f in faces if f.is_valid]
    colour_by_facing(bm, faces, *(shades or (PAL_STONE_LIGHT, PAL_STONE, PAL_STONE_DARK)), up=0.55, down=-0.25)
    return faces


def rubble(bm, rng, rx, ry, big, mid, chips, top=True):
    """A heap of broken stone over an `rx` by `ry` ellipse: `big` stones round the middle, one more lying on
    them if `top`, `mid` wedges round those and `chips` at the edge. One in three is a darker stone, so the
    heap reads as many stones and not as one."""
    def shades():
        return (PAL_STONE, PAL_STONE_DARK, PAL_STONE_DARK) if rng.random() < 0.33 else None
    turn = rng.uniform(0, 2 * math.pi)
    golden = math.pi * (3.0 - math.sqrt(5.0))

    def at(a, r, z=0.0):
        return Vector((rx * r * math.cos(a), ry * r * math.sin(a), z))
    for i in range(big):
        s = rng.uniform(0.85, 1.10)
        chunk(bm, rng, at(turn + 2 * math.pi * i / big + rng.uniform(-0.3, 0.3), rng.uniform(0.15, 0.35)),
              (0.13 * s, 0.095 * s, 0.06 * s), cut=1, shades=shades())
    if top:
        chunk(bm, rng, at(turn, 0.05, 0.075), (0.095, 0.075, 0.05), cut=1, tilt=0.40, shades=shades())
    for i in range(mid):
        s = rng.uniform(0.80, 1.20)
        chunk(bm, rng, at(turn + golden * (i + 1) * 2.0, rng.uniform(0.55, 0.80)), (0.07 * s, 0.055 * s, 0.045 * s),
              keep=6, shades=shades())
    for i in range(chips):
        chunk(bm, rng, at(rng.uniform(0, 2 * math.pi), rng.uniform(0.80, 1.0)), (0.035, 0.03, 0.025), keep=5,
              shades=shades())


def copy_into(bm, part):
    """Every face of the bmesh `part` into `bm`, with its colour."""
    src, dst = part.faces.layers.int["pal"], bm.faces.layers.int["pal"]
    verts = {v: bm.verts.new(v.co) for v in part.verts}
    for f in part.faces:
        bm.faces.new([verts[v] for v in f.verts])[dst] = f[src]


def boulder(bm, rng):
    """Bigger than any rock (stone_plan.md: it blocks its plot until the stonecutter breaks it), split by a
    crack that says it can be broken, a few chips of it at its foot. Fewer and bigger facets than the
    rocks' round lumps, so it is not taken for a big one of those: rings of four points at its foot, its
    widest and its shoulders, each turned 45 degrees from the one below, inside an `rx` by `ry` ellipse.
    Its two parts lean apart a little, which opens the crack at the top, where the game's camera sees it."""
    rx, ry, h = 0.86, 0.78, 1.15
    base_z = -GROUND_SINK
    centre = Vector((0, 0, base_z + h * 0.4))
    pts = [Vector((rng.uniform(-0.1, 0.1), rng.uniform(-0.1, 0.1), base_z + h))]
    for height, radius, turn in ((0.0, 0.92, 0.0), (0.42, 1.0, 45.0), (0.86, 0.60, 0.0)):
        for k in range(4):
            t = math.radians(turn + 90 * k + rng.uniform(-12, 12))
            r = radius * rng.uniform(0.90, 1.0)
            pts.append(Vector((rx * r * math.cos(t), ry * r * math.sin(t),
                               base_z + h * height * rng.uniform(0.9, 1.1))))
    a = rng.uniform(0, math.pi)
    n = Vector((math.cos(a), math.sin(a), 0.15)).normalized()
    plane = centre + n * 0.20                   #off the middle: a big part and a smaller split-off one
    hinge_axis = n.cross(Vector((0, 0, 1))).normalized()
    for side in (1, -1):
        part = bmesh.new()
        part.faces.layers.int.new("pal")
        add_hull(part, pts)
        bmesh.ops.bisect_plane(part, geom=part.verts[:] + part.edges[:] + part.faces[:], dist=1e-5,
                               plane_co=plane, plane_no=n, clear_inner=side > 0, clear_outer=side < 0)
        crack = bmesh.ops.holes_fill(part, edges=[e for e in part.edges if e.is_boundary], sides=0)["faces"]
        bmesh.ops.recalc_face_normals(part, faces=part.faces[:])
        colour_by_facing(part, part.faces, PAL_STONE_LIGHT, PAL_STONE, PAL_STONE_DARK, up=0.6, down=-0.05)
        colour(part, crack, PAL_STONE_DARK)
        pivot = Vector((plane.x, plane.y, base_z))
        lean = Matrix.Rotation(-side * math.radians(3.5), 3, hinge_axis)
        for v in part.verts:
            v.co = pivot + lean @ (v.co - pivot) + n * side * 0.02
        bmesh.ops.delete(part, geom=[f for f in part.faces if all(v.co.z <= base_z + 1e-4 for v in f.verts)],
                         context="FACES_ONLY")
        copy_into(bm, part)
        part.free()
    along = n.cross(Vector((0, 0, 1))).normalized()
    for k, (t, s) in enumerate(((0.95, 1.0), (-0.92, 0.8), (0.55, 0.7))):
        foot = plane + along * t * rx + n * (0.12 if k == 2 else 0.0)
        chunk(bm, rng, Vector((foot.x, foot.y, 0.0)), (0.10 * s, 0.08 * s, 0.06 * s), cut=0)


boulder.footprint_origin = True


def stone_blocks(bm, rng):
    """Cut stone stacked to be carried off, as logs pile on a woodcutter's lot: three blocks side by side,
    two across them, one on top. The cut faces - tops and ends - are paler than the dressed sides."""
    size = (0.36, 0.19, 0.14)

    def put(x, y, z, yaw):
        f = block(bm, Vector((x + rng.uniform(-0.012, 0.012), y + rng.uniform(-0.012, 0.012), z)), size,
                  yaw + rng.uniform(-0.05, 0.05))
        colour(bm, f["top"], PAL_STONE_LIGHT)
        colour(bm, f["ends"], PAL_WALL)
        colour(bm, f["sides"], PAL_STONE)
    for x in (-0.20, 0.0, 0.20):
        put(x, 0.0, -0.04, math.pi / 2)
    for y in (-0.10, 0.10):
        put(0.0, y, 0.10, 0.0)
    put(0.02, 0.0, 0.24, math.pi / 2)


#Toward the sky and the game's camera: which of a crystal's facets are lit. Baked in rather than left to
#the sun, because sharp steps between neighbouring facets are what make a crystal read as one.
CRYSTAL_KEY = Vector((-0.40, -0.55, 0.73)).normalized()


def crystal_colour(bm, faces):
    layer = bm.faces.layers.int["pal"]
    for f in faces:
        f.normal_update()
        d = f.normal.dot(CRYSTAL_KEY)
        f[layer] = (PAL_FLOATSTONE_GLINT if d > 0.80 else PAL_FLOATSTONE_LIGHT if d > 0.35
                    else PAL_FLOATSTONE if d > -0.15 else PAL_FLOATSTONE_DARK)


def shard(bm, base, direction, length, radius, sides, rot, tip):
    """A crystal: a prism of `sides` from `base` along `direction`, narrowing a little, then a point `tip`
    long."""
    axis = direction.normalized()
    u, w = frame(axis)
    offs = [(u * math.cos(rot + 2 * math.pi * k / sides) + w * math.sin(rot + 2 * math.pi * k / sides)) * radius
            for k in range(sides)]
    ra = [bm.verts.new(base + o) for o in offs]
    rb = [bm.verts.new(base + axis * length + o * 0.88) for o in offs]
    apex = bm.verts.new(base + axis * (length + tip))
    faces = []
    for k in range(sides):
        out = offs[k] + offs[(k + 1) % sides]
        faces.append(outward(bm.faces.new((ra[k], ra[(k + 1) % sides], rb[(k + 1) % sides], rb[k])), out))
        faces.append(outward(bm.faces.new((rb[k], rb[(k + 1) % sides], apex)), out + axis * radius))
    crystal_colour(bm, faces)


def floatstone_cluster(bm, rng, scale=1.3):
    """Floatstone as it grows in a seam: sharp shards fanning out of one root sunk in the ground, the
    tallest near upright. (tilt, azimuth, sides, radius, length) in degrees and units, times `scale`."""
    for i, (tilt, az, sides, radius, length) in enumerate([(6, 0, 6, 0.066, 0.27), (32, 40, 5, 0.050, 0.21),
                                                            (36, 155, 5, 0.046, 0.19), (42, 255, 4, 0.044, 0.16),
                                                            (58, 320, 4, 0.034, 0.12), (55, 205, 4, 0.030, 0.10)]):
        t, a = math.radians(tilt + rng.uniform(-4, 4)), math.radians(az + rng.uniform(-10, 10))
        d = Vector((math.sin(t) * math.cos(a), math.sin(t) * math.sin(a), math.cos(t)))
        base = Vector((0.03 * math.cos(a), 0.03 * math.sin(a), -0.05)) if i else Vector((0, 0, -0.05))
        shard(bm, base * scale, d, length * scale, radius * scale, sides, rng.uniform(0, 2 * math.pi),
              radius * scale * rng.uniform(1.6, 2.1))


def floatstone_chunks(bm, rng, count):
    """Dug floatstone: broken crystals lying in a small heap - a pile of it, or a load. Each is a stubby
    double point, the hull of a ring of five round its long axis and an end beyond each side."""
    turn = rng.uniform(0, 2 * math.pi)
    for i in range(count):
        a = turn + 2 * math.pi * i / count + rng.uniform(-0.4, 0.4)
        r = 0.0 if i == 0 else rng.uniform(0.13, 0.18)
        length, radius = rng.uniform(0.10, 0.13) * (1.25 if i == 0 else 1.0), rng.uniform(0.065, 0.08)
        axis = Euler((0.0, rng.uniform(-0.5, 0.5), rng.uniform(0, 2 * math.pi))).to_matrix() @ Vector((1, 0, 0))
        u, w = frame(axis)
        spin = rng.uniform(0, 2 * math.pi)
        pts = [axis * length * rng.uniform(0.8, 1.1), -axis * length * rng.uniform(0.5, 0.8)]
        pts += [(u * math.cos(spin + 2 * math.pi * k / 5) + w * math.sin(spin + 2 * math.pi * k / 5)) * radius *
                rng.uniform(0.8, 1.1) + axis * rng.uniform(-0.02, 0.02) for k in range(5)]
        low = min(p.z for p in pts)
        centre = Vector((r * math.cos(a), r * math.sin(a), (0.035 if i == count - 1 else 0.0) - low - 0.015))
        crystal_colour(bm, add_hull(bm, [p + centre for p in pts], drop_below=0.0))


def mine_portal(bm, rng):
    """A timber adit dug into a cliff. The ORIGIN is the middle of the mouth's sill, and the mouth faces -Y
    (glTF +Z, as range_target) with the rails running out that way; everything from the frame back - a
    shallow dark mouth, then rock in the walls' strata colours to y 0.62 - is there to sink into an uneven
    wall, so the game sets it with the wall's face between y 0.16 and 0.6."""
    #The frame: two posts and a lintel, square timbers, and a lean-to of three planks over it.
    for x in (-0.40, 0.40):
        f = block(bm, Vector((x, 0.0, -0.06)), (0.08, 0.08, 1.00), rng.uniform(-0.03, 0.03))
        colour(bm, f["ends"] + f["sides"], PAL_TIMBER)
        colour(bm, f["top"], PAL_PATH)
    f = block(bm, Vector((0.0, 0.0, 0.92)), (1.02, 0.10, 0.10))
    colour(bm, f["top"] + f["sides"], PAL_TIMBER)
    colour(bm, f["ends"], PAL_PATH)
    slope = math.atan(0.35)
    for x in (-0.34, 0.0, 0.34):
        f = block(bm, Vector((x + rng.uniform(-0.01, 0.01), 0.02, 1.05 + rng.uniform(-0.01, 0.01))),
                  (0.32, 0.70 + rng.uniform(-0.03, 0.03), 0.035), rng.uniform(-0.03, 0.03), (slope, 0.0))
        colour(bm, f["top"], PAL_TIMBER)
        colour(bm, f["ends"] + f["sides"], PAL_BARK_DARK)
    #The mouth: lined with dark boards to a black back, just behind the frame - deeper and a wall set close
    #in front of it would show inside.
    b, w, h = 0.16, 0.36, 0.92
    mouth = [((-w, 0.04, 0), (-w, b, 0), (-w, b, h), (-w, 0.04, h), (1, 0, 0), PAL_BARK_DARK),
             ((w, 0.04, 0), (w, 0.04, h), (w, b, h), (w, b, 0), (-1, 0, 0), PAL_BARK_DARK),
             ((-w, 0.04, h), (-w, b, h), (w, b, h), (w, 0.04, h), (0, 0, -1), PAL_BARK_DARK),
             ((-w, 0.04, 0.005), (w, 0.04, 0.005), (w, b, 0.005), (-w, b, 0.005), (0, 0, 1), PAL_STONE_DARK),
             ((-w, b, 0), (w, b, 0), (w, b, h), (-w, b, h), (0, -1, 0), PAL_VOID)]
    for *corners, normal, col in mouth:
        colour(bm, [outward(bm.faces.new([bm.verts.new(c) for c in corners]), Vector(normal))], col)
    #The rock round it, in three lumps: either side and over the top.
    for lo, hi in (((-0.74, 0.03, -0.08), (-0.38, 0.62, 1.12)), ((0.38, 0.03, -0.08), (0.72, 0.62, 1.05)),
                   ((-0.62, 0.05, 0.98), (0.64, 0.62, 1.34))):
        pts = [Vector((x * rng.uniform(0.92, 1.05), y + rng.uniform(-0.03, 0.03), z * rng.uniform(0.95, 1.03)))
               for x in (lo[0], hi[0]) for y in (lo[1], hi[1]) for z in (lo[2], hi[2])]
        pts += [Vector((rng.uniform(lo[0], hi[0]), lo[1] - rng.uniform(0.0, 0.04), rng.uniform(lo[2], hi[2])))
                for _ in range(3)]
        colour_by_facing(bm, add_hull(bm, pts, drop_below=0.0), PAL_ROCK_0, PAL_ROCK_3, PAL_ROCK_1, up=0.5)
    #The track: two iron rails on four sleepers, from inside the mouth to 0.62 out of it.
    for y in (0.07, -0.15, -0.37, -0.57):
        f = block(bm, Vector((rng.uniform(-0.02, 0.02), y, -0.02)), (0.34, 0.055, 0.032), rng.uniform(-0.07, 0.07))
        colour(bm, f["top"] + f["sides"] + f["ends"], PAL_BARK)
    for x in (-0.11, 0.11):
        f = block(bm, Vector((x, -0.235, 0.012)), (0.022, 0.75, 0.024))
        colour(bm, f["top"] + f["sides"] + f["ends"], PAL_STONE_DARK)


mine_portal.keep_origin = True


def mine_cart(bm, rng):
    """A small ore cart for the portal's rails, its length along y as they run: a timber tub with an iron rim,
    heaped with stone, on four wheels at the rails' gauge."""
    lo, hi = 0.075, 0.23
    bx, by, tx, ty = 0.085, 0.14, 0.115, 0.18          #half sizes of the tub's bottom and its top
    bot = {(sx, sy): bm.verts.new((sx * bx, sy * by, lo)) for sx in (-1, 1) for sy in (-1, 1)}
    top = {(sx, sy): bm.verts.new((sx * tx, sy * ty, hi)) for sx in (-1, 1) for sy in (-1, 1)}
    walls = [outward(bm.faces.new((bot[(s, -1)], bot[(s, 1)], top[(s, 1)], top[(s, -1)])), Vector((s, 0, 0)))
             for s in (-1, 1)]
    walls += [outward(bm.faces.new((bot[(-1, s)], bot[(1, s)], top[(1, s)], top[(-1, s)])), Vector((0, s, 0)))
              for s in (-1, 1)]
    walls.append(outward(bm.faces.new([bot[k] for k in ((-1, -1), (1, -1), (1, 1), (-1, 1))]), Vector((0, 0, -1))))
    colour(bm, walls, PAL_TIMBER)
    #The rim: a flat iron band round the top, which is what reads from above.
    rim = 0.018
    inner = {k: bm.verts.new((k[0] * (tx - rim), k[1] * (ty - rim), hi)) for k in top}
    ring = [(-1, -1), (1, -1), (1, 1), (-1, 1)]
    colour(bm, [outward(bm.faces.new((top[ring[i]], top[ring[(i + 1) % 4]], inner[ring[(i + 1) % 4]],
                                      inner[ring[i]])), Vector((0, 0, 1))) for i in range(4)], PAL_STONE_DARK)
    #The load: a hump of broken stone inside the rim.
    pts = [Vector((k[0] * (tx - rim), k[1] * (ty - rim), hi - 0.01)) for k in top]
    pts += [Vector((rng.uniform(-0.07, 0.07), rng.uniform(-0.12, 0.12), hi + rng.uniform(0.02, 0.055)))
            for _ in range(6)]
    colour_by_facing(bm, add_hull(bm, pts, drop_below=hi - 0.005), PAL_STONE_LIGHT, PAL_STONE, PAL_STONE_DARK)
    #Wheels: flat hexagons just outside the tub, square to x, a corner down on the rail.
    r, zc = 0.045, 0.043
    for sx in (-1, 1):
        for y in (-0.085, 0.085):
            hexagon = [bm.verts.new((sx * 0.105, y + r * math.cos(math.pi * (k + 0.5) / 3),
                                     zc + r * math.sin(math.pi * (k + 0.5) / 3))) for k in range(6)]
            colour(bm, [outward(bm.faces.new(hexagon), Vector((sx, 0, 0)))], PAL_STONE_DARK)


def quarry_crane(bm, rng):
    """A timber derrick for an open quarry: a mast held by two stiff legs on sills, a boom reaching out
    along +x from the mast's foot, its topping line from the mast head, a cut block hanging from its tip,
    and the windlass that works it behind the mast."""
    f = block(bm, Vector((0.0, 0.0, -0.06)), (0.075, 0.075, 1.86))
    colour(bm, f["ends"] + f["sides"], PAL_TIMBER)
    colour(bm, f["top"], PAL_PATH)
    head, tip = Vector((0.0, 0.0, 1.76)), Vector((0.98, 0.0, 1.46))
    for sy in (-1, 1):
        foot = Vector((-0.62, sy * 0.42, -0.04))
        side, ends = rod(bm, Vector((-0.02, 0.0, 1.15)), foot, 0.032, 4)
        colour(bm, side + ends, PAL_TIMBER)
        side, ends = rod(bm, Vector((0.0, 0.0, 0.012)), Vector((foot.x, foot.y, 0.012)), 0.03, 4)
        colour(bm, side + ends, PAL_BARK)
    side, ends = rod(bm, Vector((0.05, 0.0, 0.28)), tip, 0.034, 4)
    colour(bm, side + ends, PAL_TIMBER)
    for a, b in ((head, tip), (tip, Vector((tip.x, 0.0, 0.58)))):
        side, _ = rod(bm, a, b, 0.009, 3, caps=(False, False))
        colour(bm, side, PAL_ROPE)
    f = block(bm, Vector((tip.x, 0.0, 0.44)), (0.22, 0.15, 0.14), 0.35, bottom=True)
    colour(bm, f["top"], PAL_STONE_LIGHT)
    colour(bm, f["ends"], PAL_WALL)
    colour(bm, f["sides"] + f["bottom"], PAL_STONE)
    #The windlass: two posts and a drum wound with rope.
    for sy in (-1, 1):
        f = block(bm, Vector((-0.24, sy * 0.13, -0.04)), (0.05, 0.05, 0.34))
        colour(bm, f["ends"] + f["sides"] + f["top"], PAL_BARK)
    side, ends = rod(bm, Vector((-0.24, -0.105, 0.25)), Vector((-0.24, 0.105, 0.25)), 0.045, 6, rot=0.0)
    colour(bm, side, PAL_ROPE)
    colour(bm, ends, PAL_BARK_DARK)


def slab(bm, rng, centre, rx, ry, z0, z1, sides, jitter=0.18, taper=0.90, dip=(0.0, 0.0)):
    """One bed of rock: an irregular flat slab from `z0` up to `z1`, a little narrower at the top, its top
    rising `dip` per unit of x and y. Returns (sides, top)."""
    rot = rng.uniform(0, 2 * math.pi)
    lo, hi = [], []
    for k in range(sides):
        a = rot + 2 * math.pi * k / sides
        r = 1.0 + rng.uniform(-jitter, jitter)
        x, y = rx * r * math.cos(a), ry * r * math.sin(a)
        lo.append(bm.verts.new((centre.x + x, centre.y + y, z0)))
        hi.append(bm.verts.new((centre.x + taper * x, centre.y + taper * y,
                                z1 + dip[0] * taper * x + dip[1] * taper * y)))
    #Triangles: a dipping top makes the side quads twist (tris_of_quad).
    side = [outward(f, lo[k].co + lo[(k + 1) % sides].co - 2 * centre) for k in range(sides)
            for f in tris_of_quad(bm, lo[k], lo[(k + 1) % sides], hi[(k + 1) % sides], hi[k])]
    return side, [outward(bm.faces.new(hi), Vector((0, 0, 1)))]


def outcrop(bm, rng):
    """Bedrock showing through flat ground (stone_plan.md: the deposit a quarry stands on): three beds
    stepping up to one side, like the walls' strata, the lowest coming out of the ground at a slant, and a
    few blocks weathered off at its low end. Broad and low, so it reads as ground and not as a rock lying on it."""
    #The beds' -x ends nearly line up, a short scarp like a wall's; to +x they step down and out.
    beds = [(Vector((0.0, 0.0, 0.0)), 1.00, 0.78, -GROUND_SINK, 0.16, 9, (-0.07, 0.03), PAL_STONE_DARK),
            (Vector((-0.30, 0.10, 0.0)), 0.64, 0.52, 0.10, 0.38, 7, (-0.03, 0.0), PAL_STONE),
            (Vector((-0.52, 0.16, 0.0)), 0.38, 0.32, 0.32, 0.58, 6, (0.0, 0.0), PAL_STONE_DARK)]
    for centre, rx, ry, z0, z1, sides, dip, side_col in beds:
        side, top = slab(bm, rng, centre, rx, ry, z0, z1, sides, jitter=0.22, dip=dip)
        colour(bm, side, side_col)
        colour(bm, top, PAL_STONE_LIGHT)
    for t, s in ((0.15, 1.0), (0.55, 0.8), (-0.35, 0.7)):
        foot = Vector((1.02 * math.cos(t), 0.82 * math.sin(t), 0.0))
        chunk(bm, rng, foot, (0.11 * s, 0.08 * s, 0.07 * s), cut=1)


outcrop.footprint_origin = True


ASSETS = [
    ("tree_pine_a", 11, lambda bm, r: conifer(bm, r, 4.0, 3, 1.90, 7, 0.40, (0.05, 0.0)), (0, 0)),
    ("tree_pine_b", 12, lambda bm, r: conifer(bm, r, 5.0, 4, 1.80, 7, 0.45, (0.18, 0.10)), (1, 0)),
    ("tree_pine_c", 13, lambda bm, r: conifer(bm, r, 3.0, 2, 1.90, 8, 0.35, (0.0, -0.06)), (2, 0)),
    ("tree_pine_d", 14, lambda bm, r: conifer(bm, r, 4.4, 3, 1.70, 7, 0.50, (-0.25, 0.12), dark=True), (3, 0)),
    ("tree_oak_a", 21, lambda bm, r: broadleaf_round(bm, r, 3.6, 1.15), (0, 1)),
    ("tree_oak_b", 22, lambda bm, r: broadleaf_lumps(bm, r, 3.4, [(0.0, 0.0, 0.95, 1.05, 0.85),
                                                                  (0.70, 0.35, 1.45, 0.70, 0.60),
                                                                  (-0.55, -0.45, 1.40, 0.72, 0.60)],
                                                     trunk_r=0.17), (1, 1)),
    ("tree_oak_c", 23, lambda bm, r: broadleaf_lumps(bm, r, 4.0, [(0.0, 0.0, 1.40, 0.68, 1.30)],
                                                     colours=(PAL_LEAF_LIGHT, PAL_LEAF_LIGHT, PAL_LEAF),
                                                     trunk_r=0.11, points=26, dark_trunk=True), (2, 1)),
    ("rock_a", 31, lambda bm, r: rock(bm, r, 0.95, 0.80, 0.85, 14), (0, 2)),
    ("rock_b", 32, lambda bm, r: rock(bm, r, 0.65, 0.45, 0.35, 11, jitter=0.22), (1, 2)),
    ("rock_c", 33, lambda bm, r: rock(bm, r, 0.28, 0.24, 0.22, 9), (2, 2)),
    ("rock_cluster_a", 34, lambda bm, r: [rock(bm, r, *p) for p in [(0.30, 0.26, 0.26, 9, Vector((0.0, 0.0, 0))),
                                                                     (0.20, 0.17, 0.16, 8, Vector((0.45, 0.22, 0))),
                                                                     (0.16, 0.14, 0.12, 7, Vector((-0.30, 0.38, 0))),
                                                                     (0.13, 0.12, 0.10, 7, Vector((0.12, -0.42, 0)))]],
     (3, 2)),
    ("stump_a", 41, stump, (0, 3)),
    ("log_a", 42, log, (1, 3)),
    ("bush_a", 51, lambda bm, r: bush(bm, r, [(0.0, 0.0, 0.55, 0.55), (0.45, 0.25, 0.40, 0.42),
                                              (-0.35, 0.30, 0.38, 0.38)]), (2, 3)),
    ("bush_b", 52, lambda bm, r: bush(bm, r, [(0.0, 0.0, 0.60, 0.65)], berries=5), (3, 3)),
    #Round 2: ground cover, then a tree per later biome.
    ("grass_a", 61, lambda bm, r: grass(bm, r, [(0.0, 0.0, 0.34), (0.10, 0.06, 0.26), (-0.08, 0.08, 0.22)],
                                        [PAL_LEAF_LIGHT, PAL_LEAF, PAL_LEAF_LIGHT]), (0, 4)),
    ("grass_b", 62, lambda bm, r: grass(bm, r, [(0.0, 0.0, 0.42), (0.12, -0.05, 0.30), (-0.10, -0.08, 0.28),
                                                (0.02, 0.13, 0.24)],
                                        [PAL_LEAF, PAL_LEAF_LIGHT, PAL_LEAF_LIGHT, PAL_LEAF]), (0.5, 4)),
    ("grass_c", 63, lambda bm, r: grass(bm, r, [(0.0, 0.0, 0.20), (0.09, 0.04, 0.16)],
                                        [PAL_GRASS_0 + 2, PAL_LEAF_LIGHT]), (1.0, 4)),
    ("flowers_a", 64, lambda bm, r: flowers(bm, r, [(0.0, 0.0, 0.22), (0.12, 0.05, 0.18)],
                                            [(0.05, -0.08, 0.16), (-0.10, 0.06, 0.13), (0.14, 0.12, 0.12)]),
     (1.5, 4)),
    ("flowers_b", 65, lambda bm, r: flowers(bm, r, [(0.0, 0.0, 0.16)],
                                            [(0.08, 0.02, 0.10), (-0.06, 0.07, 0.08), (-0.03, -0.09, 0.09),
                                             (0.17, -0.10, 0.06)]), (2.0, 4)),
    ("fern_a", 66, lambda bm, r: fern(bm, r, 5, 0.55), (2.5, 4)),
    ("shrub_a", 67, lambda bm, r: bush(bm, r, [(0.0, 0.0, 0.30, 0.26)]), (3.0, 4)),
    ("mushrooms_a", 68, lambda bm, r: mushrooms(bm, r, [(0.0, 0.0, 0.14, 0.09, PAL_ACCENT),
                                                        (0.13, 0.06, 0.09, 0.06, PAL_BARK),
                                                        (0.04, 0.14, 0.07, 0.05, PAL_BARK)]), (3.5, 4)),
    ("twig_a", 69, twig, (4.0, 4)),
    #Round 3: the swamp's and the river banks' reeds (biomes_plan.md, "Reeds").
    ("reeds_a", 81, lambda bm, r: reeds(bm, r, [(0.0, 0.0, 0.95), (0.08, 0.05, 0.80), (-0.07, 0.06, 0.70),
                                                (0.03, -0.08, 0.85)],
                                        [(-0.02, 0.02, 1.05)]), (4.5, 4)),
    ("reeds_b", 82, lambda bm, r: reeds(bm, r, [(0.0, 0.0, 0.75), (0.07, -0.04, 0.62), (-0.06, -0.05, 0.68),
                                                (0.05, 0.07, 0.55), (-0.04, 0.08, 0.60)], []), (5.0, 4)),
    ("tree_pine_snow_a", 71, lambda bm, r: snow_conifer(bm, r, 4.2, 3, 1.85, 7, 0.40, (0.06, -0.04)), (4, 0)),
    ("tree_palm_a", 72, lambda bm, r: palm(bm, r, 4.4, 0.9, 6), (3, 1)),
    ("tree_willow_a", 73, lambda bm, r: willow(bm, r, 3.6, 1.45, 10), (4, 1)),
    #Winter's oaks (seasons_plan.md): after their leafy ones, whose trunks they copy.
    ("tree_oak_a_bare", 24, bare_broadleaf("tree_oak_a"), (5, 1)),
    ("tree_oak_b_bare", 25, bare_broadleaf("tree_oak_b"), (6, 1)),
    ("tree_oak_c_bare", 26, bare_broadleaf("tree_oak_c", limbs=3), (7, 1)),
    #The archery range (range_plan.md, 2026-10-09).
    ("range_target", 91, range_target, (0, 6)),
    ("hay_bale", 92, hay_bale, (1, 6)),
    ("arrow", 93, arrow, (2, 6)),
    #Fruit (foraging_plan.md, 2026-10-12): on its plant, in the plant's space - after the plants. bush_b's own berries
    #move here. The slot is unused: each is placed on its plant.
    ("bush_a_fruit", 101, plant_fruit("bush_a", 6, 0.07), (2, 3)),
    ("bush_b_fruit", 102, plant_fruit("bush_b", 6, 0.07), (3, 3)),
    ("tree_oak_a_fruit", 103, plant_fruit("tree_oak_a", 9, 0.15, apples=True), (0, 1)),
    ("tree_oak_b_fruit", 104, plant_fruit("tree_oak_b", 10, 0.15, apples=True), (1, 1)),
    ("tree_oak_c_fruit", 105, plant_fruit("tree_oak_c", 8, 0.14, apples=True), (2, 1)),
    #Stone (stone_plan.md, 2026-10-14).
    ("rubble_a", 111, lambda bm, r: rubble(bm, r, 0.34, 0.30, 3, 4, 3), (0, 7)),
    ("rubble_b", 112, lambda bm, r: rubble(bm, r, 0.42, 0.26, 2, 5, 4, top=False), (1, 7)),
    ("boulder", 113, boulder, (2, 7)),
    ("stone_blocks", 114, stone_blocks, (3, 7)),
    ("floatstone_a", 115, floatstone_cluster, (4, 7)),
    ("floatstone_b", 116, lambda bm, r: floatstone_chunks(bm, r, 4), (5, 7)),
    ("mine_portal", 117, mine_portal, (6, 7)),
    ("mine_cart", 118, mine_cart, (7, 7)),
    ("quarry_crane", 119, quarry_crane, (8, 7)),
    ("outcrop", 120, outcrop, (9, 7)),
]

SPACING = 5.0


def build_object(name, seed, builder, slot, material, collection):
    rng = random.Random(seed)
    bm = bmesh.new()
    bm.faces.layers.int.new("pal")
    builder(bm, rng)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
    #The origin goes in the middle of what touches the ground - the trunk, for a tree, so a leaning
    #one leans away from its origin rather than being centred on its crown. Not for a part built in
    #another asset's space (keep_origin: a plant's fruit), which is drawn at that asset's origin.
    #A prop that fills a plot (footprint_origin) goes in the middle of its footprint instead, where its
    #chips and ledges would pull the base's middle off to one side.
    if getattr(builder, "footprint_origin", False):
        xs, ys = [v.co.x for v in bm.verts], [v.co.y for v in bm.verts]
        shift = Vector(((min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2, 0))
        for v in bm.verts:
            v.co -= shift
    elif not getattr(builder, "keep_origin", False):
        low = min(v.co.z for v in bm.verts)
        base = [v.co for v in bm.verts if v.co.z < low + 0.3]
        shift = Vector((sum(c.x for c in base) / len(base), sum(c.y for c in base) / len(base), 0))
        for v in bm.verts:
            v.co -= shift
    bm.normal_update()

    pal = bm.faces.layers.int["pal"]
    uv = bm.loops.layers.uv.new("UVMap")
    for f in bm.faces:
        u = pal_uv(f[pal] % PALETTE_COLS, f[pal] // PALETTE_COLS)
        for loop in f.loops:
            loop[uv].uv = u
        f.smooth = False
    bm.faces.layers.int.remove(pal)

    tris = sum(len(f.verts) - 2 for f in bm.faces)
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    mesh.materials.append(material)
    obj = bpy.data.objects.new(name, mesh)
    obj.location = (slot[0] * SPACING, -slot[1] * SPACING, 0.0)
    #...placed on that asset in the file, so the two are seen together (place_at).
    on = getattr(builder, "place_at", None)
    if on and on in bpy.data.objects:
        obj.location = bpy.data.objects[on].location.copy()
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
    tex.interpolation = "Closest"       #one cell is one flat colour, as in game
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
    export_image_format="NONE",         #the game draws props with its own palette material
    export_vertex_color="NONE",
    #The villagers' rig (character_armature) and its clips. export_apply above leaves the Armature
    #modifier out and applies the rest - a figure's Mirror - so the skin survives it.
    export_animations=True,
    #Keys as they are, not one per frame: tools/blender_retarget.py writes clips already reduced to
    #the LINEAR keys the engine's slerp needs, and sampling would put every frame back. Unsampled
    #keys keep their frame's time - the exporter only slides a clip to zero when it samples - so a
    #clip is keyed from frame 0.
    export_force_sampling=False,
    export_skins=True,
    export_morph=False,
    export_cameras=False,
    export_lights=False,
    export_extras=False,
)


def setup_export(collection):
    """Saves the export with the file twice over: as the collection's exporter (one click in the
    collection's properties, or File > Export All Collections), and as the remembered settings of
    the File > Export > glTF dialog, which is how archer's file does it."""
    with bpy.context.temp_override(collection=collection):
        bpy.ops.collection.exporter_add(name="IO_FH_gltf2")
    props = collection.exporters[-1].export_properties
    for key, value in EXPORT_SETTINGS.items():
        setattr(props, key, value)
    props.filepath = "//" + os.path.relpath(GLB, os.path.dirname(BLEND)).replace("\\", "/")

    remembered = dict(EXPORT_SETTINGS, use_active_collection=True, will_save_settings=True)
    bpy.context.scene["glTF2ExportSettings"] = remembered


def stage(collection_name):
    """Working bits, outside "Export": a camera at the game's angle and a sun from the north-west."""
    coll = bpy.data.collections.new(collection_name)
    bpy.context.scene.collection.children.link(coll)
    cam = bpy.data.objects.new("camera_55", bpy.data.cameras.new("camera_55"))
    cam.data.lens = 50
    elev = math.radians(55)
    target = Vector((1.5 * SPACING, -1.5 * SPACING, 1.0))
    dist = 32.0
    cam.location = target + Vector((0, -math.cos(elev), math.sin(elev))) * dist
    cam.rotation_euler = (math.pi / 2 - elev, 0, 0)
    coll.objects.link(cam)
    bpy.context.scene.camera = cam
    sun = bpy.data.objects.new("sun_nw", bpy.data.lights.new("sun_nw", "SUN"))
    sun.data.energy = 3.0
    sun.rotation_euler = SUN_TRAVEL.to_track_quat("-Z", "Y").to_euler()
    coll.objects.link(sun)


#The way sunlight TRAVELS: from the north-west (Blender's -X +Y), about 55 degrees up - README's
#lighting section. A sun lamp shines down its own -Z.
SUN_TRAVEL = Vector((1.0, -1.0, -2.0)).normalized()


def find_layer(layer, collection):
    """The view layer's entry for `collection`, searched from `layer` down."""
    if layer.collection == collection:
        return layer
    for child in layer.children:
        found = find_layer(child, collection)
        if found:
            return found
    return None


def export_as_last(export):
    """The GLB as the file was last exported from File > Export - with the settings it remembers, which is how the
    file is exported by hand: they sample the villagers' clips, and the collection's exporter (setup_export) does not,
    so exporting with it after hand edits drops keys from them (seen 2026-10-12: Idle gone, Shoot cut to 16 channels).
    With nothing remembered, the collection's exporter."""
    remembered = bpy.context.scene.get("glTF2ExportSettings")
    if not remembered:
        with bpy.context.temp_override(collection=export):
            bpy.ops.collection.export_all()
        return
    known = bpy.ops.export_scene.gltf.get_rna_type().properties.keys()
    settings = {k: v for k, v in dict(remembered).items() if k in known and k != "filepath"}
    layer = find_layer(bpy.context.view_layer.layer_collection, export)
    if layer:
        bpy.context.view_layer.active_layer_collection = layer     #use_active_collection: the Export collection
    bpy.ops.export_scene.gltf(filepath=GLB, **settings)


def save_and_export(export, fresh=True):
    #palette.png is stored as a path relative to the .blend, unpacked, so an edit to the PNG shows up
    #here too. Written by hand: relative_remap would write it with Windows backslashes. Only in a fresh
    #build, where it is the one image: a file edited by hand holds others, and its own are left alone.
    os.makedirs(os.path.dirname(BLEND), exist_ok=True)
    if fresh:
        bpy.data.images[0].filepath = "//" + os.path.relpath(PALETTE, os.path.dirname(BLEND)).replace("\\", "/")
        print("\nimage path in the .blend: %s" % bpy.data.images[0].filepath)
    bpy.context.preferences.filepaths.save_version = 0      #no chasm_props.blend1 beside it
    bpy.ops.wm.save_as_mainfile(filepath=BLEND, relative_remap=False)

    os.makedirs(os.path.dirname(GLB), exist_ok=True)
    if fresh:
        with bpy.context.temp_override(collection=export):
            bpy.ops.collection.export_all()
    else:
        export_as_last(export)
    print("wrote %s (%d bytes)" % (GLB, os.path.getsize(GLB)))


def read_glb(path):
    """A GLB's JSON and its binary chunk - the two chunks the exporter writes, in that order."""
    with open(path, "rb") as f:
        data = f.read()
    json_len = struct.unpack_from("<I", data, 12)[0]
    gltf = json.loads(data[20:20 + json_len].decode("utf-8"))
    at = 20 + json_len
    bin_len = struct.unpack_from("<I", data, at)[0]
    return gltf, bytearray(data[at + 8:at + 8 + bin_len])


def write_glb(path, gltf, blob):
    text = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    text += b" " * (-len(text) % 4)
    blob = bytes(blob) + b"\0" * (-len(blob) % 4)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, 28 + len(text) + len(blob)))
        f.write(struct.pack("<II", len(text), 0x4E4F534A) + text)
        f.write(struct.pack("<II", len(blob), 0x004E4942) + blob)


def append_glb(path, part_path):
    """Appends every mesh node of the GLB `part_path` to the one at `path`: their bytes after its binary
    chunk, their views, accessors, meshes and nodes after its own, the nodes into its scene. Their material
    is its material of the same name. Nothing already there moves, and that is checked after writing."""
    gltf, blob = read_glb(path)
    part, part_blob = read_glb(part_path)
    old = json.loads(json.dumps(gltf))
    old_blob = bytes(blob)
    materials = {m.get("name"): i for i, m in enumerate(gltf.get("materials", []))}

    def accessor(i):
        a = dict(part["accessors"][i])
        view = part["bufferViews"][a["bufferView"]]
        start = view.get("byteOffset", 0)
        blob.extend(b"\0" * (-len(blob) % 4))
        copy = {k: view[k] for k in ("byteStride", "target") if k in view}
        copy.update(buffer=0, byteOffset=len(blob), byteLength=view["byteLength"])
        blob.extend(part_blob[start:start + view["byteLength"]])
        gltf["bufferViews"].append(copy)
        a["bufferView"] = len(gltf["bufferViews"]) - 1
        gltf["accessors"].append(a)
        return len(gltf["accessors"]) - 1

    added = []
    for node in part["nodes"]:
        if "mesh" not in node or "children" in node or "skin" in node:
            sys.exit("--merge appends plain mesh nodes only; %s is not one" % node.get("name"))
        mesh = part["meshes"][node["mesh"]]
        prims = []
        for p in mesh["primitives"]:
            q = {k: v for k, v in p.items() if k not in ("attributes", "indices", "material")}
            q["attributes"] = {k: accessor(v) for k, v in p["attributes"].items()}
            if "indices" in p:
                q["indices"] = accessor(p["indices"])
            if "material" in p:
                name = part["materials"][p["material"]].get("name")
                if name not in materials:
                    sys.exit("--merge: material %s is not in %s" % (name, path))
                q["material"] = materials[name]
            prims.append(q)
        gltf["meshes"].append(dict(mesh, primitives=prims))
        gltf["nodes"].append(dict(node, mesh=len(gltf["meshes"]) - 1))
        gltf["scenes"][gltf.get("scene", 0)]["nodes"].append(len(gltf["nodes"]) - 1)
        added.append(node["name"])
    gltf["buffers"][0]["byteLength"] = len(blob) + (-len(blob) % 4)
    write_glb(path, gltf, blob)

    #The check: every array of the old file is a prefix of the new one's and so is its binary chunk, so
    #every node, mesh, skin and clip that was there is there byte for byte.
    new, new_blob = read_glb(path)
    assert bytes(new_blob[:len(old_blob)]) == old_blob, "the binary chunk changed"
    for key, value in old.items():
        if key in ("scenes", "buffers"):
            continue
        if isinstance(value, list):
            assert new[key][:len(value)] == value, "%s changed" % key
        else:
            assert new[key] == value, "%s changed" % key
    for s_old, s_new in zip(old["scenes"], new["scenes"]):
        assert s_new["nodes"][:len(s_old["nodes"])] == s_old["nodes"], "the scene's nodes changed"
    print("\nappended to %s: %s" % (path, ", ".join(added)))
    print("unchanged: %d nodes, %d meshes, %d clips, %d bytes of data" % (
        len(old["nodes"]), len(old["meshes"]), len(old.get("animations", [])), len(old_blob)))


def save_and_merge(objects):
    """--merge: exports only `objects` and appends them to the GLB as it is (append_glb), then saves the
    .blend - so adding a prop ships nothing else, however far the file has moved from the GLB."""
    if not objects:
        print("nothing to add")
        return
    gltf, _ = read_glb(GLB)
    there = [o.name for o in objects if o.name in {n.get("name") for n in gltf["nodes"]}]
    if there:
        sys.exit("--merge only adds, and %s are in the GLB already" % ", ".join(there))
    part = os.path.join(tempfile.mkdtemp(), "part.glb")
    for o in bpy.context.view_layer.objects:
        o.select_set(o in objects)
    settings = dict(EXPORT_SETTINGS, export_animations=False, export_skins=False, use_selection=True)
    bpy.ops.export_scene.gltf(filepath=part, **settings)
    append_glb(GLB, part)
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=BLEND, relative_remap=False)
    print("saved %s" % BLEND)


def add_missing(replace, merge=False):
    """Opens the existing .blend and builds only the assets it has no object for, leaving every
    object already there - and any edit made to it - exactly as it is."""
    bpy.ops.wm.open_mainfile(filepath=BLEND)
    for name in replace:
        if name in bpy.data.objects:
            bpy.data.objects.remove(bpy.data.objects[name])
            bpy.data.meshes.remove(bpy.data.meshes[name])
    export = bpy.data.collections["Export"]
    material = bpy.data.materials["palette"]
    have = {o.name for o in bpy.data.objects}
    built = []
    print("\n%-16s %5s" % ("added", "tris"))
    for name, seed, builder, slot in ASSETS:
        if name not in have:
            obj, tris = build_object(name, seed, builder, slot, material, export)
            built.append(obj)
            print("%-16s %5d" % (name, tris))
    if merge:
        save_and_merge(built)
    else:
        save_and_export(export, fresh=False)


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if os.path.exists(BLEND) and "--add" in argv:
        replace = argv[argv.index("--replace") + 1].split(",") if "--replace" in argv else []
        add_missing(replace, "--merge" in argv)
        return
    if os.path.exists(BLEND) and "--force" not in argv:
        print("\n%s exists and is edited by hand. Re-export it from Blender, pass --add to build only "
              "the assets it lacks, or --force to rebuild it from scratch (losing those edits)." % BLEND)
        sys.exit(1)

    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"

    export = bpy.data.collections.new("Export")
    scene.collection.children.link(export)
    material = palette_material()

    print("\n%-16s %5s" % ("asset", "tris"))
    for name, seed, builder, slot in ASSETS:
        obj, tris = build_object(name, seed, builder, slot, material, export)
        print("%-16s %5d" % (name, tris))

    stage("Stage")
    setup_export(export)
    save_and_export(export)


#Run, not imported: a preview script can take the builders without building the file.
if __name__ == "__main__":
    main()
