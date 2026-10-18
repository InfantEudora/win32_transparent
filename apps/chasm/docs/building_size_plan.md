# Size and height

The user's, 2026-10-18: building bigger and taller is under-used, though the grid allows a lot of it. Until now a
building's floor area was all that counted, and wood went by it - so a bigger plot, another plot and another storey
cost the same and gave the same. The user wants **another plot to cost a bit more than a bigger plot, and another
storey more again, with a benefit for each**, so the rules lean the player toward some layouts over others.

What was in play then (the overview the user chose from):

| Kind | Most plots x storeys | Wider gave | Taller gave |
|---|---|---|---|
| House | 4 x 4 | people by area (at most 6), pantry by area, a firewood a plot-storey | the same, and arches over a road |
| Store | 6 x 3 | room by area; more workplaces beside it | room |
| Woodcutter's hut | 4 x 2 | indoor wood room | the loft: 5 wood a log instead of 4 |
| Water collector | 4 x 1 | a bigger buffer; the fill rate fixed | - |
| Builders' hut | 4 x 2 | a builder a plot-storey | the same |
| Ropewalk | 4 x 1 | faster by plot count | - |
| Stonecutter | 4 x 1 | nothing | - |

Leanings found: width, height and plot size cost alike; a house past 6 people wastes its area; houses favoured few
big plots (fire by plot-storey); builders' huts and ropewalks favoured tiny plots; settlement land favoured many
small buildings; crowding cost nothing.

## Decided (the user, 2026-10-18)

From the suggestions, the user chose four, and added the appeal and crowding:

- **A. Footings**: each plot's ground storey costs something extra, so the same area on fewer, bigger plots is
  cheaper.
- **B. Storeys 3 and up take stone.** Stone's first use beyond its own buildings (`stone_plan.md` left "what stone
  buys" for later).
- **D. Workshops take workers by their plots.** Width means more hands.
- **F. Heat rises**: a house's fire by its footprint, its upper storeys burning half. Tall, packed houses are
  cheaper to keep warm.
- **Appeal raises the birth chance**: a **garden** a small increase, smaller with every garden plot added; each
  **storey** a small increase.
- **Crowding costs**: a crowded house lowers its people's **work**, and lowers the **birth chance**.

Not taken: storeys costing more wood as they rise, shared walls cheaper, builders by area and the ropewalk by
length, a tall house holding two families, height seeing farther.

## Worked out here, for the user to overrule

- **Footings are for the kinds drawn as a house body** (`ZoneKindHasBody`): house, store, woodcutter's hut,
  collector, builders' hut, ropewalk, stonecutter's hut - where plots against plot size is the player's choice.
  Not a camp's tents, a bridge, or the one-plot kinds (winch, tower, mine). Fields and ranges already cost by the
  cell whatever its size, which is the same lean.
- **Stone by area**, as wood is: so much a unit of floor area, at least one. Only houses (4) and stores (3) reach
  a third storey. An arch's floor costs what its height would: an arch over a passage's first floor is the second
  storey, so only a 4-storey row's arches take stone.
- **Workers**: a woodcutter's hut, a collector and a stonecutter's hut take a worker per two standing plots,
  rounded up - one for one or two plots, two for three or four. The mine is one plot; the ropewalk keeps one
  ropemaker (its length is its speed); the builders' hut keeps a builder a plot-storey. **A collector fills a unit
  every `ECONOMY_WATER_SECONDS` for each worker at it** - otherwise a second carrier would only wait.
- **Heat in halves**: a plot's ground storey burns a whole firewood, each storey over it and each arch floor half.
  A house's odd half is burnt every other dawn, so it comes out exact over two days. The woodshed and fetching go by
  the day's burn rounded up. A camp is unchanged: a fire a tent.
- **Appeal**: a house's tallest standing storeys - each above the first adds `LIFE_BIRTH_PER_STOREY` - and its
  standing garden plots, the first adding `LIFE_BIRTH_GARDEN`, each after it half the one before.
- **Crowded** is more living in a house than it holds, as the house card and NEEDS ATTENTION already say. Its
  birth chance starts from `LIFE_BIRTH_CROWDED` instead of the full house's; appeal still adds to it.
- **Work while crowded**: everyone living in a crowded house works `LIFE_CROWDED_PACE` slower for each person
  over, down to at most `LIFE_CROWDED_LEAST` - every timed task (felling, building, sowing, making...), a farmer's
  field, picking or drawing by hand, and a collector's fill by its workers. Not walking.

## Current tuning

