#!/usr/bin/env python
"""Give the spider a real alternating walk: two leg shape keys, LegsSwing and LegsLift.

    "C:/Program Files/Blender Foundation/Blender 4.5/blender.exe" -b ../elfarcher_withprops.blend -P tools/blender_spider_legs.py -- --out <file.blend>

or open it in Blender's Text Editor and Run Script - then it works on the open file and saves
nothing, so the result can be looked at (and undone) before saving.

WHY TWO KEYS, AND WHY THESE TWO
-------------------------------
The old `Walking` key moves both sides' legs alike, so whatever the game does with its factor the
legs cannot alternate. A spider walks on an ALTERNATING TETRAPOD: L1 R2 L3 R4 step together
(group A) while R1 L2 R3 L4 hold the ground (group B), then the two swap. Both new keys are
SYMMETRIC about the rest pose, a quarter-cycle apart:

    LegsSwing  +1: group A forward, group B back      -1: the reverse
    LegsLift   +1: group A raised, group B pressed    -1: the reverse

so the game drives them with sin and cos of one phase, and the rest pose (both 0) is the spider
standing still:

    swing = amount * sin(phase)        lift = amount * cos(phase)

Each foot then goes round an ellipse: raised while it swings forward, down while it sweeps back.
With two linear keys and the rest pose on the ground, the "down" half is a press BELOW rest by as
much as the lift (about 0.035 here, in a 0.92-wide model) - a clean flat stance needs a third
key (lift for each group separately). Both keys need negative values: their sliders run -1..+1.
`Walking` is kept, untouched and still first, so morph target 0 is what it was.

WHAT IT DOES TO THE MESH
------------------------
The Mirror modifier is APPLIED (shape keys kept): two sides that step out of phase cannot come
from one mirrored half. Each leg is a centre line, hip to tip, read off this mesh (the numbers in
LEGS); a vertex within OWN_RADIUS of a centre line belongs to that leg and is moved by rotating
about the hip (swing about the vertical; lift about the horizontal across the leg) and flexing back
about the knee (so the knee rises more than the tip, which is what reads as a step). The body never
moves. The front leg is a separate piece in this mesh and turns whole; the others are joined to the
body and fade in from the hip.

It also adds a preview action, `SpiderWalk` (24 frames, cyclic), on the spider's shape keys. The
glTF export will carry it as an animation; the game is meant to drive the keys itself and can
ignore it.

Rerunning it is safe: an applied Mirror is not applied twice and the two keys are rebuilt.
"""
import math
import sys

import bpy

OBJECT = 'spider'

