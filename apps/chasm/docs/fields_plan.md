# Fields: built, sown, rotated

The user's, from play testing, 2026-10-08. Fields today are painted and grow at once, wheat unless changed,
the same yield every time. They become something to plan: a field is BUILT, SOWN by its farmer with a crop
you choose, and what you grow next decides how well it does. The proposals below agreed as they stand ("fits
the original plan perfectly"). BUILT 2026-10-08 - see the end. Who farms a field changed the same day: a
field belongs to a house's land, farmed by its family (`land_plan.md`).

## Decided (the user, 2026-10-08)

- **A field shows its yield** - 100% for its first crop - and lets you **pick the next crop**, each choice
  with the yield it would get. The next crop **defaults to the one with the highest yield** (since
  2026-10-08 the most FOOD - see "AUTO by food" at the end).
- **What decides the yield: a follows-what table** on the crop just harvested (gameplay_plan.md section 8,
  "Crop rotation on the coarse cell" - the idea it sketched). Repeating a crop wears it down; beans feed the
  soil for whatever follows:

  | Yield of the next crop | wheat | greens | beans |
  |---|---|---|---|
  | first crop | 100% | 100% | 100% |
  | after wheat | 60% | 100% | 100% |
  | after greens | 100% | 70% | 100% |
  | after beans | 120% | 120% | 60% |

  A harvest is the field's area x its crop's food per area (`crop_yield`) x this.
- **A field placed in play is a construction site first**: 2 wood, carried and built like any site, and then
  it is EMPTY.
- **Its first crop is chosen on its card**; until then nothing happens. **The farmer's first job is to sow
  it.**
- **Sowing costs time, no seed**: the farmer walks out and sows the whole field, at his pace (husbandry).

## Proposed, and agreed (the user, 2026-10-08)

- **2 wood per cell** - a one-cell field 2, a field of four 8 - like a garden's wall by its area.
- **Two choices per field, both player commands (recorded zone ops):** the STARTING crop (`ZoneBuilding::crop`,
  none until chosen), used for its first sowing only; and the NEXT crop (`next_crop`) for every sowing after,
  AUTO by default - the highest yield after what was just harvested, a tie to the earlier in the table
  (wheat, greens, beans). A next crop picked STAYS picked until set back to auto; the card shows auto with what
  it would choose. So left alone a field rotates by itself: wheat, greens, wheat, greens... and beans when you
  ask for them.
- **The farmer at home**: sowing first - a field built, empty, its crop decided - then fetching a harvest that
  waits. Sowing takes `ECONOMY_SOW_SECONDS` a cell at his pace, standing at the field's middle vertex (where
  he picks up). Growth then runs as now: while he works it, not in winter, not under the snow.
- **At harvest** the field is EMPTY again with its crop remembered as the last one; its yield for what comes
  next is set when he sows. The growing crop and its yield are the economy's (`EconomyField`); the choices are
  the zones'.
- **A debug paint** stands at once, but is empty too: choose its crop (`chasm_paint field_crop`) and its
  farmer sows it. Saves from before: their fields stand, sown with their crop at 100%.
- **Looks** (`CropMesh`): a site is staked out - stakes at the cell's corners on untilled ground; built and
  empty is the tilled ground that is there already (`FIELD_STAGE_BARE`); sown, the growth stages as now.
- **The card**: a site, as any site (wood, carriers, Build first). Empty and never sown: "Choose a crop to sow"
  with a chip per crop. Sown: the crop, how far grown, and its yield; then "Next:" - AUTO (naming its pick)
  and each crop with its yield. Waiting to be sown: "Waiting for its farmer to sow wheat".

## Build order

1. Zones: a field's cells built or not (`field_built`, like a garden's `ground_built`), a play paint a site and
   a debug one standing; `next_crop` and its op; the starting crop able to be none. Saves, hash.
2. Economy: a field site's wood (2 a cell), built through a raise op on the tick like any building's; the
   field's state - sown, growing crop, yield, last crop; the yield table; sowing as the farmer's errand; the
   harvest by yield.
3. The look: the staked-out site.
4. The card and the tools: yield, the next crop's chips with their yields, the starting crop's chips;
   `chasm_paint field_next`, `chasm_economy` reporting a field's sown, crop, yield, last and next.

## As built (2026-10-08)

