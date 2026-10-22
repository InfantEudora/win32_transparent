# Foraging: fruit from the wild

The user's, 2026-10-11/12, while working out how a new settlement gets going (`done/settlements_plan.md`, "Later,
to think through"). Farming is the only food that comes in, and only in bulk, after a while: a field is built, sown
and grown for days before its first harvest. Gathering should cover the gap. It is enough to keep a small group
(about **4-5 people**) going from a **camp**, never a village.

Options weighed: picking from bushes, fishing at the bank (a copy of the camp's water carrier), hunting (no
animals yet). The user chose bushes: "fits what we currently have nicely".

## Decided (the user, 2026-10-12)

- **Bushes are picked.** The forest's bushes (`PROP_BUSH_A/B`) carry fruit. A picked bush grows it back after a
  **fixed number of days**.
- **Some oaks are fruit trees**: a small chance per oak, so "the light green trees" sometimes bear fruit too.
- **Building over a bush or fruit tree removes it for good.**
- **FRUIT is a new good**, a food. It keeps no better than greens. Spoilage is not built, so for now it does
  not rot at all; once spoilage comes, fruit rots as greens do. **Fruit is eaten first**, ahead of greens.
- **Picking is a job, like the camp's water carrier**: a place at the camp, filled from those living there.
- **A posted person can pick, or draw water, by hand.** Posted people do nothing on their own, so this is a
  one-off order. Right-clicking a bush or fruit tree, or water, offers the choice: *pick fruit* or *draw water*.
  This means an expedition can live off the land before anyone pitches a tent. Today, picking or drawing only
  happens through a camp's jobs, and a camp needs a tent.
- **Everything worked out below, agreed** ("All points are perfect", the same day).
- **The start camp need not live from fruit.** The figures are tuned once the plants are counted (below).
- **The fruited meshes by script**, in `chasm_props.blend`.

## Worked out here, agreed by the user

**The wild**
- **Ripe or not.** A bush or fruit tree is ripe with a number of fruit (`FORAGE_BUSH_FRUIT`, `FORAGE_TREE_FRUIT`)
  or picked. Picking takes what the picker has room for; what is left stays on the plant. Once it is **empty**,
  its days to grow back start (`FORAGE_REGROW_DAYS`).
- **The frost.** At a dawn in winter or under the snow, the fruit on a plant is lost, and what was growing back
  with it: after winter every plant grows its fruit afresh, its days counted from the weather turning. As a field
  loses what still grows on winter's first day, and grows only out of winter and the snow.
- **A new map's bushes all start ripe**, so the settlers' first days have the most to pick.
- **Fruit trees** are oaks marked from the forest's seed with a stream of their own (a new `Chance` salt), so
  every other prop, and every seed's map, stays as it is. Felled, a fruit tree is a tree: a log, then a stump,
  and no more fruit.
- **Woodcutters pass fruit trees by** when choosing the nearest standing tree. They fell one only when it stands
  in a road's, wall's or site's way, as any tree there.
- **Built over is gone**: `EconomyPropCovered` already answers this for trees (and the view already hides what is
  under a built thing), so a covered bush is never ripe and never picked.
- **Seen**: a ripe plant shows its fruit, a picked one doesn't. Built (below) as a FRUIT MESH of its own for each
  kind, drawn over the plant at its transform rather than a fruited copy of the plant, so it shows whatever the
  plant's seasonal stage - but not on an oak thinning or bare, whose canopy it would hang outside of.

