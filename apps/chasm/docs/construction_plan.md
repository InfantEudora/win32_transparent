# Construction (play mode step 5)

Step 5 of `play_mode_plan.md`: in play mode a building is not placed but BUILT. Agreed with the user
2026-10-06: **wood is the only thing a building takes, and idle people carry it over.** BUILT the same day.

## Decided

- **Wood only.** A storey takes `ECONOMY_BUILD_WOOD_PER_AREA` (2) wood per unit of its floor area, about
  8 for a plot - a tent half that, a winch half again more. Fields and roads cost nothing and are still
  painted at once, in either mode; gardens and lots cost a little since 2026-10-06 (see the end).
  **Since 2026-10-09 a site can take other goods too** - by kind, carried the same way from wherever each is
  nearest (`defence_plan.md`, "As built: step 4"). No kind takes any yet; wood is still all that is built with.
- **Idle people carry and build.** Anyone without a job: from his home to the nearest wood, a load
  (`ECONOMY_LOAD`, 5) to the site, a while there building with it (`ECONOMY_BUILD_SECONDS`, 5, at his
  pace - strength), and home. There is no builder job and no builders' hut yet.
- **The settlers bring supplies.** A new colony's camp holds wood, food and water (`ECONOMY_START_*` - tuning,
  see `needs_plan.md`'s "Current tuning"; first 40 wood, 20 wheat, 10 beans and 20 water), so the first few
  buildings go up before there is a woodcutter. The overlay's totals count them.
- **Debug mode still places at once.** The difference travels in the command (`value[3]`, the play
  flag of step 3), never in the mode: a debug command builds what it paints, a play command plans it.

## As built

Files: `Zones.*` (`standing`, `ZONE_OP_BUILD_RAISE`), `Economy.*` (the sites, the carriers, the
supplies), `ApplicationChasmEconomy.cpp` (applying the raises, the panel, `chasm_economy`),
`BuildingMesh.cpp` (`BuildSite`, the scaffold), `ChasmSave.cpp`, `ApplicationChasmWorkersView.cpp`.

- **Zones** - per plot, `standing[v]` of its `storeys[v]` are built; the rest are a site. A play command
  raises `storeys` and leaves `standing`; a debug command sets `standing = storeys` on the plots it
  touches (so a debug click on a site finishes that plot). Removing a storey takes the top off, built
  or not. `ZONE_OP_BUILD_RAISE` stands the next storey of a plot - not a recorded command: the app
  applies the economy's `Raises()` on the tick they are built, so a replay builds them by itself.
  The rules and the walkers treat a site's storeys as there (nothing else may be painted on it, and it
  is walked into only at the end of a walk, like a building); everything a building DOES goes by what
  stands - `ZoneBuildingFigures` gives standing floor area, and the site's apart (`site_storeys`,
  `site_area`). Arches bridge only standing storeys.
- **Economy** - `EconomyState::site`, by building id, is the wood brought to a site. What a site still
  wants is its planned storeys' wood, less what it has, less what is on its way (a load for each carrier
  still going to fetch, what each carries for it), so carriers do not over-bring. An idle person, on
  his look, takes the pair of (wood, site) with the shortest straight walk from him to the wood and on
  to the site - ordered by the player's queue and "build first" since 2026-10-07 (below): wood is a
  standing store's, the camp's supplies or a woodcutter's pile. His round is
  `to_fetch`, `fetching`, `to_site`, `building`, home. When he finishes building, every storey the site's
  wood pays for stands - the lowest level first over the whole building, so it rises level by level.
  A site's storeys do nothing until they stand: no room, no family, no worker, no tent to sleep in.
  A site pulled down loses the wood it was brought; one finished while a carrier walked to it sends his
  load to a store.
- **The look** - a site plot has scaffolding: poles at the corners of the plot's share of its cells, from
  what stands up past what is planned, a ring of planks at each storey to come, a brace on every other
  side; where nothing stands, a timber frame on the ground. A camp's unpitched tent is its poles and
  rolled canvas lying on the ground. A builder bobs as he works, like a woodcutter at a tree.
