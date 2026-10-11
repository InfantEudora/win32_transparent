# Settlements: territories, and later new camps and goods between them

The user's, 2026-10-11. A tent placed far from the camp was built by a villager who walked all the way over from
the camp: nothing limits how far anyone goes for a task. And the game is heading for several settlements - one on
the rim and over the edge for floatstone, perhaps one on the mountain side for stone and coal, and across the chasm
one or two on the way to the desert and the pyramid (`gameplay_plan.md`). Talked through the same day.

**What happens today**: an idle person takes the building-site errand with the shortest straight-line walk,
anywhere on the map; a job goes by skill less the walk from home (`ECONOMY_SKILL_DISTANCE` a skill point), no cap;
only the woodcutter has a reach (`ECONOMY_WOODCUTTER_REACH`, from his hut). A painted camp is more tents for families
waiting on a house, not a base of its own.

## Decided (the user, 2026-10-11)

- **Settlements, each with a territory**; people take tasks only in their own.
- **Distance is over the map** - walking - not the straight line: everything is built on the map. Shown as a
  highlight on the terrain or an outline, **as The Settlers draws its borders**; where two settlements come close,
  the border falls between them - **a Voronoi look**.
- **About 50 units** of reach.
- **How long a walk is, shown with a person selected.**
- **Founding a camp** comes once the home settlement is up and running: **packing up a tent**; an expedition of
  **at least two or three**, one a **woodcutter who can gather wood without a house**. It will shape the start of
  the game too. To think through - later.
- **Porters are a job of their own**, maybe with **a building** where what goes where, and how much, is managed.
  To think through - later.
- **Step 1 can be built**; the rest needs more thought first.

## Step 1: territories (proposed)

- **A camp founds a settlement**; the start camp founds the first. Camps in the order placed: one outside every
  earlier settlement's territory founds its own, one inside is more tents for that settlement. A settlement is
  named by its camp's id.
- **Its territory**: every plot within `ECONOMY_SETTLEMENT_REACH` (50) of walking from its camp, over the MAP - the
  terrain's own walking rules (`Walkers::EdgeOpen`): not across water but on a standing bridge, not up or down a
  cliff but by a winch, not onto the mountain, nothing too steep - and **not what is built on it**, so a house
  going up never moves a border. Water and mountain next to it are in, as the land's edge, but lead nowhere.
- **The border between two** falls where the walks from their camps are equal: each plot to the nearer.
- **What belongs to one**: a building, field, garden, lot, road, wall or crate, by the plot it is on (a building's
  door); a person, by their home.
- **The limit**: people take tasks only in their own settlement - carrying for sites and line work, a job, fetching
  for home, a load's store, the builders' errands (towers, provisions, crates), a ropemaker's hemp. Families move only
  into houses of their own settlement; land goes only to a house or camp of the field's.
- **Not limited**: a post and its supply trips (an order is the player's), and a pressing need's nearest supply.
- **Placing**: in play, nothing new outside every territory - refused "outside the settlement", as "under the
  clouds" is, and the ghost shows it red; a bridge needs its near end inside. Erasing is never refused. Debug
  placement is free, as ever - which is how a second camp can be placed now, before founding exists.
- **Drawn**: border posts on the terrain where a territory ends or two meet, as The Settlers marks them.
- **The walk shown**: with a person selected, the plot under the cursor says how far it is to walk there.
- **Nothing saved**: territories follow from the map and the camps.

## As built: step 1 (2026-10-11)

As proposed above, with these details:

- **The flood** (`Walkers::MapFlood`): Dijkstra over the grid's edges by their length, from all of a camp's plots,
  out to `ECONOMY_SETTLEMENT_REACH`; an edge across a cliff only where a winch links it, none steeper than
  `WALKER_STEEPEST`, and onward only from dry ground or a standing bridge, never the mountain. Buildings, walls and
  gates are not asked. `Economy::Settle` runs it per founding camp at the end of `Derive` - on every change of the
  zones - each plot to the camp it was reached from soonest (the earlier camp on a tie): `EconomyState::settlement`
  by plot, `settlement_of` by building (its door, `home_plot`). Fast: about 1,600 plots for a settlement.
- **Which camps found**: those that stand, in id order, whose door is outside every earlier territory.
- **The limit** (`Economy::Ours`, building, crate or ground site): `FindSiteWork` (sites and sources), `LineJobs`,
  `MoveCrate`, `KeepTowers`, `MakeProvisions`, `MakeRope`, `FetchFrom` (fetching home, a load home after a meal),
  `FetchHome`'s crates, `FindStore`, and every "does a store take it" check that pairs with one. `AssignJobs` counts
  people per settlement and fills a workplace from its own; `HouseFamilies` moves a family, a lodger and a new couple
  only within one; `SettleLand` gives land only within one. Needs and posts are untouched.
- **Placing**: `EconomyCommandInTerritory` for the ops that place something (paint, add, field, ground, bridge,
  range), held in the zone command handler for play commands - "outside the settlement" - the ghost (`GhostKey::
  f_settled`) and a road or wall line per plot (`LinePlotAllowed`).
- **Drawn** (`ApplicationChasmBorders.cpp`): a post 0.85 tall with a terracotta flag (`PAL_ROOF`) at the middle of
  every grid edge whose plots differ in settlement, on one ground - not on a cliff edge - rebuilt when the economy's
  `zones_version` moves. `chasm_economy` reports `settlements` (id, plots, people), a worker's and a building's.
- **The walk** (`DrawWalkTip`): with a person selected and the cursor on the ground - not over a panel - "37 units to
  walk", from his straight walk where he would take one, else a route the render thread plans with a `Walkers` of
  its own; amber with "another settlement" or "outside the settlement", "No way there" for none. Worked out again only
  when he or the cursor moves to another plot, or the zones change.

**Checked** (a copied exe on 8774, the default seed): one settlement at the start, 1,585 plots, all 12 people in it.
A villager sent to the rim to explore it; in play a store 70 units out - "outside the settlement"; one by the camp -
a site, built. A debug camp 70 units off founded a second settlement (1,268 plots), the first giving up the 5 plots
nearer the new camp; a store site placed in it waited 3,000 ticks with nobody coming, while one at home was built.
The posts seen in play: along the meeting line, bending round a river's end. The walk label: "16 units to walk"
near home, "85 units to walk - another settlement" in amber at the far site, nothing over the card. A 6,303-tick
recording of it replayed twice: every tick equal, the same two settlements and the far site still waiting.

**Seen, left as they are**: the border is under the clouds where nobody has been, so at the start only a little
of it shows; in the debug view, from far out, the posts are specks. A person in a settlement with no camp of its own
standing - none today - matches only land outside every territory.

## Later, to think through

- **Founding by expedition**: packing up a tent; two or three people, one a woodcutter working without a house;
  supplies carried (the scout's pack, `defence_plan.md`). How it changes the start.
- **Porters**: a job, maybe a building, deciding what goods go where and how many - between settlements, and later
  by winch and zeppelin.
- **Per settlement**: kept stock and provisions (today the colony's), the top bar, NEEDS ATTENTION.
- **People moving** between settlements: the player's, not the game's.
