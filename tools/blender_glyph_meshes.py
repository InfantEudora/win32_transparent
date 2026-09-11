#!/usr/bin/env python
"""Generate one mesh per glyph from the text object in fonts.blend.

Run headless:
  blender --background fonts.blend --python tools/blender_glyph_meshes.py -- [options]

Rather than cutting up the pre-converted `text_all` mesh - which loses the pen origins and
shatters any glyph with more than one contour (i, j, :, =, %) - this rebuilds every glyph from
the font itself, copying all settings off the existing text object. Each glyph becomes its own
mesh object in a `Glyphs` collection, with:

  - x = 0 at the pen origin, so the layout loop only has to add pen_x (bearing stays in the mesh)
  - the baseline at 0, and 2*extrude of depth, matching the existing text_all mesh
  - stood up out of Blender's XY plane, so that a +Y-up glTF export lands them upright in the
    engine rather than flat on the floor - see stand_glyph()
  - an unapplied Decimate modifier, so the ratio stays tweakable per glyph in the .blend

Also writes a JSON metrics sidecar (advances, ink bounds, line height) next to the .blend, since
per-glyph meshes are useless for layout without the advances.

Idempotent: an existing Glyphs collection and its objects are removed first, so re-running is safe.
"""

import bpy
import json
import os
import sys
import argparse

#Printable ASCII. Space carries an advance but no geometry, so it gets a metrics entry only.
DEFAULT_CHARS = "".join(chr(c) for c in range(0x20, 0x7F))

#Reference glyph used to measure advances. Any glyph with ink works; advance(c) comes out as
#maxx("RcR") - maxx("RR"), which stays correct for glyphs that have no ink of their own.
REF_CHAR = "R"

WORK_COL_NAME = "__glyph_work"


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    p = argparse.ArgumentParser(prog="blender_glyph_meshes.py")
    p.add_argument("--text-object", default="text",
                   help="FONT object to copy font settings from (default: text)")
    p.add_argument("--collection", default="Glyphs",
                   help="collection to put the glyph meshes in (default: Glyphs)")
    p.add_argument("--chars", default=DEFAULT_CHARS,
                   help="characters to generate (default: printable ASCII 0x20-0x7E)")
    p.add_argument("--extrude", type=float, default=0.05,
                   help="text extrude; total depth is twice this (default: 0.05, matching text_all)")
    p.add_argument("--target-tris", type=int, default=144,
                   help="per-glyph triangle budget. The ratio becomes target/tris_full, so a "
                        "straight-edged glyph already under budget is left alone and every glyph "
                        "lands at roughly the same density. 0 falls back to a flat --ratio. "
                        "144 is what a flat 0.125 gives a typical curved glyph (default: 144)")
    p.add_argument("--ratio", type=float, default=0.125,
                   help="flat Decimate collapse ratio, as text_all uses. Only applied when "
                        "--target-tris is 0 (default: 0.125)")
    p.add_argument("--min-tris", type=int, default=12,
                   help="floor for the flat-ratio mode, so a period does not collapse to one "
                        "triangle; 0 disables (default: 12)")
    p.add_argument("--z-mode", choices=("base", "center"), default="base",
                   help="where the extrusion sits on the depth axis. base: [0, 2*extrude], like "
                        "text_all. center: [-extrude, extrude]")
    p.add_argument("--no-stand", dest="stand", action="store_false",
                   help="leave the glyphs lying in Blender's XY plane the way a text object makes "
                        "them. The default stands them up, which is what makes them arrive "
                        "upright after a +Y-up glTF export - see stand_glyph()")
    p.add_argument("--save", default="",
                   help="path to save the .blend to; empty means save over the opened file")
    p.add_argument("--no-save", action="store_true", help="do not save the .blend at all")
    p.add_argument("--json", default="",
                   help="metrics output path (default: <blend dir>/<blend name>_glyphs.json)")
    return p.parse_args(argv)


def safe_suffix(ch):
    """A readable name suffix for alphanumerics; everything else is identified by codepoint only."""
    return "_" + ch if ch.isalnum() and ch.isascii() else ""


def glyph_name(ch):
    #sscanf(name, "glyph_%4x", &cp) parses every one of these, suffix or not.
    return "glyph_%04X%s" % (ord(ch), safe_suffix(ch))


def stand_glyph(mesh):
    """Rotate a glyph +90 degrees about X, in place.

    A Blender text object lies in the XY plane, because Blender is Z-up and text is drawn on the
    ground. glTF is Y-up, and the exporter's conversion is gltf(x, y, z) = blender(x, z, -y) - so
    an unrotated glyph arrives in the engine with its vertical axis on -Z, lying flat like a floor
    decal. Putting the glyph's vertical on Blender +Z first is what makes it come out on glTF +Y.

    Afterwards, in Blender: x right, z up, -y the extrusion direction.
    After a +Y-up export:   x right, y up, +z the extrusion direction (facing the default camera).
    """
    for v in mesh.vertices:
        x, y, z = v.co
        v.co = (x, -z, y)


