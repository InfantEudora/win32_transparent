# Goods: categories, and rope from hemp

The user's, 2026-10-10. Rope is needed for the winch and, later, the zeppelin: a building material made from
**hemp**, grown on fields. More goods are coming (fruit, meat, leather, coal, stone, floatstone), and the cards
that show a stock will get full, so the goods get **categories** at the same time - as Kingdoms Reborn (the
user's screenshot) puts its foods under one total and its materials under another.

Talked through the same day: the proposal in chat, the user's answers to its four questions below.

## Decided (the user, 2026-10-10)

- **Hemp is a crop of its own**, grown on a field like the others. The old note that gave rope to wheat's
  straw (`gameplay_plan.md`, "Goods and food") is scrapped. **Hemp gets its own look on the field.**
- **Rope is made in a ROPEWALK**, a building of its own (a real ropewalk is a long shed: the strands are laid
  out along the whole rope). **It works from 2 plots; a 3rd and a 4th make it more efficient.** The first
  building whose size raises what it does - a mechanic thought of for houses and gardens, and this is the
  first good use of it.
- **Rope wearing out** (a winch's rope replaced now and then): **later**, kept as an idea.
- **Four categories: FOOD, WATER, MATERIALS, INPUTS.**
  - **Coal is an INPUT**: what is burned, used up, or made into a product - a continuous process.
  - **Leather is a MATERIAL**: the zeppelin is built from it.
  - **Upkeep makes a material an input too**: maintaining a winch, or repairing a zeppelin, would use up
    rope and leather, and there they are inputs.
- **The proposals below were agreed as they stand** (the user, 2026-10-10) - the categories' rule, hemp's
  rotation, the ropewalk's details, rope made whenever there is hemp, 4 rope a winch storey, the view - **except
  the first rope: the colony starts with none.** The winch waits for rope to be made: a hemp field and a
  ropewalk first. "So you have to set up production for it."

## Proposed, and agreed

### The categories

The rule, as the user put it: **a MATERIAL is built with, once; an INPUT is used up, continuously.** FOOD is
eaten and WATER drunk - the needs, which are inputs of their own kind, kept apart because everyone uses them.

| Good | Category | Status |
|---|---|---|
| wheat, greens, beans | FOOD | in the game |
| provisions | FOOD | in the game (a soldier's food, `defence_plan.md`) |
| water | WATER | in the game |
| wood | MATERIALS | in the game - and burned (below) |
| **hemp** | INPUTS | this plan: made into rope |
| **rope** | MATERIALS | this plan: the winch; the zeppelin later |
| fruit, meat | FOOD | later (`gameplay_plan.md`: short- and medium-keeping) |
| leather | MATERIALS | later: the zeppelin |
| coal | INPUTS | later: burned for warmth, perhaps for steam |
| stone | MATERIALS | later |
| floatstone | MATERIALS | later: the zeppelin's hull (`gameplay_plan.md`, "Floatstone") |
| steam (in canisters) | INPUTS | later, if it is a good (`gameplay_plan.md` section 4) |

- **A good used both ways has ONE home category**, where it is listed, by what it is mostly for; where a
  building uses it up, that building's card names it as its input. WOOD is the first such good already: it
  builds, and it is burned - and the house card shows the burning apart today, as its Woodshed beside its
  Pantry. So wood's home is MATERIALS. Rope and leather stay MATERIALS when upkeep comes; the winch's and the
  zeppelin's cards would name them as their inputs.
- **Only a category whose goods stand in for each other is ever summed.** Any food feeds someone, so FOOD is a
  total (the top bar does this today). A winch needs rope and no wood will do, so MATERIALS and INPUTS are
  groups, never sums: rope is listed next to wood, not added to WOOD's number.
- **Fifteen goods fit a mask**: `GoodsMask` is 16 bits (`Goods.h`, widened for this in `defence_plan.md`).
  Six today, eight with hemp and rope, fifteen with everything above.

### Hemp

- **A fourth crop**, `ZONE_CROP_HEMP`, appended (never renumber), and a good of its own, `GOOD_HEMP`. Chosen on
  a field's card like any crop, as its starting crop or its next.
- **Not food.** Today a crop's good is `GOOD_WHEAT + crop` (`GoodOfCrop`) and every crop is a food; hemp needs a
  table from crop to good instead. **AUTO never picks it** - AUTO is "the most food" (`fields_plan.md`, "AUTO by
  food") - so hemp is grown only where the player asks for it, and a hemp field's next crop defaults back to food.
- **In the rotation** (`crop_after`, current tuning, to agree): hemp after hemp wears it down like any repeat;
  what follows hemp does well, since hemp smothers the weeds; after beans it does well too, as everything does.

  | Yield of the next crop | wheat | greens | beans | hemp |
  |---|---|---|---|---|
  | after wheat | 60% | 100% | 100% | 100% |
  | after greens | 100% | 70% | 100% | 100% |
  | after beans | 120% | 120% | 60% | 120% |
  | after hemp | 110% | 110% | 100% | 60% |

- **Its harvest goes to a store**, never home: the land's farmer carries it to the nearest store that takes
  hemp (a store beside the ropewalk takes it, as a store beside any workplace takes that workplace's goods).
  The camp's land puts it in the camp's supplies, as with food.
- **Its look** (the user's): tall - well above the wheat - and dense, dark green stalks with fan-shaped leaves
  at the top; ripe, a paler yellow-green with seed heads. Built in `CropMesh` like the other crops, through the
  same stages (bare, sprouts, green, ripe); only the ripe stage, and the height from green on, are its own.

### The ropewalk

- **A new kind**, `ZONE_KIND_ROPEWALK`, appended, with a tool on the build grid (its key to be chosen).
  Painted over plots like any building, **one storey** - a long low shed - and **at most 4 plots**, like a house.
- **It works from 2 plots.** A drag lays one plot at a time, so a one-plot ropewalk can be placed; it stands
  idle and its card says so - "Too short: a ropewalk works from 2 plots" - as a woodcutter with no lot does
  (`yards_plan.md`). A NEEDS ATTENTION row for it, like the woodcutter's.
- **Longer is more efficient**, for the same one ropemaker: rope made per hour of his work at 2 plots x1, 3 plots
  x1.5, 4 plots x2 (current tuning: `ECONOMY_ROPE_SECONDS` a rope at 2 plots, divided by plots / 2). Only plots
  that stand count.
- **One ropemaker**, a new job (`WORKER_JOB_ROPEMAKER`), filled like any workplace. His skill: **precision**
  (proposed - a craft, next to the toolmaker and the zeppelin wright in `gameplay_plan.md`'s skills table), at
  whose pace he works.
- **His round**: fetch hemp from a store, a load at a time; make rope at the ropewalk, **2 hemp a rope**
  (tuning); the rope stays at the ropewalk, anyone's, up to a pile - carried off and fetched from like the
  woodcutter's woodpile and the collector's water. He stops when the pile is full or no hemp is in reach.
- **No demand count, unlike provisions.** Provisions are made only as wanted because they eat food that is
  needed elsewhere; hemp has no other use, so turning all of it into rope loses nothing. How much rope the
  colony makes is how much hemp the player grows.

### Rope as a cost

- **The winch is built with rope**: per storey, with its wood (`StoreyGoods`, as the tower takes water and
  provisions - the site costs per good from `defence_plan.md`, step 4). 4 rope (tuning). The zeppelin later.
- **No first rope** (the user, 2026-10-10): the colony starts with none, so a winch waits for a hemp field's
  harvest and a ropewalk. Its site stands staked out, wanting rope, until then - which is what should send the
  player to set up the production. (Proposed was a camp's supply of rope for one winch; not wanted.)

### The view

- **The top bar** keeps PEOPLE, WOOD, FOOD and WATER. Another good shows there while the colony holds any, or a
  site wants it - so a colony that has never seen rope is not shown a 0. Hovering FOOD lists its foods.
- **A card's stock is a strip per category**, as a house's already is (its Pantry, food and water, and its
  Woodshed): a store's "Food and water", "Materials" and "Inputs", each only when it holds something, each
  against the store's whole room so that together they fill as the one strip does now.
- **A store's "Takes:" chips by category**: a line each - the category's name, a chip for the whole group, then
  its goods' chips. Eight chips today fit; fifteen on one line would not.
- **Colours by family**: foods warm (yellows, greens, the beans' rose), materials browns and greys, inputs a
  family of their own - so fifteen goods do not need fifteen unrelated colours (`HudGoodColour`).

### Code that changes whatever the details

- `GoodIsFood` is `good >= GOOD_WHEAT`, and its comment says the first non-food good breaks it: replaced by
  the category, `GoodCategory(g)`, with `GOODS_FOOD` and the rest built from it.
- `GoodOfCrop` (crop to good by order) becomes a table; every per-crop table (`crop_after`, `crop_yield`,
  `CropTitle`, the food worth) takes a hemp entry, and AUTO skips a crop that is not food.
- `HudGoodsOrder` goes by category.

## Build order

1. **Goods**: the categories in `Goods.h`, `GOOD_HEMP` and `GOOD_ROPE` appended, `GoodIsFood` replaced, names,
   colours, a store's chips for them.
2. **Hemp**: the crop, its rotation and yield, AUTO skipping it, the harvest to stores, its look.
3. **The ropewalk**: the kind and its tool, its mesh, the ropemaker, working from 2 plots, efficiency by plots,
   the pile, its card and NEEDS ATTENTION row.
4. **Rope as a cost**: the winch's rope; NEEDS ATTENTION pointing a winch site short of rope at hemp and a
   ropewalk.
5. **The view by category**: the top bar, a card's strips, a store's chips.
6. **Checked in the game**: a hemp field sown, grown and harvested into a store; a one-plot ropewalk idle and
   listed, a second plot and it works; rope made faster at 3 and 4 plots; a winch site waiting for rope, then
   built with made rope; saves and a replay.

## As built

### Step 1: the goods (2026-10-10)

- **`Goods.h`**: `GOOD_HEMP` 6 and `GOOD_ROPE` 7 appended (`GOOD_COUNT` 8); `GOOD_CATEGORY_FOOD / WATER /
  MATERIALS / INPUTS` and `GoodCategory(g)` - wood and rope materials, hemp an input; `GoodIsFood` is now the
  category's, so a good appended later needs only its line there. `GOODS_FOOD` and `GOODS_EATEN` are unchanged
  (provisions stay out of `GOODS_FOOD` on purpose).
- **`Economy.cpp`**: their names, "hemp" and "rope". Nothing else in the economy needed changing: every loop over
  the goods is generic, a house keeps an explicit list (`ECONOMY_HOUSE_KEEPS`), and a good with no food worth
  feeds nobody. Stores take every good by default, so they take hemp and rope.
- **`ChasmHud.cpp`**: rope a greyer tan than wood, hemp a sage green; `HudGoodsOrder` by category - materials,
  the foods, water, inputs - which keeps today's order and puts rope beside wood: wood, rope, wheat, greens,
  beans, provisions, water, hemp.
- **`ApplicationChasmSelect.cpp`**: a site's errand says its good (`SiteErrand`) - "fetching rope for a site"
  rather than "fetching wood", which every good that was not water or food used to be called.
- **Saves**: goods are saved by name and a store taking everything is not written, so a save from before loads
  with its stores taking hemp and rope too. The state hash covers more bytes (`GOOD_COUNT` of them a stock); chasm
  keeps no hash baselines.
- **Checked** (debug, a copied exe on 8774, paused and stepped): a store given 4 wood, 3 rope, 2 beans and 5 hemp
  (`set_stock`) took them; `stored` gave hemp 5 and rope 3, and its food (72) counted no hemp; its card's strip read
  "4 wood, 3 rope, 2 beans, 5 hemp" and its Takes chips ran wood, rope, the foods, water, hemp, over three rows - which
  is what step 5 is for. Its rope chip pressed: rope off; saved, the store's `allow` written by name without rope;
  loaded: the same setting and stock back.

### Step 2: hemp, the crop (2026-10-10)

- **`Zones.*`**: `ZONE_CROP_HEMP` 3 (`ZONE_CROP_COUNT` 4), named "hemp" - so the field's crop ops, saves and the
  tools take it by name.
- **`Goods.h`**: `GoodOfCrop` is a table (wheat, greens, beans, hemp), `static_assert`ed in `Economy.cpp` against
  `ZONE_CROP_COUNT`; `GOODS_CROPS`, what a field yields - the foods and hemp.
- **`Economy.cpp`**: hemp grows 15 days and yields 0.6 an area (current tuning: wheat's season, and a one-cell field's
  harvest - 7 on the test cell of area 12.7 - is near the 4 rope a winch storey takes, at 2 hemp a rope); the
  rotation row and column as agreed. **AUTO never sows it** (`CropIsFood`): in `EconomyAutoCrop`, and in
  `EconomyFieldSowNow` - where, without the check, hemp would have been AUTO's pick whenever it was the one crop left
  that ripened in time, its food of 0 beating none. **The harvest**: `FetchHarvest` and `PickHarvest` look for food
  first, exactly as before, and for hemp once no food lies on the field, so hemp never keeps a family's food waiting;
  hemp never goes home - the camp's supplies or a store (`FindStore`). A store beside a field takes `GOODS_CROPS`.
- **The look** (`CropMesh.cpp`, the user's "its own visual"): the same bare and sprout stages as every crop; from green
  on, rows of tall plants instead of ridges - a narrow stalk widening to a crown under a four-sided point, every other
  row set half a plant along, each a little off its row and taller or shorter than the next - 0.45 high when green,
  0.75 ripe (a person is drawn 0.86). Green in the shared dark green; ripe in two colours of a new palette row,
  **`PAL_CROPS`** (row 9, spare until now): `PAL_HEMP` olive stalks, `PAL_HEMP_HEADS` a yellow-green crown. That row
  is no season's or biome's, so no season fades it and no snow lies on it. Two 8x8 cells of `palette.png` written,
  nothing else changed in it. **First built flat-topped**, and from the game's camera that read as a grid of tiles,
  hay bales once ripe; the pointed crowns and the stagger read as plants.
- **The field's card** offers Hemp with the other crops; its Next row now has five chips and widens the card to fit
  (it widens rather than wraps, as it did before).
- **Checked** (debug, a copied exe on 8774, paused and stepped, the default seed): a one-cell field started with hemp
  beside one with wheat, both the camp's land; the farmer sowed both; the hemp stood as staggered dark green plants at
  green and olive ones at ripe, the wheat as ridges beside it, and from the game's distance the hemp field read as its
  own; the harvest, 7, went to the camp's supplies in two loads; AUTO then sowed beans, not hemp, the yields after hemp
  showing wheat 110, greens 110, beans 100, hemp 60. A store painted beside the field took wheat, greens, beans and hemp.
- **Not checked by a run**: a house's land harvesting hemp into a store (the same `FindStore` every harvest load takes
  when it is not going home).

## Later

- **Rope wearing out** (the user's: later, kept as an idea): a winch's rope is used up as it hauls and has to be
  replaced - rope as a steady trade, not a one-off. With it, **upkeep** as a kind of demand: the zeppelin's
  repairs using leather and rope. There rope and leather are inputs (above).
- **Size that raises what a building does**, for houses and their gardens: the ropewalk is the first.
- **Which site gets scarce rope first** - winch or zeppelin (The Settlers II's distribution settings): the card's
  "Build first" covers it until there is more than one user of rope.
