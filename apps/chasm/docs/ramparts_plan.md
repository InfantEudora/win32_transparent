# Ramparts: walking above the ground

Walkers on a wall, with a road crossing under it; a walkway between towers; perhaps a bridge a level higher - a
castle. Talked through with the user 2026-10-10, out of the arches (`arches_plan.md`): Townscaper has no walkers,
so it never had to say who walks on top of what. Chasm's walkers do.

## Decided (the user, 2026-10-10)

1. **Feasible, and worth trying** - but whether three walkable things stacked on one plot READ from the game's
   camera is not known. So: step 1 (below), then a DEBUG PROTOTYPE to find out if it is a mechanic worth weaving
   into the rest, before anything of it is built for play.
2. **Only posted people walk the walls** - guards and anyone under an order. A villager crossing a wall would be
   the player's design mistake, but a wall-walk any villager may use is a way over a shut gate, and a tower whose
   ladder stands outside would be a way over the wall. So passability goes by WHO walks: the same hook the zombies
   need (`gameplay_plan.md` section 7, "walkers with a different map") - different walkers, different ground.
3. **Ramparts and towers could gain storeys**, the Townscaper way, for the archers' reach - later, with the range
   rules.
4. Reference: the Stronghold games have walls guards walk along, reached by stairs and towers.

## The model

The walk graph's nodes were the plots. Now a node is a plot and a LAYER on it:

- **Layer 0 is the ground**, every plot's node as before, its rules (`Walkers::EdgeOpen`) unchanged. With no
  decks, A* sees exactly the graph it saw - the same nodes, in the same order.
- **A deck** is walkable floor above a plot, APART from what stands on its ground - the arches' rule (keep what is
  over a plot out of its `storeys`). Few of them, so they are extra nodes after the plots', worked out again
  whenever the zones change, as the winches' links are.
- **A layer is counted in half storeys**: a flat deck stands on whole storeys (even), a STAIR's middle on a half
  (odd). A flat node spans its one height; a stair spans a storey, from the one under its middle to the one over.
  Two nodes on neighbouring plots on one ground join when their spans meet - a deck to a deck of its height, to a
  stair that reaches it, the ground to a stair that starts on it. So no stair can be missed and no deck reached
  but by one.
- **Connectors** join layers whose spans do not meet: a tower's ladder, from the plot it stands on up to its deck.
  Like a winch's link, an edge of its own, at its own cost.
- **Who may walk a deck** is the deck's: a tower's is open to everyone (it is a dead end - its guards, the
  caretakers who stock it); a wall's only to the posted.
- **A walker's height comes from his route**, not from the plot under him: each point of a route carries its
  layer. On a leg between two layers the height runs straight from one plot's middle to where the two meet at the
  plot's edge and on to the other's - so a stair is walked as a ramp, and a deck to a deck stays level. Up a
  ladder he steps up at the tower's wall, as he did.

Where it costs: the economy turns a position into a plot in several places and assumed that named one place -
re-planning from where he stands (`RouteTo`), straight walks (`EconomyStraightClear`, ground only), a crate put
down at his feet. Each now needs the layer too: a route's start is the plot under him on the layer he is on,
and only a walk from the ground to the ground goes straight.

## Step 1: the tower's deck, a real node

No new gameplay. A standing guard tower's platform becomes a deck node (its height `BuildingTowerDeck`), joined to
its ladder's plot by a connector at the ladder edge's old cost; a walk to the tower ends on it. The view stops
guessing: "anyone standing within its walls is on top of it" (`ApplicationChasmWorkersView.cpp`) goes, and the
height is the route's. A tower still a site has no deck - its builders walk onto its ground by the ladder, as now.
Routes keep their shape on the ground: the same plots, the same cost.

## Step 2: a debug castle

A PROTOTYPE, debug only - refused in play, not a building, nothing to build or pay. Grey stone pieces painted on
plots, one per plot, each with a height in storeys:

- **BLOCK** - solid stone, a deck on top. Its ground is shut.
- **ARCH** - a deck on pillars, the ground under it open: a road runs on under it. At least two storeys, so the
  passage clears a gate.
- **STAIR** - rises a storey, from the height below its own to its own; from the ground for a first storey. Its
  ground is shut; the stair is the way.

