#!/usr/bin/env python
"""Rename Mixamo's Left/Right bones to Blender's .L/.R convention, everywhere in the .blend.

    mixamorig:RightHandThumb4  ->  mixamorig:HandThumb4.R
    mixamorig:LeftUpLeg        ->  mixamorig:UpLeg.L
    mixamorig:Hips             ->  unchanged (no side)

Run it from Blender's Text Editor (Alt+P) with the character's .blend open, or headless:

    blender art_source/archer.blend --background --python tools/blender_mixamo_mirror_names.py
    blender ... --python tools/blender_mixamo_mirror_names.py -- --reverse --dry-run

WHY: Blender's mirror tools (Mirror modifier vertex-group mirroring, X-Axis Mirror in edit and pose
mode, Symmetrize, Paste Flipped pose) find a bone's other side by flipping a Left/Right marker
that has to be at the very START or very END of the name. "mixamorig:RightHand" has it in the
middle, after the namespace, so Blender treats every Mixamo bone as unpaired. ".R" at the end is
the most reliable form it recognises.

WHAT GETS RENAMED
-----------------
The rename is a pure function of the name (see convert()), not a table built from one armature,
so it can be applied to anything that carries a bone name, and running it twice changes nothing:

  - bones of every local armature. Blender's own rename then fixes pose channels, constraint
    subtargets on any object, bone parenting, the vertex groups of meshes deformed by it, and the
    F-curves of the actions currently ASSIGNED to it (active action and NLA strips).
  - F-curve paths and channel groups of EVERY local action, whatever it is assigned to. This is
    the part Blender does not do: a clip that is only stashed, kept by a fake user or sitting on a
    different armature keeps pointing at "mixamorig:LeftArm", and silently stops animating that
    bone. Both the legacy action API and 4.4+'s layered/slotted actions are handled.
  - vertex groups on every mesh, again, in case a mesh is skinned without an Armature modifier
    pointing at the rig (a separated hair mesh with its modifier removed, for instance).
  - driver paths and driver variable bone targets.

Library-linked data (from another .blend) is read-only here and is listed, not changed: run the
script in the file that owns it.

Re-run it after importing a fresh Mixamo clip; the new FBX arrives with the old names.
"""

import re
import sys

import bpy

#--- options (also settable from the command line after "--") ---------------------------------
DRY_RUN = False     #report what would change, touch nothing
REVERSE = False     #.L/.R back to Mixamo's Left/Right, e.g. to feed a tool that expects them

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
DRY_RUN = DRY_RUN or "--dry-run" in argv
REVERSE = REVERSE or "--reverse" in argv

#Any mixamorig namespace: a second import into the same file arrives as mixamorig1:, mixamorig2:...
MIXAMO_SIDED = re.compile(r"^(mixamorig\d*:)(Left|Right)(.+)$")
BLENDER_SIDED = re.compile(r"^(mixamorig\d*:)(.+)\.(L|R)$")
BONE_PATH = re.compile(r'pose\.bones\["([^"]+)"\]')


def convert(name):
    """The new name for a bone name, or the name itself when it has no side to move."""
    if REVERSE:
        m = BLENDER_SIDED.match(name)
        if not m:
            return name
        return m.group(1) + ("Left" if m.group(3) == "L" else "Right") + m.group(2)
    m = MIXAMO_SIDED.match(name)
    if not m:
        return name
    return m.group(1) + m.group(3) + (".L" if m.group(2) == "Left" else ".R")


def convert_path(path):
    return BONE_PATH.sub(lambda m: 'pose.bones["%s"]' % convert(m.group(1)), path)


counts = {"bones": 0, "vertex groups": 0, "fcurves": 0, "groups": 0, "drivers": 0}
skipped_linked = []
problems = []


def is_linked(id_block):
    return id_block.library is not None


