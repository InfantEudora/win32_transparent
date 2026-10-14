# Zombies: the threat, dropped in by a debug tool first

Talked through with the user 2026-10-13 and 2026-10-14. P6 in `gameplay_plan.md` - zombies from the swamp, walls,
archers, ballista towers - had its groundwork built by `defence_plan.md` (orders, the pack, provisions, the guard
tower, gate doors) and `range_plan.md` (archery, the Shoot clip), and `selection_plan.md` lets the player move
people as a group. What was missing is the enemy. This plan is the enemy: first a zombie dropped into the running
game by a debug tool, to find out how biting and shooting read from the game's camera, then the waves.

**All of it is rules**: a zombie is saved, hashed and replayed as a person is, and only recorded commands and the
tick change it.

## Decided (the user, 2026-10-14)

- **Damage comes off the HEALTH bar** - the food meter (`needs_plan.md`) - to start with. No second bar.
- **Zombies go for PEOPLE ONLY.** Walls, gates and buildings are only what stands in their way.
- **The dead do not rise.**
- **Waves of TENS**, not thousands: a colony starts with one or two archers.
- **The figure is `zombie` in `chasm_props.glb`**, unrigged and with no clips for now. It gets the man's clips later.
- **A debug tool first** (the user's, 2026-10-13): zombies dropped into the scene as it is, the way the colony itself
  was grown - one scene extended until it plays.

## Why it is won by placement

A colony is 12 settlers, nobody arrives, and a child works at 2½ years - 1 h 15 of play (`lifeline_plan.md`). A
wave that kills three people is a disaster, not a skirmish. So what a wave should usually cost is what can be
rebuilt - a wall, a harvest, a gate - and a person should die when the player left him exposed, not as the price
of every fight. That is Stronghold and Bad North (few units, where they stand is the game) rather than They Are
Billions (a cascade of the infected).

## Proposed (2026-10-13; not yet agreed point by point)

- **Zombies walk the people's graph as a third WHO** (`Walkers.h`): the hook `ramparts_plan.md` left for them.
  Walls and shut gates are closed to them at first; later they cost TIME TO BREAK instead, so a zombie goes round
  if round is short, through a gate if it is open, and breaks the weakest place if all is shut - a gate is a
  deliberate weak point, and the place for towers. A wave shares one map of the way to the village (a flow field)
  rather than an A* each.
- **Shooting moves into the rules**, from what is built: hit or miss from archery's scatter (the range's `ShotOf`),
  decided by a hash of the tick, the shooter and the target - never the shared RRandom (the open `rrand` issue);
  reach by Exploration's line of sight, so a higher post shoots further; a shot a turn of the Shoot clip (5 s).
  A zombie at swamp pace takes about four shots from an archer over 20 units of open ground - one archer stops
  about one zombie. A wall that holds them under fire multiplies that: **a wall is time, and time under fire is
  kills.**
- **Ideas for later**: winter raids on the frozen swamp, where the zombies lie dormant (the posted already cross
  ice, scouts carry two weeks out, and the thaw cuts off whoever is late); the swamp's haze thickening a day or two
  before a wave; a field crossed by a zombie losing that cell's crop; zombies guarding finds under the clouds
  (`crates_plan.md`); spearmen holding a gate by strength; arrows as a tower's stock, kept by the caretakers; the
  ballista.

## The first slice

1. **A zombie, dropped by debug** - it walks to the nearest person (below).
   1b. **Spacing out** - people and zombies stand apart from their own kind, on the way and where they stop (below).
2. **It bites**: next to someone, his health goes down at a cadence. People do not react yet. (Built as a fight:
   both sides hit - below.)
3. **Guards shoot**: a tower's guards and the posted archers shoot a zombie in range and in sight; a zombie has a
   few hit points and falls. The Shoot clip and the arrow's flight aimed at it. (Trainees on the line too - below.)
