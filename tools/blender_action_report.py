#!/usr/bin/env python
"""Which bones and keys of an action actually do something - and which could go.

    blender.exe -b art_source/chasm/chasm_props.blend -P tools/blender_action_report.py
    blender.exe -b art_source/chasm/chasm_props.blend -P tools/blender_action_report.py -- --action Zombie_Scratch
    blender.exe -b art_source/chasm/chasm_props.blend -P tools/blender_action_report.py -- --action Zombie_Scratch --clean --save

With no --action it lists every action in the file, a line each: what it is on, its length, and how
many of its bone properties sit at rest, hold still, barely move or move. That is how to find a clip
that has no name yet. With --action it goes bone by bone. Run from Blender's Text Editor instead, it
reports the active object's action (the output is in Window > Toggle System Console). It reads the
SAVED file when run with -b: save in Blender first, and close it or revert after a --save.

    --tol-rot 0.5       degrees a rotation may wander and still count as not moving
    --tol-loc 0.001     the same for a location, in world units (the armature's scale applied)
    --tol-scale 0.001   and for a scale

WHY THIS EXISTS. A clip from an FBX arrives with every channel of every bone keyed on every frame -
location, rotation and scale - when for most bones only the rotation does anything.

CLASSES, per bone and property, judged on the curve evaluated at every frame of the clip (not just
on its keys, so a Bezier that bulges between two equal keys is seen):
    rest     the property's default throughout - location 0, rotation none, scale 1.
    const    one value throughout, not the default: a bone held turned.
    ~rest    within the tolerance of the default: a bake's or a mocap's noise, usually.
    ~const   within the tolerance of one value.
    moves    the rest. Its number is how far it moves.
NEEDED is the keys the bone would need: two per curve for a property that holds still, none for one
--clean deletes, and for a moving bone the frames a linear reduction at the tolerances keeps - every
channel of the bone on the same frames, error measured against a slerp, as blender_retarget.py does.

--clean DELETES only what the game never reads or never misses, and THINS the rest of what holds
still to its first and last key. Both are lossless, in Blender and in the game. `~` properties are
reported and never touched: flattening them is a judgment about the clip. Dry run without --save.
    deleted  scale curves at 1 - GLTFLoader reads a scale channel's key times and drops its values,
             so the engine never animates scale at all; and location curves at 0 on every bone but
             the root - no Chasm clip moves another bone's location, so nothing has to put one back.
             A rig whose clips do (a prop bone sliding) should not be cleaned this way.
    thinned  a rotation at rest, the root's location, anything constant. Not deleted, because an
             unsampled export (the Export collection's) writes a channel ONLY where the action has
             a curve, and a clip with no channel for a bone does not reset it: Object::
             SwitchToAnimation keeps the pose, so the bone stays where the previous clip left it.
             Measured 2026-10-19: deleting Zombie_Scratch's at-rest Spine curves took the channel
             out of the .glb, and a zombie would have kept its walk's spine through the scratch.

Export with the Export collection's own exporter, which is unsampled. The File > Export dialog in
chasm_props.blend remembers "Always Sample Animations" ON (2026-10-19), and a sampled export bakes
every frame of every channel of every bone back in - the 2026-10-18 chasm_props.glb is one.
"""

import argparse
import math
import re
import sys

import bpy
from mathutils import Euler, Quaternion, Vector

#data path suffix -> (the column it reports in, channels, default value)
PROPS = {
    "location":            ("location", 3, (0.0, 0.0, 0.0)),
    "rotation_quaternion": ("rotation", 4, (1.0, 0.0, 0.0, 0.0)),
    "rotation_euler":      ("rotation", 3, (0.0, 0.0, 0.0)),
    "rotation_axis_angle": ("rotation", 4, (0.0, 0.0, 1.0, 0.0)),
    "scale":               ("scale", 3, (1.0, 1.0, 1.0)),
}
COLUMNS = ("location", "rotation", "scale")
BONE_PATH = re.compile(r'^pose\.bones\["(.*)"\]\.(%s)$' % "|".join(PROPS))
#Below these a property is EXACTLY its value: single-precision noise, not motion.
EXACT = {"location": 1e-5, "rotation": 1e-5, "scale": 1e-5}


