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

## Step 2: relief, and slope rules

**One function of position, on the plateau only** (`Terrain::Relief`, a 2-unit raster read bilinearly,
so cells either side of an edge agree): soft HILLS in regions - some country rolls, some lies flat for
towns - and the MOUNTAIN climbing from nothing at its foot into ridged crags. Everything fades to
nothing toward a river's wet margin, so rivers keep their flat water and cut valleys through the
mountain. The chasm floor and the shards stay flat. Seeded from the world's seed.

**`Terrain::GroundHeight(p, level)` is the ground** for everything that stands or is drawn on it - the
terrain mesh, gardens, roads, crops, walls, houses, props, walkers - and `Terrain::ground` holds it per
fine vertex for the rules.

- **Building**: a plot must also be gentle - the ground may rise at most so much across it: 0.7 for a
  house, garden or town, 1.4 for a road (it climbs), 1.3 across a field's cell ("too steep").
- **Houses** stand on their plot's ground rounded to half a storey. Rounding is what keeps a row of
  houses on a gentle slope under one roof; where the ground has climbed a half-storey the row steps,
  with a short wall. A deep footing hides the slope under the walls.
- **Walkers** are slowed by slope (speed / (1 + 3 x grade)) and stopped by a grade past 0.75. The
  mountain stays closed by its biome, not by its steepness, so the seal never depends on noise.
- **Cliff walls** follow the relief at their top; their strata are counted from the levels' drop, as
  before, so segments still meet - under a hill the strata stand a little thicker.
- **The view** (picking on the relief, the cursor and grid lines on the ground): the other window.

### Step 2, as built (2026-10-05)

Files: `Terrain.*` (`BuildRelief`, `Relief`, `GroundHeight`, `ground`, the `relief` figure),
`TerrainMesh.cpp` (ground and wall tops on the relief), `Zones.cpp` (slope limits), `Walkers.*`
(slope cost and limit), `BuildingMesh.cpp` (houses on half-storey footings), and every mesh and prop
placer moved from `TerrainGroundHeight` (now the level's bump only) to `Terrain::GroundHeight`.

- **Tuned to**: hills up to 14 on a 110 wavelength, in regions 320 across; the mountain climbs over 70
  from its foot to a floor of 10 plus crags of 42 (ridged noise, wavelengths 80 and 28); the relief
  comes back over 24 past a river's wet margin. The first hills (7 high) did not show at all at the
  game's zoom; 14 reads as rolling country, and rivers sit in valleys of their own.
- **Measured, seeds 1-30 (debug)**: every check passes; the highest ground 46-64 above the plateau;
  0.0-4.8% of the open plateau too steep for a house (mean 1.6%). A row of 82 plots painted across a
  hillside: 59 houses, 23 refused as too steep, the rows stepping where the ground does. The replay
  test passes in debug and release.
- **View** (ApplicationChasm.cpp, by the other window): picking marches the camera ray against each
  level's GroundHeight, top level first, and bisects the crossing, so a pick on a hill or the mountain
  lands under the cursor instead of off by the parallax. Hover/selection outlines, the debug grid
  layers, the pins and the issue markers all lie on GroundHeight (outline segments split every unit
  so they don't cut into slopes); "flat" still draws at y = 0. The camera's orbit point eases onto the
  plateau's relief, not the levels, so zooming in on a peak turns about the peak, and panning over the
  chasm doesn't drop the view.

**Open:**
- Snow starts at the mountain's foot, on flat ground - snow by height is step 5.
- Hills are the same everywhere they are allowed; a biome (step 4's swamp, a desert) will want its own.
- The mountain is still closed by its biome, not by being steep; pockets (step 3) open parts of it.
- Under the mountain the chasm's walls stand up to 60 taller, their strata stretched to match.

## Step 3: mountain pockets

### As built (2026-10-05)

Files: `ChasmLayout.cpp` (`PlacePockets`, the spurs in `PlaceMountainFoot`, `ChasmPocketDistance`, rivers
rising at the foot), `Grid.h` (`ChasmLayout::Pocket`, `pockets`, `pocket_rejects`), `Terrain.*` (biome
POCKET, the meadow and valley pressed into the relief, the `pockets` check), `Forest.cpp`, `Grid.cpp`
(seeds 2 and 3 re-pinned).

- **What a pocket is**: a MEADOW (radius 14-22, a wobbling edge, flat at 4-9 above the plateau) inside
  the mountain, and one VALLEY from it due south through the front of a SPUR - the foot bulging out
  round the pocket to where the valley leaves it. The valley ramps from the meadow's floor down to the
  hills at its mouth (a grade under 0.35); round both the crags rise steeply over 7 units, so it reads
  as a hollow. Biome POCKET: open, buildable, temperate colours, with a show of rock clusters until
  resources exist.
- **Where**: one on each side of the main rift, and about one map in three a second on one side,
  drawn from the mountain's own stream after its base and tongue (so those are unchanged). Tried in a
  shuffled sweep across the side - at least radius + 50 from the main tip in x, clear of rims (40 past
  the meadow), rivers (18) and each other (60) - first close under the north edge, later further
  south with the spur reaching round, and in the last tries smaller (radius 10-14) with 0.7 of the
  clearances: a small pocket is better than a side with none.
- **Rivers rise at the mountain's foot** now: a river from the north edge is cut where it first leaves
  the mountain, 10 units inside, so it springs from the mountain's face. Rivers crossing the mountain
  had blocked pockets on most seeds (step 1's open item too). A river whose whole run is inside the
  mountain keeps its course. Rivers are in the grid's hash, so seeds 2 and 3 were re-pinned - the same
  in debug and release.
- **The check, `pockets`**: each pocket's meadow must be in a region the `sealed` flood reaches from
  exactly one side, and a second flood that also refuses a walker's too-steep edges (grade 0.75) must
  agree - so the pocket can be WALKED into from its side. It lists why tries were refused.
- **Measured, seeds 1-60 (debug)**: every check passes on every seed; 133 of 141 wanted pockets placed,
  215-643 vertices each; 58 seeds have a pocket on both sides, seeds 17 and 41 on one side only (a side
  boxed in by rims and rivers). The replay test passes in debug and release.

**Open:**
- Seeds 17 and 41 leave one side without a pocket. If fairness wants a pocket per side always, the
  next lever is letting a valley bend round an obstacle instead of running due south.
- A few cells at a meadow's edge draw green up the rock face (a cell is coloured by its corners'
  biome, not its slope) - step 5's colouring by steepness fixes it.
- The pocket's look is all relief; there is no pinned cliff line round it. Enough so far.
