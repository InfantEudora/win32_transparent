"""Checks Chasm's props two ways. Never saves the .blend.

    blender.exe -b --python apps/chasm/tools/blender_chasm_props_check.py -- --verify
    blender.exe -b --python apps/chasm/tools/blender_chasm_props_check.py -- --render

--verify imports apps/chasm/assets/meshes/chasm_props.glb into an EMPTY scene - what the game gets,
not what the .blend holds - and checks every mesh: its name, its triangle count, that its origin is
the middle of its base (local bounds centred on x/y, ground at z = 0), that every UV is the centre
of a row-0 palette cell, and that every normal is its face's normal (flat shading survived).

--render opens art_source/chasm/chasm_props.blend and writes art_source/chasm/previews/:
lineup.png and lineup_cover.png (Workbench, every asset from the game's 55 degree camera),
forest_*.png (Eevee, the props scattered on palette grass under a north-west sun, to hold up against
alittleagedemo.jpg; forest_maxzoom.png is the same view at the game's furthest zoom), and
biomes.png (a patch per palette row, the props' UVs moved to that row as the game will).
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
PREVIEWS = os.path.join(REPO, "art_source", "chasm", "previews")

PALETTE_COLS = 32
PALETTE_ROWS = 16
TRI_BUDGET = {"tree_pine": 100, "tree_oak": 120, "tree_palm": 120, "tree_willow": 120,
              "grass": 12, "fern": 24, "shrub": 24}
COVER = ("grass", "flowers", "fern", "shrub", "mushrooms", "twig")
SUN_TRAVEL = Vector((1.0, -1.0, -2.0)).normalized()     #from the north-west, as blender_chasm_props.py


def verify():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=GLB)
    failures = []
    total = 0
    print("\n%-16s %5s %6s %6s %6s  %s" % ("asset", "tris", "w", "d", "h", "palette columns"))
    for obj in sorted(bpy.context.scene.objects, key=lambda o: o.name):
        if obj.type != "MESH":
            failures.append("%s: not a mesh (%s)" % (obj.name, obj.type))
            continue
        mesh = obj.data
        mesh.calc_loop_triangles()
        tris = len(mesh.loop_triangles)
        total += tris
        lo = Vector([min(v.co[i] for v in mesh.vertices) for i in range(3)])
        hi = Vector([max(v.co[i] for v in mesh.vertices) for i in range(3)])
        size = hi - lo

        #The origin: ground level at z = 0 (bases reach a little below it on purpose), and the
        #middle of the footprint near x = y = 0. Leaning trees sit off-centre by their lean, so
        #"near" is judged against the base, the verts within 0.3 of the ground.
        base = [v.co for v in mesh.vertices if v.co.z < lo.z + 0.3]
        bx = sum(c.x for c in base) / len(base)
        by = sum(c.y for c in base) / len(base)
        if not (-0.15 <= lo.z <= 0.0) or abs(bx) > 0.2 or abs(by) > 0.2:
            failures.append("%s: origin is not the base centre (min z %.3f, base centre %.2f %.2f)"
                            % (obj.name, lo.z, bx, by))

        columns = set()
        uv = mesh.uv_layers[0].data if mesh.uv_layers else None
        if uv is None:
            failures.append("%s: no UVs" % obj.name)
        else:
            for poly in mesh.polygons:
                us = {(round(uv[i].uv.x, 5), round(uv[i].uv.y, 5)) for i in poly.loop_indices}
                if len(us) != 1:
                    failures.append("%s: face %d spans %d UVs" % (obj.name, poly.index, len(us)))
                    break
                u, v = us.pop()
                col = u * PALETTE_COLS - 0.5
                row = (1.0 - v) * PALETTE_ROWS - 0.5     #back from Blender's V-up to image rows
                if abs(col - round(col)) > 1e-3 or abs(row) > 1e-3:
                    failures.append("%s: UV %.4f %.4f is not a row-0 cell centre" % (obj.name, u, v))
                    break
                columns.add(int(round(col)))

        #Flat: the importer keeps the file's normals as custom split normals.
        bent = 0
        for poly in mesh.polygons:
            for i in poly.loop_indices:
                if mesh.corner_normals[i].vector.dot(poly.normal) < 0.999:
                    bent += 1
        if bent:
            failures.append("%s: %d corners do not use their face's normal" % (obj.name, bent))

        for prefix, budget in TRI_BUDGET.items():
            if obj.name.startswith(prefix) and tris > budget:
                failures.append("%s: %d triangles, budget %d" % (obj.name, tris, budget))
        print("%-16s %5d %6.2f %6.2f %6.2f  %s" % (obj.name, tris, size.x, size.y, size.z,
                                                    " ".join(str(c) for c in sorted(columns))))
    print("%-16s %5d" % ("total", total))
    print("\n" + ("\n".join("FAIL " + f for f in failures) if failures else "all checks pass"))
    return not failures


# ---------------------------------------------------------------------------------------------

def look_from(cam, target, elevation_deg, azimuth_deg, dist):
    """Puts the camera `dist` from `target`, `elevation_deg` above the ground, looking north (+Y)
    when azimuth is 0 - the game's view, north at the top of the screen."""
    e = math.radians(elevation_deg)
    a = math.radians(azimuth_deg)
    offset = Vector((-math.sin(a) * math.cos(e), -math.cos(a) * math.cos(e), math.sin(e))) * dist
    cam.location = target + offset
    cam.rotation_euler = (-offset).to_track_quat("-Z", "Y").to_euler()