- **Saves** - a site plot is `[plot, storeys, standing]` (a plain `[plot, storeys]` stood whole, so old
  saves load built); the economy's `"sites"` and each person's `"site"`. Hashed: `standing` with the
  zones, `site` with the economy.
- **Tools** - `chasm_paint play:true` plans; `chasm_economy` lists `sites` (storeys to build and
  standing, wood needed and brought, the carriers); a building's panel says how far along it is.

**Open:**
- Delivered wood is not shown at the site; logs piled by the scaffold would read well.
- No refund for a site pulled down.
- ~~Jobs come first: a colony with as many workplaces as people builds nothing.~~ Answered 2026-10-07 by the
  BUILDERS' HUT (`builders_plan.md`): a workplace whose builders are staffed before every other job.

## Gardens and lots (2026-10-06, the user's)

A garden or a LOT (the town tool, renamed: a fenced yard) painted in PLAY is a site too, a plot at a time:
`ZoneState::ground_built[v]` 0 until built, and until then it has no wall and encloses nothing
(`ZoneGroundStands`, which `ZoneBoundaryBetween` - the walls, the walkers, the straight walks - asks).
It takes a little wood, `ECONOMY_GROUND_WOOD_PER_AREA` (0.5) of its area - about 2 a plot - carried by the
same idle people. A debug paint, and a field in either mode, is there at once, as before (a road painted
in play has been a site too since line works step 2 - see the end).

- No building id, so a worker's `site` names the plot as `ECONOMY_SITE_GROUND | plot`; the wood brought is
  `EconomyState::ground_site`, by plot (saved as `economy.ground_sites`, hashed with the economy).
  `SiteAlive` / `SiteNeed` / `SiteBrought` branch on the flag; `FindSiteWork` offers the ground sites after
  the buildings, so on a tie a building comes first.
- Built FROM OUTSIDE: a builder stands on an open plot beside it (`Economy::GroundSiteApproach`), never on
  it - otherwise the fence goes up round him and he is shut in (seen in the first run).
- Finished: `Economy::GroundRaises`, applied on the same tick as `ZONE_OP_GROUND_RAISE` (op 13).
- In a save's ground list a site's kind carries `ZONE_GROUND_SAVED_SITE` (16). Old saves were let go (the
  user), so nothing reads the town-era format.
- The build bar's BUILDING / WOOD line counts each garden or lot plot still to build as a site.

## Which site goes first (2026-10-07, the user's)

Nearest-first stays - it is what makes a colony build close together - but the player gets a say in two
ways, both in `Economy::FindSiteWork`:

- **The queue.** Of the errands about as short as the shortest - within `ECONOMY_QUEUE_SLACK` (8 world
  units, about 4 plots) of it - the building PLACED first goes first. Building ids are handed out in
  placement order and never reused, so the queue is the id. A site clearly nearer still wins; the queue
  only orders what is close enough for the walk not to matter. Gardens and lots have no place in the
  queue (no id), so they keep their place by distance among the buildings being reordered.
- **Build first.** A site can be PUT FIRST (`ZoneBuilding::f_priority`, the site card's "Build first"
  chip, `ZONE_OP_SITE_PRIORITY` - a recorded zone command, op 14): its errands come before every other
  site's, however far. Among several put first, the same queue rule. Carriers still do not over-bring
  (`SiteLeft`), so once a site's wood is all on its way the next idle person goes to the next site. It
  drops by itself when nothing of the building is left to build (`Zones::SettlePriorities`), so a storey
  added later is queued like any other. Its scaffold flies a red pennant (BuildingMesh `SiteFlag`), one
  per building. Saved as the building's `"priority": true`, hashed with the building table. Only
  buildings can be put first, not gardens, lots or roads - and FIELDS since 2026-10-08, by a cell
  (`ZONE_OP_FIELD_PRIORITY`, `fields_plan.md`), their pennant on a stake.
