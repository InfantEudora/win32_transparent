# Swamp huts: where the zombies come from

Asked by the user on 2026-10-26. The zombies (`zombies_plan.md`) can bite, break walls and be shot, but they come only
from a debug tool. P6 in `gameplay_plan.md` always had them coming "from the swamp", and nothing in the swamp said so.
This plan gives them a home there, and then a first wave out of it.

## What the user asked (2026-10-26)

- **A few solitary, shabby huts in the swamp** are where the zombies come from.
- **The first thing in the demo**: zombies come out of them **after the first year** and **creep up on the village**.
  Changed in step 2: not on a date, but once the colony has trained an archer.
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

The villagers' own route-finder (`chasm_pick` with `to_x`/`to_z`) agrees on all twelve huts. Since step 2 zombies wade
rivers, so seed 3's reach the camp too; "walk" here is still the dry walk, which the huts are placed by.

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

## Step 2: the way to the first wave (as built, 2026-10-26)

The first draft opened the huts' doors on a date, the second day of year 2's spring. The user, 2026-10-26: that is far
too soon to have an archer to defend with, and starting the colony with one would make it too easy. So the wave waits
until the colony is ready for it, and two goals on the way say how to get ready.

### What the user asked (2026-10-26)

- **After surviving the winter**, a new goal appears: **train an archer**.
- **Once the swamp is found**, a goal is added: **don't venture into the swamp unescorted**. Failing it does not end
  the game. It is failed when someone goes into the swamp without an archer.
- **Only after an archer has trained**, with a bit of archery skill, may the zombies come.

### Decided (the user, 2026-10-26)

- "After surviving the winter" is the date: the archer's goal appears whether the winter's goal was met or failed.
- "Without an archer" is per person: no trained archer near him, which is what "unescorted" says.
- The swamp's goal is **met when the first wave is beaten**.
- **Anyone** in the swamp counts, ordered there or at work.
- The numbers: 20% archery, an escort within 10 units, the second dawn after, **one zombie a hut**, about 5 s apart.
- **Zombies wade rivers**, slowly: a river buys time, never safety (seed 3).
- **They march as they come out**: no gathering at the swamp's edge.
- **Saves from before the huts are left as they are**: only a new game has huts, and so a wave.

### The goals, in order

| # | Goal | Appears | Met | Failed |
|---|---|---|---|---|
| 0 | Through the winter, nobody lost | at the start (as before) | the first dawn of year 2 | a death before then, but of old age |
| 1 | Reach the chasm | at the start (as before) | someone lowered to a balcony | never |
| 2 | Train an archer | the first dawn of year 2 | someone alive with `ARCHER_TRAINED` (20%) archery | never |
| 3 | Don't venture into the swamp unescorted | the swamp found, on a map with huts, before the wave is beaten | the first wave beaten | someone in the swamp with no trained archer near |
| 4 | Survive the first wave | the huts' doors open | its last zombie fallen | never: the colony lost is its own end |

- **A goal that has not appeared has no row** on the GOALS panel or the end's page. When one appears, the notice line
  says "New goal: train an archer". That is a new event, `EVENT_GOAL_SET` (31), journaled like met and failed, so a
  load doesn't tell it again. `ChasmGoalShown` is the test.
- **Train an archer** says the way there in its right-hand column, as the chasm's goal does: BUILD A BUILDERS' HUT
  (which brings the range), BUILD A RANGE, then TRAINING 12% OF 20% for the best anyone has. An archer trained before
  it appears meets it the moment it does.
- **The swamp goal** (`SwampUnescorted`, looked at twice a second):
  - **In the swamp**: a grown-up (young adult or older) out of doors and on the ground, on a swamp plot.
  - **Escorted**: a trained archer out of doors within `ESCORT_REACH` (10). A trained archer escorts himself.
  - The first one unescorted fails it, told by name: "Maud Hoeve went into the swamp unescorted".
  - Its right-hand column says KEEP OUT until anyone has trained, then ONLY WITH AN ARCHER.
  - A scout walking toward unexplored swamp sees it before he is in it: the goal appears then, and the player has his
    last few steps to call him back.
- **The demo is complete** once every goal in play is decided: the first three always; the wave's on a map with huts;
  the swamp's once it has appeared. `EVENT_DEMO_COMPLETE` keeps how many were in play in its `what`, so the journal
  says "4 of 5 goals"; one from before says "of 2".
- **The switches**: `CHASM_TRIGGER_*` sits after the goals, so its bits moved up by three. They are the viewer's,
  never saved, and `chasm_menu` names them: `archer`, `swamp` and `wave` are new.

### The wave

- **Called** by `TickGoals` once Train an archer is met on a map with huts (`Economy::CallWave`), due at the
  `WAVE_AFTER_DAWNS`'th (second) dawn after. A dawn that falls in winter, when the swamp is frozen, waits for the first
  dawn of spring. Met on day 68 or day 74, it is due on day 80.
