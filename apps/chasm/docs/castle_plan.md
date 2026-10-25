# The castle

Talked through with the user 2026-10-25, after the terraces' retaining walls (`terraces_plan.md`). What stands today
is the DEBUG CASTLE (`ramparts_plan.md`): grey stone blocks, arches and stairs painted on plots, each on a level of
whole storeys, their decks walked by the posted - and beside it the STONE GUARD TOWER, the guard tower's rules in
stone, debug only too.

**What it is for.** Not the game's stronghold: the main thing is to find out how far **Townscaper's tiling** carries
this game - pieces on an irregular grid that take their look from their neighbours, so a few simple choices made by the
player come out as a castle. Terraces showed it working. The long-term reason for a castle is a villager who becomes
KING and lives in one; the keep for him is kept to a minimum.

---

## Wanted (the user, 2026-10-25)

1. **The stone tower as part of the castle.** Built on its own, as now, or in a wall - in **3 or 4 storeys**, and
   **merged with the walls**: beside a two-storey castle block, its entrance is on that block's level. Proposed
   below.
2. **The walls**: more decoration; their edges as **battlements**; the **seams at corners** fixed.
3. **The keep / great hall** - in, kept to a minimum: where the king lives, not a fortress of its own.
4. **The gatehouse** - in: the gatehouse `defence_plan.md` wants (wanted 2026-10-10, never planned) now as the
   castle's - a gate in a castle wall under a deck, flanked by towers.
5. **The armoury** - in, as what the **ballista** needs (the ballista tower is the next thing after the zeppelin,
   `steam_plan.md`).

## Open suggestions (not decided)

Buildings that may stand only inside the castle's walls or on them. Left open by the user, 2026-10-25.

