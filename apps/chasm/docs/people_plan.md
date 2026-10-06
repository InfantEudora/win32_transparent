# People (P4)

Step 4 of `gameplay_plan.md`'s first prototype: the stand-in workers of P2/P3 (`economy_plan.md`) become
people who live in houses and walk to work. Talked through and BUILT 2026-10-06.

## Decided (the user, 2026-10-06)

- **Starting settlers only**: the colony is the group it starts with; it grows only once births come.
- **Every round starts and ends at the worker's house** - house, tree, store, house - so a house beside the
  hut and its store is what makes a woodcutter quick. His going home between trees is deliberate.
- **Jobs are given out automatically**, by skill and by the walk from home; the player steers it by where
  they paint.
- **Fetching food comes with eating**, in P5.

## As built

Files: `Economy.*` (the people are `EconomyState::workers`, one `EconomyWorker` each), the panel and
`chasm_economy` in `ApplicationChasmEconomy.cpp`, the overlay's PEOPLE in `ApplicationChasmTime.cpp`,
`ApplicationChasmWorkersView.cpp` (indoors, the plain tunic), `ChasmSave.*` (`"people"`, `"next_person"`).

- **The settlers**: three families of adults - four, three and three - with an id, a family, an age
  (18-45) and four base skills, 1-10 each: strength, precision, husbandry, ingenuity (the plan's proposal;
  the names may still change). Ages and skills come from the world's seed, so a map always starts with the
  same colony. Names are placeholders from two short lists; a family shares its name.
- **The camp is a building** (the user, 2026-10-06): kind `camp`, tool C, built by a house's rules but
  one storey and a person to a plot - a tent on each plot, or on one in five (by the plot's hash) a fire
  with a ring of stones, crossed logs, a flame and a log to sit on. It holds any families together, up to
  its tents, and can be walked through (open ground between tents). A new map starts with one: placed by
  the zones at the camp spot (on the home side, 30 in from the middle of its balcony's stretch of rim, on
  flat dry plateau) and grown plot by plot until it has a tent for every settler - seed 1's has 14 plots,
  10 tents and 4 fires. The settlers live in it from the start, so they can work from day one; a family
  living in a camp keeps looking for a house, and moves out as soon as one holds it.
  People in a camp are NEVER INDOORS (the user, 2026-10-06): home, they stand out in front of their own
  tent's door (`ZoneCampDoor`, the way the tent faces by `ZoneCampYaw`), so a camp full of people is the
  sign there are too few houses.
- **Houses**: a house holds one family, of at most its floor area / 1.3 people, and never more than 6 - so a
  one-plot, one-storey house holds 3, two storeys 6. Each waiting family, the largest first, takes the
  smallest empty house that holds all of them and that they can walk to. None empty that holds them: a
  smaller family living in one that would moves to an empty house that holds it, and they move in (a house
  grows a storey at a time, so the first family in often took it before it was big). A house pulled down
  sends its family to the nearest camp with room - or, with none, to the camp's spot to stand.
- **Jobs**: every workplace with nobody takes the free housed adult with the best score - his skill for
  the job (woodcutter: strength, farmer: husbandry, water carrier: ingenuity), 4 world units a point,
  less the straight line from his house - among those who can walk there. One worker a workplace; a job
  stays his until the workplace or his house goes.
- **Skill is pace**: 0.6 + 0.08 x the skill - 1.0 at 5, 1.4 at 10, 0.68 at 1. A woodcutter fells at his
  pace; a field grows at its farmer's; a field without a farmer does not grow, and a collector without a
  carrier does not fill.
- **Rounds**, from the house: what waits at the workplace first (the hut's pile, the field's harvest, the
  collector's water) when a store takes a load of it; then, for a woodcutter, a tree. Everything delivered,
  home again. At home a person is indoors and not drawn.
- **Rivers part the home side** until there are bridges: a family only takes a house it can walk to, a
  job only goes to someone who can walk to it, and someone whose way home was cut waits where he is until
  the zones change.
- **The overlay** shows PEOPLE (housed/all while some have nowhere to live), the bar sized to its text, beside WOOD, FOOD and WATER; the
  panel shows a house's family and a workplace's worker with his skill, or that nobody works there.
- **Measured**: settlers by the camp moved into three houses (the family of four into the two-storey one,
  after the family of three moved out of it to a one-storey house), the strongest (Kaat, strength 10)
  took the woodcutter's hut and the best farmer (Anouk, husbandry 10) the field. The replay test builds
  houses and a hut by the camp during its recording; PASS in debug and release, all ten housed.

- **Figures** (2026-10-06): the user's four figures in `chasm_props.glb` - `adult_male`, `adult_female`,
  `kid_male`, `kid_female` - drawn at 0.46 of their modelled size (a man 1.87 tall becomes the old
  figure's 0.86, about a tent's height), as modelled: their clothes are their own, so the job tunics
  are gone (the card says the job). A person now has a SEX (`EconomyWorker::f_female`, from the seed for
  the settlers; saved, hashed; an older save takes it from the id), which picks the figure and a first
  name from a woman's or a man's list. The children's figures are loaded and wait for births.
  What he carries goes by the GOOD, not the job - a log for wood, a sack for food, a pail for water -
  so an idle carrier taking wood to a site carries a log (it used to be a pail).

**Open:**
- No crowding yet (a house fuller than it should be costs everyone in it - agreed, comes with P5's needs).
- No day and night - none of the games this takes after has one, and it would change too often (the
  user, 2026-10-06). People work all the time. Perhaps, later, a darker winter, coming on gradually.
  Eating and fetching food come with P5.
- Children, ageing, births and skills passed on are after the prototype.
- Nobody carries between stores yet (the carriers of the plan): each worker still delivers his own goods.
