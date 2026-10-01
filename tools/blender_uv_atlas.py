#!/usr/bin/env python
"""Step 1 of the prop atlas: measure every UV island of the exported props, and say which carry detail.

    "C:/Program Files/Blender Foundation/Blender 4.5/blender.exe" -b ../elfarcher_withprops.blend -P tools/blender_uv_atlas.py -- --out <dir>
    ... -- --mode clear                      (remove everything analyse added - also needed in the open file)
    ... -- --flat 14 --detail 10             (the two thresholds, in 0-255 sRGB steps)

Or paste it into Blender's Scripting tab and Run Script: with no `--` arguments it uses DEFAULTS below,
so to clear in the open file set DEFAULTS["mode"] = "clear" there.

WHY: the props arrive as Tripo exports, each with its own 4096^2 texture, and most of their islands
are one colour painted over thousands of texels - a tree's leaves are green, however big their island.
Moving UVs alone cannot pack that better, because the target's rings have to move WITH its islands, so
the eventual atlas is a re-bake. This step decides how much space each island deserves before anything
is changed:

    flat      one colour (after a light blur, so generator grain is not mistaken for detail) - gets a
              palette swatch, and its UVs collapse into it
    smooth    a gradient, but no fine detail - keeps its layout at a quarter of the resolution
    detailed  real detail, like the target's rings - keeps its resolution

Measurements are taken INSIDE each island only (a normalised blur, weighted by the island's own mask),
so the atlas's neighbouring islands and gutter colours never leak into a verdict.

WHAT IT ADDS - all additive, all named, all removed again by --mode clear:
    colour attribute `atlas_class` on every analysed mesh, made the active one - Solid shading ->
        Color -> Attribute shows flat faces in their measured colour, smooth ones cyan, detailed magenta
        (the previously active colour attribute is remembered and restored by clear);
    images `atlas_preview:<texture>` - each source texture with unused texels dimmed, flat islands
        striped in their mean colour, smooth islands striped cyan, detailed islands untouched;
    text block `atlas_report` - the table, and the atlas size it all needs.
With --out it also writes the previews as PNGs and the report as atlas_report.txt. The .blend is never
saved; in the open file, the attribute and images go away with clear (or by not saving). The previews
are packed, so saving the file keeps them - about 3 MB each - until clear.

CLEAR IT BEFORE EXPORTING: a glTF export set to write the ACTIVE colour attribute would carry
`atlas_class` into the game as COLOR_0.
"""

import argparse
import math
import os
import sys
import time

import bpy
import mathutils
import numpy as np

DEFAULTS = {
    "mode": "analyse",         # or "clear"
    "collection": "Export",
    "skip": "Character",       # comma-separated collection names left out, children included
    "out": "",                 # a directory for PNGs + report; empty writes nothing to disk
    "flat": 14.0,              # 95th-percentile distance from the island's mean colour, below which it is flat
    "detail": 10.0,            # 95th-percentile high-frequency residual, below which it is only smooth
    "blur": 2,                 # radius of the grain blur, texels (run twice, so roughly a gaussian)
    "blur_big": 8,             # radius of the blur that separates gradients from detail
    "preview": 2048,           # preview image size; PNGs on disk use min(this, 1024)
    "min_texels": 16,          # an island covering fewer texels than this cannot carry detail: flat
}

ATTR = "atlas_class"
PREV_ACTIVE = "atlas_prev_active_color"
PREVIEW_PREFIX = "atlas_preview:"
REPORT = "atlas_report"

SMOOTH_TINT = np.array([0.0, 1.0, 1.0])
DETAIL_TINT = np.array([1.0, 0.0, 1.0])
SMOOTH_SCALE = 0.25            # linear scale a smooth island keeps; its texels cost SMOOTH_SCALE^2
SWATCH_CELL = 8                # texels per swatch side, padding included - enough to survive the mips


def parse_args():
    opts = dict(DEFAULTS)
    if "--" not in sys.argv:
        return argparse.Namespace(**opts)
    p = argparse.ArgumentParser()
    for k, v in DEFAULTS.items():
        p.add_argument("--" + k.replace("_", "-"), dest=k, type=type(v), default=v)
    return p.parse_args(sys.argv[sys.argv.index("--") + 1:])


