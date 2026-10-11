---
name: chasm-person-picking
description: "Chasm people are picked by the engine's G-buffer readback (pickable figures + person_objects), 8 px near-fallback; skinned outlines in core; tools pin the pick pixel"
metadata:
  node_type: memory
  type: project
  originSessionId: 6bcdca7d-dfbb-495a-ba72-ee961d202a1e
  modified: 2026-10-11T09:53:05.967Z
---

Built 2026-10-11 at the user's request (selection_plan.md, "Picked by the figure"; "Selecting someone indoors" As built).

- People (static figures, rigged skeletons, their loads) are PICKABLE; `person_objects` (UploadWorkers, under
  pick_mutex) maps object id -> person. `PlayPickUnder`: the hovered object first, else nearest figure middle line
  within PERSON_NEAR_PX (8) unless the G-buffer surface under the cursor is nearer the eye. Badges are NOT a target
  (user's call).
- Old method measured bad: badge drawn 20-80 px above the 14 px waist circle, so clicking a badge selected the
  building behind or cleared the selection.
- Person hover = full light blue (PERSON_HOVER_COLOUR), beats the related colour; buildings keep the faint hover.
- House/camp card family lines: `Card::Person` -> OVERLAY_HIT_FAMILY selects (view stays); hovering a line or a
  neediest row makes that person the hover; a person selected at home is shown by his house's related outline.
- Core: outline pass now draws SKINNED meshes in pose (outline_mask_skinned.vert, Renderer::AppendBoneData /
  SkeletonOf); the line where two outlined objects meet goes on the farther one in the nearer one's colour.
  Before this, rigged adults had no outline at all (outline was set on their hidden static object).
- Core: `InputController::PinPickPixel` / GetHoveredPixel / GetPickSerial. `chasm_select` under/press/hover pin it
  and wait (WaitForPickAt), so a MINIMISED app's tools get real G-buffer picks (figures, castle decks). `under`
  returns `figure` and `read`; `hover px,py` pins the scene hover too and returns it.

**Why:** the user is adding RTS-style control of soldiers; selecting them must work at every zoom.
**How to apply:** test picking with `chasm_select op under` (now exact), not by projecting points. The first
`chasm_view play` after a fresh start glides the camera - set the view again before measuring. MULTI-SELECT AGREED 2026-10-11, STEP 1 BUILT the same day (PLAY_PICK_GROUP + play_group, SelectPeople/
ToggleSelected/SelectedPeople, play_people_before so Shift on a house card's name adds to the group picked before
the house; GroupCard/CARD_PERSON; right click sends all to ONE plot until step 3); STEP 2 BUILT too (left drag = box, PeopleInBox/TakeBox; select-mode click now acts on RELEASE within
RIGHT_CLICK_SLOP; chasm_select op box, `hold` to draw it; real-mouse drag not script-testable); STEP 3 BUILT too: places on one plot are a RULE (EconomyPostPoint by rank, RePlace on join/leave),
the SPREAD is the rules' too (Economy::SpreadPost via CHASM_CMD_ORDER value[2], reachability via CanWalkPosted -
a view-side spread sent people into unwalkable pockets), one refusal line per group (group_orders). All 3 steps done: selection_plan.md "Several people
selected" - purely view state, grown-ups only, Shift+click / box / card names, click on ground clears, group orders space
people on one plot (rule change in EconomyPostPoint) or spread; 3-step build order, each step on the user's go-ahead. Related: [[ramparts-plan]], [[chasm-defence-plan]],
[[minimized-focus-leak]].
