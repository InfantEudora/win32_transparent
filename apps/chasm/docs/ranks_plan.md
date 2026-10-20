# Kills and ranks

Talked through with the user 2026-10-19, as the first of the game's RPG elements. Everyone counts the zombies they
kill, by bow and by hand, and enough kills earn a RANK. Nothing is built yet.

The colony is about twelve people and nobody arrives (`zombies_plan.md`: won by placement, not by trading lives), so
a rank is not a ladder of bigger numbers. It marks someone who has been through the fights, and makes them worth
keeping alive. The rank from kills is They Are Billions' Expertcy, where units rise by what they have killed.

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
- **The ranks are Novice, Expert and Master.** Not an archer's names alone, since kills by hand count too.
- **A kill a rank for now**, to test them faster - but each rank keeps a count of its own, so any one can be given a
  different requirement later.
- **The old Expert teaching at the range** belongs with old age: `lifeline_plan.md`, "Old".

## Proposed

### Who gets the kill

- **The hit that fells it.** A zombie takes several arrows, often from several archers; whoever's hit takes the last
  of its health has killed it. No shares and no assists - it reads plainly on a card, and it is the hit the fallen
  already lie by (`ZombieFalls`).
- **An arrow** knows who loosed it (`EconomyArrow::by`). Its landing in `Economy::Shoot` (`Shooting.cpp`) already
  finds the arrow that fells (`f_alive && m.health <= 0`); `by` is credited there.
- **A blow by hand** is not tied to its striker today: `Economy::Fights` (`Zombies.cpp`) adds a tick's blows up per
  zombie (`blows`, with the last one's push in `blown`). Beside `blown` it keeps who struck: of those whose blows land
  on the tick it falls, the last in the people's order - the blow it falls by.
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
  Expert - 15 KILLS").
- **A death**: the pop-up and the journal name the rank and the kills - "Jelle, Expert, 15 kills, was killed by a
  zombie".
- **Debug and tools**: the People panel's "Put an archer down" gets a kills slider, through its command as the
  archery slider is, so replays hold; `add_person` takes `kills`; `chasm_economy` lists kills and rank.

## Current tuning

First guesses. Tuning, not rules: each becomes a named constant.

| What | Value |
|---|---|
| The ranks | Novice, Expert and Master, at 1, 2 and 3 kills (`rank_kills`, Economy.cpp) - **a kill a rank for now**, the user's (2026-10-19), to test with; each rank has its own entry, so any one can be set apart later. First proposed: 5, 15 and 40 |
| Pace, by rank | 5%, 10%, 15% quicker (`rank_pace`): a master archer's 3.5 s turn becomes about 3.3, 3.2 and 3.0 s |
| A one-shot kill, of a hit, by rank | 10%, 20%, 30% (`rank_one_shot`) |

Arithmetic from the current tuning: three hits fell a zombie (`ARROW_HIT` of `ZOMBIE_HEALTH`), so with the one-shot
chances a ranked archer needs about 2.7, 2.4 and 2.2 hits of their own. At a kill a rank, anyone who kills three
zombies is a Master. At the first proposal's 5, 15 and 40, with waves of tens two or three a year
(`gameplay_plan.md`), three archers sharing some 20 to 30 kills a year would reach the first rank within the first
year, the second in the second or third, and the third would be a lifetime's.

## Build order

1. **(BUILT 2026-10-19)** **Kills**: the credit for arrows and blows, kept per person and on the dead, saved and hashed;
   `EVENT_ZOMBIE_FELLED`, the day row and the check; the card's line. Tested on a flat test map with posted archers
   and a few zombies: the credited kills add up to the zombies felled, a replay twice gives the same, and a save
   loads the same.
2. **(BUILT 2026-10-19)** **Ranks**: the thresholds, the pace, the one-shot kill, `EVENT_RANKED`, the card, the death pop-up, the debug
   slider and the tools.

## Later: the perks talked about

Kept from the same talk, for the user to think over; none agreed. Each changes what a person does rather than a
number, and each comes from one of the games Chasm draws on.

- **Choosing a target.** A beginner shoots the nearest zombie; someone with a rank shoots the most dangerous - the
  one biting a person, or breaking a gate.
- **Nerve.** With the alarm and fleeing (`zombies_plan.md`, step 5), a beginner posted alone may break and run when
  a zombie comes near; a Expert holds. Who goes on the far tower becomes a choice.
- **Steadying those near.** Archers within a few plots of the highest rank shoot quicker or tighter, so where they
  stand matters - placement again.
- **A choice at a rank** (Bad North, where a commander takes a path as they rise): a longbow, seeing and shooting
  further from a tower, or quick hands, shooting faster.
- **The old Expert teaches** - agreed, in `lifeline_plan.md`, "Old".
- **A mark on the figure** for a rank - a feather, a sash - once there is a look for it.

## As built: step 1, kills (2026-10-19)

As proposed, with one change, marked.

- **The record**: `KILL_BOW`, `KILL_HAND` (`Economy.h`, KILLS); `EconomyWorker::kills` and `EconomyDeath::kills`, by
  how; `EconomyKills` adds the two up - what the rank will be worked out from.
- **The credit**: `Economy::ZombieKilled` (`Zombies.cpp`) counts the kill on the person, or on his record among the
  dead, and only then tells it - so the kills and the count cannot part. Called beside `ZombieFalls` where an arrow
  lands as the last hit (`Economy::Shoot`) and where a tick's blows take the last (`Economy::Fights`, its `striker`
  beside `blown`).
