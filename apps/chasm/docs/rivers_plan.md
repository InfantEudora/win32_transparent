# Rivers: always one at home, branches, and underground rivers

Agreed with the user 2026-10-08. Three additions to the rivers of `grid_plan.md` step 9 (and their
widths and lakes of 2026-10-06), and one fix that came out of measuring them:

- **Every map has a river on the home side**, however small. The starting camp takes its water from
  one (`ZoneStartCampPlot`, "WHERE A NEW COLONY CAMPS"), so a map without one cannot start properly.
- **Branches**: two or three thinner streams joining each river.
- **Springs in the wall**: water pouring out of a dark mouth in the chasm's wall, below the rim, as
  in the Blender sketch `art_source/chasm/previews/cliffs_oblique.png` (`cut_spring` in
  `tools/blender_chasm_cliffs.py`).
- **Swallow-hole streams**: a thin stream on the plateau that ends in a hole short of the rim, and
  comes out again as a spring in the wall below. The underground river joins the other two.

Every map changes with this. Saves and recordings made before it are dropped (the user: "all the
saves and recordings can go"); the replay test finds its spots on the map by trying, so it adapts.

## How rivers are made today

- **The layout** (`ChasmLayout.cpp`, `PlaceRivers`): 2-5 wanted, on the home (east) side only. Falls
  first, at rim points with the home plateau behind and open floor ahead; then each one's course, a
  cheapest path back from the fall over noise, kept clear of rims, cracks, the far side and a band
  round every river already placed, to a source on the east or north edge. The path is then swung
  either way on noise (up to `RIVER_SWING`) for the meander. Width 5-7.5.
- **The terrain** (`Terrain.cpp`, `BuildRivers`): the line smoothed, the fall where it first
  crosses a rim, the width wandering and swelling into lakes - then one raster of "how far past the
  nearest water's edge", from which the channel, the stone banks, the wet plots and the relief's
  fade along rivers are all read. It takes the nearest water of ANY river, so where two channels
  overlap they merge with nothing more to do.
- **The water** (`WaterMesh.cpp`): flat water on every cell a channel lowers, named by the nearest
  river - across it and along it - for the shader's streaks; a sheet at each fall.

**Measured, seeds 1-100, before this work:** 21 maps had no river at all, 70 had one, 9 had two -
never the 3-5 the layout asks for. To be explained in step 1.

## Decided (the user, 2026-10-08)

- **Branch heads are inland springs**, never the map's edge. A branch from the edge would wall off the
  wedge of land between it and its river, reachable only by bridge; one rising inland leaves a way
  round its head.
- **Springs in the wall are fed by swallow-hole streams**, so every spring on the home side has a source
  you can find on the plateau - **plus one or two scenic springs on the far side**, which has no
  rivers (its water is snow and steam, `gameplay_plan.md`) but looks the better for them.
- **Always a river at home, no matter how small.**

## The pieces

### Kinds of line

A layout river (`GridRiverLine`) gets an END: a FALL over the rim (today's rivers), a JOIN into
another river (a branch: `joins` names which), or a SINK into a swallow hole (a stream feeding a
spring). All three run source first, so "along" grows downstream on every one of them, and all three
are cut into the ground the same way. A branch's or stream's points end ON its parent's centre line,
or at its hole.

### Branches

- **Where they join**: two or three junctions on each river, between a quarter and four fifths of the
  way to its fall (not where the river has just risen, nor in the fall's straight approach), well
  apart along it, each on a side of its own choosing.
- **Their course**: the same cheapest-path search as a river's, from the junction outward, with its
  own blocking: the parent's band is open to it (a branch must reach its river) but dear, so it
  leaves at an angle rather than running alongside; the parent's water itself is shut to it beyond
  the junction, so it never crosses to the other bank. The head: 30-75 of path away, upstream of the
  junction (a tributary points downstream where it joins), off the map's edge and out of the
  mountain's reach. Swung like a river, by less.
- **Width 2.2-3.4** against a river's 5-7.5. No lakes, no fall.
- **The water at a confluence**: a point is named by the river whose water it is DEEPEST in, not the
  nearest centre line - else one triangle could carry both rivers' coordinates and smear the streaks
  - and a branch's "along" carries on into its parent's at the junction, so the streaks meet in phase.
- **Bridges** already take the width of the river they cross, so a bridge over a branch is short.

### Springs in the wall

- **The mouth**: one or two wall columns set back about 2 over one stratum (`TERRAIN_STEP`, 6 high),
  one to three strata below the rim, with a dark back. The walls already stand each column's stratum
  in or out by a function of the column and the ring (`RockOffset`), so a mouth is that offset made
  deep, and neighbouring walls still meet without a crack; the mouth's sides taper where its columns
  meet the next. The roof above it is an overhang, drawn dark by the rule that already darkens
  downward faces.
- **The water**: the fall's sheet, starting on the mouth's floor instead of at the rivers' surface, and
  dropping into the mist. Only where open floor is below, as for a fall.
- **No broken columns at a mouth** (`HEIGHT_FALL_CLEAR`, as at a fall's lip).

### Swallow-hole streams

- **The hole**: a stream's last point, short of the rim by enough to leave its wet margin and a
  cliff-top path dry - so a stream that sinks does not cut the land along the rim, which is the
  difference it makes to play. Drawn as a dark hole in the water.
- **The spring** comes out of the wall straight below - a spring in the wall, as above.
- **The stream**: the branch's search, from the hole, to an inland head.

### Always a river at home

Fixed in the layout, and checked: `chasm_check`'s `rivers` fails a map with none, and the seed sweep
proves it over a few hundred seeds.

## Steps

1. **Always a river.** Find why rivers fail (the layout counts its refusals, as pockets do), fix it,
   and fall back to a smaller river - an inland source, a fall nearer the map's edge - when nothing
   else fits. The check, and a sweep. **Built.**
2. **Branches.** Layout, terrain (no fall, no lakes, the confluence), the water's naming, the checks
   and `chasm_generate`'s river list. **Built.**
3. **Springs in the wall.** The mouth in `TerrainMesh.cpp`, the sheet in `WaterMesh.cpp`; the far
   side's scenic ones. **Built.**
4. **Swallow-hole streams.** The layout's sink streams and their springs; the hole. **Built.**

After each: the seed sweep (`tools/chasm_seed_sweep.py`) and screenshots of a few seeds. (The replay
test went with the old saves; new recordings come after this work.)

## As built (2026-10-08)

**Why maps had no river** (`ChasmLayout::river_tries`, reported by `chasm_generate` as `layout.river_tries`):
a third of all tries had their course's search START in a shut cell - a fall's straight approach was 24
long and every course kept 24 from any rim, so on all but dead straight rims the first cell was shut. Most
of the rest were too near a fall already placed (`RIVER_FALLS_APART`, 80, by design). Three maps in a
hundred had no rim point at all that takes a fall: their home rim is balconies, a crack between them and
the mountain's tongue over the head.

- **The approach** now starts `RIVER_APPROACH` past the course's clearance, and a fall is only offered where
  that start is open (`Falls`).
- **The small river** (`SMALL_RIVER_*`): when no river fits, one more pass with a fall nearer the map's edge
  (30) and a crack (12), only the drop right under its sheet clear, swung 4, and an inland source if no
  edge cell is reachable. Width 3.5-5.
- **Off the map's edge** (`GRID_RIVER_EDGE`, `PlaceEdgeRiver`): when even that finds no fall - seed 39 - a
  river from inland running off the east edge. One map in 300.
- **The `rivers` check fails a map with no river**, and every line kind is counted in it.
- **Rivers crossing their rim exactly at one of its points** (a fall's lip is one) could be missed by the
  terrain's crossing test on both segments - seed 144 had a river with no fall. `SegmentsCross` takes a hair
  past either end.

**Branches** (`GRID_RIVER_JOIN`, `PlaceBranches`, `Branch`): as planned - 30-75 long, 2.2-3.4 wide, junctions
between a quarter and four fifths of the way, 45 apart, out of the mountain's reach (a river's head inside it
is cut off later). Their river's band costs 1.8x, its water is shut but 8 round the junction.
- **The confluence** took three goes. Naming a point by the deepest water alone still left a bright
  chevron in the river at a branch's mouth: one triangle carried both rivers' coordinates. Now
  (`Terrain::RiverCoords`) within one river a point is named by the nearest centre line as before, between
  rivers by the deeper water, and where two waters are within `RIVER_COORDS_BLEND` (3) of each other the
  coordinates are BLENDED; past the end of a line "across" carries on along its axis instead of flipping;
  and a branch's `along0` makes its along equal the river's where it comes in at the river's BANK, not at
  the junction on the river's line, half a width further. The branch's streaks now fan into the river.

**Springs** (`GridSpring`, `TerrainSpring`, `Terrain::mouth`, `BuildMouths`): the rim plots within half the
spring's width of its lip, over a drop past its stratum, have their wall's stratum `band` set back
`TERRAIN_MOUTH_DEPTH` (`TerrainMesh.cpp`, `InMouth`) and its face drawn the chasm's black; the sheet is a
fall's (`WaterMesh.cpp`, `Sheet`) on its own profile from the mouth's floor; foam where it meets the mist
as at a fall. **Broken columns kept off a spring** like a fall (`FringeSteps`, `near_fall`): on 17 maps in
60 one had stood in front of the mouth, leaving no wall for it. The `springs` check fails a spring with no
mouth plot. A spring's spot uses the small river's rules - its water is narrow - which more than doubled the
streams that found room.

**Swallow-hole streams** (`GRID_RIVER_SINK`, `PlaceSinks`, `Sink`): the hole `SINK_BACK` (16) in from the
rim's line, courses kept 12 off rims, 40-110 long, 2.2-3.2 wide; the hole is a dark disc ringed by wet rock
just over the water (`TerrainMesh.cpp`, `SwallowHoles`).

**Measured, seeds 1-300 (debug):** every map has a river at home - 228 one, 72 two; 14 needed the small
river, 1 of those off the map's edge. 676 branches on 372 rivers (1.8 a river). 267 swallow-hole streams:
231 maps have one or two, 69 none. Every map has one or two springs on the far side. No check fails;
`tools/chasm_seed_sweep.py` 1-60 clean. Seeds 1-3 re-pinned (`Grid.cpp`).

**Open:**
- The camera stays above the rim, so a mouth is seen only from across the chasm - as in the sketch's
  oblique view, never from below.
- A spring's water drops only into open floor. The sketch's lands on a slumped block with a splash; that
  wants a landing on an island or a balcony, and the balcony's ground made wet under it.
- Nothing marks a stream's underground run between the hole and the rim; a damp line or a few reeds could.
- A spring is scenery. Whether a winch at a balcony under one could draw its water is a gameplay question.