4. **Walls break**: a wall's and a gate's strength, the way's cost of breaking them, and a broken palisade plot a
   gap to build again like any line work.

After step 3 the user judges whether shooting reads from the camera, and how one guard against one zombie feels.
Waves from the swamp on the calendar, people fleeing and the alarm, the warnings and trampled fields come after.

### Step 1: a zombie, dropped by debug

- **A zombie** is `EconomyZombie` in `EconomyState::zombies`, an id from 1 never reused, where it stands and its
  route - the people's kind of route, all on the ground. Saved (`"zombies"`), and hashed only while there are any,
  so a colony without them hashes as before.
- **The command**: `CHASM_CMD_ZOMBIE`, recorded - subtype the plot, value[0] how many (at most
  `ZOMBIE_SPAWN_MOST`), spread out from the plot's point `ZOMBIE_SPAWN_SPREAD` apart; 0 clears every zombie.
  Refused on the wet, the mountain, a building or a wall; zombies are not held to the clouds.
- **The debug tools**, debug builds only: **Z** drops one on the plot under the cursor, in debug and in play mode;
  the panel's own Zombies section drops a number on the selected plot and clears them all; MCP `chasm_zombie`, ops
  `spawn`, `clear` and `status`.
- **Its target**: the nearest person out of doors and on the ground, by the straight line - not someone indoors,
  nor a guard on his tower's platform, whom it cannot reach. The nearest few are tried until one can be walked
  to. Looked for again every `ZOMBIE_LOOK_TICKS`, each zombie on its own tick by its id. With nobody, it stands.
- **Its walk**: straight where the line is clear, as a person's; otherwise the grid's A* as `WALK_WHO_ZOMBIE` -
  the villagers' rules, so walls and shut gates stop it for now and a building is entered only where its walk
  ends - and never a deck. At `ZOMBIE_PACE` of a person's speed, so anyone outwalks it. It stops `ZOMBIE_REACH`
  short of its target, and goes after him again once he has moved `ZOMBIE_REPLAN` from where its walk ends.
- **Drawn**: the `zombie` figure at the people's scale, one instance set for all of them, turned along its walk.
  Not drawn under the clouds in play mode.

### Step 1b: spacing out (the user's, 2026-10-14)

Seen after step 1: a group ordered somewhere takes places of its own at the end, but walks on top of itself on
the way - everyone on one path walks through the same plot points - and two zombies after one person stop on the
same spot. Agreed to come BEFORE step 2's decisions, since a bite's reach should be measured from where a body
stands, not from its route.

- **The route is the plan, the offset is the body.** A person and a zombie keep walking their route exactly as
  now - A*, straight walks, arriving and re-planning all read `pos` - and each gets a small OFFSET from it, `off`,
  saved and hashed. Where the body stands is `pos + off`: what is drawn, picked and outlined, and in step 2 what a
  bite reaches from. The economy's bookkeeping - on his post, at a store, what he sees - stays on `pos`.
- **Neighbours push the offset**: a tick's pass after everyone has moved, every body pushed away from those of its
  OWN KIND within `SPACING_RADIUS` - people from people, zombies from zombies. A zombie is never pushed off the
  person it reaches for, and several round one person spread into a ring. Pushes are summed from where everyone
  stood before any of them moved, so the order bodies are taken in changes nothing.
- **A walker steps aside, never back**: someone walking is pushed only across his way, so a group on one path
  walks side by side rather than stretching out; someone standing is pushed any way.
- **Keep to the right**: a walker with a neighbour ahead of him leans to his right as well, so two meeting on a
  road pass each other as traffic does instead of shoving.
- **Back to the route**: with nobody near, the offset drifts back to nothing, so someone alone walks his route.
- **Never through anything**: the offset is at most `SPACING_MOST`, and a body that would stand on the water, a
  wall, a building, a field or across a fence or a cliff from its route is pulled back toward it. The route is
  valid, and the body never strays far from it.
