#!/usr/bin/env python
"""Retarget a dense Mixamo clip onto a small rig, keeping only the keys the clip needs.

    blender.exe -b art_source/chasm/chasm_props.blend -P tools/blender_retarget.py -- \
        --source Armature --action Shooting30fps --source-fps 30 --target male_armature --name Shoot

Without --save it is a dry run: it does everything and reports, and writes nothing. Re-running
replaces the action and NLA track of the same name.

    --range 31 95       only these SOURCE frames, to cut one clip into several (draw, hold, release)
    --tol 2.0           base tolerance in degrees; each bone scales it (MIXAMO_15 below). 0 keeps
                        every frame, for an A/B against the reduced version.
    --tol-loc 0.005     Hips location tolerance, in the target's armature units

Keys land on whole frames at the SCENE's rate, so a clip keeps its length whatever rate it was
downloaded at (--source-fps). Shoot from Shooting30fps, 2026-10-11: 121 frames at 24 fps, 1,463 keys
written against 30,200 in the source and 7,623 for a full bake of 15 bones; worst bone-tip drift
21 mm (the drawing hand mid-draw) on a 1.4 m figure. The motion is smooth, not mocap jitter - the
draw turns the upper arm up to 22 degrees a frame - so straight lines simply need that many points.

WHY THIS EXISTS. Mixamo clips come with every channel keyed on every frame on a 25-bone rig
(Shooting30fps: 30,200 keys); the villagers have 15 bones. Retargeters (Rokoko, Auto-Rig Pro) do the
mapping but bake every frame, and Blender's Decimate is the wrong reduction for this engine: it
keeps Bezier handles to hold the curve's shape, and GLTFLoader reads a CUBICSPLINE key as its middle
value and drops the tangents, so a decimated curve plays as straight lines between the keys that
survived. Between two keys the engine slerps, and only between keys that BOTH carry the channel
(ObjectAnimation::Sample) - so the reduction here aims at exactly that: LINEAR keys, every channel
of a bone on the same frames, error measured against a slerp.

RETARGET. Each target bone takes the WORLD rotation its source bone has, as a change from that
bone's rest: target_world = source_pose * source_rest^-1 * target_rest. Copying the change in world
space rather than local values is what lets the rigs differ - the source's bone rolls, centimetres,
Y-up rest data and the bones the target lacks (Spine1, Neck, the shoulders, the toes) all drop out.
A target bone standing in for several source bones copies the LAST of them, so their motion
survives: Arm.Upper takes LeftArm's world rotation, which already includes the shoulder's. It
assumes both rests are the same POSE (Mixamo's and the villagers' A-poses agree to ~5 degrees on
every mapped bone); a T-pose source on an A-pose rig would carry the difference as an offset.
Only Hips gets location keys: the source's hip travel, scaled by the ratio of hip heights and
measured from where the clip's feet start (PLANT below).

REDUCE. Per bone, one set of frames for all its channels. From the first and last frame, the frame
furthest from the slerp between its neighbours is kept until every frame is within the bone's
tolerance (Ramer-Douglas-Peucker). A bone that does not move keeps two keys.

CHECK. The reduced clip is played back by forward kinematics against the full one and the worst
distance any bone's tip strays is reported in world units. That is the number to judge a tolerance
by: per-bone angles add up down a chain, a hip error moves everything below it.

EXPORT. The action goes on the target in a muted NLA track of its own name, its keys in a SLOT of its own name (4.4+:
pick that slot beside the action in the Action Editor to see them); with two armatures in
the file the glTF exporter writes only each one's active action and NLA strips. The export must
have "Always Sample Animations" OFF (export_force_sampling=False), or every frame comes back. Keys
start at frame 1 (--first-frame), Blender's default and the convention of every clip in the chasm file
(the user's, 2026-10-19). Unsampled, a key's time is its frame's - slide-to-zero only works when sampling -
so in the .glb such a clip starts at 1/fps, as every hand-made one does (measured on the 2026-10-19 16:40
export: 0.0417 s at 24 fps).
In-game the clip is 5.0 s, 7.3 KB; it loads in chasm (checked 2026-10-11).
"""