# ---------------------------------------------------------------- scene walking

def collect(col, skip, out):
    if col.name in skip:
        return
    for o in col.objects:
        if o.type == 'MESH' and o not in out:
            out.append(o)
    for c in col.children:
        collect(c, skip, out)


def base_color_image(mat):
    """The image node feeding Base Color, searched back through any mix or adjust nodes on the way."""
    if not mat or not mat.use_nodes:
        return None
    nodes = mat.node_tree.nodes
    for n in nodes:
        if n.type == 'BSDF_PRINCIPLED':
            stack = [l.from_node for l in n.inputs['Base Color'].links]
            seen = set()
            while stack:
                m = stack.pop()
                if m.name in seen:
                    continue
                seen.add(m.name)
                if m.type == 'TEX_IMAGE' and m.image:
                    return m
                for i in m.inputs:
                    stack.extend(l.from_node for l in i.links)
    # No principled chain to follow: the first image in the material is the best guess.
    for n in nodes:
        if n.type == 'TEX_IMAGE' and n.image:
            return n
    return None


def uv_source(tex):
    """(uv map name or None for the render-active one, 3x3 transform or None) for an image node.

    Tripo's FBX import puts a Mapping node in front of every texture. It is normally identity, but a
    non-identity one changes where the texture is actually sampled, so it is applied rather than assumed."""
    inp = tex.inputs['Vector']
    if not inp.is_linked:
        return None, None
    n = inp.links[0].from_node
    xf = None
    if n.type == 'MAPPING':
        def val(name):
            i = n.inputs.get(name)
            return mathutils.Vector(i.default_value) if i is not None else None
        loc, rot, scl = val('Location'), val('Rotation'), val('Scale')
        if n.vector_type in ('POINT', 'TEXTURE') and loc is not None:
            m = (mathutils.Matrix.Translation(loc) @ mathutils.Euler(rot).to_matrix().to_4x4()
                 @ mathutils.Matrix.Diagonal(scl.to_4d()))
            if n.vector_type == 'TEXTURE':
                m = m.inverted()
            if not all(abs(m[r][c] - (1.0 if r == c else 0.0)) < 1e-6 for r in range(4) for c in range(4)):
                # Only the UV plane matters: rows/columns 0,1 and the translation column.
                xf = np.array([[m[0][0], m[0][1], m[0][3]],
                               [m[1][0], m[1][1], m[1][3]]])
        n = n.inputs['Vector'].links[0].from_node if n.inputs['Vector'].is_linked else None
    if n is not None and n.type == 'UVMAP' and n.uv_map:
        return n.uv_map, xf
    return None, xf


# ---------------------------------------------------------------- islands

class Island:
    __slots__ = ("obj", "image", "faces", "tri_uv", "area3d", "texels", "cls", "mean", "p95", "hf95", "note")


