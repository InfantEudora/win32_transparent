---
name: chasm-issues-panel
description: chasm NEEDS ATTENTION panel (docs/issues_plan.md) - top-5 issues over the build grid; a crop left unchosen defaults to AUTO after a day; BUILT 2026-10-09
metadata:
  node_type: memory
  type: project
  originSessionId: 962d00b9-0f7e-485d-b7ba-0758261e2ced
  modified: 2026-10-09T12:10:52.514Z
---

Built 2026-10-09 with the user's agreement (docs/issues_plan.md). NEEDS ATTENTION sits over chasm's tool grid
(bottom right). It shows the top five rows by tier: danger (a hint under 2 days), countdown, stuck, next step.
Its first eight rows:
- the field crop countdown;
- build water / field / woodcutter / houses / store, the progression hints;
- nobody at a workplace;
- a woodcutter with no lot.

The list is DERIVED every frame in ApplicationChasmIssues.cpp `ChasmIssues`, never saved. A new row is a case
in that function plus a row in the plan's table. The `chasm_issues` MCP tool lists every row.

The ONLY rule in it is the crop default: `EconomyField::wait` counts ticks (saved, hashed). At
`ECONOMY_CROP_GRACE` (one day) the app sets the starting crop to ZONE_CROP_AUTO on the same tick, without
recording it, the same way the builders' raises are applied.

The user agreed to the hints being IMPERATIVE ("BUILD A STORE") and condition-based, not one-shot. Teammate
window ff (births work) was to add HOUSE CROWDED / HOUSE SHUT IN rows, which they said the user asked for.

**Why:** the user wants the list both for forgotten choices and as the tutorial / "what next" progression.
**How to apply:** future "the player should be told X" asks go here as rows, not as new HUD panels. Only a
row that changes the run needs simulation state (ticks, save, hash); the rest stays derived.
See [[chasm-game-plan]].
