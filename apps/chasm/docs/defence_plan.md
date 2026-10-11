# Defence: posts, orders, provisions, the guard tower and gate doors

Talked through with the user 2026-10-09 and agreed as it stands; nothing is built yet. The threat in `gameplay_plan.md` (P6: zombies
from the swamp, walls, archers, ballista towers) needs three things the colony does not have yet: people
who can be SENT somewhere and stay there, gates that can be SHUT, and a place to train archers. This plan
covers the first two. The training ground, the alarm and the zombies themselves are listed at the end,
agreed in the gameplay plan but not planned here.

**All of it is rules**: saved, hashed and replayed. An order, a post and a door are changed only by
recorded commands and the tick.

## Decided (the user, 2026-10-09)

- **A post is a job.** Standing there is his work. Needs stay exactly as they are: when one becomes
  pressing he goes to the nearest supply, eats and drinks, and walks BACK TO THE POST instead of home.
  Nothing in `needs_plan.md` changes.
- **Both ways of posting someone**: DIRECT ORDERS (select a person, right-click where he should stand)
  and POSTS that people are assigned to, the way jobs are given out now. There are no posts yet. The
  first one is a GUARD TOWER.
- **The guard tower**: two storeys high, in the palisade's look. It STORES WATER AND FOOD, and those are
  part of its construction cost. (Its food became provisions and its refilling the caretakers', below.)
- **A soldier takes supplies with him**: a pack, which should at least double how long he can stay.
- **Gates get a door that can be shut.**
- **The first draft's proposals were agreed as they stood** (the user, the same day), with one addition:
  a JOBLESS person under an order is given no job until he is released.
- **The builder becomes builder and caretaker**: resupplying the towers is his task, AHEAD of building.
  That is the refill rule the tower was left without.
- **Site costs per good** (build step 4) is a general change, needed later anyway.
- **Caretaking is the builders' only** - the jobless do not resupply towers. So towers need a builders'
  hut, which the colony then has to build.
- **PROVISIONS, a new food** (the user's): made by the caretaker from wheat and beans, worth the two
  together. Provisions are what stocks a tower, and what a tower is built with.
- **Soldiers eat whatever food is nearest**, provisions included. At a tower, provisions are nearly
  always nearest, so they go for them by themselves.
- **Villagers never stock provisions** - never fetched home - but eat them from a normal store when
  there is nothing else.
- **The provisions draft's proposals were agreed as they stood** (the user, the same day): the last
  resort made exact, how and when provisions are made, water kept in towers, any posted person eating at
  a tower, and a maker's pace by strength - as for the rest of a builder's work, until there is a cook.
- **Water**, worked through after: the pack is **3 water and 1 provision**, a tower **12 water and 8
  provisions**, and **a guard on his tower eats and drinks from it as at home**.
- **Masks of goods become 16 bits.**
- **The tower tuned** (the user's, 2026-10-10, after step 6): it was always square, which made it stand out on
  this grid. Now it takes its JUNCTION's shape, as every building takes its plot's, and the junction decides
  its GUARDS: a 3-junction 1, a 4-junction 2, a 5-junction 3, a 6-junction 4. Its COST is so much a guard place,
  fixed, so no shape is cheaper than another for what it holds. Its LADDER IS ITS DOOR: walkers get on and off
  it only from the ladder's side - by default the open side nearest the camp, which is usually the inside of a
  wall - and the player can move it from the tower's card. Nothing is built or walled on the ladder's plot. The
  ghost shows the shape, which is how the player finds the rarer junctions (and the terrain shows them too).
- **Step 8 extended** (the user's, 2026-10-10, after step 7): auto-filling the posts is fine, but guards must be
  RELEASABLE, and the player will often want ONE guard on a tower that could hold two or three. So a tower has a
  GUARD COUNT, 1 when placed; a right click posts the selected person there BY HAND, over an automatic pick; and a
  guard can be released. See "Guards by hand" below.

The reference here is Knights and Merchants, the one game the user remembers with soldiers who need food:
an annoying mechanic, but it worked. The aim is to keep what worked and leave out the chore. Nobody is
told to eat; the player's part is WHERE the tower, the stores and the posts stand.

## Why the pack and the tower: the clock today

From the current tuning (`needs_plan.md`, "Current tuning"; a day is 45 s at 1x):

- Someone posted with full bars goes for water after **2.7 days (2 minutes)** - the bar is 4 days and
  he goes below a third of it - and for food after 5.25 days (4 minutes). Kept there regardless, he is
  dry on day 4 and dead on day 5 (3¾ minutes).
- **Water comes first, and it is what kills**: dry, health falls three times as fast and food no longer
  helps. Per day a person drinks 0.5 water and eats 0.33 provisions - 3 water to every 2 provisions -
  so every stock below is balanced at that ratio, and runs out of both at about the same time.
- With a pack of **3 water and 1 provision** (below) he goes for water after **8.7 days** and for food
  after 8.25 - **about 6 minutes**, three times as long as without. (A first draft's 2 water ran out at
  6.7 days, food still at 8.25.)
- **On a tower** he eats and drinks from its stock as at home, so he never runs short while it has any.
  Its 12 water and 8 provisions keep its two guards for **12 days (9 minutes)**: a tower uses about 1.7
  units a day, a caretaker's load every three days. A collector fills about 11 water a day, so what makes
  supplying it hard is where the tower stands, not the amounts.
- Waves are meant to come every 7-11 minutes outside winter (`gameplay_plan.md`, "Pace"). So a packed
  soldier off a tower leaves his post less than once between waves, and a tower's guards not at all
  while the caretakers keep up.

These are sums from tuning, and change with it.

## The plan

### Posted: a job at a place

- **Per person, his post**: a plot he stands on, or a tower. Saved, hashed with the economy. Two worker
  states, appended (never renumber): `WORKER_TO_POST` and `WORKER_ON_POST`.
- **On his post** he stands there. He does not go home, does not fetch for his family, and is not idle
  labour for sites or roads.
- **On a tower, the tower is his pantry** (the user's): he eats and drinks from its stock exactly as he
  would at home - a unit whenever the whole of it fits (`Consume`) - so while it has any he never goes
  pressing, and his pack waits untouched. Only an empty tower sends him looking, as below.
- **A pressing need**: `SeekSupply` as now, with three differences. Since he is not going home, his own
  house is not "on his way back" (`f_home` false) but a supply like any store, by its distance; another
  family's house still never is. Any food counts, PROVISIONS included, and the nearest wins - on or by a
  tower, that is its provisions. And towers count, which for villagers they never do. After eating he
  refills his pack there and walks back to the post.
- **Cut off from his post** (a wall, a shut gate): as for someone cut off from home today - he waits,
  goes for pressing needs, and tries the way again every so often.
- **Drawn** standing on his plot, or on a tower's platform. His badge gets a guard's icon.

### Direct orders

- **Select a person, right-click a plot he can walk to**: a recorded `CHASM_CMD_ORDER` (his id, the plot).
  A right-click with no person selected does what it does now - puts a tool down, or clears.
- **A new order replaces the old one**: a second right-click moves him.
- **Release**: a button on his card sends the same command with no plot. Tool form: `chasm_economy` op
  `order` / `release`, beside its `set_needs`; and `chasm_select` op `press` with `button` right for the
  right click itself.
- **His job while he is ordered**: kept and suspended. The workplace stays his and stands idle - nobody
  else takes it - and on release he goes back to it. That is the gameplay plan's "trainees drop their work
  and join them", and an idle hut is the visible cost of defence. The other way, giving the job away,
  would reshuffle jobs every time an order ends.
- **Ordered people are out of the staffing altogether** (`AssignJobs`, `builders_plan.md`): a jobless one
  is given no job - nor a tower's place - until he is released (the user's), and a working one's place
  is neither taken from him nor counted free, even if a new builders' hut would otherwise free it.
  Released, he is staffed like anyone else on the next pass.
- **One person at a time** for now. Selecting several is a later step - agreed 2026-10-11, `selection_plan.md`,
  "Several people selected".

### The pack

- **Per person, what he carries for himself**, apart from any load: water and food, by good. Saved,
  hashed. Lost with him if he dies, like a load.
- **Its size** (tuning, agreed): **3 water and 1 provision** - or its 3 days as other food: 3 wheat, 6
  greens, 1-2 beans - balanced so both run out together, and under a load. Named constants with the other `NEEDS_*`/`ECONOMY_*` values.
- **Eaten as at home**: a unit whenever the whole of it fits (`Consume`), so his bars stay near full while
  the pack lasts.
- **Filled**: when he sets out for a post from home, from his home's pantry - no detour, and
  the family's fetching makes it up; a home never holds provisions - and at every supply trip, wherever
  he eats, provisions included. An order given while
  he is out sends him straight there with the pack as it is: an order may be urgent.
- **Only posted people use a pack** for now.

### Provisions

- **A new good**, `GOOD_PROVISIONS` (appended to `Goods.h` - never renumber). One is made from **a wheat
  and a beans** and is **worth the two together**: 3 days of health (`EconomyFoodWorth`: 1 + 2). It keeps
  long, for when spoilage comes.
- **A bit of its own, kept OUT of `GOODS_FOOD`.** Everything villagers do with food today - fetching
  home, a house's pantry, fair shares, a home's days of food - then ignores provisions with no change, and
  "villagers never stock provisions" holds by itself. Provisions count only where they are added on
  purpose:
  - a POSTED person's pressing need and pack: any food, nearest first;
  - a VILLAGER's pressing need, as a LAST RESORT ("when there is nothing else", made exact):
    `SeekSupply` looks for other food first, and only if no place he can reach has any, looks again with
    provisions. At a place, provisions are eaten after every other food - greens, wheat, beans, then
    provisions.
  - `GoodIsFood` (`good >= GOOD_WHEAT`) takes provisions as food, which is right; the next NON-food good
    appended will have to change it.
- **A mask of goods becomes 16 bits** (the user's, 2026-10-09), with this step: provisions would be the
  sixth of eight, and steam, floatstone, coal, rope and leather are coming. `GOOD_BIT`, the masks and
  every `uint8_t` that holds one (`ZoneBuilding::allow`, `want` in `EconomyPressing` and `SeekSupply`,
  `TakeHome`'s goods, ...). Saves name a store's goods by NAME, so nothing in them changes.
- **Stores** get a provisions chip like the other goods' (`store_allow`). Towers keep them; houses never.
- **Making them** is a caretaker's errand: fetch wheat and beans from a store, a load at a
  time; make them at the builders' hut, a while each at his pace; take them where they are wanted - a
  tower short of them, a tower's site, or else the nearest store that takes provisions.
- **Only what is wanted**: towers short of provisions and towers' sites, less what the stores
  hold and what is on its way. So no wheat and beans go into provisions nobody asked for. A stock kept
  ahead of a wave can come later.
- **No wheat or no beans, no provisions.** A colony that wants towers needs a bean field - beans become
  a crop chosen for a reason, not only for their yield.

### The guard tower

- **A new kind**, `ZONE_KIND_TOWER` (appended), on the build grid next to WALL.
- **The look**: one plot, two storeys of upright logs, a platform at the top with a pointed parapet like
  the palisade's stakes, a ladder. Whether it has a roof is open.
- **Where**: by a building's rules anywhere, and **in a wall's line**: painted on a wall plot,
  it takes that plot over. Buildings already stop walkers (`Walkers.cpp`: only a winch, a camp and a
  standing bridge can be walked through), so the wall stays sealed, and the palisade ends against the
  tower as it does against a gate's post.
- **Its guards**: places like any workplace - its junction's edges less two, so 1 to 4 (tuned, above) -
  filled in order like the others, by **archery**, then **precision** (the archer's skill). A new job,
  `WORKER_JOB_GUARD`. A guard's home stays his house.
- **Its stock**: it keeps provisions and water - provisions are its food, and a guard still drinks.
  **Only posted people draw on it** - its guards, as their pantry, and anyone under an order nearby, as a
  supply. `SeekSupply` skips it for villagers, the way it skips other families' houses, or the village
  would empty it.
- **Built with provisions**: wood by its storeys as any building, plus (tuning, agreed) **12 water and 8
  provisions** - 12 days for its two guards, both running out together. Today a site takes nothing but wood (`site`, by building id; carriers fetch from
  the nearest wood), so this needs **site costs per good**: the carrier fetches whatever good the site
  still lacks, from a store or the camp. That is the largest piece of this plan, and a general one -
  stone would use it too. Provisions come only from caretakers, so no builders' hut, no tower. The tower
  takes its guards once its provisions are in. Placed in debug, it stands full.
- **Refilled by the caretakers** - see below.

### The builder as caretaker

- **A builder's errands, in order** (the user's: towers ahead of building):
  1. a tower short of provisions or water: carry them there - provisions from a store, or made for it
     (above); water from a store or the camp;
  2. provisions a tower's site still wants: make them;
  3. building, as now.
- **Short** is below its full provisions - the same figures it is built with - so a tower is kept full,
  not topped up only when empty. A threshold below full may come with tuning, if builders spend too long
  on it.
- **The same carrying as step 4's**: a tower short of provisions is, to a builder, a site that wants
  provisions and water. So this comes after step 4 and costs little once that is there.
- **Builders only** (the user's): the jobless still carry to sites, but never to towers, and never make
  provisions. A colony with towers and no hut has towers nobody keeps.

### Guards by hand (step 8)

Agreed with the user 2026-10-10. Releasing a guard on its own would mean nothing - the auto-fill would send the
next free adult up at once - so it comes with a count the player sets.

- **The guard count**: each tower has one, 0 up to its places (its junction's guards), set by chips on its card -
  a recorded command, saved and hashed. **1 when placed** (the user's). Its places to fill are the count, not its
  junction's: the auto-fill (archery first) fills up to it; lowering it sends the extra guards home, the automatic
  ones first; raising it fills from the free again. At 0 the tower is unmanned - the builders still keep it full,
  and nobody is missed at it. Its stock and cost stay the whole tower's, so one guard on a three-place tower lives
  on it three times as long.
- **A right click** with a person selected, on a tower, makes him one of its guards BY HAND:
  - he leaves his job, or his order, for it, and goes up at once, his load put down as a crate as for an order;
    a child is refused, as for orders, and so is anyone with no house or no way there, or a tower not built yet;
  - a hand-picked guard is never swapped out by the auto-fill, nor taken by a shortage of people - kept like the
    posted;
  - with the count reached and a place to spare, the count goes up by one;
  - every place taken, he takes an AUTOMATIC guard's (the user's: the player's pick overrides the game's) - the
    weakest archer, who goes home; all of them hand-picked: refused, "every place is taken".
- **Release**: a chip on a guard's card and on his line in the tower's card. He goes home jobless, and the count
  drops to the guards it has less him, so nobody takes his place. Raising the count brings the auto-fill back.

### Gate doors

- **A gate** - a road plot a wall runs through (`ZoneState::gate`) - **gets a door**: two leaves of
  pointed logs in its frame, drawn open or shut.
- **Shut is closed to walkers**, the way a standing wall plot is: **shut for everyone, villagers too.** Until zombies exist, a gate that only stops zombies would be the same as an open one;
  and shutting the gates sending carriers round, or leaving them outside, is the trade-off the gameplay
  plan wants walls to have. A gatekeeper who opens for his own people can come later.
- **Who shuts it**: the player, from the gate's card - a recorded command. A gate has to become
  selectable for that; a road plot is not today. Tool form too. Later, the alarm shuts them all.
- **Someone whose route goes through a gate as it shuts** re-plans, as when the zones change; someone
  shut outside is cut off, as above.
- **State**: a shut flag per gate plot, saved with the zones' ground and hashed with them.
- **Cost**: none for now. The gate plot is road, and roads cost only labour.

## Build order

Each step tested before the next, by the tool and a replay.

1. **Orders** - BUILT 2026-10-09, below: the post, the two states, right-click and release, the card,
   needs and back to the post. Tested: an ordered person stays, leaves when a need is pressing, eats, and
   comes back; his workplace stands idle and he returns to it on release; an ordered jobless person is
   given no job while a place stands empty, and takes it once released; a save and load in the middle
   gives it all back.
2. **The pack** - BUILT 2026-10-09, below: filled at home and at supply trips, eaten from. Tested against
   the sums above.
3. **Gate doors** - BUILT 2026-10-09, below: the state, the look, walkers closed, the card and command. This step depends on none
   of the others and can be built at any point, by another window if one is free.
4. **Site costs per good** - BUILT 2026-10-09, below: construction carrying food and water as well as wood.
   General - stone and the rest will use it. No kind takes any other good yet.
5. **Provisions** - BUILT 2026-10-09, below: the good, its own bit, the eating rules (posted: nearest of any
   food; villagers: last resort), the store chip, and caretakers making them at the hut. Tested: made only
   when wanted; villagers eat them only with nothing else in reach and never fetch them home. Nothing wants
   any yet: the tower (step 6) is the first.
6. **The guard tower** - BUILT 2026-10-10, below: the kind, its mesh, placement including the wall's line, its
   places and the guard job (archery first), its stock for posted people only, built with provisions and water.
7. **The caretakers' resupply** - BUILT 2026-10-10, below: builders keep towers full, ahead of building.
8. **Guards by hand** - BUILT 2026-10-10, below (extended, above): the tower's guard count (1 when placed), a right click on a tower posting
   the selected person there over an automatic pick, and releasing a guard.

## Open

- **Cold on a post in winter**: proposed, warm or cold by his home as now. It may not matter while no
  zombies come in winter.
- **How long a provision takes to make** - tuning, like every other number here, which are first
  figures for playtesting to move (`needs_plan.md`, "The numbers are tuning, not rules").
- **The tower's roof** - none for now: an open platform behind a parapet (step 6). Its key is T, after RANGE
  on the build grid, which leaves ERASE on a row of its own.
- **The gatehouse** - WANTED (the user, 2026-10-10, out of `arches_plan.md`): two guard towers flanking a
  palisade gate, joined by a WALKWAY between their decks over the gate. At the decks' height (two storeys and
  `TOWER_DECK`) it clears the gate's posts and beam (`WALL_GATE_HEIGHT`), where a house's arch, its passage a
  storey high, would not. Guard places of its own on the walkway, reached by the towers' ladders - no way up of
  its own - and paid so much a guard place, as the tower is. It is not an arch: towers are never arched (for
  now), and nothing is arched over a gate - the gatehouse is what that case becomes (`arches_plan.md`). Left
  for later (the user, 2026-10-10); not in the build order. Until it comes, a wall drawn in play across a road
  with a BUILT arch over it is refused, "an arch stands over the road" (`arches_plan.md`, "As built: step 4") - a
  gap in the wall the player closes by taking the arch down first, which the gatehouse is to close for him. Open:
  how it comes (painted on the gate plot between two towers, or of itself once both stand), whether its guards
  share the towers' stock, its look, and whether it closes that gap - a house's arch over a wall's gate.
- **Releasing a tower's guard** by an order: the order would suspend the tower job like any other.
- **Sites take food and water people need** (step 4): a site's water and provisions come out of the same
  stores and camp people drink and eat from, with nothing kept back. For a tower's 12 water that is a day of
  a small colony's drinking. A floor below which carriers leave food and water for the people - the fetching
  home's days, say - is one answer, if it bites in play.
- **A load on him when he is ordered** - SETTLED: he DROPS it, left on the ground as a CRATE holding the
  load (`crates_plan.md`, step 1 built 2026-10-09). It was held on his post and taken to a store after
  release, then lost at the order as the user's stand-in. Crates are also the start of finds lying on the
  map, as They Are Billions' bonus resources (`crates_plan.md`).

## As built: step 1, orders (2026-10-09)

Files: `Economy.*` (`Order`, `TakePost`, `GoToPost`, `EconomyAtPost`, `EconomyCanOrder`; the states, `AssignJobs`,
`Tending`, `SeekSupply`, `GoHome`, `Replan`, the collectors' pace in `Tick`), `ChasmSave.cpp` (`"post"`),
`ApplicationChasmEconomy.cpp` (the command, the hash, the tool), `ApplicationChasmSelect.cpp` (the right click,
the card, the badge, the post outlined), `ApplicationChasmTime.cpp` (POSTED in the jobs corner, the refusal),
`ApplicationChasm.h`.

- **The post** is `EconomyWorker::post`, a plot, -1 none: saved, hashed. `WORKER_TO_POST` (23) and
  `WORKER_ON_POST` (24), appended. On his post means within `ECONOMY_POST_REACH` (0.5) of the plot's point.
- **The command**: `CHASM_CMD_ORDER`, recorded - subtype the person, value[0] the plot, -1 to release.
  `Economy::Order` refuses a child ("too young"), a plot with a building or a wall on it - a camp and a
  standing bridge are open ground ("something stands there") - and a plot he cannot walk to ("no way there").
  Refused, the play overlay says so for a moment, as a refused placement does: CAN'T SEND: NO WAY THERE.
- **When it lands**: the post is his at once; what he was doing he drops on the next tick (`TakePost`, from
  `TickWorker`), after whatever the zones did that tick, and the staffing follows on that tick too
  (`f_restaff`). A storey he was building is built with what it has (`Build`): its wood is in the site
  already, and nobody else would come to finish it. A load stayed on him (since step 2: it is lost at the
  order; since crates step 1, put down as a crate); his tree, site or store is let go.
  A pressing need under way goes on first. Already posted, he turns for the new post at once.
- **On the post** he only stands and looks to his needs, every look. `SeekSupply` takes his own house
  as a supply by its distance (at its door) while he is posted, since he is not going home; after the meal,
  `GoHome` sends a posted person to `GoToPost` instead. With no way there he is `ON_POST` where he stands -
  cut off - and tries again every fourth look and whenever the zones change (`Replan`).
- **Staffing**: `AssignJobs` keeps a posted worker's place before it walks the order - counted taken, never
  refilled, while the workplace is there and has the place - and lets nobody posted be chosen for anything.
  His workplace stands idle: a collector does not fill (`Tick`'s pace), his share of a field grows nothing
  (`Tending`, `EconomyFieldPace`), though it is still his.
- **The right click**: a person selected, a right click on the ground sends him there. With a tool in hand
  it still puts the tool down; with nothing selected, or anything but a person, it still clears.
  `chasm_select` op `press` takes `button: right` for it.
- **The card**: "Posted" (amber), with "this work waits until released" when he has a job, or "No job -
  none is given while posted"; a Release chip (`OVERLAY_HIT_RELEASE`); "going to the post", "standing
  guard" or "cut off from the post" as what he is doing. His post is outlined as a plot tile with his house
  and workplace. His badge shows a SPEAR in place of his job's icon (the thirst drop still comes first). The
  jobs corner gets a POSTED row while anyone is.
- **Tool**: `chasm_economy` op `order` (person, x/z or plot) and `release` (person), with `refusal`; a worker's
  `post` (plot, x, z, there); `people.posted`.
- **Tested** (seed 12's default map, debug; a builders' hut placed by the camp, its place taken by Dolf):
  - A child refused, "too young". Dolf sent 12 out: posted at once, his job kept, walking on the next tick,
    standing on his post once there; builders 1 / 1 throughout.
  - Thirsty (water set to 0.3 days): to the supply, drinking, back to the post - his camp's supplies.
  - Every jobless adult sent too, then a second builders' hut painted: its place stood empty (builders
    1 / 2) and nobody posted got a job. One released: on the next tick he was the new hut's builder, and
    Dolf released went back to his own hut. Releasing someone not posted is refused.
  - In play mode: Dirk selected and right-clicked over open ground - sent there, walked, stood guard; his
    card as above; its Release chip, pressed, sent him home.
  - Interrupted: a carrier taking 2 wood to a house site, sent off - kept the 2 wood, and another took his
    place carrying for the site; a builder mid-build sent off - left on the next tick, the site keeping the
    wood brought.
  - Replay: posted before the recording (so the start save holds a post mid-walk), then an order, a thirst
    edit, a re-order and a release during it - 1,513 ticks. Two replays: every tick's hash equal, nothing
    changed between ticks.

## As built: step 2, the pack (2026-10-09)

Files: `Economy.*` (`EconomyWorker::pack`, `FillPack`, `Unpack`, `EconomyPackFood`, `NEEDS_PACK_*`; `TakePost`,
`Needs`, the supply's end and the at-home look in `TickWorker`, `Order`), `ChasmSave.cpp` (`"pack"`, by good
name), `ApplicationChasmEconomy.cpp` (the hash, the tool's `pack`), `ApplicationChasmSelect.cpp` (the card).

- **The pack** is `EconomyWorker::pack`, a count per good: saved by good name, hashed. 3 water and 3 days of
  food (`NEEDS_PACK_WATER`, `NEEDS_PACK_FOOD` - tuning, in `needs_plan.md`'s table).
- **Filled** (`FillPack`) when he sets out for his post from home - his house's pantry or his camp's supplies,
  as he drops what he was doing (`TakePost`) - and at the end of every supply trip, from what is there. Water
  first, then food, the kind that keeps best first (beans, wheat, greens), a whole unit more while still short
  of the mark: from a camp, 2 beans - 4 days.
- **Eaten** in `Needs`, every tick while posted, by `Consume` - a whole unit whenever it fits, as at home. So
  his bars stay up while it lasts, and a pressing need sends him off only once it is empty.
- **Home again** after release, the at-home look puts it back into the pantry (`Unpack`) as far as the house
  keeps each good and has room; a camp takes it all. What does not fit stays packed, for the next post.
- **A load on him at the order is lost** (`Order`), as the user asked, until crates hold what is dropped -
  which they do since crates step 1 (`crates_plan.md`): it is put down as a crate.
- **The card** says "Pack: 3 water, 2 beans"; the tool gives a worker's `pack` with its `food_days`.
- **Tested** (seed 12, debug, a builders' hut by the camp; everyone else kept fed by debug edits so the camp's
  supplies stayed his):
  - Packed at the camp on setting out: 3 water and 2 beans. Set to 1.5 days of water and 4 of health: he drank
    a unit (3.5 days) and ate a bean (6 days) from the pack.
  - On a post from full: he went for water after **8.8 days** - the sums above said 8.7, the rest is the walk
    there - with 2.2 days of health left. At the supply his pack was filled again to 3 water and 2 beans.
  - A save and load in the middle gave the pack back. Released, he went home and unpacked into the camp.
  - A carrier with 2 wood for a house site, ordered: the wood gone on the next tick.
  - Replay with packs in play, 1,512 ticks: two replays equal on every tick, nothing changed between ticks.

## As built: step 3, gate doors (2026-10-09)

Files: `Zones.*` (`ZONE_GATE_*`, `ZONE_OP_GATE_SHUT`, `ZoneGateShut`, `ZoneShutToWalkers`, the saved flag),
`Walkers.cpp` and `EconomyStraightClear` (closed), `Economy::Order` (refused onto one), `RoadMesh.cpp` (the door),
`ApplicationChasm.cpp` (the save, `chasm_paint` op `gate_shut`), `ApplicationChasmSelect.cpp` (picking a gate, its
card), `ApplicationChasm.h`.

- **The state** is the gate itself: `ZoneState::gate` holds `ZONE_GATE_OPEN` (1, as every gate is made) or
  `ZONE_GATE_SHUT` (2), so everything that asked "is it a gate" still does. Saved as `ZONE_GROUND_SAVED_SHUT`
  (64) beside `_GATE` in a saved ground's kind; hashed with the zones, as `gate` already was.
- **The command**: `ZONE_OP_GATE_SHUT`, a recorded zone command - value[1] 1 shut, 0 open - on any plot of
  the gate. A road crossing a wall on the slant gates two plots side by side: every gate plot joined to the one
  named goes with it, so a gate is never half shut. Taking the wall or road away takes the gate, and its door.
- **Closed**: `ZoneShutToWalkers` - a standing wall plot, or a shut gate - in place of `ZoneWallStands` in the
  walkers' edges and the economy's straight walks: never walked onto, nor through; off one only for someone
  standing on it when it shut. A zone change, so everyone walking re-plans (`Replan`); whoever no longer has a
  way is cut off and waits, as anyone cut off from home or post does - and goes on when it opens again.
- **Orders** onto a shut gate are refused ("something stands there"), and into a shut-in place too ("no way
  there"), by the existing rule.
- **The look** (`BuildGate`): under the crossbeam, a LEAF from each jamb to the gate's middle - pointed logs of
  lighter timber, a little over the palisade, with two dark battens across. Shut, they meet across the road.
  Open, each is swung back about its jamb to lie along the road's edge, both to the same side by a fixed
  direction; seen from the far side the posts hide them, so an open gate reads as a clear gateway. A door is
  drawn only where its beam is: once the walls beside it stand.
- **Picking and the card**: a click on a gate's road plot selects the gate (`PLAY_PICK_GATE`, by its plot),
  outlined as a plot tile with its other plot on the slant. Its card: "Shut - nobody passes, our own people
  neither" (amber) or "Open - anyone passes", an Open / Shut pair of chips (`OVERLAY_HIT_GATE`), and "Its wall
  still has gaps to build" while a wall plot beside it is planned. `chasm_select` op `select` at its x,z picks it.
- **Tool**: `chasm_paint` op `gate_shut` at x,z or `plot` (`on`, default true; false opens); its `gate_here`
  says none, open or shut.
- **Tested** (seed 12, debug): a square of walls 12 across with a road through it east-west - a gate of two
  plots in the west wall (on the slant) and one in the east.
  - Open: a way out of the square. West shut: both its plots together, and still a way out by the east. Both
    shut: no route, no straight walk through a gate either. An order into the square: "no way there"; onto a
    shut gate: "something stands there".
  - Doors opened, someone sent in, the doors shut when he was 1.4 from the east gate: he stopped there, outside,
    cut off. Opened again, he went on at once.
  - A save and load kept all three plots shut, and the square closed.
  - In play mode: the gate selected by a click, its card as above; the Shut and Open chips pressed through the
    overlay; the door drawn shut and swung open.
  - Replay with an order and four door changes in it, 1,061 ticks: two replays equal on every tick, nothing
    changed between ticks.

## As built: step 4, site costs per good (2026-10-09)

Files: `Economy.*` (the costs, `site`, carrying, `Build`, the old-save fallback), `ChasmSave.cpp`,
`ApplicationChasmEconomy.cpp` (the hash, the tool), `ApplicationChasmSelect.cpp` (the card, what a carrier is
doing), `ApplicationChasmTime.cpp` (the BUILDING banner).

- **What a site takes**, by good: a building's storey its WOOD by floor area and kind, as before
  (`EconomyStoreyCost`), and any other good by its kind alone, so much a plot-storey (`StoreyGoods`, all 0 -
  the tower's water and provisions go here in step 6). A site on coarse cells (a field) so much a cell by kind
  and good (`EconomyCellCost`; a field its wood) - the archery range's wheat goes there. Gardens, lots and walls
  stay wood only.
- **Built when all of it is in**: a storey stands only with all it takes of every good there, the lowest
  first as before; a field's cells go down when every good is in. What it took is USED UP. Whether a tower's
  water and provisions become its stock instead is step 6's to decide.
- **Brought** is `EconomyState::site`, by building id and good; `Economy::SiteNeed`, `SiteBrought` and
  `SiteLeft` answer by good. Saved by good name (`[id, {"wood": 8, "water": 6}]`); an older save's bare number
  is read as wood. Hashed whole.
- **Carrying**: each good a site still lacks is an errand of its own in the one ranking (`FindSiteWork`): by
  walk, then the placing queue and "build first", as before - on a tie the lower good. A carrier going to fetch
  names the good in `carry_good` with nothing carried yet, as one fetching for home already did. Goods come
  from where wood came from: a store, the camp's supplies, a house nobody lives in, and a woodcutter's pile,
  which only ever holds wood. Not from a collector or a field: their goods reach a store first.
- **A load of any good is a while's building** at the site (`ECONOMY_BUILD_SECONDS`), as wood's was.
- **Shown**: a site's card has a bar per good it takes ("Water 34 of 60"); the banner over the build grid adds
  any good other than wood that some site wants ("WOOD 8/8, WATER 34/60"); a carrier is "fetching water for a
  site", "carrying food to a site"; `chasm_economy` gives a site's `needed` and `brought` by good (and
  `wood_needed`/`wood_brought` as before), a carrier on his way his `fetching`.
- **Tested** (seed 12):
  - Wood only, the build before and after the change, the same four sites painted as a player: people's states,
    positions, loads and the sites' wood identical every 250 ticks for 4,000 ticks.
  - With TEMPORARY costs, taken out again: a store's storey 6 water, a builders' hut's 60 (more than the colony
    had), a field cell 1 wheat. Water was fetched for the store and carried to it, and the store stood; wheat
    was fetched for the field, and it was built; the builders' hut had all its wood and 34 water and did not
    stand. A save and load kept its 34 water and 8 wood. The recording, 2,176 ticks: two replays equal on
    every tick.
  - That test also showed the cost of it: **nothing is kept back**. The builders' hut took the camp's water
    down to none, and "water under a day" came up - carriers take a site's food and water from the same stocks
    people live on, as they take wood from firewood today. See Open.

## As built: step 5, provisions (2026-10-09)

Files: `Goods.h` (the good, `GoodsMask`, `GOODS_EATEN`), `Economy.*` (worth, eating, the pack, `SeekSupply`,
`ProvisionsLacking`, `MakeProvisions`, `AtWorkshop`, the four states, `SetStock`), `Zones.*` (a store's goods, 16
bits), `ApplicationChasm.*` (the zones hash, `CHASM_CMD_STOCK`), `ApplicationChasmEconomy.cpp` (the command, the
tool), `ApplicationChasmSelect.cpp` (what a builder is doing, the hut's card, the chips wrapping).

- **The good**: `GOOD_PROVISIONS` (5; `GOOD_COUNT` 6), named "provisions" in saves. Worth a wheat and a beans
  together (`EconomyFoodWorth`: 3 days). Saves name goods by name, so old ones load as they were.
- **Masks are 16 bits**: `GoodsMask` (`uint16_t`) for every goods mask - a store's setting, what a building
  takes and keeps, a pressing need, a house's shortage. A store's setting is now hashed apart from its size, as
  16 bits would run into it.
- **Outside `GOODS_FOOD`**: fetching home, a house's pantry and its days of food, fair shares, the top bar's
  FOOD - all ignore provisions with no change. `GOODS_EATEN` (the crops and provisions) is what a pressing
  food need now asks for, and the seeker says whether provisions count:
  - **posted**: any food, the nearest place first (`SeekSupply` with `GOODS_EATEN`);
  - **villagers**: the crops; only if that finds no place he can get to, again with provisions - his LAST
    RESORT. A villager next to provisions and with wheat across the map walks to the wheat.
- **Eaten last**: wherever anyone eats (`Consume`) - greens, wheat, beans, then provisions. Into a pack
  FIRST (`FillPack`): provisions, beans, wheat, greens - one provision is the whole pack's 3 days.
- **No home holds them**: a house never keeps them (`ECONOMY_HOUSE_KEEPS`), and a soldier home again keeps
  his provisions in his pack, even at the camp, whose pantry takes the rest (`Unpack`).
- **Stores** take them by default and have a provisions chip; the card's goods chips now WRAP onto a second
  row instead of running off the card.
- **How many are wanted** (`ProvisionsLacking`): what the sites still lack of them (less what is on its way),
  less what lies made in a store, the camp's supplies, a house nobody lives in, a builders' hut or a crate, and
  less what is being made. Towers short of them join this in step 7 (since built). Wheat and beans at the huts are not
  counted: they become provisions only while this is above 0.
- **Making** (`MakeProvisions`, a builder's errand from home, before his building - so ahead of carrying and
  line work, and of crates, the caretakers' last): with some wanted -
  - a wheat and a beans at his hut: off to it to make them (`WORKER_TO_WORKSHOP`), one at a time
    (`WORKER_MAKING`, `ECONOMY_PROVISION_SECONDS` at his pace - strength), the wheat and beans taken at the
    start, up to a load a round, and home;
  - too few at every hut, counting what is on its way, for what is wanted: a load of the scarcer of the two
    for his own hut - as many as the pairs still wanted - from the nearest store, camp's supplies, house nobody
    lives in or crate with some to spare (`WORKER_TO_INPUT`, `WORKER_TAKING_INPUT`, then `WORKER_TO_WORKSHOP`
    carrying it). With none of the other half to be had, only as many as pair with what the hut already has of it.
  - none wanted: what lies at the hut for nobody - provisions beyond what is wanted, any wheat and beans - a
    load at a time to a store that takes it. With none that does, it stays at the hut.
- **Carried to the sites** as any good is (step 4): a builders' hut is a source for its PROVISIONS only, never
  its wheat and beans - so the range's wheat is not taken from a hut's half-made provisions.
- **Nobody eats from a hut**: `SeekSupply` passes builders' huts by, as it passes other families' houses.
- **Shown**: a builder is "fetching wheat for provisions", "taking beans to the hut", "making provisions";
  the hut's card says what it holds for them. `chasm_economy` gives `provisions`: `lacking`, and each hut's
  stock.
- **A debug edit**: `CHASM_CMD_STOCK` (`chasm_economy` op `set_stock`), a building's stock of a good set - a
  camp's pantry with `pantry` - recorded. A building placed on the same tick is not the economy's until its
  next tick; the op says so then.
- **Tested** (seed 12, a builders' hut and a store placed standing, the colony in the camp):
  - Wood only, before and after, the same sites painted as a player: identical every 250 ticks for 3,000.
  - With a TEMPORARY cost (a store's storey 2 provisions), taken out again:
    - with no site wanting any, nothing made and nothing at the hut;
    - a store site wanting 2: the builder fetched 2 wheat, then 2 beans, made 2, and the site's carriers took
      them; the site stood; nothing left at the hut or in a store;
    - a second site erased while its provisions were being made: what the hut held went to the store;
    - with no crops in the camp or any store, a hungry villager went to the store with provisions and ate;
      with wheat in a store three times as far, another walked past the provisions to the wheat; nobody
      fetched provisions home, and the camp's pantry never held any;
    - posted beside the store, a hungry man ate its provisions, nearer than the camp's wheat, and packed one;
    - a save and load kept the stores' provisions and his pack. The recording, 5,613 ticks: two replays equal
      on every tick.
  - The store card's chips: six, on two rows; the provisions chip pressed off and on through the overlay.

## As built: step 6, the guard tower (2026-10-10)

Files: `Zones.*` (`ZONE_KIND_TOWER`, `ZoneKindPaintStoreys`, the wall's plot taken, a storey less taking it whole),
`Economy.*` (`WORKER_JOB_GUARD`, `ECONOMY_TOWER_*`, `EconomyPosted`, `EconomyPostPlot`, `EconomyPostPoint`,
`EconomyAtPost`, `EconomyOnTower`, `EconomyState::door`; `AssignJobs`, `Derive`, `Tick`, `StoreyGoods`, `StoreyKept`,
`Build`, `Needs`, `SeekSupplyOf`, `GoHome`, `GoToPost`, `Replan`, `TickWorker`), `BuildingMesh.*` (`BuildTower`,
`BuildingTowerDeck`), `RoadMesh.cpp` (the palisade against a tower), `ApplicationChasm*` (tool T, the build bar, GUARDS
in the jobs corner, the cards, the ghost, guards drawn on the platform, NOBODY AT THE TOWER, `chasm_economy`'s
`towers`).

- **The kind**: `ZONE_KIND_TOWER` (11), one plot, painted WHOLE - both storeys at once (`ZoneKindPaintStoreys`); a
  click on one adds nothing, and taking a storey off takes it all. A building's rules, and IN A WALL'S LINE: on a wall
  plot, planned or standing, it takes the plot (its ground goes). Never on a road. A building stops walkers, so the
  wall stays shut; a guard walks in from either side, as anyone walks into his own house (since tuned: only by its
  ladder, below).
- **The look** (since tuned, below: its junction's shape) (`BuildTower`, drawn apart from the house bodies like the winch): a square of upright logs round the
  plot's point, a band of dark lashing at each storey's top, a thick post at each corner; whole, a platform of planks a
  little wider than the walls, a parapet of sharpened stakes round it - the palisade's - and a ladder up the first of
  its south, east, west and north sides with no wall running off that way. No roof. Square to the world. A wall beside
  it runs on past its half-way point up to the tower's side (`RoadMesh`), and a gate beside it hangs its leaf from it.
- **Built with** (since tuned, below: so much a guard place) wood by its floor area (as any building) and 12 water and
  8 provisions, half a storey (`StoreyGoods`) - the provisions the first thing that makes caretakers make them. What a
  storey takes of those it KEEPS as its stock (`StoreyKept`), so a built tower holds all of it. Placed in debug it
  stands full (`Tick`, for a tower new to the economy that stands whole).
- **Its places**: 2 (since tuned, below: by its junction), once it stands whole. Staffed in the one order like any workplace, from the
  free: by ARCHERY first - what the range trained (`range_plan.md`) - then by precision against the walk, as any
  place. `WORKER_JOB_GUARD` (5).
- **A guard** is POSTED by his job (`EconomyPosted`): from home he packs and goes up (`TakePost`), at the end of any
  errand he goes back up instead of home (`GoHome`), and he runs no errands. His post is the tower's plot
  (`EconomyPostPlot`, via `EconomyState::door`); his place on it by his rank among its guards (`EconomyPostPoint`) -
  first west and east of its middle, since tuned toward its corners (`ZoneTowerPlace`). An order to him comes first and suspends the job like any other.
- **The tower is his pantry** (`Needs`): standing on it he eats and drinks from its stock as at home - a whole unit
  whenever it fits - and from his pack only a kind the tower has none of. (First built eating the pack after the
  tower, a bean fitted where a provision did not and came out of the pack.) Tower and pack empty, a pressing need
  sends him to the nearest supply - towers count, for the posted - and back up.
- **Nobody else's**: `SeekSupplyOf` passes towers by for anyone not posted; no carrier, family or caretaker takes
  from one (they take only from the kinds they name); a tower takes no deliveries yet (since step 7: from the
  caretakers, below). The top bar's
  stored goods leave towers out.
- **Losing the job** - the tower pulled down, its places gone: off the tower and home, on the next tick.
- **Shown**: the tower card - its guards with their archery and precision, "1 of 2 guards" amber while short, its
  stock as a kept strip ("its guards'"); a guard's badge is the spear, his card "Guard on the tower 5: lives on its
  stock", what he does "going up the tower", "on watch on the tower", "cut off from the tower"; guards drawn standing
  on the platform; NOBODY AT THE TOWER in NEEDS ATTENTION; GUARDS in the jobs corner. `chasm_economy` gives `towers`
  (places, guards, stock) and a worker's `post` with `ordered`.
- **Tested** (seed 12, a builders' hut and a store placed standing by the camp, the camp given food for the run):
  - Archery first: the two best by precision (9, 8) posted out of the way, the next (precision 6) trained at a
    one-cell range to 7.8% archery, the range erased, the two released - a tower placed in debug took the trainee
    and the 8.
  - Placed in debug: 12 water and 8 provisions, 2 places. Both guards went up and stood at their two places; with
    their bars set low they ate and drank from the tower on that tick, their packs untouched, and never left it.
  - With every other water emptied, a thirsty villager never went to the tower. The tower emptied, a guard drank his
    pack down, went to the store for water and came back up.
  - On a standing wall plot: taken, the wall beside it standing; on a road: refused, "a road is there".
  - Built in play: wood 20, water 12, provisions 8 wanted; the first storey stood keeping 6 water and 4 provisions,
    the whole at tick 6,000 with 12 and 8 - the caretaker made all 8 - and took two guards. (A first run without
    the extra food stalled after the first storey: a colony with no fields ate the last wheat and beans first.)
  - Erased with its guards on it: both jobless and on their way home on the next ticks.
  - A save and load kept the towers' stock and their guards. The recording, 13,437 ticks: two replays equal on
    every tick.
  - Seen in the game: the palisade running up to the tower's sides, the ladder on a side clear of the wall, guards on
    the platform behind the parapet.

## As built: the tower tuned (2026-10-10)

The user's, after step 6 (Decided, above): it stood out on this grid by always being square. Files: `Zones.*`
(`ZoneTowerGuards`, `ZoneTowerCorners`, `ZoneTowerPlace`, `ZoneTowerLadderOk`, `ZoneTowerDefaultLadder`,
`ZoneTowerLadder`, `ZoneIsLadderPlot`, `ZoneBuilding::ladder`, `ZONE_OP_TOWER_LADDER`, the rules), `Walkers.cpp` and
`EconomyStraightClear` (the door), `Economy.*` (`ECONOMY_TOWER_*` a guard place, `TowerStoreyCost`, `EconomyTowerStock`,
the places), `BuildingMesh.*` (`BuildTower`, `BuildingTowerReach`, `BuildingOnTower`), `RoadMesh.cpp`, `ChasmSave.cpp`
(`"ladder"`), `ApplicationChasm*` (the hash, the tool, the card's chip, the ghost).

- **Its shape is its junction's** (`ZoneTowerCorners`): a polygon through the middles of the cells round its plot's
  point, `ZONE_TOWER_INSET` of the way - a triangle, a square (skewed as the grid is), a pentagon, a hexagon - a side
  facing each neighbour, so a wall running off meets a side (`BuildingTowerReach`). Its size is its plot's: the 3s are
  often small plots (one by the seed-12 camp is 1.0 against a 4's 3.8 and a 5's 6.9), so a one-guard tower is slim.
- **Its guards are its junction's edges less two** (`ZoneTowerGuards`): 1, 2, 3 - and 4 on a 6, the user's. About 96%
  of plots are 4s, 2% each 3s and 5s, 0.2% 6s; the ghost's shape is how the player finds them. Guards stand toward
  its corners, spread round it (`ZoneTowerPlace`, `ZONE_TOWER_STAND`) - one alone in the middle.
- **Its cost is so much a guard place** (`TowerStoreyCost`): `ECONOMY_TOWER_WOOD` wood, `ECONOMY_TOWER_WATER` water,
  `ECONOMY_TOWER_PROVISIONS` provisions, half a storey - wood too, not by floor area - so no junction is cheaper for
  what it holds. Its water and provisions are its stock (`EconomyTowerStock`): 12 days for each guard, whatever its
  shape.
- **Its ladder is its door** (`ZoneBuilding::ladder`, the neighbour plot it stands toward): walkers get onto the tower
  only from that plot and off only to it (`Walkers::EdgeOpen`, `EconomyStraightClear`) - its site's builders too. Placed,
  it takes the open side nearest the camp (`ZoneTowerDefaultLadder`) - one ground, dry, not too steep, no building, wall
  or gate (`ZoneTowerLadderOk`); a tower with no such side is refused, "no open side for its ladder". MOVED from the
  tower's card (`OVERLAY_HIT_LADDER`, "Move ladder") - a recorded zone command, `ZONE_OP_TOWER_LADDER`, to the next open
  side round it, or `chasm_paint` op `tower_ladder` toward a neighbour. Saved (`"ladder"`), hashed (towers only).
- **Kept clear**: nothing built, and no wall drawn (a gate there could be shut), on a ladder's plot - "a tower's ladder
  stands there" (`ZoneIsLadderPlot`), as on a winch's landing. The ghost shows the ladder where placing would put it,
  and a wall's ghost goes red on a ladder's plot.
- **Tested** (seed 12, debug placements round the camp):
  - Towers found on 3-, 4-, 5- and 6-junctions: 1, 2, 3 and 4 guards; each stood full at 6 water and 4 provisions a
    place. A play site on a 4-junction wanted 20 wood, 12 water, 8 provisions.
  - Every ladder stood toward a neighbour on the camp's side. A route onto a tower from its far side came off its
    ladder's plot; the ladder moved (by the op, and by its chip pressed through the overlay), the route came off the
    new one. A house and a wall on the ladder's plot: refused.
  - The 5-junction tower's three guards up at three places of their own. A save and load kept every ladder; the
    recording's two replays equal on every tick.
  - Seen in the game: the square, pentagon and hexagon in the grid's shape, guards on them, ladders on the camp side;
    the triangle slim on its small plot.

## As built: step 7, the towers kept (2026-10-10)

Files: `Economy.*` (`KeepTowers`, `TowerWhole`, `TowerLeft`, `EconomyTowerShort`, `EconomyComing`; `ProvisionsLacking`,
the fetch and the delivery in `TickWorker`), `ApplicationChasmSelect.cpp` (what a carrier is doing, the tower card),
`ApplicationChasmIssues.cpp` (the row), `ApplicationChasmEconomy.cpp` (`towers`' `short` and `coming`).

- **Short** is below FULL - what it is built with (`EconomyTowerStock`), so much a guard place - for a tower that stands
  whole (`TowerWhole`); a tower still a site wants its storeys' goods as any site. What it still wants brought
  (`TowerLeft`) is what it lacks less what is on its way, reckoned as a site's: a load for each carrier going to fetch
  for it, what each carries there (`EconomyComing`, which `SiteLeft` now uses too).
- **The caretaker's first errand** (`KeepTowers`, from home, before `MakeProvisions` and building): of every tower short
  and every place with water or provisions to spare - a store, the camp's supplies, a house nobody lives in, a builders'
  hut's provisions, a crate - the pair with the shortest walk by the straight line, him to the good to the tower; the
  first of the nearest four he can walk. He goes AS FOR A SITE: `site` the tower, `WORKER_TO_FETCH`, `FETCHING`,
  `WORKER_TO_SITE` - so the reservations (`SiteReservation`, `GoodFree`, the cards' stripes) and re-planning hold as
  they are, and nothing new is saved. At the tower the load goes into its STOCK, never past full; what it no longer
  lacks goes to a store; and he goes home - no building time. Up the ladder, like anyone onto a tower.
- **Provisions nobody has are made for it**: `ProvisionsLacking` counts a tower short of them, so `MakeProvisions`
  makes them at the hut, and `KeepTowers` carries them from there once made - the plan's "from a store, or made for it".
- **Builders only**: `FindSiteWork` never lists a tower that stands, and at the fetch a tower that stands wants nothing
  from anyone else - a jobless carrier whose tower site was finished on his way takes nothing. A load already carried
  for it goes in, whoever brings it.
- **Shown**: a carrier is "fetching water for a tower", "carrying food to a tower" - its site or its stock. The tower
  card under its stock: "Full - the builders keep it so", "Short of 12 water and 8 provisions: the builders are
  bringing it", or amber "..., and no builders to keep it full". NEEDS ATTENTION: TOWER: NO BUILDERS TO KEEP IT
  (`tower_unkept`), for a whole tower short with no builder in the colony. `chasm_economy`'s `towers` give `short` and
  `coming` once one stands whole.
- **Tested** (seed 12, a builders' hut - one builder - and a store placed standing by the camp, the camp given food;
  a 2-guard tower placed in debug, its guards up on it):
  - Emptied (12 water and 8 provisions to 0): 8 provisions wanted at once. The builder carried the water, made all 8
    provisions at the hut from the camp's wheat and beans, and carried them up: full again after 10,000 ticks, about
    4.4 days, most of it the making. Never past full; nobody but the builder carried for it.
  - Its water emptied again as a house site was painted in play: the builder went for the tower first, the jobless
    for the house, none to the tower; full of water again.
  - A save and load with the builder on his way up with 5 water: still on his way, carrying it, and it went in.
  - The hut erased, no builders: an emptied tower left empty, nobody carrying to it; NEEDS ATTENTION listed it.
  - The recording, 12,820 ticks: two replays equal on every tick.
  - Seen in the game: the card's amber "Short of 12 water and 8 provisions, and no builders to keep it full", and
    the row at the top of NEEDS ATTENTION.
- **Open, tuning**: kept FULL means a trip for whatever is short, even a unit its guards drank; a threshold below full
  is the plan's answer if builders spend too long on it (above). A single builder filling an empty tower from nothing
  takes days, most of it the provisions - another reason a colony with towers wants a second hut.

## As built: step 8, guards by hand (2026-10-10)

Files: `Economy.*` (`EconomyState::guards`, `EconomyWorker::f_chosen`, `ECONOMY_TOWER_GUARDS_PLACED`, `PostGuard`,
`SetGuards`, `EconomyTowerUp`; `Derive`, `AssignJobs`, `TickWorker`, the save and `Restore`), `ChasmSave.cpp` (`"guards"`,
`"chosen"`), `ApplicationChasm.h` (`CHASM_CMD_GUARD`, `CHASM_CMD_GUARDS`, `OVERLAY_HIT_TOWER_GUARDS`,
`OVERLAY_HIT_GUARD_RELEASE`), `ApplicationChasmEconomy.cpp` (the commands, the hash, the tool's ops),
`ApplicationChasmSelect.cpp` (the right click, the cards, the chips), `ApplicationChasmIssues.cpp`.

- **The count** is the economy's, by building id (`EconomyState::guards`, -1 for anything not a tower): saved as
  `"guards"`, hashed. `Derive` gives a new tower `ECONOMY_TOWER_GUARDS_PLACED` (1) and holds every count to its
  junction's places; a tower's `places` ARE its count once it stands whole, so the staffing, the jobs corner's GUARDS
  and NOBODY AT THE TOWER follow it with no change of their own. A save from before the counts gives its towers every
  place, as they had (Restore's -2). Set by `CHASM_CMD_GUARDS` (`SetGuards`), its places at once.
- **A tower standing whole** is now asked of the zones (`EconomyTowerUp`) wherever `places > 0` used to say it - the
  card's kept-full line, the issues row, the tool's `short` - since a count of 0 makes places 0 on a tower that stands.
- **Hand-picked**: `EconomyWorker::f_chosen`, saved as `"chosen"`, hashed only when set. `AssignJobs` keeps him with
  the posted, ahead of the auto-fill - his place reserved, counted out of the free - so the auto-fill never moves him
  and a shortage of people takes the automatic guards first. He loses it only with his tower, or its count lowered
  past the hand-picked (the lower person ids kept). On the tick after, `TickWorker` sends him up as it sends someone
  ordered (`TakePost`): what he was doing he drops; an automatic guard still finishes his errand first, as before.
- **`PostGuard`** (`CHASM_CMD_GUARD`, value 0 the tower): refused - "too young", "caring for her baby", "no tower
  there", "the tower is not built yet" (a site, or one placed this tick), "no house to go out from", "no way there",
  "every place is taken" (all hand-picked) - refusals shown as an order's, CAN'T SEND: ... His order, if he had one,
  gives way; a load on him is put down as a crate. Already its guard: only marked his pick. A free place under the
  count: his. None: the count up by one while it has places to spare; every place taken: the automatic guard with
  the least archery, then precision, loses the job and goes home for him. RELEASED (value 0): the count drops to the
  guards it has less him, so nobody follows him up; he goes home jobless, free for any other work.
- **Shown**: the tower card, once it stands - "Guards it takes" and a chip for each count from 0 to its places, a
  "Release a guard" row with a chip for each guard by first name (his id the chip's value), "by hand" by a hand-picked
  guard, "Unmanned: it takes no guards" at 0, and "Right click it with someone selected to post him here". A guard's
  own card: "posted by hand", and a "Release from the tower" chip. The right click with a person selected: on a tower
  - the one the cursor picks, or on its plot - a `CHASM_CMD_GUARD`, else an order as before. `chasm_economy` ops
  `guard`, `release_guard`, `guards`; a tower's `count` and `junction_places`, a worker's `chosen`. `chasm_select`'s
  `under` gives a chip's `overlay_value`.
- **Tested** (seed 12, a builders' hut and a store placed standing, the camp given food; a 4-junction tower, 2 places,
  placed in debug):
  - Placed: count 1, places 1 of 2; one guard went up. Count 2: two guards; 0: both off home, no NOBODY AT row; 1
    again: one guard.
  - By hand with a place to spare: the count rose to 2, he was marked hand-picked and went up, the automatic guard
    stayed. The count lowered to 1: the automatic guard went, the hand-picked one stayed.
  - Full, one of each: by hand, the automatic guard went home and the count stayed 2. Full of hand-picked: refused,
    "every place is taken". A child: "too young".
  - Released by the tool: jobless, home, the count down to 1, nobody up in his place. A tower site: "the tower is not
    built yet" (first "no tower there" for one placed that tick - fixed, it asks the zones first).
  - Through the overlay, as clicks would (found by `chasm_select`'s new op `hits`, every chip with its place): the card's
    count chip 2 pressed, two guards again; a guard's Release chip pressed, he went and the count fell to 1; a right
    click on the tower with a person selected, he was its guard by hand and the count rose to 2. The long card stands
    left of the tools' grid, over the scene's middle (the card rule for long cards).
  - A save and load kept the count, the guards and who was hand-picked. The recording, 785 ticks: two replays equal on
    every tick.
  - Seen: "Guards it takes 0 1 2", "Release a guard - Marit", the refusal on the overlay, CAN'T SEND: THE TOWER IS NOT
    BUILT YET.

## Later: agreed in the gameplay plan, not planned yet

- **The training ground**: a workplace where archers train, as a SHARE of a worker's time, by precision
  and experience (`gameplay_plan.md`, "People"). The time split is a change to jobs, which today are one
  per person.
- **The alarm**: one button sends everyone with a post - and the trainees - to their posts and shuts the
  gates, and another sends them home.
- **Several people selected** and ordered at once - agreed 2026-10-11 (`selection_plan.md`, "Several people
  selected").
- **The zombies** (P6), fighting, the ballista tower, moats.

## Scouting, provisioned first (the user's, agreed 2026-10-10, not built)

The user: when you post someone and take them on a scouting mission, you can tell them to **first gather
provisions and water**. They carry them along and eat while underway. Proposed and talked through the
same day; the user's answers are under "Decided" below.

**What it builds on, today:**

- **An order can already go under the clouds.** `Economy::Order` does not refuse an unexplored plot, and
  the person clears the clouds as they walk (`crates_plan.md`, "Random crates: finds": finds are the
  reward for scouting).
- **They already eat underway.** The pack is eaten every tick while posted (`Needs`, `EconomyPosted`) - on
  the way out as well as at the post.
- **But the pack is filled only from home**: their house's pantry or the camp's supplies, and only if the
  order finds them at home (`TakePost`). An order given while they are out sends them with the pack as it
  is. No home holds provisions, so a scout usually sets out with water and crops.
- **And it is small**: 3 water and 3 days of food, about 8.7 days before the first pressing need ("Why the
  pack and the tower" above). Then they walk to the NEAREST supply and back out - from deep in the clouds,
  most of the way home and out again.

**Decided** (the user, 2026-10-10):

- **A mission is a post, as it is now**: a temporary assignment, given by the right click and ended by
  Release. There is no separate scout. The player would normally pick an archer for it, but nothing
  limits it to one.
- **Asked for with a chip on the person's card.**
- **They take as much as they can carry**, more than the usual pack (`NEEDS_PACK_*`), which stays what an
  order without the chip packs.
- **From wherever they would normally get it**: their home, a store - the places a pressing need sends a
  posted person to (`SeekSupply`). Provisions first where there are any, as `FillPack` does now.
- **With none to be had, they do nothing and a message pops up**: a line at the bottom in the middle, as
  for a death or a refusal (`needs_plan.md`, "As built: step 5, deaths told").
- **When the supplies run out, only a message appears.** They do not turn back by themselves. They leave
  the post only when released, or as now, when a need gets pressing: to the nearest supply and back.

**To settle when it is built** (the details, not the rules):

- **Where the chip sits** (proposed): on a posted person's card, beside Release. Pressed, they go to fill up
  first and then on to the post, wherever they were - just ordered, on the way or standing there.
- **How much "as much as they can carry" is** in units: a named constant beside `NEEDS_PACK_*`, water and
  food in the pack's balance so both run out together ("Why the pack and the tower").
- **A second stop** when the first place has only water or only food. With something but not everything to
  be had, they take what there is - the message is for nothing at all.
- **The messages' wording**, for instance "NO FOOD OR WATER FOR JELLE DE WIT TO TAKE" and "JELLE DE WIT'S
  SUPPLIES HAVE RUN OUT".

The card shows the pack as a strip (`selection_plan.md`, "What a person carries, as a strip"), which is how
the player sees how long a scout can stay out.

**Build order** (2026-10-10): first, something for a scout to take - provisions kept in the stores, made to a number
the player sets or to an order (`goods_plan.md`, "Kept in stock, and made to order"; rope from the ropewalk the same
way) - **built and checked the same day**. Then the chip, the bigger pack and the second stop; then the two messages.

### As built (2026-10-11): the chip, the scout's pack, the second stop - and the pack on the way home

**First, a fix the user found** testing it: someone RELEASED did not drink from his pack. The pack was eaten from only
while posted (`Needs`), so a scout called back walked home with water on his back and got thirsty - and a pressing
need then sent him to the nearest supply instead. Now anyone with something packed lives on it, after his pantry if
he is home, until `Unpack` puts it there. It had nothing to do with having no job. Checked: released 100 units out
with 1 day of water, he drank from the pack at once and again on the way, ate a bean, and was home with his bars up.

The details left to settle, as settled:

- **The chip**: "Gather supplies first", a toggle under Release on a posted person's card (`OVERLAY_HIT_GATHER`),
  lit while he is at it; his line says "gathering supplies", then "packing supplies". Pressed again he goes
  straight on - turning for the post at once if he is on his way to a place for it, unless a pressing need takes
  him there anyway. Only for someone ordered out (`post`), not a tower's guard.
- **As much as he can carry**: `NEEDS_SCOUT_WATER` 6 and `NEEDS_SCOUT_FOOD` 9 days - three provisions - at the
  pack's balance: about 14.7 days before water presses and 14.25 before food does, against 8.7 for the usual pack.
  `FillPack` fills to it while gathering.
- **Where from**: `SeekSupplyOf` as for a posted person's pressing need - his home, a store, the camp's supplies, a
  collector, a field's harvest, a crate - the nearest with some of what he lacks, provisions first there.
- **The second stop**: `ECONOMY_GATHER_STOPS` 2 places in all; at the second, what the first lacked.
- **With nothing to be had**, or nowhere he can reach, his gathering is dropped and he goes on as he was - silent
  until the messages (next).

Code: `EconomyWorker::gather` (places still to go to; saved, hashed only while above 0), `Economy::Gather`,
`GatherSupplies`, the hook in `TickWorker` (on his way or on his post) and the second stop at `WORKER_SUPPLYING`;
`Order` releasing clears it. `CHASM_CMD_GATHER`, refused as an order is; `chasm_economy` op `gather`, and `gather`
on a worker.

**Checked** (debug, a copied exe on 8774, the default seed): ordered out with the usual pack (3 water, 2 beans), told
to gather - back to the camp, the nearest with both, and on with 6 water and 5 beans. With the camp's food gone and
a store holding only provisions: 6 water at the camp, then 3 provisions at the store, then the post. Refused for
someone not sent out; nothing done for a scout already full; with no water or food anywhere, on to the post. The
chip pressed on the card (`chasm_select` op press): lit, "gathering supplies"; pressed again on the way - turned for
the post at once. A 3,513-tick recording of the turn-off, a full gather and a release, replayed twice: every tick
equal.

**Next**: the two messages - nothing to take, and a scout's supplies run out.

### As built (2026-10-11): the two messages

- **NOTICES** (`Economy.h`): made by the rules - `EconomyState::notices`, the last `ECONOMY_NOTICES_KEPT`, and
  `notices_made` - so a replay makes the same ones on the same ticks; never saved or hashed, since telling changes
  nothing. The overlay tells the newest it has not told (`notices_seen`), in the refusal's place at the bottom in the
  middle, amber, for 5 seconds as a death is (a refusal has 3) - only one of the last half day.
- **"NO FOOD OR WATER FOR KAAT DE WIT TO TAKE"** (`NOTICE_NOTHING_TO_TAKE`): told to gather and nothing anywhere he can
  get to - only when he took nothing at all, so `TakePost` counts his home as his first stop when it gave him
  something, and a second stop finding nothing is silent. `GatherSupplies` says which: nothing lacking, or none.
- **"JELLE DE WIT HAS RUN OUT OF WATER"** (`NOTICE_RAN_OUT`; "of food", "of food and water"): the tick his pack goes
  from holding both to lacking one or both - once a fill, since a pack lacking one stays so until topped up with
  both. Everyone ORDERED OUT, not only those who gathered: a scout is just a post (Decided). Not a tower's guard,
  whose tower is his pantry, nor anyone released.
- `chasm_economy` reports `notices`: `made` and the `last` few.

**Checked** (a copied exe on 8774): with no water or food anywhere, Kaat sent out and told to gather - the notice on
that tick, the message on screen; supplies back, Jelle sent out with 3 water and made to drink it dry - "HAS RUN OUT
OF WATER" on the tick the last went; Kaat told again - a scout's pack, 6 water and 5 beans, on her card as a strip
(`selection_plan.md`). Recorded and replayed twice: every tick equal, the same two notices on the same ticks.

**Open**: with several people posted near home, a "run out" every eight days or so each may be more telling than
wanted - narrowing it (to those who gathered, or to posts under the clouds) is the user's call.
