#!/usr/bin/env python
"""Run a .blend's own glTF export under the profiler, and say where the time went.

    blender --background ../elfarcher_withprops.blend --python tools/blender_export_profile.py
    blender --background file.blend --python tools/blender_export_profile.py -- --out C:/tmp/probe.glb --top 40
    blender ... -- --set export_optimize_disable_viewport=True      (try one setting changed)

WHY: the export of the split archer took far longer than before, and the exporter says almost
nothing while it works. This reproduces it EXACTLY - the settings are the ones saved in the file
(the scene's glTF2ExportSettings, what the export dialog remembers), with the same active
collection - and runs it under cProfile, so the answer is measured instead of guessed.

IT NEVER WRITES THE GAME'S ASSET. The output goes to --out, by default a temp file, and is only
there to be timed.

WHAT IT PRINTS
--------------
  - the settings it used, and the collection it exported;
  - the wall time;
  - PHASES: time spent inside the exporter's big steps (animation sampling, meshes, images, the
    write), found by name in the profile - these overlap, a phase includes its callees;
  - how many times the scene was stepped to a frame (every sampled frame re-evaluates every object
    with modifiers - Geometry Nodes, Mirror - which is usually where an animation export goes);
  - the top functions by cumulative and by own time.

The collection: the dialog's "active collection" setting exports whatever collection was active
when you pressed Export. A background Blender has no UI to remember that, so it is set here to
--collection (default "Export").
"""

import cProfile
import io
import os
import pstats
import sys
import tempfile
import time

import bpy

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from blender_gltf_saved import arg, saved_settings  # noqa: E402

OUT = arg(argv, "--out", os.path.join(tempfile.gettempdir(), "blender_export_profile.glb"))
TOP = int(arg(argv, "--top", "30"))
COLLECTION = arg(argv, "--collection", "Export")

#--- The settings, as the file saved them, --set overrides included - see blender_gltf_saved.py --
settings = saved_settings(argv, OUT, COLLECTION)

print("\n=== SETTINGS (from the file)")
for key in sorted(settings):
    print("  %s = %r" % (key, settings[key]))
print("  active collection:", bpy.context.view_layer.active_layer_collection.collection.name)

#--- The export, profiled ----------------------------------------------------------------------
profiler = cProfile.Profile()
start = time.perf_counter()
profiler.enable()
result = bpy.ops.export_scene.gltf(**settings)
profiler.disable()
wall = time.perf_counter() - start

stats = pstats.Stats(profiler)
print("\n=== RESULT %s in %.1f s -> %s (%.1f MB)" % (result, wall, OUT,
      os.path.getsize(OUT) / 1e6 if os.path.exists(OUT) else 0.0))

#The exporter's big steps, by function name. Cumulative, so they overlap.
PHASES = [
    ("animations (all)",       "gather_animations"),
    ("  action sampling",      "gather_action_animations"),
    ("  armature sampling",    "get_cache_data"),
    ("  keyframe optimising",  "optimize"),
    ("meshes",                 "gather_mesh"),
    ("  primitives",           "extract_primitives"),
    ("skins",                  "gather_skin"),
    ("materials",              "gather_material"),
    ("images (encode)",        "encode_image"),
    ("images (numpy)",         "__encode_from_numpy_array"),
    ("writing the file",       "save_gltf"),
]
rows = stats.stats  # {(file, line, func): (cc, nc, tt, ct, callers)}
print("\n=== PHASES (cumulative seconds; a phase includes what it calls)")
for label, func in PHASES:
    ct = max((v[3] for k, v in rows.items() if k[2] == func), default=0.0)
    calls = sum(v[1] for k, v in rows.items() if k[2] == func)
    if ct > 0.0:
        print("  %-24s %8.2f s  %6.1f%%  (%d calls)" % (label, ct, 100.0 * ct / wall, calls))

frame_steps = [(k, v) for k, v in rows.items() if "frame_set" in k[2]]
for k, v in frame_steps:
    print("\n=== frame_set: %d calls, %.2f s own time (%.1f%% of the export)" % (v[1], v[2], 100.0 * v[2] / wall))

for order, title in (("cumulative", "CUMULATIVE"), ("tottime", "OWN TIME")):
    buf = io.StringIO()
    pstats.Stats(profiler, stream=buf).strip_dirs().sort_stats(order).print_stats(TOP)
    print("\n=== TOP %d BY %s" % (TOP, title))
    lines = buf.getvalue().splitlines()
    #Skip pstats' preamble down to the table header.
    for i, line in enumerate(lines):
        if line.strip().startswith("ncalls"):
            print("\n".join(lines[i:]))
            break
