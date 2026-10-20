---
name: chasm-zombies-plan
description: "Chasm zombies (apps/chasm/docs/zombies_plan.md): user decided 2026-10-14; steps 1, 1b, 2, 3 (drop+chase, spacing, timed meals+fight, archers/guards shoot) BUILT; 1c crowd (target sharing, attack slots, following) BUILT 2026-10-20; 3b guards hold their posts BUILT 2026-10-18 (tools/chasm_guard_test.py); step 4a done, 4b + 4d BUILT 2026-10-19 (walls/gates/every door broken, scratch clip, sounds, the harmed look); 6 THE FALLEN BUILT 2026-10-19 (death clips, bodies, angels, crows on carrion); 4c, 4e + 5 alarm next"
metadata:
  node_type: memory
  type: project
  originSessionId: f701fae2-aaab-4d87-a065-27df7cdd3f2c
  modified: 2026-10-20T18:55:21.466Z
---

Talked through 2026-10-13/14. **The user's decisions** (2026-10-14): damage off the HEALTH bar (no second bar, to
start); zombies go for PEOPLE ONLY; the dead do NOT rise; waves of TENS (a colony has 1-2 archers early); the
figure is `zombie` in chasm_props.glb, unrigged at first - rigged and drawn skinned with the man's Walking + Idle 2026-10-18 ([[chasm-villager-animation]]); start from a DEBUG TOOL
dropping zombies into the running scene.

**Step 1 BUILT 2026-10-14**: `EconomyZombie` in `EconomyState::zombies` (rules in `Zombies.cpp` as Economy
methods), `CHASM_CMD_ZOMBIE` (subtype plot, value[0] count, 0 clears), Z key (debug builds, both modes), panel
"Zombies" section, MCP `chasm_zombie` spawn/clear/status, `WALK_WHO_ZOMBIE` (villager ground, no deck), hashed at
the END of the economy part only once any existed. Checked: replays equal, guard on tower ignored, lost across
the chasm. Not checked: Z key, walls stopping one, hidden under clouds.

**Step 1b SPACING BUILT 2026-10-14** (user asked for it before step 2): "the route is the plan, the offset is the
body" - `off` on EconomyWorker/EconomyZombie, body = `EconomyBody` (pos + off), `Spacing.cpp` SpaceOut after zombies,
same-kind push within SPACING_RADIUS (walkers sideways only), keep-right lean with look-ahead SPACING_AHEAD, zombies
pushed out of a person's SPACING_BODY (never the reverse), clamp SPACING_MOST, EconomyStraightClear from pos. Rules
read `pos`; view (WorkerHeight, FeetOf, figure, range shot, zombie drawing) reads the body. Step 2 bites should
measure body to body. Later: zombies' stop points spread round the target; crowd pressure for gate jams (step 4).

**Step 2 BUILT 2026-10-14** (user: people FIGHT BACK, drinking timed too, "combined" strength+ingenuity, ~3 s fight):
MEALS - `Consume` takes ONE unit and starts a meal (meal_good/ticks/worth), `MealTick` adds it over ECONOMY_MEAL_SECONDS
(food) / ECONOMY_DRINK_SECONDS (water); none starts or goes on with a zombie within NEEDS_MEAL_SAFE of someone a zombie
can reach (indoors/deck exempt); card shows the bar. FIGHT (`Zombies.cpp` Fights): zombie bites its target within
FIGHT_REACH every 0.5 s for a day of health; grown-ups hit the nearest zombie by EconomyFightPace (avg of the two
skill factors); ZOMBIE_HEALTH 60; bitten_dead -> NEEDS_DIED_ZOMBIE "WAS KILLED BY A ZOMBIE". Average fed person wins
1v1 in 3 s; hungry or vs two he dies. Nobody flees yet. Port 8769 may be the overseer/void window's - use 8774.

**Step 3 BUILT 2026-10-14** (user: bow-carriers who STAND shoot - guards, posted archers, trainees on the line; one shot
per 5 s Shoot-clip turn): `Shooting.cpp` Shoot/ShotTarget; arrows are RULES state (`EconomyArrow`, saved, hashed),
hit/miss by a hash of tick/shooter/zombie (never RRandom - [[rrand-shared-stream-todo]]), sight via
`ExplorationSight::Clear` + SightReach (tower deck = eye height), no arrow at a zombie already doomed by arrows in
flight; person `draw`/`aim`; view plays Shoot by draw, `SHOOT_RELEASE` = `ARROW_RELEASE`. Replays bit-exact.
**Open, flagged to user**: the job filling pulls an AUTOMATIC guard off his tower for another place (Lieve died
climbing down mid-wave when the archer died); bite-induced pressing hunger sends people off posts.