def islands_of(obj, notes):
    """Split one mesh into UV islands per source image. Two faces are one island when they share a
    vertex AND its UV there - the same rule Blender's Select Linked uses in the UV editor."""
    me = obj.data
    npoly, nl = len(me.polygons), len(me.loops)
    if npoly == 0 or not me.uv_layers:
        notes.append("%s: no faces or no UV map, skipped" % obj.name)
        return []

    slots = []
    for s in obj.material_slots:
        tex = base_color_image(s.material)
        if tex is None:
            slots.append(None)
            continue
        uvname, xf = uv_source(tex)
        if uvname is not None and uvname not in me.uv_layers:
            notes.append("%s: material %s wants UV map %s, which the mesh lacks; using the active one"
                         % (obj.name, s.material.name, uvname))
            uvname = None
        if uvname is None:
            uvname = next((u.name for u in me.uv_layers if u.active_render), me.uv_layers[0].name)
        if xf is not None:
            notes.append("%s: material %s has a non-identity Mapping node - applied" % (obj.name, s.material.name))
        slots.append((tex.image, uvname, xf))

    ls = np.empty(npoly, np.int64); me.polygons.foreach_get('loop_start', ls)
    lt = np.empty(npoly, np.int64); me.polygons.foreach_get('loop_total', lt)
    mi = np.empty(npoly, np.int64); me.polygons.foreach_get('material_index', mi)
    lv = np.empty(nl, np.int64); me.loops.foreach_get('vertex_index', lv)
    face_of_loop = np.empty(nl, np.int64)
    for f in range(npoly):
        face_of_loop[ls[f]:ls[f] + lt[f]] = f

    layers = {}
    for s in slots:
        if s is not None and s[1] not in layers:
            a = np.empty(nl * 2, np.float64)
            me.uv_layers[s[1]].data.foreach_get('uv', a)
            layers[s[1]] = a.reshape(nl, 2)

    # Per loop: its slot's UV, through its slot's Mapping; and which image the face samples.
    images = []
    face_img = np.full(npoly, -1, np.int64)
    uv = np.zeros((nl, 2), np.float64)
    for si, s in enumerate(slots):
        if s is None:
            continue
        faces = np.nonzero(np.minimum(mi, len(slots) - 1) == si)[0]
        if faces.size == 0:
            continue
        if s[0] not in images:
            images.append(s[0])
        face_img[faces] = images.index(s[0])
        loops = np.nonzero(np.isin(face_of_loop, faces))[0]
        u = layers[s[1]][loops]
        if s[2] is not None:
            u = u @ s[2][:, :2].T + s[2][:, 2]
        uv[loops] = u
    if (face_img < 0).any():
        notes.append("%s: %d faces have no base colour image, left out" % (obj.name, int((face_img < 0).sum())))

    key = np.stack([lv, np.round(uv[:, 0] * 65536).astype(np.int64),
                    np.round(uv[:, 1] * 65536).astype(np.int64), face_img[face_of_loop]], axis=1)
    _, kid = np.unique(key, axis=0, return_inverse=True)
    kid = kid.ravel()

    parent = list(range(npoly))

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    first = np.full(kid.max() + 1, -1, np.int64)
    for l in range(nl):
        f = int(face_of_loop[l])
        k = kid[l]
        if first[k] < 0:
            first[k] = f
        else:
            a, b = find(f), find(int(first[k]))
            if a != b:
                parent[a] = b
    root = np.array([find(f) for f in range(npoly)])

    me.calc_loop_triangles()
    nt = len(me.loop_triangles)
    tl = np.empty(nt * 3, np.int64); me.loop_triangles.foreach_get('loops', tl); tl = tl.reshape(nt, 3)
    tp = np.empty(nt, np.int64); me.loop_triangles.foreach_get('polygon_index', tp)

    co = np.empty(len(me.vertices) * 3, np.float64); me.vertices.foreach_get('co', co)
    mw = np.array(obj.matrix_world)
    co = co.reshape(-1, 3) @ mw[:3, :3].T + mw[:3, 3]
    p = co[lv[tl]]
    tri_area = 0.5 * np.linalg.norm(np.cross(p[:, 1] - p[:, 0], p[:, 2] - p[:, 0]), axis=1)

    out = []
    tri_root = root[tp]
    order = np.argsort(tri_root, kind='stable')
    bounds = np.flatnonzero(np.diff(tri_root[order])) + 1
    for group in np.split(order, bounds):
        r = tri_root[group[0]]
        if face_img[r] < 0:
            continue
        isl = Island()
        isl.obj = obj
        isl.image = images[face_img[r]]
        isl.faces = np.nonzero(root == r)[0]
        isl.tri_uv = uv[tl[group]]
        isl.area3d = float(tri_area[group].sum())
        isl.note = ""
        out.append(isl)
    return out


# ---------------------------------------------------------------- measuring

def box_sum(a, r):
    """Sum over a (2r+1)^2 window, zero outside the array. Two cumulative sums, so any radius costs the same."""
    k = 2 * r + 1
    p = np.pad(a, [(r, r), (r, r)] + [(0, 0)] * (a.ndim - 2))
    c = np.cumsum(p, axis=0)
    c = np.concatenate([np.zeros((1,) + c.shape[1:]), c], axis=0)
    p = c[k:] - c[:-k]
    c = np.cumsum(p, axis=1)
    c = np.concatenate([np.zeros((c.shape[0], 1) + c.shape[2:]), c], axis=1)
    return c[:, k:] - c[:, :-k]