All four steps. Files: `Zones.*` (`ZONE_CROP_NONE` / `ZONE_CROP_AUTO`, `ZoneBuilding::next_crop`, `ZoneState::field_built`,
`ZONE_OP_FIELD_NEXT` 15 and `ZONE_OP_FIELD_RAISE` 16, `ZoneBuildingInfo::site_cells`; a field's floor area is its built
cells'), `ChasmSave.cpp`, `Economy.*` (`EconomyField` sown / yield / last, `crop_after`, `EconomyCropYield`,
`EconomyAutoCrop`, `EconomyFieldSowCrop`, `GoSow`, `WORKER_TO_SOW` 19 and `WORKER_SOWING` 20, field sites in
`SiteNeed` / `Build` / `FindSiteWork`, `FieldRaises`), `CropMesh.*` (`FieldLook`, the staked-out site),
`BoundaryMesh.cpp` (no fence round a site cell), the card in `ApplicationChasmSelect.cpp` (`CARD_CHOICE`,
`OVERLAY_HIT_FIELD_CROP` / `_NEXT`), the panel and `chasm_paint` / `chasm_economy` in `ApplicationChasm.cpp` and
`ApplicationChasmEconomy.cpp`, the build bar's BUILDING line in `ApplicationChasmTime.cpp`.

- **A field's place to walk to** is its middle vertex: `Economy::NearestPlot` falls back to `home_plot` for a
  building with no plots, so its farmer, its builders and the hungry all reach it there.
- **A store next to a field takes every food** now, its crop changing as it is rotated.
- **Saves**: a field's `"next"` (when not auto) and `"cells_built"` (only while some are a site); an economy
  field is `[building, crop, grown, sown, yield, last]` - an older three-number one loads sown at 100%.
  Hashed: `field_built` with the zones, `next_crop` in the building word, the field record with the economy.
- **Measured** (seed 12, debug, a copied exe): a one-cell field painted in play - "Field - being built, Wood 0
  of 2" - built by the idle 6 s later (speed 3); empty, "choose a crop to sow", no chip lit; greens chosen and
  sown in 4 s at 100%, its next row "Auto: Wheat | Wheat 100% | Greens 70% | Beans 100%"; harvested 85 s later
  (3 greens), then AUTO sowed wheat at 100%, the next row now "Auto: Greens | Wheat 60% | ...". A site is drawn
  as four corner stakes and a cord on the grass; built and empty, tilled earth inside its fence.

**Not run**: the replay test (seed 12 gets new ones - the user's).

## The frost (the user's, 2026-10-08)

Until now winter only paused a field: a crop caught by it finished in spring, and a farmer sowed again the day
after an autumn harvest, the field waiting through winter already sown. Now (`Economy.h`, THE FROST):

- **Nothing is sown in winter or under the snow.**
- **Winter's first day takes whatever still grows** - the field empty again, its soil as it was (the crop
  before still its `last`, so a frost does not count as a harvest for the rotation), and the loss kept on the
  field (`EconomyField::frost`, saved as an eighth number, hashed) for its card until it is sown again.
- **A farmer sows only what ripens before the frost**, at his pace, in the growing weather the field has left
  (`EconomyGrowingTicksLeft`: to winter, or to the snow where it comes first - the north over autumn's last
  days). **AUTO** takes the highest yield (now the most food, below) of the crops still in time; a crop the player chose that is no longer
  in time waits for spring. So late in the year a field goes to greens, then lies empty.
- **The card says why**: "The frost will take it first - N days short" (red) on a crop that will not make it
  (its farmer slower, or the field fallow a while); "The frost took its beans"; "nothing is sown in winter";
  "under the snow until spring"; "too late in the year for beans to ripen: sown in spring"; and, for a crop that
  cannot ripen there at its farmer's pace even from the thaw (`EconomyGrowingSeasonTicks`), "never ripens here
  before the frost - choose another crop". AUTO's chip names what it would sow now.
- `chasm_economy`'s fields carry `frost`: `growing_days_left`, `pace`, `sows_now`, `took`, and for a crop
  growing `ripens_in_time`.

**Checked** (seed 12, debug, a copied exe, the calendar moved with `chasm_time set_day`): beans sown on spring 1,
the calendar jumped to autumn 10 - "18 days short" - and on winter 1 the field was empty, "The frost took its
beans", nothing sown through winter; in year 2 nothing until the snow left the field (spring 3); greens still
sown on autumn 5 (6 days left, 5.7 needed at pace 1.4) and not on autumn 9. AUTO after greens: wheat on summer
8 (12.98 days left, 12.86 needed), greens from summer 9, nothing from autumn 6.