- **"Inside the walls"** - the rule they would all need: a plot is inside when no walk from it reaches the map's edge
  without crossing a castle piece (a flood over the plots, as the walkers' map is). The same answer says which side of
  a tower or gatehouse faces in.
- **Barracks** - homes for the guards, inside the walls, so they are posted from there faster.
- **A cistern or a granary** - stores that hold through a zombie wave inside the walls.
- **A courtyard well** - the castle's own water.
- **A bell tower or a signal fire** - the ALARM `zombies_plan.md` step 5 wants (gates shut, everyone home), rung from
  the castle.
- **Covered wall-walks** (hoardings) - a timber roof over a stretch of wall-walk.
- **A ballista platform on a tower** - with the armoury above as what it needs.

---

## Proposed: the tower in the castle (2026-10-25, for the user)

### Why it does not merge today

Three things keep the tower apart from the walls, each by design when it was a wooden tower on open ground:

- **Its footprint is inset**: its JUNCTION's polygon (`ZoneTowerCorners`) stands `ZONE_TOWER_INSET` of the way to the
  cells' centres, where a castle piece fills its plot's whole tile. Beside a wall the tower stands in a hole in it, the
  wall's deck running round it and its parapet facing it.
- **It stands on its own ground**, not on a level: its storeys count from `ZoneGroundAt` (`BuildingTowerDeck`), a
  castle piece's from its whole-storey `castle_base`. Its floor meets a wall-walk only where the two happen to agree.
- **Two storeys, always** (`ZoneKindMaxStoreys`), so it can never rise over a two-storey wall, and its one floor - the
  guard room's - is the only place a wall-walk can come in.

### The proposal: the castle decides its shape, the building its job

The stone tower stays **the guard tower in every rule** - a building of its own, its guards by its junction, its
stock, the caretakers who keep it, its cost a storey (`ZoneBuilding::f_stone`) - and takes **the castle's geometry**:

1. **Its plot's whole tile**, as a castle piece: no inset, so its faces run on from a wall's and the two share their
   edges. A tower may still read as wider than its wall by what it carries - a plinth, corbelling, the guard room
   overhanging - not by a different footprint.
2. **A level**, as a piece's (`castle_base`, `ZoneCastleLevelFor`): placed beside a castle piece it takes that piece's
   level, so its floors and the wall-walk line up exactly; alone, its ground rounded up as a first piece's, with the
   same settling.
3. **2 to 4 storeys** over its level (the user's; the wooden tower keeps its 2). It rises up to two storeys over a
   two-storey wall.
4. **A floor at every storey.** Where a neighbour's deck meets one of its floors (`WalkJoin`'s heights), that side
   has a **doorway** at that floor, and the wall-walk runs into it - no parapet across, no end face: the tower's own
   face is the wall there. The **guard room** is the top floor, with its windows and roof, as now.
5. **Walked as one node with a stair inside.** Inside a stone tower nobody is drawn (`InStoneTower`), so the guard room
   stays its one node, joined by its stair to the ground at its door - the ladder's connector today - and to every
   neighbouring deck at a doorway, at a stair's speed by the storeys climbed. A wall-walk on both sides of it then
   goes through it.
6. **Drawn with the castle**, quarter by quarter in `CastleMesh.cpp`, rather than as a building apart
   (`BuildStoneTower`): a tower and the wall beside it become one mesh with shared edges, and each quarter knows its
   neighbours as a block's does. The walls' **battlements** and the **corner seams** (wanted 2 above) are fixed in the
   same place, which is why the tower is worth doing first.

### Decided (the user, 2026-10-25)

- **Its ground door stays**, in a wall too, on its ladder side as now - the caretakers who stock it walk no castle
  deck. **Later in the design** it moves to the side that faces IN, once "inside the walls" exists.
- **2 to 4 storeys** over its level.
- **Its top by its cell, not by a choice**: a tower on a plot of **three** cells - a triangular tower - has a
  **crenellated** top, its guards behind the merlons; every other has the guard room and its **pointed roof**. So the
  look comes out of the tiling by itself, the Townscaper way.
- **Later, the Townscaper way further**: a block rising two storeys or more over all its neighbours BECOMES a tower by
  itself, its top growing out of the tiling as above. Noted, not planned: the garrison would have to belong to castle
  pieces rather than to a building, so it waits for the castle going into play.

### Order

1. **The rules**: a stone tower's level and its 2-4 storeys, saved and hashed; its node counted from the level
   (`WalkNodeHalves`); its joins at every floor; the ground door kept. BUILT 2026-10-25.
2. **The look**: the tower drawn by the castle, its doorways at the joins, the neighbouring pieces' parapets and end
   faces dropped against it. BUILT 2026-10-25.
3. **The walls**: battlements and the corner seams, in the same pass. BUILT 2026-10-25.

### As built: step 3, the walls (2026-10-25)

`CastleMesh.cpp` alone (and `BuildingArchedOpening`, ArchedOpening out of `BuildingMesh.cpp` for the slits).

- **The seams** were the parapets: each quarter's half-edge drew its own, set in along its own normal and never
  mitred, so where two met - at a cell's middle, round a corner, between two plots - they left a wedge open, green
  showing through, or overlapped. Now a quarter's sides only say what they carry (`QuarterSide`), and the cell draws
  its OUTLINES at once (`Outlines`): each side's join at the cell's middle (`OutlineJoin`) - round its own corner, into
  an inner corner round a quarter outside it, straight on into the next plot's at the same height, or an end - steps it
  out along the mitre the two share, and at the crossing along `BuildingWallOut`, as the terrace walls and the towers
  are. An end is closed.
- **Battlements** in place of the low parapet, on a deck's open edges: corbelled out `CASTLE_BATTLE_OUT` over the face,
  a low wall to the crenels' sills, merlons by turns with crenels stretched to fit - half a merlon at each crossing, the
  next cell's the rest, and at the cell's middle half of one going straight on into the next plot's, or a whole one round
  a corner or at an end. **Not against a wall** standing a sill's height over the deck - a taller block or stair, a stone
  tower, a terrace, a house - which guards that edge already (`StandingTop`).
- **A plinth** along an outer wall's foot on open ground - not under an arch's openings or a piece over another - mitred
  the same way, going on round the castle whatever the ground's height.
- **Arrow slits** in a block's outer wall standing a storey and a half or more over the ground outside, one a side,
  in the middle of its top storey.
- Checked on `castle_tower_heights_test.json`, `ramparts_crossing.json` and `terraces_test.json`: no light through the
  battlements anywhere; corners mitred, inner corners too; battlements going on unbroken from plot to plot and into a
  tower; an arch over a wall with battlements on both layers; none along a lower deck at a taller block's foot.

### As built: steps 1 and 2 (2026-10-25)

Files: `Zones.*` (`ZoneTowerLevel`, `ZoneTowerCrenellated`, `ZoneBuildingMaxStoreys`, `ZONE_STONE_TOWER_LEAST/_MOST`,
`ZONE_OP_TOWER_STONE`'s storeys, the restore, `BuildingWallOut`), `Walkers.*` (`StoneTowerFloors`, `WalkNodeHalves`,
`WalkJoin`, the joins' climb in `EnsureDecks`), `BuildingMesh.*` (`BuildStoneTowerQuarter` in place of
`BuildStoneTower`, `BuildingTowerDeck`, `BuildingInTower`), `CastleMesh.cpp`, `ZoneMesh.cpp`, `MeshBuild.h`
(`MeshMitre`, out of `TerraceMesh.cpp`), `ApplicationChasmWorkersView.cpp`, `ApplicationChasm.cpp` (the panel's
storeys for the Tower choice, `chasm_paint` `tower_stone` with `storeys`).

- **Its level** (`ZoneTowerLevel`): a terrace's top, else the level a first castle piece painted on its plot would take
  - worked out, not saved, so saves are as they were. A wall-walk on other ground still meets one of its floors: they
  are whole storeys apart.
- **Its height**: `ZONE_OP_TOWER_STONE`'s value is 0 for wood, 1 for stone as it is, 2 to 4 for stone that high; debug
  places it standing whole. Saved as any building's storeys, restored up to `ZONE_STONE_TOWER_MOST` for a stone one;
  `chasm_check` takes a stone tower's own most (`ZoneBuildingMaxStoreys`). Its cost a storey is unchanged, so a taller
  one costs more.
- **Walking** (`WalkJoin`): its one node, the guard room, joins any castle deck beside it that meets one of its floors
  (`StoneTowerFloors`) - a stair's top first - and two towers side by side at the highest floor they share; never the
  ground but by its ladder, as before. A join into it costs its climb inside from that floor, at a stair's speed, and
  its ladder the climb from the ground. Checked on `castle_towers_test.json` with the towers made 4 and 3 storeys: from
  the wall-walk (layer 4) a posted walk goes straight into the 4-storey tower's guard room (layer 8), and along the wall
  into the 3-storey one (layer 6); from the ground, round to its ladder side and up.
- **Drawn with the castle** (`BuildStoneTowerQuarter`, from `BuildCastleCell`): the plot's whole tile, a quarter a cell,
  its sides stepped out at a crossing along `BuildingWallOut` - which both cells work out alike - and at a corner along
  the mitre. A shaft on a plinth, a string course at each storey, an arrow slit in each storey over the first on a free
  side, its plank door at the foot of the ladder side, a doorway at every floor a deck meets. On top, on a plot of three
  cells, a crenellated parapet on a corbel table - half a merlon at each crossing, the rest the next cell's, a whole one
  at each corner - opening where a deck comes in level with the top; on any other the guard room with a window a side
  (a door where a deck meets it) and a pointed roof to a spike. An opening is drawn whole by the lower-numbered of the two
  cells either side of its middle.
- **The castle meets at the crossings** now, not the midpoints (`BuildingWallCrossing`), as buildings and terraces do -
  so a tower, a house or a terrace beside a piece meets it wall on wall, and round a point of five or six cells a piece
  is the same n-gon. A walker's height between two castle plots meets at the crossing too (`RouteHeight`).
- **Seen**: inside a roofed tower nobody is drawn; on a crenellated top a guard whose route keeps him on top is
  (`InStoneTower`), hidden only on his way up or down inside.
- Test save `saves/castle_tower_heights_test.json`: `castle_towers_test.json` with its towers made 4 and 3 storeys, and a
  3-storey crenellated one on the three-cell plot at (233.9, -6.2) beside a two-storey block. Saved, loaded, the heights
  back; `chasm_check` passes.

Not checked: a guard drawn on a crenellated top (no guard was posted in the test), and a replay of the commands.