def fcurves_of(action):
    """(slot name or None, channelbag or None, F-curve) for every curve, layered (4.4+) or not."""
    if hasattr(action, "layers") and len(action.layers):
        out = []
        for layer in action.layers:
            for strip in layer.strips:
                for bag in strip.channelbags:
                    slot = getattr(bag, "slot", None)
                    out += [(slot.name_display if slot else None, bag, fc) for fc in bag.fcurves]
        return out
    return [(None, None, fc) for fc in action.fcurves]


def users_of(action):
    """Who plays the action - as the active one or from an NLA strip - and the first armature of them."""
    names = []
    armature = None
    for ob in bpy.data.objects:
        ad = ob.animation_data
        if ad is None:
            continue
        uses = []
        if ad.action == action:
            uses.append(ob.name)
        for track in ad.nla_tracks:
            for strip in track.strips:
                if strip.action == action:
                    uses.append("%s (NLA %s%s)" % (ob.name, track.name, ", muted" if track.mute else ""))
        names += uses
        if uses and armature is None and ob.type == "ARMATURE":
            armature = ob
    return names, armature


def angle(a, b):
    #atan2 of the difference rather than acos of the dot: mathutils is single precision, and acos
    #near 1 turns its rounding into a tenth of a degree of noise. Blind to the quaternion's sign.
    d = a.rotation_difference(b)
    return 2.0 * math.atan2(math.sqrt(d.x * d.x + d.y * d.y + d.z * d.z), abs(d.w))


def as_value(prop, values, order):
    """One frame of a property as what it means: a Quaternion for any rotation, else a Vector."""
    if prop == "rotation_quaternion":
        q = Quaternion(values)
        return q.normalized() if q.magnitude > 1e-9 else Quaternion()
    if prop == "rotation_euler":
        return Euler(values, order).to_quaternion()
    if prop == "rotation_axis_angle":
        axis = Vector(values[1:])
        return Quaternion(axis.normalized(), values[0]) if axis.length > 1e-9 else Quaternion()
    return Vector(values)


class Property:
    """One bone's location, rotation or scale in one action: its curves, and what they amount to."""

    def __init__(self, bone, prop):
        self.bone = bone
        self.prop = prop
        self.column, self.size, self.default = PROPS[prop]
        self.curves = {}            #channel index -> (channelbag, F-curve)
        self.samples = []
        self.cls = None
        self.amount = 0.0
        self.plan = None            #what --clean does with it: "delete", "thin" or None

    def keys(self):
        return sum(len(fc.keyframe_points) for _, fc in self.curves.values())

    def sample(self, frames, order):
        curves = [self.curves.get(i) for i in range(self.size)]
        self.samples = [as_value(self.prop, [c[1].evaluate(f) if c else self.default[i]
                                             for i, c in enumerate(curves)], order)
                        for f in frames]
        if self.column == "rotation":
            #One hemisphere throughout, so a slerp between any two frames goes the short way.
            for i in range(1, len(self.samples)):
                if self.samples[i].dot(self.samples[i - 1]) < 0.0:
                    self.samples[i] = -self.samples[i]

    def error(self, a, b, unit):
        """How far apart two frames' values are, in this property's own measure."""
        if self.column == "rotation":
            return angle(a, b)
        if self.column == "location":
            return (a - b).length * unit
        return max(abs(x - y) for x, y in zip(a, b))

    def classify(self, tol, unit, roots):
        rest = as_value(self.prop, self.default, "XYZ")
        first = self.samples[0]
        offset = max(self.error(s, rest, unit) for s in self.samples)
        spread = max(self.error(s, first, unit) for s in self.samples)
        exact = EXACT[self.column] * (unit if self.column == "location" else 1.0)
        if offset <= exact:
            self.cls, self.amount = "rest", 0.0
        elif spread <= exact:
            self.cls, self.amount = "const", offset
        elif offset <= tol:
            self.cls, self.amount = "~rest", offset
        elif spread <= tol:
            self.cls, self.amount = "~const", spread
        else:
            self.cls, self.amount = "moves", spread
        if self.cls == "rest" and self.deletable(roots):
            self.plan = "delete"
        elif self.cls in ("rest", "const") and any(len(fc.keyframe_points) > 2 for _, fc in self.curves.values()):
            self.plan = "thin"

    def deletable(self, roots):
        """Whether the game misses nothing when this property has no curve at all - see the header."""
        return self.column == "scale" or (self.column == "location" and roots is not None and self.bone not in roots)

    def removed(self):
        """Keys --clean takes away from this property."""
        if self.plan == "delete":
            return self.keys()
        if self.plan == "thin":
            return sum(max(0, len(fc.keyframe_points) - 2) for _, fc in self.curves.values())
        return 0

    def describe(self):
        if self.cls == "rest":
            text = "rest"
        elif self.column == "rotation":
            text = "%s %.1fdeg" % (self.cls, math.degrees(self.amount))
        else:
            text = "%s %.3f" % (self.cls, self.amount)
        return "%-16s%5d" % (text, self.keys())


