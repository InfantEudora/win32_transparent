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

### Step 3: the ropewalk (2026-10-10)

- **`Zones.*`**: `ZONE_KIND_ROPEWALK` 12 (`ZONE_KIND_COUNT` 13), "ropewalk", one storey, a house body (so arches
  bridge it like the other bodies, the same kind only). **`ZoneKindMaxPlots`** replaces the house-only limit in the
  three places that had it (`ZoneCanBuild`, `ZoneCanExtend`, a stroke going on with a new building) - a house 4, a
  ropewalk `ZONE_ROPEWALK_MAX_PLOTS` 4 - each refused with its own reason ("a ropewalk is at most four plots - longer
  makes no more rope"). The house's numbers are unchanged.
- **The look** (`BuildingMesh.cpp`, `kind_looks`): plaster under thatch, a combination no other kind has; now
  `static_assert`ed to have a look for every kind. Its props came after, below.
- **The tool**: `CHASM_TOOL_ROPEWALK`, key **K** (for knot - K, P, L and M were free), on the build grid on a row of
  its own, the WORKSHOPS row, with room for those to come (the grid's cells may now be left empty); the debug panel's
  radio and `chasm_tool` / `chasm_paint` take "ropewalk".
- **The economy**: `WORKER_JOB_ROPEMAKER` 6 (`WORKER_JOB_COUNT` 7), "ropemaker", by precision; one place once
  `ECONOMY_ROPEWALK_MIN_PLOTS` (2) stand. His round from home (`Economy.h`, ROPE): a load of rope waiting there to a
  store (`FetchFromWorkplace`, now limited to what the workplace makes by `WorkplaceMakes` - rope, never the hemp
  there - and only by the load, as water is); else `MakeRope`: to the ropewalk with `ECONOMY_ROPE_HEMP` (2) hemp there,
  or for a load of hemp from a store, the camp, an empty house or a crate. At the ropewalk, `AtRopewalk`: a rope at a
  time, `ECONOMY_ROPE_SECONDS` (8) over his pace and `EconomyRopewalkPace` (plots / 2), up to a load a round, none past
  `ECONOMY_ROPEWALK_PILE` (12) waiting. All current tuning. He walks the builders' provisions errand's states
  (`WORKER_TO_INPUT` ... `WORKER_MAKING`), branched by his job where a builder's hut was; while making rope his
  `carry_good` is `GOOD_ROPE` with nothing carried, so `ProvisionsLacking` counts no rope as a provision and builders
  run exactly as before. A store beside a ropewalk takes hemp and rope; sites' carriers take its rope, never its hemp.
- **The view**: its card - "Too short: a ropewalk works from 2 plots" while fewer are painted (not while the second is
  still a site), else its length and pace, "No hemp to make rope of" when none is to be had, its ropemaker, its rope
  against the pile and the hemp on hand; NEEDS ATTENTION's `ropewalk_short` row; ROPEMAKERS in the jobs overview; the
  ropemaker's lines ("fetching hemp for rope", "making rope").
- **Checked** (debug, a copied exe on 8774, paused and stepped, the default seed): a one-plot ropewalk - "Too short",
  no ropemaker, `ropewalk_short` at the top of NEEDS ATTENTION; painted on (a diagonal step, so 3 plots) - "3 plots long:
  makes rope at x1.5", "No hemp to make rope of", the row gone, and a ropemaker: the colony's most precise (9). 20 hemp
  set in the camp's supplies and a store painted: he fetched 5, made 2 ropes (a rope about every 200 ticks - 8 s over
  pace 1.32 x 1.5), went home with 1 hemp left, fetched 5 more, made 3, and at 5 rope carried the load to the store.
  At 4 plots "x2.0"; a fifth refused with the ropewalk's reason. The grid's K ROPEWALK on its own row, ROPEMAKERS 1 / 1.
- **Not checked by a run**: a 2-plot ropewalk's x1 (the same formula), a site's carriers taking rope from the ropewalk
  (nothing is built with rope until step 4), saving and loading a ropemaker mid-round (his state is all existing fields).

### Step 4: rope as a cost (2026-10-10)

- **The cost**: `StoreyGoods` (until now a stub giving nothing) gives a winch's storey `ECONOMY_WINCH_ROPE` (4) rope,
  with its wood - current tuning. Nothing else changed for it to work: site carriers already took rope from stores,
  the camp, crates and a ropewalk's pile (step 3), and a storey already went up only with all of every good there, so
  a winch site gets its wood and then stands staked out until its rope comes. No first rope: the camp's supplies
  hold none.
- **What the colony lacks to make rope**, in one place for the card and NEEDS ATTENTION: `EconomyRopeChain` - hemp
  first (none held anywhere, none growing, no field with it chosen as its first crop before it is ever sown or as
  its next), then a ropewalk placed - and `EconomyGoodHeld`, all of a good wherever it lies or is carried, not what
  sites have been brought.
- **NEEDS ATTENTION**: `site_needs_rope`, "WINCH: NEEDS ROPE" (the site's kind, so the zeppelin's will read the same),
  stuck tier, once for all sites, while they want more rope than the colony holds and the chain is not complete. Its
  value is the first missing link: GROW HEMP - a click opens a field's card (a built one first), where hemp is chosen,
  or takes up the field tool with no field - then BUILD A ROPEWALK, the ropewalk tool. With both, it goes: the rope
  is coming, and the ropewalk's own rows (too short, nobody at it) say if it cannot work.
- **The site's card**, under "It takes ...": "No rope to be had: grow hemp on a field, and make it at a ropewalk",
  then "...: build a ropewalk to make it of hemp" (amber), then "Waiting for rope from the ropewalk" (dim). The
  sites line over the grid already counted rope: "BUILDING 1 - WOOD 0/8, ROPE 0/4".
- **Checked** (debug, a copied exe on 8774, the default seed): no balcony lies near the camp, so a villager was
  ordered to the nearest reachable rim (about 100 units off) to explore it, and a winch placed there as a play
  command. Its card: "It takes 13 wood, 4 rope", "No rope to be had: grow hemp ..."; the row GROW HEMP with the field
  tool. A field painted: the row pointed at it, its card offering hemp; hemp chosen as its first crop: BUILD A
  ROPEWALK with the ropewalk tool, and the card's second line. A ropewalk painted: the row gone, "Waiting for rope
  from the ropewalk". 4 rope put at the ropewalk: carriers brought the 13 wood, the site waited, one fetched the
  rope from the ropewalk, and the winch stood. A second winch site with the ropewalk erased: the row back, drawn at
  the top of NEEDS ATTENTION.
- **Not checked by a run**: rope made from a real hemp harvest reaching a winch (the parts were each run in steps
  2-4), and a save or replay with a winch site (its cost is worked out, not stored).

### Step 5: the view by category (2026-10-10)

- **The top bar** (`ApplicationChasmTime.cpp`): PEOPLE, then the goods in `HudGoodsOrder` - WOOD, FOOD and WATER
  always, any other good while the stores and the camp hold some or a site still wants it (`SiteCosts`, now shared
  with the sites line over the grid, less what was brought): ROPE beside WOOD, HEMP after WATER. FOOD stays wheat,
  greens and beans (`GOODS_FOOD`); hovering it lists each food in its colour, provisions too, "for posted people",
  not in the sum. The hover needed the mouse on the render thread: `overlay_mouse_x/_y`, set every pass, and
  `chasm_select` op hover pins a pixel in its place so a script can look.
- **The groups** (`ChasmHud.h`): `HudGoodsGroup` - MATERIALS, FOOD AND WATER together (as a house's pantry holds
  them), INPUTS - with `HudGoodsGroupName` and `HudGoodsGroupMask`; four categories, three groups on a card.
- **A stock strip per group** (`Card::StockByGroup`): a store's Holds, the camp's Supplies and a crate. Holding more
  than one group, its total on a line ("Holds - anyone's: 20 of 23"), then a strip a group it holds any of, each
  against the whole room - or, with no room, the whole stock - its own count at the right (`HudStock::figure`). One
  group, or empty, the one strip as before. A house's pantry and woodshed, a tower's and a site's strips unchanged.
- **A store's chips a row a group**: the group's chip (`OVERLAY_HIT_STORE_ALLOW_GROUP`) - lit while all of it is
  taken, a click takes all of it or, all taken, none - then its goods' chips from one indent past the widest group
  chip, wrapping at that indent. The card widens to three goods a row past the indent (capped as before). The
  first try wrapped from the card's edge, and the food row broke into three ragged lines.
- **Colours by family** were already so from step 1 (`HudGoodColour`): nothing changed.
- **Checked** (debug, a copied exe on 8774): the bar at the start PEOPLE, WOOD, FOOD, WATER - no ROPE 0; rope and
  hemp put in a store and the camp: ROPE 5 by WOOD, HEMP 9 last; with none held and a winch site placed: ROPE 0. The
  FOOD hover over a card: 55 wheat, 4 greens, 30 beans (FOOD 89) and 3 provisions. A store with wood, rope, wheat,
  water and hemp: the total line and three strips; the Materials chip took wood and rope off and on again, and with
  wheat off the Food and water chip lit all of them. The camp's supplies and a crate in groups.

### Step 6: checked in the game (2026-10-10)

One run, debug, a copied exe on 8774, the default seed, nothing changed in the code for it:

- **The ropewalk**: one plot - listed as `ropewalk_short`, no ropemaker; painted on (a diagonal step, so 3 plots) - the
  row gone, Siem Wever (precision 9) its ropemaker.
- **The hemp**: a field by the camp with hemp its starting crop, the camp's land; sown on day 4 of spring and
  harvested 15 days on into the camp's supplies, 12 hemp from its one cell (area 20). Its next crop was AUTO's,
  beans: AUTO never picks hemp.
- **The winch**: a villager ordered to the nearest reachable rim, about 100 units off, to explore it, and a winch
  site placed there in play. NEEDS ATTENTION had no rope row - hemp chosen and a ropewalk standing - and its wood
  was brought by day 6 while it waited for rope.
- **Rope from the harvest to the winch**: the ropemaker fetched 5 hemp from the camp and made rope; 5 were made in
  all, 4 carried to the winch's site by its carriers, and the winch stood - wood first, the rope last, as the rule
  is. 1 rope and 1 hemp were left at the ropewalk.
- **Save and replay**: the last two days - the ropemaker mid-round home, 1 rope at the ropewalk, 1 hemp in the
  camp, the site's 13 wood brought and none of its rope - recorded from the save that a recording starts from (4,500
  ticks, with a recorded `field_next` hemp at tick 1,000), and replayed twice: every tick's hash equal in every
  part (buildings, calendar, economy, explored, world, zones), nothing changed between ticks, both ending with the
  winch standing and the ropewalk holding 1 rope and 1 hemp, as the run did.