Figures to tune, not rules (`tuning_numbers` - the user's).

| Constant | Value | Meaning |
|---|---|---|
| `ECONOMY_FOOTING_WOOD` | 2 | wood a plot's ground storey takes over its area's |
| `ECONOMY_HIGH_STOREY` | 3 | the first storey that takes stone |
| `ECONOMY_HIGH_STONE_PER_AREA` | 1.0 | stone such a storey takes a unit of floor area |
| `ECONOMY_PLOTS_PER_WORKER` | 2 | standing plots a woodcutter's hut, collector or stonecutter's hut needs a worker |
| `LIFE_BIRTH_PER_STOREY` | 5 | percent a year, a storey over the first |
| `LIFE_BIRTH_GARDEN` | 8 | percent, the first garden plot; each next half the one before |
| `LIFE_BIRTH_CROWDED` | 10 | percent a year in a crowded house, before appeal |
| `LIFE_CROWDED_PACE` | 0.1 | work lost a person over the house's room |
| `LIFE_CROWDED_LEAST` | 0.5 | the slowest crowding makes anyone work |

So, on an average plot (3.3): a one-storey plot costs 7 + 2 wood; storeys 3 and 4 also 4 stone each. A 2x2 house
burns 3 a snowy dawn instead of 4, a 1x4 2.5 instead of 4. A house of 3 storeys with two garden plots has an appeal
of 10 + 12 = 22: a 72% chance a year with a place to spare, 47% full, 32% crowded.

## Steps

1. **Costs**: a storey's cost by its level (`EconomyKindStoreyCost(..., level)`), footings and stone, everywhere a
   cost is shown or asked - the site, the tool's tip, NEEDS ATTENTION, the top bar.
2. **Workers by plots**, the collector's fill by its workers, co-workers not going for the same load; the card.
3. **Heat in halves.**
4. **Appeal and crowding in the birth chance**; the house card says it.
5. **Crowded work**; the person's and house's cards say it.
6. The wiki pages that name any of it.

## As built (2026-10-18)

All six steps, the same day.

- **Costs**: `EconomyKindStoreyCost(..., level)` / `EconomyStoreyCost(..., level)`, with `...StoreysCost(from, to)` for a
  run of storeys and `EconomyPlotLeftCost` for what is still to build on a plot - storeys over those standing, or an
  arch's floors over those built, an arch's first floor counted as storey 2. `Build` asks for the storey over the
  level reached. The site, the tool's price tag (`ApplicationChasmGhost.cpp`), NEEDS ATTENTION's rope and stone (its
  NEEDS STONE row now names a house or store waiting on stone too) and the top bar all go through them.
- **Workers by plots**: `Derive`'s places for the woodcutter's hut, collector and stonecutter's hut. A collector fills
  a unit every `ECONOMY_WATER_SECONDS / workers` ticks, worked out from the tick alone, so nothing new is saved.
  `FetchFromWorkplace` counts a load for each co-worker already on his way to pick one up there. The workplace card:
  "Takes one worker - 3 plots would take 2", "Takes 2 workers, one for every 2 plots".
- **Heat**: `EconomyState::burn_halves` beside `burn` (the day's, rounded up, for the woodshed and fetching);
  `Heat` burns the odd half on odd days. Derived, not saved.
- **Births**: `EconomyBirthChance` (room, full or crowded, plus `EconomyHouseAppeal`), shared by the dawn's roll, the
  house card ("A child: 75% a year (storeys +10, garden +15)") and `chasm_pick`/`chasm_economy`'s `birth_chance`,
  `appeal`, `crowded_by`. Gardens halve in whole percents, so past the fourth plot a garden adds nothing.
- **Crowded work**: `Economy::Crowding` counts every house at the top of a tick into `crowded_by` (a member, cleared
  by `Reset`, so a load counts afresh) and re-runs `Tending` when it changes; `CrowdPace` multiplies the worker's
  pace in `TickWorker`, a farmer's field pace, picking and drawing by hand, the camp's water draw and a collector's
  share. Picking up and putting down (`ECONOMY_PICK_SECONDS`) stay unpaced, as they are by skill. The person card:
  "Lives in house 26 - crowded: works at 70%"; the house card: "Crowded - they work at 70%, a child less likely".
- **Checked** on `saves/save_20261018_123527`: a 4-storey house site on a 3.45 plot asks 30 wood and 8 stone (4 x 7 +
  2, 2 x 4); house 30's burn 2.0 -> 2.5 -> 3.0 as one plot rose to 2 and 3 storeys, appeal +5 a storey, four garden
  plots +15; house 26 cut to one plot with five living there: crowded by 3, 70%, birth chance 10, and its woodcutter's
  felling 227 -> 325 ticks; a 3-plot woodcutter's hut took two woodcutters. A crowded save loaded three times - once
  after another save - ran 4000 ticks to the same people, stocks and fields each time. The collector's faster fill and
  the co-workers' loads were not seen in play: no collector of three plots stood to test them on.
