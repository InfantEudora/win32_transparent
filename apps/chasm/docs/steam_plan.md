# Steam and the zeppelin

Talked through with the user 2026-10-21. Once a village has lived through its first winter, the game has two
things left to do: **survive the zombies**, by building defence (`zombies_plan.md`, `defence_plan.md`), and
**get to the chasm** - set up a camp on its rim, gather floatstone, and build a zeppelin. This plan is the
second: the chasm's heat, steam as a good, the buildings that make it and turn it into water, and the zeppelin
they lead to. Nothing here is built.

What came before it: `gameplay_plan.md` made steam "the key resource" (boiled anywhere from water and wood, made
in quantity by the chasm) and suggested steam as fuel and lift, collectors on the rim, and steam carried as a good
in canisters. `stone_plan.md` built the floatstone. `goods_plan.md` made leather the zeppelin's material. This
plan settles all three.

---

## Decided (the user, 2026-10-21)

- **A zeppelin floats only over the chasm.** The chasm is hot and breathes steam, and that is what holds a
  zeppelin up; over land it cannot fly at all. This **replaces** `gameplay_plan.md`'s suggestion 1, where steam
  was the fuel and a zeppelin's range came from it. What follows, for free:
  - every route a zeppelin flies is inside the chasm - the home rim, its balconies, the islands, the far rim;
  - the north mountain needs no reason of its own to be a seal: the chasm ends there, and so does flight;
  - the far village is a real foothold, since no zeppelin takes anyone on from it to the desert. They walk.
- **Steam is an ingredient of the zeppelin**, gathered and stored like any other good. It fills the envelope
  when the zeppelin is built; it is not a fuel carried along for every trip.
- **Steam can be condensed into water, and water boiled into steam.** Two directions, two buildings.
- **The steam collector and the condenser are two buildings** (the user chose this of two; the other was one
  building whose card picks steam or water). A condenser beside a collector takes its steam straight from it.
- **Steam is taken from the chasm at any level, and the lower the better - every step down more.** The rim can
  have a collector, with the lowest output; a balcony or an island, lower down and nearer the steam, gives more,
  and **each step of height down gives more again**: the chasm has several levels, and each is worth reaching.
  And that is where the condenser is needed: **a terrace has no river to draw water from**, so whoever lives down
  there - a mine's miners on a balcony, a camp on an island - gets water from steam.
- **Plot size matters, as it does for every other building.** A collector's output and a condenser's pace both
  grow with their plots. So how they pair up is the player's to work out from the depth: high on the rim a
  collector may keep one condenser of its size busy, lower down one collector may feed two smaller condensers.
- **The envelope is HEMP CANVAS**, woven from the hemp already grown for rope. This replaces leather as the
  zeppelin's material (`goods_plan.md`, 2026-10-10): the first zeppelin needs no animals. Leather stays an idea.
- **The heat has to be seen.** For now, a **temperature and weather readout beside the season** in the top-right
  panel, tied to the wind and winter's snow. Below, "The chasm's heat, shown".
- **The proposals were agreed as they stand** (the user, 2026-10-21): steam as a good kept in any store, the
  collector and condenser working under snow, the boiler, the sail loft, the dock, a floatstone seam guaranteed at
  home (since left to the user, separately: below), and the tuning figures to start from.