def render(path, engine, res):
    scene = bpy.context.scene
    scene.render.engine = engine
    scene.render.resolution_x, scene.render.resolution_y = res
    scene.render.resolution_percentage = 100
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    print("wrote %s" % path)


def grass_ground(material, size_x, size_y, cell=1.5, seed=7, row=0, centre=(0.0, 0.0)):
    """A faceted ground: a triangulated grid, each triangle one of the four grass shades - how
    Chasm's own terrain is coloured. `row` is the biome."""
    rng = random.Random(seed)
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new("UVMap")
    nx, ny = int(size_x / cell), int(size_y / cell)
    grid = [[bm.verts.new((centre[0] + x * cell - size_x / 2, centre[1] + y * cell - size_y / 2, 0.0))
             for x in range(nx + 1)] for y in range(ny + 1)]
    for y in range(ny):
        for x in range(nx):
            a, b, c, d = grid[y][x], grid[y][x + 1], grid[y + 1][x + 1], grid[y + 1][x]
            for tri in ((a, b, c), (a, c, d)):
                f = bm.faces.new(tri)
                col = rng.randrange(4)
                for loop in f.loops:
                    loop[uv].uv = ((col + 0.5) / PALETTE_COLS, 1.0 - (row + 0.5) / PALETTE_ROWS)
    mesh = bpy.data.meshes.new("preview_ground")
    bm.to_mesh(mesh)
    bm.free()
    mesh.materials.append(material)
    obj = bpy.data.objects.new("preview_ground", mesh)
    bpy.context.scene.collection.objects.link(obj)


def place(src, location, rng, scale_range=(0.85, 1.15)):
    obj = src.copy()                     #shares the mesh, like the game's instancing
    if location[1] >= BIOME_Y - 30:      #the biome strip draws copies moved to their row
        obj.data = in_row(src.data, int(round((location[0] - BIOME_X0) / BIOME_STEP)))
    obj.location = location
    obj.hide_render = False              #the lineup's originals are hidden, and copy() copies that
    obj.rotation_euler = (0, 0, rng.uniform(0, 2 * math.pi))
    s = rng.uniform(*scale_range)
    obj.scale = (s, s, s)
    bpy.context.scene.collection.objects.link(obj)


