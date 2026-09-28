#!/usr/bin/env python
"""Export a .blend with its saved glTF settings - keeping shape keys through the Mirror modifier.

    blender --background ../elfarcher_withprops.blend --python tools/blender_export_glb.py -- --out apps/archer/assets/meshes/archer.glb
    blender ... -- --out X.glb --set export_optimize_disable_viewport=False     (any saved setting overridden)
    blender ... -- --out X.glb --collection Export                              (the active collection, default Export)

WHY: the export dialog with "Apply Modifiers" on DROPS EVERY SHAPE KEY of a mesh that has a modifier
other than Armature. The glTF add-on says so in a comment and nowhere else (primitive_extract.py,
"shape keys are not preserved if we apply modifiers"): it exports the evaluated mesh, and an
evaluated mesh has no shape keys. archer_face is Mirror + Armature with a MouthOpen key, so the
key never reached the game - and whatever value the key sat at was baked into the face instead
(0.4 in the 2026-09-28 file: her mouth was exported 40% open). Blender itself will not apply a
modifier to a mesh with shape keys either, so doing it by hand means giving up the Mirror.

WHAT THIS DOES, on a mesh in the export with shape keys and modifiers BEFORE its first Armature:
  1. turns off the Armature and everything after it, so evaluating gives keys + those modifiers;
  2. evaluates the mesh once per key - every key at 0 for the basis, then each key alone at 1 -
     and reads the mirrored vertex positions;
  3. swaps in the evaluated basis as the object's mesh, drops the baked modifiers, puts the
     Armature back, and rebuilds every key on the new mesh from step 2, at value 0;
then exports with the file's saved settings (tools/blender_gltf_saved.py). THE .BLEND IS NEVER
SAVED - the Mirror, the half-face and the key values stay exactly as they were in the file.

A key must not change the mesh's TOPOLOGY through the modifiers - a Mirror's merge is decided per
evaluation, so a key that moved a seam vertex past the merge distance would add a vertex. That is
checked, and such an object is left alone (and exported without its keys, as before) with an
error saying which key.

Also on by default, and output-identical (checked 2026-09-28, see blender_export_profile.py):
export_optimize_disable_viewport, which took the archer export from 237 s to 58 s.
"""

import os
import sys
import time

import bpy

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from blender_gltf_saved import arg, saved_settings  # noqa: E402

OUT = arg(argv, "--out", None)
COLLECTION = arg(argv, "--collection", "Export")
if not OUT:
    print("\n--out PATH is required - this script does not guess where the game's asset lives.")
    sys.exit(1)
OUT = os.path.abspath(OUT)


def bake_keeping_shape_keys(obj):
    """Bakes obj's modifiers before its first Armature into its mesh, keys and all. See above."""
    mods = list(obj.modifiers)
    first_arm = next((i for i, m in enumerate(mods) if m.type == "ARMATURE"), len(mods))
    bake = [m for m in mods[:first_arm] if m.show_viewport]
    if not bake:
        return None
    keys = obj.data.shape_keys.key_blocks
    after = mods[first_arm:]
    after_vis = [m.show_viewport for m in after]
    values = [k.value for k in keys]
    for m in after:
        m.show_viewport = False

    def capture(active):
        for k in keys[1:]:
            k.value = 0.0
        if active is not None:
            active.value = 1.0
        depsgraph = bpy.context.evaluated_depsgraph_get()
        depsgraph.update()
        ev = obj.evaluated_get(depsgraph)
        m = ev.to_mesh()
        co = [0.0] * (len(m.vertices) * 3)
        m.vertices.foreach_get("co", co)
        ev.to_mesh_clear()
        return co

    basis = capture(None)
    shapes = []
    problem = None
    for k in keys[1:]:
        co = capture(k)
        if len(co) != len(basis):
            problem = "key '%s' changes the vertex count through the modifiers (%d -> %d)" % (
                k.name, len(basis) // 3, len(co) // 3)
            break
        shapes.append((k.name, co))

    #The basis mesh itself, evaluated with every key at 0 - UVs, materials and vertex groups come
    #with it, the mirrored groups included.
    for k in keys[1:]:
        k.value = 0.0
    depsgraph = bpy.context.evaluated_depsgraph_get()
    depsgraph.update()
    new_mesh = None
    if problem is None:
        new_mesh = bpy.data.meshes.new_from_object(obj.evaluated_get(depsgraph),
                                                   preserve_all_data_layers=True, depsgraph=depsgraph)
    for m, vis in zip(after, after_vis):
        m.show_viewport = vis
    for k, v in zip(keys, values):
        k.value = v
    if problem is not None:
        return "LEFT ALONE: " + problem

    baked_names = [m.name for m in bake]
    groups_before = len(obj.vertex_groups)
    obj.data = new_mesh
    for m in bake:
        obj.modifiers.remove(m)
    obj.shape_key_add(name="Basis", from_mix=False)
    for name, co in shapes:
        kb = obj.shape_key_add(name=name, from_mix=False)
        kb.data.foreach_set("co", co)
        kb.value = 0.0
    return "baked %s: %d verts, keys %s, %d vertex groups (was %d)" % (
        "+".join(baked_names), len(basis) // 3, [n for n, _ in shapes], len(obj.vertex_groups), groups_before)


#--- Bake -------------------------------------------------------------------------------------
settings = saved_settings(argv, OUT, COLLECTION)
if not any(a == "--set" and argv[i + 1].startswith("export_optimize_disable_viewport")
           for i, a in enumerate(argv[:-1])):
    settings["export_optimize_disable_viewport"] = True

export_root = bpy.data.collections.get(COLLECTION)
candidates = [o for o in (export_root.all_objects if export_root else bpy.context.scene.objects)
              if o.type == "MESH" and o.data.shape_keys and len(o.data.shape_keys.key_blocks) > 1]
print("\n=== SHAPE KEYS THROUGH MODIFIERS")
for obj in candidates:
    names = [m.name for m in obj.modifiers]
    result = bake_keeping_shape_keys(obj)
    if result is None:
        print("  %-24s nothing to bake (modifiers %s) - its keys export as they are" % (obj.name, names))
    else:
        print("  %-24s %s (modifiers were %s)" % (obj.name, result, names))

#--- Export -----------------------------------------------------------------------------------
print("\n=== EXPORT -> %s" % OUT)
start = time.perf_counter()
result = bpy.ops.export_scene.gltf(**settings)
print("  %s in %.1f s, %.1f MB" % (result, time.perf_counter() - start,
      os.path.getsize(OUT) / 1e6 if os.path.exists(OUT) else 0.0))
print("  (the .blend was not saved)")