def reduce(props, tols, unit, n):
    """
    Frames to keep so interpolating between kept neighbours stays within tolerance on every one of
    `props` - one set of frames for all of a bone's channels, the way the engine samples them
    (ObjectAnimation::Sample interpolates only between keys that carry the channel). Ramer-Douglas-
    Peucker from the first and last frame, error against a slerp for rotations and a lerp else.
    """
    keep = {0, n - 1}
    spans = [(0, n - 1)]
    while spans:
        a, b = spans.pop()
        if b - a < 2:
            continue
        worst, at = 1.0, None
        for i in range(a + 1, b):
            f = (i - a) / (b - a)
            for p in props:
                x, y = p.samples[a], p.samples[b]
                between = x.slerp(y, f) if p.column == "rotation" else x.lerp(y, f)
                e = p.error(between, p.samples[i], unit) / tols[p.column]
                if e > worst:
                    worst, at = e, i
        if at is not None:
            keep.add(at)
            spans += [(a, at), (at, b)]
    return len(keep)


class Report:
    """An action taken apart into bones and properties, classified and counted."""

    def __init__(self, action, tols):
        self.action = action
        self.users, self.armature = users_of(action)
        self.unit = self.armature.matrix_world.to_scale()[0] if self.armature else 1.0
        #Bones with no parent. Unknown without an armature, and then no location curve is deletable.
        self.roots = {b.name for b in self.armature.data.bones if b.parent is None} if self.armature else None
        start, end = action.frame_range
        self.frames = list(range(int(math.floor(start)), int(math.ceil(end)) + 1))
        self.props = {}             #(slot, bone) -> {prop name: Property}
        self.other_keys = 0         #curves that are not a bone's transform: shape keys, object motion
        self.other_curves = 0
        self.slots = set()
        for slot, bag, fc in fcurves_of(action):
            m = BONE_PATH.match(fc.data_path)
            if not m:
                self.other_curves += 1
                self.other_keys += len(fc.keyframe_points)
                continue
            bone, prop = m.group(1).replace('\\"', '"'), m.group(2)
            self.slots.add(slot)
            p = self.props.setdefault((slot, bone), {}).setdefault(prop, Property(bone, prop))
            p.curves[fc.array_index] = (bag, fc)
        self.needed = {}
        for key, props in self.props.items():
            order = self.rotation_order(key[1])
            for p in props.values():
                p.sample(self.frames, order)
                p.classify(tols[p.column], self.unit, self.roots)
            moving = [p for p in props.values() if p.cls == "moves"]
            kept = reduce(moving, tols, self.unit, len(self.frames)) if moving else 0
            needed = 0
            for p in props.values():
                if p.cls == "moves":
                    needed += len(p.curves) * kept
                elif not (p.cls in ("rest", "~rest") and p.deletable(self.roots)):
                    needed += len(p.curves) * min(2, len(self.frames))
            self.needed[key] = needed

    def rotation_order(self, bone):
        pb = self.armature.pose.bones.get(bone) if self.armature else None
        return pb.rotation_mode if pb and pb.rotation_mode in ("XYZ", "XZY", "YXZ", "YZX", "ZXY", "ZYX") else "XYZ"

    def ordered(self):
        """Bones parents first when the armature is known, else as the action lists them."""
        if not self.armature:
            return list(self.props)
        depth = {b.name: len(b.parent_recursive) for b in self.armature.data.bones}
        index = {b.name: i for i, b in enumerate(self.armature.data.bones)}
        return sorted(self.props, key=lambda k: (str(k[0]), depth.get(k[1], 99), index.get(k[1], 999)))

    def all_props(self):
        return [p for props in self.props.values() for p in props.values()]

    def counts(self):
        out = {"rest": 0, "const": 0, "~rest": 0, "~const": 0, "moves": 0}
        for p in self.all_props():
            out[p.cls] += 1
        return out

    def keys(self):
        return sum(p.keys() for p in self.all_props())

    def removed(self):
        return sum(p.removed() for p in self.all_props())

    def secs(self):
        render = bpy.context.scene.render
        return (self.frames[-1] - self.frames[0]) * render.fps_base / render.fps