import argparse
import math
import sys

import bpy
from mathutils import Matrix, Quaternion, Vector

#target bone, the Mixamo bone whose world rotation it copies, and its tolerance as a multiple of
#--tol. Tight at the root of each chain, since an error there moves every bone below it.
MIXAMO_15 = [
    ("Hips",        "mixamorig:Hips",         0.5),
    ("Spine",       "mixamorig:Spine2",       0.5),
    ("Head",        "mixamorig:Head",         1.5),
    ("Arm.Upper.L", "mixamorig:LeftArm",      0.75),
    ("Arm.Lower.L", "mixamorig:LeftForeArm",  1.0),
    ("Hand.L",      "mixamorig:LeftHand",     1.5),
    ("Arm.Upper.R", "mixamorig:RightArm",     0.75),
    ("Arm.Lower.R", "mixamorig:RightForeArm", 1.0),
    ("Hand.R",      "mixamorig:RightHand",    1.5),
    ("Leg.Upper.L", "mixamorig:LeftUpLeg",    0.5),
    ("Leg.Lower.L", "mixamorig:LeftLeg",      0.75),
    ("Foot.L",      "mixamorig:LeftFoot",     1.5),
    ("Leg.Upper.R", "mixamorig:RightUpLeg",   0.5),
    ("Leg.Lower.R", "mixamorig:RightLeg",     0.75),
    ("Foot.R",      "mixamorig:RightFoot",    1.5),
]

#Target bones whose sources' midpoint, at the clip's first frame, is put where it is at rest. A
#Mixamo hip track is not relative to the rig's rest - Shooting30fps stands a metre to one side of it
#- so the hips' travel is measured from the feet the clip starts on.
PLANT = ("Foot.L", "Foot.R")


def rot(m):
    """Rotation of a matrix that may carry a uniform scale (the Mixamo rig's object is 0.01)."""
    return m.to_3x3().normalized().to_quaternion()


def source_bone_name(src, name):
    """The Mixamo name, or the .L/.R form tools/blender_mixamo_mirror_names.py renames it to."""
    if name in src.pose.bones:
        return name
    prefix, _, rest = name.rpartition(":")
    for side, suffix in (("Left", ".L"), ("Right", ".R")):
        if rest.startswith(side):
            alt = prefix + ":" + rest[len(side):] + suffix
            if alt in src.pose.bones:
                return alt
    sys.exit("source %s has no bone %s" % (src.name, name))


def in_hierarchy_order(bones):
    return sorted(bones, key=lambda b: len(b.parent_recursive))


def sample_source(src, action, names, first, last, src_fps, fps):
    """World rotation of each named source bone and world position of the first, at every frame
    of the scene's rate - so the clip keeps its length whatever rate it was downloaded at."""
    scene = bpy.context.scene
    ad = src.animation_data
    keep = (ad.action, getattr(ad, "action_slot", None), scene.frame_current)
    ad.action = action
    if hasattr(ad, "action_slot") and ad.action_slot is None and len(action.slots):
        ad.action_slot = action.slots[0]
    count = int(math.floor((last - first) * fps / src_fps + 1e-6)) + 1
    samples = []
    for k in range(count):
        f = first + k * src_fps / fps
        scene.frame_set(int(math.floor(f)), subframe=f - math.floor(f))
        ev = src.evaluated_get(bpy.context.evaluated_depsgraph_get())
        mw = ev.matrix_world
        samples.append({n: (rot(mw @ ev.pose.bones[n].matrix), mw @ ev.pose.bones[n].head) for n in names})
    ad.action = keep[0]
    if keep[1] is not None:
        ad.action_slot = keep[1]
    scene.frame_set(keep[2])
    return samples


