#!/usr/bin/env python
"""Export the Glyphs collection to a single .glb, the whole set in one go.

  blender --background fonts.blend --python tools/blender_glyph_export.py -- --out data/glyphs_unispace.glb

Same settings as the rest of data/*.glb: +Y up, modifiers applied. The glyph meshes are generated
already rotated for that convention (see stand_glyph() in blender_glyph_meshes.py), so each node
arrives with an identity transform and geometry in glyph space: x from the pen origin, y from the
baseline, z the extrusion depth.
"""

import bpy
import os
import sys
import argparse


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    p = argparse.ArgumentParser(prog="blender_glyph_export.py")
    p.add_argument("--collection", default="Glyphs")
    p.add_argument("--out", required=True, help="output .glb path")
    p.add_argument("--keep-materials", action="store_true",
                   help="export materials; off by default since the glyphs carry none and the "
                        "engine assigns material slots per Object anyway")
    return p.parse_args(argv)


def main():
    args = parse_args()

    col = bpy.data.collections.get(args.collection)
    if col is None:
        raise SystemExit("no collection named %r - run blender_glyph_meshes.py first"
                         % args.collection)
    meshes = [o for o in col.objects if o.type == 'MESH']
    if not meshes:
        raise SystemExit("collection %r has no meshes" % args.collection)

    out = os.path.abspath(args.out)
    os.makedirs(os.path.dirname(out), exist_ok=True)

    bpy.ops.object.select_all(action='DESELECT')
    for o in meshes:
        o.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]

    bpy.ops.export_scene.gltf(
        filepath=out,
        export_format='GLB',
        use_selection=True,
        export_apply=True,          #bake the Decimate modifier into the exported mesh
        export_materials='EXPORT' if args.keep_materials else 'NONE',
        export_cameras=False,
        export_lights=False,
        export_animations=False,
        export_normals=True,
        export_tangents=False,
        export_yup=True,            #same convention as the rest of data/*.glb
    )
    print("exported %d glyphs -> %s (%d bytes)" % (len(meshes), out, os.path.getsize(out)))


if __name__ == "__main__":
    main()
