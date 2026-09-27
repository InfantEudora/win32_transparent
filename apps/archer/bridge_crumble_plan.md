# Bridge and Crumble Plan

Two pieces of level that fail under her: a **rope bridge** that sways, groans under hard landings
and snaps - from overload or because she reached a spot - and **crumbling rocks** that give way a
moment after she stands on them, so a crossing is a run of quick hops. Talked through 2026-09-27;
nothing here is built yet except where a section says so.

Both are blocked out in the main level like everything else, past the branches. What they look
like is a later question; this plan is the rules, the blockout, the warnings and the sounds.

---

## Decided

- **Overload snaps the bridge WITH WARNINGS**, and the warnings are audible: a creak that follows
  the load, then cracks as it weakens, then the snap. Never a snap out of nowhere.
- **The first trigger is reaching a spot** - a zone, see below. The arrow cut is a later trigger.
- **The sway is looks only.** Balance already exists, on the branches; the bridge does not add a
  second one.
- **Crumbled rocks are gone** until the level restarts. A failed crossing leaves her needing
  another way over, so every crumble crossing has a detour route that works with all of its rocks
  gone, and the rules test proves it.

---

## The split: both live in the rules

The same reasoning as `plant_mechanics_plan.md`, and the deciding fact is sharper here: **she can
only stand on what `Stage` knows about.** The rules integrate her, and a physics prop reaches them
only as an axis-aligned box she bumps into (`StageObstacle`). A plank tilting on a hinge is not
that, a replay would walk through it, and `make rules` could never prove a crossing.

So both are declared in `Stage.h`, built into the level by `BuildMainLevel`, stepped in
`Stage::Tick`, deterministic, and read by the app for drawing - the spring plants' pattern exactly.

**rp3d gets what falls AFTER a failure**, which nobody stands on: a crumbled rock's debris and, on
a snap, a plank or two knocked loose. That is the breakable wall's path, `BreakBlocks` and
`SpawnDebris`, and it is reused rather than copied.

**The cue plan's rule holds throughout** (`cue_plan.md` section 2): a snap, a crumble and a zone
firing CHANGE THE GAME, so they are Stage effects, tested and replayed; the creaks, cracks, rattles
and shakes only PRESENT them, and are cue rows in `assets/cues/archer.json`.

---

## 1. The trigger: a zone

"She reached this spot" is exactly the zone of `cue_plan.md` section 8: a rectangle in the rules,
the same shape as a `StageBlock`, that reports `entered`, `left` and `stayed` off her body box. That
plan has it as its step 8, not built yet.

**This plan builds only the part it needs**, to that section's spec so the cue work grows from it
rather than beside it: the rectangle (centre, half extents, a name, an id), its `entered` report as
a Stage event, drawing as an outline in the F2 blockout view, and a test. How-often, flags,
conditions and the narrator stay with the cue plan's step 8. Agree it with whoever holds the cue
plan before building, so there is one zone type.

**What a zone does here** is a Stage effect with a delay in ticks: `snap bridge B at link k after N
ticks`, `start crumble group G`. A short list on the zone, not a scripting language - the same
restraint as the cue plan's conditions.

### The arrow cut, later

A rope bridge hangs from ropes tied to a post at each end. Shooting that rope where it meets the
post cuts it and drops that end of the bridge. It is `plant_mechanics_plan.md`'s "cutting with
arrows" idea: the arrow already hits things the rules know about (`StageArrowImpact`), and the
anchor becomes one more thing it can hit. Not in the first steps.

---

## 2. Crumbling rocks

A new block kind, **`BLOCK_CRUMBLE`**, with a small state beside it:

| state | what happens |
|---|---|
| **intact** | an ordinary solid block: she stands on it, arrows hit it, props rest on it |
| **shaking** | started the tick she lands on it, or by a zone. It still holds her, for `shake_ticks` (about 20-30) - this is the warning, seen and heard |
| **gone** | `f_alive` cleared. It goes out as an event, exactly as a kicked wall does (`broken_blocks`), so the app takes out its collider, hides it and throws its debris |

- **Once started it goes**, whether or not she stays on it. That is what makes it a run.
- **Gone is gone** until `NewGame`, which rebuilds the level - so a restart, and a recording's start
  state, bring every rock back.
- **Ticks throughout**: the shake is counted in ticks, so `sim_step` walks through it and a replay
  crumbles on the same tick.