def retarget(src, tgt, mapping, samples):
    """Per target bone, its local rotation at every frame; and the Hips location track."""
    src_rest = {s: rot(src.matrix_world @ src.data.bones[s].matrix_local) for _, s, _ in mapping}
    tgt_rest = {t: rot(tgt.matrix_world @ tgt.data.bones[t].matrix_local) for t, _, _ in mapping}
    tmw_inv = rot(tgt.matrix_world).inverted()
    tscale = tgt.matrix_world.to_scale()[0]
    bones = in_hierarchy_order(tgt.data.bones)
    source_of = {t: s for t, s, _ in mapping}

    root = bones[0]
    src_root = source_of[root.name]
    src_root_rest = src.matrix_world @ src.data.bones[src_root].head_local
    ratio = (tgt.matrix_world @ root.head_local).z / src_root_rest.z     #hip heights above the ground
    feet = [source_of[t] for t in PLANT]
    plant = sum((src.matrix_world @ src.data.bones[s].head_local for s in feet), Vector()) / len(feet)
    plant -= sum((samples[0][s][1] for s in feet), Vector()) / len(feet)
    plant.z = 0.0           #heights are the clip's own: a crouch stays a crouch

    local = {t: [] for t in source_of}
    root_loc = []
    for frame in samples:
        arm_rot = {}        #each bone's armature-space rotation this frame, parents first
        for b in bones:
            rb = b.matrix_local.to_quaternion()
            parent = b.parent.matrix_local.to_quaternion() if b.parent else None
            if b.name not in source_of:
                #Unmapped (a prop socket): it keeps its rest pose and follows its parent.
                arm_rot[b.name] = (arm_rot[b.parent.name] @ parent.inverted() @ rb) if b.parent else rb
                continue
            s = source_of[b.name]
            r = tmw_inv @ (frame[s][0] @ src_rest[s].inverted()) @ tgt_rest[b.name]
            arm_rot[b.name] = r
            q = rb.inverted() @ (parent @ arm_rot[b.parent.name].inverted() if b.parent else Quaternion()) @ r
            series = local[b.name]
            if series and q.dot(series[-1]) < 0:
                q.negate()          #keep one hemisphere, so a slerp between any two keys is short
            series.append(q)
        moved = tmw_inv @ ((frame[src_root][1] + plant - src_root_rest) * ratio) / tscale
        root_loc.append(root.matrix_local.to_quaternion().inverted() @ moved)
    return local, root.name, root_loc, ratio


def angle(a, b):
    #atan2 of the difference rather than acos of the dot: mathutils is single precision, and acos
    #near 1 turns its rounding into a tenth of a degree of noise the reduction would chase.
    d = a.rotation_difference(b)
    return 2.0 * math.atan2(math.sqrt(d.x * d.x + d.y * d.y + d.z * d.z), abs(d.w))


def reduce(qs, locs, tol_rad, tol_loc):
    """Frame indices to keep so a slerp (and lerp) between kept neighbours stays within tolerance."""
    n = len(qs)
    if tol_rad <= 0.0:
        return list(range(n))
    keep = {0, n - 1}
    spans = [(0, n - 1)]
    while spans:
        a, b = spans.pop()
        if b - a < 2:
            continue
        worst, at = 1.0, None
        if qs[a].dot(qs[b]) < 0.0:
            worst, at = math.inf, (a + b) // 2     #over half a turn apart: slerp would go the short way
        else:
            for i in range(a + 1, b):
                f = (i - a) / (b - a)
                e = angle(qs[a].slerp(qs[b], f), qs[i]) / tol_rad
                if locs is not None:
                    e = max(e, (locs[a].lerp(locs[b], f) - locs[i]).length / tol_loc)
                if e > worst:
                    worst, at = e, i
        if at is not None:
            keep.add(at)
            spans += [(a, at), (at, b)]
    return sorted(keep)


