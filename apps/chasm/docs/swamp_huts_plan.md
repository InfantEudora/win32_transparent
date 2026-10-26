# Swamp huts: where the zombies come from

Asked by the user on 2026-10-26. The zombies (`zombies_plan.md`) can bite, break walls and be shot, but they come only
from a debug tool. P6 in `gameplay_plan.md` always had them coming "from the swamp", and nothing in the swamp said so.
This plan gives them a home there, and then a first wave out of it.

## What the user asked (2026-10-26)

- **A few solitary, shabby huts in the swamp** are where the zombies come from.
- **The first thing in the demo**: zombies come out of them **after the first year** and **creep up on the village**.
- **Not modelled in Blender**: generated in code, the way every building on the terrain is.

## Step 1: the huts (as built, 2026-10-26)

### In the rules: a building that is nobody's

- **A hut is a building**, kind `ZONE_KIND_HUT` (21), one plot and one storey. It is placed when a new colony starts,
  the way the start camp is: `Zones::PlaceSwampHuts`, right after `PlaceStartCamp`, through `Zones::Apply` and the same
  rules as any building. It is standing from the first tick and never a site. So it is saved, loaded, hashed and
  replayed like any building, and nothing new was needed in the save.
- **It is nobody's** (`ZoneKindWild`):
  - no tool paints one;
  - in play, the erase tool passes over it ("the swamp hut is not the colony's to take down");
  - no garden or lot goes under it, and no house tool adds to it;
  - it employs, houses and holds nothing;
  - it never joins or grows a settlement's land, even with a settlement around it (`Economy::Settle`);
  - it is not in the statistics' buildings.
  - Debug may still paint and erase one.
- **It has no door anyone uses**, so walkers and zombies treat it as a wall (`ZoneKindHasDoor` stays false). The
  **plot before its door** (`ZoneHutDoor`) is where whatever comes out steps down. That plot is the dry neighbour that
  is most open, chosen by the ground alone so it never moves. A zombie can be dropped there: `SpawnZombies` refuses a
  building's plot.
- **Under the clouds** it is hidden, like everything else in the swamp until someone explores there. Its card says
  "Swamp hut / Nobody you know lives here".

### Where they stand

`ZoneSwampHutSpots` scores every swamp plot, and `ZoneSwampHutPlots` takes the best, each `ZONE_HUT_APART` (30) from
the ones already taken, up to `ZONE_SWAMP_HUTS` (3).

- **May stand** (`HutIsBuildable`) on a swamp plot whose own point is dry. It is on stilts, so unlike a house its
  corners may be in the wet, and it may take a foundation's slope. There must be a dry plot before its door.
- **Refused** within `ZONE_HUT_EDGE_LEAST` of the map's edge, within `ZONE_HUT_CAMP_LEAST` (70) of the camp, and with a
  tree on a plot beside it. A hut stands in a clearing: the first look had a willow growing through one's wall.
- **Scored** on:
  - **deep**: in from the swamp's edge, full marks from `ZONE_HUT_DEEP` of its radius in;
  - **by the water**: best with `ZONE_HUT_WATER_BEST` of the ground within 5 units wet, so its stilts stand at a
    pool's edge rather than on a dry hummock or among the pools;
  - **a little of the seed's own**, so alike swamps don't put their huts alike.
- **A hut whose zombies can walk to the camp is taken first**: a walk from its door over the ground as it is, with no
  bridge. Where none can, the best are taken anyway.

On the four maps New game offers:

| Map | Huts | From the camp, straight | Can its zombies walk to the camp? |
|---|---|---|---|
| Seed 12 | 3 | 134, 161, 184 | yes - every spot in the swamp can |
| Seed 3 | 3 | 118, 121, 150 | **no** - a river lies between, and no spot in its swamp can |
| Seed 27 | 3 | 120, 124, 148 | yes - every spot can |
| Seed 41 | 3 | 213, 226, 231 | yes for these three; 694 of its 2,033 spots can |

The villagers' own route-finder (`chasm_pick` with `to_x`/`to_z`) agrees on all twelve huts.

### How it looks (`BuildSwampHut`)

One room on stilts, knocked together from whatever came to hand and left to rot. Each hut is a little wrong in its
own way, chosen by its plot's hash:

