#!/usr/bin/env python
"""Render a contact sheet of the glyph meshes, to check what decimation did to them.

  blender --background fonts.blend --python tools/blender_glyph_preview.py -- --out preview.png

The glyph objects all sit at the origin on purpose - their local coordinates are the glyph, so
that is what should be exported - which means they are an unreadable pile in the viewport. This
grids copies of them for one Workbench render and saves nothing back to the .blend.

--wire renders the triangles instead of the surface, which is the view that shows decimation
damage. Orientation is detected from the meshes, so this works either side of --no-stand.
"""

import bpy
import sys
import math
import argparse


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    p = argparse.ArgumentParser(prog="blender_glyph_preview.py")
    p.add_argument("--collection", default="Glyphs")
    p.add_argument("--out", required=True, help="output PNG path")
    p.add_argument("--cols", type=int, default=12)
    p.add_argument("--cell", type=float, default=0.85, help="grid pitch in glyph units")
    p.add_argument("--res", type=int, default=1600, help="output width in pixels")
    p.add_argument("--wire", action="store_true",
                   help="render the triangles instead of the surface, via a Wireframe modifier")
    p.add_argument("--wire-thickness", type=float, default=0.004,
                   help="wireframe strut thickness in glyph units (default: 0.004)")
    return p.parse_args(argv)


def detect_up_axis(glyphs):
    """Which Blender axis the glyphs' vertical runs along: 2 when stood up, 1 when left flat.

    The generator's default rotates glyphs onto +Z so a +Y-up glTF export gets them upright, but
    --no-stand leaves them in the XY plane, and the camera has to go somewhere different for each.
    """
    spread = [0.0, 0.0, 0.0]
    for o in glyphs:
        for i in (1, 2):
            vals = [v.co[i] for v in o.data.vertices]
            spread[i] = max(spread[i], max(vals) - min(vals))
    return 2 if spread[2] > spread[1] else 1


def main():
    args = parse_args()
    scene = bpy.context.scene

    col = bpy.data.collections.get(args.collection)
    if col is None:
        raise SystemExit("no collection named %r" % args.collection)
    glyphs = sorted((o for o in col.objects if o.type == 'MESH'), key=lambda o: o.name)
    if not glyphs:
        raise SystemExit("collection %r has no meshes" % args.collection)

    #The grid is laid out in the plane the glyphs live in; the third axis is the view direction.
    ax_h = 0
    ax_v = detect_up_axis(glyphs)
    ax_d = 1 if ax_v == 2 else 2

    #Everything already in the scene - text, text_all, the glyph pile at the origin - would
    #otherwise render straight through the grid.
    for o in scene.objects:
        o.hide_render = True

    #Grid of linked copies in a throwaway collection, so the originals keep their origins.
    preview = bpy.data.collections.new("__glyph_preview")
    scene.collection.children.link(preview)

    cols = args.cols
    rows = math.ceil(len(glyphs) / cols)
    for i, src in enumerate(glyphs):
        dup = bpy.data.objects.new(src.name + "__preview", src.data)
        #Copy the modifier stack so the render shows the decimated result, not the full mesh.
        for m in src.modifiers:
            nm = dup.modifiers.new(m.name, m.type)
            if m.type == 'DECIMATE':
                nm.decimate_type = m.decimate_type
                nm.ratio = m.ratio
        if args.wire:
            #Workbench's WIREFRAME shading mode is a viewport thing and renders empty to a still,
            #so turn the edges into real geometry instead and light them like everything else.
            wf = dup.modifiers.new("Wireframe", 'WIREFRAME')
            wf.thickness = args.wire_thickness
            wf.use_replace = True
        loc = [0.0, 0.0, 0.0]
        loc[ax_h] = (i % cols) * args.cell
        loc[ax_v] = -(i // cols) * args.cell
        dup.location = loc
        preview.objects.link(dup)

    #Frame on the real extent of the placed glyphs rather than on the nominal grid: the ink does
    #not fill its cell, and ascenders and descenders push past the row pitch.
    #From the vertices, not bound_box: a freshly created object's bound_box is still zeroed until
    #the depsgraph has evaluated it, which would silently shrink the frame and clip a column.
    hs, vs = [], []
    for o in preview.objects:
        if o.type != 'MESH':
            continue
        for v in o.data.vertices:
            hs.append(o.location[ax_h] + v.co[ax_h])
            vs.append(o.location[ax_v] + v.co[ax_v])
    pad = args.cell * 0.15
    h0, h1 = min(hs) - pad, max(hs) + pad
    v0, v1 = min(vs) - pad, max(vs) + pad
    w, h = h1 - h0, v1 - v0

    scene.render.engine = 'BLENDER_WORKBENCH'
    scene.render.image_settings.file_format = 'PNG'
    scene.render.resolution_x = args.res
    scene.render.resolution_y = max(1, int(round(args.res * h / w)))
    scene.render.resolution_percentage = 100
    scene.render.filepath = args.out
    scene.render.film_transparent = False

    #The image aspect was just matched to the frame, and ortho_scale spans whichever pixel
    #dimension is larger - so it is the larger of the two frame axes, portrait or landscape.
    cam_data = bpy.data.cameras.new("__glyph_cam")
    cam_data.type = 'ORTHO'
    cam_data.ortho_scale = max(w, h)
    cam = bpy.data.objects.new("__glyph_cam", cam_data)

    loc = [0.0, 0.0, 0.0]
    loc[ax_h] = (h0 + h1) / 2.0
    loc[ax_v] = (v0 + v1) / 2.0
    if ax_v == 2:
        #Standing: look along +Y from behind the glyphs' front face. An unrotated camera looks
        #down -Z, and rotating +90 about X swings that to +Y.
        loc[ax_d] = -10.0
        cam.rotation_euler = (math.pi / 2.0, 0.0, 0.0)
    else:
        #Flat: straight down -Z, which is how an unrotated camera already points.
        loc[ax_d] = 10.0
        cam.rotation_euler = (0.0, 0.0, 0.0)
    cam.location = loc
    preview.objects.link(cam)
    scene.camera = cam

    shading = scene.display.shading
    shading.light = 'STUDIO'
    shading.color_type = 'SINGLE'
    shading.single_color = (0.85, 0.85, 0.85)
    shading.show_shadows = False
    shading.show_cavity = True

    bpy.ops.render.render(write_still=True)
    print("rendered %d glyphs (%dx%d grid, %s) -> %s"
          % (len(glyphs), cols, rows,
             "standing" if ax_v == 2 else "flat", scene.render.filepath))


if __name__ == "__main__":
    main()