Joined to a standing tower at its deck's height (two storeys), a wall reaches the tower and its ladder. Saved with
the zones and hashed, so a replay holds. Walked by the posted only: an order on a castle plot posts him on top of
it. What it is for: watch a guard walk up a stair, along a wall, over a road with carriers passing under him,
and judge from the camera whether it reads.

**Not in it**: a cost, builders, a site; fields painted over a piece afterwards; picking the top of a piece over
the plot behind it (the pick is the ground's, as for buildings); archers shooting from it.

## Later (not planned)

- **Ramparts**: a wall plot gaining storeys, a walkway on top at the tower's deck height. Towers and ramparts then
  join with no special case.
- **The gatehouse** (`defence_plan.md`) as the arch rule applied to walls: a gate whose wall neighbours are rampart
  or tower at one height gets a deck over it.
- **Storeys for reach**: the higher the post, the further an archer shoots.
- **A deck at the rim's height over a crack**: a bridge over the chasm's cracks, by the same nodes.
- **Zombies**: their own WHO - walls shut but breakable, decks never (unless ladders, later).

## Build order

1. The tower's deck as a node; a walker's height from his route. (BUILT 2026-10-10)
2. The debug castle: blocks, arches, stairs; posted people only; saved and hashed. (BUILT 2026-10-10)

Then the user plays with it and decides whether ramparts are a mechanic.

## As built: step 1, the tower's deck (2026-10-10)

Files: `Walkers.*` (`WalkPlace`, `WalkDeck`, `WalkLayersMeet`; the decks and their joins, `EnsureDecks`, worked out
again at every zones version as the winches' links are; A* over nodes, `FindPath`; `GoalPlace`; `PlanRoute` takes
and gives places), `Economy.*` (`EconomyWorker::route_layer`, one a point, empty when all of it is the ground;
`EconomyRouteLayer`, `EconomyWorkerLayer`; `RouteTo` sets out from his layer and goes straight only from the ground
to the ground; `StopWhereHeIs` for the four places a route was cut to where he stands; `CanWalkPosted` for orders
and guards by hand), `ApplicationChasmWorkersView.cpp` (`WorkerHeight`, `LayerHeight` - the tower's guess gone),
`ChasmSave.cpp` and the economy's hash (the layers, only off the ground), `chasm_pick` (`posted`, `from_layer`, and
a route's `layers`), `chasm_economy` (a worker's `layer`).

- **A node** is a plot's ground below the plots' count, a deck above it. The ground's rules are the old ones; a
  walk's end on a deck is none of the ground's ends, so a standing tower's ground is never entered - the walk ends
  on its deck instead.
- **The ladder** is a join from the ladder's plot to the deck at the cost its edge always had, so routes keep
  their shape: the same plots, now ending on layer 4.
- **Checked** (debug, seed 12, port 8769): a tower painted by the camp, its guard's route
  `[...27684, 27683, 27681]` with layers `[0, ..., 0, 4]`; on post at layer 4, drawn on the platform behind the
  parapet; the way down from layer 4 by the ladder. A second tower, a guard by hand and an order, recorded (632
  ticks): two replays identical.

## As built: step 2, the debug castle (2026-10-10)

Files: `Zones.*` (`ZoneState::castle`, `ZoneCastlePiece` and its readers, `ZoneCanCastle`, `ZONE_OP_CASTLE` -
refused in play; buildings and ground kept off a piece but a road under an arch; a tower's ladder not onto one;
`ZoneShutToWalkers` takes a block's or a stair's ground; `RestoreCastle`), `Walkers.*` (`WalkNodeLayer`,
`WalkJoin` - the one rule the walkers and the drawing share), `CastleMesh.*` (new), `ZoneMesh.cpp`,
`Economy.cpp` (props under a piece covered), `ApplicationChasm.*` (the panel's **Castle (prototype)** tool with
its shape and storeys - no key, no build bar; Erase takes a piece first; the hover's green and red; the props hidden;
the save, its load and the zones hash; `chasm_paint` op `castle`, `chasm_pick`'s `zone_castle`), `ChasmSave.*`.

- **Stairs climb one way.** First built joining every side its layers met, a first stair sloped down to the ground
  on three sides - a mound. Now a stair comes down to the GROUND only on the side across from the neighbours its
  top meets (`STAIR_FOOT_ACROSS`), and joins other pieces wherever they are, so a flight may turn: the test wall's
  did, through a right angle.
- **Drawn where they are walked**: a top's height at its plot's point and, toward a neighbour it joins, where the
  two meet - the same numbers `WorkerHeight` walks him through. A stair's ramp is drawn as steps (six a storey,
  treads half way up each, so he is never more than half a step off them) between side walls that stand half a
  step over it. A block's or arch's edge that joins nothing has a low parapet: the wall-walk.
- **Checked** (debug, seed 12): a road laid north-south at x 244 and a wall across it at z -12 - two stairs up from
  the ground, blocks at two storeys, two arches where the road crosses, a stair up to a block at three. Posted, the
  route from the ground is stair, stair, the wall at layer 4 over both arches, stair, the top at 6; a villager has
  no way up at all, and walks the road under the arches at the ground. An order put a man on the top block; another
  carried on along the road under him. Saved and loaded: the pieces and his layer back. Castle pieces placed, two
  orders and the walks across them, recorded (732 ticks): two replays identical. In play, the op is refused. The
  panel's tool, driven by a scripted click: a block placed, and taken off by Erase.

## As built: an arch over a piece (2026-10-10)

Asked by the user once the castle read well: "a block of level 1 under an arch level 3". Decided: at least **two
storeys of headroom** (`ZONE_CASTLE_HEADROOM`) between a piece's top and an arch over it, and a walk to such a plot
ends on its TOP deck - a guard sent there stands on the arch; the deck under it is still walked through.

Files: `Zones.*` (`ZoneState::castle_over`, `ZoneCastleOver`, `ZoneCastleGoesOver`; `ZoneCanCastle` and the op),
`Walkers.*` (`WalkDeckLayers` - up to `WALK_MOST_DECKS` a plot - and `WalkJoin` by node, `WalkJoinAny` for the
drawing; a stair's way up asks every deck of its neighbours), `CastleMesh.cpp` (`PieceQuarter`, once for the piece and
once for the arch over it, its pillars on the piece's top), the save, hash and tool reports in `ApplicationChasm.cpp`.

- **Painting**: an arch painted where a piece stands goes OVER it when it is two storeys or more above the piece's
  top, and takes its place otherwise - so an arch at 3 on a block at 1 stacks, an arch at 2 replaces it. A piece
  under an arch is swapped only for one the arch still clears ("an arch stands over it"). Taken off from the top
  down, by shift or Erase. Saved as paint, in order: the plot twice, the arch after its piece - no new save field.
- **Walking**: a plot's nodes are its ground and each of its decks; two plots join node by node. Never to the ground
  under a block or a stair - the walkers' joins and the drawing both lost those at once.
- **Checked** (debug, seed 12, `saves/ramparts_crossing.json`): a storey-1 wall north-south at x 244 and a storey-3
  wall east-west at z 8, crossing on three plots, each a block at 1 under an arch at 3. Posted routes: the lower wall
  stair, then layer 2 the whole way, through the crossing; the upper wall's stairs, then layer 6 over it. A route to
  a crossing plot ends at layer 6. A block at 2 painted on a crossing refused. Two guards sent along the two walls,
  recorded (2,283 ticks): two replays identical, ending one on each wall. The save lists each crossing plot twice.
  (One gap in the test wall was the line sampled through a cell's corner - two plots touching only at a point, which
  rightly do not join.)

**Open, for the user's judgement:**
- **Does it read?** The point of the prototype - from the game's camera, in play, with people on it.
- **Picking on top**: a click on a tall piece picks the ground the ray reaches, which can be the plot behind it -
  orders onto a wall are easiest from above.
- **Steps are chevrons**: a ramp is drawn per quarter, so its treads bend at the plot's middle. Fine for a
  prototype; a real stair would want its own piece of geometry.
- **A stair with nothing above it** still takes every side - a mound.
- **Not refused**: a field painted over a piece afterwards (fields do not know the castle).
- The panel's controls were checked by a scripted click, not by eye: the app ran minimised.