- **Changed from the plan**: a kill by hand is the **last** striker of the tick in the people's order, not the first -
  the blow the zombie falls by (THE FALLEN), so the way it falls and whose kill it is agree.
- **The event**: `EVENT_ZOMBIE_FELLED` (25, "zombie_felled"), `person` the killer, `amount` how, at the zombie's
  body. Counted, not journaled (`EconomyCounted`): `EconomyDay::zombies`. No sound of its own - it has no cue row,
  and the arrow's hit and the punch already sound the moment. The notice line does not tell it.
- **Saved** only when there is something to save, so a colony without kills saves as before: a person's
  `"kills": [bow, hand]`, a tenth element on a dead record's line, a seventh on a ledger row. **Hashed** the same way,
  only when above 0, so it hashes as before too.
- **Shown**: the person's card, under archery - "Kills 2: 1 by bow, 1 by hand", or "Kills 2, by bow" when all one
  way. The Statistics page: a sixth tile, ZOMBIES KILLED, only once there has been one - a colony that never met a
  zombie (the demo) is not told of them. `chasm_stats`: `totals.zombies` and each row's `zombies`; `chasm_economy`:
  `kills` on a person and on the dead, once any.
- **The check** (`ChasmStatsCheck`): the zombies killed in the history against the kills of the living and the dead.
  No baseline is needed: the history begins at a landing or at the load of a save from before it, and nobody had
  kills then.
- **Checked** (debug, a copy on port 8775, flat test maps, paused and stepped): `tools/chasm_kills_test.py`, six
  cases, all pass -
  - `credit`: two archers, one zombie, watched tick by tick - the kill was the archer whose arrow landed as its last
    hit, and nobody else had one;
  - `bow`: two archers and four zombies, down in 1,110 ticks - 3 by bow, 1 by hand once one got close; killed 4,
    rows 4, kills held 4;
  - `hand`: a man with no bow, one kill by hand in 280 ticks;
  - `after`: an archer dying with his last arrow in the air - the kill on his record among the dead;
  - `saved`: kills on the living, the dead and the rows, saved and loaded: the same;
  - `replay`: a fight recorded and replayed twice - 1,501 ticks traced each time, equal on every tick, nothing changed
    between ticks, the kills where the run left them. The test removes its own recording.
  - The fallen test's six cases and the guard test's seven pass, the guard test's figures as before (the wave down in
    1,825 ticks, the drink case 1,925).
  - An A/B of sorts: `autosave_1.json`, written by the build before this one (13 people, a history, no kills), loaded
    and saved again at once - the same in every part but the autosave's own `meta`.
  - Screenshots: the card's line, and the page's tile beside the other five.
- **Not checked**: two people's blows landing on one zombie on the same tick (the rule is read from the code only);
  a release build.
- **On a test map** the statistics' check always reports its people line, since people put down by `add_person`
  were never born or settled in the history (`zombies_plan.md`, 4a). The kills test reads only the check's zombies
  line for that reason.

## As built: step 2, ranks (2026-10-19)

The user's, the same day: **a kill a rank for now**, to test with, each rank's count its own so any one can be set
apart later. As proposed otherwise, with two changes, marked.