- **Who takes part**: everyone out of doors on the ground. Someone indoors, a baby carried, and anyone whose
  route leaves the ground (up a tower, on the castle) has his offset settle back to nothing.

What it does NOT do yet: make a crowd queue. Bodies squeeze, and still all get where they are going. A gate that
jams - crowd pressure slowing walkers where they are packed - is for step 4, when walls break.

### Step 2: meals, and a fight (the user's, 2026-10-14)

Asked of the user after step 1b, answering the questions below ("To settle at step 2"):

- **Eating and drinking take time.** Today a meal is done in a single tick: every whole unit that fits is eaten at
  once (`Consume`), its health or water there at once - a trip to a store waits 2 s and then eats it all - so
  nobody is ever seen eating. Now a unit at a time, its worth coming in over the meal, shown on the person's card
  - "eating beans", "drinking". Drinking too (the user's choice of the two).
- **No meal starts with a zombie near**, and one under way waits while it is.
- **People fight back**: both sides hit, so a zombie has HIT POINTS from now. A person's STRENGTH AND INGENUITY
  together decide how hard and how often he hits.
- **A fight at full health lasts about 3 seconds**.

Proposed with it (tuning, all of it):

- **A meal**: a unit of food takes `ECONOMY_MEAL_SECONDS`, of water `ECONOMY_DRINK_SECONDS`, whatever it is - its
  worth added a tick at a time, so the bar fills as he eats. Taken from the stock when he starts; one unit, then the
  next while more fits. **Near** is a zombie within `NEEDS_MEAL_SAFE` of him. A trip to a store waits only for its
  meals now, unit by unit, rather than 2 s and then everything.
- **The fight**: a zombie within `FIGHT_REACH` of whom it is after BITES him every `ZOMBIE_BITE_SECONDS`, a day of
  health a bite (`ZOMBIE_BITE`); he HITS the nearest zombie within the same reach every `FIGHT_SWING_SECONDS`, for
  `FIGHT_HIT` of its `ZOMBIE_HEALTH` - both by his FIGHT PACE, the average of his strength's and his ingenuity's
  `EconomySkillFactor`: harder and more often for a strong, clever one. Each side's first blow comes a whole
  interval after it is in reach. Both reckoned from where everyone stood at the tick's start, so neither goes first.
- **The sums**: an average person (5 and 5) at full health kills a zombie at 3.0 s, bitten six times - a day of
  health left. At half health he dies at 2.0 s, and so he does against two zombies. The strongest and cleverest (10 and
  10) kill one at 1.8 s; the weakest (1 and 1) die at 3.5 s, the zombie half alive. So one zombie is a fair fight
  for a fed villager, and hunger or numbers decide it - placement again.
- **Children do not fight back**; babies carried are never reached. **Nobody flees or stops what he does yet**:
  someone walking outwalks a zombie, and a fight happens where people stand - at work, at the camp, on a post.
- **A bite that empties the bar kills**, "killed by a zombie", a cause of its own; his meal waits while a zombie is
  near, so nothing he eats can save him mid-fight. A zombie at 0 falls and is gone.
- `ZOMBIE_REPLAN` comes down to 0.4, so a zombie keeps within reach of someone moving slowly.

### To settle at step 2 (settled above)

- **Eating heals at once.** A posted person eats from his pack, and a guard from his tower, a whole unit
  whenever it fits (`Consume`) - so with damage off the health bar a bite is undone on the next tick while either
  holds food. Food as a combat resource is the point (Knights and Merchants'), but not all at once: a meal
  should take its time, or a pack should heal only so fast.
- **A bite killing outright**: health at 0 from a bite must kill even when there is food to eat on that tick
  (`Needs` eats before it strikes the dead off) - a cause of its own, "killed by a zombie".
- **How hard and how often**: tuning, against a full bar of 7 days.
- **Someone indoors** is safe for now. Whether zombies wait at the door, or break in, is open.
- **Where a bite reaches**: a zombie may stand up to `ZOMBIE_REACH` + `ZOMBIE_REPLAN` from someone moving slowly
  before it goes after him again (as built, below). A bite's reach wants that slack, or `ZOMBIE_REPLAN` smaller.

### Step 3: guards shoot (the user's, 2026-10-14)

Today a shot is the view's alone: a trainee's arrows are drawn from a hash of his stint, and the rules never hear of
them. Now a shot at a zombie is the rules', saved, hashed and replayed. Asked of the user:

- **Who shoots: anyone with a bow who STANDS** - a tower's guards on their platform, archers posted by order on their
  post, and trainees on the range's line, who stop training while they shoot. An archer walking walks on.
- **One shot a turn of the Shoot clip, 5 s**, its arrow leaving the bow at the clip's release.

Proposed with it (tuning, all of it):

- **Whom he shoots**: the nearest zombie he can SEE - Exploration's line of sight over the ground, from his eye: on a
  tower's platform from its height, so a higher post shoots further; walls and buildings do not stop it. Not one the
  arrows already in the air will kill. Grown-ups only, as in a fight; with a zombie within `FIGHT_REACH` he fights it
  by hand instead.
- **Hit or miss**: a scatter each way, wider the further it is and narrower the more archery he has, drawn from a hash
  of the tick, him and the zombie - never the shared RRandom. Inside the zombie's body it hits, for `ARROW_HIT` of
  its `ZOMBIE_HEALTH`.
- **An arrow is the rules'**: it flies at `ARROW_SPEED` and does its harm when it lands - a hit following its zombie
  as it walks. A hit stays stuck in the zombie until it falls; a miss lies in the ground a few seconds.
- **Arrows without end** for now: a tower's stock of them is for later.
- **The sums**: a zombie walking at an average trained archer (archery 50%) from 20 units off gives him four shots,
  the first at 3.9 s; he lands about three of the four and usually drops it. An untrained one lands about two and
  finishes it by hand.

## As built: step 1 (2026-10-14)

Files: `Zombies.cpp` (new: `SpawnZombies`, `TickZombies`, `ZombieLook`, `ZombieRouteTo`, `EconomyZombieFacing`),
`Economy.*` (`EconomyZombie`, `ZOMBIE_*`, the state and the save's lists, the tick's call after the people move and
before `Needs`, `Restore`), `Walkers.*` (`WALK_WHO_ZOMBIE`: no deck, in `JoinOpen` and `GoalPlace`), `ChasmSave.cpp`
(`"zombies"`, `"next_zombie"`, only once there have been any), `ApplicationChasmEconomy.cpp` (the command; the hash,
at the end of the economy's part, likewise), `ApplicationChasmZombies.cpp` (new: the drawing, the panel, the tool),
`ApplicationChasm.*` (`CHASM_CMD_ZOMBIE`, Z, the prop loaded), `wiki/zombie.md`.

- **As planned**, with one thing more: a zombie that could get to none of the nearest is LOST (`f_lost`, saved and
  hashed) and looks only one time in `ZOMBIE_LOST_LOOKS`, as someone cut off from home tries every fourth look - a way
  that is not there costs A* all the ground it can reach. A zones change sends every zombie after someone looking
  again on that tick, by the same test `FollowZones` makes, so a replay re-plans on the same ticks.
- **Drawn with the people's material**, not the ground shader: no snow settles on one.
- **Checked** (debug, a copied exe on 8769, the default map, paused and stepped):
  - dropped 25 west of the camp, it walked 0.95 a second (`ZOMBIE_PACE` of 1.6), switched to whoever came nearer,
    and stopped 0.6 from Siem at the camp, facing him with its arms out;
  - with someone walking past, it went after them and switched as the nearest changed;
  - three dropped across the chasm, where nobody can be reached: lost, standing;
  - a guard on a tower 2.4 away was passed by for someone 21.75 away;
  - a recording started with zombies mid-walk in its save, with four more dropped and an order during it - 1,600
    ticks: two replays equal on every tick, nothing changed between ticks, and both ended where the run did;
  - a route that cannot be found, from the camp across the chasm, cost no more than the tool's own ~15 ms - but the
    camp's ground is closed in by rivers, so a larger region has not been measured.
- **Not checked**: the Z key (no tool presses keys) and the panel's buttons; a wall or shut gate stopping one (the
  villagers' rules, not tried with a zombie); hidden under the clouds in play mode.
- **Seen, for later**: two zombies after one person end on nearly the same spot - nothing keeps them apart. (Step 1b.)

## As built: step 1b, spacing out (2026-10-14)

Files: `Spacing.cpp` (new: `Economy::SpaceOut`, after the zombies move and before `Needs`), `Economy.h` (`off` on a
person and a zombie, `EconomyBody`, `SPACING_*`), `ChasmSave.cpp` (`"off"`, a person's only when he has one), the
economy's hash (likewise), `ApplicationChasmWorkersView.cpp` (`WorkerHeight` and the figure at the body),
`ApplicationChasmSelect.cpp` (`FeetOf`, so badges, the box and picking follow the body), `ApplicationChasmRange.cpp`
(a shot leaves from the body), `ApplicationChasmZombies.cpp` (drawn at the body; the tools' `off`, `body_distance`).

- **As planned**, with three things added once seen in the game:
  - **A zombie is kept out of a person's room** (`SPACING_BODY`), never the other way: pushed round him by the others,
    one stood inside him. Well inside its reach, so it changes nothing about reaching him.
  - **Looking ahead** (`SPACING_AHEAD`): two walking head-on passed only 0.33 apart, as the lean had a fifth of a
    second inside `SPACING_RADIUS` at their speed. A walker now leans right for one coming at him, or standing in his
    way, from further off - they passed 0.47 apart. One walking his way ahead is still left be until he is near.
  - **A zombie stood by its target faces him** (the view): pushed round him, they faced the way they came, some with
    their backs to him.
- **How it moves** (`SpaceOut`): the bodies swept in x order, every pair within `SPACING_AHEAD` - of one kind they
  lean and, within `SPACING_RADIUS`, push both; a person and a zombie within `SPACING_BODY` push the zombie. Two on
  one spot part by a hash of their ids. Then each offset moves, drifts back, is held to `SPACING_MOST`, and is checked
  by `EconomyStraightClear` from the route's point: refused, the one it had if still allowed, else half, else none.
- **Checked** (debug, a copied exe on 8769, the default map, paused and stepped):
  - four sent as a group 25 units off: their routes as close as 0.05-0.14, their bodies held at 0.55 most of the way,
    the offsets at most 0.36;
  - five zombies on one person: their routes all ended on one point (0.00 apart), their bodies 0.51 apart in a ring,
    0.38-0.76 from him, facing him;
  - two walking head-on along one line: each leaned to his own right, 0.47 apart as they passed;
  - a recording with a group order and six zombies, 1,600 ticks: two replays equal on every tick, nothing changed
    between ticks, and both ended as the run did.
- **Not checked**: a body against a wall or a fence (the refusal path ran only in the open); the person's card and
  outline on a body well off its route, by eye.
- **For later**: zombies after one person all stop on ONE point and only spread by their offsets, so past five or six
  they crowd within `SPACING_MOST` of it - each could stop at a place of its own round him, as a group's posts do.
  And a crowd still does not queue (above).

## As built: step 2, meals and the fight (2026-10-14)

Files: `Economy.*` (the meal on a person - `meal_good`, `meal_ticks`, `meal_worth` - and `swing`; a zombie's `health` and
`bite`; `Consume` taking one unit and starting its meal, `MealTick`, the trip to a store waiting for its meals,
`NEEDS_DIED_ZOMBIE` and `bitten_dead`; the `MEALS` and `A FIGHT` tuning), `Zombies.cpp` (`Fights`, `ZombieNear`,
`EconomyFightPace`, `Reachable` shared with the look), `Spacing.cpp` (a zombie pushed out of a person's room any way, even
walking), `ChasmSave.cpp` and the hash (`"meal"`, `"swing"` only while there is one; a zombie's `"health"`, `"bite"`),
`ApplicationChasmSelect.cpp` (the card: the meal's bar, "Fighting a zombie", "A zombie is near"), `ApplicationChasmTime.cpp`
("WAS KILLED BY A ZOMBIE"), the tools (a worker's `meal`, `fighting`; a zombie's `health`, `next_bite_ticks`), the wiki.

- **As planned**, with these settled on the way:
  - **"Combined" is the average of the two skills' `EconomySkillFactor`** - one pace for how hard and how often, so
    strength and ingenuity count alike.
  - **"Near" counts only for someone a zombie can get at.** A family indoors, or a guard on his platform, still eats
    with a zombie at the door: there is no fight to heal through.
  - **At a store with a zombie near he waits there** for it to go - walking off hungry would only send him straight back.
  - **When the last zombie falls, nobody fights on** (found testing: `Fights` returned with none left, and a card kept
    saying "Fighting a zombie").
- **Checked** (debug, a copy on 8774 - 8769 was the overseer window's):
  - a meal: wheat over 100 ticks, health rising 0.24 of a day a half second, the next unit begun as it ended;
  - a zombie dropped by someone mid-meal: the meal stood at 99 ticks left while it was near, and went on once it fell -
    the camp's grown-ups round it all hit it, and it fell in 1.3 s;
  - one against one, Bente (strength 3, ingenuity 5) at 5.2 days: in reach for 150 ticks - 3.0 s - six bites, five blows,
    dead with the zombie at 15 of 60, cause "a zombie". At 1.1 days her hunger pressed and she walked into it: nobody flees;
  - Jelle (9 and 5) at full health: the zombie down in 2.2 s, four bites, 2.9 days left - and eating from his pack at once;
  - Jelle against two: dead, as the sums said;
  - a recording with meals, an order and two fights, 1,500 ticks: two replays equal on every tick, nothing changed
    between ticks, and the same death on the same day;
  - in play, the cards: "Eating fruit" with its bar; "Standing guard", "Fighting a zombie" in red.
- **Not checked**: a scout's or a guard's meals from a pack or a tower, by a run (the same `Consume`); the card's "A zombie
  is near" and the meal's "waits" line, by eye.
- **For later**: a zombie lost behind a palisade but within `NEEDS_MEAL_SAFE` of someone keeps him from eating, though it
  cannot reach him.
- **Found by the user after, and fixed (2026-10-14): nobody filled up at home.** Someone at home acts only on his look,
  and a worker set out on his next errand at it - before `Needs` began a meal - so a visit home began one unit at most,
  eaten on the way; before meals took time, that one tick ate everything that fitted. In `village_last`, with 20 wheat
  put in his house, the woodcutter came home at 2.2 days, left within half a second, and stayed between 2.0 and 2.8 days
  for 6,000 ticks - one round's unit was water, and he ate nothing. Now he stays in for his meals, as at a store
  (`EatingAtHome`, before anything else at home; `NextMeal`, what `Consume` would take, split out of it): the same run
  ate him from 2.2 to 6.9 days in 11 s before he went out, and left at once the next time, at 6.2, with nothing that
  fitted. A recording of it replayed twice: equal on every tick. (That save is short of food too - one bean in the colony.)

## As built: step 3, guards shoot (2026-10-14)

Files: `Shooting.cpp` (new: `Economy::Shoot`, `ShotTarget` - who shoots, at which zombie, the arrows' flights), `Economy.*`
(`EconomyArrow`, the state's and the save's `arrows`, a person's `draw` and `aim`, the `ARROWS` tuning; `Shoot` in the tick
after the spacing and before the fight; a trainee's stint and a meal waiting while he shoots), `Zombies.cpp`
(`EconomyReachable`, shared), `Exploration.*` (`ExplorationSight::Clear`, a line of sight between two points; `SightReach`),
`ChasmSave.cpp` and the hash (`"arrows"`, `"draw"`, only while there are any), `ApplicationChasmZombies.cpp` (the arrows
drawn with the range's props; the tool's `arrows`), `ApplicationChasmWorkersView.cpp` (the Shoot clip by the rules' turn,
facing the zombie; a clip kept in step with its clock), `ApplicationChasmRange.cpp` (a trainee's straw arrow not left hanging
while he shoots), `ApplicationChasmSelect.cpp` (the card's "Shooting at a zombie"), the wiki.

- **As planned**, with these settled on the way:
  - **A turn has a target from its start**: with nothing to shoot at before the release, the bow comes down and the turn is
    lost; after it, the turn plays out. Several archers who see one zombie on one tick shoot in step.
  - **A meal waits while he shoots**, and none begins - so a guard with zombies in sight shoots rather than eats.
  - **The arrow leaves from his eye's ground and `ARROW_BOW` over it**, in the rules; the view draws it from the same point.
  - **A hit whose zombie fell first** - to a blow, or another arrow landing sooner - comes down where it stood, as a miss.
  - **The release beat is one number**: `SHOOT_RELEASE` (the view's) is now `ARROW_RELEASE` (the rules').
- **Checked** (debug, a copy on 8774, `village`, paused and stepped):
  - Bente (archery 50%) posted in the open, one zombie dropped 20 off: missed at 18.4 and 16.7, hit at 11.4, 6.9 and 2.3 -
    it fell 2.2 from her, as the sums said;
  - Lieve (no archery) on a tower placed by debug, two storeys up: took aim at 22.9 - past a ground archer's 20 - and hit two
    of seven;
  - two zombies at Bente: she shot the second at 1.7, then fought it by hand at 1.0, and won;
  - a trainee on the range's line: shot one down, and her stint ran on about 700 ticks later than it would have - the time
    she shot;
  - two guards by hand on one tower against ten: 22 arrows, 12 hits, four felled with exactly three hits each - none wasted
    on a zombie already doomed;
  - a recording with the tower, an order and two drops, 2,013 ticks: two replays equal on every tick, nothing changed between
    ticks. One started with arrows in the air and both guards mid-turn, 600 ticks: equal replays, ending exactly where the
    run did. A save mid-fight, loaded and run 600 ticks: everyone and every arrow as before;
  - in screenshots: the guards on the platform with their bows, an arrow leaving it, arrows stuck in zombies, misses standing
    in the ground; a guard's card "Shooting at a zombie".
- **Not checked**: the bow drawn and loosed, close up - the camera keeps too far for it; a shot over a cliff's edge, up or down
  (the sight's raster is 2 units a cell); hit rates measured over many shots in the game (the hash was, against a true random,
  within a few percent at 8 to 18 units).
- **Found, not changed - for the user**: **a guard is taken off his tower by the job filling.** With Bente the range's archer
  and Lieve the tower's automatic guard, Bente's death opened the range's place, and the filling gave it to Lieve on the same
  tick: she climbed down mid-wave to go home, and died. A guard by hand stays. Whether an automatic guard should keep his tower
  over another place - or anyone keep his place while zombies are about - is for the user. And as in step 2, a bite that takes
  someone below a pressing hunger sends him off his post for food, through the fight.
- **For later**: trees and walls do not stop a shot or a sight; a guard on a tower sees no further for EXPLORATION, only for
  shooting (`TickExploration` looks from the ground); arrows run out never.
