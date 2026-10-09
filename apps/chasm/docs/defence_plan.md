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

1. **Orders** - BUILT 2026-10-09, below: the post, the two states, right-click and release, the card,
   needs and back to the post. Tested: an ordered person stays, leaves when a need is pressing, eats, and
   comes back; his workplace stands idle and he returns to it on release; an ordered jobless person is
   given no job while a place stands empty, and takes it once released; a save and load in the middle
   gives it all back.
2. **The pack** - BUILT 2026-10-09, below: filled at home and at supply trips, eaten from. Tested against
   the sums above.
3. **Gate doors** - BUILT 2026-10-09, below: the state, the look, walkers closed, the card and command. This step depends on none
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
- **A load on him when he is ordered is LOST** for now (the user, 2026-10-09; `Economy::Order`). It was
  held on his post and taken to a store after release; to come instead: he DROPS it, left on the ground as a
  CRATE holding the load - the start of crates lying on the map at random, as They Are Billions' bonus
  resources. Handed to its own plan (a crates plan, being written up by another window).

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
  order, until crates); his tree, site or store is let go.
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
- **Filled** (`FillPack`) when he sets out for his post from home - his house's larder or his camp's supplies,
  as he drops what he was doing (`TakePost`) - and at the end of every supply trip, from what is there. Water
  first, then food, the kind that keeps best first (beans, wheat, greens), a whole unit more while still short
  of the mark: from a camp, 2 beans - 4 days.
- **Eaten** in `Needs`, every tick while posted, by `Consume` - a whole unit whenever it fits, as at home. So
  his bars stay up while it lasts, and a pressing need sends him off only once it is empty.
- **Home again** after release, the at-home look puts it back into the larder (`Unpack`) as far as the house
  keeps each good and has room; a camp takes it all. What does not fit stays packed, for the next post.
- **A load on him at the order is lost** (`Order`), as the user asked, until crates hold what is dropped.
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

## Later: agreed in the gameplay plan, not planned yet

- **The training ground**: a workplace where archers train, as a SHARE of a worker's time, by precision
  and experience (`gameplay_plan.md`, "People"). The time split is a change to jobs, which today are one
  per person.
- **The alarm**: one button sends everyone with a post - and the trainees - to their posts and shuts the
  gates, and another sends them home.
- **Several people selected** and ordered at once.
- **The zombies** (P6), fighting, the ballista tower, moats.