**Step 3b BUILT 2026-10-18** (the user's): every guard keeps his tower until RELEASED - `AssignJobs` keeps them as it keeps the hand-picked (posted + hand-picked first, then automatic, so a lowered count still drops automatic first); `ZombiesInRange` (Shooting.cpp: a zombie in FIGHT_REACH, or ShotTarget with NO arrows counted) stops a pressing need sending anyone posted off his post; `PackUp` (TakePost + GoHome) sends a guard with < NEEDS_PACK_WATER by the nearest water first, towers excluded (`SeekSupplyOf` f_towers). **The test: `python apps/chasm/tools/chasm_guard_test.py --port N`** against a DEBUG copy (replaces the map; flat test map, paused): shortage, death (Lieve), player, wave, pack, pack_errand - 5 failed before, all 6 pass after, ~1 min. Replays bit-exact. **Drinking between shots BUILT** (user's yes): `DrinkBetweenShots` (Shooting.cpp) holds the next Shoot turn for a 1 s drink when a whole water unit fits - tower water first, else pack; water only, food still waits. Test case `drink` (7 cases, all pass). The user is judging how shooting feels in the game first. Small findings stay as notes in the plan's "Noted, for later" list.

**Steps 4 + 5 DECIDED 2026-10-18, not built** (zombies_plan.md "Step 4: walls break", "Step 5: the alarm"): the user's - gate WEAKER than a palisade plot; houses BREACHED not broken (far less time, then passable to zombies); garden walls/lot fences a SLOW crossing (climbed, unharmed); ONLY BUILDERS repair, only with no zombies about; the ALARM shuts the gates and everyone not on guard HIDES in his house; gate jams for people later. My proposals in the plan: breaking as a route COST in seconds (EdgeOpen -> cost for WALK_WHO_ZOMBIE), strengths in zombie-seconds (wall 60, gate 30, house 10, climb 3), harm saved/hashed while any, a broken plot back to a wall site via the zones (like ground_raises) leaving half its wood as a crate, a FLOW FIELD for targets by walking time, sub-steps 4a-4e with tools/chasm_wall_test.py. The user, 2026-10-18: people in a BREACHED house ARE reachable (hiding buys time, not safety).

**4a DONE 2026-10-18, no rule change**: `tools/chasm_wall_test.py` (ring, gate_shut, gate_open control, through) on flat test maps - walls and shut gates stop zombies (they go LOST), and nothing reaches through: nearest body across a palisade 4.40 vs FIGHT_REACH 1.0, since bodies stand on plot points/walks and a palisade is a plot wide. A 'nothing between' rule for bites/blows was weighed and LEFT OUT; re-ask with `through` in 4b. add_person people on a test map log a ChasmStats check error each dawn (not born/settled) - for the test maps window.

**4b BUILT 2026-10-19** (+ the user's `Zombie_Scratch` clip, played while biting or hitting): WalkBreaks (Walkers.h) makes EdgeOpen cost a zombie the seconds left of a standing palisade plot / shut gate / standing house, climb 3 s over fences; the first such thing ends its route (stands IN a wall/gate plot's cell, 0.8 off its point; outside a house), ZombiesHit adds a tick a tick (<= ZOMBIE_HITTERS 4), state.harm (saved "harm", zombie "hit") -> ZONE_OP_GROUND_BREAK (wall back to a site) / gate opened + BROKEN (shut refused in ApplyZoneCommand) / house BREACHED (CanGetAt: people indoors reachable). Events wall_broken/gate_broken/house_breached. TRAP fixed: a zombie standing in the wall cell re-planned FROM the wall plot and walked through free - now it keeps its way while hitting and plans from the neighbour. tools/chasm_wall_test.py = 7 cases (ring, gate_shut, gate_open, round, house, fence, through), house case needs a REAL gap (a lone corner plot's neighbours join without it). Only houses breach - other buildings stay shut (flagged). Broken plots are rebuilt by the line works (jobless) until 4c.

**2026-10-19, every building's DOOR breachable** (the user's): ZoneKindHasDoor (house, store, woodcutter, water, builders, ropewalk, stonecutter, mine, well; NOT camp/winch/bridge/tower); renamed HARM_DOOR, ZOMBIE_BREACH_DOOR, EconomyBreached, WalkBreaks::DoorLeft, EVENT_BREACHED "breached" (amount = kind, ChasmKindLabel in the texts). **4d SOUND built**: ApplicationChasmSound.cpp SignalZombies - `zombie_strike` (bluntstrike_wood_1/2) once per scratch turn at 0.8 s (the clip's one swipe), clock in ECONOMY TICKS from the view's id phase; `punch` (punch_1-3) on each bite/blow the rules land (bite/swing count going up). Encoded the five with sound_ogg's settings ONLY (speech/warning wavs are someone else's). Bites every 0.5 s vs one swipe per 2.29 s: the user says the timing is fine for now. `near` is a Windows macro - never a variable name. Test scripts retry read-only MCP calls (an occasional request is never answered).

