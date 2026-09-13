"""Orbit Outpost - model the table's repeated parts in Blender and export them as parts.glb.

WHY A SCRIPT AND NOT A .blend
-----------------------------
pinball_design.md 3.2 asks for `meshes/parts.glb`: one file, one named node per part, each
modelled at the origin with its pivot where the engine will rotate it. Stage 0 was built from
core/Primitives instead because no such file existed, and the review of that build (see
docs/pinball_findings.md) asked for the Blender route the design describes.

This script IS that route, made reproducible: it builds every part with bmesh and Blender's
modifiers - tapered bats with rounded ends, domed bumper caps, torus rubbers, bevelled targets,
a chamfered saucer rim - and exports the lot. Anyone can open the result in Blender and improve a
part by hand; anyone can also rerun this and get the same file back, which a hand-made .blend
cannot promise. The dimensions come from the same numbers as Table.h (they are repeated here,
deliberately few, and listed together at the top so the two can be compared at a glance).

RUN IT HEADLESS
---------------
    "C:/Program Files/Blender Foundation/Blender 4.5/blender.exe" -b -P tools/pinball_parts_blender.py -- --out apps/pinball/assets/meshes/parts.glb

ApplicationPinball::LoadParts picks the file up if it exists and falls back to the primitives if
it does not, part by part, so a missing or half-finished file never stops the app.

CONVENTIONS THE APP DEPENDS ON
------------------------------
  - Engine axes: +X right, +Y up, +Z toward the player. Blender is Z-up, and its glTF exporter
    turns Blender (x, y, z) into glTF (x, z, -y). So engine (x, y, z) is modelled at Blender
    (x, -z, y) - the B() helper below - and comes out the right way round.
  - Each part's ORIGIN matches the primitive it replaces: a cylinder or box is centred, a bat has
    its pivot at the origin and lies along +X, the ball is centred. The app then places a part or
    its fallback with the same position and never has to know which it got.
  - Node names are the OBJECT names below. Materials are named after the app's own (pin_*) so
    they resolve to the app's tuned values; the colours set here only matter inside Blender.
  - Every mesh is UV-unwrapped (smart project) so TEXCOORD_0 exists; the loader copes without it
    but a texture on a part later would not.
"""

import argparse
import math
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector

# --- the numbers, mirroring Table.h -------------------------------------------------------------
BALL_RADIUS = 0.135
FLIPPER_LENGTH = 0.80
FLIPPER_U_LENGTH = 0.62
FLIPPER_THICKNESS = 0.20      # vertical
FLIPPER_WIDTH = 0.22          # at the pivot
FLIPPER_TIP_WIDTH = 0.13
BUMPER_RADIUS = 0.30
BUMPER_SKIRT_RADIUS = 0.42
BUMPER_HEIGHT = 0.52
BUMPER_CAP_HEIGHT = 0.14      # the primitive cap's height; the dome's origin is that cap's centre
POST_RADIUS = 0.075
RUBBER_RADIUS = 0.105
POST_HEIGHT = 0.36
SAUCER_RADIUS = 0.24
DROP_DEPTH, DROP_HEIGHT, DROP_WIDTH = 0.12, 0.30, 0.40
STANDUP_WIDTH, STANDUP_HEIGHT, STANDUP_DEPTH = 0.40, 0.32, 0.10
SPINNER_WIDTH, SPINNER_HEIGHT = 0.34, 0.24
GATE_WIDTH, GATE_HEIGHT = 0.30, 0.30
PLUNGER_TIP_RADIUS, PLUNGER_TIP_LENGTH = 0.11, 0.10
PLUNGER_ROD_RADIUS, PLUNGER_ROD_LENGTH = 0.05, 0.95
PLUNGER_KNOB_RADIUS = 0.14


def B(x, y, z):
    """Engine (x, up, toward-player) -> Blender (x, -z, y)."""
    return Vector((x, -z, y))


# --- scene plumbing -----------------------------------------------------------------------------


def clear_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)


_materials = {}


def material(name, rgb, metallic, roughness):
    if name in _materials:
        return _materials[name]
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = (rgb[0], rgb[1], rgb[2], 1.0)
    bsdf.inputs["Metallic"].default_value = metallic
    bsdf.inputs["Roughness"].default_value = roughness
    _materials[name] = mat
    return mat


def select_only(obj):
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj


def finish(obj, name, mat, smooth, bevel=None, bevel_segments=3):
    """Name it, colour it, round its edges, smooth it, unwrap it, bake its transform in."""
    obj.name = name
    obj.data.name = name
    obj.data.materials.clear()
    obj.data.materials.append(mat)
    select_only(obj)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    if bevel:
        mod = obj.modifiers.new("bevel", "BEVEL")
        mod.width = bevel
        mod.segments = bevel_segments
        mod.limit_method = "ANGLE"
        mod.angle_limit = math.radians(40)
        bpy.ops.object.modifier_apply(modifier=mod.name)
    if smooth:
        bpy.ops.object.shade_smooth()
        # Blender 4.1+: keep genuinely sharp edges sharp after smoothing.
        try:
            bpy.ops.object.shade_smooth_by_angle(angle=math.radians(35))
        except Exception:
            pass
    else:
        bpy.ops.object.shade_flat()
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(66), island_margin=0.02)
    bpy.ops.object.mode_set(mode="OBJECT")
    return obj