def played_back(series, kept, n):
    """What the engine samples at every frame from the kept keys alone."""
    out = []
    j = 0
    for i in range(n):
        while j + 1 < len(kept) - 1 and kept[j + 1] <= i:
            j += 1
        a, b = kept[j], kept[min(j + 1, len(kept) - 1)]
        f = 0.0 if b == a else (i - a) / (b - a)
        x, y = series[a], series[b]
        out.append(x.slerp(y, f) if isinstance(x, Quaternion) else x.lerp(y, f))
    return out


def tips(tgt, local, root, root_loc, i):
    """World position of every mapped bone's tail at frame i, by forward kinematics."""
    mats = {}
    out = {}
    for b in in_hierarchy_order(tgt.data.bones):
        q = local[b.name][i] if b.name in local else Quaternion()
        basis = q.to_matrix().to_4x4()
        if b.name == root:
            basis = Matrix.Translation(root_loc[i]) @ basis
        rest = (b.parent.matrix_local.inverted() @ b.matrix_local) if b.parent else b.matrix_local
        mats[b.name] = (mats[b.parent.name] @ rest @ basis) if b.parent else (rest @ basis)
        if b.name in local:
            out[b.name] = tgt.matrix_world @ (mats[b.name] @ Vector((0.0, b.length, 0.0)))
    return out