With that, every step of the build order is built and checked.

### The ropewalk's props (2026-10-10)

The user: "give it a few props to make it clear it's a ropewalk" - plaster under thatch alone read as a house.

- **What it has** (`BuildRopewalkYard`, `BuildingMesh.cpp`): its WALK - three strands of rope over T-posts,
  waist-high, along each two of its plots side by side, so a longer ropewalk has a longer walk and a one-plot one none;
  and its YARD - a SPINNING WHEEL on two A-frames, a COIL of rope for every 2 waiting there (stacks of 3, six for a full
  pile) and a SHEAF of hemp, leant on the wall, for every rope's worth on hand. Rope has its own palette cell,
  `PAL_ROPE` in the crops row (no season fade, no snow).
- **Where**: only on open ground - no building, road, garden, field or water - in the cells of the open plots beside
  it, which nothing else draws in. A stretch of walk goes in the cell beyond the two plots' edge whose other two plots
  are open, the one toward the camera (south) of two; the yard by the open plot most toward the camera, since a yard
  behind the building is behind its roof. An inside corner counts for less (the wheel barely fits), and ground another
  ropewalk or a builders' hut touches - they draw on open ground too - only if nothing else is open. The wheel always
  faces the camera: an upright wheel is foreshortened from above, so it is big (`ROPEWALK_WHEEL_R` 0.48).
