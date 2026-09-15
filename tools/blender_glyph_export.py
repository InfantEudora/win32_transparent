#!/usr/bin/env python
"""Export the Glyphs collection to a single .glb, the whole set in one go.

  blender --background art_source/fonts.blend --python tools/blender_glyph_export.py -- --out shared_assets/meshes/glyphs_unispace.glb

Same settings as the rest of the engine's .glb assets: +Y up, modifiers applied. The glyph meshes
are generated already rotated for that convention (see stand_glyph() in blender_glyph_meshes.py),
so each node arrives with an identity transform and geometry in glyph space: x from the pen
origin, y from the baseline, z the extrusion depth.

WHAT THIS FILE CARRIES, AND WHAT IT DELIBERATELY DOES NOT
---------------------------------------------------------
A glyph is an extruded outline that is flat-shaded in whatever colour the app picks. It needs
positions and normals, and nothing else. Both of the settings below are about saying that in the
file rather than leaving the loader to work it out from an absence.

ONE DEFAULT MATERIAL, ASSIGNED TO EVERY GLYPH. Its values are never read: LoadGlyphSetFromGLB
passes NULL for the material-list out-param, and the app assigns the colour it wants to slot 0
(see ApplicationTetris::SetLabelText). Its only job is to EXIST, because a primitive with no
material at all is a hole rather than a choice - core/GLTFLoader.cpp falls back to material id 0
and says "Mesh primitive has no material" while it does, once per glyph, 94 times per font load.
One material for all 94 also costs almost nothing: the loader dedupes by name, so it is built
once and the other 93 primitives hit LookupLoadedMaterial.

NO TEXCOORDS. Nothing samples a texture on a glyph, so a UV is two floats per vertex describing
an unwrap that no shader will ever read. Blender's text-to-mesh conversion generates a UVMap
anyway and the exporter writes it out unless told not to, and the result was actively misleading:
62% of those triangles are collinear in UV space - every side face of an extrusion nobody
unwrapped - which is exactly the shape of a real authoring mistake, so GLTFLoader reported it as
one. Leaving the attribute out entirely says "untextured on purpose", which the loader now reads
as the statement it is.

Measured 2026-09-15, the whole set: 407,776 -> 317,184 bytes. 71,672 of that is the UVs; 6,432 is
268 VERTICES THAT NO LONGER HAVE TO EXIST, which is the part worth knowing - a vertex shared by
two faces with different UVs has to be split in two to carry both, and with no UVs the two merge
again. The rest is 94 accessors and bufferViews of JSON. The geometry is untouched either way:
24,792 indices before and after, and identical POSITION bounds per glyph.

The two belong together and are not flags. A glyph set with materials but no UVs is the only
combination that is correct here, and making either optional would only preserve the ability to
regenerate the asset that this file exists to stop producing.
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
    p.add_argument("--material", default="glyph",
                   help="name of the default material given to every glyph that has none "
                        "(default: glyph). The name is all that reaches the engine; see the "
                        "module docstring for why the values do not matter.")
    return p.parse_args(argv)


def ensure_material(meshes, name):
    """Give every glyph a material, so no primitive is exported without one.

    Appended only where a mesh has no slots at all - a glyph that someone has deliberately
    given its own material in the .blend keeps it. use_nodes because the glTF exporter reads
    a Principled BSDF; a node-less material exports through a legacy path that has nothing to
    do with how the rest of the assets in this repo are written.
    """
    mat = bpy.data.materials.get(name)
    if mat is None:
        mat = bpy.data.materials.new(name)
        mat.use_nodes = True
    given = 0
    for o in meshes:
        if not o.data.materials:
            o.data.materials.append(mat)
            given += 1
    return mat, given


def main():
    args = parse_args()

    col = bpy.data.collections.get(args.collection)
    if col is None:
        raise SystemExit("no collection named %r - run blender_glyph_meshes.py first"
                         % args.collection)
    meshes = [o for o in col.objects if o.type == 'MESH']
    if not meshes:
        raise SystemExit("collection %r has no meshes" % args.collection)

    mat, given = ensure_material(meshes, args.material)
    print("material %r assigned to %d of %d glyphs (%d already had one)"
          % (mat.name, given, len(meshes), len(meshes) - given))

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
        export_materials='EXPORT',  #the default material - see the module docstring
        export_texcoords=False,     #glyphs are never textured - see the module docstring
        export_cameras=False,
        export_lights=False,
        export_animations=False,
        export_normals=True,
        export_tangents=False,
        export_yup=True,            #same convention as the rest of the engine's .glb assets
    )
    print("exported %d glyphs -> %s (%d bytes)" % (len(meshes), out, os.path.getsize(out)))


if __name__ == "__main__":
    main()
