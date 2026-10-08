# Needs and winter (P5)

Step 5 of `gameplay_plan.md`'s first prototype, after people (P4, `people_plan.md`) and construction
(`construction_plan.md`): people eat, drink and keep warm, and die when they cannot. Talked through with
the user 2026-10-07; step 1 (health, water, death) BUILT the same day - see the end.

**The numbers are tuning, not rules** (the user, 2026-10-08). What was decided is the MECHANICS: health as
the food meter, thirst as a bar of its own, foods worth different amounts, eating only what fits, dry and
cold as faster drains, starving. Every figure - how long a bar lasts, what a unit is worth, how fast it
drains, when a need is pressing, what the settlers bring - is the current tuning. It will change with
playtesting and as features arrive, and a difficulty setting may later pick a different set of starting
values and drains. The code is where the numbers live (`NEEDS_*` and `ECONOMY_START_*` in `Economy.h`,
`EconomyFoodWorth` in `Economy.cpp`); "Current tuning" below lists them and is the one place here kept up to
date. A figure anywhere else in this plan is the tuning of the day it was written, to read its reasoning by.

## Decided (the user, 2026-10-07)

- **Health is the food meter.** A person has HEALTH, which falls steadily and is raised again by eating;
  there is no hunger bar of its own. A unit of food is a whole number, so what a food is WORTH is how much
  health it gives - **beans the most, wheat between, greens the least**. (First tuned as a 5-day bar falling
  20% a day, wheat a day, greens half a day, beans two.)
- **Thirst is a bar of its own**, filled by water a unit at a time; once it is empty, health falls faster.
  (First tuned as a 3-day bar, a unit filling a day of it.)
- **Cold works the same way**: in winter, a person whose home has no firewood loses health faster.
- **They starve.** Health at zero is death. Nobody leaves and nobody arrives - there are no immigrants -
  so a colony that cannot feed itself dies out, and that is the first way to lose. (The prototype's table
  said "a shortage makes them leave"; changed.)
- **The target for the fields: one field cell feeds one person for a year.**
- **Tuned once it runs - and again and again.** Real people last weeks without food; early gathering
  (berries, say) may come later to soften the start; a difficulty setting may tune it all differently.

## The model

In ticks (a day is `CALENDAR_DAY_TICKS`) and whole numbers, saved and hashed as a state part of their own,
per person: a HEALTH bar and a WATER bar, each one tick lower every tick.

- **Eating when there is room.** A food is eaten only when the whole of it fits - health at most full less
  its worth - so nothing is wasted. A person eats the drain back: a day of health a day, whatever the bar's
  size, so the bar decides how long he lasts without food, not how much he eats. Of the foods that fit, the
  one that keeps worst goes first: greens, then wheat, then beans. Water the same way.
- **Dry** (the water bar empty): health falls faster and **food no longer raises it**, so eating cannot
  outrun the thirst - as first put, the bar empty and then health falling faster, it could, and death came
  well after the water ran out. A dry person eats nothing.
- **Cold** (built 2026-10-08, step 3): his home under the snow at dawn (`UnderSnow` - the south stays bare,
  so it is never cold there) and its firewood not burned that dawn. Health falls faster and **food still
  counts**, so a cold family eats more - twice as much with the drain as tuned: a wood shortage shows up as a
  food shortage first, and kills only once the food runs out too. Cold and dry add up. Someone with no home
  at all is cold under the snow where he waits, at the camp's spot.

## Current tuning

The values in the code on 2026-10-08, with the first ones for comparison. Change the code and this table
together; a difficulty setting would make this one column of several.