def to_gltf(p):
    """Blender coordinates to the glTF coordinates a +Y-up export writes."""
    return (p[0], p[2], -p[1])


def gltf_bounds(bmin, bmax):
    """glTF-space (min, max) from Blender-space bounds; the mapping negates and swaps axes."""
    lo = to_gltf(bmin)
    hi = to_gltf(bmax)
    return ([min(lo[i], hi[i]) for i in range(3)],
            [max(lo[i], hi[i]) for i in range(3)])


def bounds(mesh):
    """Local-space (min, max) of a mesh, or None when it has no geometry."""
    if not mesh.vertices:
        return None
    co = [v.co for v in mesh.vertices]
    return ([min(c[i] for c in co) for i in range(3)],
            [max(c[i] for c in co) for i in range(3)])


class GlyphBuilder:
    def __init__(self, args):
        self.args = args
        src = bpy.data.objects.get(args.text_object)
        if src is None or src.type != 'FONT':
            raise SystemExit("no FONT object named %r in this .blend" % args.text_object)
        self.src_data = src.data

        #Temp objects still have to live in the scene to be evaluated by the depsgraph.
        self.work_col = bpy.data.collections.new(WORK_COL_NAME)
        bpy.context.scene.collection.children.link(self.work_col)

    #--- conversion helpers -------------------------------------------------------------------

    def make_font_obj(self, body):
        data = self.src_data.copy()
        data.body = body
        data.extrude = self.args.extrude
        data.align_x = 'LEFT'
        data.align_y = 'TOP_BASELINE'
        obj = bpy.data.objects.new("__glyph_tmp", data)
        self.work_col.objects.link(obj)
        return obj

    def to_mesh(self, obj):
        """Evaluate a text object into a real mesh datablock (no operators, headless-safe)."""
        bpy.context.view_layer.update()
        dg = bpy.context.evaluated_depsgraph_get()
        return bpy.data.meshes.new_from_object(obj.evaluated_get(dg))

    def drop_font_obj(self, obj):
        data = obj.data
        bpy.data.objects.remove(obj, do_unlink=True)
        bpy.data.curves.remove(data)

    def measure(self, body):
        """Bounds of a string's geometry, without keeping anything behind."""
        obj = self.make_font_obj(body)
        mesh = self.to_mesh(obj)
        bb = bounds(mesh)
        bpy.data.meshes.remove(mesh)
        self.drop_font_obj(obj)
        return bb

    #--- font-level metrics -------------------------------------------------------------------

    def measure_font(self):
        """Advance baseline and line height, measured rather than assumed."""
        rr = self.measure(REF_CHAR * 2)
        if rr is None:
            raise SystemExit("reference char %r has no geometry in this font" % REF_CHAR)
        self.ref_maxx = rr[1][0]

        #Two lines of the reference char: the baseline gap between them is the line height.
        two = self.measure(REF_CHAR + "\n" + REF_CHAR)
        single = self.measure(REF_CHAR)
        line_height = None
        if two and single:
            #The top line sits at y=0, so the lower line's floor is one line height below its own.
            line_height = single[0][1] - two[0][1]
        return line_height

    def advance(self, ch):
        """advance(c) = maxx(RcR) - maxx(RR). Works for glyphs with no ink, e.g. space."""
        bb = self.measure(REF_CHAR + ch + REF_CHAR)
        if bb is None:
            return None
        return bb[1][0] - self.ref_maxx

    #--- the glyphs ---------------------------------------------------------------------------

    def clear_previous(self):
        col = bpy.data.collections.get(self.args.collection)
        if col is None:
            return 0
        removed = 0
        for obj in list(col.objects):
            mesh = obj.data if obj.type == 'MESH' else None
            bpy.data.objects.remove(obj, do_unlink=True)
            if mesh is not None and mesh.users == 0:
                bpy.data.meshes.remove(mesh)
            removed += 1
        bpy.data.collections.remove(col)
        return removed

    def decimate_ratio(self, tris):
        """How hard to decimate a glyph whose undecimated triangle count is `tris`.

        The flat 0.125 that text_all uses is only right for curve-tessellated glyphs. A glyph made
        of straight strokes ('A', 'E', 'L', '1') already has no redundant triangles, and collapsing
        it bends the strokes. A budget leaves those alone and only cuts the ones with detail to
        spare, which also evens out the density across the set.
        """
        args = self.args
        if tris <= 0:
            return 1.0
        if args.target_tris > 0:
            return min(1.0, args.target_tris / tris)
        ratio = args.ratio
        if args.min_tris > 0:
            ratio = max(ratio, args.min_tris / tris)
        return min(1.0, ratio)

    def build_glyph(self, ch):
        src = self.make_font_obj(ch)
        mesh = self.to_mesh(src)
        self.drop_font_obj(src)

        if not mesh.vertices:
            bpy.data.meshes.remove(mesh)
            return None, 0, 0.0

        #Blender extrudes symmetrically about z=0; lift it to match text_all if asked.
        if self.args.z_mode == "base":
            for v in mesh.vertices:
                v.co.z += self.args.extrude

        if self.args.stand:
            stand_glyph(mesh)

        mesh.name = glyph_name(ch)
        obj = bpy.data.objects.new(glyph_name(ch), mesh)
        obj["glyph"] = ch
        obj["codepoint"] = ord(ch)
        self.out_col.objects.link(obj)

        tris = sum(len(p.vertices) - 2 for p in mesh.polygons)
        ratio = self.decimate_ratio(tris)

        if ratio < 1.0:
            mod = obj.modifiers.new("Decimate", 'DECIMATE')
            mod.decimate_type = 'COLLAPSE'
            mod.ratio = ratio

        return obj, tris, ratio

    def run(self):
        args = self.args
        dropped = self.clear_previous()
        if dropped:
            print("removed %d object(s) from a previous run" % dropped)

        self.out_col = bpy.data.collections.new(args.collection)
        bpy.context.scene.collection.children.link(self.out_col)

        line_height = self.measure_font()

        glyphs = {}
        blank = []
        for ch in args.chars:
            adv = self.advance(ch)
            obj, tris, ratio = self.build_glyph(ch)

            entry = {"char": ch, "advance": round(adv, 6) if adv is not None else None}
            if obj is None:
                blank.append(ch)
                entry["mesh"] = None
            else:
                bb = bounds(obj.data)
                if args.stand:
                    bb = gltf_bounds(bb[0], bb[1])
                entry.update({
                    "mesh": obj.name,
                    "min": [round(c, 6) for c in bb[0]],
                    "max": [round(c, 6) for c in bb[1]],
                    "tris_full": tris,
                    "decimate_ratio": round(ratio, 6),
                })
            glyphs[str(ord(ch))] = entry

        #Post-decimate counts need a depsgraph evaluation of the modifier stack.
        bpy.context.view_layer.update()
        dg = bpy.context.evaluated_depsgraph_get()
        total_tris = 0
        for entry in glyphs.values():
            if not entry["mesh"]:
                continue
            ev = bpy.data.objects[entry["mesh"]].evaluated_get(dg).data
            tris = sum(len(p.vertices) - 2 for p in ev.polygons)
            entry["tris"] = tris
            total_tris += tris

        advances = sorted({round(e["advance"], 5) for e in glyphs.values()
                           if e["advance"] is not None})

        meta = {
            "font": self.src_data.font.name if self.src_data.font else None,
            "font_file": self.src_data.font.filepath if self.src_data.font else None,
            "size": self.src_data.size,
            "extrude": args.extrude,
            "depth": args.extrude * 2.0,
            "z_mode": args.z_mode,
            #Which frame every min/max below is expressed in, so the C++ side is not left guessing.
            "space": ("gltf: +x right, +y up, +z depth, as a +Y-up export writes it"
                      if args.stand else
                      "blender: +x right, +y up, +z depth, unrotated text-object plane"),
            "line_height": round(line_height, 6) if line_height else None,
            "monospace": len(advances) == 1,
            "advance": advances[0] if len(advances) == 1 else None,
            "collection": args.collection,
            "name_scheme": "glyph_<4 hex digits of codepoint>[_<char if alphanumeric>]",
            "glyphs": glyphs,
        }
        return meta, blank, total_tris, advances

    def cleanup(self):
        bpy.data.collections.remove(self.work_col)


