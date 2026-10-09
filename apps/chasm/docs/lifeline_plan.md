# Lifelines

Talked through with the user 2026-10-09; step 1 (families and ageing) BUILT the same day - see "As built"
at the end. Until then the colony was the settlers it started with: ten adults whose ages never changed and
who had no parents (`people_plan.md`). This plan
gives people a whole life - expected, born, cared for, playing, working, growing old - and gives everyone
a mother and a father, which the skills passed on (`gameplay_plan.md`, "Children") need and which the
player should be able to see.

**All of it is rules**: saved, hashed and replayed, unlike the seasons' looks. Every chance in it comes
from a hash of the world's seed and the people's ids, the way the settlers' ages and skills already do
(`Economy::MakeSettlers`), never from the shared random stream, so a replay holds.

## Decided (the user, 2026-10-09)

- **One year of age per game year.** Everyone has a birthday on one day of the 40-day year and is a year
  older that day. (One a season was proposed and turned down.)
- **The stages of a life:**
  - **Expecting, half a year**, before the birth.
  - **Baby, one year.** A full year in which the MOTHER cares for it: caring is her job, she cannot do her
    own, and someone else has to fill in for her. She still runs the house's errands - fetching food,
    water and firewood.
  - **Child, a year and a half.** Their job is PLAYING, which every so often has them walk around the
    house.
  - **Young adult, two years.** They work, while their skills are still growing.
  - **Adult** from then on: 4½ years after birth.
  - **Old**: they stop working, walk around the house now and then like the children, and still run the
    house's errands. They die of old age **around 40**, which is what most decides how fast a colony
    grows.
- **A birth needs a warm house and fed parents.**
- **A mother has at most so many children.** The settler mothers start with the children they already
  have counted against it.
- **The settlers are families**, and two of them are children: twelve settlers instead of ten. **One
  settler couple is expecting** when the game starts. **An expecting mother works until the birth.**
- **No orphan mechanic** (the user: it would complicate the system a lot, for nothing useful yet). A child
  whose parents die grows up in the house as they would have anyway.
- **Every feature as separate and small as it can be**, built in the order below, with the original plan
  (`gameplay_plan.md`, "Children") first in mind.
- **Parents and couples as proposed** (below): everyone keeps a mother and a father, and a couple forms
  when two grown people of different families move into a free house together. The other proposals of
  the first draft were accepted as they stood.

## The time scale

A year is 30 minutes at normal speed (`Calendar.h`).

| Stage | Age shown | From birth | At normal speed | At 4x |
|---|---|---|---|---|
| Baby | 0 | the first year | the first 30 min | the first 7½ min |
| Child | 1 - 2 | 1 - 2½ years | 30 min - 1 h 15 | 7½ - 19 min |
| Young adult | 2 - 4 | 2½ - 4½ years | 1 h 15 - 2 h 15 | 19 - 34 min |
| Adult | 4 - 35 | 4½ - 36 years | 2 h 15 - 18 h | 34 min - 4½ h |
| Old | 36 on | from 36 years, dying at 38 - 43 | from 18 h | from 4½ h |

A stage is counted from the day of birth, not from birthdays, so a child becomes a young adult half a
year after their second birthday. The gameplay plan estimated one to two hours of play before a child
works (`gameplay_plan.md`, "Pace"); here a young adult starts work at 1 h 15.

**A life is about 20 hours at normal speed** (2 at 10x). So the settlers grow old over a game, and nobody
born in it is likely to: a child is an investment that pays back for a long time. Old age is mostly
how the first generation leaves.

## The stages

### Expecting

- **Half a year from the chance of a child to the birth** (see Births). The mother works as usual until
  the baby comes.
- **A mother who dies while expecting** loses the child with her.

### Baby: the mother's year

- **Born at home**, into the couple's house, and **indoors all year**: not drawn, so no baby figure is
  needed.
- **Lives on the house's stock**, eating and drinking like anyone (a full share for now; tuning).
- **The mother's job becomes CARING** for the year. Her place at her workplace is free at once, and the
  usual handing out of jobs fills it (`Economy::AssignJobs`): the free housed adult with the best score,
  or, with fewer people than places, the newest workplace loses its worker instead. A caring mother is
  not counted among the people the places share out.
- **She still runs the house's errands**: fetching food, water and firewood when the house runs low, and
  her own pressing needs. Otherwise she is at home.
- **After the year she is free again**, and takes whatever place the order gives her - not necessarily
  her old one, which someone else holds by then.

