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
  (Since 2026-10-11 a pressing need is limited after all: its own settlement or unclaimed land - "Founding:
  decided", "In need, only their own".)
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
standing - none today - matches only land outside every territory. (Since founding step 1, any home founds one,
and a person with no home is crammed into one: see "Founding: as built, step 1".)

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
- **A tent is a tent**, never a chance of being a campfire (the user tried erasing every tent and fire: everyone
  went to one spot and the settlement was gone). The fires can be props around the tents, but don't take their
  place.
- **Erase the camp and the oldest house is the centre**; with no house either, the settlement is gone, as now.
- **The area by beds** (the agent's, the user: "a good idea, with the proposed numbers"). A settlement's reach
  grows with its beds - tents, and houses' room - from 20 units for one tent up to `ECONOMY_SETTLEMENT_REACH`. Beds
  rather than tents, so families leaving tents for houses, or the empty tents cleared after, never shrink it. Not by
  building on the edge: a chain of buildings would walk the border across the map and bring back the long walks
  step 1 stopped, and the border would move with every house.
- **The house place of someone leaving with a tent** (the user's question, the agent's answer, agreed): kept until
  the tent is pitched, so an expedition turned back walks home to its bed. Pitching gives it up: the tent is their
  home, the place goes to whoever needs one, and they live apart from their family from then on - a household of
  their own, as a new couple founds one.
- **Settling without a tent** (the user's, 2026-10-11): someone can move over to another settlement without
  bringing a tent. In place of Pitch, their card reads **Settle here** - only for someone posted onto the ground of
  that settlement, and from the moment they are inside its borders. Never refused for want of a bed (the user's,
  overruling the agent's first default, which refused with "no free bed here"): a tent nobody sleeps in or an
  empty house if there is one; else they squeeze in - "a new person settling in a camp with 2 tents should be
  homeless but belong to that settlement" - crammed into the home there with the most room to spare.
- **In need, only their own** (the user's, 2026-10-11): water and food when a need presses come from their own
  settlement or unclaimed land outside every settlement, never another village. This replaces step 1's "not
  limited" for a pressing need's nearest supply, and holds for someone posted and a scout gathering too.
- **A camp's goods are left as a crate** (the agent's suggestion, the user: "good to pick up"): when its last tent is
  packed or erased, its supplies and pantry lie where it stood - an outpost packed up loses nothing.

- **A partner left behind: they rejoin** (the user's, 2026-10-11: "they should for sure rejoin when their partner
  settles in"). Partners living in different settlements stay partners, with no children while apart - births want
  both in one house, as ever. Whoever then pitches or settles in the settlement where the other lives joins the
  other's household and home, whatever its room (as a birth grows a family past it); a pitched tent stands spare.
- **A lone house takes a partner** (the user's, 2026-10-11, testing three settlers of three families in tents: "only
  Dirk moves in, while at least they should form a couple"). Couples formed only into a house still empty, and the
  homeless are housed first, so the house went to one of them alone and no couple could form. Now one who lives
  alone in a house that holds two pairs off as before and the other moves in there.

### Founding: still open

- **Children left behind**: if the last grown-up of a family leaves - by tent or by settling - the children stay
  in the old home with nobody to fetch for them, and a baby with no mother. The agent's suggestion: the children go
  with the last grown-up; or leaving is refused while they would be left alone.

### Founding: worked out here, for the user to overrule

- **Carried**: the tent is its carrier's load until pitched; released from the post, they keep it (built, step 4).
- **The camp's water**: its place walks to the nearest dry plot beside a wet bank within reach of the camp, draws
  water there, slower than a collector, and carries it to the camp's supplies, up to a cap as the camp's woodpile
  is. Enough for three, not for twelve, so the start still wants a collector soon. Nothing under snow (built, step 2).
- **Overlap**: the camp's places are the code the woodcutting plan is changing (`ECONOMY_CAMP_WOODCUTTERS`,
  `ECONOMY_CAMP_FARMERS`), so these steps come after its steps 1 and 2 are in.

### Founding: steps

Each its own, checked in the game, with the start playable after every one.

1. **Homes keep a settlement** - BUILT, below: a tent is always a tent, any home founds, the oldest the centre, the
   homeless crammed in, and the area by beds (the old step 5, since it is worked out in the same place).
2. **The camp's water place**, the start camp's included - BUILT, below.
3. **A tent costs rope** - BUILT, below.
4. **Take a tent and pitch** - BUILT, below: the card's toggle, the stop at the camp, the pitch chip, the house
   place given up.
5. **Settle here**, without a tent - BUILT, below.
6. **A camp's goods left as a crate** - BUILT, below.

### Founding: as built, step 1

Built 2026-10-11:
- **A tent is a tent.** Every camp plot is one, and a bed (`ZoneCampTents`, the camp's `tents_of`), so the start
  camp is 12 plots for 12 settlers where it was about 15. Its fires are props: `ZoneCampFireTent` gives a fine cell
  a fire when one corner is a standing tent and the other three are open ground - no building, garden, lot, road or
  wall, no field, not wet, not the mountain, one terrace step - one such cell in `ZONE_CAMP_FIRE_EVERY` by the
  cell's hash. It burns past the cell's middle from the tent, with a log to sit on beyond it. So fires ring a camp's
  edge, and a tent hemmed in by tents has none. The cell draws it (`BuildCampFire` from `BuildHouseCell`), not the
  tent, so a building put on any of its corners redraws it away. The hearth's smoke rises from it.
- **Homes found** (`Economy::Settle`): camps and houses that stand, in id order, so the oldest first. One outside
  every earlier territory founds its own, named by its id, and floods out to `ECONOMY_SETTLEMENT_REACH`.
  - **Its reach**: `EconomySettlementReach` of its beds - `ECONOMY_SETTLEMENT_FIRST` (20) for one, then
    `ECONOMY_SETTLEMENT_PER_BED` (5) each more, so 7 beds reach the full 50. Its beds are the capacity of the homes
    whose doors lie within the reach and nearer it than any earlier settlement. The reach starts at the most and
    narrows while the beds inside hold up less, so it is the widest one its own beds support. A later settlement
    taking land nearer to it doesn't recount an earlier one's beds.
  - `EconomyState::settlements` lists each (id, beds, reach); `chasm_economy` reports them.
- **Crammed** (`HouseFamilies`):
  - **Finding a home**: a family with no home looks for one in the settlement it stands in - the one its home was
    in - first an empty house that holds them, then a camp with room. Standing in none, it looks anywhere, as before.
  - **Squeezing in**: still with nowhere, it squeezes into a house or camp of that settlement, over its room if need
    be: the one with the most room to spare that they can walk to.
  - **Moving out**: families sharing a house past its room wait for an empty house that holds them, the largest
    first, and stop waiting once the house is no longer over its room. A family alone in a house that has outgrown
    it is left as it was.
  - **No home left** in the settlement means no settlement: they wait at the start camp's spot, as before.

**Checked** in a copied exe on 8774, the default seed:
- **At the start**: one settlement, 12 beds, reach 50, 1,564 plots, all 12 people in it. In the game, all 12 camp
  plots are tents, with two fires at the camp's edge.
- **House A** (6 beds) by debug: the De Wits (5) moved in, and the settlement had 18 beds.
- **Every tent erased**: all 12 people crammed into house A. The settlement became the house's (id 2): 6 beds,
  reach 45, 1,233 plots.
- **House B**: the De Wits moved out into it, and the Wevers and Veens (7) stayed in A.
- **Every home erased**: no settlement, everyone homeless at the camp's spot.
- **A lone tent 70 units off**: a settlement of 1 bed, reach 20, 260 plots.
- **Three tents beside the last house** (3 beds): 4, then 6 beds, reach 35, then 45, the house still its centre.
- **Replay**: a 1,027-tick recording replayed twice, every tick equal, ending as the run did.

### Founding: as built, step 4

Built 2026-10-11, after step 1 (steps 2 and 3 wait for the woodcutting plan's camp steps):
- **Take a tent** (`Economy::TakeTent`, `CHASM_CMD_TENT`): a chip on a posted person's card under Gather supplies first,
  lit while it is on, and "Take a tent each" on a group's card. On the way to their post, or standing on it, they go
  first to the nearest camp with a tent to spare (`GoForTent`) - before any gathering - and their card says "going for
  a tent", then "packing a tent". There they eat and drink, fill their pack as on a gathering stop, and pack the tent
  (`TentToPack`).
  - **A tent to spare**: the camp's tents, less those packed already this tick, those others are on their way to
    pack, and those slept in by its people. A resident away carrying a tent doesn't sleep there, and one on their
    way for a tent counts once. So a person's own tent is always one to spare for them.
  - **Which tent**: the one at the camp's edge (fewest of its tents beside it), so the camp stays one piece.
  - **The plot**: out of its camp on the same tick, as a finished storey goes up (`TentsPacked`, applied as
    `ZONE_OP_BUILD_ERASE` in `TickEconomy`).
  - **None to spare** anywhere they can get to: on as they were, and "NO TENT TO SPARE FOR NAME TO TAKE" (`NOTICE_NO_TENT`).
- **Carried** (`ECONOMY_TENT_CARRIED`): drawn as a canvas roll with its poles across their back (`LOAD_TENT`, the
  fifth load, `WORKER_LOADS` now in ApplicationChasm.h). Released, they keep it, and keep their house place.
- **Pitch** (`CHASM_CMD_PITCH`): "Pitch the tent here" on the card of anyone carrying one, posted or not, and on a
  group's card for all of them, in the order selected.
  - **Where** (`Economy::PitchSpot`): the plot they stand on, or the nearest within `ECONOMY_PITCH_RINGS` on their
    terrace step. It must be a plot with nothing built on it that a camp may take, with no crate and no standing
    tree. Beside a pitched tent it joins that camp, so a group pitching together makes one camp.
  - **No territory check**: outside every settlement is what it is for.
  - **Putting it up** (`Zones::PitchTent`): a camp plot, standing at once, with the stroke in progress kept.
  - **Moving in** (`Economy::Pitched`): the zones' change is followed straight away (`FollowZones`, split out of
    `Tick`), and the carrier moves in. Their house place goes (a new family number when others share theirs); the
    order is over and they are home; and they're offered an empty house, as anyone in a camp is.
  - **Refused**, as an order is: "no tent to pitch", or "no room for a tent here: ..." with the rules' reason.
- **On the road**: a family of nothing but tent carriers, with no home, is given none (`HouseFamilies`). It pitches
  its own.
- Saved, hashed, and in `chasm_economy`: a worker's `tent`; ops `take_tent` and `pitch`; the `no_tent` notice.

**Checked** in a copied exe on 8774, the default seed:
- **Refused**: "not sent out" (take a tent, not posted) and "no tent to pitch".
- **Setting out**: three settlers (1, 3, 6) were ordered 70 units out, each told to take a tent and gather. All
  three went to the start camp, packed one each (12 tents down to 9), filled their packs and walked out.
- **Pitching**: all three pitched on arrival. One camp of 3 tents founded settlement 2 (3 beds, reach 30, 562
  plots). Each became their own household (families 4, 5, 6), released and home; the start settlement went to 9
  people. Person 1 took the camp's woodcutter place (`woodcutting_plan.md`) and went felling.
- **Moving on**: person 6 took their own tent again from the outpost. The outpost dropped to 2 beds (reach 25) while
  they still lived there. They pitched 12 units off, a camp of their own in settlement 2, and took its woodcutter
  place.
- **Seen**: the roll on a carrier's back, the card's "Carrying a tent" and its pitch chip, and the outpost's tents
  and border posts in play.
- **Replay**: a 5,805-tick recording replayed twice, every tick equal.
- **Two bugs** found on the way and fixed:
  - two people packing on one tick took the same tent, now counted;
  - pitching onto the plot a tent had just gone up on did nothing, silently; now only empty plots are tried, and
    a failed pitch always says why.

**Seen, left as it is**: tents pitched apart are separate camps, each with its own woodcutter place (the woodcutting
plan's), so three people pitching apart all become woodcutters; pitched together, one camp, one woodcutter.

### Founding: as built, steps 2, 3, 5 and 6

Built 2026-10-11:
- **The camp's water place** (step 2).
  - **Where**: a camp with a river's or pool's edge within `ECONOMY_CAMP_WATER_REACH` of walking from its door has
    `ECONOMY_CAMP_WATER_CARRIERS` place (`drawer_places`). Its spot is `draw_plot`: the dry plot beside a wet one
    nearest by the walk, off the mountain, with nothing built on it but a road or the camp's tents (`Derive`,
    `Walkers::MapFlood`).
  - **Who**: filled in `AssignJobs` right after the camp's woodcutter and the same way - of those living there, the
    one it has, the free, then one from a later place - the most ingenious first, as a collector's carrier is.
  - **The work**: a `WORKER_JOB_WATER` at a camp. While its supplies hold less than `ECONOMY_CAMP_WATER_HOLD` and
    no snow lies at the spot, he walks there ("going to draw water"), draws `ECONOMY_CAMP_WATER_LOAD` at
    `ECONOMY_CAMP_WATER_SECONDS` a unit ("drawing water"), and carries the load home into the camp's supplies,
    which take it whole ("carrying water to the camp"). A collector's pace is untouched; the camp's is a quarter of
    it, about 2.5 units a day when the walk is short.
  - **The start camp has one too**, from the first tick. Its 40 water sits over the 12-unit hold, so he idles
    until the settlers have drunk it down. `chasm_economy` shows a camp's `draws_at`.
- **A tent costs rope** (step 3): `ECONOMY_TENT_ROPE` (1) a camp plot-storey, through `StoreyGoods` as a winch's
  rope, so the price tag, the site and the builders' errands all have it. One placed by debug, the start camp, and
  a pitched tent cost nothing.
- **Settle here** (step 5): `Economy::SettleHere`, `CHASM_CMD_SETTLE`, "Settle here" on the card of someone posted
  onto another settlement's ground, inside its borders now and carrying no tent (`CanSettleHere`; on a group's
  card for those of them who may).
  - **A bed**: the nearest camp of it with a bed to spare (`CampSpare`, the count taking a tent uses), else the
    nearest empty house of it.
  - **Moving in**: their house place goes (`OwnHousehold`, shared with pitching: a new family number when others
    share theirs), the order ends, and they go home to the new bed.
  - **Refused**, as an order is: "not sent to another settlement", "not there yet", "no free bed here".
- **A camp's goods as a crate** (step 6): `Derive` keeps the last Derive's doors and which buildings were camps - the
  zones give a gone building no kind - and a camp gone leaves its supplies and pantry as one crate at its door
  (`CratePlotNear`, the search `DropLoad` uses, split out of it), of origin `CRATE_CAMP`: "A camp's goods", "Left
  where a camp stood". A store pulled down still loses what it had.

**Checked** in a copied exe on 8774, the default seed:
- **The start's jobs**: a water carrier (2) and a woodcutter (3) at the camp from the first tick.
- **Drawing**: with the camp's water set to 2, he walked to the bank in 600 ticks, drew for 2,400, and carried 3
  units home; once unloaded, out again.
- **Rope**: a camp painted in play was a site needing 1 rope and 6 wood.
- **An outpost** pitched by three had a draw plot 15 units off. Its own carrier had drawn 3 water by the time it
  was erased.
- **Settling**: Dirk, sent there, was refused "not there yet" on the way and "no free bed here" once there (3 tents,
  3 people). A tent added by debug, he settled: home in the outpost, a household of his own, 8 and 4 people.
- **Seen on the card**: the chip came up once he was inside the border, still walking to his post.
- **The crate**: the outpost erased left one crate at its door, "camp", with 11 beans, 18 water and 6 wood - its
  supplies and the pantry its people had unpacked into.
- **Replay**: a 14,484-tick recording replayed twice, every tick equal.
- **Changed the same evening** (the user's, after trying it):
  - **Settle here squeezes in**: with no bed free it is not refused - he is crammed into the home there with the
    most room to spare, the nearest on a tie, and belongs to that settlement.
  - **A pressing need only from his own**: `SeekSupplyOf` passes over any source - building or crate - in another
    settlement's land; his own settlement's and unclaimed land's only. Gathering a scout's pack goes the same way.
  - *Checked* (copied exe, 8774): Dirk settled into an outpost of 3 tents and 3 people - 4 living there now.
    Anouk of the home village, posted at the outpost with no water packed and her thirst pressing, set off on the
    93 units home to her own camp, not to the outpost's pantry beside her; Siem of the outpost drank there at once.
    A 4,854-tick recording replayed twice, every tick equal.
- **Two slips on the way**:
  - a gone camp was looked for by its kind, which the zones clear - now by what it was at the last Derive;
  - the first test run set the start camp's water to 2 and left it, and people died of thirst, which freed a bed.

### Founding: as built, partners and the hut (2026-10-11)

- **Rejoining**: `PartnerLivingIn` - the partner, if they have a home in that settlement and are not leaving it with
  a tent. `Pitched` and `SettleHere` then take the partner's family and home in place of `OwnHousehold` and a bed.
- **A lone house**: `HouseFamilies`' COUPLES takes, besides the empty houses, a house where one unpaired adult
  lives alone and that holds `LIFE_COUPLE_ROOM`; for a pair, the one living alone's house first (the elder's if
  both do), else the smallest empty one as before. Nobody taking or carrying a tent is offered a partner.
- **A hut takes over** (the user's, the same test: "the woodcutter house creates an extra woodcutter, leaving no
  job for building"): a camp has no woodcutter's place of its own while a woodcutter's hut stands in its
  settlement (`woodcutting_plan.md`). Worked out after `Settle`, so the trees out of every woodcutter's reach are
  now counted after it too.
- *Checked* on `saves/camp_3.json` (copied exe, 8774): with the outpost's hut standing, Marit lost the camp's
  woodcutter place at the zones' next change. A load alone restaffs nothing, so a replay keeps to the run it was cut
  from. A debug house on a plot that holds two: Dirk moved in, and Bente, the elder of the two women, moved in with
  him as his partner. Marit stayed in her tent and took the water, and Bente, housed, was free to build. Siem of the
  home village pitched at the outpost, a household of his own; Anouk, posted after him, settled there and joined him,
  in his tent. Their two children stayed behind alone (still open, above). An 8,457-tick recording replayed twice,
  every tick equal.
- *Seen in the save*: the home village is a camp of 9 tents with no water stored and only the camp's water carrier,
  meant for a few. Three had died of thirst before the save, and Siem and Anouk did on days 13 and 14 of a run
  left alone - the test topped its water up.
- *The house the user placed* held one, most likely: a plot of area 2.2 holds 1 (`ECONOMY_PERSON_AREA`), one of 2.7
  holds 2. Then no couple fits, and Dirk alone is right.
- **A bridge nobody built** (the user's, the next day, the same save): its site stood "waiting for someone free to
  carry" with Marit idle beside it. A building is in the settlement of its door, and a bridge's door is its lowest
  end that can be walked to - here the far one, on land no settlement holds - so it was nobody's, though placing it
  checks the near end (`EconomyCommandInTerritory`). Now `Settle` gives a bridge whose door lies outside the
  settlement of its other dry end. *Checked*: the bridge the outpost's; Marit carried for it between her trips for
  food, 11 of its 18 plots standing after 8,000 ticks, the found crates' wood going in. An 8,001-tick recording
  replayed twice, every tick equal.

## Growing from every building (the user's, 2026-10-12)

The territory was a walk from the settlement's oldest home alone, its reach by beds: houses beyond the first only
lengthened it, so a village growing one way pushed its border out evenly round the camp, not past its newest houses.
The user: "it should grow with each building, even a guard post or a wall." The agent's proposal, agreed whole:

- **The heart stays**: the founding home reaches 20 to 50 by the settlement's beds - every home of it now, wherever
  its land took it.
- **Every standing building adds land round itself**: `ECONOMY_SETTLEMENT_BUILDING` 15 units of walking (houses,
  stores, huts, collectors, the ropewalk, the builders' hut, winches, bridges - a bridge's far end claims the far
  bank); a guard tower `ECONOMY_SETTLEMENT_TOWER` 30, as military buildings hold land in The Settlers; each built
  wall or gate plot `ECONOMY_SETTLEMENT_WALL` 10, so a wall carries the border with it. A home far from the heart
  adds the building's 15, not the settlement's reach by beds (the user's: that would spread land too fast).
- **Once it stands, not as a site**, so painting sites spreads nothing. **Not fields, ranges, roads, gardens or
  lots**: cheap to lay, they would spread land for next to nothing (the user's: leave them out).
- **It chains**: one standing on land another added adds more in turn - a settlement spreads by building outward.
- **Where two meet** each plot goes to the shorter walk from any of their buildings; the older keeps a tie. A
  building gone takes its land with it; what then stands outside stays, nothing new goes there.

### As built (2026-10-12)

- `Economy::Settle`, in rounds: hearts founded oldest first; after each, the land GROWS in waves - whatever stands
  on a settlement's land joins it (its door's, else any plot of it held: a bridge's far end), and each wave floods
  the newly joined together by settlement and reach (`MapFlood`), until nothing new joins; then the next home left
  outside founds. Beds are counted over the homes joined; a heart whose beds hold up less narrows, and the round is
  worked out again (`ECONOMY_SETTLEMENT_ROUNDS` at most).
- `chasm_pick` reports a plot's `settlement` (0 nobody's), for checking borders.
- *Checked* (the default map, debug, a copied exe): the camp alone, as before - 12 beds, reach 50, 1,564 plots, the
  border 44 units straight south. A store at 42: border at 56, 1,631 plots; a second store on that new land at
  55.6: border at 66, 1,741 - walks along the grid's edges run some 15-20% past the straight line; the first
  erased: back to 44 and 1,564, the second left outside adding nothing. A guard tower at 42: border at 68, 1,918.
  Three wall plots at 40-44: border at 52, 1,595. A store site at 42 in play: still 44, 1,564; raised, 56 and 1,631.
  `camp_3` loaded: both settlements, every building and person of the outpost still in it; a tick 31 ms.

## Later, to think through

- **Foraging**: food from the wild, slow (the user's, above) - what, where, and who gathers it.
- **Porters**: a job, maybe a building, deciding what goods go where and how many - between settlements, and later
  by winch and zeppelin.
- **Per settlement**: kept stock and provisions (today the colony's), the top bar, NEEDS ATTENTION.
- **People moving** between settlements: the player's, not the game's.