def summary(tols):
    render = bpy.context.scene.render
    print("\nactions in %s (%g fps); bone properties by class, keys now and keys needed"
          % (bpy.path.basename(bpy.data.filepath), render.fps / render.fps_base))
    print("%-26s %9s %7s %6s %7s | %4s %5s %5s %6s %5s | %7s %7s  %s"
          % ("action", "frames", "secs", "bones", "keys", "rest", "const", "~rest", "~const", "moves",
             "clean", "needed", "on"))
    for action in bpy.data.actions:
        r = Report(action, tols)
        c = r.counts()
        print("%-26s %4d-%-4d %7.3f %6d %7d | %4d %5d %5d %6d %5d | %7d %7d  %s"
              % (action.name[:26], r.frames[0], r.frames[-1], r.secs(), len({b for _, b in r.props}),
                 r.keys() + r.other_keys, c["rest"], c["const"], c["~rest"], c["~const"], c["moves"],
                 r.keys() - r.removed(), sum(r.needed.values()),
                 ", ".join(r.users) or "nothing (fake user only?)"))
    print("clean: the keys left after --clean (lossless).  needed: what a reduction at the tolerances keeps.")


def detail(action, tols):
    r = Report(action, tols)
    print("\n%s on %s" % (action.name, ", ".join(r.users) or "nothing"))
    print("frames %d..%d = %.3f s; location in world units (armature scale %.3f); tolerances %.2f deg,"
          " %.4f, scale %.4f" % (r.frames[0], r.frames[-1], r.secs(), r.unit, math.degrees(tols["rotation"]),
                                 tols["location"], tols["scale"]))
    if r.other_curves:
        print("plus %d curves that are not a bone's transform (%d keys) - not judged here"
              % (r.other_curves, r.other_keys))
    print("\n%-16s %-21s %-21s %-21s %6s %6s"
          % ("bone", "location       keys", "rotation       keys", "scale          keys", "keys", "needed"))
    still = []
    for key in r.ordered():
        slot, bone = key
        props = r.props[key]
        cells = []
        extra = []
        for column in COLUMNS:
            here = [p for p in props.values() if p.column == column]
            cells.append(here[0].describe() if here else "%-21s" % "-")
            extra += ["  also %s: %s" % (p.prop, p.describe()) for p in here[1:]]
        label = bone if len(r.slots) < 2 else "%s/%s" % (slot, bone)
        print("%-16s %s %s %s %6d %6d"
              % (label[:16], cells[0], cells[1], cells[2], sum(p.keys() for p in props.values()), r.needed[key]))
        for line in extra:
            print(line)
        if all(p.cls != "moves" for p in props.values()):
            still.append(label)

    c = r.counts()
    keys = r.keys()
    deleted = [p for p in r.all_props() if p.plan == "delete"]
    thinned = [p for p in r.all_props() if p.plan == "thin"]
    near_keys = sum(p.keys() for p in r.all_props() if p.cls in ("~rest", "~const"))
    print("\nbone properties: %d - rest %d, const %d, ~rest %d, ~const %d, moves %d"
          % (len(r.all_props()), c["rest"], c["const"], c["~rest"], c["~const"], c["moves"]))
    for column in COLUMNS:
        ps = [p for p in r.all_props() if p.column == column]
        if ps:
            print("  %-9s %3d keyed, %3d at rest, %3d moving  (%d keys)"
                  % (column, len(ps), sum(1 for p in ps if p.cls == "rest"),
                     sum(1 for p in ps if p.cls == "moves"), sum(p.keys() for p in ps)))
    print("keys: %d now, %d after --clean: it deletes %d properties (%d keys) and thins %d to two keys a curve (%d keys)."
          % (keys, keys - r.removed(), len(deleted), sum(p.removed() for p in deleted),
             len(thinned), sum(p.removed() for p in thinned)))
    print("      %d in curves within tolerance (~rest, ~const); a reduction at the tolerances needs %d."
          % (near_keys, sum(r.needed.values())))
    print("bones that never move beyond the tolerances: %d of %d%s"
          % (len(still), len(r.props), (" - " + ", ".join(still)) if still else ""))
    return r