- **Drawn** by `ApplicationChasmRopewalk.cpp`: one mesh for every ropewalk, rebuilt when the zones change or a
  ropewalk's coils or sheaves would - not in the zone mesh, since the coils and sheaves are the economy's. View only.
- **Checked** (debug, a copied exe on 8774): two L-shaped ropewalks, a 4-plot one and a one-plot one, from close and at
  play distance - walks along the sides toward the camera (an L whose open sides all face away has its walk behind
  the roof, the only place there is room), the yards in front, the one-plot one's yard with no walk and close to its
  wall; 12 rope and 5 hemp drew 6 coils and 3 sheaves, 0 and 0 none.
- **Not done**: the ropemaker is still drawn at the ropewalk's plot, under its roof, while he makes rope - walking
  the walk backward, as ropemakers did, would show him at it.

## Kept in stock, and made to order (the user's, 2026-10-10)

The user: a scout is to take provisions along (`defence_plan.md`, "Scouting, provisioned first"), but the builders
make provisions only for the sites and towers that want them, so normally there are none. And rope is made whenever
there is hemp, so it fills the stores unless they are told not to take it. **The builders should make an amount on
demand, or stock up to a set limit - and the same mechanic then works for any building, the ropewalk too.**

### Decided (the user, 2026-10-10, answering four questions)

- **Both**: a standing **KEPT** number - made again whenever the colony holds fewer, so a scout's take is replaced -
  and a one-off **"make N more"** order.
- **For the colony, a number per good**: "the colony keeps 10 provisions". Set from the card of any building that
  makes the good; two builders' huts work toward the one number.
- **Rope keeps being made while there is hemp**, as now. The kept number decides how much of it goes to the stores;
  the rest waits at the ropewalk, its pile full, and he stops.
- **This round: the builders' hut and the ropewalk.** The woodcutter (wood kept to a number, the forest spared) later.

### The rules (proposed)

- **What counts is what the top bar counts**: the good in stores and the camp's supplies (`EconomyStoredTotals`),
  plus what is on its way there from a workshop. Not what waits at the workshop, which is the sites'.
