---
name: chasm-test-maps-plan
description: "ALL 6 steps BUILT 2026-10-18 - map KIND in GridSettings (flat, title), chasm_economy add_person, title town coded in TitleTown.cpp; A/B: every save unchanged; menu_town.scap deletion is the user's call"
metadata:
  node_type: memory
  type: project
  originSessionId: aef268dc-ec2f-4c08-b164-560b63713f56
  modified: 2026-10-18T17:22:44.935Z
---

Plan: `apps/chasm/docs/test_maps_plan.md` (written 2026-10-18 at the user's go-ahead; as-built notes under each step).

**Built 2026-10-18:** `chasm_generate {"kind":"flat","target_fine_cells":4000}` (test map: no chasm, trees, nobody);
`chasm_economy op add_person` (CHASM_CMD_PERSON, ordinary villagers; user chose homeless ALLOWED, the rest as
recommended); `kind:"title"` = the title's town, built by code at Init (`ApplicationChasm::BuildTitleTown`,
`TitleTown.cpp`) and captured IN MEMORY - the title loads no file. The A/B (step 4) passed after steps 3, 5 and 6:
60-seed sweep identical, all 17 saves byte-identical, a 6000-tick zombie replay equal. `apps/chasm/build/chasm_before.exe`
is the A build for re-runs.

Bug found on the way: title-first (ship) builds drew summer broadleaves MAGENTA - the palette's summer row uploads
only with the first season fade in the world's PreRender. The title branch of PreRender now calls UpdateSeasonPalette.

GOAL SWITCHES (user, same day): debug panel "Goals" + `chasm_menu {"triggers":{"death":false,...}}` turn off what
play mode tells/stops for (goal notices, death pop-up, lost/complete pages). VIEW ONLY on purpose - goals stay
rules journaled as events, so replays/stats unchanged; session only, all on after a restart.

User's calls: the IRREGULAR grid stays (no square grid); the diorama from the same means.

**Why the kind must be in GridSettings, not an app flag:** `LoadSave` and every replay regenerate from the
save's settings, and `Economy::Restore` keeps `Economy::Reset`'s freshly made settlers when the save has no
people. So a flag kept elsewhere brings back a chasm map plus 12 settlers on every load and replay.

**How to apply:** test in debug mode first, since play mode on a map with no villagers is a dead end (clouds,
territory, locked tools, the death pop-up). Run step 4's A/B again after any change that touches a shared path.
A homeless person is never employed: shooters need a house. Background `cd X && (a) & (b) &` only cds the first job.
Related: [[chasm-game-plan]], [[worktree-ab-build]], [[chasm-demo-menus]], [[chasm-birds-plan]].