| What | In the code | Now | First (2026-10-07) |
|---|---|---|---|
| Full health: how long without food | `NEEDS_HEALTH_FULL` | 7 days | 5 days |
| Health drain | `Needs` | 1 a tick (a day a day) | the same |
| ...more while dry | `NEEDS_DRY_DRAIN` | +2 a tick (3x) | the same |
| ...more while cold | `NEEDS_COLD_DRAIN` | +1 a tick (2x) | - (step 3) |
| Firewood a home burns at dawn under the snow | `ECONOMY_FIREWOOD_BURN` | 1 a plot-storey (a camp: a plot) | - (step 3) |
| A house's woodshed | `ECONOMY_WOODSHED_DAYS` | 4 days of its burning, apart from its room | - (step 3) |
| Fair share's margin, food and firewood | `ECONOMY_FAIR_DAYS` | 1 day | - |
| A unit of food | `EconomyFoodWorth` | wheat 1 day, greens 1/2, beans 2 | the same |
| Full water bar | `NEEDS_WATER_FULL` | 4 days | 3 days |
| A unit of water | `NEEDS_WATER_WORTH` | 2 days | 1 day |
| Pressing: water below | `NEEDS_PRESSING_WATER` | a third of the bar (1.33 days) | a third (1 day) |
| Pressing: health below | `NEEDS_PRESSING_HEALTH` | a quarter of the bar (1.75 days) | a third (1.67 days) |
| A house fetches below | `ECONOMY_HOME_DAYS` | 2 days for its family | - (step 2) |
| A house's room | `ECONOMY_HOUSE_ROOM` | 4 a unit of floor area | 1.5 |
| The settlers bring | `ECONOMY_START_*` | 40 wood, 40 wheat, 30 beans, 40 water | 40, 20, 10, 20 |

So per person a day: 1 wheat (or 2 greens, or half a bean) - unchanged since the first tuning - and now
half a unit of water, where it was one.
- **Death** at zero health: the person is gone, his job is given out again, his family stays. Nothing
  is left behind yet. With everyone gone, the overlay says the colony is lost.

## Agreed (the user, 2026-10-07, on the proposals)

- **Eating and drinking happen at home** - from the house's own stock (`ECONOMY_HOUSE_KEEPS` gains water)
  or, for a family in the camp, the camp's supplies. ~~And only there~~: CHANGED by the user the same day -
  **a pressing need is met wherever there is food or water.** Below a pressing level of his water or his
  health (a third of each, as first tuned), a person takes on no new work and goes to the nearest store, camp or collector that has it, and eats and
  drinks there ("this is what would happen in reality - you wouldn't necessarily drink at home"). Home
  stays where he normally eats; fetching (step 2) is still what keeps him from those trips.
- **Fetching home** - the gameplay plan's errand, the walk to the shops that makes where a house stands
  matter. When a home holds less than a few days (`ECONOMY_HOME_DAYS`) of what its family eats, drinks or
  (in autumn and winter) burns, a member fetches a load (`ECONOMY_LOAD`) from the nearest store that has it: an idle member
  first, otherwise its worker between rounds. It comes before carrying wood to a site.
- **Firewood**: a home under the snow burns wood by its floor area at each dawn (1 a plot-storey, as a first
  number), from its own stock; when it cannot, its family is cold that day. A camp burns by its floor area
  too - seed 1's 14 plots would burn 14 a day against about 4 for ten people in houses - which is the reason
  to have built the houses by winter.
