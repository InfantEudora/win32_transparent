# Zeppelin flight

Talked through with the user 2026-10-24: step 6 of `steam_plan.md`, which built the zeppelin on its dock and left
flying it to a plan of its own. What came before it: `gameplay_plan.md` gives flight its jobs (the islands'
floatstone, the far side, the desert beyond), `steam_plan.md` decided a zeppelin floats only over the chasm and is
filled with steam once, not fuelled, and `settlements_plan.md` decided porters will work much as a zeppelin's route
will. Nothing here is built.

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

1. **It flies.** Selected, its card; sent over the chasm to hover, to an edge to set down, home to moor; the clouds
   cleared as it goes; saved and hashed. Its look: lift-off, bob, propellers, shadow. Tested on
   `saves/zeppelin_dock_test.json`: across the chasm and back, the clouds cleared on the way, saved in flight and
   reloaded.
2. **People.** Aboard and off by order, the ladder, packs and tents, the cabin's stock kept full at the dock and
   eaten from aboard. Tested: four with tents flown to an island and set down, a tent pitched, a settlement there;
   passengers left hovering a day, eating from the cabin.
3. **The mast and goods.** The mast, the load list, carriers loading at a mast or the dock, unloading into its
   store or as a crate. Tested: wood and rope dropped as crates on an island, a mast built there by its settlers, a crate of
   floatstone loaded there and unloaded into the home mast.
4. **A new zeppelin**: the dock's button.
5. **Islands worth going to**: floatstone seams on islands - "scarce" or "without end" is still to settle - and
   collectors there.
6. **Routes**, with porters (`settlements_plan.md`).
7. **Later**: the far rim's village, and losing a zeppelin - the winter storm (`weather_plan.md`) is the favourite
   cause, grounding it in winter as winter closes the swamp.

## Current tuning

Figures to start from, each a named constant, none of them a rule (`tuning_numbers_not_rules`):

| What | Starting figure |
|---|---|
| its pace | about 5 units a second, three times a walker's |
| its cruise | a storey or two over the rim |
| passengers (`ZEPPELIN_PASSENGERS`) | 8 |
| cargo (`ZEPPELIN_CARGO`) | 60 units, a frame's worth of wood |
| a mast | wood and some rope, as a winch; its store by floor area, as a store's |
| the dock's store | a mast's, whatever the dock's size |
| the cabin's stock | about a tower's for 8 guards: a few days' water and provisions for a full cabin |

## Open

- **Lift by the chasm's warmth**, shrinking in winter: set aside for now (the user's), an idea for later.
- **Losing one**, and **floatstone on the islands**: as `steam_plan.md` leaves them.
