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

## Founding: taking a tent

Talked through with the user on 2026-10-11, after step 1. The other half - a woodcutter working from a camp, at a
lower rate than from a hut - is `woodcutting_plan.md` ("the camp's two places"), with another agent.

### Founding: decided

- **A tent goes with you** (the agent's, the user: "great"). Taking one is cheaper than making one, and a camp is
  what lets a woodcutter work with no hut. A camp is one person to a tent, so each member of an expedition carries
  their own as their load, with water and food in their pack (the scout's, `defence_plan.md`): three people, three
  tents, a camp of three plots.
  - **Take a tent**, a toggle on the person's card beside *gather supplies*, and the same stop at the camp: they
    pack a tent nobody sleeps in - its plot leaves the camp - and fill their pack.
  - **Pitch**, on their card where they stand: the tent goes up on that plot, or beside one already pitched there,
    and is their home from then on.
  - No woodcutter to pick: since step 1 a settlement fills its places from its own people, so the camp's
    woodcutter's place goes to one of the three.
- **A tent is just a house.** Any home - a tent or a house - keeps its settlement; a settlement vanishes only when
  the last thing in it that can be lived in does. Three people who pitched three tents, built a house and erased
  the tents: they cram into the house.
- **Pitching in another settlement's area** makes it your home there, and you are given a house when one has room,
  as anyone in a camp is. The tent can be packed up again and the move repeated. An outpost of tents alone vanishes
  once they are all packed.
- **Water from the river's edge**: a camp set up near a river draws its own water. Villagers can't walk the wet
  bank, so it is drawn standing beside it.
- **The camp's three places**: woodcutter, farmer, water - the start camp's too, so the mechanic is tested from
  the first minute of every game.
- **Foraging** - food from the wild - is just slow. An item of its own, to talk through later.
- **Making a tent** stays as it is - a camp painted from the build options - but costs rope too: **at least 1 a
  tent**.

### Founding: proposed, not yet answered

- **The area by beds.** A settlement's reach grows with its beds - tents, and houses' room - from about 20 units
  for one tent up to `ECONOMY_SETTLEMENT_REACH`. Beds rather than tents, so families leaving tents for houses, or the
  empty tents cleared after, never shrink it. Not by building on the edge: a chain of buildings would walk the
  border across the map and bring back the long walks step 1 stopped, and the border would move with every house.
- **The house place of someone leaving with a tent** (the user's question): kept until the tent is pitched, so an
  expedition turned back walks home to its bed. Pitching gives it up: the tent is their home, the place goes to
  whoever needs one, and they live apart from their family from then on - a household of their own, as a new couple
  founds one. Open: a partner left behind.

### Founding: worked out here, for the user to overrule

- **The territory's centre**: with no camp needed, the flood starts from the settlement's oldest home that stands
  (the lowest id). Erase the first camp and the next home becomes the centre - inside the old territory, so the
  border shifts rather than leaps. Still nothing saved.
- **Which homes found**: any that stands outside every earlier territory. In play only a pitched tent can be:
  placing outside is refused, as now.
- **Cramming**: today someone whose home goes walks back to the start camp's spot and belongs to no settlement
  (`HouseFamilies`, `HomePlot`'s `camp_plot`). Instead someone with no home takes a place in a house of their own
  settlement, over its room if need be, and moves on as now when a house with room comes free. Crowding does
  nothing else yet.
- **A pitched tent is a tent**: `ZoneCampFire` makes about one plot in 5 a fire, by the plot's hash; a tent pitched
  on such a plot must still be a bed.
- **Carried**: the tent is its carrier's load until pitched; released from the post, they keep it.
- **The camp's water**: its place walks to the nearest dry plot beside a wet bank within reach of the camp, draws
  water there, slower than a collector, and carries it to the camp's supplies, up to a cap as the camp's woodpile
  is. Enough for three, not for twelve, so the start still wants a collector soon. Nothing under snow.
- **Overlap**: the camp's places are the code the woodcutting plan is changing (`ECONOMY_CAMP_WOODCUTTERS`,
  `ECONOMY_CAMP_FARMERS`), so these steps come after its steps 1 and 2 are in.

### Founding: steps

Each its own, checked in the game, with the start playable after every one.

1. **Homes keep a settlement**: the centre from the oldest home, any home founds, and the homeless cram into a
   house of their own settlement.
2. **The camp's water place**, the start camp's included.
3. **A tent costs rope.**
4. **Take a tent and pitch**: the card's toggle, the stop at the camp, the pitch chip, the house place given up.
5. **The area by beds**, if agreed.

## Later, to think through

- **Foraging**: food from the wild, slow (the user's, above) - what, where, and who gathers it.
- **Porters**: a job, maybe a building, deciding what goods go where and how many - between settlements, and later
  by winch and zeppelin.
- **Per settlement**: kept stock and provisions (today the colony's), the top bar, NEEDS ATTENTION.
- **People moving** between settlements: the player's, not the game's.
