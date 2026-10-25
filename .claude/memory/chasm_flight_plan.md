---
name: chasm-flight-plan
description: "Chasm zeppelin flight (apps/chasm/docs/flight_plan.md): steps 1-3 BUILT (flies, passengers 2026-10-24; mast + goods 2026-10-25); step 4 new-zeppelin button next"
metadata:
  node_type: memory
  type: project
  originSessionId: 8a43c07d-a515-47eb-b112-6ad10766c43d
  modified: 2026-10-25T11:39:31.257Z
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

**Step 2 BUILT 2026-10-24 (passengers).**
- WORKER_ABOARD state; EconomyWorker::zeppelin and deck_y; the cabin is EconomyZeppelin::cabin.
- Aboard people KEEP their post plot, so they stay posted (out of staffing). They are left out of post places, the ice check, spacing and zombie reach.
- Any Order steps someone off via StepOff, and only while it is down.
- A gangway (not a ladder): set down, the deck is a step over the edge.
- Tests: scratchpad fly2.py. saves/zeppelin_passengers_mid.json has passengers in flight.
- Child (not baby) going along is untested: add_person makes grown-ups only.

**Sounds and view added 2026-10-25, at the user's request.**
- Cues: `zeppelin` (a loop following the nearest one in the air) and `zeppelin_steam`, from SignalZeppelins.
- The flying take's seam was crossfaded so it loops cleanly; the raw take is airship_flying_unlooped.wav.
- `tools/sound_ogg.py --only` was added so only those two wavs were converted.
- Selecting a zeppelin fades the camera height to its deck. It uses `cam_ground_y`, kept apart from the camera, so the view can come back down afterwards.

**Step 3 BUILT 2026-10-25 (the mast and goods).**
- ZONE_KIND_MAST (20), one plot at a drop (the steam collector's rule). Its look IS the dock's `dock_mast` prop, placed in the moored zeppelin's frame: no shed body. A shed+pole in code was tried first and looked poor.
- Moors nose-in, out over the drop, origin at ground. The landing is the open plot beside the mast, kept in EconomyZeppelin::plot while moored (orders onto building plots are refused).
- Mast and dock store: ECONOMY_MAST_ROOM 60, a source like a store (EconomyKindStores) but accepts nothing. The plan's "room by floor area" gave about 20, too small.
- Cargo and load list: builders load via FindSiteWork, PUT FIRST ahead of construction (distance-only ranking lost to nearer sites). Delivery fills the cabin first, then the cargo. Unload (CHASM_CMD_UNLOAD) goes into the store, the rest as a CRATE_CARGO crate.
- Tests: scratchpad setup3.py / cargo.py / mast_try.py. saves/zeppelin_mast_test.json is a flight with cargo and a list.
- Untested: island settlers building and loading their own mast.

**How to apply:** step 4 (the dock's "Begin a new zeppelin" button) next, on the user's go-ahead.
