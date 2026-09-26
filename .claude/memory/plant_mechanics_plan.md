---
name: plant-mechanics-plan
description: "AGREED 2026-09-26 archer plan for gameplay plants (tree with arm platforms, bounce pad then bending leaf, thin branch with balance, arrow pegs/rope arrows/arrow cuts/wind); plan in apps/archer/plant_mechanics_plan.md"
metadata:
  node_type: memory
  type: project
  originSessionId: 95091de0-2e6d-47f4-a5dd-59fe26a32d0c
  modified: 2026-09-26T10:53:28.527Z
---

Archer's next mechanics, agreed with the user 2026-09-26 and written down in
`apps/archer/plant_mechanics_plan.md`: a tree climbed by its arms (blockout first, at the right of
the main level, which may be extended), a bounce pad then a bending leaf that slides her off and
flings her on a timed jump, a thin branch with a balance mechanic, and smaller ideas (stuck arrows
as pegs, rope arrows, cutting with arrows, wind).

**Why:** the user wants plants that are gameplay, not decoration - extending the vine system
([[vine-rope-system]]) with new prop types.

**How to apply:** gameplay plants are DECLARED IN STAGE (rules) and the app builds the mesh from the
same declaration - unlike the view-only `DeclareVines`. Order: tree, bounce pad, leaf, branch; pegs
anywhere. Trunk defaults to standing behind her in depth. Branch lean: toward/away from the camera,
corrected on up/down, shown by pose + a balance arc. Check the plan's status table before starting.