# ------------------------------------------------------------------------------------------------
# The legs. Coordinates are the spider's OBJECT space (Z up, head toward -Y), for the +X side; the
# -X side is the same leg mirrored. Centre lines run hip first, tip last, from geodesic bands walked
# in from each tip.
# ------------------------------------------------------------------------------------------------
LEGS = {
    'L1': [(0.128, -0.030, -0.047), (0.174, -0.033, 0.024), (0.229, -0.080, 0.055), (0.256, -0.134, 0.045),
           (0.282, -0.180, 0.065), (0.290, -0.249, 0.037), (0.289, -0.288, 0.018), (0.277, -0.325, -0.010),
           (0.262, -0.362, -0.040), (0.239, -0.408, -0.097), (0.229, -0.420, -0.110)],
    'L2': [(0.095, 0.026, -0.070), (0.135, 0.026, -0.068), (0.187, 0.020, -0.056), (0.251, 0.039, -0.020),
           (0.282, 0.022, -0.022), (0.322, -0.006, -0.010), (0.350, -0.041, -0.014), (0.376, -0.078, -0.024),
           (0.400, -0.130, -0.060), (0.415, -0.180, -0.100), (0.419, -0.222, -0.145)],
    'L3': [(0.103, 0.052, -0.046), (0.127, 0.061, -0.034), (0.147, 0.066, -0.002), (0.165, 0.089, 0.022),
           (0.184, 0.098, 0.043), (0.222, 0.095, 0.054), (0.267, 0.104, 0.069), (0.314, 0.132, 0.065),
           (0.353, 0.137, 0.050), (0.392, 0.140, 0.019), (0.436, 0.129, -0.022), (0.453, 0.122, -0.050)],
    'L4': [(0.110, 0.050, -0.040), (0.160, 0.100, 0.020), (0.200, 0.140, 0.060), (0.249, 0.163, 0.097),
           (0.257, 0.199, 0.096), (0.260, 0.242, 0.085), (0.278, 0.270, 0.083), (0.273, 0.310, 0.058),
           (0.281, 0.334, 0.045), (0.277, 0.373, 0.011), (0.272, 0.398, -0.016), (0.263, 0.413, -0.053),
           (0.259, 0.443, -0.079)],
}
KNEE = {'L1': 4, 'L2': 5, 'L3': 6, 'L4': 6}     # index of the knee in each centre line
GROUP_A_PLUS_X = {'L1', 'L3'}                   # +X legs in group A; on -X it is L2 and L4
RIGID_LEG = 'L1'                                # a separate piece: turns whole, no fade at the hip
BODY_POINT = (0.056, -0.092, -0.018)            # inside the cephalothorax, a piece that never moves

OWN_RADIUS = 0.05       # a vertex further than this from every centre line is body
# Where each leg hinges (index into its centre line) and the arclength past the hinge over which
# its motion fades in. The back two legs are FUSED to the abdomen's underside near their roots in
# this mesh, so they hinge further out - at the hip they would drag the abdomen with them.
HIP_INDEX = {'L1': 0, 'L2': 0, 'L3': 0, 'L4': 2}
HIP_BLEND = {'L1': (0.015, 0.09), 'L2': (0.015, 0.09), 'L3': (0.09, 0.17), 'L4': (0.0, 0.08)}
KNEE_BLEND = 0.035      # half-width of the knee's fade, either side of the knee

# Fore/aft swing about the hip, each way. Less on the front and back pairs: they point along the
# body, so a swing is mostly sideways for them, and at the middle pairs' angle they cross them.
SWING_DEG = {'L1': 9.0, 'L2': 14.0, 'L3': 14.0, 'L4': 10.0}
LIFT_HIP_DEG = 16.0     # femur raised about the hip...
LIFT_KNEE_DEG = 20.0    # ...and the tibia flexed back down about the knee, so the tip rises less

PREVIEW_ACTION = 'SpiderWalk'
PREVIEW_FRAMES = 24     # one full cycle

UP = (0.0, 0.0, 1.0)

def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def mul(a, s): return (a[0] * s, a[1] * s, a[2] * s)
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def norm(a): return mul(a, 1.0 / math.sqrt(dot(a, a)))

def rotate(p, axis, ang):
    c, s = math.cos(ang), math.sin(ang)
    return add(add(mul(p, c), mul(cross(axis, p), s)), mul(axis, dot(axis, p) * (1 - c)))

def smoothstep(e0, e1, x):
    t = min(1.0, max(0.0, (x - e0) / (e1 - e0)))
    return t * t * (3 - 2 * t)

def closest_on_line(pts, p):
    """(distance, arclength from the first point) of p's closest point on the polyline."""
    best = (math.inf, 0.0)
    s0 = 0.0
    for a, b in zip(pts, pts[1:]):
        ab = sub(b, a)
        L = math.sqrt(dot(ab, ab))
        t = min(1.0, max(0.0, dot(sub(p, a), ab) / (L * L)))
        d = math.dist(p, add(a, mul(ab, t)))
        if d < best[0]:
            best = (d, s0 + t * L)
        s0 += L
    return best

def arclen(pts, upto):
    return sum(math.dist(a, b) for a, b in zip(pts[:upto], pts[1:upto + 1]))

