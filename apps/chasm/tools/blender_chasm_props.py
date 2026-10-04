"""Builds the FIRST art_source/chasm/chasm_props.blend - Chasm's low-poly props - and exports it.

    blender.exe -b --python apps/chasm/tools/blender_chasm_props.py -- [--force]

Writes the .blend and apps/chasm/assets/meshes/chasm_props.glb. The .blend is the source from then
on and is edited by hand, so this refuses to overwrite it without --force - re-running it throws
away every edit made in Blender since. To re-export after an edit, use the "Export" collection's
exporter (Collection properties > Exporters > Export), which this sets up; no script needed.

Everything is generated from fixed seeds, so a forced rebuild gives the same props.

THE LOOK (README "No textures: one palette"): flat-shaded facets, every face one colour, picked by
pointing all of the face's UVs at the centre of one palette cell (Palette.h). Row 0 only - the game
picks the biome by shifting rows. A face's colour comes partly from which way it faces (lighter on
top, darker underneath) - a little baked occlusion, on top of what the sun does in game.

BUDGET: conifers are the forests, drawn instanced by the ten thousand, so they stay near 50
triangles; nothing here is over about 110. Faces nobody can see - trunk and rock bottoms, which sit
in the ground - are not built at all.
"""

import math
import os
import random
import sys

import bmesh
import bpy
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
BLEND = os.path.join(REPO, "art_source", "chasm", "chasm_props.blend")
GLB = os.path.join(REPO, "apps", "chasm", "assets", "meshes", "chasm_props.glb")
PALETTE = os.path.join(REPO, "apps", "chasm", "assets", "textures", "palette.png")

#Palette.h - must agree with it.
PALETTE_COLS = 32
PALETTE_ROWS = 16
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
    loose = [g for g in res["geom_interior"] + res["geom_unused"] if isinstance(g, bmesh.types.BMVert)]
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


#name, seed, builder, slot (column, row) in the authoring layout
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
]

SPACING = 5.0


def build_object(name, seed, builder, slot, material, collection):
    rng = random.Random(seed)
    bm = bmesh.new()
    bm.faces.layers.int.new("pal")
    builder(bm, rng)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
    #The origin goes in the middle of what touches the ground - the trunk, for a tree, so a leaning
    #one leans away from its origin rather than being centred on its crown.
    low = min(v.co.z for v in bm.verts)
    base = [v.co for v in bm.verts if v.co.z < low + 0.3]
    shift = Vector((sum(c.x for c in base) / len(base), sum(c.y for c in base) / len(base), 0))
    for v in bm.verts:
        v.co -= shift
    bm.normal_update()

    pal = bm.faces.layers.int["pal"]
    uv = bm.loops.layers.uv.new("UVMap")
    for f in bm.faces:
        u = pal_uv(f[pal])
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
    export_animations=False,
    export_skins=False,
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


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if os.path.exists(BLEND) and "--force" not in argv:
        print("\n%s exists and is edited by hand. Re-export it from Blender, or pass --force to "
              "rebuild it from scratch (losing those edits)." % BLEND)
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

    #palette.png is stored as a path relative to the .blend, unpacked, so an edit to the PNG shows up
    #here too. Written by hand: relative_remap would write it with Windows backslashes.
    os.makedirs(os.path.dirname(BLEND), exist_ok=True)
    bpy.data.images[0].filepath = "//" + os.path.relpath(PALETTE, os.path.dirname(BLEND)).replace("\\", "/")
    bpy.context.preferences.filepaths.save_version = 0      #no chasm_props.blend1 beside it
    bpy.ops.wm.save_as_mainfile(filepath=BLEND, relative_remap=False)
    print("\nimage path in the .blend: %s" % bpy.data.images[0].filepath)

    os.makedirs(os.path.dirname(GLB), exist_ok=True)
    with bpy.context.temp_override(collection=export):
        bpy.ops.collection.export_all()
    print("wrote %s (%d bytes)" % (GLB, os.path.getsize(GLB)))


main()