- **The doors open** at the due tick (`Economy::TickWave`, before the zombies' tick): every hut standing, by id, each
  with `WAVE_FIRST_PER_HUT` (1) inside, each `HUT_BEAT_SECONDS` (1.7 s) after the one before. A hut lets one out every
  `HUT_TRICKLE_SECONDS` (5 s) from the plot before its door - or, with something standing there, the nearest open plot
  round the hut; with none, a trickle later. A hut taken down meanwhile lets out no more.
- **They march as they come out**, for the nearest person they can get to, as any zombie does.
- **Beaten** once the huts are empty and the zombies it let out have all fallen (`EconomyWaveBeaten`). Zombies the
  debug tools drop are none of it; clearing them all with the debug tools beats it.
- **Told**: `EVENT_WAVE` (32) as the doors open, "something stirs in the swamp", and `EVENT_WAVE_SEEN` (33) when the
  first of them stands on explored ground, "zombies, from the swamp!" - both in the journal and on the notice line, with
  the chime for now (`wave`, `wave_seen` in the cue table).
- **In the rules**: the wave follows from the goals, which are rules, and the calendar, so a replay makes it again with
  no command recorded. `EconomyWave` - when it is due, opened and seen, each hut's still inside and next out, its
  zombies standing - is saved (`wave` in a save's economy) and hashed only once it is called, so older saves and states
  are as before. `chasm_zombie`'s status shows it as `wave`.

### Zombies wade

A zombie's walk (`WALK_WHO_ZOMBIE`) is no longer closed by water: an edge with a wet end, off a bridge, is open to it at
`WALKER_SPEED_WADE` (0.4, against 1.6 on open ground and 1.0 in the swamp), times its own pace. A* takes the dry way
round where that is quicker. Placing the huts is unchanged: `ZoneSwampHutSpots` still prefers huts with a dry walk to
the camp, with its own flood, so no map's huts moved.

### Files

- `Economy.h`: the goals, the events, `ARCHER_TRAINED`, `ESCORT_REACH`, `EconomyWave` and its numbers, `CallWave`,
  `WaveSeen`, `TickWave`. `Economy.cpp`: names, the journal, save and restore, the call in `Tick`.
- `Zombies.cpp`: `CallWave`, `WaveSeen`, `TickWave`.
- `Walkers.h`, `Walkers.cpp`: wading.
- `ApplicationChasmGoals.cpp`: goals that appear, `SwampUnescorted`, the right-hand columns, the wave's first sight, the
  demo complete. `ApplicationChasm.h`: `goal_set`, `f_swamp_seen`, `ChasmGoalShown`. `ApplicationChasmExplore.cpp`:
  both read from the journal.
- `ChasmSave.cpp` and `ApplicationChasmEconomy.cpp`: the wave saved and hashed.
- `ChasmStats.cpp`, `ApplicationChasmStats.cpp`, `ApplicationChasmTime.cpp`: titles, journal lines, notices, and the
  end's page with room for five goals. `ApplicationChasmZombies.cpp`, `ApplicationChasmTitle.cpp`: the tools.
- `assets/cues/chasm.json`: `goal_set`, `wave`, `wave_seen`.

### Checked (debug, a copy on 8769, paused and stepped)

- **Seed 12**: no new goal in year 1; Train an archer appeared at day 40. An archer (30%) and a villager put down at a
  hut's door: the swamp goal appeared as he found the swamp, and held while they stood together. The villager sent on
  alone failed it 10.6 units from him, by her name. The wave was due at tick 94,500, the second dawn; the doors opened
  on it, and the three came out 85 ticks apart, one a hut. All five goals and the notices were in the journal.
- **The creep**: on seed 12 about 0.4 units a second through the swamp toward the colony.
- **Seed 3** (the river): an archer trained at the camp. All three zombies reached the camp in 11,000-14,000 ticks
  (4-5 minutes at normal speed), none lost; a villager killed them by hand and Survive the first wave was met. The
  swamp's goal never appeared: nobody found the swamp.
- **Met together**: an archer alone in the swamp (his own escort), the wave cleared: the swamp's and the wave's goals
  both met.
- **Winter**: met on day 68 and on day 74, both due on day 80.
- **A save at the doors' opening**, loaded: the wave as it was, huts, zombies and goals.
- **On screen**, in play: the GOALS panel with all five, and the colony lost's page with five goals in its foot.
- `chasm_check` no failures; the statistics' check empty.

**Not checked**: a wave against walls and a tower; the release build; a woodcutter at work in the swamp failing the
goal on his own (the rule counts him; not run).

**Not built**, of the first draft: the crows going up over the huts as the doors open, and a click on the second
notice taking the camera to the zombie - the notice line takes no clicks yet.

## Later (ideas, nothing agreed)

- **Clearing the swamp**: a hut that can be burnt down, so a wave comes from fewer doors, or a hut that empties.
- **Finding a hut** is a discovery (`EVENT_DISCOVERED`, `areas_plan.md`): "found where they come from".
- **The swamp's haze thickening** a day before a wave (`zombies_plan.md`, ideas).
- **Winter**: the swamp frozen, the huts dormant. A raid on a hut across the ice (`zombies_plan.md`, ideas).
- **A light in a doorway**, or smoke from a stovepipe that should be cold, before a wave.
