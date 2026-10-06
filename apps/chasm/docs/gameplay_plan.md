# Gameplay

Talked through with the user 2026-10-05. Until now every plan was about the world - grid, terrain,
biomes, painting, drawing - and the README said so: *the first goal is the building mechanic, not a
game*. This page is the game that goes on top of it. Nothing here is built.

It has three parts: **what the user set out** (the direction), **suggestions** (proposed, not
agreed - each one is the user's call), and **the first prototype** (the smallest set of features
that tests whether the loop is any fun).

The concept art (`art_source/chasm/chasm 1.jpg`) is the mood: airships over a chasm with
waterfalls, cloud and steam below, a town on the rim.

---

## The direction (from the user)

### Steam and the chasm

- **Steam is the key resource.** A little can be made anywhere by boiling **water with wood**. The
  chasm makes it in quantity, and taking it from there is far more productive. That is why the
  chasm matters: it is where the power is, not just a barrier.
- **The chasm can be crossed only by zeppelin.** They fly high enough to clear it; nothing else
  does. **There are no bridges over the chasm.** Bridges exist, but only over rivers.
- **The chasm floor below the clouds cannot be built on.** The shards, columns and terraces in the
  middle can, and they **produce a resource you need**.

### Terraces: balconies and islands (agreed 2026-10-05)

**Terrace** is the word for any piece of the old ground standing in the chasm below the rim. In the
game there are two kinds, with names of their own, because the player reaches them differently:

- **Balconies** are on the **edge**: ledges along the chasm wall, a step below the rim. Reached
  from the rim **by winch**.
- **Islands** are in the **middle**: free-standing, out of reach from either side. Reached **by the
  first zeppelins**.

**How this maps onto the generator** (`grid_plan.md`, "Balconies, whole sides, rivers at home"):
a BALCONY is its own feature, a ledge joined to a wall 12 below the rim, built on like the plateau
except for fields. The islands are the `shards`, `columns` and `ledges` - the last being step 10's
old "terraces", which stand off the wall with a drop behind them and so are islands too. The code
says terrace for the pair and uses the game's names for the kinds.

**Agreed 2026-10-05:** at least one balcony on the home (east) side, which the generator now
guarantees; the far side may have none. Anything can be built on a balcony except food, so whoever
lives there has to be supplied from the rim. Islands give floatstone without end, and more room to
collect steam at a better yield.

### Floatstone (agreed 2026-10-05)

**Floatstone** is the chasm's resource: a light ore that zeppelins are **built** from (hull and
envelope frame), while steam **runs** them. It is found only on terraces, and it is **scarce**:
**all of it together is enough for only a few zeppelins**. The exact number is to be decided. Every
zeppelin is then a big decision, and losing one matters.

- **Balconies, by winch.** A winch on the rim lowers to a balcony and hauls floatstone up. Small
  amounts, and a balcony runs out. This is the early source: it needs no zeppelin, so it is how the
  first zeppelin gets built.
- **Islands, by zeppelin.** Out of reach until you have one. They hold what the next few zeppelins -
  and so the crossing - are built from.

So the chasm has a bootstrap: winch floatstone up from the balconies, build a first zeppelin, fly
it to an island, and build the zeppelins that carry people across from what the islands give. The
balconies running out is what pushes you off the rim and into the air.

Every seed has one or two balconies at home and none to two on the far side (built 2026-10-05).

### The threat: zombies from the swamp

- **The swamp spawns small waves of zombies**, and the colony has to defend against them.
- **Walls, palisades and moats are the defence**, and that is the trade-off: everything that keeps
  zombies out also slows down your own goods. Walling a town in is a strategic choice with a cost
  for logistics, not a free upgrade.
- **Goods are carried physically** (agreed 2026-10-05), by walkers on the A* graph. That is what
  makes the trade-off real, and the user accepts it is the most expensive part of the game to build.
- **Fighting back: trained archers and ballista watchtowers.** Archers are trained from the
  workers, as a **share of their time** (see "People" below): a woodcutter who trains half his
  time becomes fairly good at both, and cuts half the wood. The threat costs labour, not just
  materials. A ballista tower is built and needs no one walking about.
