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

## The stairs (talked through 2026-10-26)

The STAIR piece as it was: one plot, told its storeys by hand, sloping down to whatever was a storey or so under it.

### What was wrong

- **Too steep**: a two-storey stair on one plot came down to the ground - a storey and a half in its front half-plot,
  about 60 degrees. The stair's foot took the ground as much as a storey under it (`WalkJoin`), meant for a level built
  up over a hill, and that let any stair start on flat ground. A plot is about 2 across and a storey 1.3, so a storey
  a plot is about 33 degrees - a stair.
- **Mounds**: a stair with nothing to climb to, or tops on opposite sides, sloped down every side - a stepped pyramid;
  and a stair always took the ground in front, so its front could not be closed.
- **Zigzag treads and holes**: each quarter's ramp was two triangles of different slopes, so the step edges came out as
  chevrons, and where a flight's two plots met, risers were missing and a tread hung in the air.
- **Heights by hand**: every plot's storeys set apart, and a drag is one height, so a flight could not be dragged.
- **Massive**: solid stone to the ground, nothing along its open side.

### Decided (the user, 2026-10-26)

- **The DRAG gives a stair its direction.** Its heights come of what it starts from, a storey a plot.
- **One storey a plot**, at most.
- **Three forms, from the neighbours**: a FLIGHT goes in at the foot and out at the top on the opposite side, straight;
  where the way turns the plot is a LANDING, flat at its top; a stair that has to climb more than a storey with no room
  for a flight is an OPEN TURRET - a spiral round a round core, its steps outside, as the curved stair in the user's
  photo (Poppi's courtyard). So a perron against a wall comes out of two drags out from its middle: `[up ->][landing]
  [<- up]`, two plots a side against a two-storey wall; with its flights taken away, the landing is a turret.
- **A BALUSTRADE** along the open sides of flights and landings and round the turret's steps - the photo's, so the
  stairs stand out against the plain walls and battlements.

### How it works (worked out from the above)

- **Drag from the top down**: pressed on a castle piece or a stair, or on open ground beside a deck, the drag starts at
  that deck's height (the highest flat deck beside the first plot); each plot after is a storey lower going straight
  on, the same height where the drag turns - the plot it turns at a landing - until the ground: a plot with nowhere
  lower to go is refused, and the drag stops there. **Or from the bottom up**: pressed on open ground with no deck
  beside it, the first plot rises a storey from the ground and each after a storey more, a turn making the plot it
  turns at a landing. A stair is never painted over another piece - but over an arch a storey under the drag's height,
  as before (`castle_over`), going straight on.
- **Kept per plot**: its top in storeys over its level, as every piece's, and the stair it climbs to (`castle_up`) - or
  none, for one climbing to a deck. Its FORM is worked out from those, stored as its shape: a LANDING where a stair below
  it starts at its own top; a FLIGHT with a stair below it a storey lower, or with nothing below it but ground or a deck a
  storey under its top on its far side; an open TURRET where there is nothing a storey under its top - its foot the
  ground two or more storeys down. Worked out again round any change, so taking a flight away can make a turret.
- **Taken off from its foot up**: a stair with a stair below it is refused ("the flight below comes off first").
- **Walked**: a flight at its middle, joined only up its way (to the stair it climbs to, or the decks on that side) and
  down it (to the stairs below, or the ground or a deck on its far side - the ground only at its level's foot, never two
  storeys down); a landing as a deck; a turret's top as the stone tower's guard room, joined to the deck it climbs to and
  to the ground at its foot, the climb round it at a stair's pace.
- **Drawn**: a flight's steps straight across it, square to its way up - one plane over its whole plot, so the steps of
  neighbouring cells and plots line up and a walker on it follows the same plane; its sides a wall to half a step over
  the treads with a balustrade on it, open-arched underneath where its foot stands a storey or more over the ground; a
  landing a deck with a balustrade; the turret its core, its steps round it - half a turn a storey, an odd number of half
  turns, so its foot faces the way down - a balustrade on their outer edge, and its last step on a short landing to the
  deck it climbs to. Walkers go round it.

### Order

1. **The rules**: `castle_up`, the forms, the drag both ways, taking off, the save and the hash, `chasm_paint`, the
   panel (no storeys for a stair), a drag filling the plots it skips. BUILT 2026-10-26.
2. **Walking**: the joins of the three forms; a walker's height on a flight from its plane. BUILT 2026-10-26.
3. **The look of flights and landings**: straight steps with no holes, the sides, the balustrade, the arch under a high
   flight. BUILT 2026-10-26.
4. **The open turret**, and walkers going round it. BUILT 2026-10-26.

### As built (2026-10-26)

Files: `Zones.*` (`ZONE_CASTLE_LANDING`/`_TURRET`, `ZoneCastleStairForm`, `ZoneState::castle_up`, the stroke's
`castle_stroke_last`/`f_castle_stroke_climb`, `ZoneHasStair`, `ZoneStairTop`/`Up`/`Downs`/`UpDir`/`FootMeets`/`Form`,
`ZonePlanStair`, `Zones::RefreshStairs`, the castle op and `RestoreCastle`), `Walkers.*` (`FlightTakes`, `TurretNode`,
`WalkJoin`, the turret's join in `EnsureDecks`, `WALK_TURRET_ROUND`), `CastleMesh.*` (`CastleFlight`, `CastleTurret`,
`FlightSteps`, `FlightSide`, `Balustrade`, `Turret`), `ApplicationChasmWorkersView.cpp` (`RouteTurretAt`, the flight's
plane in `RouteHeight`), `ApplicationChasm.*` (the drag, `castle_drag_stroke` for the hover, the panel, the save, the
hash, `chasm_paint`/`chasm_pick`'s `up`), `ChasmSave.cpp` (a stair's `up`, fifth on its castle entry).

- **The drag** (`ZonePlanStair`, also the hover's colour): pressed on a piece, a stair or a stone tower it only starts
  there; on open ground it comes down from the highest flat deck beside it within four storeys, or climbs from the
  ground with none. Going down, straight on (within `ZONE_STAIR_STRAIGHT` of the way the last plot came down) is a storey
  lower, a turn the same height and the last plot a landing; a flight that already goes on down refuses a turn ("a stair
  turns on a landing"), and a plot with nowhere lower to go is refused ("down at the ground"), ending the drag. Going up,
  a turn makes the last plot a landing at the height of the stair below it, and a plot beside a deck at the height
  climbed to is a landing onto it rather than a storey past it. Each plot must neighbour the last; the castle tool now
  paints the plots a quick drag jumps over, in order, for walls too.
- **The forms** are stored as the shape and worked out again round every castle change (`RefreshStairs`, a few rounds
  until none changes): a plot with a stair below at its own top is a LANDING; with one a storey lower, or one storey
  over its level, a FLIGHT; with none below, two or more storeys up and no ground or deck a storey under its top on its
  far side, a TURRET.
- **Taking off** is from the foot up ("the stair below it comes off first"), and no other piece takes a stair's place
  while one is below it. Taking a perron's lower flight away left its upper plot a turret, as the user foresaw.
- **Walking**: a flight joins only up its way (the stair it climbs to, or the decks that side) and down it (its stairs
  below, or with none what is across from its way up); the ground only at a first flight's foot, as much as a storey
  under it - never two storeys down. A landing is a deck. A turret's top joins up its way, and the ground at its level on
  any other side; that join is slowed by the walk round (`WALK_TURRET_ROUND` times the climb), so it costs A* the climb
  and takes the walker as long as he is seen climbing.
- **The look**: a flight's steps cut from its plane (`CastleFlight`: up at the nearest crossing on its way up, down at
  the nearest on its way down, never shorter than 0.8), six to a storey, straight across and the same in every cell; its
  sides stepped as the treads, and on a flank a string wall half a step over the plane with a coping. A BALUSTRADE on a
  flight's string and along a landing's open edges - a base, balusters with a belly, a rail, a newel where it turns or
  ends - but not against a wall a sill over it. Under a flight or a landing a storey or more over the ground, a dark arched
  doorway in each open half-side, on the plinth: an arcade under a perron. The TURRET drawn whole by its plot's first cell:
  a round base, a core to a pointed cap, steps round it climbing anticlockwise (half a turn a storey, an odd number of half
  turns, so its foot faces away from its exit), each a block with a slab more under it so the soffit climbs in steps, a
  balustrade on their outer edge, and its last step a landing out to its exit, balustraded both sides.
- **Walkers** on a flight follow its plane (`RouteHeight`); round a turret they go straight to its foot and round the
  steps' middle (`RouteTurretAt`), facing the way they go, and out across its landing.
- Checked on a flat map (seed 12, 8000 cells) with a two-storey wall: a perron from two drags out from one plot - landing
  at 2, flights 2 and 1 each side - walked up from the grass round to a flight's foot and over the landing, never up its
  closed front, and down the other side; a single stair against the wall a turret, walked round onto the wall-walk; drags
  climbing from the ground three and two plots out, the first ending in a landing level with the wall-walk. Saved as
  `saves/castle_stairs_test.json` and loaded the same, every form and link; `chasm_check` passes;
  `ramparts_stair_arch.json`'s flight over an arch still loads and stands.
- Not checked: a replay of the commands, the panel and the hover's colour on screen (the test copy runs minimised), stairs
  on a hill, and a turret of three or four storeys. **Old saves'** stairs come back without the stair each climbs to,
  which their saves never had, so their multi-plot stairs come back as turrets (saves are not migrated).

---

## The gate (talked through 2026-10-26)

The gatehouse `defence_plan.md` wanted, as the castle's: a GATE in a castle wall where a road goes through it, the way
a road through a palisade makes a palisade gate.

### Decided (the user, 2026-10-26)

- **It comes of a road and a wall crossing**, either way round: a road painted across a castle wall, or a castle wall
  dragged across a road, makes that plot a GATE piece. The road stays road.
- **Two storeys at least**: a one-storey wall across a road leaves the road OPEN - a gap in the wall - and a road
  painted through a one-storey wall takes the plot.
- **The gate is always the same height**: its passage is a two-storey wall's, and a taller wall over it is just more
  wall over the passage. No gatehouse rising over the wall.
- **A portcullis and doors**, each opened and shut on its own - two actions. Either one shut, nobody passes.

### As built (2026-10-26)

Files: `Zones.*` (`ZONE_CASTLE_GATE`, `ZoneState::portcullis`, `ZONE_OP_PORTCULLIS`, `ZoneGateShut` taking the
portcullis in, `ZoneGateDoorShut`, `ZonePortcullisDown`, `ZoneCastleGate`, `ZoneCastleGateWay`; the road and castle ops,
`ZoneCanCastle`, `ZoneCanGround`, the restore), `CastleMesh.cpp` (`GateBarrel`, `Portcullis`, `GateDoors`), `Walkers.cpp`
and `Economy.cpp` (the gate's way), `RoadMesh.cpp` (no palisade posts on a castle gate), `ApplicationChasm*.cpp` and
`ChasmSave.cpp` (the save, the hash, the card, the broken gate, `chasm_paint`).

- **It is a gate's mark** (`ZoneState::gate`, its DOORS), so its card, the walkers stopped when it is shut, zombies
  breaking it, the save and the replay are a gate's already. The PORTCULLIS is a state of its own beside it
  (`ZoneState::portcullis`), and "the gate is shut" (`ZoneGateShut`) means either: everything that stops at a shut gate
  stops at either. The save keeps the doors in the ground's flags as before and the portcullis as a fourth number on
  the gate's castle entry; hashed only where down.
- **How it comes**: a castle drag's block on a road plot is a GATE there at two storeys or more, refused at one ("a
  one-storey wall leaves the road open"), so the drag leaves a gap; a road painted through a block makes it a gate, or
  at one storey - or where the ground rises into the wall's level, leaving no passage - takes the plot. A gate's road
  taken up closes the wall over it again, a block; the gate taken off by the castle tool takes its doors and portcullis.
- **Its way** (`ZoneCastleGateWay`): into a gate and out of it only along its road - from a road plot or the next gate
  plot - for the walkers' edges and the straight walks alike, so nobody walks through the wall beside the passage.
- **Its look** (`CastleMesh.cpp`): it opens - by the same test - toward its road, and is solid wall every other way; a
  barrel vault along the passage, which bends at the plot's point where the road does, a round arch with voussoirs at
  each end, its passage always a two-storey wall's (`CASTLE_GATE_SPRING`), the wall over it as tall as the wall is. In
  each arch the PORTCULLIS, a timber grille - down to the road, or drawn up into the wall with only its feet under the
  crown - and at the plot's point the DOORS, two battened leaves under a timber tympanum, swung back against the
  passage's walls when open. Battlements over it as over any deck.
- **The card**: "Castle gate", a Doors open / shut pair and a Portcullis up / down pair, each its own recorded command;
  a broken gate refuses both until mended. `chasm_paint` op `portcullis` (`on`), `doors_here` and `portcullis_here`.
- **Zombies** break it as they break a palisade gate for now, whichever is shut; broken, both stand open. A stronger
  castle gate - each of the two its own break - when the castle goes into play.
- **Which side faces out** is not known until "inside the walls" is: a portcullis in both arches for now.
- Checked on `castle_tower_heights_test.json` (debug, port 8769): a road with a two-storey wall dragged across it - a
  gate; a one-storey one - refused at the road's two plots, a gap; a road painted through a three-storey wall - a
  three-storey gate. Shut by the portcullis, the walk from one side to the other went round the wall; open, through the
  gate; from the grass beside a gate on a five-cell plot, round by the road rather than through the wall. Saved as
  `saves/castle_gates_test.json` with one gate's doors shut and the other's portcullis down, and loaded the same;
  `chasm_check` passes. Not checked: zombies at a castle gate, and a replay of the commands.

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
  look comes out of the tiling by itself, the Townscaper way. **Changed 2026-10-26** (the user's): crenellated on plots
  of five and six cells too - only a four-cell plot gives a tower a roof (`ZoneTowerCrenellated`).
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
  cells (since 2026-10-26 any but four), a crenellated parapet on a corbel table - half a merlon at each crossing, the rest the next cell's, a whole one
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
