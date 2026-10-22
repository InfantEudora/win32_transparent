# Settlements, part two: goods between them

**Part one is finished**: territories, borders, founding by taking a tent, Settle here, one camp to a colony and
land growing from every building are built, and the record of what was decided and built is
`done/settlements_plan.md` (moved there 2026-10-21). **Code comments naming `settlements_plan.md` with a section
name mean that file.** This one holds what part one left to think through: how goods get from one settlement to
another, and what is still counted for the whole colony.

## What happens today

- **People work only in their own settlement** (`Economy::Ours`): carrying, fetching, a job, the builders' errands.
  So nothing a settlement holds ever reaches another on its own.
- **Goods reach a new settlement only by the player's hand**: what an expedition packs when it sets out, what its
  own camp's woodcutter, picker and water carrier bring in, and crates - a camp's goods left where it stood, a load
  dropped by someone posted, a PICK UP that sends builders past their land (`stone_plan.md`).
- **Counted for the whole colony**, not per settlement:
  - **Kept stock and orders** (`EconomyState::kept`, `ordered`). `KeptShort` and `ProvisionsLacking` count every
    store and every site and tower in the colony. So one settlement's stores can hold the kept provisions while
    the other's people, who may not fetch from them, have none and make none.
  - **The top bar** (`EconomyStoredTotals`): every store and camp's supplies together.
  - **NEEDS ATTENTION** (`ApplicationChasmIssues.cpp`): its wood check reads the colony's total.

## To think through