def island_blur(img, mask, r):
    """Blur that only ever averages texels of the island itself: outside texels weigh nothing, so a
    neighbouring island's colour cannot pull an edge off its mean. Two passes of a box ~ a gaussian."""
    m = mask.astype(np.float64)
    w = np.maximum(box_sum(m, r), 1e-9)[..., None]
    out = img
    for _ in range(2):
        out = box_sum(out * m[..., None], r) / w
    return out


def raster(tri_px, x0, y0, w, h):
    """Mask of the texels whose centres fall inside any triangle. Centres are at integer coordinates
    (tri_px is uv * size - 0.5). A triangle too thin to cover a centre still claims the texel under
    its centroid, so no island ends up with no texels to judge."""
    mask = np.zeros((h, w), bool)
    for t in tri_px:
        xs, ys = t[:, 0] - x0, t[:, 1] - y0
        cx, cy = int(round(xs.mean())), int(round(ys.mean()))
        if 0 <= cx < w and 0 <= cy < h:
            mask[cy, cx] = True
        ix0, ix1 = max(int(math.ceil(xs.min())), 0), min(int(math.floor(xs.max())), w - 1)
        iy0, iy1 = max(int(math.ceil(ys.min())), 0), min(int(math.floor(ys.max())), h - 1)
        if ix1 < ix0 or iy1 < iy0:
            continue
        gx, gy = np.meshgrid(np.arange(ix0, ix1 + 1), np.arange(iy0, iy1 + 1))
        (ax, ay), (bx, by), (qx, qy) = zip(xs, ys)
        e0 = (bx - ax) * (gy - ay) - (by - ay) * (gx - ax)
        e1 = (qx - bx) * (gy - by) - (qy - by) * (gx - bx)
        e2 = (ax - qx) * (gy - qy) - (ay - qy) * (gx - qx)
        inside = ((e0 >= 0) & (e1 >= 0) & (e2 >= 0)) | ((e0 <= 0) & (e1 <= 0) & (e2 <= 0))
        mask[iy0:iy1 + 1, ix0:ix1 + 1] |= inside
    return mask


