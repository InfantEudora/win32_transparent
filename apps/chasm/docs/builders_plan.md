# Builders and the job overview

Raised by the user's play testing, 2026-10-07: once there are as many workplaces as people, everyone has a
job and nobody builds - the open question in `construction_plan.md` ("Whether a builder should be a job of
its own comes with P5"). Talked through the same day, the proposals below agreed as they stand - "it
gives us the option to always have at least one builder, so the town doesn't stall when there are too
many jobs". BUILT the same day - see the end.

## Decided (the user, 2026-10-07)

- **A builders' hut**, a workplace kind of its own. It employs builders, picked by strength like any job
  (`gameplay_plan.md` already lists builder under strength). More huts, more builders - steered by painting,
  as every job is (`people_plan.md`: "the player steers it by where they paint"). Not a count with buttons,
  not a chip on a person's card.
- **Builders first, then the newest workplace waits.** Places are filled in an order: the builders' huts'
  first, then every other workplace in the order it was placed. When there are fewer people than places,
  the most recently placed workplaces stand empty - the same queue rule construction sites use.
- **An overview of the jobs**, bottom left of the screen: how many people work at what.

## Proposed, and agreed (the user, 2026-10-07)

- **Places**: one builder per plot-storey that stands - a one-plot hut of two storeys employs two. A
  workshop's two storeys at most (`ZoneKindMaxStoreys`, like the woodcutter's), any number of plots.
- **A hut being built already employs one builder**, who builds it. The exception to "a site does nothing
  until it stands", and the reason for it: when everyone has a job, nobody would be free to build the hut
  that frees them.
- **What a builder does** is what the jobless do now: carry wood to a site (nearest, the queue, "build
  first"), then road work. Rounds start and end at his house, as every job's; the hut is where he is
  employed, nothing is fetched from it. His pace is his strength.
- **Nothing to build: he waits at home.** He does not take another job meanwhile - keeping hands free for
  building is what the hut is for.
- **The jobless still build too**, as now. A builders' hut adds builders; it is not the only way to have them.
- **Staffing** (`AssignJobs`): with P free-or-working housed adults, the first P places in the order above
  are staffed. Someone keeps his job while his workplace is among them; a workplace that falls outside -
  a hut has taken its person - frees its worker, who fills a hut's place. Who fills an empty place: the
  best score (skill against the walk from home), as now. So a death costs the NEWEST staffed workplace its
  worker, who moves to the dead person's older place - a walk to a new job, but the rule held.
- **The overview**: a row per job that has places - Builders, Woodcutters, Farmers, Water carriers -
  as working / places (`Farmers 2 / 3`), amber where a place is empty; then the jobless (who carry and
  build). Display only. Bottom left, level with the build bar; lifted above it when the window is too
  narrow for both side by side.
- **The hut**: tool G (B was taken - the previous seed), BUILDERS on the build bar. The woodcutter's workshop body for now, with a stack of
  beams and a ladder against it to tell them apart. Its card: its builders - name, what each is doing - and
  places filled.
- **State**: `ZONE_KIND_BUILDERS` (9, appended - kinds are never renumbered), `WORKER_JOB_BUILDER` (3).
  Saves hold both as numbers; hashed with the zones and the economy as now. Nothing new is recorded: the
  hut is painted like any building.

## Build order

1. The kind: rules (a workshop's), tool B, the build bar, a look, the card's title.
2. The job and the staffing order, with the site's one builder.
3. The overview.
4. The hut's card: its builders.

## As built (2026-10-07)

All four steps. Files: `Zones.*` (`ZONE_KIND_BUILDERS`, "builders", two storeys), `Economy.*` (`WORKER_JOB_BUILDER`,
`EconomyState::places`, `EconomyJobOf`, `EconomyJobCounts`, `AssignJobs`, the own-hut errand in `FindSiteWork`),
`BuildingMesh.cpp` (`BuildBuildersYard`), `ApplicationChasm.*` (tool G, the panel's radio button, the tools'
lists), `ApplicationChasmTime.cpp` (the bar's BUILDERS, `DrawJobsOverview`), `ApplicationChasmSelect.cpp` (the
hut's card, the builder's line on a person's card), `ApplicationChasmEconomy.cpp` (`chasm_economy`'s `jobs`).

- **Places** (`EconomyState::places`, worked out in `Derive`, so a loaded save has them too): one for a workplace
  that stands; a hut's standing plot-storeys, at least one.
- **Staffing** (`AssignJobs`): the huts in id order, then every other workplace in id order; each gets as many of
  its places as the people left reach. A worker keeps his job while his workplace's share has room for him (the
  lower person ids first); otherwise he is free. Freed because the workplace went, his load goes with it (as
  before); freed by the order, he only drops what he was on his way to fetch - a load in his hands he still takes
  to a store. Empty places are filled by the best score, as before.
- **The builder** is the jobless at home and on a road stint. In `FindSiteWork` errands to his own hut go before
  even a site put first, so the hut's first builder builds it.
- **The look**: plank walls under red tile (`kind_looks`), and on its lowest standing plot, toward its first open
  neighbour, three beams stacked by the wall and a ladder leant up to the eaves.
- **The overview**: bottom left, a row per job with places or people - BUILDERS, WOODCUTTERS, FARMERS, WATER
  CARRIERS as working / places, amber where one is short - then NO JOB. It blocks clicks like the other panels.
- **Measured** (seed 12, debug, a copied exe): ten fields and a collector, 11 places for 10 people - nine farmers
  and the carrier, the newest field empty, nobody jobless. A hut painted in play, a site: its one place came from
  the newest staffed field (8 / 10 farmers), and that builder fetched the camp's wood and stood its first storey
  15 s later at speed 3. A second storey (2 places) took the next newest field's farmer: BUILDERS 2 / 2, FARMERS
  7 / 10 in amber.

**Not done**: the replay test (seed 12 needs new ones - the user's). A hut's builders start from home like
everyone; the hut itself is only where they are employed - giving it a stock of wood of its own is a later idea.