def clean(r):
    """Applies each property's plan. Returns (curves deleted, keys removed)."""
    deleted = removed = 0
    for p in r.all_props():
        if p.plan == "delete":
            for bag, fc in p.curves.values():
                removed += len(fc.keyframe_points)
                (bag.fcurves if bag else r.action.fcurves).remove(fc)
                deleted += 1
        elif p.plan == "thin":
            for _, fc in p.curves.values():
                points = fc.keyframe_points
                while len(points) > 2:
                    points.remove(points[1], fast=True)
                    removed += 1
                fc.update()
    return deleted, removed


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    ap = argparse.ArgumentParser(prog="blender_action_report.py")
    ap.add_argument("--action", help="the action to take apart; default a summary of them all")
    ap.add_argument("--tol-rot", type=float, default=0.5, help="degrees")
    ap.add_argument("--tol-loc", type=float, default=0.001, help="world units")
    ap.add_argument("--tol-scale", type=float, default=0.001)
    ap.add_argument("--clean", action="store_true", help="delete what the game never reads, thin what holds still")
    ap.add_argument("--save", action="store_true", help="save the .blend after --clean")
    args = ap.parse_args(argv)
    tols = {"rotation": math.radians(args.tol_rot), "location": args.tol_loc, "scale": args.tol_scale}

    action = None
    if args.action:
        action = bpy.data.actions.get(args.action)
        if action is None:
            sys.exit("no action called %r; the file has %s" % (args.action, [a.name for a in bpy.data.actions]))
    elif not bpy.app.background:
        ob = bpy.context.active_object
        action = ob.animation_data.action if ob and ob.animation_data else None
    if action is None:
        if args.clean:
            sys.exit("--clean needs --action")
        summary(tols)
        return

    r = detail(action, tols)
    if not args.clean:
        return
    deleted, removed = clean(r)
    print("\n--clean: %d curves deleted, %d keys removed in all" % (deleted, removed))
    if not args.save:
        print("dry run: nothing written (--save saves the .blend)")
        return
    bpy.ops.wm.save_mainfile()
    print("saved %s" % bpy.data.filepath)


main()