- **Which block she stands on.** The rules need to know it to start the shake - the sweep already
  finds the ground under her; it records the block index as well.
- **Kept out of the terrain.** Crumble blocks are not melted into the marching-cubes field, and
  grow no plants or rocks: they are their own meshes, drawn as plain shaking boxes in the blockout.
  (Terrain cannot be remeshed from the physics thread in any case - see the note on `NewGame`.)

**Two layouts from those parts:**

- **Stepping stones**: a row of small crumble blocks over a pit, a hop apart. Each lasts
  `shake_ticks` after she lands, so the crossing is a run of hops with no stopping.
- **The chase**: a floor of crumble blocks with a zone at its start. Entering it starts the whole
  group, each block `k` ticks after the one before, so the floor falls away behind her at a set
  speed - slower than her run, with a margin the rules test measures.

**The detour.** Under each crossing is a pit with a way back up to the far side - a ledge to catch,
a pad, a tree - that works with every rock gone. Harder and slower than the stones, never
impossible.

---

## 3. The rope bridge

**A chain of planks between two pinned anchors**, stepped in the rules every tick: points along the
span, each plank a fixed-length link, integrated under gravity and held to length by a fixed number
of constraint passes (a position-based, Verlet chain - the same idea as the testbed's hinge chain,
without the solver). A fixed pass count, so it is deterministic.

- **It sags** under its own weight, more where she stands, and bounces when she lands: her weight
  is a load on the two points either side of her, and a landing adds her fall as a push.
- **She stands on it as a surface**: a fourth `SurfaceKind` beside plants, branches and ramps. Its
  height under her is read off the chain, and its vertical speed from the points' speeds, so the
  landing, the ride and the step-off all come from `Stage::CollideSurfaces` unchanged. A sagging
  bridge is sloped near the ends, and a steep enough stretch slides her by the existing slip rule.
- **One-way, like a branch**: landed on from above; Down drops through it.

### Strain, warnings and the snap

Each link carries a **strain** from 0 to 1, built from how hard it is pulled beyond what the bridge
comfortably carries:

- **Walking and standing add nothing.** The comfort level is above her weight at rest.
- **A landing adds by how hard it was** - her fall speed, and the pumping stomp (Down held through
  the fall, as on the pad) adds more. A drop onto it or a stomp is what hurts a bridge.
- **Strain does not heal** within a run. A restart rebuilds the bridge.
- **Warnings at thresholds**, each reported once as an event with the link and the level:
  `bridge_strained` (about a third), `bridge_cracking` (about two thirds). Two or three hard
  landings to go from sound to snapped - tuned in the rules test, not by feel alone.
- **At 1, that link snaps** - `bridge_snapped`, with the link and where. Its collision goes, and a
  plank or two are thrown as rp3d debris.

**After the snap** the two halves hang from their anchors and are still simulated: they swing down.
A half stays a surface while its slope is walkable - she slides by the slip rule, or falls off as it
swings steeper. Climbing a hanging half, the way she climbs the rope, is a later idea.

**The zone-triggered bridge** is the same bridge with a zone at its middle: entering starts a short
countdown (strain set to the cracking level, so she hears it go), then the snap. A dash to the far
end makes it; stopping does not.

### The sway (looks only)

A spring of its own per bridge: how far the middle of the span has swung toward or away from the
camera, with a frequency and a damping like the plants' (`hz`, `damping`). Her footsteps, her
landings (sideways by a little, by landing speed) and the wind at the bridge push it; the points
are offset in z by it, most at the middle and none at the anchors.

**It does not touch the rules.** The rules are 2D and never read z; her balance stays the branch's
business. It is stepped in the tick and sampled at `stage.ticks` like the camera shake, so it
replays, freezes under `sim_pause` and moves one tick per `sim_step`.

---

## 4. The sounds and the shake

Cue rows, in `assets/cues/archer.json`, on events the rules report. The files are to be authored.

| event | cue |
|---|---|
| on the bridge | a **creak held while she is on it**, like the bow's, its gain following the load and the sway - quiet walking, loud after a landing |
| a landing on the bridge | a heavier creak and a plank knock, by landing speed |
| `bridge_strained` | a groan: the first warning |
| `bridge_cracking` | a sharp crack, and a small shake |
| `bridge_snapped` | a snap and a rope twang, a shake scaled by the load, planks knocking below |
| crumble shaking | a **rattle and grind held for the shake**, scoped to that block, so it stops when it goes |
| crumble gone | a crumble - the broken wall's sound can serve until there is its own - and a small shake |
| the chase starting | a rumble, held while the group is falling |