#--- 1. bones ----------------------------------------------------------------------------------
#Object mode: renaming through Bone rather than EditBone is what triggers Blender's dependent
#fix-ups, and an armature left in edit mode would overwrite the rename on exit.
if not DRY_RUN and bpy.context.object and bpy.context.object.mode != "OBJECT":
    bpy.ops.object.mode_set(mode="OBJECT")

for arm in bpy.data.armatures:
    if is_linked(arm):
        skipped_linked.append("armature " + arm.name)
        continue
    renames = [(b.name, convert(b.name)) for b in arm.bones if convert(b.name) != b.name]
    existing = {b.name for b in arm.bones}
    clashes = [new for old, new in renames if new in existing]
    if clashes:
        #Blender would quietly make these "...Hand.L.001" and break the pairing it was for.
        problems.append("armature %s: target names already exist: %s" % (arm.name, ", ".join(clashes)))
        continue
    for old, new in renames:
        if not DRY_RUN:
            arm.bones[old].name = new
        counts["bones"] += 1

#--- 2. vertex groups --------------------------------------------------------------------------
for obj in bpy.data.objects:
    if obj.type != "MESH" or is_linked(obj):
        continue
    for vg in obj.vertex_groups:
        new = convert(vg.name)
        if new == vg.name:
            continue
        if new in obj.vertex_groups:
            problems.append("mesh %s: both %s and %s exist, left alone" % (obj.name, vg.name, new))
            continue
        if not DRY_RUN:
            vg.name = new
        counts["vertex groups"] += 1


#--- 3. actions --------------------------------------------------------------------------------
def channel_sets(action):
    """(fcurves, groups) pairs of an action, for both the layered (4.4+) and the legacy API."""
    layers = getattr(action, "layers", None)
    if layers is not None and len(layers):
        for layer in layers:
            for strip in layer.strips:
                for bag in getattr(strip, "channelbags", ()):
                    yield bag.fcurves, bag.groups
    elif hasattr(action, "fcurves"):     #removed in 5.0, where every action is layered
        yield action.fcurves, action.groups


for action in bpy.data.actions:
    if is_linked(action):
        skipped_linked.append("action " + action.name)
        continue
    for fcurves, groups in channel_sets(action):
        for fc in fcurves:
            new = convert_path(fc.data_path)
            if new != fc.data_path:
                if not DRY_RUN:
                    fc.data_path = new
                counts["fcurves"] += 1
        for group in groups:
            new = convert(group.name)
            if new != group.name:
                if not DRY_RUN:
                    group.name = new
                counts["groups"] += 1

#--- 4. drivers --------------------------------------------------------------------------------
for id_block in list(bpy.data.objects) + list(bpy.data.armatures) + list(bpy.data.shape_keys):
    ad = getattr(id_block, "animation_data", None)
    if not ad or is_linked(id_block):
        continue
    for fc in ad.drivers:
        changed = False
        new = convert_path(fc.data_path)
        if new != fc.data_path:
            if not DRY_RUN:
                fc.data_path = new
            changed = True
        for var in fc.driver.variables:
            for t in var.targets:
                if t.bone_target and convert(t.bone_target) != t.bone_target:
                    if not DRY_RUN:
                        t.bone_target = convert(t.bone_target)
                    changed = True
                if t.data_path and convert_path(t.data_path) != t.data_path:
                    if not DRY_RUN:
                        t.data_path = convert_path(t.data_path)
                    changed = True
        counts["drivers"] += changed

#--- report ------------------------------------------------------------------------------------
#Counts after step 1 include only what the script itself changed; F-curves of assigned actions
#were already fixed by Blender's bone rename, so a low F-curve count is expected, not a miss.
print("\n%s%s Mixamo side names:" % ("[dry run] " if DRY_RUN else "",
                                     ".L/.R -> Left/Right" if REVERSE else "Left/Right -> .L/.R"))
for key, n in counts.items():
    print("  %-14s %d" % (key, n))
for name in skipped_linked:
    print("  skipped (linked from a library): " + name)
for p in problems:
    print("  PROBLEM: " + p)
