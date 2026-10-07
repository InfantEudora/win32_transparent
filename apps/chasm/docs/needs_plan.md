# Needs and winter (P5)

Step 5 of `gameplay_plan.md`'s first prototype, after people (P4, `people_plan.md`) and construction
(`construction_plan.md`): people eat, drink and keep warm, and die when they cannot. Talked through with
the user 2026-10-07; step 1 (health, water, death) BUILT the same day - see the end.

## Decided (the user, 2026-10-07)

- **Health is the food meter.** A person has HEALTH, which falls 20% a day - five days from full to
  nothing - and eating raises it again. A unit of food is a whole number, so what a food is WORTH is how
  much health it gives: **wheat 20% (a day), greens 10% (half a day), beans 40% (two days)**. There is no
  hunger bar of its own.
- **Thirst is a bar of its own**, three days from full to empty; a unit of water fills a day of it. Once
  it is empty, health falls faster.
- **Cold works the same way**: in winter, a person whose home has no firewood loses health faster.
- **They starve.** Health at zero is death. Nobody leaves and nobody arrives - there are no immigrants -
  so a colony that cannot feed itself dies out, and that is the first way to lose. (The prototype's table
  said "a shortage makes them leave"; changed.)
- **The target for the fields: one field cell feeds one person for a year.**
- **First numbers, tuned once it runs.** Real people last weeks without food; early gathering (berries,
  say) may come later to soften the start.

## The model

In ticks (a day is `CALENDAR_DAY_TICKS`, 2,250) and whole numbers, saved and hashed as a state part of
their own, per person:

| | Full | Falls | Refilled by |
|---|---|---|---|
| Health | 5 days (11,250) | 1 a tick, +1 cold, +2 dry | wheat +2,250, greens +1,125, beans +4,500 |
| Water | 3 days (6,750) | 1 a tick | a water +2,250 |

- **Eating when there is room.** A food is eaten only when the whole of it fits - health at most full less
  its worth - so nothing is wasted: a bean every two days, a wheat a day, greens twice a day. Of the foods
  that fit, the one that keeps worst goes first: greens, then wheat, then beans. Water the same way.
- **Dry** (the water bar empty): health falls three times as fast (60% a day) and **food no longer raises
  it**. Without water a person lasts about four and a half days from full, whatever he eats. As first put
  - the bar empty at three days, then health falling faster - eating could have outrun the thirst, and
  death would have come well after the three days.
- **Cold** (the user agreed to the drain; the details are proposed): his home's plot under the snow
  (`CalendarSnowAt` - the south stays bare, so it is not cold there) and no firewood burned at home today.
  Health falls twice as fast (40% a day) and **food still counts**, so a cold family eats twice as much: a
  wood shortage shows up as a food shortage first, and kills only once the food runs out too. Cold and
  dry add up.
- **Death** at zero health: the person is gone, his job is given out again, his family stays. Nothing
  is left behind yet. With everyone gone, the overlay says the colony is lost.

## Agreed (the user, 2026-10-07, on the proposals)

- **Eating and drinking happen at home, and only there** - which is what forces the fetching. From the
  house's own stock (`ECONOMY_HOUSE_KEEPS` gains water) or, for a family in the camp, the camp's supplies.
  Going home early when low turned out not to be needed: every round starts and ends at home (P4) and
  none runs past about a day - a farmer waits for his harvest at home, not at the field.
- **Fetching home** - the gameplay plan's errand, the walk to the shops that makes where a house stands
  matter. When a home holds less than two days of what its family eats, drinks or (in autumn and winter)
  burns, a member fetches a load (`ECONOMY_LOAD`, 5) from the nearest store that has it: an idle member
  first, otherwise its worker between rounds. It comes before carrying wood to a site.
- **Firewood**: a home under the snow burns 1 wood per plot-storey of floor area at each dawn, from its
  own stock; when it cannot, its family is cold that day. A camp burns by its floor area too - seed 1's
  14 plots would burn 14 a day against about 4 for ten people in houses - which is the reason to have
  built the houses by winter.

## What the rates mean (rough, from today's constants)

- **Fields** grow about 30 days a year (not in winter, not under the snow - fewer in the north) and a
  coarse cell is about 16 in area. For a cell to feed a person a year, 40 days' worth, the yields
  (`crop_yield` in `Economy.cpp`) go up:

  | Crop | Days to grow | Harvests a year | Yield per area now | For one person a cell |
  |---|---|---|---|---|
  | Wheat | 18 | 1.7 | 0.6 | 1.5 |
  | Greens | 8 | 3.75 | 0.35 | 1.35 |
  | Beans | 25 | 1.2 | 0.45 | 1.05 |

  As they stand, the 10 settlers would need about 27 cells of wheat. A farmer's pace (0.68-1.4) moves
  every figure either way.
- **The first spring is on a knife-edge.** The camp's 20 wheat and 10 beans are 40 days of food, four for
  ten people, and full health is five more: the first death about day 9 - just when the earliest greens,
  painted on day 1, could come in. Its 20 water is two days; with the three in the bar and the dry spell
  after, deaths about day 7 without a collector. A collector makes about 11 a day, enough for ten. The
  starting supplies may want raising.
- **Winter**, for ten people: 100 days of food and, where the collectors are under the snow, 100 water,
  stocked before it - about ten plot-storeys of store - and the firewood.
- **A house's room is too small.** `ECONOMY_HOUSE_ROOM` gives about 6 a plot-storey, and two days of food
  and water for a family of three is already 12. It goes to about 4 (16 a plot-storey).

## Not in this step

- **Spoilage** (`economy_plan.md` put it with eating): the foods already differ in worth and growing time.
  Greens rotting when winter comes is the first piece of it, once eating runs.
- **Crowding** (`people_plan.md`): nothing fills a house past its room until there are births.
- **Working slower when hungry**, a warning before death: not asked for; a candidate when tuning.
- **Early gathering** (the user): food from the wild for the first days.

## Build order

1. Health and water per person: falling, eating and drinking at home from the house's or the camp's
   stock, dying. Saved, hashed, on the person's card and in `chasm_economy`.
2. Fetching home from the stores.
3. Cold: firewood burned under the snow, and the cold drain.
4. The yields and a house's room retuned. Measured: a new colony with nothing built (the first death about
   day 9), the same with a collector and a greens field (it lives), and a first winter with and without
   firewood.
5. The overlay: a death says so, as a refusal does; a lost colony says so.

After each step, `tools/chasm_replay_test.py` must still pass.

## As built: step 1, health, water and death (2026-10-07)

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

**Until step 2, a family in a house dies within four days**: nothing fills a larder, and only the camp's
supplies feed anyone. Every save with houses lived in - `village` among them - dies off.

**Open:**
- A worker HOLDING a load no store will take waits where he is, away from home, and can starve there with it.
- Nothing yet says a death happened beyond the card and the tool - that is step 5's overlay.