- **Stilts**: seven crooked posts sunk in the mud, with a brace across the back.
- **Floor and porch**: a plank floor, and a porch in front of the door with one plank gone.
- **Walls**: planks of three woods and ragged lengths, an eighth of them missing so the dark inside shows through.
- **Window**: one, on a gable end, with a board nailed across it.
- **Door**: hanging open on the dark (the breached door's code), with a plank running down from the porch to the plot
  before it.
- **Roof**: old reed thatch gone grey-brown. The ridge sags in the middle and the eaves sag with it. The thatch hangs
  ragged at the eaves, and on some huts one strip of fresher reed shows where it was mended. Boards are nailed over a
  rotten patch, and there is moss low on the slopes.
- **Ridge**: the ridge pole and the rafters cross over each gable end.
- **Extras**, on some huts: a cold, crooked stovepipe; a pole with a rag hanging off it; the stakes of a fence long
  fallen.
- **Lean**: the whole hut leans up to `HUT_LEAN`, sunk on one side.
- **Colours**: all from the swamp's palette row, so it reads as the swamp's and not the colony's.

At the game's zoom it is a small dark shack among the willows.

### Files and checks

**Files**:

- `Zones.h` and `Zones.cpp`: the kind, `ZoneKindWild`, where huts may stand, the spots, `PlaceSwampHuts`, the
  play refusals, and no ground under a hut.
- `BuildingMesh.cpp`: `BuildSwampHut`, and the hut drawn apart in `BuildHouseCell`.
- `ApplicationChasm.cpp`: the huts placed at new game, and `chasm_huts`.
- `Economy.cpp`: no settlement land.
- `ChasmStats.cpp`: not counted.
- `ApplicationChasmSelect.cpp`: the card and the pick height.

**Checked** (debug, a copy on 8769):

- the four maps above, each with three huts;
- `chasm_check` passes on all four;
- a save with huts, loaded after another map, has its three huts back;
- six days run at speed 10 with nothing logged;
- a zombie dropped at a hut's door finds a person and walks to them: 28 units in its first minute, about 0.5 a second
  through the swamp;
- screenshots of all three huts on seed 12, close and at the game's zoom;
- the card, in play.

**Not checked**:

- a breach or a fight near a hut;
- a player's settlement grown into the swamp round a hut, by a run (the rules were read, not run);
- the release build.

**One thing changed for every new game**: a new map's zones now hold three more buildings, so a new colony's state hash
is not what it was. Saves and recordings made before are unchanged.

## Step 2: the first wave (proposed)

Not built. This is the design for "after the first year, creeping up on our village". The questions for the user are
collected below.

- **When**: the swamp thaws with year 2's spring (`gameplay_plan.md`: no zombies while it is frozen; spring is the
  dangerous moment). Proposed: the huts' doors open at **dawn on the second day of spring, year 2**, so not on the same
  dawn as "Through the winter" is met. `chasm_time set_day 41` reaches it.
- **How many**: proposed **two a hut** (six on a three-hut map), the smallest wave there will be. For scale, from
  step 3's sums:
  - a fed villager is a fair fight for one zombie;
  - a trained archer usually drops one walking at them from 20 units;
  - a colony starts with one or two archers.

  Six reaching an undefended camp would cost lives; two archers on a tower and a wall should hold them.
- **How they come out**: one at a time, `HUT_TRICKLE` apart (about 5 s), from the plot before each hut's door, each
  hut on its own beat. This uses the existing `SpawnZombies`, one zombie per call.
- **Creeping up**:
  - They walk for the colony as any zombie does: the nearest person, the wave sharing itself out (step 1c).
  - They are not drawn under the clouds, so the swamp hides them. The first the player sees is a zombie stepping out
    of the cloud's edge.
  - At the measured pace (about 0.5 a second in the swamp, 0.95 out of it) a hut 130-230 units off is **three to six
    minutes** away at normal speed, four to eight days. That is time to see it coming and man the walls, and it is
    the creep.
- **The warning**: when the doors open, a notice with a chime, "Something stirs in the swamp", and the crows go up
  over the huts. When the first zombie comes into anyone's sight (exploration's), a second notice, "Zombies, from the
  swamp!", whose click takes the camera to it. Both are events on the notice line and in the journal.
- **In the rules**:
  - The wave runs on the calendar, as the seasons do, so a replay makes it again with no command recorded.
  - The economy keeps it: when the next wave is due, and for each hut how many are still inside and when the next
    steps out.
  - It is saved and hashed only once a wave has started, so older states hash as before.
- **The demo's goal**: a third goal, "Survive the first wave", met when its last zombie has fallen
  (`modes_plan.md` keeps goals by mode).

### For the user to decide

1. **Seed 3: the river between the swamp and the camp.** Its zombies would stand lost at their huts until the player
   bridges the river. Three ways:
   - **A.** The river is a moat until it is bridged. This fits "walls, palisades and moats are the defence", but seed
     3's demo would have no first wave until a bridge is built.
   - **B.** Zombies wade rivers, slowly (a high cost per step in the water, as breaking a wall is a cost): a river
     buys time but never safety. Moats the player digs could be deeper later.
   - **C.** Huts only where their zombies can walk to the camp, which on seed 3 means none in its swamp.

   I'd suggest B: the first wave then reaches every map, and a river still matters.
2. **Together or strung out?** Today each zombie heads off the moment it steps out, so a wave arrives in a long file,
   the nearest hut's first. The alternative is that they gather at the swamp's edge until all are out and come as one
   pack. That needs a new "wait for the rest" state; the file needs nothing new.
3. **The numbers**: two a hut, the second day of spring, about 5 s apart.
4. **Saves from before the huts** have none, so they would never get a wave. They could be placed on loading a chasm
   save that has none. That is cheap, and the same every time, since they come from the seed.
5. **Later waves**: how often, and growing how fast. They are not needed for the demo, which only needs the first.

## Later (ideas, nothing agreed)

- **Clearing the swamp**: a hut that can be burnt down, so a wave comes from fewer doors, or a hut that empties.
- **Finding a hut** is a discovery (`EVENT_DISCOVERED`, `areas_plan.md`): "found where they come from".
- **The swamp's haze thickening** a day before a wave (`zombies_plan.md`, ideas).
- **Winter**: the swamp frozen, the huts dormant. A raid on a hut across the ice (`zombies_plan.md`, ideas).
- **A light in a doorway**, or smoke from a stovepipe that should be cold, before a wave.