def main():
    args = parse_args()
    b = GlyphBuilder(args)
    try:
        meta, blank, total_tris, advances = b.run()
    finally:
        b.cleanup()

    blend = bpy.data.filepath
    json_path = args.json or os.path.splitext(blend)[0] + "_glyphs.json"
    with open(json_path, "w", encoding="utf-8") as f:
        json.dump(meta, f, indent=2, ensure_ascii=False)

    n = sum(1 for e in meta["glyphs"].values() if e["mesh"])
    print("\n=== %d glyph meshes in collection %r, %d tris total ===" % (n, args.collection, total_tris))
    print("font        : %s (size %g, depth %g)" % (meta["font"], meta["size"], meta["depth"]))
    print("line height : %s" % meta["line_height"])
    print("advances    : %s%s" % (advances, "  (monospace)" if meta["monospace"] else ""))
    if blank:
        print("no geometry : %s" % " ".join(repr(c) for c in blank))

    built = [e for e in meta["glyphs"].values() if e["mesh"]]
    if built:
        counts = sorted(e["tris"] for e in built)
        print("tris/glyph  : min %d, median %d, max %d"
              % (counts[0], counts[len(counts) // 2], counts[-1]))
        untouched = [e["char"] for e in built if e["decimate_ratio"] >= 1.0]
        if untouched:
            print("undecimated : %s  (already under the %d-tri budget)"
                  % (" ".join(untouched), args.target_tris))
    print("metrics     : %s" % json_path)

    if not args.no_save:
        target = args.save or blend
        bpy.ops.wm.save_as_mainfile(filepath=target)
        print("saved       : %s" % target)


if __name__ == "__main__":
    main()
