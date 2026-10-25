# Zeppelin flight

Talked through with the user 2026-10-24: step 6 of `steam_plan.md`, which built the zeppelin on its dock and left
flying it to a plan of its own. What came before it: `gameplay_plan.md` gives flight its jobs (the islands'
floatstone, the far side, the desert beyond), `steam_plan.md` decided a zeppelin floats only over the chasm and is
filled with steam once, not fuelled, and `settlements_plan.md` decided porters will work much as a zeppelin's route
will. Steps 1 to 3 are built ("As built" below): the zeppelin flies, carries people (2026-10-24), and moors at masts
to be loaded and unloaded (2026-10-25).

What flying is for, in the order the plan reaches it: **the islands** - floatstone for the next zeppelins and the
best steam, several steps down; **supplying who lives there**, since an island, like a balcony, grows no food; and
**the far rim**, the second village and the walk on to the desert.

---

## Decided (the user, 2026-10-24)

- **The proposals as they stand** ("already quite perfect"): the zeppelin a vehicle in the world, not a ride; it
  explores from the air; routes later, with porters; no fuel; no losing one at first; the look; the build order -
  with these answers:
- **Ordered by click**, as people are: select it, right-click where it is to go.
- **People get in and out as at a winch, but only when ordered.** So away from a mast nothing goes aboard but the
  people and what they carry - their provisions and a tent if they packed one.
- **Goods are delivered to it at a MAST.** Unloaded, its cargo goes **into the mast's store** when it is moored at
  one, and is **dropped as a crate** anywhere else.
- **No pilot.** It needs nobody to fly it.
- **It flies always, winter too.** Lift by the chasm's warmth was too much for now.
- **The dock stays a building site**: once its zeppelin is finished, its card gets a button to begin a new one.
- **The dock is also a mast**, functionally: goods are delivered to the zeppelin there and unloaded into its store.
- **Passengers' packs run down as on a post, and the cabin is their pantry**: the zeppelin's cabin holds stores, and
  they refill from it as a guard does from his tower.

---

## The zeppelin in the world

A winch is a link in the walk graph, always open. A zeppelin is at one place at a time, so it is a thing of its
own in the rules: where it is, its heading and height, where it is bound and what it is doing (moored, flying,
hovering, set down), who is aboard and what it carries - saved, hashed and moved by the tick, as a walker is.

- **Where it flies**: over the open chasm (`ChasmRiftDistance` inside the rift), and over land only on its last leg
  into a dock, which stands back from the edge. Its path is found on the rift's raster and smoothed, and it turns at
  a rate, so it swings round the chasm's bends rather than snapping.
- **Its height**: it cruises over the rim, so no island stands in its way, and comes down to a place's height to
  set down or moor.
- **It sees from the air**: it clears the clouds as it goes, by the same line of sight people have, from its
  height - so the first flight maps the chasm. Like an order, it is not held to the clouds: flying under them is how
  they are cleared.
- **Winter, storm and snow** change nothing about it, for now.
- **It belongs to the colony**, not a settlement: it moors at any dock or mast with room.

## Ordering it

- **Selected** with a click, outlined as a building is, with a card: where it is and where bound, who is aboard,
  its cargo and load list, and its buttons.
- **A right click** sends it:
  - over the open chasm: it flies there and **hovers**;
  - on an edge plot over the open chasm - the rim, a balcony, an island, the far rim: it **sets down** beside it,
    hovering at the edge with a rope ladder down;
  - on a dock or mast with its berth free: it **moors** there.
  Anywhere else is refused, saying why.

## People

- **Aboard by order**: select people, right-click the zeppelin. They walk to it where it is set down or moored and
  climb aboard; refused when they can't walk there, as any order is. At most `ZEPPELIN_PASSENGERS`.
- **Aboard, they are posted**: no job, no errands.
- **The cabin is their pantry**, as a guard tower is its guards' (`wiki/tower.md`): it keeps a stock of water and
  provisions, and they eat and drink from it as at home, their packs only for a kind it has none of. Climbing down,
  they top their packs up from it. Both empty, they wait aboard hungry - nobody leaves unordered - and the card
  says so.
- **Kept full** at a mast or dock as a tower is: below its stock, it is to that settlement's builders a site wanting
  water and provisions. This is the cabin's own stock, apart from the cargo.