# --- the shapes ---------------------------------------------------------------------------------


def add_cylinder(name, radius, height, mat, centre_y=0.0, segments=32, bevel=None, smooth=True):
    """Centred at (0, centre_y, 0) in engine terms, axis along engine +Y - the MakeCylinder
    convention the app places by centre."""
    bpy.ops.mesh.primitive_cylinder_add(vertices=segments, radius=radius, depth=height,
                                        location=B(0, centre_y, 0))
    return finish(bpy.context.active_object, name, mat, smooth, bevel)


def add_box(name, sx, sy, sz, mat, bevel=None):
    """Engine-sized box, centred at the origin."""
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0, 0))
    obj = bpy.context.active_object
    obj.scale = B(sx, sy, sz)
    obj.scale = Vector((abs(obj.scale.x), abs(obj.scale.y), abs(obj.scale.z)))
    return finish(obj, name, mat, smooth=True, bevel=bevel, bevel_segments=2)


def add_sphere(name, radius, mat, squash_y=1.0):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=16, radius=radius,
                                         location=(0, 0, 0))
    obj = bpy.context.active_object
    obj.scale = Vector((1.0, 1.0, squash_y))       # engine Y is Blender Z
    return finish(obj, name, mat, smooth=True)


def add_torus(name, major, minor, mat):
    bpy.ops.mesh.primitive_torus_add(major_segments=32, minor_segments=14,
                                     major_radius=major, minor_radius=minor,
                                     location=(0, 0, 0))
    return finish(bpy.context.active_object, name, mat, smooth=True)


def add_bat(name, length, width, tip_width, height, mat):
    """A flipper bat: in plan a tapered capsule - a semicircle of `width` at the pivot, a
    semicircle of `tip_width` at the tip, straight flanks between - extruded `height` upward.
    Pivot at the origin, lying along engine +X, standing on engine y = 0. That origin is the whole
    point of the part: the hinge joint rotates the object about it (pinball_design.md 3.2)."""
    r0, r1 = width * 0.5, tip_width * 0.5
    n = 12
    outline = []
    # pivot end: from +90 deg round the back to +270 (i.e. -90), in the engine XZ plane
    for i in range(n + 1):
        a = math.radians(90 + 180 * i / n)
        outline.append((r0 * math.cos(a), r0 * math.sin(a)))
    # tip end: from -90 round the front to +90
    for i in range(n + 1):
        a = math.radians(-90 + 180 * i / n)
        outline.append((length + r1 * math.cos(a), r1 * math.sin(a)))
    mesh = bpy.data.meshes.new(name)
    verts = [B(x, 0.0, z) for x, z in outline] + [B(x, height, z) for x, z in outline]
    m = len(outline)
    faces = [list(range(m))[::-1], list(range(m, 2 * m))]
    for i in range(m):
        j = (i + 1) % m
        faces.append([i, j, m + j, m + i])
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    select_only(obj)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.normals_make_consistent(inside=False)
    bpy.ops.object.mode_set(mode="OBJECT")
    return finish(obj, name, mat, smooth=True, bevel=0.03, bevel_segments=3)


def add_dome(name, radius, height, mat, base_y):
    """A shallow dome with a flat flange: a pop bumper's cap. The top half of a sphere squashed to
    `height`, sitting on a thin disc of the same radius, the pair centred so that the object's
    origin is where the primitive cap's centre was (see BUMPER_CAP_HEIGHT)."""
    bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=16, radius=radius,
                                         location=(0, 0, 0))
    obj = bpy.context.active_object
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    # cut away everything below the equator and cap the hole
    geom = bm.verts[:] + bm.edges[:] + bm.faces[:]
    result = bmesh.ops.bisect_plane(bm, geom=geom, plane_co=(0, 0, 0), plane_no=(0, 0, 1),
                                    clear_inner=True)
    edges = [e for e in result["geom_cut"] if isinstance(e, bmesh.types.BMEdge)]
    bmesh.ops.contextual_create(bm, geom=edges)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(obj.data)
    bm.free()
    obj.scale = Vector((1.0, 1.0, height / radius))
    obj.location = B(0, base_y, 0)
    dome = finish(obj, name + "_dome", mat, smooth=True)
    # the flange
    bpy.ops.mesh.primitive_cylinder_add(vertices=32, radius=radius, depth=0.03,
                                        location=B(0, base_y - 0.015, 0))
    flange = bpy.context.active_object
    flange.name = name + "_flange"
    select_only(dome)
    flange.select_set(True)
    bpy.context.view_layer.objects.active = dome
    bpy.ops.object.join()
    return finish(dome, name, mat, smooth=True)