- **Brass in the palette** for the steam works, and **no tip on the readout** (the user, 2026-10-21, on seeing step 2).
- **After this plan: the ballista tower** (the user picked it from the gatehouse, the ballista, coal, and the
  swamp's winter work). Not planned here.

---

## Agreed (proposed, and agreed the same day)

### The chasm's heat, shown

One **temperature** for a place and a date, in degrees, read in the panel for **the ground the view is centred
on** - pan to the rim and it rises, look down at a balcony and it rises again. A function of the date and the
map alone, like the snow, so nothing is saved, hashed or stepped, and the rules could read it later.

- **The air**: the year's curve, warm in summer and below freezing in midwinter, colder towards the north;
  colder still under winter's storm (`Weather.h`'s `WeatherStorm`, which travels with the snow's front); and
  never above freezing where snow lies (`CalendarSnowAt`), so the readout and the white ground agree.
- **The chasm's warmth on top**: the most at the open chasm's edge and fading inland over some tens of units -
  and below the rim more for every step down, the same steps that raise a collector's yield. The warmth is the
  reason the collector gathers more lower down, and the readout is how the player sees it before building one.
- **The readout**: the degrees, a small rising-heat mark when the chasm's warmth is part of them, and the wind
  - an arrow the way it blows and its strength (calm, breeze, wind, storm), from the same wind the trees lean
  in.

**The snow does not follow it yet**, and that is worth knowing: with snow as it is today a northern balcony can
read above freezing under snow, since the snow is laid by the front alone. Letting the snow follow the temperature
- no snow where the chasm keeps the ground above freezing, a green band along the rim and on the terraces in
winter, `gameplay_plan.md`'s suggestion 2 - is the obvious next step. It changes a rule that fields, collectors,
the posted crossing ice and the swamp all read, so it is the user's call (Open).

### Steam, the good

- **`GOOD_STEAM`, an INPUT** (`goods_plan.md`'s rule: used up, not built with once - it is spent filling an
  envelope, and in a condenser). Appended to `Goods.h` as good 11; never renumber.
- **Kept in any store**, taken by default like every other good, and drawn there and on a carrier's back as
  **brass canisters**. A canister is how steam is carried, not a good of its own: there is no canister to make
  first (`gameplay_plan.md` section 4, without the extra chain).
- **It keeps**, as stone does. Steam that leaks, or condenses in a cold store in winter, is a later idea.
- **The mask**: 13 goods with steam and canvas. `GoodsMask` is 16 bits, so coal, leather and meat fill it
  exactly; a sixteenth more would need it widened.

### The steam collector

A **cowl** - a hood of planks and brass leaning out over the drop, catching the updraft - built along the
**last plots at an edge over the open chasm**, the way a water collector is built along a bank's last dry plots.

- **Where**: on the rim, on a balcony's outer edge, on an island's edge. With the winch and the mine it is the
  third building allowed against a cliff - and only against one that drops into the open chasm, not a terrace
  step or a painted cliff.
- **Its yield is by its height**: one more share for every step of `TERRAIN_STEP` its plots stand below the rim,
  read from the plot, so a new terrace needs no table of its own:

  | Standing on | Steps below the rim | Yield |
  |---|---|---|
  | the rim | 0 | 1x |
  | an island one step down | 1 | more |
  | a balcony | 2 | more again |
  | the lowest islands, just over the cloud deck | up to 6 | the most |

- **Its plots set its carriers, and so its output**, as a water collector's do: a carrier for every
  `ECONOMY_PLOTS_PER_WORKER` plots, chosen by ingenuity (`gameplay_plan.md`'s table already names "boiler and
  collector operator, condenser" under it). It fills while it has its carriers, holds a load and more by its floor
  area, its carrier takes full loads to the nearest store that takes steam, and its card can stop work. A store
  beside it takes only steam.
- **Not stopped by snow** - the chasm keeps its edge warm. Today a water collector stops under snow and the north
  lives on stored water through winter; a collector and a condenser on the rim that keep going make them **the
  colony's winter water**, a reason to reach the rim before winter and not just after it.

### The condenser

Turns steam into water: **a unit of steam into a unit of water**, at its workers' pace.

- **Its plots set its workers**, a worker for every `ECONOMY_PLOTS_PER_WORKER` plots, so a bigger condenser
  turns more steam a day.
- **Beside a collector, it takes the collector's steam straight from its hold** - nobody carries it - as a store
  beside a workplace takes only that workplace's goods. Several condensers beside one collector share its hold,
  unit by unit; what they leave builds up into loads its carrier takes to a store as steam. A condenser that
  outpaces its collector waits on it, idle - which is how the player sees the pairing is wrong.
- **Anywhere else, it is fed steam from a store**, as a ropewalk is fed hemp.
- **Otherwise a water collector** whose water comes from steam: its water carried to a store that takes water,
  the thirsty drinking there as at a collector, houses fetching from the stores as ever.
- **Not stopped by snow**, as the collector.

### The boiler

Water and wood into steam, **anywhere** - the user's "water can also be converted into steam", and the old
"a little can be made anywhere". **The lowest priority** of the four: built when a map needs steam where no
collector reaches.

- **Slow and costly**: a unit of steam from a unit of water and some wood. Water to steam to water again only
  burns wood, so there is nothing to farm in the loop.
- **Its tuning is the choice it makes**: a boiler by a river at home about matches a collector on the rim, so the
  rim is no better than boiling - and one on a balcony beats both. That is what sends the colony down a winch.

### Hemp canvas and the sail loft

- **`GOOD_CANVAS`, a MATERIAL**, appended as good 12. Woven from hemp in a **SAIL LOFT**, a workplace of its own
  - a long, high loft where the canvas is laid out and sewn. Fed hemp from a store, as the ropewalk is, and a
  store beside it takes hemp and canvas.
- **Hemp is then wanted twice** - rope and canvas - so a hemp field or two more, on land a food field would
  take. That is the cost of the envelope, and it is paid in farmland.

### The zeppelin dock, and building the zeppelin

- **A DOCK on the rim**: a mooring mast and a cradle over the drop, where the zeppelin is built. A site like any
  other, waiting for what it lacks, with its card saying so and NEEDS ATTENTION listing it, as the winch's rope.
- **The zeppelin is the dock's second site**, once the dock stands: its frame (wood), hull (floatstone), rigging
  (rope), envelope (canvas) - and **steam last**, filling it, so a zeppelin is never left half-filled for want of
  canvas. Built, it lies moored at the dock.
- **Flight is its own step**, planned when it is reached. The likely shape: a ride from mooring to mooring like
  the winch's, only long, with the zeppelin at one end at a time and room for a few people and a load. An
  island's landing needs no building, as a winch's landing needs none.

### Floatstone at home: a seam guaranteed

**Some maps cannot build a first zeppelin at all.** Each balcony with a long enough wall holds a seam on a roll of
`DEPOSIT_SEAM_CHANCE` (50%, `Deposits.cpp`), with nothing guaranteeing one, and a seam's fallen crystal is laid only
where a seam is. "Balcony" there is every patch below the rim touching the plateau, the many ledges along it too, so
seams are commoner than the one or two big balconies suggest: **measured over seeds 1-20 (2026-10-21), 0 to 8 seams a
map, at 1 to 5 steps down - seed 17 with none at all, and seeds 3, 8, 13 and 15 with a single one**, which may well be
on the far side (which side was not measured). **A seam guaranteed at home is taken out of this plan** (the user,
2026-10-21): floatstone not on every map is an issue the user is looking into separately.

---

## Build order

Each step is playable on the one before.

1. **(BUILT 2026-10-21, "As built: step 1" below)** **The chasm's heat, shown.** The temperature for a place and a
   date, the chasm's warmth by its edge and by steps down, and the readout beside the season with the wind. Tested:
   the readout rising towards the rim and again over a balcony; below freezing under the snow; colder in the storm.
2. **(BUILT 2026-10-21, "As built: step 2" below)** **Steam and the collector.** `GOOD_STEAM`; the collector on an
   edge over the open chasm at any level, its yield by steps down, its carriers by plots, its card and its look, with
   a plume of steam rising from its hood (view only). Tested on a real map: one on the rim and one on a balcony, the
   balcony's filling faster; steam in a store; collectors under winter's snow still filling.
3. **(BUILT 2026-10-21, "As built: step 3" below)** **The condenser.** Beside a collector, several to one, and fed
   from a store. Tested: a condenser beside a rim collector taking every unit as it comes, one beside a balcony's
   leaving the rest to build up for its carrier, and one alone condensing steam its hand fetched from a store.
4. **Canvas and the sail loft.** Tested: hemp to canvas; the store beside it.
5. **The dock and the zeppelin as a site.** Tested: a zeppelin built from goods gathered on a balcony, its steam last.
6. **Flight** - its own plan, then.
7. **The boiler**, whenever a map wants steam without a collector.

Each step adds its state to the saves and the trace, and its page to the wiki.

## As built: step 1 (2026-10-21)

Files: `Climate.h`/`.cpp` (the temperature), `ApplicationChasmWeather.cpp` (`UpdateWeather` measures it at the view),
`ApplicationChasmTime.cpp` (`DrawHeatReadout`), `wiki/temperature.md`. Named Climate, not Heat: the economy's `Heat`
and `HEAT_WARM`/`HEAT_COLD` are a house's firewood (`wiki/cold.md`).

- **The temperature** is `ClimateAir` plus `ClimateChasm`, both functions of the date and the map: the air by a cosine
  year coldest on winter's third day, the north, the date's storm and the snow; the chasm's warmth by
  `ChasmRiftDistance` from the rim and `ClimateStepsDown` - steps counted on balconies and islands only, so painted
  cliffs and river channels are not warmed. `ClimateStepsDown` is what the collector's yield will read.
- **Measured at the view's centre** on the physics thread, beside the storm, and handed to the render thread in
  `WeatherView` with the camera's yaw. Nothing saved, hashed or stepped; the rules do not read it.
- **The readout**: a strip under the calendar, the same width always - rising orange strands while the chasm warms the
  place (brighter the more), the degrees with a drawn ring for the sign (the font is ASCII), pale blue below freezing,
  and the wind as an arrow in the camera's frame with CALM, BREEZE, WIND or STORM, eased over half a second so a gust
  does not flick it. "RUNNING AT" moved under it. A tip on hover - the air, the chasm's share, the steps - was taken
  out again (the user, 2026-10-21): the strip says enough.
- **`chasm_time`'s `weather.heat`**: air, chasm, total, steps down, snow, and where it was measured; the Weather panel
  has the same line.
- **Measured on seed 12** (2026-10-21): spring's first day, inland +2.5, the rim +6.7, balconies 2, 4 and 5 steps down
  +12.9, +18.8 and +23.8; winter's fourth day under the snow -8.7, at the storm's front -12.4, the rim -3.7, a balcony 2
  steps down +2.0 - above freezing under snow, as expected while the snow ignores the heat.

## As built: step 2 (2026-10-21)

The kind is `ZONE_KIND_STEAM` ("steam" in saves and tools), the good `GOOD_STEAM` (11, an input), the job
`WORKER_JOB_STEAM` (10, "steam_carrier", by ingenuity), the tool **P** (`CHASM_TOOL_STEAM`), unlocked by the chasm's
discovery with the winch. Pages: `wiki/steam_collector.md`, `wiki/steam.md`.

- **Where** (`SteamIsBuildable`, Zones.cpp): at the edge of a drop - a corner of the plot's cells lower than it and none
  higher - on the plateau, a balcony or an island; the drop is any lower ground but a cliff painted by hand, so the rim
  above a balcony takes one too, beside the winch's places. Refused inland ("not at the edge"), against a balcony's own
  wall ("against a wall"), on a painted cliff, on the floor and on bare rock; the house's rules otherwise. The slope grid
  leaves it out, as it does the water collector.
- **The simulation** is the water collector's (`Economy.cpp`): carriers by plots, its hold `water_room` by the same
  `EconomyCollectorRoom`, filled in the tick a unit every `ECONOMY_STEAM_SECONDS` / (carriers x `EconomySteamYield`),
  never stopped by snow. Its carrier fetches only whole loads (`FetchFromWorkplace`'s `least`), so a full collector
  waits for him. A store beside it takes steam only. Stoppable, has a door, a body - so zombies breach it and an arch
  can bridge it.
- **The look** (`BuildSteamHood`): the house body in planks under slate, and per plot a post at the lip, a boom out over
  the drop toward its lower corners, a brass hood open below and a pipe back to the roof. Brass is the palette's row 12,
  PAL_METALS, added for it (the user, 2026-10-21): three shades, never snowed on or faded by a season.
- **The plume** (`UploadSteamPlumes`, ApplicationChasmHearth.cpp): the chimneys' puff in the falls' foam white,
  bigger and higher than smoke (`STEAM_*`), one column per plot of a collector being worked, not stopped and not full,
  rebuilt only when which hoods plume changes. The mist's own greys were tried first and lost against the mist below.
  It does not lean in the wind yet.
- **The card**: the steps down and the yield ("On the rim: gathers at x1.00 - every step down 35% more"), workers by
  plots, its collected steam against its hold, the stop. `chasm_pick`'s building economy has `steps_down` and `yield`.
- **Tested** (2026-10-21, debug build): seed 12 - placed on two rim spots and a balcony's outer edge, refused inland all
  along two lines in from the rim; a camp painted beside the rim ones founded a settlement and its people took both as
  steam carriers; 10 steam in its store after 4,000 ticks. `casm_edge.json` (seed 58), whose winch lowers to a wide
  balcony two steps down: one collector on that balcony's edge and two on the rim, three carriers - after 1,000 ticks
  rim 2 and 2, balcony 5; after 2,000, 5 and 5 against 9 (x1.8, the ideal 1.7 rounded to whole ticks); the balcony's
  carrier took his loads up the winch to a store. Screenshots checked: the hoods over the drop, the plumes. Then
  winter's fourth day, both rim collectors under the snow: 7 to 10 in 1,200 ticks, a unit every 400 as on a summer's
  day. Staffing is by building order, as for every workplace: in a colony short of people the newest - the collectors
  - go unworked first, and a water shortage took one of their carriers for the water collector.
- **Not tested**: a store set not to take steam.

## As built: step 3 (2026-10-21)

The kind is `ZONE_KIND_CONDENSER` ("condenser"), its job `WORKER_JOB_CONDENSER` (11, "condenser_hand", by ingenuity),
the tool **L** (`CHASM_TOOL_CONDENSER`), unlocked with the chasm as the collector is. Page: `wiki/condenser.md`.

- **Where**: anywhere a house could stand. "Beside" a collector is a plot of it sharing a fine edge with a standing
  collector plot (`EconomyCondenserFeeds`), the same rule a store beside a workplace has.
- **Condensing** is in the tick, as a collector fills (`Economy.cpp`): every `ECONOMY_CONDENSER_SECONDS` / its hands, a
  unit of steam from the lowest-numbered collector beside it with any - else from its own stock - into a unit of water,
  up to `water_room` (`EconomyCollectorRoom`, as both collectors). Snow never stops it.
- **Its hands' round**: at home, a load of its water to a store first (`FetchFromWorkplace`, which carries off only
  water - `WorkplaceMakes`); then, with no collector beside it, its own steam used up and room for water, a load of
  steam from the nearest store, camp, empty house or crate (`FeedCondenser`, the ropemaker's hemp errand, the same
  `WORKER_TO_INPUT` and `WORKER_TO_WORKSHOP` states), put into its stock on arrival (`AtCondenser`). One hand at a time
  goes. Beside a collector none is fetched: it waits on the collector.
- **Water out of it**: houses fetch from it (`FetchFrom`), the thirsty drink there and wait for the next unit as at a
  water collector, never under a snow rule. A store beside it takes steam and water.
- **The look**: the water collector's stone walls under timber, and on every plot a brass tank through the roof where a
  chimney would stand (`CONDENSER_TANK_W`), banded dark at the top.
- **Tested** (2026-10-21, debug, `casm_edge.json`): the three collectors of step 2, a condenser beside the balcony's (2
  steps down) and one beside a rim collector, eight people added. Over 3,000 ticks: the rim pair - collector 0, 0, 0,
  water 3, 5, 8 (each unit condensed as it came); the balcony pair - collector 2, 4, 5 and its carrier coming for it,
  water 3, 5, 8; the lone rim collector 3, 5, 8. A third condenser alone, a store given 30 steam: its hand walked
  there, brought 5 back and it condensed them (4 steam, 1 water at the next look). Screenshot checked: brass tanks and
  hoods.
- **Not tested**: two condensers beside one collector (the share is by building order, a unit at a time), and houses
  fetching from one.

## Current tuning

Figures to start from, each a named constant, none of them a rule (`tuning_numbers_not_rules`):

| What | Starting figure |
|---|---|
| a collector on the rim | a unit of steam every 8 s a carrier (half a water collector's pace) |
| for each step down | +35% on that: x1.7 on a balcony, about x3 just over the cloud deck |
| a condenser | a unit of water from a unit of steam every 8 s a worker - so a rim collector keeps one of its size busy, and a deep one two |
| the chasm's warmth | +5 degrees at the open edge, gone some 24 units inland; +3 for each step down |
| the air | about +20 in midsummer, -8 in midwinter, a few degrees colder at the north edge and in the storm |
| a boiler | a unit of steam from a unit of water and 1 wood, every 8 s |
| a canvas | 2 hemp, as a rope |
| the first zeppelin | 40 floatstone, 20 rope, 20 canvas, 30 steam, its frame's wood |

A seam holds 50 floatstone (`DEPOSIT_SEAM_FLOATSTONE`), so a home seam builds one zeppelin and part of a second;
the islands give the rest. How many zeppelins' worth there is in all is still open (`gameplay_plan.md`).

## Open

- **The snow following the temperature**: the green band along the rim and on the terraces in winter (above,
  "The chasm's heat, shown"). The user's call; it changes a rule much else reads.
- **Floatstone on the islands**: "without end" or "scarce" - `gameplay_plan.md` says both. To settle with flight.
- **What a zeppelin carries** and how many there can be at once.
- **A zeppelin lost**: whether one can be, and to what - the gameplay plan says losing one should matter.
- **The far rim's dock**: built by the first who land there, or a mooring on any edge plot.

## Ideas, for later

- **Waterfalls make steam**: a collector near a fall's foot gives more, where the water meets the hot floor.
- **A steam winch**: steam to ride faster, or to haul more at once.
- **Steam that leaks** from a store, or condenses back into water in winter's cold.
- **Leather**, for a better envelope, once there are animals.
- **The plume tells the yield**: a collector's steam drawn thicker the more it gathers, so the best spots read
  from across the map.
- **The heat drawn in the world**, past the readout: a shimmer over the rim, plumes rising from the chasm in
  winter, the mist thicker in the cold.