- **Off by order**: a passenger selected (from the zeppelin's card), a right click on a plot by where it is set
  down - they climb down and stand there, posted. "Everyone off" on the card orders them all.
- **What goes with them** is what they carry: their pack and a packed tent. So an expedition founds a settlement on
  an island as on land - packed tents, flown over, set down, pitched (`wiki/founding.md`).

## Goods and the mast

- **A MAST** is a building at the chasm's edge: a mooring mast with a **store of its own**. It stands where a steam
  collector does, on the last plots at an edge over the open chasm - the rim, a balcony, an island, the far rim -
  and is built like any site, of wood and rope. Its store takes any good, its room by floor area as a store's.
- **Moored at a mast**, the zeppelin hangs over the drop beside it, its nose in the mast's cup.
- **The dock is a mast too** (the user's): its zeppelin is loaded there and unloads into the dock's store - a
  mast's, whatever the dock's size.
- **Loading**: the zeppelin's card has a **load list** - a good and an amount, up to `ZEPPELIN_CARGO` in all. While
  it is moored at a mast, that settlement's carriers bring what is listed from its stores, the mast's own first, as
  builders bring a site's goods.
- **Unloading**, by its button: at a mast into the mast's store, what does not fit left as a crate beside it;
  anywhere else, a crate on the edge plot it is set down by (`crates_plan.md`'s crates, picked up as any).
- **Routes** - repeated trips with a load each way - are a later step, together with porters and the depot
  (`settlements_plan.md`).

## The dock: another zeppelin

- Once its zeppelin is finished, the dock's card has **"Begin a new zeppelin"** - only while its berth is empty,
  its zeppelin away. The scaffold goes up and the dock is a site again, stage by stage as the first.
- **One zeppelin to a dock** at a time, built or building. A zeppelin bound for a dock that is building another
  moors at a mast instead.
- How many there can be is floatstone's to say: a home seam holds 50, a zeppelin takes 40.

## How it looks

- **Leaving**: the mooring lines drop, it rises, turns its nose - already toward the chasm - and goes.
- **In flight**: a slow bob and a little roll in the wind, the propellers turning (split out of `zeppelin_5` in
  Blender to turn), its shadow on the mist below.
- **Set down**: a rope ladder to the edge, people at its foot.
- **At a mast**: its nose in the cup, its lines down.

---

## Build order

Each step is playable on the one before.

1. **(BUILT 2026-10-24, "As built: step 1" below)** **It flies.** Selected, its card; sent over the chasm to hover,
   to an edge to set down, home to moor; the clouds cleared as it goes; saved and hashed. Its look: lift-off, bob,
   propellers, shadow. Tested on `saves/zeppelin_dock_test.json`: across the chasm and back, the clouds cleared on the
   way, saved in flight and reloaded.
2. **(BUILT 2026-10-24, "As built: step 2" below)** **People.** Aboard and off by order, the ladder, packs and tents,
   the cabin's stock kept full at the dock and eaten from aboard. Tested: four with tents flown to an island and set
   down, a tent pitched, a settlement there; passengers left hovering a day, eating from the cabin.
3. **(BUILT 2026-10-25, "As built: step 3" below)** **The mast and goods.** The mast, the load list, carriers loading at
   a mast or the dock, unloading into its store or as a crate. Tested: wood and rope dropped as a crate on an island, a
   mast there, moored at and unloaded into; loaded at the home mast, from a store and from the mast's own.
4. **A new zeppelin**: the dock's button.
5. **Islands worth going to**: floatstone seams on islands - "scarce" or "without end" is still to settle - and
   collectors there.
6. **Routes**, with porters (`settlements_plan.md`).
7. **Later**: the far rim's village, and losing a zeppelin - the winter storm (`weather_plan.md`) is the favourite
   cause, grounding it in winter as winter closes the swamp.

## As built: step 1 (2026-10-24)

The rules are `Flight.cpp` (Economy's members and the free functions beside them), the view
`ApplicationChasmFleet.cpp`; Economy.h's FLIGHT block says what it does. Page: `wiki/flight.md`.

- **A craft of its own**: `EconomyZeppelin`, in `EconomyState::fleet` (saved as `fleet`, `next_zeppelin`, and
  hashed). The dock's last stage standing makes one moored on it (`ZeppelinFinished`); a dock finished with none of
  its own in the fleet - a save from before flight - gets one on load, since nothing loses a zeppelin yet. A finished
  dock stays at its fifth stage: it builds no other until step 4's button.
- **Sent** by `SendZeppelin` (`CHASM_CMD_ZEPPELIN`, a right click with it selected, `chasm_economy op zeppelin`):
  a dock to moor on (`DockBerthFree`: standing whole, fitting, its own zeppelin finished, nobody else on or bound for
  its berth); else a plot of land to set down by (`EconomySetDownSpot`); else a point over the open chasm to hover at
  (`EconomyFlyable`: `ZEPPELIN_CLEAR` inside the rift). The view sends all three - the dock under the cursor, the plot
  under it unless it is the floor, and where the cursor's ray is at `ZEPPELIN_CRUISE` - and the rules take the first
  that applies. A refusal is told as an order's is.
- **Its way** is found once, when sent: A* over the rift's raster points that are flyable, pulled straight wherever
  it sees over the chasm - so one point or two for most flights, and round the bends for long ones. Off a dock or
  onto one it goes **sideways to the nearest point over the chasm**, not along the dock's nose: built first along
  the nose, the test dock - lying along the rim, as docks do - sent it 37 units over land before the chasm. Up at
  `ZEPPELIN_MAST_CLEAR` or more over the dock's ground it clears the mast, and turns onto the dock's lie over the
  berth before coming down.
- **Flown** by the tick in legs (`FLIGHT_*`): rise where it is, cruise along the way - steering for the next point,
  slowing as it turns and for the stop, sliding straight onto its end over the last two units so it never circles
  it - turn to its last heading, and come down. Hovering ends at `ZEPPELIN_CRUISE`; set down, its deck is
  `ZEPPELIN_SET_DOWN_STEP` over the plot's ground, side-on to it.
- **Set down** (`EconomySetDownSpot`): the nearest point, ring by ring out to `ZEPPELIN_SET_DOWN_REACH` from the
  plot's middle, inside the rift with nothing under the zeppelin as high as its keel, propellers or envelope there
  (`BodyClear`'s samples, as modelled). An inland plot has none: "not at an edge".
- **It sees from the air**: `TickExploration` looks round from every zeppelin as from a person, its eye over its
  deck. A discovery made so is told as "someone's" for now.
- **The look**: `zeppelin_body` - the finished stage with the propellers left off their booms - and
  `zeppelin_propeller`, new in chasm_props.glb (`--add --merge`), as objects of their own so the selected one is
  outlined; it bobs and leans into turns off its mooring, its propellers turn with its pace and idle while it holds
  still. The dock draws its mooring lines while a zeppelin is moored on it. Picked by its body: an ellipsoid for the
  envelope, a box for the gondola (`ChasmZeppelinAlongRay`).
- **Its card**: what it is doing and where ("Set down by an island", "Flying, to moor at its dock"), and the right
  click's three. A finished dock's card says whether its zeppelin is moored there or away, and its berth free.
- **Tested** (2026-10-24, debug, `saves/zeppelin_dock_test.json`, the zeppelin finished by `set_stock site`): off the
  dock to hover over the chasm, set down beside an island two steps down and by the home rim, home to moor, each
  screenshot; refused onto its own berth, inland, and over land; a flight saved midway (`saves/zeppelin_fly_mid.json`),
  reloaded and stepped 300 ticks - the same to the bit as without the reload; selected by a click on its envelope,
  gondola, nose and tail in play mode, sent by right clicks over the chasm and on the dock; flown 100 units south,
  clearing 1,600 cells of cloud.
- **Not tested**: a dock erased under a moored zeppelin (it should hover where it is, and go out to the chasm when
  sent); two zeppelins.

## As built: step 2 (2026-10-24)

The rules are in `Flight.cpp` again (`Board`, `BoardIfThere`, `StepOff`, the cabin's), with their hooks in Economy.cpp;
Economy.h's FLIGHT block has a PASSENGERS paragraph. Page: `wiki/passengers.md`.

- **Sent aboard** (`Board`, `CHASM_CMD_BOARD` - a right click on the zeppelin with people selected, `chasm_economy op
  board`): an ORDER to its landing (`EconomyZeppelinLanding`: the plot it is set down by, or the one under its berth),
  spread round it for a group, and `EconomyWorker::zeppelin` named after it. On his post within `ZEPPELIN_BOARD_REACH`
  of the landing with the zeppelin there, he goes aboard (`BoardIfThere`): `WORKER_ABOARD`. The zeppelin away when he
  gets there, he waits on his post for it. Refused in the air, full (`ZEPPELIN_PASSENGERS` grown-ups, those on their
  way counted), aboard already, or as any order is. "Pack a Tent" and "Grab Supplies" send him for those first, as
  anyone posted.
- **Aboard**: his round stops at its first line (`TickWorker`); `TickFleet` stands him in his place on the deck after
  the zeppelin moves - `EconomyDeckPlace`, by his rank among those aboard, two abreast inside the rail forward of the
  cabin - with his feet's height in `deck_y` (saved). He keeps his post's plot, so he stays posted - out of the
  staffing, no errands - but those aboard are left out of a post's places (`EconomyPostPoint`, `SpreadPost`), the ice
  check, the spacing and every zombie's reach (`EconomyReachable`). He faces out over the rail.
- **The cabin** (`EconomyZeppelin::cabin`, saved and hashed) is his pantry, the tower's rule in `Needs`. Kept full at a
  dock as a tower is: `KeepTowers` takes a moored zeppelin's cabin among the towers, `FETCHING` asks `CabinLeft`, the
  load goes in on arrival, and `ProvisionsLacking` counts it. `set_stock` with `pantry` on a dock sets the cabin of the
  zeppelin moored there - a debug's shortcut.
- **Off** (`StepOff`): any `Order` while it is down - asked the way from its landing, his deck place put back if
  refused - and a release, which sends him home from it; his pack topped up from the cabin. In the air every order is
  refused. "Everyone off" on its card is an order each to the landing, spread, as a group's.
- **Children**: one going with his grown-up (`WithTheGrown`) takes the grown-up's zeppelin and goes aboard on his post,
  taking no place, and steps off when the grown-up does. A baby is carried, as ever.
- **The look**: a GANGWAY while it is down, built in code (`BuildGangway`) - off the deck's rail forward of the cabin to
  the first ground within a step of the deck: the edge it is set down by, or the dock's ground. A plank, not the plan's
  rope ladder: set down, the deck is a step over the edge (`ZEPPELIN_SET_DOWN_STEP`). Its bob is the rules' tick's now
  (`EconomyZeppelinBob`), so those on its deck bob with it; held still while it is down, easing in and out of it.
- **Cards**: the zeppelin's lists those aboard (a click selects one), the places taken, those on their way, its cabin
  against full, "Everyone off" while it is down, and warns with nothing left aboard. A person's: "aboard the zeppelin",
  "going aboard the zeppelin", "waiting to go aboard the zeppelin"; a builder's errand "carrying water to the zeppelin".
- **Tested** (2026-10-24, debug, `saves/zeppelin_dock_test.json`): four sent aboard at the dock, two packing tents
  first, all aboard; the builder carrying water into the cabin from a crate; flown to the island two steps down and set
  down, eating from the cabin; "Everyone off" pressed on the card - all four on the island, packs topped up from the
  cabin; a tent pitched there: COLONY 2. Three sent aboard again from the island, flown home and moored; boarding and an
  order refused in the air; saved mid-flight with three aboard (`saves/zeppelin_passengers_mid.json`), reloaded, 500
  ticks the same to the bit; one released at the dock (home), one ordered onto a plot near it; a mother aboard with her
  baby; a person selected and sent aboard by a right click on the zeppelin in play mode; screenshots of the deck at the
  dock with its gangway.
- **Not tested**: a child (not a baby) going along - the debug tools put down grown-ups only; a group past
  `ZEPPELIN_PASSENGERS` (the last refused).

## As built: its sounds and the view over it (2026-10-25)

The user's two sounds, before step 3 (the user, 2026-10-25: "a flying one that should be loopable, and a steam one for
landing or going up"; and the camera "to not follow the terrain under it, but gracefully fade the follow height to the
airship's, giving the player a good overview").

- **The engines**: the cue `zeppelin` (assets/cues/chasm.json), a scope looping `airship_flying` while the nearest
  zeppelin in the air is within `SOUND_ZEPPELIN_OPEN` of the listener - one loop, as the nearest water's is
  (`SignalZeppelins`, ApplicationChasmSound.cpp). The game measures `zeppelin_distance` (in 3D to its body's middle),
  `zeppelin_dx`, `zeppelin_pace` and `zeppelin_engine` - spooled from 0 to 1 over `SOUND_ZEPPELIN_SPOOL_S` as it takes
  off and back as it lands - and the table makes them loud, high and panned. The take's own seam clicked (a jump ten
  times a step's): looped as it came, every ten seconds. It is crossfaded over half a second into its start, so the loop
  is seamless; the take as it came is `art_source/chasm/sound/airship_flying_unlooped.wav`.
- **The steam**: the cue `zeppelin_steam`, `airship_steam` once as one lifts off its dock or an edge and once as it
  begins to come down, where it is - told from what each was doing at the last pass. Both on the activity bus, so they
  stop with the world.
- **Encoded alone**: `tools/sound_ogg.py chasm --only airship_` - a new `--only`, since the other wavs dropped beside
  them (pickaxe, zombie, leah) were not this work's to move.
- **Held through a pause**: what each was doing is kept while the world is paused, so one sent off or down then hisses
  as it goes on - sent in a pause, the first build lost its lift-off's steam.
- **The view**: in play, with a zeppelin selected, the orbit point's height is faded from the ground's follow over to
  the zeppelin's deck over `CHASM_CAM_AIR_SECONDS`, eased in and out (`cam_air`), and back when it is let go. The
  ground's follow runs on apart meanwhile (`cam_ground_y`): built first by easing the point itself, it never came back
  down - over the open chasm no ground near holds a level, and `ViewGroundHeight` kept the zeppelin's.
- **Tested** (2026-10-25, debug, a copy on its own port): the engines' loop opened as it lifted off, followed it
  (distance, side, pace) and closed once it moored and the engines ran down; the steam hissed at the lift-off - sent in
  a pause - and as it began to come down onto the dock; selected over the chasm, the view rose from the rim's -0.1 to
  its deck's 9.35 in about 2 s, smoothly, and came back down as smoothly when let go.

## As built: step 3, the mast and goods (2026-10-25)

The rules are in `Flight.cpp` again (`EconomyMastBerth`, `MastBerthFree`, `SetLoad`, `Unload`) and the zones'
(`ZONE_KIND_MAST`, `ZoneMastAt`), with hooks in Economy.cpp; Economy.h's FLIGHT block has a MAST AND GOODS paragraph.
Pages: `wiki/mast.md`, and `wiki/flight.md`, `wiki/dock.md`, `wiki/passengers.md`, `wiki/crate.md`.

- **The mast** (`ZONE_KIND_MAST`, kind 20, "mast"): one plot, placed by a click where a steam collector could stand -
  the collector's ground rule, its refusals in a mast's words - and built as a winch is, wood by its floor
  (`BuildFactor` 1.5) and `ECONOMY_MAST_ROPE` rope. Its tool is on the build bar with no key, as the dock's, and
  comes with the chasm's discovery.
- **Its look is the dock's mast** (chasm_props.glb's `dock_mast`), not a model of its own: placed in the frame a
  zeppelin moored at it has, so its cup is where the nose goes. Built first as a shed with a pole and a cup in code
  (`BuildMast`), it read as a hut with a flagpole beside the dock's lattice tower; the dock's mast fits exactly - its
  front legs at the lip (`ZoneMastAt`: toward the plot's lower corners, as a steam hood leans), the cup
  `ZONE_MAST_PAST_LIP` past them and `ZONE_MAST_CUP` up. No house body, so no shed: its store is the card's.
- **Moored at a mast**, nose-in, the zeppelin hangs out over the drop as high as it stands on a dock - its origin at
  the mast's ground, its deck `ZEPPELIN_DECK` over it - its middle `ZEPPELIN_NOSE` out from the cup
  (`EconomyMastBerth`). Its LANDING is the open plot beside the mast nearest the cup, kept in `plot` while it is
  moored there (an order onto the mast's own plot is refused, as onto any building). A mast with something under where
  the zeppelin would hang, or nowhere to step off, takes none: its card says why. Its way ends at the berth, turned
  nose-in and down onto it. The gangway runs from the bow, beside the mast, to the lip.
- **The store** of a mast, and of a dock that stands whole and fits a zeppelin: `ECONOMY_MAST_ROOM` (60, a whole
  cargo), whatever the size - the plan said "by floor area, as a store's", but a mast's one plot holds about 20 that
  way, and a load would never fit. Anyone's to take from as a store's (`EconomyKindStores`: site goods, food and water,
  provisions' wheat, hemp, steam, the stored totals), but no carrier delivers to it (`accepts` none) - it holds what
  zeppelins unload, so an unload is never crowded out by the woodcutter's wood.
- **Cargo and the load list** (`EconomyZeppelin::cargo`, `load`; saved and hashed): the list is the player's, up to
  `ZEPPELIN_CARGO` (60) in all (`SetLoad`, `CHASM_CMD_LOAD`). Moored at a mast or dock, its settlement's BUILDERS load
  it: `CabinLeft` is the cabin's lack and the list's together, the moored zeppelin is a site in `FindSiteWork` - PUT
  FIRST, ahead of construction, as a crate to PICK UP is (first built by the distance alone, the one builder went to a
  nearer house site every time) - and what is brought goes into the cabin while it lacks, then the cargo. The mast's
  own store is the nearest source, so it goes first: tested, wood went from the mast's store straight into the cargo.
- **Unloaded** by its card's chip (`Unload`, `CHASM_CMD_UNLOAD`): moored, into the mooring's store up to its room, a good
  at a time in good order; the rest, and everything set down anywhere else, a crate on the landing or the nearest open
  plot (`CRATE_CARGO`, "A zeppelin's cargo"). Refused in the air, or with nothing aboard.
- **Cards**: the zeppelin's - "Moored at a mast on the rim", its cargo against 60, each listed good with its amounts
  (0, 5, 10, 20, 40, 60, cut to what the rest leaves), the goods to add a row a group, whether it is being loaded (or
  "Nobody lives here to load it" at a mast with no settlement), Unload. The mast's: its store, and its berth - moored,
  coming, free, or why none can moor. A dock's shows its store.
- **MCP**: `chasm_economy` op `load` (good, amount) and `unload`; `set_stock` with `cargo` sets a moored zeppelin's
  cargo; the fleet lists `cargo` and `load`; `chasm_paint` paints `mast`.
- **Tested** (2026-10-25, debug, a copy on its own port, `saves/zeppelin_dock_test.json`): a mast painted on the home
  rim by the dock, the zeppelin sent onto it - out over the chasm, turned nose-in, down into the cup; wood and rope
  listed, the builder filled the cabin first and then carried wood from a store into the cargo; 65 set aboard and
  unloaded at the mast - 60 into its store (it counts in the stored totals), 5 floatstone a crate on the landing;
  set down by an island two steps down and unloaded there - a crate of wood and rope; a mast painted on the island,
  moored at and unloaded into; flown home and loaded from the mast's own store; two people sent aboard at the mast,
  drinking from the cabin, ordered off onto its landing; saved mid-flight with cargo and a list
  (`saves/zeppelin_mast_test.json`), reloaded, 1,500 ticks the same; the card's chips pressed in play - an amount, a
  good added, Unload; a right click on the island's mast with the zeppelin selected sends it to moor there; the island
  mast erased under it, it hovered, then flew home when sent.
- **Not tested**: a mast built by an island's own settlers, and loaded by them - the plan's test - which wants a
  builders' hut there; the island mast was painted by debug and nobody lives by it.
- **Noticed**: loading is the builders' pace - a load of `ECONOMY_LOAD` a trip and home between errands, as for a
  site - so a full 60 from a store across the village takes one builder days. An erased mast loses its store, as a
  store does.

## Current tuning

Figures to start from, each a named constant, none of them a rule (`tuning_numbers_not_rules`):

| What | Starting figure |
|---|---|
| its pace (`ZEPPELIN_SPEED`) | 5 units a second, three times a walker's; turning 0.4 radians a second, climbing 1.5 |
| its cruise (`ZEPPELIN_CRUISE`) | 8 over the rim; and 6.5 over a dock's ground, off it or onto it |
| setting down (`ZEPPELIN_SET_DOWN_REACH`) | within 12 of the plot's middle |
| passengers (`ZEPPELIN_PASSENGERS`) | 8 grown-ups; aboard from a post within 8 of its landing (`ZEPPELIN_BOARD_REACH`) |
| cargo (`ZEPPELIN_CARGO`) | 60 units, a frame's worth of wood |
| a mast | wood and some rope, as a winch (`ECONOMY_MAST_ROPE` 4); its store `ECONOMY_MAST_ROOM` 60, a whole cargo |
| the dock's store | a mast's, whatever the dock's size |
| the cabin's stock (`ZEPPELIN_CABIN_WATER`, `_PROVISIONS`) | 24 water and 16 provisions: six days for eight |

## Open

- **Lift by the chasm's warmth**, shrinking in winter: set aside for now (the user's), an idea for later.
- **Losing one**, and **floatstone on the islands**: as `steam_plan.md` leaves them.
