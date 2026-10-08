# Goods, stores and the first production (P2 + P3)

The next step of `gameplay_plan.md`'s prototype, after the calendar (P1). Built on the buildings as things
(`buildings_plan.md`): a store, a woodcutter, a field, a water collector are each a building with an id.
Agreed 2026-10-06; P2 and P3's first version BUILT 2026-10-06 - see the end.

## From the user (2026-10-06)

- **Wood, food and water first.** Food in three keeping-kinds - the crops wheat (medium, and a material
  too), greens (fast, not over winter) and beans (high value, long keeping) stand for one of each.
- **Many stores, and what each takes.** A store BUILT NEXT TO a workplace takes only that workplace's
  goods - a store beside the woodcutter's hut is his wood store. A store standing on its own takes
  anything by default, and the player can set which goods it takes.
- **The woodcutter walks straight to his tree.** Off the roads he need not follow the grid: he goes
  straight to the nearest tree - or the nearest log already lying felled.

## The plan

### Goods (P2)

- **Five goods**: wood, water, and the three foods by crop - wheat, greens, beans. Food is not one pile,
  because what keeps is the whole point of the three; "food" is their sum where a number is wanted.
- **A store's room** is its floor area: plots x storeys x a room per plot, the measure houses already use.
  Every good takes room by the unit; no good is bulkier than another yet.
- **What a store takes** is a set of goods per store, simulation state, saved and hashed:
  - **Next to a workplace** - sharing a plot edge with it - a store takes exactly that workplace's goods,
    and the panel shows it as fixed while it stands there. Next to two (a woodcutter and a collector), it
    takes both. Pulled down or the workplace gone, it falls back to its own setting.
  - **On its own**, everything by default; the player ticks goods off and on (a panel button per good,
    and an MCP op). A recorded zone command, like a crop change.
- **Stocks** are per store, a whole number per good; shown on the panel's building info and summed for
  the colony in the overlay (wood, food, water beside the date).

### Production (P3)

- **Every workplace has its worker** - a STAND-IN until P4 brings families: one walker who belongs to the
  building, sleeps there and works from it. P4 swaps him for a person from a house; the job stays.
- **Delivery**: a worker carries what he made to the nearest store that takes it (an attached one is the
  nearest there is). Nearest by walk; none in reach, he holds it and waits. Carriers between stores, and
  people fetching food, are P4.
- **The woodcutter**:
  - Picks the nearest tree (pines, oaks and the biome trees - not bushes or rocks) within a reach of his
    hut, or a log lying felled if one is nearer.
  - **Walks straight** to it when the line is clear - over open ground and roads, on one level, not across
    water, a building, a field, a wall or a fence; samples along the line say so. Where it is not clear he
    takes the grid's A* as walkers do now. So the walk is free in the forest and the grid's in the village.
  - Fells it (some seconds), and it leaves a **stump and a log** - the props exist (`stump_a`, `log_a`).
    Carries the log home, or to the nearest store that takes wood, as wood.
  - **The forest shrinks**: which trees are felled is simulation state - one bit per prop, by its index in
    `ForestData` (stable for a world), saved and hashed. The view drops felled trees from their instance
    sets and adds the stumps. Regrowth and a forester are later.
