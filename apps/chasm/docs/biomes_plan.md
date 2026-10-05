# Biomes and terrain height

Talked through with the user 2026-10-05. The terrain so far is a few flat LEVELS joined by cliffs
(plateau, shard, chasm floor) with a quarter-unit bump on top; every rule leans on that - a plot is
buildable when its corners share a level, a walker never crosses a level change. Biomes bring
height and regions to it.

**Decided:**

- **Option C, both.** Levels stay for the hard edges - the chasm, shards, and later the mountain's
  crest cliffs and the shelves of its pockets. A smooth RELIEF field on top gives the height: rugged
  peaks in the mountain, soft hills on the plateau, a dip for the swamp. Buildable becomes "one level
  and a gentle slope"; walkers pay for slope and stop at a steepest. Rivers keep their flat water by
  a corridor where the relief fades to nothing.
- **The north mountain is the one rule a biome enforces**: it closes the chasm off from the map's
  north edge, so at the start nothing - no walker, no goods, nobody - gets from one side of the
  chasm to the other. Impassable to everything.
- **Mountain pockets** - small buildable shelves in the mountain - are reachable from ONE side only,
  and are mainly for resources.

## The order

| Step | What |
|---|---|
| 1 | The biome map and the north mountain band, and the check that the two sides are sealed |
| 2 | The relief field, and slope rules for building and walking |
| 3 | Mountain pockets, reachable from one side; the crest's look |
| 4 | The swamp lowland |
| 5 | Snow above a height, and the biomes' colours generally |

## Step 1: the mountain band

**Why the band needs a tongue.** The main rift runs north from the south edge to a tip 12-32% of the
map's depth short of the north edge, so today the two sides of the map meet round that tip. Any way
from the west side to the east goes either across the main rift - cliffs - or across the line from
its tip due north to the edge. So the mountain has to cover that line, and nothing else is needed:
forks and second rifts only divide a side further. A straight band across the north would have to be
a third of the map deep to be sure of it; instead the mountain's FOOT is a curve across the map,
shallow mostly (about a tenth of the depth, on noise) and reaching south in a TONGUE round the main
rift's head, past its tip by a margin. Where the foot passes south of the tip at the tip's x, the
seal is made - by construction, for every seed.

- **Generated with the layout** (`ChasmLayout::mountain_foot`, from the main tip), from its own
  random stream, so adding it moves nothing else: same rifts, rivers and grid hashes for every seed.
  The foot is not pinned into the grid - with relief (step 2) the mountain's front will rise
  smoothly, and needs no edge in the grid. The crest cliffs that will need one come in step 3.
- **A biome per fine vertex** (`Terrain::biome`): mountain north of the foot, temperate elsewhere.
  The chasm's head inside the mountain is mountain too - the frozen chasm end of the first concept.
- **Until there is relief the mountain is a colour**: its cells draw from the palette's frozen row
  (snow and grey rock).
- **Impassable and unbuildable**: no zone on a plot touching it ("on the mountain"), no forest, and
  no walker's edge into it.
- **The check, `sealed`**: flood the walking graph from the plateau along the south edge - with
  nothing painted, and rivers counted as crossable, since bridges will cross them - and no region
  may reach both sides of the main rift's mouth. So the guarantee is checked on every seed, and goes
  on being checked once relief, pockets and passes change the mountain.

### Step 1, as built (2026-10-05)

Files: `ChasmLayout.cpp` (`PlaceMountainFoot`, `ChasmMountainFootAt`), `Grid.h` (`ChasmLayout::mountain_foot`,
`main_tip`, `main_mouth_x`), `Terrain.*` (`biome`, `Mountain(v)`, the `sealed` check), `TerrainMesh.cpp`
(the frozen row), `Zones.cpp`, `Walkers.cpp`, `Forest.cpp`.

- **The foot**: a base 11% of the depth from the north edge, swinging 5% either way on two octaves of
  noise (wavelengths 260 and 70), and a parabolic tongue centred on the main tip's x, 40 past the tip
  and 95-130 either side at the base's depth, joined by a smooth maximum (24). One point every 4
  units, west to east. Its stream is its own: grid hashes, rifts and rivers unchanged for every seed
  (seeds 1-3 still match their pinned hashes).
- **The biome** is mountain for a vertex north of the foot, the chasm's head included. A cell draws
  from the frozen palette row where two or more of its corners are mountain; the skirt stays earth.
- **Mountain rules**: zones refuse a plot with a mountain corner ("on the mountain"); walkers' edges
  into it are closed; the forest puts only a scatter of rocks on it.
- **`sealed`** (debug check): union-find over fine edges that join two non-mountain vertices on one
  level - rivers ignored, nothing painted - then no region may touch the south edge's plateau on both
  sides of the main mouth. Reports each side's reachable plateau and the mountain's share; a failure
  marks the joined region's vertex nearest the main tip.
- **Measured, seeds 1-60 (debug)**: every check passes on every seed; mountain 11-19% of the map's
  vertices (mean 14.6%). **Proved to catch a
  break**: with the tongue stopped 60 short of the tip, seeds 1, 2 and 3 all fail `sealed`, marked at
  the gap. The replay test passes in debug and release.

**Open:**
- Rivers rising on the north edge run across the mountain on its snow; with relief they will need
  their corridor (step 2), or should rise at the mountain's foot instead.
- The foot is a function of x, so the mountain cannot reach south anywhere but in its one tongue -
  enough for the seal, and step 3's pockets and spurs may want more.
- Debug and release mountains were not compared vertex for vertex (the biome is not in any hash);
  the foot is plain float arithmetic on the layout's own stream, like the rest of the layout.