### Child: playing

- **Drawn with the kid figures** (`kid_male`, `kid_female` in `chasm_props.glb`), loaded since 2026-10-06
  and waiting for this.
- **PLAYING is at home**, with now and then a short walk to a plot or two round the house and back - when,
  and where to, from a hash.
- **No work, no errands, no building.** They eat and drink at home, and the family's fetching keeps the
  larder up. A child still sees to a pressing need of their own like anyone, so a child never dies of
  thirst beside an empty larder with a store in reach.

### Young adult

- **Works**: takes a job the usual way and counts among the people like an adult; drawn with the adult
  figures.
- **Skills still growing** (below). Not yet free to form a couple.

### Adult

- **Skills fixed.** Can form a couple and have children.

### Old

- **Stops working** at 36: their place is filled as a caring mother's is, and the old are not counted
  among the people the places share out.
- **Walks around the house now and then**, like a child playing, and **still runs the house's errands**:
  food, water and firewood.
- **Has no more children**, whatever the count.
- **Dies of old age between 38 and 43**, the year from a hash of the person - a new cause of death beside
  hunger, thirst and cold (`NEEDS_DIED_*`). Drawn with the adult figures (an old figure could come later).

## Families and parents

### Who is whose

- **Every person keeps a `mother` and a `father`**: person ids, 0 for the parents of the couples that
  founded the settler families, who never came. Ids are never reused, so a dead parent is still named.
  The colony's record of its dead (`EconomyDeath`) keeps the same two fields, so the tree outlives the
  people in it.
- **Everyone keeps their own surname**, given at birth, and keeps it in a couple. Today a surname is a
  hash of the family's number (`EconomyPersonName`), which is enough until couples found new families;
  from then it has to be kept per person, and saved.
- **Shown**:
  - a person's card says whose child they are and lists their children;
  - a house's card shows its family as a small tree, with the dead marked;
  - `chasm_economy` gives every person's mother, father, partner, born and stage.

### The settlers

Three families, twelve people: ten grown and two children.

| Family | Who | Size | Mother's children so far |
|---|---|---|---|
| 1 | a couple, two grown children, a child | 5 | 3 |
| 2 | a couple, a grown child, a child | 4 | 2 |
| 3 | a couple, a grown child | 3 | 1 |

- **Sizes 5, 4 and 3** keep the choice the old 4, 3 and 3 gave: a family of three fits a one-plot house,
  and the larger two need more floor.
- **The grown children are the couple's own**, with parent links from the first tick. Their skills come
  from the inheritance rule below, so the trait system shows from the start.
- **Among the four grown children there is at least one son and one daughter, both adults, in different
  families**, so a couple can form; the rest is the seed's.
- **Ages, from the seed**: the couples 24 to 32, so they work 4 to 12 years (2 to 6 hours) before they
  grow old; the grown children 5 to 12; the two children 1 or 2.
- **One couple is expecting**, picked by the seed among the mothers below the limit, the birth due
  between day 5 and day 20 of the first year - before the first winter, so the first baby is seen early.
- **The camp gets twelve tents**, one for each (`EconomySettlerCount`, which the camp spot's survey
  already follows).

### Couples

- **A couple forms when two grown people move into a free house together**: an adult man and an adult
  woman, of different families, not related (no shared parent or grandparent), neither already in a
  couple. They found a NEW FAMILY there.
- **So a new house is a new couple, and a new couple brings children.** The colony grows by building, as
  `gameplay_plan.md` wants ("a growing population turns into building work").
- **Their children take one of the two surnames**, picked by a hash of the couple.
- **The settler couples are couples from the start.**
- **When several could pair**: the eldest unpaired adult first, with the eldest who can pair with them, the
  lower id on a tie. Families that have outgrown their house move into a free house that holds them all
  first (`Economy::HouseFamilies`, as today), and couples take what is still free.

### Births

- **A couple in their own house has one chance a year of a child**, on a day of the year that is the
  couple's own (a hash). A chance that comes up starts the half year of expecting.
- **Only when** the house is warm - in winter, its firewood burned that dawn (`HEAT_WARM`, `needs_plan.md`;
  outside winter every house is warm) - and both parents are fed: neither short of food nor dry that day
  (`EconomyPressing` empty for both).