- **The swamp freezes in winter** (agreed 2026-10-05), so no zombies come then. Summer and autumn
  are about defence, winter about stocks and heat; the two never pile up into one crisis. Spring,
  as the swamp thaws, is the dangerous moment, and a mild winter could be a bad year *because* the
  swamp never freezes.

### Seasons

- **Winter pushes the snow down from the north**, a long way. You need **wood and coal** to keep
  warm.
- **Several crop types, rotated every year**, or the field's yield drops. Some of the harvest has
  to be **stocked for the winter**.

### People (agreed 2026-10-05)

A combination of mechanics from other games, made possible by the building mechanic: the grid
lets a house be painted right against anything.

- **Where you live matters.** A woodcutter whose **house is attached to the woodcutter's hut** gets
  to work in no time and is very efficient. But the hut is out by the forest, so he - or someone in
  his family - walks further to get **food from the town centre**. Every home is a trade-off
  between the walk to work and the walk to the shops, and the player makes it simply by where they
  paint. Townscaper's freedom becomes an economic choice.
- **Families.** A house holds a family, and errands like fetching food can be done by any of them.
  People are born into a house.
- **A house fits 3 people; the largest 5 or 6.** Capacity follows floor area (plot area x storeys,
  the measure since step 5), so adding a storey is how a house makes room.
- **Base skills.** Every person has **3 or 4 base skills**, and every profession relies on one or
  two of them. At the start of the game everyone's skills are **random**.
- **Children** (agreed 2026-10-05). Growing up is fast: a child **can work after a few years**, and
  **their skills have matured a few years later**. Skills **grow only in childhood**; an adult's base
  skills are fixed.
  - A child has a **large chance to take after their parents**.
  - While growing up they **gain skill from the house they live in**: a child in a woodcutter's
    house grows towards the woodcutter's skills.
  - **But not always.** There is a chance that a woodcutter's child grows up better suited as, say,
    an archer. Families do not lock a trade in for ever.
  - **When a grown child's house is full**, they move into another house - which is the player's to
    provide - or the house has to be expanded. A growing population turns into building work.
  - **Until then the house is crowded**, and a crowded house costs **productivity for everyone
    living in it**, not just the newcomer (agreed 2026-10-05). Ignoring it is possible but never
    free.
- **Time is split.** A worker's time can be divided between professions - a woodcutter who trains
  as an archer for 50% of his time becomes a bit good at both, and does half as much of each.
  Training archers is therefore paid for in work time, not in a separate pool of soldiers.
- **Defence in practice**: a **small garrison** of full-time archers, and when more are needed,
  **trainees drop their work** and join them.

**Base skills and experience** (agreed 2026-10-05): base skills are fixed in adulthood, yet a
woodcutter who trains as an archer gets better at it. So there are two layers: **base skills** (set in childhood, fixed after) decide how
well someone *can* do a profession, and **experience** in a profession (grown by practice, at any
age) is how much of that they have reached. Training builds the experience; the base skill caps
it, or scales how fast it grows. A naturally precise woodcutter then becomes a good archer faster
than a strong one does.

**Base skills, a proposal** (the user asked for 3-4; names and assignments open):

| Skill | Professions that rely on it |
|---|---|
| **Strength** | woodcutter, quarry and coal mine, carrier, winch operator, builder |
| **Precision** | archer, ballista crew, toolmaker, zeppelin wright |
| **Husbandry** (growing and tending) | farmer, herder, cook and baker, forester (planting) |
| **Ingenuity** (machines and steam) | boiler and collector operator, condenser, zeppelin pilot, winch builder |

Most professions lean on one skill and some on two (archer: precision and a little strength), so a
person who is good at one thing is usually passable at a neighbouring one. The archer split above
works better because of this: a strong woodcutter is not a natural archer, while a precise
toolmaker is.

### Pace: a year is about 30 minutes (agreed 2026-10-05)

Two more reference games, besides A Little Age and Townscaper:

- **Kingdoms Reborn** has seasons, and a game year takes about **30 minutes** of real time. To the
  user that always felt slow, though it is apparently the fastest of games like it.
- **They Are Billions** sends a wave about every **10 minutes**, which is fast.

**Chasm takes the 30-minute year**, because it also has waves to fill it. That makes a season about
7.5 minutes at normal speed. With the swamp frozen in winter, the waves fall in the other three
seasons, roughly 22 minutes: two or three waves a year at They Are Billions' rate, fewer if they
are bigger. A speed control (P1) lets a player who finds it slow move it along.

**This puts a clock on growing up.** "A few years" until a child can work is now one to two
hours of play, and their skills settle later still. That is probably right for the game's scale -
a generation is a long-game event, not a moment-to-moment one - but it means **the first
generation does most of the work of a whole game**. The numbers of growing up should be chosen
with this in mind, not in years alone.

### The goal: the pyramid

- **A large pyramid in the middle of the desert** is the end goal.
- **The desert is on the far side of the chasm**, so reaching it means getting people across by
  zeppelin and **founding a second village** there. Getting from there to the desert should be
  costly, so the second village is a real foothold, not a staging post.
- **No river on the far (west) side** (agreed and built 2026-10-05), so water there comes only from
  **melted snow or condensed steam**. The far side then plays differently from home, not just as
  more of the same land. Home is the east, with the swamp; the desert is on the far side.

---

## Suggestions, to review later

Each of these tries to tie two of the ideas above together, so that one mechanic does two jobs. The
user liked them all (2026-10-05) and parked them for review; none is agreed yet. Two have since
been agreed and moved up: floatstone (5) and the swamp freezing (6). They are left here, struck
through, so the numbering stays.

### 1. Zeppelins float on steam

Make the zeppelin a **steam dirigible**: steam is its fuel *and* its lift. That gives three things
for the price of one rule:

- **Range comes from steam**, so a zeppelin cannot fly far from where it was filled. That is the
  reason a second village has to be founded rather than supplied from home: it is the refuelling
  point for the trip on to the desert.
- **It explains why the north mountain is still a seal.** The air is too cold up there and the
  steam condenses. Air lift fails over snow, so a zeppelin cannot go round the chasm's end. Without
  a reason like this, "it flies over the chasm but not over the mountain" needs one.
- **Steam from the chasm is what makes zeppelins practical.** Boiler steam is enough for a short
  hop. Regular traffic needs a chasm collector.

### 2. The chasm keeps its rim warm

The steam rising out of the chasm **holds the snow back along the rim**. In winter, when the snow
line moves south, the rim stays a green band either side of the chasm. Building close to the edge
is then worth something - warmth, and nearness to the collectors - which works against the cliff
rules that already keep houses back from it. It is also a strong winter image: white land, green
rim, a white plume. The mist (`Mist.cpp`) could grow thicker in winter to show it.

### 3. Collectors on the rim, not on the floor

Since the floor cannot be built on, chasm steam is taken by **collectors that hang over the edge**:
a cowl or funnel on the rim, or on a shard, catching the updraft. Shards are then doubly worth
reaching: they have the mid-chasm resource, and they are surrounded by updraft on every side.

### 4. Steam travels as canisters

If steam is a **good carried in canisters**, like wood and grain, it travels the same way as
everything else: carried by walkers, stored, loaded onto a zeppelin. There is one goods model and
no separate pipe network. Pipes can come later as an upgrade, if moving it by hand gets tedious.

### ~~5. What the shards make: floatstone~~ - agreed, see "Floatstone" above

(The other candidates that were weighed: a fungus or lichen that grows only in the steam; old-world
salvage; crystals that store heat, and so stand in for coal on the far side. The last could still
be a second chasm resource.)

### ~~6. The threat and winter take turns~~ - agreed, see "The threat" above

### 7. Zombies are walkers with a different map