- **The ranks**: `RANK_NONE` .. `RANK_Master` (`Economy.h`, RANKS); the tables `rank_kills`, `rank_pace` and
  `rank_one_shot` in `Economy.cpp`, an entry a rank; `EconomyRank` (the highest rank the kills reach),
  `EconomyRankName`, `EconomyRankKills`, `EconomyRankPace`, `EconomyRankOneShot`. Worked out from the kills every
  time, never kept: nothing new is saved for a person, and a rank cannot be lost.
- **The pace**: `EconomyShotTicks` divides a turn's seconds by the rank's pace. **Changed from the plan**: by hand
  only the blows' interval is quickened, not `EconomyFightPace` itself, which would have made them harder too - the
  user asked for speed. `Economy::Fights` works the interval out with the rank; the blow's harm is as before.
- **The one-shot kill**: `EconomyArrow::f_fells`, decided at the loose beside `f_hit`, from `ShotHash` with the
  zombie's id turned over (`~m.id`) so it is a number of its own, apart from the scatter's and the pace's.
  `EconomyArrowHarm` is what a hit takes - all of `ZOMBIE_HEALTH` for one that fells - used where it lands and in
  the doom count (`coming`), so nobody shoots at a zombie such an arrow is on its way to. Saved (`"fells"`) and
  hashed only when set.
- **Rising**: `EVENT_RANKED` (26, "ranked"), `person` and the new rank in `amount`, made by `ZombieKilled` when a
  kill takes the living to a new rank - never for the dead, though their rank is read from their kills all the same.
  Journaled ("Jelle De Wit rose to Expert"), on the notice line ("Jelle De Wit has risen to Expert"), with the
  notice chime (a cue row `ranked` in `assets/cues/chasm.json`).
- **Shown**: the card's subtitle starts with the rank ("Expert, age 24 - Guard"); its kills line says what the next
  rank takes ("Kills 2, by bow - Master at 3"). A death: `EconomyKillsTold` - the pop-up "Jelle De Wit, Expert,
  2 kills, of thirst" and the red line the same. **Changed from the plan**: the journal has only the rank, before the
  name - "Expert Jelle De Wit died of thirst" - since its column cut the longer line off before the cause.
  `chasm_economy`: `rank` beside `kills`, on a person and on the dead; `chasm_zombie`: `fells` on an arrow.
- **Kills given by the debug tools**: `add_person`'s `kills` (by bow, 0-31) and a kills slider beside the People
  panel's "Put an archer down" (and X), the rank they make shown beside it. They ride CHASM_CMD_PERSON's packed value,
  five bits over `PERSON_POSTED` (`PERSON_KILLS_SHIFT`), clear in older recordings. `AddPerson` counts them into the
  history's new `kills_from` - saved and hashed only when above 0 - so the statistics' check still holds: the zombies
  killed and the kills given are everyone's kills.
- **Checked** (debug, a copy on port 8775): `tools/chasm_kills_test.py`, ten cases now, all pass -
  - `ranks`: three zombies one at a time - after each, 1 kill Novice, 2 Expert, 3 Master; the journal's three
    lines in order; his card "Master, age 24 - No job / posted";
  - `given`: put down with 2 kills, a Expert at once; one zombie more, a Master, one journal line; kills_from 2;
  - `pace`: the first turn of the bow on the same map, the same tick and so the same jitter - 194 ticks with no rank,
    169 as a Master, where 194 / 1.15 is 168.7; a fight by hand 272 ticks against 257;
  - `one_shot`: a Master through eight zombies one at a time - six hits flagged to kill outright, two of them on a
    zombie at full health; no arrow loosed at a zombie one of them was already flying at; one saved and loaded in
    the air, still flagged;
  - `replay` now with a Expert: two replays equal on all 1,501 ticks, nothing changed between ticks;
  - the five step 1 cases as before.
  - The fallen test's six cases, the guard test's seven and the wall test's eight pass. The guard test's waves end
    sooner, as they should with guards rising by their kills: 1,575 ticks (1,825 before), the drink case 1,825 (1,925).
  - `autosave_1.json` loaded and saved again at once: the same but for the autosave's `meta`.
  - Screenshots: the card and the notice line as a Novice archer rose to Expert; the death pop-up and the red
    line for a Expert; the journal's line for him.
- **Not checked**: the People panel's slider by hand (ImGui cannot be driven from the tools; `add_person` rides the
  same command); the notice chime by ear; a release build.