def leg_weights(p, rigid):
    """(leg, w_hip, w_knee) for a +X position; leg None for the body."""
    if rigid:
        k = RIGID_LEG
        d, s = closest_on_line(LEGS[k], p)
        w_hip = 1.0
    else:
        best = None
        for k, pts in LEGS.items():
            d, s = closest_on_line(pts, p)
            if d < OWN_RADIUS and (best is None or d < best[1]):
                best = (k, d, s)
        if best is None:
            return (None, 0.0, 0.0)
        k, d, s = best
        sh = arclen(LEGS[k], HIP_INDEX[k])
        w_hip = smoothstep(sh + HIP_BLEND[k][0], sh + HIP_BLEND[k][1], s)
    sk = arclen(LEGS[k], KNEE[k])
    return (k, w_hip, smoothstep(sk - KNEE_BLEND, sk + KNEE_BLEND, s))

def pose(p, leg, w_hip, w_knee, swing, lift):
    """A +X position with its leg swung and lifted by `swing` and `lift` (-1..+1), as rotations."""
    pts = LEGS[leg]
    H, K, T = pts[HIP_INDEX[leg]], pts[KNEE[leg]], pts[-1]
    d = norm((T[0] - H[0], T[1] - H[1], 0.0))
    side = norm(cross(d, UP))           # +angle about this raises the tip
    # Swing: -angle about +Z moves a +X tip toward -Y, forward.
    sa = -math.radians(SWING_DEG[leg]) * swing
    q = add(rotate(sub(p, H), UP, sa * w_hip), H)
    Kq = add(rotate(sub(K, H), UP, sa), H)
    side_q = rotate(side, UP, sa)
    # Lift: femur up about the hip, then tibia back down about the (moved) knee.
    la = math.radians(LIFT_HIP_DEG) * lift
    q = add(rotate(sub(q, H), side_q, la * w_hip), H)
    Kq = add(rotate(sub(Kq, H), side_q, la), H)
    return add(rotate(sub(q, Kq), side_q, -math.radians(LIFT_KNEE_DEG) * lift * w_knee), Kq)

def key_deltas(p, rigid):
    """(swing, lift) offsets of one full-mesh vertex. Each is HALF the difference between the
    +1 and -1 poses, so the linear key passes through rest at 0 and is the same size either way;
    keying the +1 pose itself would make -1 an extrapolated leg 10% longer."""
    mirrored = p[0] < 0.0
    ph = (-p[0], p[1], p[2]) if mirrored else p
    leg, wh, wk = leg_weights(ph, rigid)
    if leg is None:
        return (0.0, 0.0, 0.0), (0.0, 0.0, 0.0)
    g = 1.0 if (leg in GROUP_A_PLUS_X) != mirrored else -1.0
    ds = mul(sub(pose(ph, leg, wh, wk, g, 0), pose(ph, leg, wh, wk, -g, 0)), 0.5)
    dl = mul(sub(pose(ph, leg, wh, wk, 0, g), pose(ph, leg, wh, wk, 0, -g)), 0.5)
    if mirrored:
        ds, dl = (-ds[0], ds[1], ds[2]), (-dl[0], dl[1], dl[2])
    return ds, dl

# ------------------------------------------------------------------------------------------------
# Blender
# ------------------------------------------------------------------------------------------------

