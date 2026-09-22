#!/usr/bin/env python
"""
What a .glb actually contains, frame by frame - clips, rigs, skin weights and root tracks.

    python tools/gltf_clip_dump.py apps/archer/assets/meshes/archer.glb
    python tools/gltf_clip_dump.py apps/archer/assets/meshes/archer.glb --clip Twirl
    python tools/gltf_clip_dump.py apps/archer/assets/meshes/archer.glb --clip Twirl --frames 40

WHY THIS EXISTS RATHER THAN A BLENDER SCRIPT. When an animation looks wrong in the game the
question is always "is the clip wrong, or is the engine wrong with it", and the honest place to
settle that is the EXPORT - not the .blend, because the export is what the engine was given and
half the interesting failures are things the exporter did. This reads the .glb directly with no
Blender, no engine, no GPU and no third-party packages, so it runs anywhere and answers in a
second.

It is what found three things in one session on apps/archer: that a first export contained no
mesh at all, that 29% of its vertices carry a fourth bone influence this engine's three-weight
skinning drops, and - with --clip - that Twirl authors a straight 0.879-unit step while spinning,
which is how the engine's orbiting version was pinned on root-yaw extraction rather than on the
clip.

Reads standard MSYS/Windows python either way; there are no imports beyond the standard library.
"""

import json
import math
import struct
import sys


def load(path):
    """The JSON chunk and the binary chunk of a .glb."""
    data = open(path, 'rb').read()
    magic, version, length = struct.unpack_from('<III', data, 0)
    if magic != 0x46546C67:
        raise SystemExit('%s is not a .glb (bad magic)' % path)
    offset = 12
    js = None
    binary = None
    while offset < length:
        chunk_len, chunk_type = struct.unpack_from('<II', data, offset)
        offset += 8
        if chunk_type == 0x4E4F534A:
            js = json.loads(data[offset:offset + chunk_len].decode('utf-8'))
        else:
            binary = data[offset:offset + chunk_len]
        offset += chunk_len
    return js, binary


COMPONENT = {5120: ('b', 1), 5121: ('B', 1), 5122: ('h', 2),
             5123: ('H', 2), 5125: ('I', 4), 5126: ('f', 4)}
COUNT = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}


def accessor(g, binary, index):
    """Every element of an accessor, as tuples. Honours byteStride and accessor byteOffset."""
    acc = g['accessors'][index]
    view = g['bufferViews'][acc['bufferView']]
    base = view.get('byteOffset', 0) + acc.get('byteOffset', 0)
    fmt, size = COMPONENT[acc['componentType']]
    n = COUNT[acc['type']]
    stride = view.get('byteStride') or size * n
    return [struct.unpack_from('<' + fmt * n, binary, base + k * stride)
            for k in range(acc['count'])]


def invert4(m):
    """Plain Gauss-Jordan. Used only on inverse bind matrices, so speed does not matter."""
    a = [[m[r][c] for c in range(4)] + [1.0 if r == c2 else 0.0 for c2 in range(4)]
         for r in range(4)]
    for i in range(4):
        pivot = max(range(i, 4), key=lambda r: abs(a[r][i]))
        a[i], a[pivot] = a[pivot], a[i]
        d = a[i][i]
        a[i] = [x / d for x in a[i]]
        for r in range(4):
            if r != i and a[r][i] != 0.0:
                f = a[r][i]
                a[r] = [a[r][c] - f * a[i][c] for c in range(8)]
    return [row[4:] for row in a]


def twist_y_degrees(q):
    """
    The Y component of a quaternion's swing-twist split, in degrees.

    The same decomposition core/ObjectAnimation.cpp does (DecomposeSwingTwistY), so a number
    printed here is directly comparable with what the engine extracts as root yaw.
    """
    x, y, z, w = q
    length = math.sqrt(y * y + w * w)
    if length < 1e-6:
        return 0.0
    return math.degrees(2.0 * math.atan2(y / length, w / length))


