# Woodcutting: who fells a tree, and what a woodcutter's hut is worth

The user's, 2026-10-11. It came out of the price tag (`construction_plan.md`, "What it costs, before it is
placed"): at the start a woodcutter's hut cost 2 wood on a tiny plot and 14 on a large one, and the large one
bought nothing - one woodcutter, a woodpile of `ECONOMY_HUT_PILE` on a lot, whatever its size. And a forest
could be cleared with no woodcutter at all: the idle fell the trees in a planned road's way, each a whole
tree's wood (`line_works_plan.md`), so roads painted everywhere would take it down. Talked through the same day.

## Decided (the user, 2026-10-11)

- **Only a woodcutter fells a tree.** Villagers no longer do: a planned road's or wall's trees wait for one.
  "That makes clearing a forest harder, which makes sense."
- **Building over trees the same.** A building, a field, a garden or a lot painted over trees waits for a
  woodcutter to fell them first - "making sitting on the edge of a forest preferred".
- **The camp employs one farmer and one woodcutter**, filled automatically as every job is. More of either
  takes houses and huts.
- **A hut takes the camp's woodcutter's place over** (the user's, later the same day, testing an outpost of three:
  "the woodcutter house creates an extra woodcutter, leaving no job for building"): while a woodcutter's hut stands
  in a camp's settlement, the camp has no woodcutter of its own. Built and checked: `settlements_plan.md`, "Founding:
  as built, partners and the hut".
- **A log's worth goes by where its woodcutter works** - "a larger shop makes working easier":
  - from the camp, with no hut: **2 wood**;
  - from a hut: **4**, as a tree is worth today (`ECONOMY_WOOD_PER_TREE`);
  - from a hut with a second storey: **5**.
- **The second storey counts when it stands on every plot of the hut**, so a raised corner of a big hut
  does not buy it.
- **A hut keeps wood indoors, by its floor area** - a cheap storehouse for wood. The plot it is painted on
  decides its room, so what a big plot costs buys something. A hut on a tiny plot holds little, and needs a
  lot to pile on.
- **Indoors is a buffer**, as the woodpile is today: what lies there goes on to a store that takes wood,
  first thing on a round. Until there is one, the hut is the store.

## Proposed with it (the agent's, agreed)

- **Trees in the way first.** A woodcutter fells a tree in the way of a planned road or wall before any
  other, within his reach (`ECONOMY_WOODCUTTER_REACH` of his hut - or of the camp). The idle still lay a road
  once its way is clear. A line out of every woodcutter's reach waits, and NEEDS ATTENTION says so.

## Worked out here, for the user to overrule

- **A log's wood is counted when it is taken up**, at the rate of the woodcutter taking it. A woodcutter takes
  up what he fells at once, and nobody else fells any more, so a log is left lying only when he is called away
  between the two; it needs no worth of its own, and no new saved state.
- **He fells only with somewhere to put the wood**: a store with room, his hut's room or its lot, as today -
  a tree in the way too, so no log is left lying where a site will stand. A hut full with no store to empty
  into holds up its lines and sites, and NEEDS ATTENTION says so.
- **The camp's wood has a cap** (`ECONOMY_CAMP_WOODPILE`, tuning). A camp's supplies hold any amount, so
  without one its woodcutter would fell everything within reach. He fells while they hold less wood than
  that, and a store that takes wood gets his loads first, as from a hut.
- **The lot adds room**: a hut's room is what it holds indoors and what its yard's standing lot plots hold,
  each by its area. That is the yards plan's "a woodpile that grows with the yard" (`yards_plan.md`,
  "Noted"), answered by area. A hut with room indoors and no lot works; NEEDS ATTENTION's NO LOT TO PILE ON
  becomes a hut with no room for a log.