def measure_image(image, islands, args, preview_size, stats):
    W, H = image.size
    px = np.empty(W * H * 4, np.float32)
    image.pixels.foreach_get(px)        # byte images come back as raw sRGB / 255: what the eye compares
    img = px.reshape(H, W, 4)[..., :3]
    del px

    cov = {c: np.zeros((H, W), bool) for c in ("any", "flat", "smooth", "detailed")}
    prev = img * 0.2
    stripe_w = 4 * max(1, W // preview_size)

    for isl in islands:
        t = isl.tri_uv.copy()
        t[..., 0] = t[..., 0] * W - 0.5
        t[..., 1] = t[..., 1] * H - 0.5
        x0, x1 = int(math.floor(t[..., 0].min())) - 1, int(math.ceil(t[..., 0].max())) + 1
        y0, y1 = int(math.floor(t[..., 1].min())) - 1, int(math.ceil(t[..., 1].max())) + 1
        w, h = x1 - x0 + 1, y1 - y0 + 1
        if w > W or h > H:
            # The island repeats the texture across itself: it tiles, so it cannot be moved into an
            # atlas cell at all without re-baking a tile of its own. Kept whole, and said so.
            isl.cls, isl.texels, isl.mean, isl.p95, isl.hf95 = "detailed", W * H, np.zeros(3), 0.0, 0.0
            isl.note = "tiles (UVs span more than the texture)"
            continue
        rows = (y0 + np.arange(h)) % H
        cols = (x0 + np.arange(w)) % W
        mask = raster(t, x0, y0, w, h)
        n = int(mask.sum())
        crop = img[np.ix_(rows, cols)].astype(np.float64)

        small = island_blur(crop, mask, args.blur)
        vals = small[mask]
        mean = vals.mean(axis=0)
        p95 = float(np.percentile(np.linalg.norm(vals - mean, axis=1), 95)) * 255
        big = island_blur(crop, mask, args.blur_big)
        hf95 = float(np.percentile(np.linalg.norm(vals - big[mask], axis=1), 95)) * 255

        if n < args.min_texels or p95 < args.flat:
            cls = "flat"
        elif hf95 < args.detail:
            cls = "smooth"
        else:
            cls = "detailed"
        isl.cls, isl.texels, isl.mean, isl.p95, isl.hf95 = cls, n, mean, p95, hf95

        ix = np.ix_(rows, cols)
        cov["any"][ix] |= mask
        cov[cls][ix] |= mask
        stripe = (((rows[:, None] + cols[None, :]) // stripe_w) % 2).astype(bool)
        pc = prev[ix]
        if cls == "flat":
            val = np.where(stripe[..., None], mean * 0.6, mean)
        elif cls == "smooth":
            val = np.where(stripe[..., None], crop * 0.5 + SMOOTH_TINT * 0.5, crop)
        else:
            val = crop
        prev[ix] = np.where(mask[..., None], val, pc)

    stats[image.name] = {k: int(v.sum()) for k, v in cov.items()}
    stats[image.name]["size"] = W * H
    stats[image.name]["detailed_or_smooth"] = int((cov["detailed"] | cov["smooth"]).sum())
    # "smooth" texels that some detailed island also covers are paid for already, at full resolution.
    stats[image.name]["smooth_only"] = int((cov["smooth"] & ~cov["detailed"]).sum())
    return prev


# ---------------------------------------------------------------- outputs

def write_preview(image, prev, args):
    H, W = prev.shape[:2]
    f = max(1, W // args.preview)
    small = prev[:H - H % f, :W - W % f].reshape(H // f, f, W // f, f, 3).mean(axis=(1, 3))
    name = (PREVIEW_PREFIX + image.name)[:63]
    old = bpy.data.images.get(name)
    if old:
        bpy.data.images.remove(old)
    h, w = small.shape[:2]
    pim = bpy.data.images.new(name, w, h, alpha=False)
    rgba = np.concatenate([small, np.ones((h, w, 1))], axis=2).astype(np.float32)
    pim.pixels.foreach_set(rgba.ravel())
    if args.out:
        disk = min(1024, w)
        if disk != w:
            g = w // disk
            rgba = rgba.reshape(h // g, g, w // g, g, 4).mean(axis=(1, 3)).astype(np.float32)
            tmp = bpy.data.images.new(name + "_disk", disk, disk, alpha=False)
            tmp.pixels.foreach_set(rgba.ravel())
        else:
            tmp = pim
        safe = "".join(ch if ch.isalnum() or ch in "._-" else "_" for ch in image.name)
        tmp.filepath_raw = os.path.join(args.out, "preview_%s.png" % safe)
        tmp.file_format = 'PNG'
        tmp.save()
        if tmp is not pim:
            bpy.data.images.remove(tmp)
    # foreach_set fills the buffer but does not tell the editors: without update() the Image Editor
    # keeps drawing the blank texture it made at new(). And a generated image is regenerated blank
    # when the file is saved and reopened, so it is packed to survive that - and given a fake user,
    # because saving drops any image nothing uses, which is every preview not open in an editor.
    # clear removes them again.
    pim.update()
    pim.pack()
    pim.use_fake_user = True


def write_attribute(obj, islands):
    """Face-corner colours: flat faces in their island's mean colour, smooth cyan, detailed magenta,
    faces with no texture grey."""
    me = obj.data
    npoly = len(me.polygons)
    face_col = np.full((npoly, 3), 0.3)
    for isl in islands:
        c = isl.mean if isl.cls == "flat" else SMOOTH_TINT if isl.cls == "smooth" else DETAIL_TINT
        face_col[isl.faces] = c
    ls = np.empty(npoly, np.int64); me.polygons.foreach_get('loop_start', ls)
    lt = np.empty(npoly, np.int64); me.polygons.foreach_get('loop_total', lt)
    loop_col = np.empty((len(me.loops), 4), np.float32)
    loop_col[:, 3] = 1.0
    for f in range(npoly):
        loop_col[ls[f]:ls[f] + lt[f], :3] = face_col[f]

    ca = me.color_attributes
    if ATTR in ca:
        ca.remove(ca[ATTR])
    if PREV_ACTIVE not in me:
        me[PREV_ACTIVE] = ca.active_color.name if ca.active_color else ""
    a = ca.new(ATTR, 'BYTE_COLOR', 'CORNER')
    a.data.foreach_set('color_srgb', loop_col.ravel())
    ca.active_color = a


def swatches(colours, tol):
    """Greedy grouping of flat colours: within tol (0-255, per channel) of a swatch's first colour -> that swatch."""
    out = []
    for c in sorted(colours, key=lambda c: tuple(c)):
        if not any(np.abs(c - s).max() <= tol for s in out):
            out.append(c)
    return len(out)


def fit(texels):
    for side in (512, 1024, 2048, 4096, 8192):
        if texels <= side * side:
            return "%d^2 (%.0f%% full)" % (side, 100.0 * texels / (side * side))
    return "more than 8192^2"


def report(objs, islands, stats, notes, args, secs):
    L = []
    L.append("PROP ATLAS ANALYSIS  (%s, skipping %s)  thresholds: flat p95 < %.1f, smooth hf95 < %.1f, blur %d/%d"
             % (args.collection, args.skip, args.flat, args.detail, args.blur, args.blur_big))
    L.append("%d objects, %d islands, %.1f s" % (len(objs), len(islands), secs))
    L.append("")
    for im_name, s in stats.items():
        im = bpy.data.images[im_name]
        L.append("== %s  %dx%d  used %.1f%%  (flat %.1f%%, smooth %.1f%%, detailed %.1f%%)"
                 % (im_name, im.size[0], im.size[1], 100.0 * s["any"] / s["size"], 100.0 * s["flat"] / s["size"],
                    100.0 * s["smooth"] / s["size"], 100.0 * s["detailed"] / s["size"]))
        L.append("   %-28s %6s %5s %5s %5s %10s %9s" % ("object", "isl", "flat", "smth", "det", "det_texels", "texel/m"))
        for o in objs:
            mine = [i for i in islands if i.obj is o and i.image is im]
            if not mine:
                continue
            det = [i for i in mine if i.cls != "flat"]
            det_tex = sum(i.texels for i in mine if i.cls == "detailed")
            area = sum(i.area3d for i in det)
            dens = math.sqrt(sum(i.texels for i in det) / area) if area > 0 else 0.0
            L.append("   %-28s %6d %5d %5d %5d %10d %9.0f" % (
                o.name[:28], len(mine), sum(i.cls == "flat" for i in mine), sum(i.cls == "smooth" for i in mine),
                sum(i.cls == "detailed" for i in mine), det_tex, dens))
        L.append("")

    flat = [i for i in islands if i.cls == "flat"]
    cols = [i.mean * 255 for i in flat]
    L.append("FLAT ISLANDS: %d -> swatches at tolerance 3: %d, 6: %d, 10: %d"
             % (len(flat), swatches(cols, 3), swatches(cols, 6), swatches(cols, 10)))
    p = np.array([i.p95 for i in islands if not i.note]) if islands else np.zeros(1)
    hf = np.array([i.hf95 for i in islands if i.cls != "flat" and not i.note])
    L.append("p95 over all islands (flatness), percentiles 10/25/50/75/90: %s"
             % " ".join("%.1f" % v for v in np.percentile(p, [10, 25, 50, 75, 90])))
    if hf.size:
        L.append("hf95 over non-flat islands (detail), percentiles 10/25/50/75/90: %s"
                 % " ".join("%.1f" % v for v in np.percentile(hf, [10, 25, 50, 75, 90])))
    L.append("")

    # Budget. Union coverage per source texture, so stacked or mirrored islands are paid for once; the
    # packer's padding and gaps are covered by assuming 70% of the atlas ends up used.
    eff = 0.70
    det = sum(s["detailed"] for s in stats.values())
    smo = sum(s["smooth_only"] for s in stats.values())
    n_sw = swatches(cols, 6)
    sw = n_sw * SWATCH_CELL * SWATCH_CELL
    L.append("BUDGET  (packing assumed %.0f%% efficient, %d swatches of %dx%d)" % (eff * 100, n_sw, SWATCH_CELL, SWATCH_CELL))
    L.append("   today: %d textures of %s" % (len(stats), ", ".join(sorted({"%dx%d" % tuple(bpy.data.images[n].size) for n in stats}))))
    for scale in (1.0, 0.5, 0.25):
        tex = (det * scale * scale + smo * (SMOOTH_SCALE * scale) ** 2) / eff + sw
        L.append("   detail at %4.0f%% of source resolution: %9d texels -> %s" % (scale * 100, tex, fit(tex)))
    L.append("")

    tiled = [i for i in islands if i.note]
    if tiled:
        L.append("TILING ISLANDS (kept whole, cannot go into an atlas cell as they are):")
        for i in tiled:
            L.append("   %s on %s: %s" % (i.obj.name, i.image.name, i.note))
        L.append("")
    for im_name in stats:
        im = bpy.data.images[im_name]
        if not im.packed_file:
            notes.append("image %s is NOT packed; it loads from %s" % (im_name, im.filepath))
    if notes:
        L.append("NOTES:")
        L.extend("   " + n for n in notes)
        L.append("")
    L.append("Remove the attribute, previews and this text with --mode clear before exporting.")
    return "\n".join(L)


# ---------------------------------------------------------------- modes

def clear():
    n_attr = 0
    for me in bpy.data.meshes:
        ca = me.color_attributes
        if ATTR in ca:
            ca.remove(ca[ATTR])
            n_attr += 1
        if PREV_ACTIVE in me:
            prev = me[PREV_ACTIVE]
            if prev and prev in ca:
                ca.active_color = ca[prev]
            del me[PREV_ACTIVE]
    ims = [im for im in bpy.data.images if im.name.startswith(PREVIEW_PREFIX)]
    for im in ims:
        bpy.data.images.remove(im)
    t = bpy.data.texts.get(REPORT)
    if t:
        bpy.data.texts.remove(t)
    print("atlas: cleared %d attributes, %d previews%s" % (n_attr, len(ims), ", the report" if t else ""))


def analyse(args):
    t0 = time.time()
    col = bpy.data.collections.get(args.collection)
    if col is None:
        raise SystemExit("atlas: no collection %r" % args.collection)
    objs = []
    collect(col, {s.strip() for s in args.skip.split(",") if s.strip()}, objs)
    notes = []
    islands = []
    per_obj = {}
    for o in objs:
        per_obj[o] = islands_of(o, notes)
        islands.extend(per_obj[o])
    print("atlas: %d objects, %d islands (%.1f s)" % (len(objs), len(islands), time.time() - t0))

    if args.out:
        os.makedirs(args.out, exist_ok=True)
    stats = {}
    by_image = {}
    for isl in islands:
        by_image.setdefault(isl.image.name, []).append(isl)
    for name, isls in by_image.items():
        im = bpy.data.images[name]
        if im.size[0] == 0:
            notes.append("image %s did not load (missing file?) - its %d islands are unmeasured" % (name, len(isls)))
            for i in isls:
                i.cls, i.texels, i.mean, i.p95, i.hf95, i.note = "detailed", 0, np.zeros(3), 0.0, 0.0, "image missing"
            continue
        t1 = time.time()
        prev = measure_image(im, isls, args, args.preview, stats)
        write_preview(im, prev, args)
        del prev
        print("atlas: %s, %d islands (%.1f s)" % (name, len(isls), time.time() - t1))

    for o, isls in per_obj.items():
        if isls:
            write_attribute(o, isls)

    text = report(objs, islands, stats, notes, args, time.time() - t0)
    t = bpy.data.texts.get(REPORT) or bpy.data.texts.new(REPORT)
    t.clear()
    t.write(text)
    if args.out:
        with open(os.path.join(args.out, "atlas_report.txt"), "w") as f:
            f.write(text)
    print(text)


def main():
    args = parse_args()
    if args.mode == "clear":
        clear()
    else:
        analyse(args)


main()