def write_action(tgt, name, first_frame, local, kept, root, root_loc):
    old = bpy.data.actions.get(name)
    if old is not None:
        bpy.data.actions.remove(old)
    ad = tgt.animation_data or tgt.animation_data_create()
    for track in [t for t in ad.nla_tracks if t.name == name]:
        ad.nla_tracks.remove(track)

    act = bpy.data.actions.new(name)
    act.use_fake_user = True
    keep = (ad.action, getattr(ad, "action_slot", None))
    #4.4+: a slot of the clip's own name, made before the action is assigned. Left to Blender, assigning it reuses
    #the name of the slot the target last played - the new keys landed in a slot called "Walking" (2026-10-19) and
    #the Action Editor showed an empty clip under the clip's own name.
    own = act.slots.new(id_type="OBJECT", name=name) if hasattr(act, "slots") else None
    ad.action = act
    if own is not None:
        ad.action_slot = own

    def curve(path, index, group):
        if hasattr(act, "fcurve_ensure_for_datablock"):     #4.4+: layered actions with slots
            fc = act.fcurve_ensure_for_datablock(tgt, path, index=index)
            bag = act.layers[0].strips[0].channelbag(ad.action_slot)
            fc.group = bag.groups.get(group) or bag.groups.new(group)
            return fc
        return act.fcurves.new(path, index=index, action_group=group)

    for bone, series in local.items():
        channels = [("rotation_quaternion", 4, series)]
        if bone == root:
            channels.append(("location", 3, root_loc))
        frames = kept[bone]
        for prop, size, values in channels:
            for c in range(size):
                fc = curve('pose.bones["%s"].%s' % (bone, prop), c, bone)
                fc.keyframe_points.add(len(frames))
                for kp, i in zip(fc.keyframe_points, frames):
                    kp.co = (first_frame + i, values[i][c])
                    kp.interpolation = "LINEAR"
                fc.update()

    slot = getattr(ad, "action_slot", None)
    ad.action = keep[0]
    if keep[1] is not None:
        ad.action_slot = keep[1]
    track = ad.nla_tracks.new()
    track.name = name
    strip = track.strips.new(name, first_frame, act)
    if slot is not None:
        strip.action_slot = slot
    track.mute = True       #exported all the same; muted so the viewport keeps playing the active clip
    return act


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    ap = argparse.ArgumentParser(prog="blender_retarget.py")
    ap.add_argument("--source", required=True, help="the Mixamo armature object")
    ap.add_argument("--action", help="the source clip; default the source's active action")
    ap.add_argument("--source-fps", type=float, default=30.0, help="rate the clip was downloaded at")
    ap.add_argument("--target", required=True, help="the armature object to animate")
    ap.add_argument("--name", required=True, help="name of the new action and its NLA track")
    ap.add_argument("--range", type=float, nargs=2, metavar=("FIRST", "LAST"), help="source frames")
    ap.add_argument("--tol", type=float, default=2.0, help="base tolerance, degrees")
    ap.add_argument("--tol-loc", type=float, default=0.005, help="Hips location tolerance")
    ap.add_argument("--first-frame", type=int, default=1, help="the frame the clip's first key lands on")
    ap.add_argument("--save", action="store_true", help="save the .blend")
    args = ap.parse_args(argv)

    src = bpy.data.objects[args.source]
    tgt = bpy.data.objects[args.target]
    action = bpy.data.actions[args.action] if args.action else src.animation_data.action
    for b in tgt.pose.bones:
        if b.rotation_mode != "QUATERNION":
            sys.exit("%s.%s rotates in %s; the keys written are quaternions" % (tgt.name, b.name, b.rotation_mode))
    mapping = [(t, source_bone_name(src, s), k) for t, s, k in MIXAMO_15]
    missing = [t for t, _, _ in mapping if t not in tgt.data.bones]
    if missing:
        sys.exit("target %s has no bones %s" % (tgt.name, missing))

    fps = bpy.context.scene.render.fps / bpy.context.scene.render.fps_base
    first, last = args.range if args.range else action.frame_range
    samples = sample_source(src, action, [s for _, s, _ in mapping], first, last, args.source_fps, fps)
    local, root, root_loc, ratio = retarget(src, tgt, mapping, samples)
    n = len(samples)

    kept = {}
    reduced = {}
    print("\n%s -> %s.%s: source frames %g..%g at %g fps = %d frames at %g fps, hip ratio %.3f"
          % (action.name, tgt.name, args.name, first, last, args.source_fps, n, fps, ratio))
    print("%-12s %5s %9s" % ("bone", "keys", "worst deg"))
    for t, _, scale in mapping:
        locs = root_loc if t == root else None
        kept[t] = reduce(local[t], locs, math.radians(args.tol * scale), args.tol_loc)
        reduced[t] = played_back(local[t], kept[t], n)
        worst = max(angle(a, b) for a, b in zip(local[t], reduced[t]))
        print("%-12s %5d %9.2f" % (t, len(kept[t]), math.degrees(worst)))
    reduced_loc = played_back(root_loc, kept[root], n)

    drift, drift_at = 0.0, None
    for i in range(n):
        full, cut = tips(tgt, local, root, root_loc, i), tips(tgt, reduced, root, reduced_loc, i)
        for b in full:
            if (full[b] - cut[b]).length > drift:
                drift, drift_at = (full[b] - cut[b]).length, (b, i)
    travel = max(v.length for v in root_loc) * tgt.matrix_world.to_scale()[0]
    channels = sum(len(kept[t]) * (7 if t == root else 4) for t in kept)
    print("keys: source action %d, written %d (%d frames x %d bones would be %d)"
          % (sum(len(fc.keyframe_points) for fc in fcurves_of(action)), channels, n, len(kept),
             n * (len(kept) * 4 + 3)))
    print("worst bone-tip drift from the full clip: %.1f mm (%s, frame %s); hips travel up to %.1f mm"
          % (drift * 1000.0, drift_at[0] if drift_at else "-", drift_at[1] + 1 if drift_at else "-",
             travel * 1000.0))

    if not args.save:
        print("dry run: nothing written (--save writes the action and saves the .blend)")
        return
    #Frame 1 by default, Blender's own and the file's other clips': keyed alike, they start alike in the .glb.
    write_action(tgt, args.name, args.first_frame, local, kept, root, root_loc)
    bpy.ops.wm.save_mainfile()
    print("saved %s with action %s on %s" % (bpy.data.filepath, args.name, tgt.name))


def fcurves_of(action):
    """Every F-curve of an action, layered (4.4+) or not."""
    if hasattr(action, "layers") and len(action.layers):
        return [fc for layer in action.layers for strip in layer.strips
                for bag in strip.channelbags for fc in bag.fcurves]
    return list(action.fcurves)


main()