- **Firewood, fetched** (the user, 2026-10-08): only from AUTUMN on, from storage, the way food is fetched -
  shared out evenly across the houses with a margin: a house is held to its FAIR SHARE, the colony's
  firewood AT HOME (as food's, not counting the stores) spread over every house's burning, and a day more.
  No wood is kept back for building sites: by autumn the building should be done. A home burns only while
  someone lives in it - a camp too, which works as a house does. A house keeps its firewood APART from its
  food and water, in a woodshed of its own.
- **Seen from outside** (the user's): a warm home smokes - a grey-white column from its chimney, or from a
  camp's fires - and a house's windows glow orange. EVERY HOUSE HAS A CHIMNEY, and what varies is its kind:
  through the roof, or a stack up the side of the house.

## What the rates mean (rough sums from the current tuning - redo them when it changes)

Arithmetic from the "Current tuning" table, not measurements. Each line says what it rests on, so it can
be redone for another tuning or difficulty.

- **Fields** (redone 2026-10-08 for the frost, `crop_days` 15 / 8 / 20, the yields doubled to 1.2 / 0.7 /
  0.9 and AUTO by food). A crop must ripen between the thaw and winter: 28 growing days by seed 12's camp
  (snow until spring 3), 30 where no snow comes or in the first year. A coarse cell is 14 in area on
  average (seed 12, 336 sampled: a tenth under 9, a tenth over 18.6). A person eats 40 health-days a year.
  A cell's harvest at 100%, in health-days:

  | Crop | Days to grow | A cell's harvest | Health-days | Per growing day |
  |---|---|---|---|---|
  | Wheat | 15 | 16.8 units | 16.8 | 1.12 |
  | Greens | 8 | 9.8 units | 4.9 | 0.61 |
  | Beans | 20 | 12.6 units | 25.2 | 1.26 |

  AUTO sows the most food per growing day of the crops that still ripen in time (the last column, times
  the rotation's percentage). At an average farmer's pace (1.0) a field takes turns: beans one year;
  wheat at 120% and then greens the next. That comes to about 25 health-days a cell a year, so **ten
  people need about 16 cells** - three farmers, since a share is whole fields of 6 cells or more
  (`ECONOMY_FARMER_CELLS`). A husbandry-10 farmer fits beans and then wheat every year: about 45 a cell,
  9 cells. A husbandry-2 farmer still fits beans (26 of the 28 days): about 23 a cell, 18 cells.
  Husbandry 1 is too slow for beans. AUTO then sows wheat every year, because wheat at 60% is still more
  food per day than greens: about 10 a cell, 40 cells. Before (AUTO by yield percentage, the yields half
  these) it was 37 cells for an average farmer: AUTO never sowed beans, since they tied with wheat or
  greens at 100% and lost on table order. One large field needs only one farmer, but sowing takes 3 s a
  cell, twice a season, so keep fields to a share's size or so.
- **The first spring.** The camp's 40 wheat and 30 beans are 100 days of food, ten for ten people, and full
  health is seven more: with water but nothing harvested, the first death from hunger about day 17 - room
  for a first greens harvest (8 days' growing) and most of a wheat one. At the first tuning (20 wheat, 10
  beans, a 5-day bar) it was about day 9, a knife-edge. Its 40 water is 80 days, eight for ten; with the four
  in the bar and the dry spell after, deaths from thirst about day 14 without a collector (day 7 at the
  first tuning) - so with nothing built, thirst still comes first. A collector makes about 11 a day, enough
  now for about twenty.
- **Winter**, for ten people. With the frost, the store has to last until the first harvest of the next
  year, not only through winter. On AUTO at pace 1.0 the last harvest comes on autumn 3 (beans) or autumn 6
  (greens), and the next year's first on summer 8 (wheat) or autumn 3 (beans): 35 to 37 days. That is
  about 300 health-days of food in store by autumn, after the 7 days each person carries in their health
  bar. As beans that is about 150 units; as wheat and greens about 360, 12 plot-storeys of store at
  `ECONOMY_STORE_ROOM` 7.5 (1.5 times the first 5, the user's, 2026-10-08, for a decent row of stores at
  ten people). Add 50 water where the collectors are under the snow (100 at the first tuning) and the
  firewood. In the first year the settlers' 100 days cover only the first 10. Its first sowing should be
  greens, because wheat sown on day 3 ripens on day 18, after the first hunger deaths (about day 17,
  below); AUTO then follows with wheat.
- **A house's room was too small** at the first tuning: about 6 a plot-storey, when two days of food and
  water for a family of three was already 12. It went to about 16 with step 2, which could not work without
  it (see "As built: step 2").

## Not in this step

- **Spoilage** (`economy_plan.md` put it with eating): the foods already differ in worth and growing time.
  Greens rotting when winter comes is the first piece of it, once eating runs.
- **Crowding** (`people_plan.md`): nothing fills a house past its room until there are births.
- **Working slower when hungry**, a warning before death: not asked for; a candidate when tuning.
- **Early gathering** (the user): food from the wild for the first days.

## Build order

1. Health and water per person: falling, eating and drinking at home from the house's or the camp's
   stock, dying. Saved, hashed, on the person's card and in `chasm_economy`.
2. Fetching home from the stores. BUILT 2026-10-07, below.
3. Cold: firewood burned under the snow, and the cold drain. BUILT 2026-10-08, below.
4. A first tuning pass: the yields and a house's room (the room already, with step 2; the bars, water and
   the settlers' supplies were retuned in 2b40394). Measured: a new colony with nothing built (when the
   first dies), the same with a collector and a greens field (it lives), and a first winter with and
   without firewood. Not the last pass - see "The numbers are tuning, not rules".
5. The overlay: a death says so, as a refusal does; a lost colony says so.

After each step, `tools/chasm_replay_test.py` must still pass. (Retired 2026-10-08 - see `README.md`; a new
replay comes after the mapping work.)

## As built: step 1, health, water and death (2026-10-07)

(The as-built sections record what was built and measured on their day, with that day's tuning; for the
values now, see "Current tuning".)

Files: `Economy.*` (`Needs`, `EatAndDrink`, `EconomyFoodWorth`, `EconomyDeath`), `ChasmSave.cpp`, the hash and
`chasm_economy` in `ApplicationChasmEconomy.cpp`, the person's card in `ApplicationChasmSelect.cpp`.

- **Per person** `health` and `water`, ticks of life left (`NEEDS_HEALTH_FULL` 11,250, `NEEDS_WATER_FULL`
  6,750), after his round each tick: water down one, health down one - three while dry. Saved per person
  (`"health"`, `"water"`; a save from before starts everyone full) and hashed with the economy.
- **At home** - `WORKER_AT_HOME` and standing there - from `stock[house]`, a house's or the camp's: water
  while a whole unit fits, then, unless dry, greens, wheat, beans, each while a whole one fits.
  `ECONOMY_HOUSE_KEEPS` gained water. Campers eat in worker order, so when the camp runs short the lowest ids
  eat first.
- **Death**: struck from `workers` with whatever he carried, written into `EconomyState::dead` (id, family,
  sex, age, tick, hunger or thirst - saved as `"dead"`, hashed), then `HouseFamilies` and `AssignJobs` on the
  same tick, so an emptied house is taken and a job given out again.
- **Seen**: the card has Health and Water bars, amber with two days of health or one of water left, and
  "Water 0% - thirsty, losing health fast" when dry; the house card lists its larder; `chasm_economy` gives
  each person's `health_days`, `water_days` and `dry`, and the dead with the colony's day they died on.
- **Measured** (seed 1, debug):
  - A new colony, nothing built: the camp's wheat lasts two days and its water two, its beans go on day
    4, everyone is dry on day 5 and all ten die of thirst at day 6.33 - the plan's sums exactly.
  - No food and no water at all - a family in a house, whose larder nothing fills yet - dies at day 3.67:
    dry at day 3 with two days of health left, which go at three times the pace. A family of four in a
    two-storey house by the camp died then, and on that tick a camp family moved into the house.
  - A save and load in the middle of a run gives back every person's health and water to the tick.
  - The replay test PASSES.

**The thirst mark** (the user's, 2026-10-07; `DrawSelectionOverlay` in `ApplicationChasmSelect.cpp`): in play
mode, everyone down to a day of water - where the card's bar goes amber - has a drop on a stem over his head,
drawn like a selection pin; ringed red once he is dry. A pinned person's sits beside his pin; a family indoors
gets one over its roof. Only on screen - unlike a pin it is not kept at the edge, or a thirsty camp would
bury the pins. Seen on seed 12 with nothing built: the marks come up on day 5, the rings on day 6.

**The badge** (the user's, same day; `DrawPersonBadge` in `ApplicationChasmSelect.cpp`) replaced the thirst
mark for anyone out of doors: one badge over everyone, always - his job's icon in a ring that is his health,
full at full and running back to twelve o'clock (core's new `UIOverlay::AddArc`). The thirst mark lives on in
it: below the same third of water the drop takes the job icon's place. The ring is coloured as the card's
health bar - amber with two days left - and red while he is dry. The selection's pins merged into it too
(`selection_plan.md`). A family indoors still gets the drop alone over its roof, ringed red if one is dry.
Seen (seed 12, debug, 3x speed with no collector): drops on everyone at 4.3 days of health, the ring two
thirds white; then all dry at 2.8 days, the ring red and under half. Not seen: a roof's drop (no family was
indoors and thirsty in the run), and amber, which dry overtakes in a camp without water.

**Open:**
- Nothing yet says a death happened beyond the card and the tool - that is step 5's overlay.

## As built: pressing needs (2026-10-07)

The user's change to "only at home", above. Files: `Economy.*` (`EconomyPressing`, `SeekSupply`, `Consume`,
`WORKER_TO_SUPPLY` / `WORKER_SUPPLYING`), the card's line in `ApplicationChasmSelect.cpp`.

- **Pressing**: water below `NEEDS_PRESSING_WATER` (a third: one day), or health below `NEEDS_PRESSING_HEALTH`
  (a third: 1.67 days) and not dry - food does nothing for the dry, and would only send him from store to store.
  The thirst mark now uses the same threshold, so a drop over someone means he is going for water.
- **Where it is checked** - wherever he would take on new work: at home before a job or an errand, at the end
  of every task (`GoHome`), between pieces of road work (the stint ends), and holding a load nobody takes
  (he takes it along, then tries to deliver it again). What he is in the middle of, he finishes.
- **Where he goes** (`SeekSupply`): what his home has he gets at home; the rest, at the nearest building with
  it - a store, the camp's supplies, a collector's water, a field's harvest, never another family's house -
  by the straight line, the first of the nearest four he can walk to. **Nowhere: he carries on as before**,
  since stopping would save nobody and the work may be the collector that would.
- **Claims**: a supply is only gone to while some of it is not already counted on - one unit for each person
  walking there or eating there. Counting each one's full drink instead kept three thirsty people at home
  while the collector refilled under them, and they died.
- **There**: a while eating (`ECONOMY_MEAL_SECONDS`), then he eats and drinks his fill, as at home
  (`Consume`, shared with home). At a **collector that is filling** he waits for the next unit, a few seconds
  off, until he is full (unless hunger is also pressing) - walking home with nothing and back again kept two
  of a camp dry for a day.
- **Fixed on the way** (an old bug, `TO_HOME` in `TickWorker`): a person walking home when his family was
  given a house arrived at the OLD home and stayed there "at home, indoors" in the new one - out of sight,
  never looking for work or water, until the zones next changed. He now walks on to the new home. Painting a
  house in a new colony's first ticks did it every time.
- **Measured** (seed 12, debug, speed 3; a 3-storey house 16 from the camp, the Veens in it and one of them the
  collector's carrier; a collector on the river 42 east of the camp; no store, so its water stays there):
  - Day 2.2: the Veens, their house empty, go to the camp for water. Day 4: the camp's water gone, all ten go
    to the collector, a 0.6-day walk from the camp; the last to arrive wait there for its refill.
  - Nobody dies of thirst: the lowest water anyone reaches is 0.06 days, waiting at the collector. All ten
    die of hunger on days 8.3-9.0 - no fields - which is the plan's "first death about day 9".
  - Before the waiting and the one-unit claims, the same colony lost one or three to thirst.
- **The replay test** on seed 12 (Grid.h's default since 2026-10-07): both replays end in the original's
  zones and their traces match; its layout checks fail - the oaks and the woodcutter's spot are seed 1's.
  Seed 12 gets new tests (the user's).

**Open:**
- Every trip is a long walk where the water is far: from this camp more than a day's round trip. That is
  the layout's price, as intended - a store by the camp, filled by the carrier, is the cure.
- ~~Housed families still eat only from the camp and the stores until fetching home (step 2).~~ Built, below.

**Fixed 2026-10-08 - stranded short of home** (the user's woutveen recording: Wout Veen idle in the road,
dead of thirst). Someone a re-plan stopped short of home - "at home" where he stood, the way there cut -
only waited for the zones to change, and never looked to his needs: when the player stopped building, he
stood there until he died. Now, every look, he goes for a pressing need wherever he can reach it (his home's
larder no help out there: `SeekSupply`'s `f_home`), and every fourth look tries the way home again. What cut
Wout's way was the picker missing the point he stood on (`grid_plan.md`, Step 2, "Watertight"); with that
fixed the recording plays through with nobody dead. The stranded case itself was not seen again - nothing in
the recording cuts anyone off now - so it is checked by reading only.

## The People panel (2026-10-07, the user's)

The debug panel's **People** section (`RenderPeoplePanel`, `ApplicationChasmEconomy.cpp`), in place of the retired
walker's: every villager - id and name, age, job, what he is doing - with his health and water as sliders in
days; the name amber while a need is pressing, red while he is dry. A slider sends one recorded
`CHASM_CMD_NEEDS` (person, health and water in ticks, -1 for unchanged) when it is let go, not one a frame
while dragged; ctrl-click types a value. Buttons above set everyone at once: all full, all thirsty (half
of the pressing level), all dry, all hungry. The dead are listed under. The tool's form is
`chasm_economy op:set_needs` (person, health_days, water_days).

An edit lands before the tick's eating, so someone at home with food and water tops straight back up -
health 0 kills only someone who cannot eat on that tick.

## As built: step 2, fetching home (2026-10-07)

Files: `Economy.*` (`HomeShort`, `FetchHome`, `TakeHome`, `RouteHome`; states `WORKER_TO_GET`, `WORKER_GETTING`,
`WORKER_TO_LARDER`, appended), the card's lines and a store's related people in `ApplicationChasmSelect.cpp`.

- **When**: a HOUSE holding less than `ECONOMY_HOME_DAYS` (2) of what its family drinks or eats, counted in days
  of their bars (water at `NEEDS_WATER_WORTH` a unit, food at its worth). The scarcer first, water on a tie.
  Only with room at home for a trip's worth - half a load, or half the room if less - or a house too small
  for both was fetched for a unit at a time.
- **From where** (the user's choice): the nearest STORE or CAMP with some to spare - the camp's supplies are the
  colony's. Never a collector or a field: what they make goes to the stores by their workers. Spare is what is
  there less a load for each other fetcher on his way to it. Of food, the kind the place has most of.
- **Who**: an idle member first; one with a job only between rounds, and only with no jobless member home to
  go instead. One errand a house at a time. Checked at home after a pressing need, before wood for a site and
  before a worker's own round.
- **How much**: a load (`ECONOMY_LOAD`, 5), no more than the room left, and no more than takes the house a day
  past the mark (3 days). Without that last cap water filled a house's room and left none for food - nine
  water and nothing to eat for a day, the family of four going hungry beside it.
- **Into the house** on arrival, as far as it keeps the good and has room; what does not fit goes to a store.
  A family moved while he walked: on to the new home with it.
- **A pressing trip brings a load home**: someone who ate or drank at a store or the camp, his house short of
  what is there, carries a load home rather than going back for it.
- **A camp's own families do not fetch** (the user's choice): its supplies are what the settlers brought.
- **The house's room** went from 1.5 to 4 of floor area (about 16 a plot-storey), this plan's own figure:
  two days of water and food for four is 12, and the old room was 11 for a two-storey one-plot house.
- **Measured** (seed 12, debug, a two-storey house of four 11 from the camp):
  - Stocked from the camp: water, then wheat, one errand at a time, by the jobless members while the farmers
    stayed home. Over nine days the family kept 5-7 days of health and 2-4 of water, never pressing.
  - One of them set thirsty (0.5 days of water) with the house empty: he drank at the camp and carried 4 water
    home with him.
  - A store beside a water collector, the house 5 from it and 23 from the camp: the first water came from the
    camp (the store was still empty), every load after from the store; food from the camp, the only place
    with any.
- **The replay test**: both replays end in the original's zones and their traces match. It still prints FAIL,
  on two checks that fail the same with this step switched off (a build with `HomeShort` returning nothing and
  the old room, run the same way): nobody employed at the end ("working 0") and the road's oaks, which are
  seed 1's.

**Open:**
- ~~Firewood (step 3): `HomeShort` counts water and food; wood joins it with the burning.~~ Done in step 3.
- The cap counts what is in the house, not what is on its way: a pressing trip's load and a fetcher's can both
  arrive (nine water once, room allowing).

## As built: step 3, firewood and cold (2026-10-08)

- **The fire** (`Economy::Heat`): at each dawn every inhabited home - a house, or a camp - whose first plot is
  under the snow burns `burn` wood from its own stock (its standing plot-storeys; a camp, its plots, tents
  and fires alike) and is WARM until the next dawn; with less than that it burns none and is COLD. Any other
  home is neither. `heat`, by building (slot 0 for anyone with no home, cold under the snow at the camp's
  spot), is saved (`"heat"` in a save's economy - absent in older saves, which start with no fire until the
  next dawn) and hashed with the economy.
- **The cold drain**: a cold person's health falls `NEEDS_COLD_DRAIN` a tick faster. A death while cold (and
  not dry) is recorded as of the cold (`NEEDS_DIED_COLD`).
- **The woodshed**: a house's firewood has room of its own, `ECONOMY_WOODSHED_DAYS` of its burning; food and
  water share the rest of its room (`HouseRoom`), so firewood never crowds out a meal.
- **Fetching it** (`HomeShort`, `TakeHome`, `FetchHome`): a house the full winter's snow reaches (`snowed`,
  from the front at its furthest) fetches firewood from autumn's first day through winter, and in spring
  while the snow still lies on it - below `ECONOMY_HOME_DAYS` of its burning, up to a day more, never past
  its FAIR SHARE (`FirewoodFair`: the firewood in every such house over their burning, its own burning's part
  of that, and `ECONOMY_FAIR_DAYS` more). Wood comes from stores, and from the camp once nobody lives there
  - an inhabited camp keeps its wood for its own fires, its families being unable to fetch more. The
  scarcest kind is fetched first, in days: water, food and firewood each its own errand.
- **The chimneys** (`BuildingMesh.cpp`, `HouseChimney`): every house has exactly one, on its lowest standing
  plot - through the roof as before, or for half of them by the house's hash a stone STACK up the middle of
  an outside wall, a wide base to the eaves and a capped flue past the ridge, in place of that wall's window.
  Other kinds keep their chimneys by chance. Every house changes look with this (its chimney moved or added).
- **Smoke and glow** (`ApplicationChasmHearth.cpp`): a warm house's chimney top, and each fire of a warm
  camp, gets a column of five smoke puffs on core's LIFE motion (the palette's new `PAL_SMOKE`, a light
  grey); a warm house's window panes are overlaid by unlit orange ones (`PAL_WINDOW_LIT`). Where they go is
  where the house's mesh put them: `BuildHouseCell` reports its chimneys and panes when asked. Rebuilt when a
  home's heat or the zones change.
- **Measured** (seed 12, debug, 2026-10-08): three two-plot houses by the camp (families of 4, 3 and 3, each
  burning 2 a dawn), the empty camp's 40 wood the only firewood, from autumn's sixth day at 10x:
  - Each house fetched 4 (two days' burning, its fair share then) on that first day, and held it.
  - The snow reached them on winter's third dawn (day 33): every house warm from then, burning 2 a dawn and
    topping up from the camp, whose wood was gone by winter's seventh day. Nobody cold.
  - Two died that day - of THIRST: the camp's water ran out, with no collector in the test.
  - The same run before a fix (a wood errand's pickup took food - `WORKER_GETTING` knew only water and food):
    no house fetched wood, all three were cold from day 33, and the least health fell from 5.7 days to 0.5
    in three - the cold drain at work.
  - Smoke rose from the three chimneys (15 puffs) and 25 window panes glowed. A camp's smoke was not seen in
    a test: no camp was lived in under the snow.
- **The side stack, fixed** (the user's, same day): first built as slabs against the wall, the flue and its
  cap showed their open backs above the eaves, where the roof slopes away, and the cap's top fought the
  flue's; they are closed blocks now, the cap's top above the flue's.