def apply_mirror_keeping_keys(o):
    """Apply the Mirror modifier without losing the shape keys, which Blender refuses to do itself
    for a modifier that changes the vertex count: evaluate the mesh once per key with only the
    Mirror on, rebuild the mesh from the basis, and put every key back from its own evaluation.
    The order of the evaluated vertices is the same every time, so key i lines up with basis i."""
    mirror = next((m for m in o.modifiers if m.type == 'MIRROR'), None)
    if mirror is None:
        return False
    me = o.data
    # An object outside the view layer (its collection excluded - the export collections often are)
    # is never evaluated, and evaluated_get quietly hands back the bare half mesh. Link it into the
    # scene for the duration.
    scene = bpy.context.scene
    f_linked = o.name not in bpy.context.view_layer.objects
    if f_linked:
        scene.collection.objects.link(o)
    old_hide = o.hide_viewport
    o.hide_viewport = False
    # By name: every access to o.modifiers makes a new Python wrapper, so `is` never matches.
    others = [(m, m.show_viewport) for m in o.modifiers if m.name != mirror.name]
    for m, _ in others:
        m.show_viewport = False
    keys = list(me.shape_keys.key_blocks) if me.shape_keys else []
    saved = [(kb.name, kb.value, kb.slider_min, kb.slider_max, kb.relative_key.name, kb.mute) for kb in keys]
    # Each key is evaluated by setting it to 1 and the rest to 0. NOT with the shape key pin
    # (show_only_shape_key): pinned, the modifiers are skipped and every "evaluation" is the half.
    old_show_only = o.show_only_shape_key
    o.show_only_shape_key = False
    coords = []
    dg = bpy.context.evaluated_depsgraph_get()
    for i in range(len(saved) + 1):     # the last pass, all zero, is the basis the mesh is built from
        for j, kb in enumerate(keys):
            kb.mute = False
            kb.slider_min = min(kb.slider_min, 0.0)
            kb.value = 1.0 if j == i and j > 0 else 0.0
        dg.update()
        ev = o.evaluated_get(dg)
        if i < len(saved):
            m = ev.to_mesh()
            coords.append([v.co.copy() for v in m.vertices])
            ev.to_mesh_clear()
    new = bpy.data.meshes.new_from_object(ev, preserve_all_data_layers=True, depsgraph=dg)
    o.show_only_shape_key = old_show_only
    for kb, (_, value, smin, _, _, mute) in zip(keys, saved):
        kb.slider_min, kb.value, kb.mute = smin, value, mute
    for m, vis in others:
        m.show_viewport = vis
    o.hide_viewport = old_hide
    if f_linked:
        scene.collection.objects.unlink(o)
    counts = {len(c) for c in coords} | {len(new.vertices)}
    if len(new.vertices) <= len(me.vertices):
        bpy.data.meshes.remove(new)
        raise RuntimeError("the Mirror was not evaluated - the result is no bigger than the half mesh")
    if len(counts) != 1:
        bpy.data.meshes.remove(new)
        raise RuntimeError(f"mirror gives different vertex counts per key {counts} - a key moves a seam vertex off X=0")

    name = me.name
    me.name = name + '_half'
    new.name = name
    o.data = new
    if me.users == 0:
        bpy.data.meshes.remove(me)
    o.modifiers.remove(mirror)
    for i, (kname, value, smin, smax, rel, mute) in enumerate(saved):
        kb = o.shape_key_add(name=kname, from_mix=False)
        for v, c in zip(kb.data, coords[i]):
            v.co = c
        kb.slider_min, kb.slider_max, kb.value, kb.mute = smin, smax, value, mute
    for kname, _, _, _, rel, _ in saved:
        new.shape_keys.key_blocks[kname].relative_key = new.shape_keys.key_blocks[rel]
    moves = [max((a - b).length for a, b in zip(c, coords[0])) for c in coords[1:]]
    print(f"  mirror applied: {len(new.vertices)} vertices, keys {[s[0] for s in saved]}, "
          f"largest move per key {[round(m, 3) for m in moves]}")
    return True