- **A site over trees** is carried for as before and rises only once its trees are down. Its trees stand,
  drawn, until felled; their stumps then go under it, as now. The price tag counts them ("COSTS 8 WOOD -
  3 TREES TO FELL"), so the forest's edge shows its worth before the click. A hut painted in a forest waits for
  the camp's woodcutter - out of his reach, for nobody.
- **A woodcutter's order**: what waits at his workplace, as now; then a tree in the way of a site or a line;
  then a log lying; then the nearest tree.
- **The debug panel** places things standing, as ever, over its trees as now: the rule is play's.

## Steps

Each its own, checked in the game, the start playable after every one.

1. **A log's worth by the workplace** (`EconomyLogWood`): a hut's 4, a hut two storeys high on every plot 5;
   the camp's 2 for step 2. The hut's card says what its logs are worth, and what a second storey would make.
2. **The camp's two places**: its land's farmers capped at one, and a woodcutter of its own, felling within
   reach of the camp into its supplies, up to `ECONOMY_CAMP_WOODPILE`.
3. **The hut's room**: indoors by floor area, and its lot plots by area; the lot no longer needed; the
   NEEDS ATTENTION row.
4. **Only woodcutters fell**: the trees in a road's or wall's way to the woodcutters, first; the idle only
   lay; a row for a line waiting on trees nobody can reach.
5. **Sites over trees**: buildings, fields, gardens and lots wait for theirs; the trees drawn until felled; the
   price tag's count; a row for a site waiting on trees.

Then the start's balance by a run - `ECONOMY_START_WOOD` and the camp's cap - since its first wood now comes
at 2 a tree.

## As built (2026-10-11)

All five steps, the same day, each checked in the game before the next (debug, a copied exe on 8774, paused and
stepped, the default seed). The start's balance is not looked at yet.

1. **A log's worth** - `EconomyState::log_wood` by building, derived: a hut's `ECONOMY_WOOD_PER_TREE`, one with every
   plot two storeys high `ECONOMY_LOFT_LOG_WOOD`, the camp's `ECONOMY_CAMP_LOG_WOOD`. Taken up at that, and a load
   off the pile is one such log. The hut's card: "A log makes 4 wood - 5 with a second storey on all of it".
   *Checked*: a two-storey hut's pile went 5, 10, 15, 20; a storey taken off, its card said 4 and the rest.
2. **The camp's two places** - its land's shares capped at `ECONOMY_CAMP_FARMERS` (`LandPlaces`: a field past the
   cap has no share, fallow); `cutter_places` (`ECONOMY_CAMP_WOODCUTTERS`), filled in `AssignJobs` after its farmer
   and the same way - those living there, the one it has, the free, then one from a later place, the strongest
   first. He puts his wood in the camp's supplies at its door, up to `ECONOMY_CAMP_WOODPILE`, and never carries
   the supplies off to a store (`FetchFromWorkplace`). `WorkplaceMakes` for a woodcutter is wood alone now.
   *Checked*: on a new map the strongest settler (strength 10) took it and the camp's wood went 42, 44, 46, 48;
   two camp fields, a farmer for one, the other fallow.
3. **The hut's room** - `hut_room` (floor area x `ECONOMY_HUT_ROOM`) plus its yard's standing lot plots by area
   (`ECONOMY_LOT_ROOM`) = `pile_room`, which replaces `ECONOMY_HUT_PILE`. With no lot he puts wood down at the
   hut's door. The pile is drawn for what is past `hut_room`. NEEDS ATTENTION's `no_lot` became `no_room`
   ("WOODCUTTER: NO ROOM FOR WOOD"), for a standing hut that cannot keep a log. *Checked*: a one-storey hut on a
   3.9 plot "Keeps 23 indoors - no lot", working; a lot painted for it, "Keeps 23 indoors, 26 on its lot".
4. **Only woodcutters fell** - `LineJobs` gives the idle only plots to lay; `WoodNear` ranks a tree in a line's way
   (`TreesInTheWay`) first; `Felled` raises a wall waiting on the tree. `WORKER_CLEARING` is no longer entered.
   `trees_unreached` (derived) feeds NEEDS ATTENTION row 12, "TREES IN THE WAY: NO WOODCUTTER NEAR", whose click
   takes up the woodcutter tool. *Checked*: a road of 8 plots through the forest edge - the camp's woodcutter
   felled its trees one by one, the jobless laid each plot as it cleared; a road 45 units west, with a villager
   sent to explore it: the row, "4 PLACES"; a wall through trees went up once cleared and carried for.
5. **Sites over trees** - `EconomyPropCovered` is now what is BUILT (a standing storey, built ground, a built
   cell), and the view's `PropHidden` asks it; `EconomyTreeSite` names the site a tree is in the way of, and
   `WoodNear` ranks those first; `Build` waits while `SiteTreesStanding`, and `Felled` builds the site its tree
   stood on. The cards count the trees ("2 trees to fell first - a woodcutter's work"), the price tag too
   ("COSTS 6 WOOD - 2 TREES TO FELL"; `EconomyTreesOn`, the ghost's key has `felled_version`). *Checked*: a
   house painted on two trees had its 6 wood by tick 1343 and stood at once when the second fell, near 3300.

Not checked by a run: a field or a garden site over trees (the same `Build` path), the `no_room` row (at
`ECONOMY_HUT_ROOM` 6 even the smallest plots keep about a log, so it hardly shows - a question for the user:
whether a hut should need room for more than one log before it works), and a tag's tree count for a road or
wall (left out: their trees are along the line, the economy's `RoadTrees`, not the plot's).

## Retuned (the user, 2026-10-11)

After playing it: "a lot holds... a lot. Too much for the cost", and "the woodcutter's hut only stores wood inside
above a minimum size. A cost 4 woodcutter would start having space indoors."

- **A lot** holds `ECONOMY_LOT_ROOM` 2 per unit of its area (was 7): about a store's room for the wood it costs - a
  store holds `ECONOMY_STORE_ROOM` for `ECONOMY_BUILD_WOOD_PER_AREA`, a lot costs `ECONOMY_GROUND_WOOD_PER_AREA`.
  About 6 on a middling plot.
- **Indoors** only for the floor past `ECONOMY_HUT_WORKSHOP` 1.5, the floor he works in (`EconomyHutRoom`): a hut
  of 3 wood keeps none, one of 4 begins to. The agent's reading: the first 1.5 of floor never keeps anything, so
  room grows from nothing at cost 4 rather than jumping in there whole - for the user to overrule.
- **The price tag** says what a hut would keep: "COSTS 7 WOOD - KEEPS 11 WOOD INDOORS", "TOO SMALL TO KEEP WOOD";
  a storey more or an extension "KEEPS 9 MORE WOOD".
- *Checked*: tags over the start area ran 5 wood -> 3 kept, 6 -> 7-8, 8 -> 13-14, 10 -> 18-20, 14 -> 32; a 5-wood
  hut (keeps 3, under a log) placed: "Too small to keep a log, and no lot beside it", NO ROOM FOR WOOD listed; a
  lot painted for it (4 more): the row gone and its woodcutter out for wood.

## Fixed: the reach is a walk (2026-10-11)

The user saw Bente, the woodcutter of a pitched camp (`saves/camp.json`), go for a tree very far away. It stood
19 units from her camp in a straight line, across a stream. The way to it was 115 units, round the stream's end,
and back again with the log. `ECONOMY_WOODCUTTER_REACH` and "the nearest" were both measured by the straight line.

- **Now by the walk.** `WoodNear` floods the map from where the search starts, out to its reach
  (`Walkers::MapFlood`, the settlements' flood). The map's own ways count: a river only by a bridge, a cliff only by
  a winch, not what is built. Wood is in reach within `ECONOMY_WOODCUTTER_REACH` of walking from the hut's door,
  and the nearest is the nearest walk.
- **The second log** keeps the straight line from the hut, as the first log was well inside it.
- **NEEDS ATTENTION's** "no woodcutter near" (`trees_unreached`, `Derive`) uses the same flood from each woodcutting
  workplace, so it agrees with where woodcutters go.
- **Checked** on the save: after finishing the far trip she was on when it was saved, her next four trees were 21
  to 28 units' walk away, on her side of the stream. A 2,500-tick recording from the save replayed twice, every
  tick equal.

## The collector's hold (the user's, 2026-10-11)

"Apply the same logic to the water collector, size wise": it held `ECONOMY_WATER_HOLD` 10 whatever its size, filled
at one rate, and had one carrier, so a big one cost more for nothing.

- **It holds by its standing floor**, as a hut keeps wood: `ECONOMY_WATER_ROOM` 6 for every unit past
  `ECONOMY_WATER_PUMP` 1.5, the floor its pump takes (`EconomyCollectorRoom`, derived `water_room`).
- **But never under a load** (`ECONOMY_LOAD`) - the agent's, agreed: its carrier comes only for a whole one, and it
  has no lot to spill onto, so a collector holding less would never be emptied. A site holds a load.
- **Its pace is left alone**: one unit every `ECONOMY_WATER_SECONDS`, about 11 a day, enough for some 22 people, so
  water is not what holds a colony back yet. A faster fill for a longer collector along the bank is the other lever.
- **The price tag** says what it would hold: "COSTS 6 WOOD - HOLDS 12 WATER"; an extension "HOLDS 16 MORE WATER".
- *Checked* (the default seed, play mode): tags along the camp's bank ran 4 wood -> 7, 6 -> 11-13, 8 -> 17-18,
  10 -> 23, 12 -> 30; one placed on a 2.7 plot (6 wood) was a load as a site, 12 built, filled to 12 and stopped with
  no store; a store painted, its carrier took a load (12 -> 7) and it filled back to 12.

## Strength (the user's, 2026-10-11)

Looked into, the same day: strength only sped felling (`EconomySkillFactor`, 4.3 s at 10 against 8.8 s at 1),
and a round is mostly walking, so the strongest brought in some 10-20% more wood than the weakest. Yet the job
went by strength at `ECONOMY_SKILL_DISTANCE` 4 units a point, while each unit further from home costs about two of
walking a round: a point saved half a second, the 4 units it bought cost some 5. The user: "both can be tuned a
little ... only change both by about 25%".

- **Carrying by strength**: anyone with a load walks `ECONOMY_CARRY_STRENGTH` faster a point over 5, slower under
  (`Walk`): 0.9 at 1, 1.125 at 10, about 25% between them. Empty-handed, as before.
- **Weighed a quarter less**: a place scores skill at `ECONOMY_SKILL_DISTANCE` 3 a point, was 4. The camp's
  woodcutter is still its strongest, all living there alike.
- First for the woodcutter alone, then for every job the same day - "so they have the same rule" (the user's).
- Worked out, not measured: the strongest woodcutters now bring in some 20-30% more wood a day than the weakest.
  *Checked* (the woodcutter-only build): the camp's woodcutter (strength 10) walked 1.8 loaded, 1.6 empty.

## Later

- The builders' hut and the ropewalk still cost by area for what does not grow with it: places and pace by
  counting plots. A flat cost per plot-storey for those was suggested (2026-10-11), not decided. The woodcutter's
  hut and the water collector now have their area's worth: their room.
- More woodcutters at a bigger hut - suggested, and left for later in favour of the better logs.
- The wall's cost, `ECONOMY_WALL_WOOD`, was "one felled tree's logs" (the user's, 2026-10-08): a figure of its
  own now, to retune if wanted.