**The season it leaves**: by the camp on seed 12 the snow leaves on spring 3, 28 growing days a year (30 in the
first, which has no snow). At the first tuning (`crop_days` wheat 18, greens 8, beans 25) beans fitted only when
sown in the first few days after the thaw at an average farmer's pace, and never for husbandry 3 or less - the
user could not get them through at all. **Retuned the same day (the user's): wheat 15, beans 20, greens 8.** At
an average farmer's pace (1.0) in that 28-day season: greens three times; wheat once - twice from husbandry 6
(pace 1.08: 27.8 days for two); beans once, sowable until about eight days after the thaw, and for every farmer
but husbandry 1 (pace 0.68: 29.4 days). Checked on seed 12 (its camp's farmer is husbandry 10, pace 1.4): in
year 2 beans could be sown until summer 5, wheat until summer 9; beans sown on spring 3, the day the snow left,
were harvested on summer 7 (6 beans from one cell), and AUTO sowed wheat after them. (A first run lost its
farmer before the harvest - the test colony, with no collector, died of thirst on day 55 - so the needs were
topped up with `chasm_economy set_needs` for the second.)

## A field site: Build first, and its crop before it stands (the user's, 2026-10-08)

- **Build first** on a field site's card, as on a building's (`construction_plan.md`, "Which site goes first"):
  `ZONE_OP_FIELD_PRIORITY` (op 18, a recorded zone command on any cell of it - a field has no plots for
  `ZONE_OP_SITE_PRIORITY`). `Zones::SettlePriorities` counts a field open while any cell is still staked out,
  and runs after `FIELD_RAISE` and `FIELD_ERASE` too, so the flag drops when the field is built. Its first
  stake flies the red pennant (`CropMesh.cpp`, `FirstSiteCell`). The economy needed nothing: a field site's
  errands were already keyed by its building id. `ZoneOpOnCell` (Zones.h) now says which ops name a cell,
  where two lists had to be kept in step. Also `chasm_paint op:field_priority` (`on`), `chasm_economy
  site_priority` at a field's x,z, and the panel's checkbox.
- **Start with** on the site card: the starting crop chosen while it is still being built, so its farmer sows
  it the day it stands - offered as long as the field was never sown (a sown field growing by more cells keeps
  its next crop). `ZONE_OP_FIELD_CROP` always took a field site; only the card did not offer it.
- **Checked** (seed 12, debug, a copied exe, clicks by `chasm_select op:press`): both chips pressed on a site's
  card - "Build first" on, Beans lit; built, its card said "its farmer is to sow beans, at 100%" and Build
  first had gone. A/B with eight house sites placed first and the field furthest: in the queue nobody
  carried for it in 900 ticks; put first, two carried for it from the first step.

## AUTO by food, and the yields doubled (the user's, 2026-10-08)

With the frost, an average farmer on AUTO needed about 37 cells to feed ten people (`needs_plan.md`, "What
the rates mean"). The main reason was AUTO's tie rule. It never sowed beans: after greens they tied with
wheat at 100%, after wheat with greens, and lost on table order both times. Beans are the most food for
their growing time, and greens about half of it.

- **AUTO sows the most food per growing day** (`CropFood` in `Economy.cpp`): a crop's yield per area x the
  rotation's percentage x what a unit is worth eaten (`EconomyFoodWorth`), over its growing days. That is
  of the crops that still ripen before the frost, as before; a tie still goes to the earlier crop in the
  table. Area and the farmer's pace are the same for every crop, so they do not change the choice. The
  card still shows each crop's yield percentage, so AUTO can name a crop at a lower percentage: wheat at
  60% is still more food per day than greens at 100%.
- **The yields are doubled**: `crop_yield` 1.2 / 0.7 / 0.9 (from 0.6 / 0.35 / 0.45).
- **A store holds 1.5 times as much**: `ECONOMY_STORE_ROOM` 7.5 (about 30 a plot-storey). The user wants a
  decent row of stores at ten people, rather than one shed holding the winter.
- **What it comes to** (arithmetic, not measured): an average farmer's field takes turns - beans one year,
  wheat at 120% and then greens the next. That is about 25 health-days a cell a year, so about 16 cells feed
  ten people.

## AUTO as a starting crop (the user's, 2026-10-09)

A field nobody chose a crop for lay empty for good. Now (`issues_plan.md`): the starting crop can be AUTO too -
the same rule as the next crop's, the most food of the crops that still ripen before the frost - on the card's
"Start with:" row as a chip of its own. A field built and never sown with no starting crop waits one game day
(`ECONOMY_CROP_GRACE`, tuning) for the player, its countdown on its card and in NEEDS ATTENTION, and then
starts with AUTO: the economy names it, the app sets it on the same tick (`ZONE_OP_FIELD_CROP`, not recorded).