- **Fields**: the crop grows through its season, not under snow (P1's front); at harvest the field yields
  by its area and crop, and the farmer - the field's stand-in worker - carries it in, in loads, to the
  nearest store that takes it. Wheat harvests late summer, beans in autumn, greens twice (end of spring and
  of summer) since they grow fast. The rows could show the growth - bare, green, ripe - a look-only cue.
- **The water collector** fills slowly all year (not while the water is under snow, P1) and its worker
  carries it to the nearest store that takes water.
- **Spoilage** belongs here as the reason the three foods differ: greens rot when winter comes, wheat a
  little each season, beans not at all. Small, and could wait for P5 (eating), where it starts to matter.

### What it is measured by

- The replay test grows a village with a woodcutter, his attached store, a free-standing store set to food
  only, a field and a collector, runs a few days, and must end with the same stocks and felled trees in
  both replays.
- The panel and `chasm_building` show each store's stock and what it takes; the overlay the colony's totals.

## Decided (2026-10-06, the user's answers)

1. **A stand-in worker per workplace** until P4: a walker who belongs to the building. P4 swaps him for a
   person from a house; the job stays.
2. **The worker carries his own goods** to the nearest store that takes them. Carriers between stores
   are P4.
3. **An attached store's goods are fixed** while it stands next to the workplace.
4. **Spoilage comes with eating**, in P5.

## As built: the simulation (2026-10-06)

Files: `Goods.h`, `Economy.*` (the rules), `ApplicationChasmEconomy.cpp` (tick, publish, hash, panel, the
`chasm_economy` tool), `Zones.*` (a store's `allow`, `ZONE_OP_STORE_ALLOW`, `split_from`), `Walkers.*`
(`PlanRoute`; a walk may now start or end on a water collector's wet plot), `ChasmSave.*` (`"economy"`,
and a store's `"allow"`), the overlay's totals in `ApplicationChasmTime.cpp`.

- **Goods**: wood, water, wheat, greens, beans (`Goods.h`). The overlay shows WOOD, FOOD (the three
  summed) and WATER in the stores, at the top centre (the date has moved to the top right, 2026-10-08).
- **Stores**: room = floor area x 7.5 (about 30 a plot-storey; 5 until 2026-10-08, the user's 1.5x for
  a decent row of stores at ten people). Next to a woodcutter or collector (a shared
  fine edge) or a field (one plot away - buildings keep a plot off fields) a store takes exactly that
  workplace's goods (`attached`, derived); otherwise its own `allow`, set per good in the panel (select
  the store) or by `chasm_economy store_allow`, a recorded zone command. A split store shares its stock
  by floor area; a store pulled down loses it.
- **Workers**, one per workplace, made and removed with it, ticked in id order. A round:
  - woodcutter: the nearest standing tree within 40 of his hut, or a log lying felled, that nobody
    else is after and nothing is built on; walks there; fells it (6 s, it becomes a log), picks the log
    up (1 s, a stump is left) and carries 4 wood to the nearest store that takes wood and has room;
    puts it down (1 s) and walks home;
  - farmer: waits at his field; at harvest carries its food off in loads of 5;
  - water carrier: carries the collector's water off in loads of 5.
  Nobody to take the load: he waits where he is, holding it, and looks again every half second.
- **The woodcutter's pile** (the user, 2026-10-06: never idle in the wood): no store takes his log, he
  carries it home and stacks it outside the hut - the hut's own stock, up to `ECONOMY_HUT_PILE` (24 wood,
  six logs, drawn as a stack on the first open plot beside the hut, `ApplicationChasmPiles.cpp`). At home,
  the pile goes first: whenever a store takes wood he carries the pile there a log at a time before he
  fells again. Only with the pile full and no store does he wait - at home. Measured: with no store he
  piled six logs and waited; a store painted beside the hut had all 24 within two of his rounds, and he
  went back to felling.
- **The farmer and the water carrier** only pick a load up when a store will take it; until then it
  waits at the field or the collector, which is their pile.
- **Going home between trees is deliberate** (the user): it is what makes it worth building his house
  near the hut and its store, once he has a house (P4).
- **Walking**: straight where the line is clear (`EconomyStraightClear`: every 0.5 units along it, the
  plot must be on his level, dry, off the mountain, not built on, not inside a field, a neighbour of the
  last with no wall or fence between and no step steeper than a walker's) - otherwise A* over the plots,
  with a straight step from where he stands onto the grid and off it at the end. Re-planned from where he
  stands whenever the zones change; a route is saved, never re-planned on load.
- **Fields** grow every tick that is not winter and not under the snow: wheat 15 days, greens 8, beans
  20 (18 and 25 until the frost came, 2026-10-08 - `fields_plan.md`, "The frost"); then they yield 1.2,
  0.7 or 0.9 food per unit of area into their own stock (double the first 0.6 / 0.35 / 0.45, the user's,
  2026-10-08: about 37 cells were needed to feed ten), and grow again. A crop changed is sown again
  from nothing.
- **Collectors** fill a unit every 4 s, up to 10, not under the snow.
- **Measured** (debug, seed 1, 3x speed): a woodcutter with a store beside him fells and stores a tree
  every ~15 s of game time, filling the store (56 of its 59) and then waiting with his next log; a
  2-cell greens field yielded 11 at harvest, carried in 5 + 6 to a free store set to food only; a
  collector's carrier, with no store taking water, waited holding 5.

## As built: the look (2026-10-06, worker window 39)

Files: `ApplicationChasmWorkersView.cpp`, `CropMesh.*`, `ZoneMesh.*`, the forest's sets in `ApplicationChasm.cpp`.

- **The forest shrinks**: a felled tree drops out of its set; a stump stands in its place, and while its log
  lies there, the log beside it. Only the felled tree's chunk is rebuilt.
- **Workers** are the walkers' figure in a tunic by job - woodcutter dark green, farmer wheat yellow,
  water carrier blue - with what he carries on him: a log on the shoulder, a sack on the back, a pail in the
  hand. A woodcutter at work bobs with his swings.
- **Fields grow** in four looks: bare tilled ridges (under 10% grown), sprouts (under 35%), green rows (under
  75%), and ripe in the crop's colour - the old look.
- Found on the way: a mesh shared by objects is freed when the last one lets go of it (`Object::SetMesh`
  releases), so the workers' six meshes are retained for good - without that the third farmer's mesh swap
  freed a mesh the renderer still wrote to.
- **Tested**: the replay test (a woodcutter on his way to a tree, his store, a store set to food only)
  PASSES in debug and release.

**Open:**
- A worker who is walking when his store fills keeps going and finds another at the door; nothing reserves
  room ahead.
- A route over a winch would be drawn cutting across the cliff; no workplace sits on a balcony yet.

## As built: house storage and the woodpile's lot (2026-10-06)

- **A house keeps food and firewood** - and water, since P5 (`needs_plan.md`), whose families eat and
  drink from it: `room` at
  `ECONOMY_HOUSE_ROOM` (4 since needs step 2; 1.5 before) of its standing floor area - about 16 a
  plot-storey against a store's 30 - and `keeps` = `ECONOMY_HOUSE_KEEPS` (the foods and wood). Shared out on
  a split like a store's stock, lost when it is pulled down. A house is not in `accepts`, so no carrier
  delivers there: its own family fetches (`needs_plan.md`, step 2). The house card shows "Food and firewood
  n of room".
- **The woodpile is on a LOT** beside the hut (`ZoneLotBeside`, the lowest plot index of any of the hut's
  plots' lot neighbours; `EconomyState::pile_plot`, derived). ONE LOT PLOT TO A WOODCUTTER: they are given
  out in building-id order, each taking the lowest one nobody before him has (the user found two huts
  sharing one). A NEW woodcutter is refused without a lot of its own beside it - a lot plot no other
  woodcutter stands beside (`ZoneLotTaken`) - in both modes ("a woodcutter needs a lot of its own beside
  it, for his woodpile" - a placement rule
  only, so a load and the debug checks pass the building's own id). With no lot standing - erased, or
  still a site - he fells nothing, and his card says so.
- **The hut is the yard's door**: a lot is fenced, so wood goes onto the pile and comes off it at the hut
  plot beside it (`pile_door`), exactly where he always came home to - nobody walks into the yard. The
  pile view (ApplicationChasmPiles.cpp) draws the logs on `pile_plot`, leaning toward that hut plot.
- The lot's fence is low now: stakes 0.52 (were 0.95), the gate posts 0.95 (were 1.35) - about the
  garden wall's height.

## Fixed: a field's harvest went untouched (2026-10-07)

Found by the user in play: crops waiting at a field were picked up by nobody - not its farmer, not anyone
hungry. A field lies on coarse cells, so it has no plots in `plots_of`, and `Economy::SourcePlot` - where goods
are taken from a building - asked only those: -1 for a field. Since `5dbb953` (the woodcutter's pile door)
`FetchFromWorkplace` walks the farmer there, so his way to plot -1 failed and he never set out; and
`SeekSupply` passes over a building with no plot to walk to, so the hungry never went to a field. Now a building
with no plots is reached at its `home_plot` - a field's middle vertex, where its farmer went before - and
`FetchFromWorkplace` no longer reads a position for plot -1.

Measured (seed 12, debug): a one-cell greens field's harvest (3) sat untouched with no store; a store beside it,
and its farmer walked out and carried the 3 in. With the camp out of food and no store, a villager made hungry
walked to the field and ate them there.

The rule that was never broken: a farmer only fetches when some store takes his crop and has room for a load.

## Fixed: walkers twitched back on every placement (2026-10-07)

Found by the user in play: placing anything made the villagers on their way step back and then go on. Any
zone change re-plans everyone walking (`Economy::Replan`), from where he stands - and on a grid route
`RouteTo` started from the plot under him, which part way along an edge is often the one he has just left. He
walked back to it and turned round. Measured with a paused, tick-by-tick run (seed 12, carriers routing
round a row of houses to a site, a garden plot painted and erased eight times): 24 of 29 walkers on grid
routes stepped back on the tick of the change; straight walks did not.

Now, when the path's first plot is behind him (the turn there sharper than square) and its second can be
walked to straight, he goes on to the second along the edge he is on. The same run: 0 of 45. The carriers
still finished the site.

## The woodcutter: logs first, two a round (2026-10-08, the user's)

Seen in play: with logs lying all round the village - the road builders fell the trees in a road's way and
leave them where they fall (line_works_plan.md) - the woodcutters still felled trees. He took the nearest
piece of wood by the straight line from his hut, standing or felled alike, and his hut is in the wood while
the logs lie by the roads, so a tree was nearly always nearer. And he carried one log a round.

Now (`Economy::WoodNear`, `GoForWood`, the log branch of `TickWorker`):

- **Logs first.** Of what lies within his reach of the hut (`ECONOMY_WOODCUTTER_REACH`), the nearest felled
  log, before any standing tree, however near; a tree only with no log in reach.
- **Two logs a round.** With a log on his shoulder, he looks for another within `ECONOMY_LOG_PAIR_REACH` (12,
  about six plots) of where he picked it up, and picks that up too - `ECONOMY_WOODCUTTER_LOGS`, 8 wood - before
  he carries them off; only when a store, or his pile, has room for both. A tree he felled counts as his
  first: a log lying near it is his second.
- **The second gone** when he gets there - another woodcutter was quicker - he carries off the one he has
  rather than walk home with it; and so does one moved to another job on his way to it
  (`AssignJobs`), to a store, or holding it.
- Nothing new is saved: what he carries was always saved and hashed.
- **Measured** (`village.json`, seed 12, debug, a copied exe): planned roads painted through the wood, cleared
  by the idle, left their logs. With their piles full the two woodcutters waited; with stores painted they
  carried the piles over first, then went for logs - and felled nothing in the two minutes after, at speed 3,
  while logs lay in reach. Of the log rounds most were two logs, 8 wood; a single log, 4, where no second lay
  within reach. The logs by the wood went from 9 to 0.

