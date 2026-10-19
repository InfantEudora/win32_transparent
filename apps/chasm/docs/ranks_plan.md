# Kills and ranks

Talked through with the user 2026-10-19, as the first of the game's RPG elements. Everyone counts the zombies they
kill, by bow and by hand, and enough kills earn a RANK. Nothing is built yet.

The colony is about twelve people and nobody arrives (`zombies_plan.md`: won by placement, not by trading lives), so
a rank is not a ladder of bigger numbers. It marks someone who has been through the fights, and makes them worth
keeping alive. The rank from kills is They Are Billions' veterancy, where units rise by what they have killed.

## Decided (the user, 2026-10-19)

- **Kills are counted per person, by bow and by hand.** Both are kept and shown. Hand kills are rare today; they
  still count.
- **A rank comes from kills alone, all kills together**, bow and hand. Training at the range gives archery, which is
  how well someone shoots; a rank is what they have been through.
- **A rank is never lost.**
- **For now a rank gives two things**: a slight increase in speed, and a chance that a hit kills outright - a
  ONE-SHOT KILL. The perks talked about (below, "Later") are the user's to think over.
  - **The speed is the pace of fighting**: shots and blows by hand come quicker. Walking stays as it is.
  - **The one-shot kill is an arrow's**, not a blow's.
- **The ranks are Blooded, Veteran and Champion.** Not an archer's names alone, since kills by hand count too.
- **The old veteran teaching at the range** belongs with old age: `lifeline_plan.md`, "Old".

## Proposed

### Who gets the kill

- **The hit that fells it.** A zombie takes several arrows, often from several archers; whoever's hit takes the last
  of its health has killed it. No shares and no assists - it reads plainly on a card, and it is the hit the fallen
  already lie by (`ZombieFalls`).
- **An arrow** knows who loosed it (`EconomyArrow::by`). Its landing in `Economy::Shoot` (`Shooting.cpp`) already
  finds the arrow that fells (`f_alive && m.health <= 0`); `by` is credited there.
- **A blow by hand** is not tied to its striker today: `Economy::Fights` (`Zombies.cpp`) adds a tick's blows up per
  zombie (`blows`, with the last one's push in `blown`). Beside `blown` it keeps who struck: of those whose blows land
  on the tick it falls, the first in the people's order.
- **An arrow and a blow on the same tick**: whichever of `Shoot` and `Fights` runs first in the tick fells it, and
  that one is credited. No rule of its own.
- **An archer who dies while their arrow is in the air** still gets the kill, on their record among the dead, so the
  sums below hold.
- **Zombies cleared by the debug tool** are nobody's kill.

### What is kept

- **Per person**, the kills by bow and by hand. Saved, and hashed with the person once above 0, as archery is.
- **The dead keep theirs** (`EconomyDeath`), for the death pop-up and, later, a roll of the fallen.
- **The rank is worked out, not kept**: from the total and the thresholds (`EconomyRank`). Kills only ever go up,
  so a rank cannot be lost without a rule that takes it.
- **A zombie felled is an event**: a new `EVENT_ZOMBIE_FELLED` (appended - EVENT numbers are saved), `person` the
  killer and `amount` bow or hand. The statistics count it in the day row, and `wiki/events.md`'s proposed "one
  down" for sound is the same event.
- **The check** (`ChasmStatsCheck`): the kills of the living and the dead add up to the zombies felled in the
  history, as the trees felled already add up against the forest.
- **An older save** loads with no kills; saves are not migrated.

### What a rank does

- **Pace.** A rank makes a fighter quicker in both kinds of fight: a shot turn is shorter (`EconomyShotTicks`
  divides its seconds by the rank's pace) and blows come sooner (`EconomyFightPace` multiplies by it). Walking stays
  as it is: a quicker walk would also run every errand faster, which is not what a rank is for.
- **The one-shot kill.** A HIT from someone with a rank has a chance, by rank, to take all the zombie's health. A
  miss is still a miss, so archery still decides most. It is decided as the arrow leaves the bow, with `f_hit`, from
  a hash of its own (`ShotHash` with a salt, never the shared random stream) and kept on the arrow, saved and hashed
  with it.
- **Decided at the loose, so it counts against the zombie at once**: the doom count in `Shoot` (`coming`) counts
  such an arrow as the zombie's whole health. Nobody wastes an arrow on a zombie that is as good as down, and the
  archer turns to the next.
- **Arrows only** for now - "a shot". A blow by hand could have the same chance; left out until fighting by hand
  matters more.

### What the player sees

- **The person's card**: the rank by the name, and a line for the kills - "Kills 12: 9 by bow, 3 by hand".
- **Rising a rank** is an event, `EVENT_RANKED` (appended), journaled and on the notice line ("JELLE IS A
  VETERAN - 15 KILLS").
- **A death**: the pop-up and the journal name the rank and the kills - "Jelle, Veteran, 15 kills, was killed by a
  zombie".
- **Debug and tools**: the People panel's "Put an archer down" gets a kills slider, through its command as the
  archery slider is, so replays hold; `add_person` takes `kills`; `chasm_economy` lists kills and rank.

## Current tuning

First guesses. Tuning, not rules: each becomes a named constant.

| What | Value |
|---|---|
| The ranks | Blooded, Veteran and Champion, at 5, 15 and 40 kills (`RANK_KILLS`) |
| Pace, by rank | 5%, 10%, 15% quicker (`RANK_PACE`): a master archer's 3.5 s turn becomes about 3.3, 3.2 and 3.0 s |
| A one-shot kill, of a hit, by rank | 10%, 20%, 30% (`RANK_ONE_SHOT`) |

Arithmetic from the current tuning: three hits fell a zombie (`ARROW_HIT` of `ZOMBIE_HEALTH`), so with the one-shot
chances a ranked archer needs about 2.7, 2.4 and 2.2 hits of their own. With waves of tens, two or three a year
(`gameplay_plan.md`), a colony's archers share some 20 to 30 kills a year; three archers reach the first rank within
the first year, the second in the second or third, and the third is a lifetime's.

## Build order

1. **Kills**: the credit for arrows and blows, kept per person and on the dead, saved and hashed;
   `EVENT_ZOMBIE_FELLED`, the day row and the check; the card's line. Tested on a flat test map with posted archers
   and a few zombies: the credited kills add up to the zombies felled, a replay twice gives the same, and a save
   loads the same.
2. **Ranks**: the thresholds, the pace, the one-shot kill, `EVENT_RANKED`, the card, the death pop-up, the debug
   slider and the tools.

## Later: the perks talked about

Kept from the same talk, for the user to think over; none agreed. Each changes what a person does rather than a
number, and each comes from one of the games Chasm draws on.

- **Choosing a target.** A beginner shoots the nearest zombie; someone with a rank shoots the most dangerous - the
  one biting a person, or breaking a gate.
- **Nerve.** With the alarm and fleeing (`zombies_plan.md`, step 5), a beginner posted alone may break and run when
  a zombie comes near; a veteran holds. Who goes on the far tower becomes a choice.
- **Steadying those near.** Archers within a few plots of the highest rank shoot quicker or tighter, so where they
  stand matters - placement again.
- **A choice at a rank** (Bad North, where a commander takes a path as they rise): a longbow, seeing and shooting
  further from a tower, or quick hands, shooting faster.
- **The old veteran teaches** - agreed, in `lifeline_plan.md`, "Old".
- **A mark on the figure** for a rank - a feather, a sash - once there is a look for it.
