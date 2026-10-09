---
name: crates-plan
description: "AGREED 2026-10-09 chasm crates (apps/chasm/docs/crates_plan.md) - dropped loads as give-only stores, caretakers move them, a few seeded finds; crate_a prop modelled, no code yet"
metadata:
  node_type: memory
  type: project
  originSessionId: 47e6cba0-a787-423c-b97b-1ca2ffa6b50e
  modified: 2026-10-09T12:47:20.167Z
---

Chasm crates, agreed with the user 2026-10-09 in `apps/chasm/docs/crates_plan.md`; nothing in the game
built yet. The user's decisions: a crate is a STORE VILLAGERS ONLY TAKE FROM, destroyed when empty;
moving crates to a storehouse is the CARETAKERS' (builders') errand, LAST on their list, not the jobless's; building over a
crate is refused with a message; A FEW finds are generated from the seed, and debug can spawn more;
holding (WORKER_HOLDING) becomes dropping for EVERYONE; one crate look for everything.

The drop hooks where defence step 2 currently throws the load away: `Economy::Order` (`k->carry = 0`).

Prop: `crate_a` in art_source/chasm/chasm_props.blend's Export collection (100 tris, path-cell panels,
dark-bark frame and braces, 0.52 x 0.42 x 0.40), made by a background-Blender script and saved in place;
preview art_source/chasm/previews/crate.png. NOT yet exported to chasm_props.glb - step 1 does that.

**Why:** the user's fix for a posted carrier's load (lost at the order since defence step 2), and the
small start of They Are Billions-style finds under the clouds.
**How to apply:** build in the plan's order (drop, take/move, card, finds, holding); still open: only
FETCH FIRST and the badge. Related: [[chasm-defence-plan]],
[[chasm-game-plan]].
