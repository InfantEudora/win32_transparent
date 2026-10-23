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

## As built: a stair on an arch (2026-10-10)

The user's next ask. A STAIR painted on an ARCH, one storey over it, goes ON it: it rises from the arch's top to its
own over the open passage - a stair climbing over a road that runs on under it - and covers the arch's deck, which
stops being a place to walk (`ZoneCastleOverSits`). `castle_over` holds a whole piece now (`ZoneCastleOverShape`,
`...Storeys`, `...Layer`), an arch over a piece or a stair on an arch; the arch under a stair is only ever swapped for
the same arch ("a stair stands on it"). Drawn as a stair whose sides stand on the arch's top (`PieceQuarter`'s
`under`), the arch under it drawing no top and no parapet of its own. Saves unchanged: a piece over a piece is painted
after it, so a save from the arch-over step loads as it was.

- **Checked** (debug, seed 12, `saves/ramparts_stair_arch.json`): on the first wall's east arch over the road (plot
  31093) a stair at 3, the wall beyond it raised to 3. Posted, the wall at layer 4, the west arch at 4, the stair on
  the arch at 5, the wall beyond at 6; a villager walks the road under both arches at the ground. A block, or an arch
  at 3, in the arch's place: refused. A guard walked from the top down over it, recorded (567 ticks): two replays
  identical. The crossing save (`ramparts_crossing.json`) loads with its arches over its blocks.

## As built: picking by the surface, orders to a layer, the way shown (2026-10-10)

The user's: use the engine's position and normal under the mouse to pick, and the level with it; and show where the
selected person is going.

- **The engine reads the surface** under the mouse every frame (`Renderer::ReadPickingAsync`: the G-buffer's position,
  normal and object id) - but only over a PICKABLE object, and chasm had made every chunk unpickable, so it never
  answered. The terrain's and the zones' chunks are pickable now (their ids in `surface_ids`); props, people, water
  and the clouds stay unpickable. Nothing in core changed. In debug, a click on the ground now also fills the engine's
  inspector with the chunk under it, and its Input panel shows the position and normal at the mouse.
- **`PickUnderMouse`**: over a chunk, the surface point stepped `PICK_SURFACE_INSET` back along its normal names the
  plot - the top of a tall wall, a building's wall, its own plot, where the ray against the ground reached the plot
  behind - and `SurfaceLayer` the place on it nearest that height: its ground (none under a block or a stair) or a
  deck. A frame old, as the engine's answer is. Over anything else, the ray as before (`PickUnderPixel`), which
  scripted picks (`chasm_select` press, `chasm_pick` px) still use.
- **What reads the layer**: the hover outline, drawn flat at the deck's height; and a right-click ORDER, which posts
  the person on that layer (`EconomyWorker::post_layer`, saved and hashed only when set; `CHASM_CMD_ORDER` value[1]
  layer + 1, so a recording from before asks for the top). A layer it cannot have - a deck he may not walk, the ground
  under a block - falls back to the top (`Walkers::GoalPlace`). Picked by the ray alone, the top, as before.
- **The way shown**: with a person selected in play, dots along the rest of his route every `PATH_DOT_EVERY`, at
  the route's own height (`RouteHeight`, the walker's height generalised), and a ring where it ends; rebuilt only as
  he passes a dot. A posted person's post marker is laid on the deck he is posted to, not the ground under it.
  `FeetOf` (badges, the person pick) reads his height on a deck too.
- **Checked**: `chasm_pick surface + normal` (the mapping the mouse uses): the crossing plot gives layer 6 at the arch's
  height and 2 at the block's under it; the road under an arch 0; a point on a wall's south face with its normal the
  wall's own plot (31080, layer 4) where with no normal it is the plot in front (1444). Orders: person 3 to the crossing
  at layer 2 ends under the arch at 2; person 1 to the same plot with none ends on top at 6. Recorded (1,124 ticks):
  two replays identical. Screenshots in play: the dots along the lower wall, down its stair, over the grass and up the
  first wall; the post marker at the deck's height.
- **Not checked by a run**: the live mouse - the app ran minimised, with no cursor over it. The engine's readback is
  the same one every app uses; what the mouse picks from it is what `chasm_pick surface` was checked with.

**Open, for the user's judgement:**
- **Does it read?** The point of the prototype - from the game's camera, in play, with people on it.
- **Picking on top** (improved since by the surface pick, below): a click on a tall piece picked the ground the ray reaches, the plot behind it -
  orders onto a wall are easiest from above.
- **Steps are chevrons**: a ramp is drawn per quarter, so its treads bend at the plot's middle. Fine for a
  prototype; a real stair would want its own piece of geometry.
- **A stair with nothing above it** still takes every side - a mound.
- **Not refused**: a field painted over a piece afterwards (fields do not know the castle).
- The panel's controls were checked by a scripted click, not by eye: the app ran minimised.

## As built: levels, as a terrace's (2026-10-23)

The user, with the terraces built (`terraces_plan.md`): on a hill a castle wall showed steps - each plot's deck its own
ground plus its storeys - and a castle at the height of a terrace beside it had no way onto it, its deck tilting
toward it. Asked: the castle's height as a terrace's.

- **A level per piece** (`ZoneState::castle_base`, `ZoneCastleBase`): whole storeys, as a terrace's top, its storeys
  counted from it - its ground built up to it. A drag is one level (`ZoneCastleLevelFor`): its first piece takes the
  level of the piece it is painted on, else the highest ground under its plot rounded up; every piece after takes the
  same. A piece over a piece stands on its level.
- **Up a hill**: a piece stands a storey over all the ground under it, an arch two (`ZoneCanCastle`) - so a level drag
  uphill stops where the hill rises past it ("the ground rises past this level - start a new drag up there, or a
  stair"); a two-storey wall climbs a storey further. Downhill it grows taller.
- **Heights as heights** (`WalkNodeHalves`): a node's height in half storeys - a castle deck's over its level, a
  tower's over its ground, the ground's own, a terrace's top exactly. `WalkJoin` compares those (`SpansMeet`) and
  returns where they meet; a deck level with a terrace's top or the hill joins it, a stair's foot comes down to ground
  as much as a storey under it, a tower's platform is still got onto by its ladder alone. `BuildingLayerHeight` and
  `BuildingMeetHeight` draw and walk them (`CastleMesh.cpp`, `RouteHeight`): flat decks over a hill, and flat onto a
  terrace, with no parapet on that edge and no coping on the terrace's wall there (`TerraceMesh.cpp`).
- **Saved** as `[plot, piece, level]` (an older save's pieces take the level a piece painted there now would) and
  hashed; `chasm_pick`'s `zone_castle` gives `level` and `top_y`.
- **Checked** on `saves/terraces_test.json`: two one-storey blocks in the notch under the top-4 terrace, at level 3 -
  their top 5.2, the terrace's - a posted walk from the terrace onto them (`[32711, 32729, 32731]`, layers
  `[0, 2, 2]`), none for a villager; drawn flat onto the terrace, parapets on the other sides. A one-storey drag up the
  west hill: two blocks at level 5, the third refused as the ground rose; a two-storey drag beside it climbed all
  three at one level. On screen both walls flat.
- Also (`ApplicationChasm.cpp`): the debug view's outlines - hover, selection, a drag's plan - lie on a terrace's top,
  not the ground under it, as the castle's hover lay on its deck.
- **Changed for the prototype's walls and towers**: a tower's platform and a wall now join where their decks are level,
  not where their storeys count the same - a wall built up to a level may meet a tower a storey off.
