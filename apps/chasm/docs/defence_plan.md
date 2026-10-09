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
- **On a tower, the tower is his larder** (the user's): he eats and drinks from its stock exactly as he
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
- **Release**: a button on his card sends the same command with no plot. Tool form: `chasm_order`.
- **His job while he is ordered**: kept and suspended. The workplace stays his and stands idle - nobody
  else takes it - and on release he goes back to it. That is the gameplay plan's "trainees drop their work
  and join them", and an idle hut is the visible cost of defence. The other way, giving the job away,
  would reshuffle jobs every time an order ends.
- **Ordered people are out of the staffing altogether** (`AssignJobs`, `builders_plan.md`): a jobless one
  is given no job - nor a tower's place - until he is released (the user's), and a working one's place
  is neither taken from him nor counted free, even if a new builders' hut would otherwise free it.
  Released, he is staffed like anyone else on the next pass.
- **One person at a time** for now. Selecting several is a later step.

### The pack

- **Per person, what he carries for himself**, apart from any load: water and food, by good. Saved,
  hashed. Lost with him if he dies, like a load.
- **Its size** (tuning, agreed): **3 water and 1 provision** - or its 3 days as other food: 3 wheat, 6
  greens, 1-2 beans - balanced so both run out together, and under a load. Named constants with the other `NEEDS_*`/`ECONOMY_*` values.
- **Eaten as at home**: a unit whenever the whole of it fits (`Consume`), so his bars stay near full while
  the pack lasts.
- **Filled**: when he sets out for a post from home, from his home's larder - no detour, and
  the family's fetching makes it up; a home never holds provisions - and at every supply trip, wherever
  he eats, provisions included. An order given while
  he is out sends him straight there with the pack as it is: an order may be urgent.
- **Only posted people use a pack** for now.

### Provisions

- **A new good**, `GOOD_PROVISIONS` (appended to `Goods.h` - never renumber). One is made from **a wheat
  and a beans** and is **worth the two together**: 3 days of health (`EconomyFoodWorth`: 1 + 2). It keeps
  long, for when spoilage comes.
- **A bit of its own, kept OUT of `GOODS_FOOD`.** Everything villagers do with food today - fetching
  home, a house's larder, fair shares, a home's days of food - then ignores provisions with no change, and
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
- **Its guards**: places like any workplace (2, tuning), filled in order like the others, by
  **precision** (the archer's skill). A new job, `WORKER_JOB_GUARD`. A guard's home stays his house.
- **Its stock**: it keeps provisions and water - provisions are its food, and a guard still drinks.
  **Only posted people draw on it** - its guards, as their larder, and anyone under an order nearby, as a
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

1. **Orders**: the post, the two states, right-click and release, the card, needs and back to the post.
   Tested: an ordered person stays, leaves when a need is pressing, eats, and comes back; his workplace
   stands idle and he returns to it on release; an ordered jobless person is given no job while a place
   stands empty, and takes it once released; a save and load in the middle gives it all back.
2. **The pack**: filled at home and at supply trips, eaten from. Tested against the sums above.
3. **Gate doors**: the state, the look, walkers closed, the card and command. This step depends on none
   of the others and can be built at any point, by another window if one is free.
4. **Site costs per good**: construction carrying food and water as well as wood. General - stone and
   the rest will use it.
5. **Provisions**: the good, its own bit, the eating rules (posted: nearest of any food; villagers: last
   resort), the store chip, and caretakers making them at the hut. Tested: made only when wanted;
   villagers eat them only with nothing else in reach and never fetch them home.
6. **The guard tower**: the kind, its mesh, placement including the wall's line, its places and the guard
   job, its stock for posted people only, built with provisions and water.
7. **The caretakers' resupply**: builders keep towers full, ahead of building.
8. **A right-click on a tower** posts the selected person there, if it has a free place.

## Open

- **Cold on a post in winter**: proposed, warm or cold by his home as now. It may not matter while no
  zombies come in winter.
- **How long a provision takes to make** - tuning, like every other number here, which are first
  figures for playtesting to move (`needs_plan.md`, "The numbers are tuning, not rules").
- **The tower's roof**, and its key on the build grid.
- **Releasing a tower's guard** by an order: the order would suspend the tower job like any other.

## Later: agreed in the gameplay plan, not planned yet

- **The training ground**: a workplace where archers train, as a SHARE of a worker's time, by precision
  and experience (`gameplay_plan.md`, "People"). The time split is a change to jobs, which today are one
  per person.
- **The alarm**: one button sends everyone with a post - and the trainees - to their posts and shuts the
  gates, and another sends them home.
- **Several people selected** and ordered at once.
- **The zombies** (P6), fighting, the ballista tower, moats.