def add_ring(name, outer, inner, height, mat):
    """A saucer rim: a cylinder with a hole, chamfered. Centred, like the primitive it replaces."""
    bm = bmesh.new()
    n = 32
    lo, hi = -height * 0.5, height * 0.5
    ring = []
    for r in (outer, inner):
        for y in (lo, hi):
            ring.append([bm.verts.new(B(r * math.cos(2 * math.pi * i / n), y,
                                        r * math.sin(2 * math.pi * i / n))) for i in range(n)])
    outer_lo, outer_hi, inner_lo, inner_hi = ring
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((outer_lo[i], outer_lo[j], outer_hi[j], outer_hi[i]))    # outside wall
        bm.faces.new((inner_hi[i], inner_hi[j], inner_lo[j], inner_lo[i]))    # inside wall
        bm.faces.new((outer_hi[i], outer_hi[j], inner_hi[j], inner_hi[i]))    # top
        bm.faces.new((inner_lo[i], inner_lo[j], outer_lo[j], outer_lo[i]))    # bottom
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    return finish(obj, name, mat, smooth=True, bevel=0.015, bevel_segments=2)


# --- the parts list -----------------------------------------------------------------------------


def build_parts():
    chrome = material("pin_chrome", (0.92, 0.94, 0.97), 0.95, 0.13)
    orange = material("pin_orange", (0.91, 0.34, 0.12), 0.15, 0.34)
    cream = material("pin_cream", (0.91, 0.89, 0.81), 0.05, 0.42)
    rubber = material("pin_rubber", (0.055, 0.055, 0.065), 0.0, 0.72)
    ball = material("pin_ball", (0.95, 0.96, 0.98), 1.0, 0.08)
    teal = material("pin_teal", (0.12, 0.48, 0.50), 0.15, 0.38)

    add_sphere("ball", BALL_RADIUS, ball)

    add_bat("flipper_bat", FLIPPER_LENGTH, FLIPPER_WIDTH, FLIPPER_TIP_WIDTH, FLIPPER_THICKNESS, orange)
    add_bat("flipper_bat_upper", FLIPPER_U_LENGTH, FLIPPER_WIDTH, FLIPPER_TIP_WIDTH, FLIPPER_THICKNESS, orange)

    add_cylinder("bumper_body", BUMPER_RADIUS, BUMPER_HEIGHT, cream, bevel=0.02)
    # The primitive cap is a 0.14-tall disc placed by its centre; the dome keeps that origin, so
    # its base sits half a cap below the origin and it rises to a little above where the disc's
    # top was.
    add_dome("bumper_cap", BUMPER_SKIRT_RADIUS, BUMPER_CAP_HEIGHT + 0.06, orange,
             base_y=-BUMPER_CAP_HEIGHT * 0.5)

    add_cylinder("post", POST_RADIUS, POST_HEIGHT, chrome, bevel=0.012, segments=24)
    add_torus("rubber", POST_RADIUS + 0.005, RUBBER_RADIUS - POST_RADIUS + 0.01, rubber)

    add_ring("saucer_rim", SAUCER_RADIUS, SAUCER_RADIUS - 0.06, 0.10, teal)

    add_box("target_drop", DROP_DEPTH, DROP_HEIGHT, DROP_WIDTH, cream, bevel=0.015)
    add_box("target_standup", STANDUP_WIDTH, STANDUP_HEIGHT, STANDUP_DEPTH, teal, bevel=0.015)
    add_box("spinner_vane", SPINNER_WIDTH, SPINNER_HEIGHT, 0.02, chrome, bevel=0.006)
    add_box("gate_flap", GATE_WIDTH, GATE_HEIGHT, 0.03, chrome, bevel=0.006)

    # The plunger pieces lie along engine +Y like MakeCylinder does; the app turns them onto +Z.
    add_cylinder("plunger_tip", PLUNGER_TIP_RADIUS, PLUNGER_TIP_LENGTH, chrome, bevel=0.015, segments=24)
    add_cylinder("plunger_rod", PLUNGER_ROD_RADIUS, PLUNGER_ROD_LENGTH, chrome, segments=16)
    add_sphere("plunger_knob", PLUNGER_KNOB_RADIUS, orange, squash_y=0.75)


def export(path):
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", export_yup=True,
                              export_apply=True, export_materials="EXPORT",
                              export_normals=True, export_texcoords=True,
                              export_tangents=False, export_cameras=False, export_lights=False,
                              use_selection=False)
    print("wrote %s with %i parts" % (path, len(bpy.data.objects)))
    for obj in sorted(bpy.data.objects, key=lambda o: o.name):
        dims = obj.dimensions
        # report in ENGINE axes: (x, y_up, z) = blender (x, z, y)
        print("  %-18s %5i verts  extents x %.3f  y %.3f  z %.3f" % (
            obj.name, len(obj.data.vertices), dims.x, dims.z, dims.y))


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    args = ap.parse_args(argv)
    clear_scene()
    build_parts()
    export(args.out)


if __name__ == "__main__":
    main()