- **Porters** (the user's, 2026-10-11): a job of its own, maybe with a building where what goes where, and how
  much, is managed - between settlements, and later by winch and zeppelin (`steam_plan.md`).
- **Per settlement**: kept stock and provisions, the top bar, NEEDS ATTENTION.

## Decided (the user, 2026-10-21)

- **Per colony first**, as a step of its own, before porters. **The Statistics page shows the totals, and each
  colony's.** (This takes up what `statistics_plan.md` left for later: "Per settlement: not now".)
- **Porters work much as a zeppelin's route will.** How a route is set up is the user's to think through.
- **A depot, a building of its own**, so the goods in it are easy to reserve: **only porters carry to it or from
  it.**
- **The top bar shows the colony under the camera**; with none there, the colony under the cursor.

A colony is a settlement, as the game names it (COLONY 1, 2, 3, `SelectionColony`); "the whole" is all of them.

## Step 1: per colony (agreed, the user 2026-10-21; all four built 2026-10-22)

### A colony's number, and it lasts

Kept numbers and the statistics' rows are saved, so they need a name for a colony that lasts. Today's doesn't: a
settlement is named by its oldest home's building id, which changes when that home goes, and COLONY N is its place
in the list, which changes when an older colony goes.

- **Numbered once**: 1, 2, 3, given when a settlement is first founded, never given again.
- **Carried by its homes** (by building id, saved and hashed): a home takes the number of the settlement it lies
  in, at each `Settle`; a settlement takes its heart's - the home that gave it its land. So erasing the camp keeps
  the colony's number: the oldest house already carries it. A home founding one, with no number yet, takes the
  next.
- **Two growing into one** (a colony of houses taken into another's land): the homes take the one they joined.
  The other colony has ended; its rows stay in the history.
- **COLONY N on the panel is this number**, so colony 2 stays COLONY 2 when colony 1 is gone.

### Kept stock and orders by colony

- `kept` and `ordered` by colony. **A new colony keeps none** (`ECONOMY_KEPT_START`), as a new game does.
- **Counted in its own land only**: `KeptShort`, `ProvisionsLacking` and `OrderMore` count the colony's stores,
  camp supplies, sites, towers and the crates on its land. A builders' hut makes provisions for its own colony,
  as its builders already work only there.
- **The cards** of a builders' hut and a ropewalk show and set their own colony's ("Colony 2 keeps 10
  provisions"; with one colony, as today). `CHASM_CMD_KEEP` and the order for more carry the colony, taken from
  the building whose card it is. `chasm_economy` reports and sets kept stock by colony.

### The top bar and NEEDS ATTENTION

- **The top bar**: the colony under the camera's centre, else under the cursor; over nobody's land with neither,
  the last one shown, so panning over open land doesn't make it flicker (the agent's default, agreed). It names
  the colony once there is more than one. The view's alone: nothing in the rules changes with it.
- **NEEDS ATTENTION** (the agent's proposal, agreed): follows the top bar's colony, so "WATER 2 DAYS" is about the
  place being looked at. **Dangers** (`ISSUE_DANGER`) show from every colony, named, so a colony starving
  off-screen is never hidden.

### Statistics by colony

- **A row a day for each colony** beside the whole's: its people by their home's colony, its goods by where they
  lie (a load or a pack by its carrier's colony), its trees felled, rocks broken, storeys raised and zombies killed
  by where it happened. The journal's events carry the colony they happened in.
- **Nobody's**: the homeless and what lies on land no colony holds are counted in the whole only, as a share of
  their own, so the colonies and that share add up to the whole. The check (`statistics_plan.md`) adds that sum.
- **The page**: ALL, COLONY 1, COLONY 2... to choose from; with one colony, as today.

### Steps

Each checked in the game, and a recording replayed twice, every tick equal.

1. The colony's lasting number, and COLONY N by it.
2. Kept stock and orders by colony: the cards, the commands, provisions and rope.
3. The top bar and NEEDS ATTENTION by colony.
4. Statistics by colony: the rows, the journal, the page, the check.

### As built: the lasting number (step 1, 2026-10-22)

- **Carried**: `EconomyState::colony` by building id and `next_colony`, saved (`colonies` as [home, number] and
  `next_colony`, once one is given) and hashed - only once a second has been given, since until then every home
  carries 1, which the zones already say, so a colony of one hashes as before. `EconomySettlement::colony`;
  `EconomyColonyNumber`.
- **Given** at the end of `Settle`: each settlement, in the order founded, its heart's number - the next when the
  heart carries none or one an older heart took - then every home joined to it that number.
- **A save from before** has none: the settlements standing are numbered afresh in the order founded, which is what
  COLONY N showed then.
- **Shown**: COLONY N (`SelectionColony`) by it; the Statistics page's NOW lists COLONIES, each "colony N";
  `chasm_economy`'s and `chasm_stats`' settlements report `colony`.
- *Checked* (a copied exe on 8774, the default seed, debug placement): the start camp colony 1; a house 82 units
  west colony 2; a house beside the camp, in colony 1. Every tent erased: the far house, its id the older, was
  founded first and stayed COLONY 2, the house by the camp COLONY 1, all 12 crammed into it - in the old order the
  two would have swapped. That house erased, colony 1 gone; a house on its old land founded colony 3. Saved as
  `saves/colony_numbers_test.json` ([[2, 2], [4, 3]], next 4) and loaded: 2 and 3 again. `camp_3`, from before:
  1 and 2, as before. A store and a house chained off colony 2, the house joining it; the store erased, the house
  cut off founded its own - COLONY 4, colony 2 keeping 2. A 3,994-tick recording (the first part) replayed twice,
  every tick equal, ending at colonies 2 and 3.

### As built: kept stock by colony (step 2, 2026-10-22)

- **By colony number**: `EconomyState::kept` and `ordered` are a row of goods per colony; `EconomyKept`,
  `EconomyOrdered` and `EconomyKeptLevel` take the colony, a colony with no row keeping the start's. `KeptShort`,
  `ProvisionsLacking` (its sites, towers, stores, camp's supplies, empty houses, huts, the crates on its land, what
  is made and carried there) and `EconomyStoredTotals` take a settlement; the order cleared each tick per colony.
  A builder's errands (`MakeProvisions`, `AtWorkshop`) and a workplace's made goods to a store go by the colony of
  his workplace; `MakeProvisions` counts only its own colony's huts as making.
- **The command**: `CHASM_CMD_KEEP`'s value[2] is the colony's number, taken from the card's building; 0 - a
  recording from before - the oldest standing. `chasm_economy` op keep and order_more take `colony` (default the
  oldest standing); `kept` and `provisions.lacking` are listed by colony, each hut with its colony.
- **The cards**: "Colony 2 keeps 5 provisions in its stores - 8 there now" with more than one colony, "The colony
  keeps" with one; "No store takes" looks at its own colony's stores.
- **Saved** as `kept` and `ordered` by colony number, then good (`{"1": {"provisions": 10, "rope": 0}}`). A save
  from before names the goods at once: they become colony 1's. Hashed as before for colony 1, with the colony for
  any other, so a colony of one hashes as it did.
- *Checked* (a copied exe on 8774, the default seed, debug placement; a builders' hut and a store at the camp,
  colony 1; a far house of three put down, a store, a builders' hut, a debug well and a crate of fruit, colony 2;
  12 wheat and 12 beans at each hut):
  - Colony 1 keeps 10: lacking 10 there, 0 in colony 2. Colony 1's hut made its 10 into its store; colony 2's
    made none - its wheat went to its store, as a hut's does with nothing wanted.
  - Colony 2 keeps 5 while colony 1's store held 10: colony 2 lacking 5 - colony-wide, the 10 would have covered
    it and nothing been made. Its hut made 5, into its own store.
  - An order for 3 more at colony 2: its level 8, lacking 3; made, and once its store held 8 the order was gone
    and the level 5 again. Colony 1 untouched throughout.
  - The cards: "Colony 2 keeps 5 provisions in its stores - 8 there now", "Colony 1 keeps 10 ... 10 there now".
  - Saved as `saves/kept_by_colony_test.json` and loaded: the same. A save made in the old form (provisions 7,
    rope 3): colony 1's, colony 2 none.
  - An 18,311-tick recording of all of it replayed twice: every tick equal, ending as the run did.
- *Seen on the way*: an outpost put down with no water dies of thirst in a few days, and its family eats a store's
  wheat before the builders can make provisions of it - the economy, not this step. The tests gave it water and
  fed the hut directly.

### As built: the top bar and NEEDS ATTENTION by colony (step 3, 2026-10-22)

- **The colony in view** (`ViewColony`, render thread and the tools): the plot under the camera's point on the ground
  (`weather_view.at`, as the heat readout reads it), else under the cursor (`GetHoverPick`), else the last shown -
  kept by its number - else the oldest. With one colony or none: ECONOMY_EVERY_SETTLEMENT, everything as before.
- **Whose a person is** for these (`ChasmPersonSettlement`): his home's, else the land he stands on - what COLONY N
  (`SelectionColony`) already did, now shared.
- **The top bar**: COLONY N first, dim; PEOPLE its people, the goods its stores' and camp's supplies, a good shown
  while one of its own sites wants it (`ChasmBarGoods` takes the settlement).
- **NEEDS ATTENTION** (`ChasmIssues` takes a settlement; `ViewIssues`): every row of its own - buildings and sites by
  their door, plots by their land, people as above; days of water and food (`ChasmDaysHeld`), the camp's count
  (`ChasmInCamp`), wood, rope and stone held - so an outpost is told BUILD A WATER COLLECTOR for itself. Then every
  other colony's dangers, "COLONY 1: ...", and all sorted as ever. Titled "NEEDS ATTENTION, COLONY 2".
- **The tools**: `chasm_issues` lists the panel's, with `colony` for the list and each row; `chasm_economy`'s
  `bar` and `held` are the colony in view's, as drawn, with `colony` (0 the whole).
- *Checked* (a copied exe on 8774, `saves/kept_by_colony_test.json`, play mode):
  - The camera on the camp: COLONY 1, PEOPLE 12, its 60 wood; WATER UNDER A DAY its danger.
  - On the outpost: COLONY 2, PEOPLE 3, FOOD 2, WOOD 0; its own BUILD A WATER COLLECTOR (WATER 2 DAYS), FIELD and
    WOODCUTTER, under "COLONY 1: BUILD A WATER COLLECTOR - WATER UNDER A DAY" in red. Seen on screen.
  - The camera over land nobody holds: COLONY 2 kept. The cursor pinned on the camp: COLONY 1; the cursor moved off
    over open land: COLONY 1 kept.
  - A new map, one colony: no name on the bar, `colony` 0, the bar and list as before.
- **No replay**: the view's alone - nothing the rules read changed, so nothing a trace hashes can.
- *Seen, left for the user*: the jobs overview and MOST IN NEED, at the bottom left, are still the whole colony's.
  *Answered* (the user, 2026-10-22): MOST IN NEED covers every colony - that is right. The jobs overview left as it is.

### As built: statistics by colony (step 4, 2026-10-22)

- **The rows** (`Economy.h`, THE HISTORY, BY COLONY): `EconomyDay::colonies`, a row per colony by its number in
  number order (`EconomyColonyRow`), each the same as the whole's with its `colony`; colony 0 NOBODY'S. Filled in the
  same passes as the whole's: `EconomyDayLevels` - people by `EconomyPersonLand` (their home's settlement, the
  homeless by the land they stand on; shared now with the view's `ChasmPersonSettlement`), a building's stock and
  pantry by its settlement, a crate by its plot, a load and a pack by its carrier - and `Economy::Event`'s counts
  to the event's colony. Every standing colony has its row; nobody's, and a colony gone, only while they hold
  anything.
- **An event's colony** (`EconomyEvent::colony`, stamped in `Event`): its building's settlement, else its plot's, else
  its person's - by number.
- **Saved**: a row's colonies after its zombies (then always written), each the same array with its number in the
  day's place; a journal line's colony as a tenth field when it has one. A save from before has none.
- **Hashed** only on a day with a second colony, and an event's colony only past 1: a colony of one hashes as before.
- **The figures** (`ChasmStats.h`): `ChasmColonyHistory` - a colony's history from the whole's: its rows, its journal,
  from its first row (FOUNDED then, or KEPT SINCE in a history from before the colonies were counted) -
  which `ChasmStatsSum`, `ChasmStatsSeasons` and the charts read as they read the whole's; `ChasmStatsNowOf` with a
  colony - its own row, people, homes, buildings, beds, and the deaths its journal has; `ChasmStatsColonies`.
- **The check** (`ChasmStatsCheck`): each row's colonies and nobody's add up to it - people, goods, counts - the
  start, every row and today's.
- **The page**: ALL, COLONY 1, COLONY 2... chips beside the title with more than one colony
  (`OVERLAY_HIT_STATS_COLONY`); ALL each time it opens; an end's page is the whole's. `chasm_select`'s op press
  presses them as the mouse would, and its action names were brought up to date.
- **`chasm_stats`**: `colony` narrows every figure but `check` and shows it on the page; `colonies`, `page_colony`; a
  row's `colonies`, a journal line's `colony`.
- *Checked* (a copied exe on 8774, a new map of the default seed; an outpost house far west with a store, a well, a
  woodcutter's hut, three people put down and a crate of fruit; three days stepped):
  - One colony: no chips, `colony` 0, the check clean.
  - Two: the last row's colonies 12 and 3 people; trees felled 7 and 2 of the whole's 9; the add-up check clean.
    Colony 2's journal its two lines, found a river.
  - Saved as `saves/stats_by_colony_test.json` and loaded: colony 2's totals, rows, start and journal the same.
  - A 6,917-tick recording of it replayed twice: every tick equal, the history part included; ending as the run did.
  - The page: opened, its COLONY 2 chip pressed (op press) - COLONY 2, FOUNDED Y1 SPRING 1, SINCE IT WAS FOUNDED, its
    3 people, 4 buildings, 42 water, its charts and journal. Seen on screen.
  - The check's one line, "row day 0: 15 people, but ... make 12", is the test's own: three people put down by the
    debug tool, no birth - as before this step.
