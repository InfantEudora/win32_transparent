---
name: sign-text-anchors
description: Text on props (archer signpost) is placed by Cube empties text_<n> parented to the prop in Blender; user confirmed it works 2026-09-25
metadata:
  node_type: memory
  type: project
  originSessionId: 7254b368-1eab-41dd-a679-f5e77c2d9d41
  modified: 2026-09-25T19:43:20.511Z
---

apps/archer/Sign.h/.cpp: a prop node plus child Cube empties named `text_0..n` (Blender `.001` suffix ignored). The empty's scale = the text box's half-extents (depth = letter relief, centred on the board face); unrotated reads from Blender Front view. Strings come from the level (`StageSign` in Stage.h, `SignVariant` + `SIGN_NODES`). Needs `GLTFLoader::GetNodeChildNames` (core). The glTF exporter writes a child's transform relative to its parent as Blender (x,z,-y), so no rotation fix-up is needed; this was checked on a Blender 4.5 export.

The user authored the real signpost this way on 2026-09-25 and it worked first time. Treat this as the approved pattern for marking points on assets.

**Why:** the user liked authoring the placement in Blender over hand-typed offsets in code.
**How to apply:** for similar asset markers (sockets, attach points, more sign kinds), prefer parented empties read through GetNodeChildNames/GetNodePosition. Open follow-ups the user may pick up: a `size_group` for matching text size across boards, Blender custom properties (extras) as defaults, and moving Sign to core when a second app wants it. Related: [[asset-origin-convention]], [[archer-app]].