The walker's A* (`Walkers.cpp`) already treats walls as closed and gates as the only way into a
walled garden. A zombie is a walker with **its own costs**: walls closed but **breakable over
time**, fields slow, roads fast, water closed. So a **moat is just water you dig**, and a gate is
a deliberate weak point. Defence then comes down to shaping paths, which is the same skill as
building roads. A Townscaper-like builder gets a tower-defence layer without a second set of rules.

What fights back is decided: trained archers and ballista towers (above).

### 8. Crop rotation on the coarse cell

Fields are already coarse cells (`Zones.h`). Each field keeps **the crops of its last two years**,
and a yield table says what follows what well. **Fallow or pasture** restores the soil. That is
cheap to store, easy to show (a tint on the zone view) and fits the patchwork the fields already
draw. With 3-4 crops it is a small planning puzzle each spring, not bookkeeping.

### 9. The far side as a hard mode

With no river on the west, water there comes from **condensers** (which turn steam into water) and
**snow melters** (which need heat). Water then **costs steam or fuel**. The far village depends on
the chasm in a way home never does, and boiling water to make steam - home's cheap trick - stops
working there. If the second village is founded in summer, it has to be ready for winter in one
season.

---

## Open questions

- **How many zeppelins' worth of floatstone** in all, how it is split between balconies and
  islands, and whether a seed must guarantee a balcony on each side.
- **The base skills**: the four proposed above, or others.
- **The numbers of growing up**: how many years (at 30 minutes each) until a child works and
  until their skills are set; and the chances of taking after the parents.
- **Waves per year**, and whether they grow over the years.
- **How steep the crowding cost is** - a flat penalty, or worse per extra person - and whether a
  badly crowded house eventually loses someone (they leave the colony).
- **What reaching the pyramid does** - the end screen, a new era, the source of the steam or the
  zombies?
- **Coal**: from where? Mountain pockets ("mainly for resources") are the obvious place, and would
  give them their purpose.
- **Losing**: can the colony die out, or only stall?

---

## The first prototype

**The aim is to find out whether one season's loop, on one side, is fun before the chasm is
involved.** The chasm, zeppelins and the far side are the expansion. They lean on goods, people and
seasons, so those come first, and the first prototype has nothing in it that the second village
would not also need.

Everything here is simulation state, changed only by commands and the tick, saved and replayed like
the zones and walkers already are. Each part adds its own state-hash part.

| # | Feature | Smallest version that tests it | Builds on |
|---|---|---|---|
| P1 | **Calendar and seasons** | Days and a year in ticks (a year about 30 minutes at normal speed), four seasons; the snow line moves south in winter and back; fields do not grow under snow. A speed control. | `SnowLine` in `Terrain`, the ground colour |
| P2 | **Goods and storage** | Three goods: wood, grain, steam. A storage zone (a built zone, like a house). Stocks per store, shown on the panel. | `Zones`, a new zone kind |
| P3 | **Production** | Felling: a forestry zone fells nearby props (the forest finally shrinks). A field yields at harvest. A boiler turns wood + river water into steam. | `Forest` (felling), fields, rivers |
| P4 | **People and carriers** | Houses hold families. Every person has the base skills, random at the start. A worker walks from home to a workplace (an attached house means no walk); someone from the house fetches food from the town centre; a carrier takes goods to the nearest store. All by A*, so walls and roads change how long it takes. | `Walkers` |
| P5 | **Needs and winter** | People eat grain daily and burn wood for heat in winter. A shortage makes them leave. That is the first way to lose. | P1-P4 |
| P6 | **Zombies** | A small wave from the swamp in summer, none while it is frozen: walkers with their own costs (walls closed, breakable). Stopped by a ballista tower and by archers: workers who give a share of their time to training. | `Walkers`, boundaries, gates |

**P1-P5 is the first test**: can you get a village through its first winter, and does building it
still feel like Townscaper while it runs? P6 comes straight after, because it is what gives walls a
cost and a purpose, and the trade-off needs carriers (P4) to exist.

**Left for after the prototype**, in rough order: births and skills passed on, crop types and
rotation (P3 has one crop), a winch to a balcony and floatstone, the chasm collector and a first zeppelin, the shards, the second
village, coal and the mountain pockets, the desert and the pyramid.