The creak is the one to get right: it is the warning the whole overload design depends on. Held
sounds whose gain follows a value already exist in the cue system (the bow's tension).

---

## 5. The new section of the main level

Past the branches, where the right-hand wall stands at x 174..176 today. The wall moves to the new
end. Rough layout, settled when it is blocked out and proved by the rules test:

1. **The overload bridge** over a gorge with a detour out of it: cross it gently and it holds; land
   hard on it a few times and it goes.
2. **The zone bridge**: reaching the middle snaps it. Run.
3. **Stepping stones** over a pit, with the detour under them.
4. **The chase**: a floor that falls away behind her, onto solid ground at the end.

Jump numbers to design against (from `Stage.h`): a plain jump rises 3.2, her hands reach 5.0, she
runs at 9, and a running jump covers about 7 units.

---

## 6. Testing

In `make rules`, like the plants:

- **The bridge**: it sags at rest within its expected range; she walks across adding no strain;
  a plain landing strains it less than a stomp; the warnings come in order, each once; it snaps on
  the landing the tuning says and not before; after the snap the halves hang from their anchors;
  she on the broken part ends in the gorge; the zone bridge snaps the set number of ticks after she
  enters; two identical runs give identical chains.
- **Crumbling**: a stone holds for its shake and then goes; stepping off it early does not save it;
  gone stones stay gone until `NewGame`, and `NewGame` brings them back; the chase is outrun at her
  run speed with a margin.
- **Route checks.** The solver that found the pad-to-canopy recording (2026-09-27) becomes a test
  utility: a route is a start and a goal, and the test searches the jump timings and fails if the
  route is impassable or its timing window is narrower than a set number of ticks. Every crossing
  here gets one - the stones, the chase, each bridge gently crossed, and each detour with the rocks
  gone or the bridge snapped. The pad-to-canopy route is the first.

In the app: replays across each piece, and the cue log (`tools/cue_replay.py`) for the warnings,
so the creak-crack-snap order is checked as well as heard.

---

## Order

1. **The zone** (the minimal one, section 1) and **the route-check utility**, with the existing
   pad-to-canopy route as its first test.
2. **Crumbling**: the block kind and its state, the ground-block index, the app side through the
   broken-wall path, the stepping stones and their detour, the tests. First because it is the
   simplest, and it proves the zone and the break path.
3. **The chase**: a zone starting a crumble group.
4. **The bridge as a surface**: the chain, sag, walking and landing on it, the blockout planks.
5. **Strain, the warnings and the snap**, the halves after it, the loose planks.
6. **The zone-triggered bridge.**
7. **The sway**, then **the cue rows** and the sounds to author.

Later: the arrow cut on the anchor ropes, climbing a hanging half, the meshes.

---

## Step 1, as built (2026-09-27)

- **`StageZone`** in `Stage.h`: centre and half extents, a name, an id, and `arrive` - the feet
  spot a teleport to it uses. `Stage::zones`, declared by each level with `AddZone(name, left,
  right, bottom, top, arrive)`; `TickZones`, last in `Tick`, reports `zones_entered` and
  `zones_left` in `StageEvents`; `CurrentZone()` is the smallest one she overlaps. A restart
  forgets what she was in, so its first tick enters the start zone again. Nothing yet listens to the
  events - the cue plan's step 8 hooks them to cues.
- **The zones**: the main level's eight, side by side from the terrain bay to the end wall -
  Terrain bay, Start, Gaps and rope, Ledges and walls, Tree, Spring plants, Branches, Test ground;
  the range's one; the rope level's Rope and Slide gallery.
- **The test ground**: the main ground run now goes to x 266 and the end wall stands at 264..266,
  so x 176..264 is flat, empty and in its own zone - where steps 2 onward are blocked out.
- **In the app**: the zone she is in at the top of the screen (the panel's "zone label" box turns
  it off), in `archer_state` as `zone`, and as outlines in the F2 blockout view. The panel's Zones
  section has a button per zone of the live level; `archer_zone` lists them or goes to one by name
  or unique prefix. Both go through `ARCHER_CMD_ZONE`, resolved on the physics thread.
- **Route checks**: `RouteCheck.{h,cpp}`, engine-free, in `make rules`. The pad-to-canopy route is
  the first: passable in 5.8 s, windows onto the pad 20, shelf 9, leaf 31, canopy 15 ticks, against a
  minimum of 4 (`ROUTE_MIN_WINDOW`), and its solved keys replayed from the start arrive.
- **A cost to know about**: the wind field covers the whole level, so the longer ground makes its
  solve larger - 637x88 nodes and 934 iterations, where it was 457x88 and 740. Measured in the app:
  the "wind" loading step 229 -> 398 ms, the whole load 2.37 -> 2.61 s. Startup stays that much
  slower until the field is limited to where the wind matters.

---

## Step 2, as built (2026-09-27)

- **`BLOCK_CRUMBLE`** with `StageBlock::crumble_ticks` (-1 whole, else ticks shaking) and
  `CRUMBLE_SHAKE_TICKS` 24. `Stage::TickCrumbles`, after she moves: the stone she is standing on
  (`StandingOn` - on the ground, feet at its top, over it; tested directly rather than threaded out
  of the sweep) starts; a shaking one counts on whether she stays or not; at 24 it goes -
  `f_alive` cleared, and any arrow stuck in it let go to fall. Events `crumbles_started` and
  `crumbled_blocks`. Nothing grows on or heaps against a crumble stone, the terrain does not melt
  it and the wind does not see it (Boulders' `IsWall` needed telling; the rest already only
  counted their own kinds).
- **The stepping stones**, in the test ground: the ground run now ends at 196; a pit to 214, its
  floor at -4; four stones across at 199.5, 203.5, 207.5, 211, level with the ground; and the far
  side from 214 is a LEDGE, so the detour out of the pit is a jump, a catch and a climb. Their own
  zone, "Stepping stones" (x 176..218, arriving at 192); "Test ground" is the rest, 218..264.
- **In the app**: the stones are sandstone, turn orange the tick she lands (`ShakeCrumbles`) and
  shake - the static BODY is moved a few hundredths each tick, since every body writes its pose
  over its object's; the rules still collide her against the block where it stands. Going goes
  through `BreakBlocks`: collider off, box hidden, rubble dropped rather than thrown. Signals
  `crumble_started` and `crumble_fell` (x) for the cue rows of step 7.
- **Tests**: a stone holds her for exactly 24 ticks and drops her into the pit; leaving early
  does not save it; untouched ones stay whole; gone stays gone; a restart brings them back; a stuck
  arrow falls with its stone. Route checks: **the stones are crossable** (5.0 s, windows 13, 10, 10,
  8 and 13 onto the far rim) and **with every stone gone the pit's far ledge is the way on** (3.8 s,
  the catch-and-climb window 24).
