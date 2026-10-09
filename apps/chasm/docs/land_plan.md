# Family land: fields that belong to a house

The user's, from play testing, 2026-10-08. Five one-cell fields painted apart get five farmers. The same
five painted in one go, as a five-cell field, get one, and one farmer could easily work all five. So fields
are to belong to houses, the way a family owns its lots, and each family works its own land. This was talked
through the same day. The decisions below are the user's answers, and the rest was proposed and agreed ("you
are good to go"). BUILT 2026-10-08 - see the end.

## Decided (the user, 2026-10-08)

- **A farmer works an area, not a field** (buildings_plan.md: "a farmer per area"): up to
  `ECONOMY_FARMER_CELLS` coarse cells, 6 for now (tuning), however many fields they are split over.
- **Fields belong to a house and its family farms them.** A house's fields are its LAND. The family member
  best at husbandry farms it, and a second member joins when the land is more than one farmer's worth.
- **Ownership is automatic.** A field gets its owner when it is built: the nearest house that is lived in.
  The player can give it to another house from its card.
- **The camp owns land too**, so fields can be placed and sown from the first minute, before any house
  stands. A field built while no house is lived in is the CAMP'S LAND, farmed by whoever lives in the camp.
  Camp land is never kept: as soon as a house is lived in, the land passes to the nearest one, by the same
  rule a new field follows.
- **Staffing.** A house's land is one workplace whose places only its own family fills. It takes its place
  in the order by when the house was placed: the builders' huts first, as now, then every workplace and
  every house's land by id.

## Proposed

### Who owns what

- **Each field has one owner**, a house or the camp. The economy keeps it with the field's growing record
  (`EconomyField::owner`), saved and hashed. The player's choice belongs to the zones
  (`ZoneBuilding::land_of`, 0 for automatic, set by a recorded zone op), as the crop choices do: the choices
  are the zones', and what follows from them is the economy's (fields_plan.md).
- **The rule**, worked out again whenever the zones or the families change:
  1. A choice naming a house that is lived in: that house.
  2. Otherwise the field keeps its owner while that owner is a lived-in house. Automatic land does not move
     when a nearer house is built, so nobody's farm changes under them.
  3. Otherwise (a new field, the camp's land, or a field whose house was pulled down or left empty): the
     nearest lived-in house that can be walked to, by the walk, with the lower id on a tie. With no such
     house, the nearest camp.
- **A choice whose house is not lived in counts as automatic meanwhile**, and holds again once a family
  moves in. One naming a house that is gone is ignored. Ids are never reused, so a stale choice can never
  come to mean another house.
- **Land belongs to the house, not the family.** When the automatic housing moves a family out to let a
  bigger one in (`HouseFamilies`), the land stays with the house and the new family farms it. Land is part
  of what makes a house's spot good.
- **A field split by an erase** passes its owner and its choice to every piece.

### The farmers

- **A house's land has PLACES.** Its fields are shared out in field order, and each farmer takes whole
  fields until they have `ECONOMY_FARMER_CELLS` cells or more. So five one-cell fields need one farmer, seven
  need two (6 + 1), and a single ten-cell field still needs one, as today. A field is never split between
  farmers.
- **Only the family fills the places**, best husbandry first. For the camp's land, anyone living in the camp
  can, by skill against the walk as now. A farmer's workplace (`EconomyWorker::building`) is the house, or
  the camp, and the job is still FARMER. Fields themselves employ nobody any more.
- **Fewer hands than places: the rest lies fallow.** A family with fewer free adults than its land has places
  farms its first farmers' shares, and the rest LIES FALLOW: not sown and not growing until someone farms it.
  The field's card says so, and the job overview's FARMERS row shows the empty place in amber.
- **The work.** A field grows at the pace of the farmer it falls to in the share-out. Sowing and fetching the
  harvest are errands for any of the land's farmers: nearest first, sowing before fetching (as now), and
  never two people on one errand.

### The harvest goes home first

- **Home before the store.** A load goes to the farmer's own house while the house holds less than
  `ECONOMY_HOME_DAYS` of its family's food (fetching's measure, needs_plan.md step 2). The rest goes to a
  store that takes it. A family beside its fields eats from them without fetching, and the surplus feeds
  everyone else.