**4d LOOK BUILT 2026-10-19**: the harm reaches the zone chunks as ZoneHarmLook (ZoneMesh.h), worked out per frame in UploadZones like zone_field_stage, in ZONE_HARM_STEPS 8; chunks rebuilt when a mark's step moves (ZoneHarmChunks). Each wall/gate mark keeps `from` (hitters' middle when first seen) so things give AWAY from the blows. RoadMesh HarmedLog (logs lean, drop, broken = fallen + stumps, jambs stand), BuildGate (leaves give/sag; broken hang askew), BuildingMesh BreachedDoor (PAL_VOID doorway + leaf swung out); MeshTurn/MeshShift in MeshBuild.h. RULE CHANGE: a broken palisade plot KEEPS its harm at strength while a wall site (ForgetHarm forgets it once it stands again or is erased) - the view needs it, and 4c will. Wall test `ring` checks kept-as-broken and rebuilt-whole. TRAP: the test map's gate is TWO plots on the slant (door in 4 pieces, zombies hit one plot, standing along its leaf's line) - a gate plot takes its neighbour's mark, pieces turn with the leaf of the plot's one jamb, and 'away' is by the CHORD between a plot's wall neighbours, never the piece's own line. Checked with screenshots from straight above (pitch 89).

**Step 6 THE FALLEN BUILT 2026-10-19** (the user's: Die_Forward/Die_Backward on all 3 rigs; fall by the killing blow - from the front backward, keep facing; zombie lies 3 days, sinks the last; a person sinks after a few seconds and the props' `angel` rises): rules record EconomyFallen (saved "fallen", hashed 0xFA11E) by ZombieFalls/PersonFalls/TickFallen; facing shared via EconomyZombieLooks/EconomyWorkerLooks (a hand-fighter now faces the zombie). View: zombies keep their skeleton by id; people get a FallenFigure pool (worker skins are by INDEX); kids tip over; FALLEN_SINK_DEPTH 0.15 (clips lay the hips AT the ground). TRAPS: the economy snapshot's tick freezes when nothing changes - view/tools use calendar_shown; birds don't move under sim_pause+sim_step - test them unpaused; a flush's reach is a scare the next roost avoids. Crows: BirdWorld::carrion, 70% within 40 units, pecks. tools/chasm_fallen_test.py 6 cases.

**Step 1c A CROWD KEEPS ITS ROOM BUILT 2026-10-20** (the user's, after 20+ zombies clumped on saves/archers_test.json; all three suggestions approved, incl. the two fight changes): (1) target SHARING in ZombieLook - score = distance + ZOMBIE_CROWDED per zombie after him nearer him, own target ZOMBIE_KEEP_TARGET nearer (must exceed CROWDED or targets churn: 86 switches/8 s at 0.5); (2) attack SLOTS - `ZombiePlaces` every tick between looks and walks: rings round a person (first at ZOMBIE_REACH = biters), rows at what it breaks (front row only hits: EconomyZombieHitting checks ring<=0), greedy cheapest (distance + ZOMBIE_RING_COST/ring - ZOMBIE_SLOT_KEEP held), places validated by EconomyStraightClear from the target/`from`; `EndAt` moves the route's end; (3) FOLLOWING in SpaceOut (`Follow`) - `crowd` pace on worker+zombie (saved/hashed, walk clip rate too), only behind one walking AWAY (else mutual crawl), ~0 at contact, slowed passing a stander, overtakes slower (SPACING_OVERTAKE). Measured: 21/3 on one archer -> ~6 each; walking min 0.04 -> ~0.4-0.5; fight 0.01 -> 0.43+. All 4 chasm tests pass, replays bit-exact. New wiki page wiki/crowds.md (zombie/person pages were at the 45-line cap). Left: a broken gate funnels 20 through one plot (gate jam, later); brief crossings early in a drop.

**Next (each on the user's go-ahead)**: 4c (repair by builders, no zombies about, the crate), 4e (flow field). Older list: 4 walls break (time-to-break cost, flow field). Also open: fleeing/alarm,
waves from the swamp, arrows as tower stock.

**Why:** the colony is 12 people with no immigration, so combat is designed to be won by placement, not by
trading lives (Stronghold/Bad North, not They Are Billions).

**How to apply:** read zombies_plan.md before touching zombies; update wiki/zombie.md with each step. Related:
[[chasm-defence-plan]], [[chasm-range]], [[chasm-game-plan]].
