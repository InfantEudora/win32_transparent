# Areas Plan: places to discover, and the water heard on the way

Agreed 2026-10-16. The user's first good ambient sounds - a river's stream, and archer's waterfall - came
with a need the event system already had: discovering areas. Both are built on the world's generated
data, so they are one plan.

## What the user decided (2026-10-16)

- **Unexplored water is heard, faintly.** A waterfall in the fog is a reason to go and look.
- **The areas:** each river, each waterfall, each biome as one area, and the starting land - split by
  forest density into its open ground and its forest.
- **Only the first of its kind gets a notice.** The first waterfall found is told; the second is not.
- **The areas show as a toggleable overlay in debug mode.**

## Two halves

| | The water heard | Discovering an area |
|---|---|---|
| What | two looping sounds, a river's and a waterfall's | an event, the first time someone sees a place |
| Belongs to | the view: a count each pass, never an event | the rules: in the tick, journaled, replays the same |
| Made from | the rivers and falls (`Terrain`) and the exploration | the areas (below) and the exploration |

**What joins them:** you hear a waterfall before you find it. Under the clouds it is faint; when someone
sees it, it is discovered, and from then on it plays at its full level.

## The water heard

- **A river:** each pass, the distance from the view's centre to the nearest water's edge - 0 over it -
  with the listener's height, so zooming out turns it down. The side it is on pans it.
- **A waterfall:** the same, to the nearest fall's lip.
- **Under the clouds:** whether that nearest point is explored. It eases between the two over a second or
  so, so a fall coming into sight swells rather than jumps.
- **The game measures, the table decides.** These are cue parameters; two looping rows in `chasm.json`
  turn them into loudness and pan, the faint factor included. Each loop starts when its water comes within
  reach and stops when it leaves, its curve silent before the stop - archer's waterfall's pattern.
- **On an `ambience` bus:** it plays on in a pause (water does not stop because time does) and fades out
  under the title.
- **Core, needed for it:**
  - a following loop's gain can be several curves that multiply, as `gain_by` can - distance and the
    faint factor;
  - a loop is kept: the cue layer started loops as one-shots, and with every voice busy the oldest
    one-shot is taken - which a loop always is. Archer's waterfall shares the fix.

## Areas

An AREA is a part of the map that can be discovered, generated with the map - so never saved - and built
once into the world (`ChasmWorld::areas`):

| Kind | One for | Ground |
|---|---|---|
| river | each river, branch and stream | along its water |
| waterfall | each fall | round its lip |
| the plateau | the starting land's open ground | temperate plateau, outside the forest's cores |
| the forest | the starting land's forest | temperate plateau, inside the forest's cores |
| the mountain, a mountain meadow, the swamp, the desert | each biome there is on the map | that biome's plateau |

- **Seen when any of its ground is explored** - the floatstone seam's rule (`LookForSeams`). An area is a
  list of the exploration's cells; looking through the ones not yet seen costs a few thousand byte reads
  when the exploration changes.
- **Read from the save** (`demo_plan.md`, "Discoveries"): which areas are seen is worked out again from the
  saved exploration after a load, so nothing new is saved and nothing is told twice.
- **Not told at the landing:** what the settlers see on their first look is home, not a discovery.
- **The chasm, its balconies and islands are not areas yet.** The chasm is the obvious next one - its
  first sight is the music's `reveal` - and a balcony is the demo's second goal.

## The discovered event

- **`EVENT_DISCOVERED`**, appended to the kinds (the journal saves their numbers): `person` who saw it,
  `amount` the area, `pos` where it was first seen, `what` 1 when it is the first of its kind.
- **Journaled**, so the statistics list what was found and when.
- **The notice line** tells it only when it is the first of its kind: "Jelle De Wit has found a
  waterfall".
- **Sound:** the first of its kind chimes with the notice, heard anywhere; any other is a chime near the
  view, as a tree felled is. Every event has the notification sound for now.
- **Goals and unlocks** can be conditions on the areas seen (`demo_plan.md`).

## The overlay

Debug mode, off by default: the panel's View checkbox and `chasm_view`'s `areas` layer. Each area's edge
drawn round its cells in its kind's colour, bright when seen and dim when not, with a post at its middle;
the panel lists the areas.

## Steps

1. **The water:** the core changes, `waterfall.ogg` copied from archer, the parameters and loops, the
   `ambience` bus.
2. **The areas:** the forest's density kept per plot, the areas built with the world, the overlay.
3. **The discovered event:** `LookForAreas` beside `LookForSeams`, the notice line, the journal, the cue
   rows, `chasm_areas` for agents, the wiki.

## As built (2026-10-16)

All three steps, checked in a muted instance on seed 12.

- **The water** (`ApplicationChasmSound.cpp`, `SignalWater`): `river_*` and `falls_*` parameters each pass;
  loops open within 55 (river) and 65 (falls) and close past 65 and 75. A river is faint where the stretch
  nearest the view is unexplored; a FALL is faint until its area is found - its lip's own cell, on the rim's
  edge, can stay unexplored while everything round it is seen, and the first try (by the lip's cell) kept a
  found waterfall faint. Faint eases over 1.5 s, so found water swells. `ambience` plays on in a pause and
  fades out under the title. Core: `follow.gain` takes a list of curves (`FollowGain`), loops are
  `SOUND_KEEP` (`CueSoundOutput`), and `CueSystem::Parameter` reads one back for tools. A check in
  `tools/cue_test.cpp`.
- **The areas** (`Areas.h`, built in `RegenerateGrid` into `ChasmWorld::areas`): seed 12 has 4 rivers, 1
  waterfall, the plateau, the forest, the mountain, a mountain meadow, the swamp and the desert - 11, built in
  about 12 ms. The land is assigned by CELL, each cell taking the kind of the plot under its middle: built
  from the plots' own cells, the areas were full of holes, plots standing further apart than a cell. The
  forest's core is kept per plot (`ForestData::core`), the same noise the trees use, so no prop moved.
- **The event**: `EVENT_DISCOVERED` (12), `Economy::Discovered`, journaled ("Anouk Wever found a river");
  `LookForAreas` beside `LookForSeams`, the landing's first look (nothing explored before it) silent. The
  notice line tells the first of its kind. Sound: `discovered_first` on `ui`, anywhere; `discovered` near
  the view only. `chasm_economy` lists a discovered event with `first` and `area`; `chasm_areas` lists the
  areas and which are seen.
- **The overlay**: the `areas` layer (panel and `chasm_view`), rebuilt at most once a second while the
  exploration moves; the panel lists each area, its cells and whether it is seen.

Seen in the run: the camp's picker found the first river branch at tick 1580 in two separate runs from the
same start (told, chimed anywhere), a second branch (not told, chimed near the view), then a man ordered west
found the main river (not told) and the waterfall (told) - its loop swelling from faint to full over three
seconds.

Open with the user:
- The far side of the chasm is the same temperate plateau, so it is part of "the plateau" - found at the
  landing. It could be an area of its own, with the chasm.