- **Not** while she is expecting or caring for a baby, once either of them is old, or once she has had
  her most children (`ECONOMY_CHILDREN_MOST`, tuning; the settler mothers' children so far count). That
  limit, and old age, replace the bearing years of the first draft.
- **A full house does not stop a birth.** Being crowded is the cost (`people_plan.md`, agreed
  2026-10-05) that pushes the player to build or add a storey.
- **The baby** gets the next person id, the couple's surname, and a first name from the woman's or the
  man's list by sex.

### Orphans: no mechanic

A child can lose both parents - to hunger, thirst, cold or old age - and simply **grows up in the house as
they would have anyway** (the user, 2026-10-09). A baby whose mother dies in her caring year likewise:
nobody takes the caring over. A mechanic that sends orphans to relatives was proposed and left out - it
would complicate the system a lot, and does nothing useful yet.

## Skills passed on

The rules are `gameplay_plan.md`'s "Children" (agreed 2026-10-05): take after the parents, grow towards
the house's trade, but not always. With the stages above:

- **At birth**, each skill takes after the mother or the father (a large chance), and otherwise is random,
  1 to 10.
- **While growing** - child and young adult, 3½ years - on each birthday the skill the house's trade leans
  on grows a point. The trade is the most common job among the adults living there (`EconomyJobSkill`).
- **Not always**: some children have a CALLING, a skill of their own picked by a hash, which grows instead.
- **Fixed at adulthood.** Experience, the plan's second layer (getting better at a job by doing it), is
  not part of this.
- **The settlers' grown children** have only what they inherited, with no growing: the colony's history
  before the game is not simulated.

## Saved and hashed

- **Per person**: `born` (the tick of birth, before 0 for the settlers), `mother`, `father`, `partner`,
  the surname, a mother's children so far, and `due` (the tick an expected child is born, 0 none).
  Age, birthday and stage are all worked out from `born` and the calendar, so `age` is no longer saved.
- **The dead** keep `born`, `mother`, `father` and the surname.
- **An older save** loads with `born` from its age (the birthday from the id's hash), and with no parents
  and no couples, so it has no births: a new game is needed to see them.

## Current tuning

First guesses, except where marked. Tuning, not rules: each becomes a named constant.

| What | Value |
|---|---|
| Expecting | half a year (the user's) |
| Baby | 1 year (the user's) |
| Child | 1½ years (the user's) |
| Young adult | 2 years (the user's) |
| Old | from 36 |
| Dying of old age | between 38 and 43 ("around 40", the user's) |
| A couple's chance of a child, each year | 1 in 2 |
| A mother's most children | 4 |
| Taking after a parent, per skill | 7 in 10 |
| Growing towards the house's trade | a point a birthday, as child and young adult |
| A calling | 1 child in 5 |
| Playing, and the old: a walk round the house | on one look in 60 at home, about every 30 seconds at normal speed; a plot or two away, 4 seconds there (`LIFE_PLAY_*`) |
| A baby's and a child's food and water | a full share |

With one chance a year, half a year expecting and a year of caring, a couple has a child every two years
or so; four children take a mother about eight years.

## Open questions

1. **A widowed adult**: free to form a couple again, bringing their children into the new house? Left out
   for now, in the spirit of the orphans: a widowed adult stays single.

## Build order

Each step is its own small feature, playable and checked before the next.

1. **(BUILT 2026-10-09)** **Families and ageing**: `born`, birthdays and the stages; `mother` and `father`; the settlers as three
   families with two children; children PLAYING (no job, no errands, no site work; the kid figures; a walk
   round the house now and then) until they are young adults and take a job; the cards and
   `chasm_economy` showing age, stage and parents. Skills stay random, as today. No births, no old age.
2. **Births**: the settler couples, the yearly chance and its conditions, expecting, the baby indoors, the
   mother's CARING year (her place filled, her errands kept), a mother's most children; the settler couple
   that starts out expecting. Babies' skills random until step 5.
3. **Couples**: grown people of two families pairing into a free house, a new family; surnames kept per
   person.
4. **Old age**: stopping work at 36, the walks, still running errands, dying of it.
5. **Skills passed on**: taking after the parents, at birth and for the settlers' grown children.
6. **Skills growing**: towards the house's trade, and callings.
7. **Crowding** (`people_plan.md`, `needs_plan.md`): a house past its room costs everyone in it. Births
   are what make it happen.

## As built: step 1 (2026-10-09)

Files: `Economy.*` (LIFELINES in `Economy.h`; `MakeSettlers`, `GoPlay`, the grow-up in `Tick`),
`ChasmSave.cpp`, `ApplicationChasmEconomy.cpp` (the hash, `chasm_economy`, the panel),
`ApplicationChasmSelect.cpp` (the cards, the badge), `ApplicationChasmWorkersView.cpp` (the kid figures).

- **A person keeps `born`, `mother` and `father`**; `age` is gone. Age, stage and birthday are worked out
  from `born` and the calendar (`EconomyAgeAt`, `EconomyStageAt`, `EconomyBirthday`), against the tick the
  economy last ran or was loaded at (`EconomyState::tick`; `Economy::Restore` now takes the save's
  calendar tick). The dead keep the same three. Saves from before were dropped, not converted (the
  user's): the saves folder was empty already, and the two recordings that started from old saves were
  deleted (the user's).
- **PLAYING is a job, not a flag**: `WORKER_JOB_PLAYING` (-2), below `WORKER_JOB_NONE`, so everything that
  looks for an idle adult - the builders' wood, road work, fetching home, the job counts - passes a child
  by on its own. The job hand-out leaves a child out (`employable`), and so does a load home after a meal
  at a store. Saved and hashed as any job.
- **Growing up**: on the tick a child's stage reaches young adult, `Economy::Tick` sets the job to none and
  hands out the places again, so a free place is taken at once; a child out playing finishes and comes
  home first. The kid figures are drawn by STAGE, before young adult; their loads are fitted down to the
  figure's height (none is carried in this step).
- **Playing** (`WORKER_TO_PLAY`, `WORKER_PLAYING`): a plot one or two steps from home with nothing built on
  it, or among a camp's tents - which are open ground to the walkers. Without the tents, a child in the
  middle of the 12-tent camp had no plot to go to and never went out.
- **Shown**: the card says "Age 2, child - birthday winter 3", "Son of Wout and Cato" ("the late ..." for
  the dead), "Children: Daan, Ivo, Bas", and for a child "Plays near home - grows up in 12 days"; the
  badge has a ball; the house card's list "Child". `chasm_economy` gives every person's age, stage,
  birthday, born, mother and father, `people.children`, and the dead's age, mother and father; `jobs.none`
  no longer counts children.
- **Measured** (seed 12, debug):
  - The settlers: the Veens (Wout 31, Cato 29, Daan 9, Ivo 12, Bas 2), the Vissers (Maas 27, Lotte 28,
    Nora 6, Sanne 2) and the Smits (Otto 30, Cato 25, Roos 8); a son and a daughter in different families.
  - Both children went out to play within 40 s at 2x, Bas from a house and Sanne from the camp, and came
    home.
  - Bas grew up on day 12 and went straight to fetching for his house; Sanne on day 15, with builders'
    places free, took one on that tick.
  - Two replays of a 9,000-tick recording across Sanne's growing up: the same, tick for tick.
  - The camp's 12 tents: all 30 surveyed seeds still get a camp with all 12 housed (seeds 1-30), with
    fewer spots passing than with 10 (seed 4: 24 of 148).
- **Names** (the user's, same day): 30 first names a list and 28 surnames, stepped through by a stride with
  no factor in common with the list's length, so the first 30 people, and families, never share one (two
  Catos on seed 12 before, from a hash over 15).
- **Seen in passing, not lifelines - a building's door can be boxed in.** A building's door - where its
  family walks home to, where its workers are reached (`home_plot`) - is its LOWEST-NUMBERED plot
  (`Economy::Derive`). The coarse grid's vertices keep the lowest numbers when it is subdivided
  (`Subdivide`), and a coarse vertex's only fine neighbours are the four or so midpoints round it; so a
  building that takes a coarse vertex and every plot round it - a 3 x 3 block centred on one, a plus -
  has its door in its middle. A walk enters a built plot only where it ends (`Walkers::EdgeOpen`), so
  nobody reaches it: a family never moves into such a house (HouseFamilies wants a way to the door), a
  workplace's empty places are never filled (AssignJobs likewise), and a woodcutter's pick-ups at his hut
  fail. Found on a builders' hut painted as a 3 x 10 block: the builders given places while it was small
  kept them, and a new one could not be given one. A building EXTENDED over its door's neighbours moves
  the door inside under a family already living there, who then cannot get home. Stores, fields and the
  camp are not touched by it (their goods go to the nearest plot; a field's door is a cell's centre; a
  camp's people have their tents). Fixing it - the lowest plot with a way in from outside - waits for the
  user's go-ahead.
