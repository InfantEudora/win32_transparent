---
name: chasm-yards
description: chasm yards (docs/yards_plan.md) - gardens/lots owned by a house or woodcutter via ZoneState::ground_owner; select a building then paint; BUILT 2026-10-09
metadata:
  node_type: memory
  type: project
  originSessionId: 962d00b9-0f7e-485d-b7ba-0758261e2ced
  modified: 2026-10-11T10:50:19.607Z
---

Built 2026-10-09 with the user's agreement (docs/yards_plan.md). Gardens and lots belong to a building:
- a house owns gardens and lots;
- a woodcutter owns lots only;
- no other kind owns ground, and roads and walls are never owned.

How ownership is set:
- **The selected building:** paint with it selected. Each plot must touch the building or its yard.
- **The card's chips:** "Add lot" / "Add garden" take up the tool and keep the building selected.
- **Automatic:** with nothing selected, a plot goes to a building it touches (lowest id), else to the owner of
  yard ground it touches, else to nobody.
- **A new building** takes the ownerless ground beside it (flood fill), so "lot first, then the hut" still works.

A woodcutter can now be built with no lot. His woodpile is a built lot of his own beside the hut; without one
he fells nothing (card warning + issues row 8). The owner travels in CHASM_CMD_ZONE value[3] = owner*2 + play
(see ZoneCommandPlay/ZoneCommandOwner). Any new writer of value[3] must go through ZoneCommandValue3.

Postponed by the user: a house's lots extending its storage (their idea), and what gardens do.

EXTEND (the user's, 2026-10-10; yards_plan.md "Extend"): the house card's "Extend" chip takes up the house tool
with the house selected; any building tool with a same-kind building selected sends it as the command's owner and
Zones::Apply extends that one or refuses (ZoneCanExtend, ZoneJoinCorner). A building tool's click storey is sent on
the RELEASE (paint_press_plot), so a drag from a building no longer adds a floor. The old drag-from-a-building way
was kept; the user may want it gone - ask, don't assume.

SITES SELECTABLE + STAKED (built 2026-10-11, yards_plan.md "As built (2026-10-11)"): a click selects the PATCH
(same ground + owner, joined, built or not, while any plot is a site) as PLAY_PICK_GROUND by its lowest plot; no
Build first for ground. Both were MY calls on the plan's open points, offered to the user to overrule.

**Why:** the user wanted ownership to work like fields' land, and the woodcutter to behave like a shut-in
house (buildable, just not working).
**How to apply:** new "a building's own ground" features hang off ZoneGroundOwner / ZoneYardPlots; widen
ZoneOwnsGround for new kinds (store, builders) rather than adding parallel rules. See [[chasm-game-plan]],
[[chasm-issues-panel]].
