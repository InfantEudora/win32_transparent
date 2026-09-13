---
name: pinball_stage0_review
description: "Pinball (apps/pinball, \"Orbit Outpost\") stage 0 reviewed and re-laid 2026-09-13 - Table.h is the machine, tools/pinball_plan.py is the acceptance test, parts.glb comes from a Blender script; the traps that cost time"
metadata: 
  node_type: memory
  type: project
  originSessionId: 3a7bcb0c-73e8-48d0-b66a-aa1f9d113ca8
  modified: 2026-09-13T12:49:58.866Z
---

`apps/pinball` is a stage-0 (static, no physics) pinball machine on the engine, first built by
another agent (commit `d59ab93`) and reviewed/re-laid by me on 2026-09-13. Findings and the
re-layout rationale: `docs/pinball_findings.md`; the layout itself: `apps/pinball/Table.h`
(revision note at the top); the design: `apps/pinball/pinball_design.md` (its §1.5 numbers are
the FIRST draft, kept for reasoning, and do not match the build).

**Why:** the first layout looked fine and did not work - a ball from the plunger could not reach
the play area, the inlanes were under a ball wide, the ramps had 0.18 between their rails, the
right flipper rested in its flipped pose. All invisible in the coordinates. The user's three
asks were: the machine is too long vs the reference (now 9.2 x 5.8, 3:2 cabinet, 800x1200
window), the ball may not fit the guides (it did not; now every lane is 1.48-1.9 balls clear),
and the parts should come from Blender as the design says (now they do).

**How to apply:**
- `Table.h` is the single source of coordinates. After moving ANYTHING, run
  `python tools/pinball_plan.py` with the app running (it reads `pinball_layout`'s `plan`, never
  a copy) - it floods the deck from the plunger and must say "clear". Do not resurrect a
  copy-the-numbers checker.
- Clear widths, not centre-to-centre: subtract half of each bounding wall's thickness. The old
  "1.43 balls" inlane was 0.88.
- The design's "interior rails fat in collision (0.6)" rule closes every lane; interior rail
  colliders must be at visual thickness (0.14) and the swept-raycast tunnel guard is mandatory
  in stage 1. `--collider 0.6` on the plan tool demonstrates it.
- Repeated parts: `tools/pinball_parts_blender.py` (headless Blender 4.5 at
  `C:/Program Files/Blender Foundation/Blender 4.5/blender.exe`) -> `apps/pinball/assets/meshes/parts.glb`;
  `LoadParts` falls back to primitives per part. Engine (x,y,z) is modelled at Blender (x,-z,y);
  the app logs each part's extents as the orientation check. `table.glb` deliberately waits for
  the layout to freeze; TableBuilder (now with `MakeTube` wires) is the static machine.
- Mirrored flipper = same bat along +X rotated by 180 - angle, never a bat along -X.
- Camera shots are solved by `MakeShot` from two points + elevation; don't type camera literals.
- Trap: with the mingw PATH set for a build, `python` is MSYS's and has no PIL - run the tools in
  a separate call (also in CLAUDE.md now).
- Stage 1 next: colliders + ball + flippers + tunnel guard; see findings §4 for the three
  physics questions (habitrail U-turn radius 0.34, 0.46 drop off the habitrail ends, orbit ->
  upper flipper hand-off). Related: [[shared_build_output_coordination]], [[per_app_build_layout]].
