# Construction (play mode step 5)

Step 5 of `play_mode_plan.md`: in play mode a building is not placed but BUILT. Agreed with the user
2026-10-06: **wood is the only thing a building takes, and idle people carry it over.** BUILT the same day.

## Decided

- **Wood only.** A storey takes `ECONOMY_BUILD_WOOD_PER_AREA` (2) wood per unit of its floor area, about
  8 for a plot - a tent half that, a winch half again more. Fields and roads cost nothing and are still
  painted at once, in either mode; gardens and lots cost a little since 2026-10-06 (see the end).
- **Idle people carry and build.** Anyone without a job: from his home to the nearest wood, a load
  (`ECONOMY_LOAD`, 5) to the site, a while there building with it (`ECONOMY_BUILD_SECONDS`, 5, at his
  pace - strength), and home. There is no builder job and no builders' hut yet.
- **The settlers bring supplies.** A new colony's camp holds 40 wood, 20 wheat, 10 beans and 20 water,
  so the first few buildings go up before there is a woodcutter. The overlay's totals count them.
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
  to the site: wood is a standing store's, the camp's supplies or a woodcutter's pile. His round is
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
- No priorities: every site is served nearest-first. A player may want to say which goes first.
- No refund for a site pulled down.
- Jobs come first: someone given a job stops carrying when his errand is done, so a colony with as many
  workplaces as people builds nothing. Whether a builder should be a job of its own comes with P5.

## Gardens and lots (2026-10-06, the user's)

A garden or a LOT (the town tool, renamed: a fenced yard) painted in PLAY is a site too, a plot at a time:
`ZoneState::ground_built[v]` 0 until built, and until then it has no wall and encloses nothing
(`ZoneGroundStands`, which `ZoneBoundaryBetween` - the walls, the walkers, the straight walks - asks).
It takes a little wood, `ECONOMY_GROUND_WOOD_PER_AREA` (0.5) of its area - about 2 a plot - carried by the
same idle people. A debug paint, and a road or field in either mode, is there at once, as before.

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

Open: roads in play as a labour-only site (trees felled first) and walls are win32-transparent-cc's
(line_works_plan.md), on this same per-plot path.