- **The level**: the kept number, or an order's level if higher. An order of N sets its level to what the stores hold
  then, or the kept number if that is higher, plus N - "N more" than the colony would otherwise keep. A second order
  adds N more to it; Cancel clears it; it is done, and cleared, once the stores reach it.
- **Provisions**: made for the sites and towers as now, and besides, until the stores hold the level - one sum,
  what the sites lack plus the level, less everything made, wherever it lies, and being made (`ProvisionsLacking`).
  What the hut holds beyond what the sites still lack goes to a store, so the kept ones lie where a scout or anyone
  posted takes from. Nothing made past the level: they cost a wheat and a beans.
- **Rope**: made to the ropewalk's pile as now (`ECONOMY_ROPEWALK_PILE`); carried off to a store only while the stores
  hold less than the level. Sites' carriers take it from the pile as now. **So by default no rope goes to the stores
  at all** - a change from step 3, where every load did.
- **Both start at nothing** (`ECONOMY_KEPT_START`): no provisions made unasked, no rope carried to the stores unasked.
- **Recorded** commands, saved and hashed, like the guard count: a replay sets them on the same tick.
- **On the card** of a builders' hut and a ropewalk: "The colony keeps 10 provisions in its stores - 7 there now", a
  row of chips for the number (0, 5, 10, 20, 40), chips to make 5 or 10 more, and while an order runs, how far it has
  got and a Cancel chip.

### Build order

1. **The rules**: kept and ordered per good, the commands, save and hash, `chasm_economy` ops.
2. **The cards**: the hut's and the ropewalk's lines and chips.
3. **Checked in the game**: provisions kept and ordered, rope carried up to the number and no further, a replay.

Then the scouting itself (`defence_plan.md`, "Scouting, provisioned first"), which takes from the stores this fills.

### As built (2026-10-10): all three steps

- **The rules** (`Economy.*`): `EconomyState::kept` and `ordered`, by good, for the made goods (`GOODS_MADE`);
  `EconomyKeptLevel` the higher of the two; `SetKept`, `OrderMore`; an order cleared on the tick the stores
  (`EconomyStoredTotals`) reach it. `ProvisionsLacking` counts the level, and what a builder carries to a store as
  held, and with `spare` gives what lies made past the sites' share and the stores' part of the level - what
  `MakeProvisions` and `AtWorkshop` now carry to a store whenever there is some, not only once nothing is wanted.
  `KeptShort` is what the stores still lack, less what is on its way to one; `FetchFromWorkplace` and the pick-up
  at a workplace carry a made good only up to it - and a ropemaker's last few for the level, not only by the load.
  `ECONOMY_KEPT_START` (0) both.
- **Recorded**: `CHASM_CMD_KEEP` (subtype the good; the count; an order or the kept number); saved as `kept` and
  `ordered` by the good's name; hashed only once set. `chasm_economy` ops `keep` and `order_more`, and `kept` in
  its report.
- **The cards** (`KeptLines`): on a builders' hut and a ropewalk that stand - the line, the chips 0 5 10 20 40
  (`OVERLAY_HIT_KEEP`), "Make more, once" or how far the order has got, and 5 more, 10 more and Cancel
  (`OVERLAY_HIT_MAKE_MORE`); amber when no store takes the good. The good is the building's (`EconomyMadeAt`).
- **Checked** (debug, a copied exe on 8774, the default seed, a store and a builders' hut beside the camp, one
  builder): kept 10 provisions - he fetched wheat and beans, made them, carried them to the store, and stopped at
  10; an order for 5 more - the level went to 15, the store reached it and the order cleared itself; the store set
  down to 3 - made again up to 10, exactly. A 3-plot ropewalk with hemp in the camp: kept 0, the pile filled to 12
  and stayed there, none to the store, the ropemaker idle with hemp still in the camp; kept 5 - one load of 5 to the
  store; an order for 3 more - a load of 3, the order cleared at 8. The chips pressed as the player's
  (`chasm_select` op press): 20 kept, 5 more from 20 to 25, Cancel. A 3,000-tick recording with a `keep` and an
  `order_more` in it, from a save holding a running order, replayed twice: every tick equal, both ending as the live
  run did.
- **Seen in the test**: a colony keeping provisions turns wheat into them, so its food on the top bar drops by
  what was made - they are still eaten, as a last resort.

## Later

- **Rope wearing out** (the user's: later, kept as an idea): a winch's rope is used up as it hauls and has to be
  replaced - rope as a steady trade, not a one-off. With it, **upkeep** as a kind of demand: the zeppelin's
  repairs using leather and rope. There rope and leather are inputs (above).
- **Size that raises what a building does**, for houses and their gardens: the ropewalk is the first.
- **Which site gets scarce rope first** - winch or zeppelin (The Settlers II's distribution settings): the card's
  "Build first" covers it until there is more than one user of rope.
