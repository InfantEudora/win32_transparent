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
  *Done 2026-10-18:* rigged by the user (`zombie_armature`, a copy of the man's), drawn skinned with his Walking and
  Idle (`ApplicationChasmZombies.cpp`, UploadZombieSkins).
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
   3b. **Guards hold their posts** - kept until released, held by zombies in range, a pack of water each (the user's,
   2026-10-18, below).
4. **Walls break**: a wall's and a gate's strength, the way's cost of breaking them, and a broken palisade plot a
   gap to build again like any line work - houses breached, fences climbed, builders repairing (the user's,
   2026-10-18, below).
5. **The alarm**: the gates shut and everyone not on guard hiding in his house (the user's, 2026-10-18, below).

After step 3 the user judges whether shooting reads from the camera, and how one guard against one zombie feels.
Waves from the swamp on the calendar, people fleeing, the warnings and trampled fields come after.

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

## Step 3b: guards hold their posts (the user, 2026-10-18)

The two things step 3 found "for the user" are answered, and built the same day ("As built: step 3b", below). **How
shooting reads from the camera, and how one guard against one zombie feels**, the user is looking at in the game.

- **A guard keeps his tower until the player releases him**, as someone ordered out keeps his post. The job filling
  never takes him off it: not for a place opening somewhere else (Lieve, above), not for a better archer, not for a
  shortage of people. He leaves it only when released, when the tower's count is lowered past him, or when the tower
  is gone.
- **Zombies in range keep him there**: a pressing need does not send him off for water or food while there is a zombie
  he could shoot or is fighting. Once none is left in range he goes, as now.
- **Every guard carries water for a few days**, as the posted do.

**What there is today** (read 2026-10-18): a guard already counts as posted (`EconomyPosted`) and already has the posted
person's pack (`NEEDS_PACK_*`: 3 water and a provision's worth), eaten from only for a kind his tower has none of
(`Needs`). But it is filled only when he sets out from home (`TakePost`) or on a supply trip, so an automatic guard who
goes up straight from an errand climbs with whatever is in his pack already, usually nothing. And `AssignJobs` keeps only
a hand-picked guard (`f_chosen`) out of the filling.

**Proposed with it**, to settle when it is built:

- **Kept**: a guard who has taken his tower is reserved in `AssignJobs` as a hand-picked one is. Lowering the count
  still sends the automatic guards home first; "by hand" keeps its one meaning, that the player's pick takes an
  automatic guard's place.
- **His pack before he climbs**: on his way up, by his home - or the nearest supply, `SeekSupplyOf` - as a scout's
  gathering does with one stop, unless his pack already holds it. Topped up on every supply trip after, as now. The
  pack stays the posted one's size, `NEEDS_PACK_*`.
- **In range** is a zombie `ShotTarget` would pick for him, or one within `FIGHT_REACH`. **For anyone posted**, an
  archer ordered out as well as a guard; someone posted without a bow is held by a fight only.
- **Drinking under fire**: today a meal waits while he shoots and none begins, so a long wave can dry a guard out
  with water on his back. Whether he drinks from his pack between turns of the Shoot clip is the user's call. *Yes*
  (the user, 2026-10-18, after it was seen - below): built.

## As built: step 3b (2026-10-18)

Files: `Economy.cpp` (`AssignJobs`' keeping, `PackUp` and its two callers `TakePost` and `GoHome`, `SeekSupplyOf`'s
`f_towers`, the hold on the post in `TickWorker`), `Economy.h`, `Shooting.cpp` (`ZombiesInRange`, `DrinkBetweenShots`),
`tools/chasm_guard_test.py` (new), the wiki's tower, posts and zombie pages.

- **Kept**: `AssignJobs` keeps every guard as it kept the hand-picked one - the posted and the hand-picked first, then
  the automatic guards - so a count lowered still sends the automatic ones home first, and a shortage of people empties
  the newest of the OTHER workplaces.
- **Held**: on his post, a pressing need is not seen to while `ZombiesInRange` - a zombie within `FIGHT_REACH`, or one
  `ShotTarget` finds for a shooter. It asks with none of the arrows in the air counted, since otherwise he was free to
  walk off the moment every zombie in sight was doomed by arrows on their way. For anyone posted.
- **Packed**: `PackUp`, where a guard sets out for his tower (`TakePost`, and `GoHome` for one coming off an errand or a
  supply trip): with less than `NEEDS_PACK_WATER` in his pack, the nearest place with water (`SeekSupplyOf`, towers
  left out), where he packs as anyone posted does on a supply trip. Never once he stands on his tower. Each stop takes
  water from where it is, so it ends - his pack full, or nowhere left with any.
- **The test**: `python apps/chasm/tools/chasm_guard_test.py --port <port>`, against a DEBUG copy (it replaces the map).
  Each case is built on a flat test map (`test_maps_plan.md`) from nothing and run paused:
  - `shortage`: a builders' hut painted with nobody free;
  - `death`: Lieve's case - the range's archer dies;
  - `player`: release, count up, count down;
  - `wave`: tower and pack dry, thirsty, three zombies in sight;
  - `pack`: setting out from an empty home;
  - `pack_errand`: made a guard on the range's line.
  Before the change `player` passed and the other five failed, each as the user saw it - the guard made a builder, made
  the range's archer, off to the store with three zombies standing, up the tower with 2 water and with none. After it,
  all six pass, about a minute in all.
- **Checked**: a recording of 3,003 ticks - the shortage, four zombies, the guard made thirsty - replayed twice: equal on
  every tick, nothing changed between ticks, and both ended as the run did.
- **Seen - drinking under fire mattered more now.** In that recording the guard, his tower emptied and his water set to
  1.2 days, ended the run on his tower DRY with 3 water in his pack: a meal waits while he shoots, and the hold no longer
  let him walk off for it. In the `wave` case three zombies took 2,750 ticks (55 s) to bring down, and he went dry in
  it. Dry, health falls three times as fast, so a long enough wave would have killed a guard of thirst on his post.
- **A drink between shots** (the user's yes, the same day): `DrinkBetweenShots` (Shooting.cpp), asked in `Shoot` as a
  turn of the Shoot clip would begin. A drink under way, or one begun then, holds the turn - the bow down, as with nobody
  to shoot - and the next turn follows it. Water alone, offered to `Consume` on its own so a drink is what it begins:
  from his tower's water, or with none there his pack's, as `Needs` would; a unit only when it fits, so about one drink
  of a second every two days of the bar, against turns of 5 s. Food still waits for the wave - slower to eat, worth less
  a unit, and hunger kills slower. Not with a zombie within `NEEDS_MEAL_SAFE`, as for every meal.
  - A seventh case, `drink`: his tower dry, his pack full, thirsty at 1.2 days, three zombies in sight. On the build
    before it he drank nothing in a 2,500-tick wave and fell to 0.10 days; on this one he drank from his pack at tick 250
    - his turn's end - never fell below 1.10 days, and shot on: the wave took 2,550 ticks, a second more. All seven pass.
  - The same 3,003-tick recording made again: two replays equal on every tick, nothing changed between ticks, and
    stopped at the recording's end each was where the run was - 1.95 days of water, a unit gone from the pack.
- **Not checked**: on a chasm map - none of today's saves has a tower and a wave in it, and a save with towers now staffs
  differently, as it should; an ordered archer held by zombies (the same `ON_POST` path, not run); a guard's pack stop
  in a settlement whose only water is a collector.

## Step 4: walls break (the user's, 2026-10-18)

Talked through after step 3b. Today a standing palisade plot, a shut gate and every building are CLOSED to a zombie, as
to a villager: a colony walled in with its gates shut cannot be touched, and its zombies stand LOST outside. This step
makes them obstacles that cost TIME instead - "a wall is time, and time under fire is kills" (Proposed, above).

**Decided** (the user, 2026-10-18):

- **A gate is weaker than a palisade plot** - the deliberate weak point, and so the place for towers (and for the
  gatehouse, `defence_plan.md`, "Open").
- **Houses are BREACHED, not broken**: in far less time than a wall, and then passable to zombies. Nothing of the house
  comes down.
- **A garden's wall and a lot's fence are a slow crossing** for a zombie - climbed, not broken.
- **Only builders repair**, and only with no zombies about: a wall, a gate, a breached house. Not the jobless, who
  build line works today.
- **Gate jams** - crowd pressure slowing people where they are packed (step 1b) - are left for later.

**Proposed with it** (tuning, all of it, to be measured on test maps):

- **Breaking is a cost on the way, not a wall.** A zombie's walk (`WALK_WHO_ZOMBIE`) may enter a standing palisade
  plot, a shut gate or a house at the cost of its strength left, in seconds, on top of the walk - where today
  `EdgeOpen` shuts it. The route search already adds seconds edge by edge (`EdgeSpeed`: roads fast, fields and the
  swamp slow), so round-if-shorter, through-an-open-gate and the-weakest-place-if-all-is-shut all come out of the one
  search. A zombie walks about a unit a second, so one that a palisade plot holds for a minute goes round any stretch
  of wall it can pass in less than about 58 units: a short wall is walked round, and only a closed ring is broken.
- **Strength, in zombie-seconds**, a named constant each: a palisade plot `ZOMBIE_BREAK_WALL` 60, a gate
  `ZOMBIE_BREAK_GATE` 30, a house `ZOMBIE_BREACH_HOUSE` 10. A garden's wall or lot's fence costs `ZOMBIE_CLIMB_SECONDS`
  3 to cross and takes no harm. Against them: two guards drop a zombie about every 10 s, so a gate holds one zombie
  easily and loses to four.
- **Hitting**: a zombie whose next step is a closed thing, and within reach of it, stands and hits it - a second of
  strength a second. Several at one face add up, but only as many as stand there after spacing (step 1b) - three or
  four - so a crowd at a gate breaks it sooner without ten of them doing it ten times as fast.
- **The harm is the rules'**: per palisade plot, per gate and per house, what has been taken off its strength - saved
  and hashed only while there is any, as the zombies are. Built from scratch, a plot is whole.
- **Broken**: a palisade plot at 0 goes back to a wall SITE - unbuilt, so open ground, as a planned wall is - through
  the zones the way a laid road is raised (`ground_raises`, `ZONE_OP_GROUND_RAISE`), and half its wood
  (`ECONOMY_WALL_WOOD`) is left on it as a crate (origin `broken`). A gate at 0 is open, its door broken. A house at
  0 is BREACHED: passable to zombies, a flag on the building, until repaired. The breach is told -
  `EVENT_WALL_BROKEN`, `EVENT_GATE_BROKEN`, `EVENT_HOUSE_BREACHED` - for the notice line, the journal and the cues.
- **Repaired by builders**, an errand after keeping the towers full and before building, and only with no zombie
  within `ECONOMY_REPAIR_SAFE` (25 units, about a tower's sight) of it: a harmed plot or gate made whole again, and a
  breached house shut, for a builder's time at his pace; a broken plot rebuilt as a wall site, its wood carried - the
  crate it left first. The jobless never repair (the user's), and the line works' other plots go on as today.
- **Whom it goes for, by the way there.** Today a zombie tries the four people nearest by the straight line; with
  breaking, almost everyone is reachable at some cost, so it would go for the one nearest behind the wall over a
  woodcutter outside it. A **flow field** (Proposed, above) settles both that and a wave's cost: one search outward
  from every person a zombie can get at, by the zombies' costs, on a fixed cadence - every plot then knows its time
  to the nearest person, and each zombie walks downhill. Built in this step, or with the waves at the latest.
- **Nothing reaches through a wall.** A bite and a blow ask only that two bodies be within `FIGHT_REACH`; a zombie
  pressed against a palisade must not bite someone the other side of it. Checked first.
- **Seen and heard**: a palisade plot's stakes lean and drop out as its harm grows, a gate's door hangs, a breached
  house's door stands open (the view, from the rules' harm); a thud on the cue table for each blow, and the
  breaches told as events.
- **Not in this step**: a tower is never breached (its ground is reached only by its ladder, and zombies never climb);
  the camp, a winch and a standing bridge are open ground already; moats (water dug) and the ballista are later.

**Build order**, each tested before the next on flat test maps (`test_maps_plan.md`) by a `tools/chasm_wall_test.py`
in the guard test's way, and by a replay:

1. **4a**: a standing wall and a shut gate stop a zombie today (never checked), and nothing reaches through one. *Done
   2026-10-18, below - both hold, with no change to the rules.*
2. **4b**: breaking as a cost of the way; hitting; the harm saved and hashed; broken plots, broken gates and breached
   houses; the events. Cases: a short wall walked round; a closed ring broken at its gate; a house on the ring breached
   before its gate; a fence climbed; the times as the strengths say. *Done 2026-10-19, with the zombie's scratch
   clip (the user's), below.*
3. **4c**: repair and rebuilding by the builders, only with no zombie near; the crate.
4. **4d**: the look and the sound. *The sound done 2026-10-19 (below); the look to come.*
5. **4e**: the flow field, if not already in 4b.

## As built: step 4a (2026-10-18)

Nothing in the rules changed: what 4a asked holds today. New: `tools/chasm_wall_test.py`, in the guard test's way -
`python apps/chasm/tools/chasm_wall_test.py --port <port>` against a DEBUG copy (it replaces the map), each case on a
flat test map (seed 5, 4,000 cells) with a ring of palisade 28 by 20 drawn by `chasm_road`, run paused, about 80 s in
all. Before any zombie is dropped it asks the walkers' own A* (`chasm_pick` with a `to`) that the ring is closed, or
that the road's gate opens it, so a pass is never a ring with a gap.

- `ring`: three zombies dropped outside, one person inside - all three LOST, none inside, nobody bitten, 1,500 ticks.
- `gate_shut`: a road through the west wall, its gate shut - the same.
- `gate_open`: its gate open - the control: one came in through it at tick 720 and was fought at 1,140, so the ring
  and the fight are real and a quiet `ring` means something.
- `through`: a man on the plot nearest the inside of the north wall; outside, a runner ordered along it, its line
  stepping between z 9 and 12 on this grid, his walk hugging it, three zombies after him. Nobody fought; the nearest
  any zombie came to the man was **4.40 units** against `FIGHT_REACH` 1.0.

**Why nothing reaches through, by the grid and not by a rule**: a body stands on a plot's point or on the walk between
two, off it by at most `SPACING_MOST`, and a palisade is a whole plot wide - so with one between them two bodies are
the wall plot and a plot's walk on either side apart, about four units. A rule that a bite and a blow need nothing
between the bodies (the straight-walk test without its water and fields) was weighed and left out: nothing in the game
today can bring two bodies within reach across a palisade or a shut gate. `through` is kept to ask again in 4b, when a
zombie first stands against a wall to hit it - and a fence, a line between two plots with no width, is climbed from
then, not reached over.

All four ran twice with the same figures to the tick. Seen along the way, for `test_maps_plan.md`: a flat map with
people put down by `add_person` logs a statistics check error every dawn - "row day 0: 2 people, but the day before's
0, 0 born and 0 dead make 0" - since the people the command adds are neither born nor settled in the journal.

## As built: step 4b (2026-10-19)

Files: `Walkers.*` (`WalkBreaks`; `EdgeOpen`, `FindPath` and `PlanRoute` taking it), `Zones.*` (`ZONE_OP_GROUND_BREAK`,
appended), `Economy.*` (the strengths, `EconomyHarm` and the state's `harm`, a zombie's `hit`, the three events,
`GroundBreaks`/`GateBreaks`, `ZombiesHit` in the tick after `TickZombies`), `Zombies.cpp` (`ZombieBreaks`, the route,
`ZombiesHit`, `ForgetHarm`, `CanGetAt`), `ChasmSave.cpp` and the hash (`"harm"`, a zombie's `"hit"` - each only while
there is any), `ApplicationChasmEconomy.cpp` (the breaks applied to the zones), `ApplicationChasm.cpp` (a broken door
refuses to shut), `ApplicationChasmZombies.cpp` (the scratch, the tool), the notice line, the journal, the cue table, the
wiki's zombie, house, roads-and-walls and events pages.

- **The way**: a zombie's walk is planned with `WalkBreaks`, which the economy answers - so `EdgeOpen` lets it through a
  standing palisade plot, a shut gate or a house (one that stands) at the seconds left of it, and over a garden's wall or
  a lot's fence at `ZOMBIE_CLIMB_SECONDS`. Every other building, a castle's stone, a tower and its ladder stay shut.
- **At it**: the first such thing on the way ends the walk - ZOMBIE_HIT_STAND off a palisade's or a gate's plot point,
  close to its stakes, or just outside a house's wall - and at the end of its walk it hits: a tick of strength a tick,
  the first `ZOMBIE_HITTERS` at one thing by id. A zombie at what it breaks keeps that way until something changes.
- **Broken**: a palisade plot goes back to a wall site (`ZONE_OP_GROUND_BREAK`, as a laid road goes up) and its harm is
  forgotten; a gate's door is opened (`ZONE_OP_GATE_SHUT` 0) and its harm kept at strength - BROKEN, and a shut refused,
  "its door is broken"; a house is BREACHED, walked through from then on, and whoever is indoors there `CanGetAt`.
  Each is an event - `wall_broken`, `gate_broken`, `house_breached` - journaled, told on the notice line ("the zombies
  have broken the wall"), chimed. Every zombie looks again on the next tick. Harm to what has gone - a plot taken down,
  a gate's road taken up, a house erased - is forgotten as the zones change.
- **The scratch** (the user's, 2026-10-19: `Zombie_Scratch`, its one attack): played, at rate 1 and on its own step,
  while whom it is after is within reach (its bite counting down) or at what it breaks - facing that, along its last
  leg, rather than its target behind the wall. A props file without the clip stands it in its idle.
- **Found on the way, and fixed**: the first build let zombies through a wall for nothing. To touch its stakes a zombie
  stands inside the palisade plot's own cell, and at its next look - every half second - it planned again from that
  plot; a walk may always leave the plot it starts on, so its way ran on through the wall. Now a zombie at what it
  breaks keeps its way, and one stood in what stops it plans from the neighbour it came from.
- **The test**, `tools/chasm_wall_test.py`, now 4b's - seven cases, all passing (about 2.5 minutes):
  - `ring`: wall plot 2649 broken at tick 2,110 - three at it, its harm rising 3.00 s a second, 20 s from the first blow -
    and in at 2,290;
  - `gate_shut`: the gate broken, not the wall, at 1,124, twice as fast; shut again: "its door is broken";
  - `gate_open`: in through it at 720, fought at 1,140, nothing broken - the control;
  - `round`: a palisade 8 long between them walked round, nothing broken;
  - `house`: two of the west wall's plots taken out - a real gap a villager walks through - and a house of three plots
    over it, someone living in it: the house breached in 10/3 s by three at it, and then all three after the one
    indoors. (The first setup swapped a single plot for the house; its neighbours in the line met without it, so the
    house sat outside the ring and the zombies rightly broke the wall behind it - 60 s, against 10 and 60.)
  - `fence`: someone in a house's garden nobody else can walk into - the zombie climbed in, nothing harmed;
  - `through`: three breaking the plot in front of a man standing behind it: nearest body 3.91 against `FIGHT_REACH`
    1.0, nobody fought until it broke - so still no rule needed, as 4a found.
  The guard test's seven cases pass with their figures unchanged.
- **Checked**: recorded from a state with three zombies already hitting a shut gate - harm and `hit` in the save the
  recording starts from - through the break, the way in and the fight, 2,400 ticks: two replays equal on every tick,
  nothing changed between ticks, and both ended where the run did. Screenshots: three at a gate, crouched against its
  door, clawing, their pose changing frame to frame.
- **Not checked**: the notice line's words and the breached events' chime, by eye or ear; the scratch while biting (the
  same choice in the view, by `bite`); a gate two plots wide (either plot's break opens both, as the door's shutting
  does); a lot's fence (a garden's only); a chasm map.
- **For later** (4c-4e): a broken plot is a wall site like any, so for now the line works rebuild it - the jobless, even
  mid-wave - until 4c makes it the builders' with no zombie about, with the crate of half its wood; nothing is repaired
  yet, so a gate stays broken. Stakes and doors are drawn whole however harmed (4d). Whom a zombie goes for is still by
  the straight line (4e): breaking makes nearly everyone reachable, so it goes for whoever is nearest, walls or not.
- **For the user**: only houses are breached. A store, a workshop, a builders' hut stays shut to zombies like a castle,
  so a ring of them is a wall nothing breaks. *Answered the next day: every building's door (below).*

## As built: every building's door, and the sound of it (2026-10-19)

**Every door** (the user's, 2026-10-19: "all doors in buildings should be breachable"). `ZoneKindHasDoor` (Zones.h) names
the kinds gone into at a door - a house, a store, a woodcutter's, stonecutter's and builders' hut, a water collector, a
ropewalk, a mine, a well - and the walkers, the route and the hitting ask it where they asked for a house. Not a camp, a
winch or a standing bridge, walked through already, and not a guard tower: its ladder is its door, and zombies climb
nothing. Renamed with it: `HARM_DOOR`, `ZOMBIE_BREACH_DOOR`, `ZOMBIE_DOOR_STAND`, `EconomyBreached`, `WalkBreaks::DoorLeft`,
and the event `EVENT_BREACHED`, "breached", its `amount` the building's kind - so the notice line says "the zombies have
broken into a store" and the journal "Zombies broke into a builders' hut" (`ChasmKindLabel`). People are indoors only at
home, so "whoever is indoors there can be got at" is still a home's. An eighth case, `store`: a store of three plots in the
ring's gap - breached by three in 3.3 s, the zombies in at tick 1,560; the other seven as before.

**The sound** (step 4d, half of it; the user's files, 2026-10-19), from `ApplicationChasmSound.cpp`'s `SignalZombies`, the
work sounds' way - the rules say what is happening, the cue table what it sounds like:
- **`zombie_strike`** (`bluntstrike_wood_1`, `_2`): a zombie at what it breaks strikes once a turn of its scratch, at its
  swipe - 0.8 s into `Zombie_Scratch`, where both arms turn quickest (read off the props file: one swipe a 2.29 s turn).
  Its clock counts the rules' ticks, as the clip is played, and starts at the phase the view starts the clip at
  (`UploadZombieSkins`), so the blow lands as the arms come down, at 4x too, and stops in a pause. The nearest four.
- **`punch`** (`punch_1`-`_3`): a fight's bite or blow, each on the pass the rules land it - a zombie's `bite`, or a
  person's `swing`, counting from the top again. Within the work's reach; four at once at most.
- Encoded as `tools/sound_ogg.py` does, the five alone (the speech takes beside them, `speech/warning/`, are another piece
  of work): 265 KB of wav to 28 KB, the wavs kept in `art_source/chasm/sound/`. Gains 0.35 and 0.4: the takes are 5-7 dB
  louder at their loudest than the axe's, and a fight should carry over a felling.
- *Checked*, a gate attacked live with the camera on it: each of three zombies struck every 115 ticks, a turn, staggered by
  its phase, the two takes alternating; the gate's chime as it broke; then the punches as the fight inside went - about ten
  a second, three zombies on one man and his blows. *Not checked by ear*: I have none.
- **For the user**: a bite lands every `ZOMBIE_BITE_SECONDS` (0.5) while the scratch swipes once in 2.29 s, so the punches
  run four times as fast as the arms. Either the clip or the cadence could give - the rules' 3-second fight was tuned
  before the clip existed.

Still to do of 4d: the look - stakes leaning and falling as a palisade is harmed, a gate's door hanging, a breached door
standing open.

Seen while testing, not the game's: now and then one request of the thousands a test run makes is never answered (three
times in two days), while the app goes on answering the next - the sim idle, no crash. Read-only questions in both test
scripts now time out after 30 s and are asked again; commands still wait, since one may have run.

## Step 5: the alarm (the user's, 2026-10-18)

Agreed with step 4: today a gate is shut one at a time from its card, and nobody takes cover. The alarm was agreed in
`defence_plan.md` ("Later") as one button that sends everyone with a post to it and shuts the gates.

**Decided** (the user, 2026-10-18): **with the alarm, everyone who is not on guard hides in his house.**

**Proposed with it:**

- **One recorded command**, on and off (`CHASM_CMD_ALARM`): a button on the top bar in play, a key, and a tool op.
- **Alarm**: every gate shut - the alarm remembers which it shut, so the all-clear opens only those; the guards are on
  their towers already (step 3b) and the ordered stay on their posts; EVERYONE ELSE goes home and indoors - work
  dropped where it stands, a load put down as a crate as for an order, children and babies with their families -
  trainees too, unless posted.
- **No house**: a camp's tents are no cover (a camp is open ground), so a camp's people and the homeless crowd into
  the nearest house of their settlement with room, as the CRAMMED do (`settlements_plan.md`); with none, they stay
  at the camp.
- **Hiding**: they eat and drink from their home's pantry as at any time; a pressing need does not send anyone out
  while the alarm is on - stocking the houses before a wave is the player's part.
- **All clear**: the gates the alarm shut open, and everyone goes back to what he does.
- **A breached house is no cover** (decided, the user, 2026-10-18). Today nobody indoors can be reached. With this
  step the people in a house become reachable once it is breached, and zombies go for those hiding inside at the cost
  of breaching it - so hiding buys time under the guards' fire rather than safety, and keeping zombies off the houses
  is the defence's job. Grown-ups inside fight back as anywhere (step 2). Without it the alarm would make a colony
  untouchable - its zombies standing lost round shut doors while the guards picked them off.
- **Later**: an alarm the colony sounds itself - crows over the swamp, a guard who sees the first zombie.

The waves from the swamp come after step 5, with the calendar.

## Noted, for later (2026-10-18)

The smaller things found along the way, kept here so they can be picked up later or left. None is planned.

- **Shots and sight** pass through trees and walls; a guard on a tower sees no further for exploration, only for
  shooting; arrows never run out.
- **Zombies on one person** all stop on one point and spread only by their offsets, so past five or six they crowd
  within `SPACING_MOST` of it. A crowd does not queue: gate jams are left for later (the user, 2026-10-18).
- **A zombie lost behind a palisade** within `NEEDS_MEAL_SAFE` of someone keeps him from eating, though it cannot
  reach him.
- **Someone indoors is safe**: settled - houses are breached (step 4), and a breached house is no cover (step 5).
- **"Run out" notices** (`defence_plan.md`, the two messages): with several posted near home, one every eight days or so
  each may say too much.
- **Cold on a post in winter** (`defence_plan.md`, "Open").
- **Sites take the food and water people need** with nothing kept back (`defence_plan.md`, "Open").
