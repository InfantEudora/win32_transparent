# Spoilage: food lying out rots

The user's, from a bug report's save, 2026-10-24. Seed 12 in year 16, 24 people: its fields held 954 wheat, 996
beans and 312 greens - field 3 alone 562 wheat and 442 beans - while the colony's one store, a single plot, was
full at about 95 units of everything. About six years of the colony's eating lay out on its fields, and FOOD in the
top bar, which counts only stores and the camp's supplies, said about 100. Nothing told the player.

**Why it piles up.** Every other producer stops when nothing takes what it makes: the woodcutter fells only while a
store or his pile has room for a log, a collector fills to its hold, the camp's water carrier and picker stop at
theirs, a ropewalk at its pile. A field does not. Its farmer sows whenever it is empty (`GoSow` never looks at what
lies there), the harvest goes into the field's own stock, which has no room, and he fetches it only when a store or
his home takes a load (`FetchHarvest`). With the store full, every harvest went on the pile.

**Why the spoilage already planned would not have cleared it.** `economy_plan.md` made spoilage a property of the
crop wherever it is - greens rot when winter comes, wheat a little each season, beans not at all - and half of this
pile was beans. So this plan splits it by WHERE food is: lying out, it rots; stored, it keeps. The crop's own
keeping in a store stays the later step it was (below).

## Decided (the user, 2026-10-24)

- **Food lying out rots, a share at each dawn.** LYING OUT is a FIELD's harvest and a CRATE. Everything else keeps:
  a store, a home, the camp's supplies and pantry, a builders' hut, a tower.
- **By the day, per food**, in the order food is already eaten - the one that keeps worst first - but **beans rot
  too**, slowest. Shares at Current tuning, below.
- **Rounded up to whole units**: a food lying out loses at least one a dawn, so a small pile is gone too rather
  than lying for ever.
- **It starts after two days.** Food that came to a field or a crate lies untouched for two days first; only after
  that does it rot. A colony that carries its harvest in loses nothing.
- **Crates rot too** - the load a posted man put down, what a camp left, what a site was brought.
- **A farmer sows over a harvest lying there.** Nothing holds up sowing: by the time the new crop is grown, most of
  the old one has gone. A field nobody fetches from settles at a few harvests' worth, and stops growing.

## How the two days are kept: per place, by the day it came

Food is not a set of items, and should not become one. A field's stock is a count of each good over all its cells;
a crate's the same, one crate to a plot. So the age is kept PER PLACE - a field by its building id, a crate - and
BY THE DAY the food came, not per unit:

- **Beside its stock, a place keeps its FRESH food**: how much of each food came today (since this morning's dawn),
  and how much the day before. Whatever else of a food it holds is LYING: past its two days.
- **Taken from oldest first, with no change to the taking.** Every take - a farmer's load, a meal on a pressing
  hunger, a scout's pack, a builder's goods, PICK UP - just lowers the stock, as now. At dawn the fresh counts are
  held to what the stock still has, the day before's first. So what was taken came out of what lay longest, then the
  older fresh, and a load put back on the same tick (no store took it after all) is never counted as new.
- **What comes in counts as today's**: a harvest landing on its field (the tick, at harvest), and anything put into
  a crate (`AddToCrate`, every way into one: a load put down, a camp's goods, a site's, a debug spawn).
- **At each dawn**, for every field and crate, in this order:
  1. the fresh held to the stock, the day before's first;
  2. what lies rots: of each food, its share of what lies, rounded up;
  3. the day before's fresh joins what lies, and today's becomes the day before's.

  So food that came during a day first rots at the third dawn after it - between two and three days after it came,
  never less than two.
- **Saved and hashed with the place**: a field's record and a crate's each gain their two days of fresh food. A save
  from before has none, so everything in it lies, and rots from its first dawn (saves are not migrated). What came
  in before the load is that much older than it was, which is no loss worth keeping a migration for.

## Current tuning

Tuning, not rules (`needs_plan.md`, "Current tuning"): named constants in `Economy.h`, these figures until retuned.

| | Rots a dawn, lying out | Half gone in about |
|---|---|---|
| Fruit | 10% | 7 days |
| Greens | 10% | 7 days |
| Wheat | 4% | 17 days |
| Beans | 2% | 34 days |
| Provisions | 1% (proposed, not yet talked through) | 69 days |
| Grace before any of it | 2 days | |

Water, wood, hemp, rope and stone do not rot. A year is 40 days.

**What it comes to** (arithmetic, not measured): a field harvested every T days with nothing fetched settles just
after a harvest at about the harvest over `1 - (1 - share)^T`: wheat every 15 days about 2.2 harvests, beans every
20 days about 3, greens every 8 days under 2. The bug report's pile would be down to about 190 wheat, 450 beans and
none of its greens a year on, from the old piles alone.

## Seeing it (proposed with the rule, 2026-10-24)

- **Counted, not told one by one**: what rotted goes into the ledger's row of the day, by good and by colony, like
  the trees felled - an event of its own (`EVENT_ROTTED`), COUNTED but not JOURNALED, with no notice and no sound.
  The Statistics page and `chasm_stats` show it.
- **A field's card and a crate's** say what lies there and how it is keeping: "fresh for one more day", or "rotting:
  about 3 wheat a day".
- **NEEDS ATTENTION**: "FIELD: HARVEST ROTTING" for a field with food rotting on it that no store of its colony has
  room for - what the bug report's colony needed telling. Not for a field whose harvest is only slow to carry: that
  is the cost of a store far from the fields.
- **FOOD in the top bar** keeps its sum. Its hover gains a line for what lies out on fields and in crates, beside
  provisions, which are not in the sum either.
- `chasm_economy`: each field and crate reports its fresh food and what lies.

## Build order

1. **The rule**: the two days' fresh food on fields and crates, the dawn step, the shares, rounded up; saves and the
   hash; `chasm_economy`. Checked by arithmetic on a crate put down in debug and stepped over its dawns, and on a
   field harvested with no store.
2. **The count**: `EVENT_ROTTED` in the ledger by good and colony; the Statistics page and `chasm_stats`.
3. **The signals**: the cards, NEEDS ATTENTION, the FOOD hover.
4. **The wiki**: Food, Field, Crate, Statistics.

## Later, not in this plan

- **Keeping in a store**, the crop's own (`economy_plan.md`): greens and fruit rotting when winter comes, wheat a
  little each season, beans not at all, provisions keeping long. Stored food keeps, for now.
- **The cold**: whether rot slows in winter or under the snow. The same all year, for now.
- **A rotting look** on a field's harvest or a crate.