- **Not chosen:** assigning people to a site by hand. Builders are whoever is idle; giving one a site of
  his own is state to keep valid through jobs, needs, deaths and sites pulled down, and fights the
  automatic jobs. A builder JOB came instead: the builders' hut (`builders_plan.md`).
- **Roads and walls join the queue** (decided the same day, not built): each drawn line one entry in the
  same placing order, roads no longer only what the idle do with no wood to carry, never put first -
  `line_works_plan.md`, "Queued with everything else", with its step 3.

ROADS in play are sites on this same per-plot path since 2026-10-06 (line_works_plan.md, step 2): labour
only, no wood, so not among the ground sites the carriers serve - an idle person with no wood to carry
fells the trees in a planned road's way and lays it, through the same `ZONE_OP_GROUND_RAISE`. Walls are
next there (step 3), with wood through the carriers as a garden's.

## What it costs, before it is placed (2026-10-11, the user's)

"A small feature that would show us what the cost of placing any of the buildings or plots would be."
Built the same day, view only (`ApplicationChasmGhost.cpp`, THE PRICE TAG):

- **The price tag**: in play, with a tool in hand, beside the cursor - what a click there would cost, the
  goods the site will ask for: "COSTS 8 WOOD", "A STOREY MORE: 8 WOOD" over a building, "COSTS 20 WOOD,
  8 PROVISIONS, 12 WATER" for a tower, "NO GOODS - ONLY WORK" for a road. It is worked out with the ghost,
  for exactly what the ghost shows: a new building's plot at its painted storeys, and an extension's
  corner plot that comes with it (`ZoneJoinCorner`); a garden's, lot's or wall's plot; a field's or
  range's cell; a line being drawn or a bridge dragged, the whole of it, with its plots. Nothing for
  ground that is that already (painting a lot on a lot only claims it), a wall over a road (it only gates
  it), or erasing. Dim where the ghost is red. `chasm_tool` returns it as `cost`.
- **The build bar's tip**: hovering a tool on the grid, beside it, what one plot or cell of it costs. A
  plot's cost goes by its area, so it is a plot of the map's median area - "ABOUT 7 WOOD A PLOT, AS MUCH
  AGAIN A STOREY MORE" - and the tag has the plot's own. A tower's is by its guard places, which come with
  where paths meet ("10 WOOD, 4 PROVISIONS, 6 WATER A GUARD PLACE").
- **One rule for both**: the economy's costs with the kind given rather than read off the plot -
  `EconomyKindStoreyCost` (which `EconomyStoreyCost` now calls) and `EconomyGroundKindCost` (`EconomyGroundCost`'s)
  - so the tag is what the site will ask for. Checked: a house placed where the tag read 8 wood was a site
  wanting 8; a wall line's tag counted the one plot of it that was placed.
- The walk's tip (`DrawWalkTip`, settlements_plan.md) steps aside while a tool is in hand: a right click then
  puts the tool down rather than sending anyone, and the tag has the spot.

## Cancel (the user, 2026-10-16)

"A cancel on the bridge would also be nice, now we have to erase it bit by bit" - and "it'd be nice to have the
brought resources stored in a crate". BUILT the same day (`bridge_plan.md`, "Trees across the water"):
- **Cancel** on a site's card, while nothing of it stands: `ZONE_OP_SITE_CANCEL` (by a plot) or
  `ZONE_OP_FIELD_CANCEL` (by a cell, a field's or range's) - recorded, the whole building taken off at once;
  refused "part of it stands" otherwise, when the eraser is the way. `chasm_paint` op `site_cancel` / `field_cancel`.
- **What it was brought is left where it stood**, as a crate of its own origin, `CRATE_SITE` ("A site's goods") -
  for any site gone, so the eraser taking a site's last plot does the same. `Derive` keeps, before it looks again,
  a dry plot of each site with goods in a settlement's land (a bridge's near end), as it keeps a camp's door.
  Before, a site pulled down lost what was brought. A load on its way to it goes to a store, as before.
- The history counts such a crate as held (`EconomyDayLevels`), as a camp's.