def unwrapped_turn(quats):
    """
    (net turn, total yaw travelled) over a clip, in degrees.

    Both numbers matter and they answer different questions. The NET says whether the clip is a
    pivot - whether it leaves the character facing somewhere else. The TOTAL says whether the yaw
    moves at all during the clip, which is what decides whether extracting it would sweep an
    authored translation around a circle. A twirl that spins a full turn and comes back nets zero
    and still sweeps.

    Unwrapped per step the same way core/ObjectAnimation.cpp does it (WrapAngleDelta), so a
    continuous spin reads as one number rather than as a sawtooth across the +-180 seam.

    TREAT A BIG NUMBER ON A CLIP THAT OBVIOUSLY DOES NOT SPIN AS AN ARTEFACT, not as a finding.
    Swing-twist is ill-conditioned when the rotation approaches a half turn about an axis
    perpendicular to the twist axis, and a clip that tilts the hips hard can send the twist term
    swinging: apps/archer's Kick_Front reads -347 degrees for a front kick. That costs nothing as
    long as the clip's yaw is NOT extracted, because the engine recomposes swing * twist * ref and
    gets the authored rotation back exactly however the split behaved. It is only a reason not to
    extract the yaw of a clip whose hips leave vertical.
    """
    net = 0.0
    total = 0.0
    previous = twist_y_degrees(quats[0])
    for q in quats[1:]:
        current = twist_y_degrees(q)
        delta = math.fmod(current - previous + 540.0, 360.0) - 180.0
        net += delta
        total += abs(delta)
        previous = current
    return net, total


def summarise(g, binary):
    print('=== %s ===' % ('contents',))
    meshes = g.get('meshes', [])
    print('meshes     : %d %s' % (len(meshes), [m.get('name') for m in meshes]))
    if not meshes:
        print('             *** NO MESH IN THIS FILE - there is nothing to draw ***')
    for node in g['nodes']:
        if 'mesh' not in node:
            continue
        prim = g['meshes'][node['mesh']]['primitives'][0]
        attrs = sorted(prim['attributes'].keys())
        verts = g['accessors'][prim['attributes']['POSITION']]['count']
        print('  node %-42s %6d verts  skin %-5s  %s'
              % (node.get('name'), verts, node.get('skin'), ','.join(attrs)))
    for mat in g.get('materials', []):
        pbr = mat.get('pbrMetallicRoughness', {})
        print('material   : %-40s metallic %.2f roughness %.2f  baseColorTexture %s'
              % (mat.get('name'), pbr.get('metallicFactor', 1.0),
                 pbr.get('roughnessFactor', 1.0),
                 'yes' if 'baseColorTexture' in pbr else 'NO'))
    print('images     : %d' % len(g.get('images', [])))

    for skin in g.get('skins', []):
        joints = skin['joints']
        acc = g['accessors'][skin['inverseBindMatrices']]
        view = g['bufferViews'][acc['bufferView']]
        base = view.get('byteOffset', 0) + acc.get('byteOffset', 0)
        ys = []
        for k in range(len(joints)):
            v = struct.unpack_from('<16f', binary, base + k * 64)
            M = [[v[c * 4 + r] for c in range(4)] for r in range(4)]
            ys.append(invert4(M)[1][3])
        child = set()
        for j in joints:
            for c in g['nodes'][j].get('children', []):
                child.add(c)
        roots = [g['nodes'][j].get('name') for j in joints if j not in child]
        print('skin       : %-24s %3d joints, roots %s' % (skin.get('name'), len(joints), roots))
        print('             bind height %.4f  (y %.4f .. %.4f)' % (max(ys) - min(ys), min(ys), max(ys)))

    #Skin weights, because this engine keeps only THREE per vertex - see GetSkinnedVertex.
    for node in g['nodes']:
        if 'mesh' not in node or node.get('skin') is None:
            continue
        prim = g['meshes'][node['mesh']]['primitives'][0]
        if 'WEIGHTS_0' not in prim['attributes']:
            continue
        w = accessor(g, binary, prim['attributes']['WEIGHTS_0'])
        fourth = sum(1 for v in w if v[3] > 0.001)
        worst = min(v[0] + v[1] + v[2] for v in w)
        mean = sum(v[0] + v[1] + v[2] for v in w) / float(len(w))
        print('weights    : %d of %d vertices have a 4th influence (%.1f%%); first three sum '
              'mean %.4f, worst %.4f' % (fourth, len(w), 100.0 * fourth / len(w), mean, worst))