- **The camp's land feeds the camp.** Its harvest goes into the camp's supplies, which take all of it, so
  the first fields feed the settlers before there is a store.
- **A load is picked up** when home or a store will take it. Today only a store counts.

### What the player sees

- **A field's card** names its owner: "Land of the Veen family", or "The camp's land". When fallow it adds
  "lies fallow: nobody to farm it". Then a "Give to:" row (`CARD_CHOICE`, like the crops'): AUTO, naming what
  it would pick, and the four nearest lived-in houses by family name, with the owner lit.
- **A house's card** shows its land: fields, cells, and who farms it ("Land: 3 fields, 5 cells - Anna farms
  it").
- **"Whose is this?"** (`FindRelatedPlaces`): a selected house outlines its land, and a selected field its
  house. Today's "a house: its family's workplaces" already lists a farmer's field; it becomes the house's
  fields.
- **Tools:** `chasm_paint field_land` (a house id, or 0 for automatic). `chasm_economy` gives each field its
  owner, choice, farmer and whether it lies fallow, and each house its land and places.

### Saves and state

- **New state:** `ZONE_OP_FIELD_LAND` (17), `ZoneBuilding::land_of`, `EconomyField::owner` and
  `ECONOMY_FARMER_CELLS`. No new worker states.
- **Saves.** The economy's field record gains its owner: `[building, crop, grown, sown, yield, last,
  owner]`. Older records load with no owner and get one on the first tick. A save holds the choice as the
  field's `"land"`, written only when one is set.
- **Older saves' farmers.** Their workplace is a field. `AssignJobs` frees them on load, and they are taken
  on again by its owner's land: a walk home, then back to work.
- **Replay tests:** the seed-12 ones get remade anyway (the user's).

## Build order

1. Land: the owner and the rule, the camp's included; the choice and its op; saves and the hash.
2. Farmers: places by the share-out, staffed from the family in the placing order; the house as a farmer's
   workplace; growth by the share-out; errands across the land; fallow fields.
3. The harvest home first, and the camp's into its supplies.
4. The cards, the outlines, the tools and the overview's counts.

## Later

- A garden could belong to the house it adjoins in the same way (buildings_plan.md), with the same owner,
  once a garden does something.

## As built (2026-10-08)

All four steps. Files: `Zones.*` (`ZoneBuilding::land_of`, `ZONE_OP_FIELD_LAND` 17, a split field's pieces keep
it), `ChasmSave.cpp` (a field's `"land"`; the economy field record's seventh number, the owner), `Economy.*`
(`EconomyField::owner`, `ECONOMY_FARMER_CELLS`, `SettleLand`, `LandPlaces`, `Tending`, `EconomyState::farmer_of`,
`GoSow` / `FetchHarvest` / `PickHarvest` across the land, the `AssignJobs` rewrite, `EconomyJobOf`: a house and
the camp employ farmers, a field nobody), `ApplicationChasm.*` (the hash, `chasm_paint field_land`,
`OVERLAY_HIT_FIELD_LAND`), `ApplicationChasmSelect.cpp` (the field's and house's cards, "Give to:", the outlines),
`ApplicationChasmEconomy.cpp` (the panel, `chasm_economy`'s field `owner` / `land_of` / `farmer` / `fallow` / `x` /
`z` and a house's `land` / `land_places`), `ApplicationChasmExplore.cpp`.

- **Staffing** walks the order once: each workplace keeps its own workers as far as its share of the people
  goes, and a land takes its family's - those on it already, then the free, then those it takes from a place
  later in the order - before the empty places of the rest are filled as before. A land counts against the
  people only as far as its family reaches.
- **A farmer's errand names the field in `store`** (`building` is his house): sowing the nearest field of his
  land that wants it and whose share is farmed, fetching from the one with the most lying. Nobody goes for an
  errand another farmer is already on.
- **The walk test** for an owner goes the way a farmer walks, from the house out to the field's middle - and it
  holds for the camp too: fields painted across the chasm from everyone (seed 12's replay save) took two camp
  farmers who could never reach them. Such a field is NOBODY'S LAND, and its card says no house or camp can
  reach it. A camp that owns a field keeps it unasked, so camp land costs no search on every change.
- **Two things the first runs showed, fixed:** fetching home (needs_plan.md, step 2) now goes to the house's own
  fields for food too - a family walked to the camp for food past its own full fields; and someone holding food
  that no store takes brings it home once the house is short again - a harvest left over when the pantry filled
  on the way was held for good.
- **A save from before land** (`EconomySaved::f_before_land`, a field record of six numbers) is settled on its
  first tick: owners, places, farmers - without a re-plan, so everyone walks on as saved. Only such a save: a
  field without an owner is otherwise one nobody can reach, and settling a save of today's again would part a
  replay from the run it was cut from.
- **Measured** (seed 12, debug, a copied exe): five one-cell fields painted apart round the camp - the camp's
  land, ONE place, Bas Visser (husbandry 10) sowing all five in turn; seven - two places, Bas 6 cells and Lotte
  Visser 1. A two-storey house painted: the Veens moved in, all seven passed to it, Wout and Daan Veen took the
  places and the Vissers were freed. Field 8 given to a house nobody lived in (it held too few for a family):
  the choice was kept, and house 9 farmed it meanwhile, until a storey more let the Vissers in - then it and
  four fields given with it went over: four places for
  three Vissers, the last field "lies fallow: too few hands" (FARMERS 5 / 6). A harvest went home: the farmer
  carried his field's greens into the pantry, and the jobless fetched from their own fields once it ran low.
  Save and load kept every owner, choice and farmer.
- **Seen, not changed** (a consequence of the user's camp rule, worth a look in play): with all the camp's land
  passing to the first house lived in, and its harvest feeding that family first, the camp's families are left
  with the camp's supplies and what they pick up at a field on a pressing need. In a test of five one-cell
  fields for ten people, the camp starved first.

**Not run**: the replay test - seed 12's get remade anyway (the user's); its saves are from before land, and
load as such.

## Fair shares (2026-10-08, the user's)

Seen in play: one house well stocked, the next with nothing. Home first let the family that owns the land keep
its fields' food - the farmer took it home, the family fetched from its own fields - while every other house
could only fetch from the stores and the camp. Every house had the same ceiling (a day past
`ECONOMY_HOME_DAYS`); what differed was who could reach the food.

Proposed and agreed ("the farm houses will get some more food for themselves this way"):

- **Food at home in days a head**, as the bars count it (a unit at what it is worth), for every home alike.
- **The colony's average**: all the food in homes - houses, and a camp's supplies for those living in it - over
  everyone living in them (`Economy::FairShare`). A hungry camp pulls it down too.
- **A house keeps its land's food only while it is less than `ECONOMY_FAIR_DAYS` (1) a head ahead of that**
  (`HomeAhead`), and still only while it is short, as before; a load from its own field brings it up to that
  and no further (`TakeHome`). Otherwise the farmer carries the harvest to a store, where the other houses fetch
  it as they always have; with no store, it stays on the field, where anyone with a pressing need eats.
- **The same for fetching from its own fields**: a house ahead fetches from storage like any other.
- **Not held to it**: fetching from a store or the camp - that food is everyone's already.
- The camp's land still goes into the camp's supplies whole: they are the colony's.
- **Measured** (seed 12, debug, a copied exe; a farm house with its own fields, a house with none, the Smits in
  the camp; water kept up, food left to run). Five fields and a store: both houses stayed about level, two to
  three days a head; a harvest went to the store while the farm house was not short, and the landless house
  fetched it from there. Ten fields and no store, so the farm out-grew the rest: of 18 trips home from the
  land, none set off with the house ahead, and in the lean days (12-14) the farm house held 1.1-1.5 days a head
  - the average and a day - where the old ceiling alone would have let it fill to three; what it could not
  take stayed on the fields for anyone with a pressing need. Both colonies starved in the end (five or ten
  one-cell fields do not feed ten - needs_plan.md's sums); the shares held to the last.
