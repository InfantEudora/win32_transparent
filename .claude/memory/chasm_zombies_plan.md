---
name: chasm-zombies-plan
description: "Chasm zombies (apps/chasm/docs/zombies_plan.md): user decided 2026-10-14; steps 1, 1b, 2, 3 (drop+chase, spacing, timed meals+fight, archers/guards shoot) BUILT; walls break next; guard-pulled-off-tower staffing issue open"
metadata:
  node_type: memory
  type: project
  originSessionId: f701fae2-aaab-4d87-a065-27df7cdd3f2c
  modified: 2026-10-14T10:32:02.659Z
---

Talked through 2026-10-13/14. **The user's decisions** (2026-10-14): damage off the HEALTH bar (no second bar, to
start); zombies go for PEOPLE ONLY; the dead do NOT rise; waves of TENS (a colony has 1-2 archers early); the
figure is `zombie` in chasm_props.glb, unrigged, no clips yet (gets the man's later); start from a DEBUG TOOL
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

**Next (each on the user's go-ahead)**: 4 walls break (time-to-break cost, flow field). Also open: fleeing/alarm,
waves from the swamp, zombies' stop points spread round a target, arrows as tower stock.

**Why:** the colony is 12 people with no immigration, so combat is designed to be won by placement, not by
trading lives (Stronghold/Bad North, not They Are Billions).

**How to apply:** read zombies_plan.md before touching zombies; update wiki/zombie.md with each step. Related:
[[chasm-defence-plan]], [[chasm-range]], [[chasm-game-plan]].