def clips(g, binary, root_bone):
    root = None
    for i, node in enumerate(g['nodes']):
        if node.get('name') == root_bone:
            root = i
    print()
    print('=== clips (root bone %s) ===' % root_bone)
    print('%-22s %7s %8s %8s %8s %8s  %s'
          % ('clip', 'secs', 'travel', 'u/s', 'net', 'yaw move', 'notes'))
    for anim in g.get('animations', []):
        duration = 0.0
        translation = rotation = None
        for c in anim['channels']:
            s = anim['samplers'][c['sampler']]
            acc = g['accessors'][s['input']]
            if acc.get('max'):
                duration = max(duration, acc['max'][0])
            if c['target']['node'] == root:
                if c['target']['path'] == 'translation':
                    translation = s
                if c['target']['path'] == 'rotation':
                    rotation = s
        travel = 0.0
        if translation:
            p = accessor(g, binary, translation['output'])
            travel = math.hypot(p[-1][0] - p[0][0], p[-1][2] - p[0][2])
        net = 0.0
        swung = 0.0
        if rotation:
            net, swung = unwrapped_turn(accessor(g, binary, rotation['output']))
        note = ''
        if travel > 0.05 and swung > 20.0:
            #The combination that cannot have its yaw extracted while its travel stays on the
            #bone - the offset gets rotated instead of translated. See extract_yaw_root_motion.
            #Judged on how far the yaw MOVES, not on where it ends: a clip that spins and comes
            #back nets zero and still sweeps its travel round a circle on the way.
            note = 'travels AND its yaw moves - do not extract its yaw alone'
        elif abs(net) > 45.0:
            note = 'a real pivot - this is what extracting yaw is for'
        print('%-22s %7.3f %8.3f %8.3f %8.1f %8.1f  %s'
              % (anim.get('name'), duration, travel,
                 travel / duration if duration else 0.0, net, swung, note))


def frames(g, binary, clip_name, root_bone, rows):
    root = None
    for i, node in enumerate(g['nodes']):
        if node.get('name') == root_bone:
            root = i
    for anim in g.get('animations', []):
        if anim.get('name') != clip_name:
            continue
        translation = rotation = None
        for c in anim['channels']:
            if c['target']['node'] != root:
                continue
            s = anim['samplers'][c['sampler']]
            if c['target']['path'] == 'translation':
                translation = s
            if c['target']['path'] == 'rotation':
                rotation = s
        if not translation:
            raise SystemExit('clip %s has no translation track for %s' % (clip_name, root_bone))
        t = accessor(g, binary, translation['input'])
        p = accessor(g, binary, translation['output'])
        q = accessor(g, binary, rotation['output']) if rotation else None
        print()
        print('=== %s, root bone %s, as AUTHORED (armature space) ===' % (clip_name, root_bone))
        print(' %7s %9s %9s %9s %11s' % ('t', 'x', 'y', 'z', 'twist deg'))
        step = max(1, len(p) // rows)
        for k in range(0, len(p), step):
            print(' %7.3f %9.4f %9.4f %9.4f %11.1f'
                  % (t[k][0], p[k][0], p[k][1], p[k][2],
                     twist_y_degrees(q[k]) if q else 0.0))
        print(' start -> end  straight line %.4f units, net turn %.1f deg'
              % (math.hypot(p[-1][0] - p[0][0], p[-1][2] - p[0][2]),
                 (twist_y_degrees(q[-1]) - twist_y_degrees(q[0])) if q else 0.0))
        return
    raise SystemExit('no clip called %s' % clip_name)


def main():
    args = sys.argv[1:]
    if not args:
        raise SystemExit(__doc__)
    path = args[0]
    clip = None
    rows = 12
    root_bone = 'mixamorig:Hips'
    i = 1
    while i < len(args):
        if args[i] == '--clip':
            clip = args[i + 1]; i += 2
        elif args[i] == '--frames':
            rows = int(args[i + 1]); i += 2
        elif args[i] == '--root':
            root_bone = args[i + 1]; i += 2
        else:
            raise SystemExit('unknown option %s' % args[i])
    g, binary = load(path)
    summarise(g, binary)
    clips(g, binary, root_bone)
    if clip:
        frames(g, binary, clip, root_bone, rows)


if __name__ == '__main__':
    main()
