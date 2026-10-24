---
name: chasm-flight-plan
description: "Chasm zeppelin flight (apps/chasm/docs/flight_plan.md, steam_plan step 6): step 1 BUILT 2026-10-24 (it flies); people next; the user's answers on orders, masts, no pilot, winter"
metadata:
  node_type: memory
  type: project
  originSessionId: 8a43c07d-a515-47eb-b112-6ad10766c43d
  modified: 2026-10-24T15:49:35.961Z
---

Talked through 2026-10-24. The user called my proposal "already quite perfect" and added:
- **Ordered by click**, like people.
- **People in and out only when ordered**, as at a winch. Away from a mast, only their packs and tents go with them.
- **Goods are delivered to the zeppelin only at a MAST.** Unloading goes into the mast's store, or drops a crate anywhere else.
- **No pilot job.**
- **It flies always, winter too.** Lift by the chasm's warmth was "too much for now".
- **The dock is a build site**: once its zeppelin is finished, a button begins a new one.
- **The dock is also a mast**, functionally: goods are loaded there and unloaded into its store.
- **The cabin is the passengers' pantry**: packs run down as on a post, and passengers refill from the cabin's stock as a guard does from his tower.

Agreed as proposed:
- The zeppelin is a vehicle in the world, saved and hashed, not a walk-graph link like the winch.
- It flies over the open chasm, and over land only on its last leg into a dock.
- It clears the clouds from the air.
- Routes come later, with porters.
- No fuel, and no losing one at first. The winter storm is the favourite cause for later.

The build order: flies, then people, then mast and goods, then the new-zeppelin button, then island seams, then routes, then the far rim and losing one.

**Why:** flight is the path to the islands' floatstone, then the far rim and the desert ([[chasm-steam-plan]], [[chasm-settlements]]).

**Step 1 BUILT 2026-10-24.**
- Code: rules in Flight.cpp (EconomyZeppelin in EconomyState::fleet; SendZeppelin; TickFleet; CHASM_CMD_ZEPPELIN), view in ApplicationChasmFleet.cpp. New props `zeppelin_body` and `zeppelin_propeller`.
- Lesson: docks lie ALONG the rim, so their nose runs parallel to the edge. Leave and arrive sideways to the nearest point over the chasm (DockOut), never along the nose: the nose sent it 37 units over land.
- Tests: scratchpad fly.py / fly_ui.py / probe.py; saves/zeppelin_fly_mid.json is a flight in progress.

**How to apply:** step 2 (people aboard) next, on the user's go-ahead.