def forest(props, rng):
    """A clearing in a pine forest, as in the reference: a dense block of conifers with a ragged
    edge, broadleaf trees and bushes along it, rocks in the open."""
    pines = [o for n, o in props.items() if n.startswith("tree_pine") and "snow" not in n]
    oaks = [o for n, o in props.items() if n.startswith("tree_oak")]
    rocks = [o for n, o in props.items() if n.startswith(("rock", "stump", "log"))]
    bushes = [o for n, o in props.items() if n.startswith("bush")]
    cover(props, rng, (-38, 38), (-26, 26), 4200)
    spacing = 1.35
    for iy in range(-16, 17):
        for ix in range(-24, 25):
            x = ix * spacing + rng.uniform(-0.45, 0.45)
            y = iy * spacing + rng.uniform(-0.45, 0.45)
            edge = math.hypot(x / 1.4 + 4, y + 2) - 10 + 2.5 * math.sin(x * 0.7) * math.cos(y * 0.5)
            if edge < 0:
                continue                 #the clearing
            if edge < 1.5 and rng.random() < 0.5:
                continue                 #a ragged edge
            if x > 14 and rng.random() < 0.7:
                continue                 #thinning out to the east
            place(rng.choice(pines), (x, y, 0), rng)
    for _ in range(9):
        a = rng.uniform(0, 2 * math.pi)
        place(rng.choice(oaks), (-5.6 + 12 * math.cos(a), -2 + 8.5 * math.sin(a), 0), rng)
    for _ in range(10):
        a = rng.uniform(0, 2 * math.pi)
        place(rng.choice(bushes), (-5.6 + 11 * math.cos(a), -2 + 7.5 * math.sin(a), 0), rng)
    for _ in range(9):
        place(rng.choice(rocks), (rng.uniform(-12, 1), rng.uniform(-6, 3), 0), rng)


#How common each kind of ground cover is, relative to the others.
COVER_WEIGHTS = {"grass_a": 6, "grass_b": 5, "grass_c": 6, "flowers_a": 1.5, "flowers_b": 1.5,
                 "fern_a": 2, "shrub_a": 1, "mushrooms_a": 0.4, "twig_a": 0.6}


def cover(props, rng, xs, ys, count, kinds=None):
    names = [n for n in COVER_WEIGHTS if n in props and (kinds is None or n.startswith(kinds))]
    weights = [COVER_WEIGHTS[n] for n in names]
    for _ in range(count):
        n = rng.choices(names, weights)[0]
        place(props[n], (rng.uniform(*xs), rng.uniform(*ys), 0), rng, (0.8, 1.3))


#The biome strip sits far north of the forest: one patch per palette row, side by side.
BIOME_Y = 400.0
BIOME_X0 = -36.0
BIOME_STEP = 24.0
_row_meshes = {}


def in_row(mesh, row):
    """A copy of `mesh` with every UV moved down to palette row `row` - what the game does to a
    whole biome's props."""
    if row == 0:
        return mesh
    key = (mesh.name, row)
    if key not in _row_meshes:
        copy = mesh.copy()
        for loop_uv in copy.uv_layers[0].data:
            loop_uv.uv.y -= row / PALETTE_ROWS
        _row_meshes[key] = copy
    return _row_meshes[key]


def biomes(props, material, rng):
    """Temperate, desert, frozen, swamp: each patch on its own row of grass, with the trees made
    for it - so the snow pine is seen as snow, not as a two-tone pine."""
    plan = [
        ([n for n in props if n.startswith(("tree_pine_a", "tree_pine_b", "tree_pine_c", "tree_oak"))], 22),
        (["tree_palm_a", "rock_a", "rock_b", "shrub_a"], 12),
        (["tree_pine_snow_a", "tree_pine_snow_a", "rock_b", "rock_c"], 22),
        (["tree_willow_a", "tree_willow_a", "bush_a", "fern_a", "log_a"], 12),
    ]
    for row, (names, count) in enumerate(plan):
        cx = BIOME_X0 + row * BIOME_STEP
        grass_ground(material, BIOME_STEP, 22, seed=row, row=row, centre=(cx, BIOME_Y))
        for _ in range(count):
            n = rng.choice(names)
            place(props[n], (cx + rng.uniform(-10, 10), BIOME_Y + rng.uniform(-9, 9), 0), rng)
        cover(props, rng, (cx - 11.5, cx + 11.5), (BIOME_Y - 10.5, BIOME_Y + 10.5), 500)