- **A wind bug the pit found**: the field is cut 3 below the lowest walkable top, and a pit floor at
  -4 pulled that below the ground runs' -4 bottoms - an open channel under the whole level, the
  ground a floating obstacle, and a jet under the tree's slab (wind_test's "no jets" failed at
  2.37 against 2.0). `Wind::BuildDistance` now makes every column solid below its lowest block,
  down to the floor plane; a gap with no floor keeps the plane alone. Fastest wind is 1.63 again.
- One existing ledge test picked the far rim as "a standable ledge" (it takes the last LEDGE low
  enough); it now wants one standing up out of the floor.

---

## Open questions

- The tuning: how many hard landings to a snap, the shake length, the chase's speed. The rules
  tests hold them once chosen.
- Whether a creak should also come from standing still on a strained bridge - a warning that keeps
  talking - or only from landings.
- Whether a snapped bridge's far half is ever part of a route (climbable), or only scenery.

---

## Status

| # | Piece | State |
|---|---|---|
| 1 | Zone (minimal) and route checks | BUILT 2026-09-27 - see "Step 1, as built" |
| 2 | Crumbling stones and detour | BUILT 2026-09-27 - see "Step 2, as built" |
| 3 | The chase | planned |
| 4 | Bridge as a surface | planned |
| 5 | Strain, warnings, snap | planned |
| 6 | Zone-triggered bridge | planned |
| 7 | Sway, cues and sounds | planned |