def parts_of(me):
    """Connected piece index per vertex, and the size of each piece."""
    n = len(me.vertices)
    adj = [[] for _ in range(n)]
    for e in me.edges:
        a, b = e.vertices
        adj[a].append(b)
        adj[b].append(a)
    part = [-1] * n
    sizes = []
    for s in range(n):
        if part[s] >= 0:
            continue
        part[s] = len(sizes)
        stack, count = [s], 0
        while stack:
            v = stack.pop()
            count += 1
            for w in adj[v]:
                if part[w] < 0:
                    part[w] = len(sizes)
                    stack.append(w)
        sizes.append(count)
    return part, sizes

def build_leg_keys(o):
    me = o.data
    sk = me.shape_keys
    for name in ('LegsSwing', 'LegsLift'):
        if sk and name in sk.key_blocks:
            o.shape_key_remove(sk.key_blocks[name])
    basis = [tuple(v.co) for v in (me.shape_keys.key_blocks[0].data if me.shape_keys else me.vertices)]
    part, sizes = parts_of(me)

    def piece_near(p):
        return part[min(range(len(basis)), key=lambda i: math.dist(basis[i], p))]
    body_pieces = {piece_near(BODY_POINT)} | {i for i, s in enumerate(sizes) if s < 40}   # + the eyes
    tip = LEGS[RIGID_LEG][-1]
    rigid_pieces = {piece_near(tip), piece_near((-tip[0], tip[1], tip[2]))}

    if not me.shape_keys:
        o.shape_key_add(name='Basis', from_mix=False)
    swing = o.shape_key_add(name='LegsSwing', from_mix=False)
    lift = o.shape_key_add(name='LegsLift', from_mix=False)
    moved = 0
    for i, p in enumerate(basis):
        if part[i] in body_pieces:
            ds, dl = (0, 0, 0), (0, 0, 0)
        else:
            ds, dl = key_deltas(p, part[i] in rigid_pieces)
        swing.data[i].co = add(p, ds)
        lift.data[i].co = add(p, dl)
        moved += any(ds) or any(dl)
    for kb in (swing, lift):
        kb.slider_min, kb.slider_max, kb.value = -1.0, 1.0, 0.0
        kb.relative_key = me.shape_keys.key_blocks[0]
    print(f"  LegsSwing/LegsLift: {moved} of {len(basis)} vertices move")

def add_preview_action(o):
    """SpiderWalk: swing = sin, lift = cos over PREVIEW_FRAMES, keyed at the quarters and cycled.
    Auto-clamped Bezier through 0, 1, 0, -1 is within a few percent of a sine."""
    sk = o.data.shape_keys
    if sk.animation_data and sk.animation_data.action:
        sk.animation_data.action = None
    old = bpy.data.actions.get(PREVIEW_ACTION)
    if old:
        bpy.data.actions.remove(old)
    q = PREVIEW_FRAMES / 4
    for name, values in (('LegsSwing', (0, 1, 0, -1, 0)), ('LegsLift', (1, 0, -1, 0, 1))):
        kb = sk.key_blocks[name]
        for j, v in enumerate(values):
            kb.value = v
            kb.keyframe_insert('value', frame=1 + j * q)
        kb.value = 0.0
    act = sk.animation_data.action
    act.name = PREVIEW_ACTION
    act.use_fake_user = True
    for fc in act.fcurves:
        fc.modifiers.new('CYCLES')
    # The old key would mix into the preview; the game sets every factor itself regardless.
    if 'Walking' in sk.key_blocks:
        sk.key_blocks['Walking'].value = 0.0
    print(f"  action {PREVIEW_ACTION}: frames 1..{1 + PREVIEW_FRAMES}, cyclic")

def main():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    out = argv[argv.index('--out') + 1] if '--out' in argv else None
    o = bpy.data.objects[OBJECT]
    print(f"{OBJECT}: {len(o.data.vertices)} vertices, modifiers {[m.type for m in o.modifiers]}")
    apply_mirror_keeping_keys(o)
    build_leg_keys(o)
    add_preview_action(o)
    if out:
        bpy.ops.wm.save_as_mainfile(filepath=out, copy=True)
        print(f"saved {out}")

main()