def preview():
    bpy.ops.wm.open_mainfile(filepath=BLEND)
    scene = bpy.context.scene
    os.makedirs(PREVIEWS, exist_ok=True)
    props = {o.name: o for o in bpy.data.collections["Export"].objects}
    material = bpy.data.materials["palette"]
    cam = scene.camera

    #Lineup: Workbench, the palette texture, studio light with shadows.
    shading = scene.display.shading
    shading.light = "STUDIO"
    shading.color_type = "TEXTURE"
    shading.show_shadows = True
    scene.world = bpy.data.worlds.new("lineup_sky")      #a Workbench render's background
    scene.world.color = (0.70, 0.80, 0.86)
    scene.display.light_direction = (-0.45, 0.45, 0.77)
    scene.render.film_transparent = False
    look_from(cam, Vector((10.0, -10.0, 1.0)), 55, 0, 38)
    cam.data.lens = 45
    render(os.path.join(PREVIEWS, "lineup.png"), "BLENDER_WORKBENCH", (1600, 1100))
    #The ground cover, close enough to see.
    look_from(cam, Vector((10.0, -20.0, 0.1)), 55, 0, 28)
    render(os.path.join(PREVIEWS, "lineup_cover.png"), "BLENDER_WORKBENCH", (1600, 600))

    #Forest: hide the lineup, build a clearing out of shared-mesh copies, and light it like the
    #game - one sun from the north-west, a sky ambient, no tone mapping.
    for o in props.values():
        o.hide_render = True
    grass_ground(material, 80, 56)
    forest(props, random.Random(3))
    biomes(props, material, random.Random(5))
    sun = bpy.data.objects["sun_nw"]
    sun.rotation_euler = SUN_TRAVEL.to_track_quat("-Z", "Y").to_euler()
    sun.data.energy = 3.2
    sun.data.angle = math.radians(1.0)
    world = bpy.data.worlds.new("sky")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.62, 0.74, 0.92, 1.0)
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.9
    scene.world = world
    scene.view_settings.view_transform = "Standard"
    engine = "BLENDER_EEVEE_NEXT" if "BLENDER_EEVEE_NEXT" in {
        e.identifier for e in bpy.types.RenderSettings.bl_rna.properties["engine"].enum_items} else "BLENDER_EEVEE"

    cam.data.lens = 35
    look_from(cam, Vector((-2, 0, 0)), 55, 0, 46)
    render(os.path.join(PREVIEWS, "forest_wide.png"), engine, (1600, 900))
    #The same view at the game's furthest zoom, about 15 px per house (2 units): that frame is 47
    #units across, so 350 px. This is the one that says whether a prop is detail or noise.
    render(os.path.join(PREVIEWS, "forest_maxzoom.png"), engine, (352, 198))
    look_from(cam, Vector((BIOME_X0 + 1.5 * BIOME_STEP, BIOME_Y, 0)), 55, 0, 92)
    render(os.path.join(PREVIEWS, "biomes.png"), engine, (1600, 700))
    cam.data.lens = 50
    look_from(cam, Vector((-14, -3, 1.0)), 55, 0, 22)     #the clearing's west edge: every kind of prop
    render(os.path.join(PREVIEWS, "forest_close.png"), engine, (1600, 900))


argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
if "--render" in argv:
    preview()
else:
    sys.exit(0 if verify() else 1)
