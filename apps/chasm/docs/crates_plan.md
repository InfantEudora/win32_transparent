# Crates: dropped loads, and finds on the map

Written up 2026-10-09 and talked through with the user the same day: the decisions are below. Steps 1 (the
crates themselves and the drop), 2 (taking from them and moving them) and 3 (the card, but for FETCH FIRST
and the badge, left open) BUILT the same day - see "As built" at the end. Defence step 1 (`defence_plan.md`, "As built: step 1") lets the player send a
person to a post with a right click. At first, anyone carrying a load when the order came (wood for a
site, food fetched home, a woodcutter's logs) kept it for the whole posting, and with no store held it for
good. Since defence step 2 the load is LOST the moment the order lands (`Economy::Order`), as the user's
stand-in until crates exist. The user wants the load DROPPED instead, and left on the ground as a CRATE.
Crates are also meant to grow into something bigger: finds lying on the map, the way They Are Billions
scatters bonus resources to be reached.

**All of it is rules**: a crate is saved, hashed and replayed like a store's stock. Random crates come
from a hash of the world's seed, never from the shared random stream, the same way the lifelines work
(`lifeline_plan.md`).

## Decided (the user, 2026-10-09)

- **A posted person drops their load** instead of losing it, and it is left on the ground as a CRATE
  holding it. This replaces the stand-in under "Open" in `defence_plan.md` (the load lost at the order).
- **Crates will also lie on the map at random**, found and fetched for their goods, as in They Are
  Billions. This plan deals with the dropped load first, and random crates come next.
- **A crate is a store that villagers can only take from**, and it is destroyed when it is empty.
- **Moving crates to a storehouse is a CARETAKER'S errand**: the builders', as caretakers
  (`defence_plan.md`, "The builder as caretaker"), and the LAST on their list.
- **Building over a crate is refused, with a message.**
- **A few crates are generated on the map, and in debug mode more can be spawned.**
- **Holding becomes dropping, for everyone**: anyone with a load that nobody takes puts it down as a
  crate. The user: "a nice solution for that problem too".
- **One crate for everything**: the same crate whatever it holds, "that makes it clear what it is".
- **The crate prop** is added to `chasm_props.blend` by an agent, and the user tunes it if needed (see
  "The prop", at the end).

## The plan

Proposed in the first draft, with the answers above worked in; the user read it through after and had
nothing to correct. FETCH FIRST and the badge are still proposals, marked where they come.

### What a crate is

- **A store that only gives** (the user's), kept in **its own list in the economy, not as a building**
  (`EconomyState::crates`, in id order; ids start at 1 and are never reused, from `next_crate`). Stocks by
  building id, the zones and the staffing know nothing of crates. A crate has:
  - a plot, and a point on it;
  - a stock by good, like a building's (`std::array<int,GOOD_COUNT>`), so a find can hold a mix;
  - where it came from, DROPPED or FOUND, and for a dropped one, who dropped it and when (for its card);
  - for a find, whether it has been FOUND yet (below); and FETCH FIRST (below).

  It is saved as `crates` (goods by name, as a store's stock is) and hashed in the trace part `economy`.
- **The forest's logs are the model for how a crate behaves, not for how it is stored.** A log lies
  where it fell until a woodcutter fetches it. But `prop_state` is one byte per prop, on a fixed list
  generated from the seed. A crate's goods change as it is emptied, and a dropped crate is made by play,
  so crates need a list of their own.
- **Destroyed when empty** (the user's). It has no room limit, and nothing in it spoils (until spoilage
  exists).
- **Crates lie only on open ground**: a plot with nothing built on it. That rules out a building (a
  bridge and the camp count as buildings), a wall, a garden, a lot and a field. A road counts as open
  ground, so a crate may lie on one.
- **One crate per plot.** A load put down where a crate already lies goes into that crate. This keeps
  picking, the card and the look simple.

### Dropping

- **When**: the moment the order lands, in `Economy::Order`. That is where the load is lost today
  (defence step 2), so the line that throws the load away becomes the line that puts it down, and nothing
  else in the posting changes. This also covers someone ordered on the way to a pressing need: the load
  is already gone from them at the order, so it is dropped there and not carried to the meal.
  - The crate is placed against the zones as they stand when the order lands. A paint command after it
    sees the crate and is refused (below), and both are recorded commands in order, so a replay agrees.
  - The other place it could go is `TakePost`, on the next tick, after that tick's zones. That would need
    a second hook for someone on the way to a meal, since `TakePost` skips them. Not worth it.
- **At once**, with no time taken to put it down. An order may be urgent (`defence_plan.md`, "The pack").
- **Where they stand**, if that is open ground. Otherwise the crate goes on the nearest open plot
  beside it, the lowest index on a tie, the way `GroundSiteApproach` picks where a builder stands. A
  person stands on a building's plot whenever an order lands just as they arrive somewhere.
- **At a store that takes the load, the load goes into the store instead.** That covers standing at
  the store's plot, unloading there, or just arriving. The load was going there anyway.
- **The whole load is dropped**, whatever it is: wood for a site, a woodcutter's logs (as wood), a
  harvest, food or firewood fetched home, or water. The PACK (`EconomyWorker::pack`, defence step 2) is
  not a load, and stays with them.
- **Release is unchanged.** It already just sends them home (`Economy::Order`), since there is no load
  left on them to deal with.

### Taking from a crate, and moving it

- **Villagers take from a crate wherever they take from a store** (the user's: a store they can only take
  from). So a crate is a source of:
  - **wood for a site** (the sources in `FindSiteWork`), ranked by the walk like a store's. A carrier
    who is sent off on the way to a site drops the wood on the way, so the next carrier's nearest wood is
    very likely that crate.
  - **what a house fetches** (`FetchHome`): food, water and firewood. Food dropped on the way home is
    fetched by that house the next time it runs short.
  - **a pressing need** (`SeekSupply`): people eat and drink at a crate as at a store.

  Nobody ever delivers to a crate: it is never in the list of what takes goods (`accepts`), so no
  carrier, harvest or woodcutter takes a load there.
- **The caretakers move crates to a storehouse** (the user's). It is a builder's errand, a load
  (`ECONOMY_LOAD`) at a time, to the nearest store that takes it. A load is picked up only when a store
  takes the whole of it and has the room, so nothing is picked up just to be held.
  - **The jobless do not move crates**, just as they do not resupply towers (`defence_plan.md`: caretaking
    is the builders' only). They still TAKE from crates, for sites and for home, like anyone.
  - So **without a builders' hut, crates are used where they lie and never moved**.
  - **LAST on the caretaker's list** (the user's): after towers, provisions and building. A crate keeps,
    while a site is waiting on its wood. A crate put FETCH FIRST (below) comes before
    building.
- **The person who dropped it is not sent back for it.** That would tie a person to a crate, and a crate
  belongs to everyone. Whatever wanted the load usually finds it anyway (above).
- **If nothing wants the goods and nothing moves them, the crate just lies there.** The goods are on
  the ground rather than on a person, which is the point.

### Building over a crate

- **Refused, with a message** (the user's): a play command that would cover a crate's plot - a building,
  a wall, a garden, a lot, or a field over the crate's cell - is refused, and the play overlay says
  CAN'T BUILD: A CRATE LIES THERE, as it does for the clouds. The check goes where "under the clouds" is
  checked (the zone command's handler, before the zones see the command). The ghost shows the plot red,
  and a wall drawn as a line asks the question per plot, as the clouds rule does. A road may run under a
  crate.
- **A debug placement goes ahead anyway**, and the crate under it is lost, as a tree under a house is.
  Debug commands skip the clouds rule in the same way.

### Random crates: finds

- **A few are placed when a new colony starts** (the user's; `Economy::Reset`, after the camp is found).
  Each one comes from a hash of the world's seed and the crate's number (`Mix`, which also gives the
  settlers their ages and skills), tried again until it lands on a plot that fits. The shared random
  stream is never used.
- **More can be spawned in debug mode** (the user's), by a recorded debug command, `CHASM_CMD_CRATE`
  (like `CHASM_CMD_NEEDS`): a crate with given goods on a plot. It is issued by the tool (`chasm_economy`
  op `crate`, at x, z or a plot) and by the debug panel, at the plot under the cursor. A spawned crate is
  a find, already found.
- **Saved like any other crate.** Spawned and dropped crates have to be saved, so finds are saved whole
  too: one list. A later change to how finds are placed then cannot move them in a saved game. A save
  made before crates existed loads with none.
- **Where**: open, dry ground anywhere a person could one day stand. That includes both halves of the
  valley and the chasm's terraces (balconies and islands), where a find waits for a winch or a
  zeppelin. Never on the mountain, in water, or on a plot with a tree or a rock. Never within the camp's
  first sight, and well apart from each other (the numbers are tuning, below).
- **What they hold** (tuning): finds near the camp are small and hold one good: wood, wheat or beans.
  Finds further out are larger and mixed. Later, finds could hold goods the colony cannot make yet (rope,
  coal, floatstone, from `gameplay_plan.md`), which is where they get interesting.
- **A find is FOUND when its spot is explored** (`Exploration::Explored` at its point). Exploration
  belongs to the app, not the economy, so the app marks finds in the tick, after `TickExploration`. The
  exploration raster is simulation state, so this is as deterministic as everything else. Until it is
  found, no search includes the crate and it is not drawn (it is under the clouds anyway). In debug mode
  with the clouds off it shows faintly, so testing can see where the finds are.
- **When a crate is found, the overlay says so** for a moment, the way it shows a refusal: "CRATE
  FOUND: 12 WOOD, 8 BEANS". Clicking that line glides the view to the crate, as a MOST IN NEED row does.
- **Orders give finds a purpose.** Today people explore only as far as their work takes them. A right
  click can now send someone into the clouds on purpose (`Economy::Order` does not refuse an unexplored
  plot), and finds are the reward for scouting.

### The look

- **One crate for everything** (the user's): the same crate whatever it holds and however much, so it is
  always clear what it is. The prop is `crate_a` in `chasm_props.blend` (see "The prop", at the end),
  instanced like the other props, drawn where the crate lies with a yaw from its id.
- **A badge** (proposed) like a person's - a crate on a dark disc - over each crate in view. At play zoom
  a crate is a few pixels across, and a few badges on the map are not a crowd.

### Selecting and the card

- **A crate is picked the way a person is**: the nearest one within a few pixels of its middle on
  screen, after people and before buildings.
- **The card** shows "Crate" or "Found crate", then its goods, one row each. Then where it came from,
  for example "Dropped by Dirk when sent to a post, summer 3" or "Found summer 9". Last, who is on the
  way to it, and what for: a site, a house or a store.
- **FETCH FIRST** (proposed), a chip on the card, sends a recorded command that puts moving this crate
  ahead of the caretakers' building, the way a site can be put first. A find far out is what the player
  will want brought home.
- **Tools**: `chasm_economy` gives `crates` (id, plot, x, z, goods, origin, by, tick, found, first), and
  op `crate` spawns one (above); `chasm_select` selects a crate by id.

### Dropping instead of holding, for everyone (the user's)

- **Today a load that nobody takes is HELD** (`WORKER_HOLDING`). This happens to:
  - a carrier whose site is pulled down while no store takes wood;
  - a woodcutter whose store is full and whose pile is full too;
  - a farmer whose house filled its pantry before the farmer got home with the harvest.

  The person holding it can do no other work until something takes the load, and with no store at all
  that is forever. It is the same problem the posted carrier has.
- **Now anyone holding a load that nobody takes puts it down** at once, by the same rules as a posted
  person, and is free again. The farmer's leftover harvest becomes a crate on the way home, and the house
  fetches from it when it runs short, which is what the holding was waiting for. `WORKER_HOLDING` keeps
  its number, unused (states are never renumbered).
- **The dead drop what they were carrying**, instead of it vanishing with them.
- **Full stores then show as crates piling up beside them**, which tells the player to build a store
  more clearly than people standing still does.

## Build order

Each step is tested before the next, with the tool and with two replays of one recording compared
against each other.

1. **(BUILT 2026-10-09)** **Crates and the drop**: the list, saved and hashed; a posted person's load dropped (on open ground,
   one crate per plot, or into a store that takes it); the debug spawn and the tool; crates drawn with
   the prop. Tested: a carrier taking wood to a site and sent off leaves a crate where they stood; one at
   a store's door puts the load in the store; a load dropped where a crate lies goes into it; a save and
   load keeps every crate.
2. **(BUILT 2026-10-09)** **Taking and moving**: crates as a source of wood for sites, of what houses fetch, and of pressing
   needs; the caretakers moving crates to stores; building over a crate refused in play mode, with the
   message, and shown red by the ghost. Tested: the site's next carrier takes the dropped wood; a crate
   lies until there is a store that takes it and a builder free, and is then moved there; dropped food
   is fetched home; the jobless never move a crate.
3. **(BUILT 2026-10-09, but FETCH FIRST and the badge)** **The card**: picking, the card, the badge and
   FETCH FIRST. The last two left open for now (the user, 2026-10-09).
4. **Random crates**: placing them from the seed, finding them by sight, the overlay message, and their
   numbers. Tested over the 30 surveyed seeds: every map gets its few, none within the camp's first
   sight, and the same seed always gives the same crates.
5. **Dropping instead of holding**, and the dead dropping their loads.

## Current tuning

First guesses. Each one becomes a named constant beside the `ECONOMY_*` ones.

| What | Value |
|---|---|
| Moved per trip | `ECONOMY_LOAD` (5), as a site's carrier carries |
| Random crates on a map | 5 ("a few", the user's) |
| Closest a find may be to the camp | 60 (the camp's first sight reaches 20-40) |
| Closest two finds may be to each other | 60 |
| A find within 200 of the camp | 8-12 of one good: wood, wheat or beans |
| A find further out | 20-30, of two goods |

## Open

- **FETCH FIRST and the badge**: proposed, not yet talked through.
- **Water in finds**: left out for now. A collector is cheap and water is everywhere.
- **Spoilage**, when it comes: if food in a crate out in the open spoils faster than food in a store,
  clearing crates starts to matter more.
- **Zombies** (P6) could break crates open, or guard a find, as in They Are Billions.

## The prop (2026-10-09)

`crate_a`, added to the Export collection of `art_source/chasm/chasm_props.blend` by an agent, for the
user to tune. Preview: `art_source/chasm/previews/crate.png`, beside a man at game scale and a stump.

- **The look**: light cut-wood panels (the path cell, column 13, which the stumps' tops and the logs' ends
  already borrow) set 0.012 into a dark frame (dark bark, column 23), with a diagonal brace across each
  side and the lid. The lid matters most, since the game's camera looks down on it. Both cells are the
  same in every season's row.
- **Size**: 0.52 x 0.42, 0.40 high - a little under half a man's 0.86 in the game (the figures are drawn
  at 0.46), and about a stump's height. No bottom face; the walls start 0.04 under the ground, as the
  other props sink.
- **100 triangles**, flat shaded, origin at the centre of the base. It stands at (20, -15) in the
  authoring layout, after `bush_b` on the stump and log row.
- **Exported with step 1**: the collection's own exporter, its saved settings - the same as the one-click
  export. Compared node by node with the glb before it: the other 36 nodes byte for byte the same, and
  `crate_a` added (200 vertices).

## As built: step 1 (2026-10-09)

Files: `Economy.*` (CRATES in `Economy.h`; `EconomyCrate`, `EconomyCrateGround`, `EconomyCrateOn`;
`DropLoad`, `AddToCrate`, `LoseCoveredCrates`, `SpawnCrate`; `Order`, `Tick`, the save and `Restore`),
`ChasmSave.cpp` (`"crates"`, `"next_crate"`), `ApplicationChasmEconomy.cpp` (`CHASM_CMD_CRATE`, the hash,
the tool, the panel), `ApplicationChasmCrates.cpp` (new: drawing them), `ApplicationChasm.*` (the command,
loading the prop, `UploadCrates` from `PreRender`, the panel's call), `makefile`, `chasm_props.glb`.

- **A crate** is `EconomyCrate` in `EconomyState::crates`, in id order (`next_crate`): its plot, its point,
  a stock by good, where it came from (`CRATE_DROPPED`, `CRATE_FOUND`), who put it down and the calendar
  tick. Saved as `crates` (goods by name), hashed in the trace part `economy`; `crates_version` tells the
  view. A save from before crates loads with none.
- **The drop** is `DropLoad`, where `Order` used to throw the load away: at a store that takes it, into the
  store as far as it has room; the rest at his feet if his plot is open ground, else onto the nearest open
  plot within `ECONOMY_CRATE_RINGS` (6) steps - stepping only across open ground, a camp and a standing
  bridge, on his own level. Into the crate already on that plot if there is one.
- **Open ground** is `EconomyCrateGround`: dry, off the mountain, no building, no ground but road, no field
  over the cell at the crate's point.
- **Built over**: when the zones change, a crate no longer on open ground is lost (`LoseCoveredCrates`, in
  `Tick` after `Derive`). Only a debug placement can do that until step 2 refuses it in play.
- **Spawning** is `CHASM_CMD_CRATE`, recorded: subtype the plot, value[0] the good, value[1] how many - one
  good a command, so a crate of several goods is several, each into the same crate. A spawned crate is
  `CRATE_FOUND`. Refused off open ground ("not open ground"). From `chasm_economy` op `crate` (x, z or
  `plot`, `contents` by good name) and from the debug panel: the selected plot's crate, if any, and a good,
  an amount and "Put a crate here" / "Add to the crate", disabled off open ground.
- **Drawn** (`ApplicationChasmCrates.cpp`): one instance set of `crate_a` for every crate, at scale 1 (the
  rocks' and bushes'; trees and their logs are 0.7), turned by a hash of its id, rebuilt when
  `crates_version` moves.
- **Tool**: `chasm_economy` gives `crates` (id, plot, x, z, goods, origin, by, by_name, day).
- **Found in testing**: the first search for open ground stepped on only across open ground, and a carrier
  standing among the camp's tents lost his load - every plot round him was the camp's. It now steps across
  the camp too, and six rings out.
- **Also found**: a build of chasm at 16:20 caught these files half edited, so the next build relinked
  objects from two versions of `ApplicationChasm.h` and the crates were never drawn. Touching the changed
  headers and building again put it right.
- **Tested** (seed 12, debug, five house sites placed in play by the camp, sim paused and stepped):
  - Floris, carrying 5 wood among the camp's tents, ordered off: a crate of 5 wood, by him, on the open plot
    just outside the camp; his load gone from him.
  - Jelle, carrying wood on open ground, ordered off: a crate where he stood, to the thousandth.
  - Siem, unloading 2 wood at a store when ordered off: the store went from empty to 2 wood, no crate.
  - Spawned wood 8 and beans 4 at one point: one crate holding both; water 3 there after: the same crate;
    a spawn on a house site refused, "not open ground".
  - Saved and loaded: the same three crates. A debug house painted on a crate's plot: the crate gone on the
    next tick.
  - Replay: a 251-tick recording with an order that dropped wood, a spawned crate and a release, replayed
    twice: every tick's hash equal, nothing changed between ticks, the same two crates at the end of both.
  - Drawn: a crate beside the camp's tents, in a close screenshot (distance 6), carriers walking past it.

## As built: step 2 (2026-10-09)

Files: `Economy.*` (CRATES in `Economy.h`; `ECONOMY_STORE_CRATE`, `ECONOMY_CRATE_STAND`, `WORKER_TO_CRATE`
(27) and `WORKER_LIFTING` (28); `SourceStock`, `CrateById`, `CrateTouched`, `CrateStand`, `SourcePoint`,
`MoveCrate`, `EconomyCrateInTheWay`; `FindSiteWork`, `GoodFree`, `SourcePlot`, `SeekSupply`, `FetchFrom`,
`FetchHome`, `TakeHome`, `TickWorker`, `Replan`), `ApplicationChasm.*` (the zone command's refusal, the
ghost's key), `ApplicationChasmGhost.cpp`, `ApplicationChasmEconomy.cpp` (a worker's `crate` in the tool).

- **A crate as a source**: a worker's `store` names a crate as `ECONOMY_STORE_CRATE | id`, the way a
  garden's site is named in `site`. Every stock taken from goes through `SourceStock` - a building's or a
  crate's, NULL once a crate is gone - so a crate emptied, or built over, while someone walks to it is
  simply nothing there when he arrives, as an empty store is. He stands `ECONOMY_CRATE_STAND` (0.5) short of
  it on the side he comes from (`CrateStand`), as at a tree's foot.
- **Taken from wherever a store is**: `FindSiteWork` lists crates beside the stores as places a site's goods
  lie (by good, what nobody is already on his way for - `GoodFree`); `SeekSupply` and `FetchHome` list them
  beside the buildings, by the same masks - so provisions, outside `GOODS_FOOD`, stay a last resort there by
  themselves (defence step 5); `FetchFrom` takes any crate; `TakeHome`, a meal (`WORKER_SUPPLYING`) and a
  posted person's pack refill take from it. Every take ends in `CrateTouched`: the view told, and the crate
  gone if that emptied it.
- **Moved by the caretakers**: `MoveCrate`, from `TickWorker`'s at-home look, only for a builder and only
  when neither `FindSiteWork` nor `FindRoadWork` gave him anything - the last of his errands (the user's).
  Defence step 5's provisions errand is to go in before `FindSiteWork` (agreed with that window). The
  nearest crate holding a good some store takes a whole load of, with a way from the crate to that store,
  that is not already being emptied by others; to it (`WORKER_TO_CRATE`), a load picked up
  (`WORKER_LIFTING`: the good he came for, else any a store still takes whole), and off by `FindStore` as
  any carrier. Nothing taken if no store will have it by then.
- **Building over a crate refused in play**: `EconomyCrateInTheWay` - a building on its plot, ground but road
  there, a field or range over its cell - asked by the zone command beside "under the clouds" (play commands
  only; refusal "a crate lies there", on the play overlay as CAN'T BUILD) and by the ghost, red, a wall line
  per plot. The ghost's key carries `crates_version`, so it turns red as a crate lands under the cursor.
- **Found on the way**: `chasm_tool` crashed on every call since the range was added: its name table had 15
  names for 16 tools, and every name was compared against the NULL at the end. "range" added, with a
  `static_assert` that the table matches `ChasmTool`.
- **Tested** (seed 12, debug, sim paused and stepped):
  - A crate of 7 wood: lay untouched with no store (600 ticks); with a store and no builders' hut, still
    untouched and nobody in `to_crate`/`lifting` (600 more); a builders' hut placed - Dolf, its builder, moved
    it in two loads (5, then 2), the crate gone when he lifted the last; the store 7 wood. Only he moved it.
  - Refused in play over a crate: a house, a garden, a field - "a crate lies there"; a road accepted, the
    crate still there.
  - A house (debug) with a crate of 9 wheat and 9 water beside it: its family fetched 5 water from the crate.
  - Eight house sites placed in play past what the camp's and the store's wood could pay for: two jobless
    carriers took 5 of a 12-wood crate's wood for them.
  - Jelle posted beside a crate of 6 water, made thirsty until his pack was empty: he drank at the crate and
    filled his pack from it (6 -> 2).
  - The ghost in play mode, the house tool held over a crate: red.
  - Replay: 1,201 ticks recorded with crates being emptied by site carriers and a house, a spawned crate, a
    carrier's load dropped by an order, a refused paint over a crate - replayed twice: every tick's hash
    equal, nothing changed between ticks, the same two crates at the end of both.

## As built: step 3, the card (2026-10-09)

Files: `ApplicationChasmSelect.cpp` (`PLAY_PICK_CRATE` in `PlayPickUnder`, `CrateCard` in `MakeCard`,
`RelatedPeople`, the crate outlines in `UploadSelection`, `Doing`, the tool), `ApplicationChasm.h`,
`ApplicationChasmCrates.cpp` (`CrateYaw`, shared with the outline). View only: nothing recorded, nothing a tick
reads.

- **Picking**: a crate is `PLAY_PICK_CRATE`, by its id, the plot it lies on with it. Under the cursor, after
  people and before buildings: the nearest crate whose middle (`CRATE_MIDDLE`, 0.2 over its ground) is within
  `CRATE_PICK_PX` (12, at 810 high) of the cursor on screen - as a person is picked by his.
- **The card** (`CrateCard`): "Crate" or "Found crate"; a goods strip, "Holds - anyone's", striped where
  somebody is already on his way to take it (`EconomyReserved`, which a crate's flagged id needed nothing
  for); where it came from - "Dropped by Kaat, spring 1" (the late ... for the dead), or "Found spring 1", the
  year only when it is not this one; then a line for everyone on the way to it or taking from it, with what
  he is doing ("Floris Veen, fetching wood"), or "Nobody is on the way to it". The wording leaves out WHY it
  was dropped - "when sent to a post" in the plan's example - since step 5 will drop loads for other reasons.
  A crate emptied or built over while selected loses its card, as a person who dies does.
- **Outlined**: the crates are one instance set, which the outline pass skips, so two outline-only copies
  of the crate's mesh (`crate_outline`, the mesh retained for good) stand where the selected crate lies, in
  the selection's orange, and the hovered one, faint - turned by `CrateYaw`, as the set draws it. The people
  on their way to it are pinned and outlined in the related colour (`RelatedPeople`).
- **Doing**: "going to move a crate" and "lifting from a crate" for step 2's states, which had shown an empty
  line on a builder's card.
- **Tool**: `chasm_select` op select takes `crate` (an id), and x, z selects a crate lying on the plot there
  when no building or gate is; op under and the selection report kind `crate`.
- **Tested** (seed 12, debug, play mode, minimised - core's overlay draws there):
  - By id: Kaat's dropped 3 wood - "Crate", "Holds - anyone's: wood 3", "Dropped by Kaat, spring 1", "Nobody
    is on the way to it"; a spawned crate of 6 wheat and 2 water - "Found crate", "Found spring 1"; crate 999
    refused, "no such crate".
  - Under the cursor: a scan of the screen found the crate picked over a patch about 24 pixels across on its
    plot; 60 pixels off, nothing. A press there selected it and drew its card, the crate outlined in orange.
  - Floris on his way to fetch Kaat's wood for a site: the card's line "Floris Veen, fetching wood", the strip
    striped "3 reserved", and Floris pinned in amber.
- **Not checked**: the hover's faint outline over a crate (it needs a mouse over the window; the code is the
  selected outline's with the hover's colour).