**The camp's picker**
- **One place at a camp** (`ECONOMY_CAMP_PICKERS`), camp residents only, by **husbandry** - since 2026-10-14
  residents first, then anyone in the colony no other place needs (done/settlements_plan.md, "The camp's own work
  is the colony's"), so picking goes on once everyone has moved into houses. Filled after the
  water carrier: a founding party of three is a woodcutter, a water carrier and a picker. A farmer's place comes
  only with fields, so it doesn't compete in a new outpost.
- **Where**: the nearest ripe plant **in the camp's own settlement** (`Ours`, as every errand) - and a camp has
  the place only while a plant stands in that land, ripe or growing back, so land with none keeps nobody on. The ceiling comes
  from the land: what grows back inside the borders. A small camp's land is small (20 units for one tent), so
  picking feeds a few and never a village, with no rule saying so.
- **Round**: picks the nearest ripe plants, one after another, until a load (`ECONOMY_LOAD`), at
  `FORAGE_PICK_SECONDS` a fruit at the picker's pace. Then home, into the camp's supplies.
- **While** the supplies hold less fruit than `ECONOMY_CAMP_FRUIT_HOLD`, as the water carrier stops at its hold.
  So the land isn't stripped into a pile nobody needs yet, and spoilage won't waste it once that comes.
- **Nothing takes it over**, unlike a hut taking over the camp's woodcutter or a collector its water carrier.
  Picking fades by itself as fields and houses cover the bushes.
- The **start camp** has the place too, as it has a water carrier, so it is in play from the first minute. It
  also softens the start (`needs_plan.md`).

**The posted: picking and drawing by hand**
- **The right click**: with someone posted (or anyone, since a right click posts them) selected, a right click on
  a plot with a ripe bush or fruit tree opens a small menu at the cursor: **Pick fruit** and **Post here**. On
  water, **Draw water** only, since posting onto water is refused anyway. Anywhere else, a right click posts as
  today, with no menu.
- **Picking**: they walk to the plant, pick what their pack has room for, up to a scout's food
  (`NEEDS_SCOUT_FOOD`), and stay posted there. One plant, once. For more, click again.
- **Drawing**: they walk to the dry plot beside the bank nearest by the walk, as the camp's water carrier does,
  draw at its pace (`ECONOMY_CAMP_WATER_SECONDS`) up to a scout's water (`NEEDS_SCOUT_WATER`), and stay posted
  there. Under the snow, refused: the water is frozen.
- **Whose land**: their own settlement's or unclaimed land, the rule a pressing need already follows.
  Another settlement's plants and banks are refused, the reason told as an order's refusal is.
- **A group**: each picks the nearest ripe plant to the one clicked, as `SpreadPost` spreads a group's posts;
  everyone draws water at the bank.
- **One command**, `CHASM_CMD_FORAGE` (person, plot, fruit or water), recorded and replayed like any order.
  The card says "picking fruit" / "drawing water", then "posted".

## Current tuning

What 4-5 people need: a person eats a day's worth a day (a wheat), a day is 45 s, and a year is 40 days with 10
of winter. So **4-5 days' worth of food per day in season** from one picker, and land that grows back at least
that fast.

| What | Constant | First figure |
|---|---|---|
| A fruit's worth eaten | `EconomyFoodWorth` | 1 day (as wheat) |
| Fruit on a ripe bush | `FORAGE_BUSH_FRUIT` | 2 |
| ...on a ripe fruit tree | `FORAGE_TREE_FRUIT` | 6 |
| An oak's chance of fruit | `FORAGE_FRUIT_TREE_CHANCE` | 10% |
| Days to grow back, once empty | `FORAGE_REGROW_DAYS` | 10 (a season), not counting days under the snow |
| Picking a fruit, average pace | `FORAGE_PICK_SECONDS` | 3 s |
| The camp's pickers | `ECONOMY_CAMP_PICKERS` | 1 |
| ...while its supplies hold less fruit than | `ECONOMY_CAMP_FRUIT_HOLD` | 20 |

The ceiling at those figures: 5 people x 10 days = 50 fruit per regrowth, so about **25 bushes**, or fewer with
fruit trees among them, inside a camp's land. Bushes stand about 1 in 50 spots in open land, thicker along the
forest's edge and thickest in the swamp.

**Counted** (2026-10-12, the default seed, `chasm_economy`'s settlements' `plants`): the start camp's land - its
full reach of 50 units, 12 beds, 1,564 plots - has **23 bushes and 2 fruit trees: 58 fruit** when all are ripe.
With 10 days to grow back that is at most about **5.8 fruit a day, 5-6 people**, if the picker could keep up and
nothing were built over them. The whole map has 733 plants. An outpost of three tents reaches 30 units, about a
third of that area, so about **8 plants and 2 fruit a day** - an estimate by area, not counted. Left as they are
for the user to tune (the user: the start camp need not live from fruit).

## Steps

All five built 2026-10-12 (below).

1. **Fruit, the good.** `GOOD_FRUIT` appended (never renumber), in `GOODS_FOOD`: fetched home, kept in stores
   and pantries, summed in FOOD, eaten first. A colour (`HudGoodColour`). Count the bushes and oaks in each
   settlement's land (a `chasm_economy` field) and set the tuning from it.
2. **The wild bears fruit.** Fruit trees from the seed; each plant's fruit and its days to grow back in the
   economy's state (saved, hashed); growing back at dawn, not under the snow; covered = gone; woodcutters pass
   fruit trees by. The fruited meshes and the view's swap.
3. **The camp's picker.** `WORKER_JOB_PICKER`, the camp's `picker_places` after the water carrier's, the round
   and the hold. Their card line and the camp's card.
4. **Picking and drawing by hand.** The right-click menu, `CHASM_CMD_FORAGE`, the walk, the pack, the refusals
   and notices, groups. `chasm_select` able to press the menu, as it presses chips.
5. **The wiki**: a Fruit page, and Food, Camp, Founding, Posts and Tree brought up to date. A recording of a
   camp picking and a posted person picking and drawing, replayed twice, every tick equal.

## Open

- **Fishing** stays an option for river camps, a copy of the water carrier, if bushes alone prove too thin.
- **Hunting** waits for animals (meat and leather, `gameplay_plan.md`).
- **Spoilage**, when it comes, rots fruit as it rots greens.
- **Idea, not built (the user, 2026-10-12): the hungry eating at a ripe plant**, as at a field's harvest. Today
  nobody picks for themselves: a pressing hunger looks only at buildings and crates (`SeekSupplyOf` - a store, the
  camp's supplies, a field's harvest), so the hungry walk past ripe bushes to them, and a scout out of food with
  no store in reach starves beside one unless sent to pick (*Pick fruit*). Fruit reaches people only through the
  camp's picker or that order. Worked out so far: the nearest ripe plants of his settlement (or land none holds)
  as places to eat, by distance among the others and not a last resort as provisions are; others' meals counted
  as `spare` does; eaten to his fill at the picking pace, the rest left on the plant; the posted also packing
  from it, a villager taking none home (a plant is no `FetchFrom` place). The bulk of it is a plant as a third
  kind of `store` beside a building and a crate - the walk, the meal, the card line, saving, the hash - so left
  for now, the user judging it a big change. Undecided with it: **the posted drinking at the water's edge**
  (`BankPlot` already finds it), as nobody in need drinks from a river today either, only at collectors and
  supplies.

## As built (2026-10-12)

- **The good**: `GOOD_FRUIT` (8, appended), in `GOODS_FOOD` and the food category; worth a wheat (`EconomyFoodWorth`);
  eaten first (`Consume`'s order), packed last (`FillPack`); its colour a berry red (`HudGoodColour`).
- **The plants**: `PropInstance::f_fruit` from `Chance(seed,v,90)` on oaks (`ForestBears`, `FOREST_FRUIT_TREE_CHANCE`);
  `EconomyState::fruit` and `regrow` by prop, `fruit_version` for the view; `Economy::plants` the bearing props.
  `FruitAtDawn` (the frost and growing back), `PlantsCovered` in `Derive` (FORAGE_GONE), `PickFrom`. Saved as the
  economy's `plants` ([prop, fruit, regrow] for those not ripe and full) and hashed with the props. Woodcutters pass
  a standing fruit tree by unless it is in the way (`WoodNear`).
- **The picker**: `WORKER_JOB_PICKER` (7), `picker_places` set in `Derive` after the settlements, filled in
  `AssignJobs` after the water carrier by husbandry (the camp's own places now one table, `camp_places`). The round in
  `TickWorker`: `GoPick` (the nearest ripe plant of the settlement, others' targets passed over), `pick_on` (the next
  within `FORAGE_ROUND_REACH`, else to the camp's supplies). Jobs overview row PICKERS, a berries badge, card lines.
- **By hand**: `Economy::Forage` (`CHASM_CMD_FORAGE`), `BankPlot`, `EconomyWorker::forage` (saved, hashed); the units
  on the post in `TickWorker`'s `WORKER_ON_POST` (`ForageWant`, `ForageUnit`, `ForageUnitTicks`). Any order, release,
  pitch or settling clears it. The right click's menu: `ForageMenu`, `DrawForageMenu`, `OVERLAY_HIT_FORAGE`; the
  right click's sending is `SendSelected` now, shared with the menu. `chasm_economy` op forage and op plants;
  `chasm_select` names the chips `forage`.
- **The meshes**: `bush_a_fruit`, `bush_b_fruit`, `tree_oak_a/b/c_fruit` (15-80 triangles, PAL_ACCENT) built by
  `blender_chasm_props.py --add` (`plant_fruit`): berries on a bush's upward faces, apples on an oak's outer ones, in
  the plant's own space (`keep_origin`, `place_at`). **bush_b's own five berries were moved into `bush_b_fruit`**,
  so a picked bush_b is bare. Drawn as one more set per chunk for each kind (`forest_fruit_names`, `ForestFruitMesh`,
  `ForestFruitShows`).
- **The GLB was merged, not re-exported.** Exporting the .blend - with the "Export" collection's exporter or with the
  settings File > Export remembers - drops the villagers' **Idle** clip: in the saved file Idle has only a fake
  user and is on no armature, so the committed GLB was exported from an earlier state of the session. Only the six
  changed meshes were exported and merged into the committed GLB; its clips are byte for byte as they were. The
  next export from Blender needs Idle on an armature again. `--add` now exports with the remembered settings
  (which sample the clips, as the file's own exports do) and leaves the file's images alone.

*Checked* on a copied exe on 8774, the default seed, from a new colony:
- **The picker**: Anouk (husbandry 10) took the camp's place from the first tick; picked three bushes to a load of 5,
  carried it into the supplies, and the camp ate it first (5 to 0 within a few hundred ticks, the other foods
  untouched). Jobs: picker 1 of 1.
- **By hand**: Jelle sent to a bush with 2 fruit - "going to pick fruit", picked both (one eaten from his pack at
  once, as fruit goes first), stood posted; the bush counted a day toward growing back at the next dawn. Sent to
  the river beside the camp, he went to the bank plot beside it and drew a unit at a time up to 6. Two sent as a
  group went to two different ripe bushes near the one clicked. A plot with no plant: "no ripe fruit there".
- **Built over**: a house painted on a bush's plot made it gone (fruit -1), and erasing the house left it gone.
- **The frost**: stepped over winter's first dawn, every plant's fruit went, 33 to 0.
- **The menu**: a right click on a ripe bush with Jelle selected opened *Pick fruit* / *Post here*; pressing *Pick
  fruit* sent him, the card saying "Going to pick fruit", and closed it. On the river, *Draw water* alone. On open
  ground, an order as before, no menu.
- **Seen**: a fruit tree's apples and the bushes' berries at a close zoom; the picker's berries badge.
- **Replay**: a 4,200-tick recording from a state with plants picked and a forage under way - a group's picking, a
  draw, a release - replayed twice: every tick equal, ending where the run had.
