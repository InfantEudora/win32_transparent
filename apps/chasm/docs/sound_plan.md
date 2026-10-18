# Sound Plan: events, the colony at work, and the view

Proposed 2026-10-16, from an overview the user accepted ("very good overview"). Step 1 built with the
statistics; step 2 and the first of steps 3, 4 and 6 built the same day - see "As built" at the end. The
choices the user has not yet made are under "Open questions".

## What the user asked (2026-10-15)

- Do Chasm's sounds need to depend on state, as archer's do? The user thought not.
- Start with simple background sounds: sites being built, trees chopped, farmland, maybe footsteps and
  some talking.
- Some sounds belong to game events: NEEDS ATTENTION, someone born, someone dead, something new
  discovered. The demo plan already needs a few of these.
- Can general events and sound events share the same logic, or should they be separate?
- A list of the events the game can have, from what is in it now and what is planned.

## The answer, in short

- **Game events share one path.** A birth, a death or a discovery is made once, by the rules, as one
  event record. Everything that cares reads that record: the notice line, the pop-ups, the statistics
  journal, the discoveries, and the sound.
- **Background sound is not made of events.** Chopping, hammering, footsteps and talking are a count,
  each frame, of what is going on near the camera. The counts drive a few looping sounds.
- **One rule sorts most cases: the activity is background, its completion is an event.** Chopping is
  background and the tree falling is an event. The same goes for hammering and a finished storey,
  sowing and the harvest, and the stonecutter at work and a rock broken.
- **Little depends on state.** Archer's per-character machinery is not needed here. What is left is
  context as plain numbers: the season, the zoom, what the view looks at, and pause.

## What there is today

- **No sound at all.** Chasm's makefile does not set `USE_SOUND`, and nothing in the app touches
  `SoundSystem`, `CueSystem` or `MusicPlayer`.
- **Archer's cue layer is core** (`core/CueSystem.h`, `apps/archer/docs/cue_plan.md`):
  - The game reports EVENTS (a signal with a few named numbers), and a JSON table of CUES says how
    each is answered: which sounds, how loud, how often, what stops it.
  - The table is hot-reloaded while the game runs.
  - Looping sounds can follow a parameter (`looping` with `follow`), which is what background sounds
    need.
  - Sounds play on buses, and a bus can be faded and held (`SoundSystem::SetBusPaused`).
  - There are 32 voices (`NUM_SOUND_VOICES`), the scarce resource.
- **Chasm has five separate ways of telling the player that something happened**, each made
  differently:
  - `EconomyState::notices` (`Economy.h`): made by the rules, the last 8 kept with a running count
    (`notices_made`). The view tells the ones it has not told (`notices_seen`, `ApplicationChasmTime.cpp`).
    Not saved and not hashed. Four kinds: nothing to take, ran out, no tent, seam seen.
  - `EconomyState::dead`: saved; the view tells the new entries (`deaths_seen`).
  - `arch_notices`: an atomic count in the app.
  - Order refusals: an atomic count and a string.
  - NEEDS ATTENTION (`ApplicationChasmIssues.cpp`): worked out on the render thread each frame, never
    made by the rules.
- **Two more are planned**: the statistics journal (`statistics_plan.md`, for another agent) and the
  demo's discoveries (`demo_plan.md`, "Discoveries"). Neither is built.
- **Several moments leave no trace at all**: a harvest, a building finished and a tree felled change
  the state but are never told (`statistics_plan.md`, "What there is today").
- **The music is drafted, not played** (`apps/music/readme.md`, "chasm.json"):
  - four season sections, each with its own nature sounds: spring's birds, a woodpecker and frogs;
    summer's crickets; autumn's wind and gusts; winter's snow wind;
  - `clouds` and `chasm`, meant to play on players of their own, faded in by what the camera looks at;
  - a `reveal` stinger for the first sight of the chasm.
- **Speeds are 1x, 2x and 4x** (`physics_time_factor`), and pause stops the ticks
  (`Scene::PausePhysics`).
- **The villagers have three clips**: Walking, Idle and Shoot. Nothing exists to time an axe blow to.

## Three kinds of sound

| | Examples | Made by | Shares with |
|---|---|---|---|
| **Game events** | born, died, tree felled, storey finished, harvest, discovery | the rules, in the tick | the notice line, pop-ups, the journal, discoveries |
| **Activity and the world** | chopping, building, farming, footsteps, talking, river, falls | a count each frame of what is near the camera | the music's camera share (below) |
| **View and UI** | needs attention, placed, refused, clicks, pop-ups | the view | the HUD |

## Game events: one record, many readers

### The record

`EconomyNotice` grows into the one event record, and the four notice kinds become four of its kinds:

| Field | |
|---|---|
| `tick` | the calendar's |
| `kind` | `EVENT_*`, a number per row in the event list below |
| `person` | who, 0 for nobody |
| `building` | which, 0 for none |
| `plot` | where, -1 for nowhere: for the sound's position and the view's glide |
| `pos` | x and world z, where on it |
| `what` | a goods mask, as the notice has now |
| `amount` | a harvest's yield, a cause of death, a discovery's number |

- **Made by one call in the rules**, `Economy::Event(...)`, at the moment it happens. Rules that run in
  the app during the tick (`TickEconomy` raising what the builders finished, an arch coming down) call
  it too.
- **Kept like the notices are now**: the last `ECONOMY_EVENTS_KEPT` (64) in `EconomyState::events`,
  with `events_made` counting them. Published with the economy, so the render thread reads them.
  Never saved and never hashed: telling changes nothing in the colony.
- **Each reader keeps its own count** of what it has seen, as `notices_seen` does now. A reader that
  finds more new events than are kept has missed some. For the notice line and sound that costs
  nothing.
- **Facts stay facts.** The record is the *telling*, not the fact. What must survive a load stays
  saved state: the dead, the discoveries, the journal. So a load replays no sounds and tells nothing
  again. A reader that must never miss one, such as the death pop-up, checks the fact it is about
  (`dead`), as the death line does today.
- **Kind numbers are kept from the start.** The journal will save them, so they must not be renumbered.

### The readers

- **The notice line and the pop-ups** pick the kinds they tell, and look up names and causes in the
  state, as the notice line does now. The demo's single pop-up (death, goal met, colony lost) is one
  more reader.
- **The statistics journal** keeps the saved subset: births, deaths, harvests with their yield,
  buildings finished, settlements founded and discoveries. If the journal is written inside
  `Economy::Event`, the journal and the telling cannot disagree. That is the statistics agent's to
  decide (see "Working with the statistics plan").
- **The discoveries** (`demo_plan.md`): the rule that sets a discovery's saved flag also makes a
  `discovery` event, with the discovery's number as `amount`.
- **Sound**: every new event is signalled to the cue table by its name, with `x`, `z` and `amount` in
  the payload. Whether it makes a sound, and which one, is a row in the table, as in archer: the code
  reports everything and the JSON decides. Adding a sound to an event is an edit to the table, with
  no code.

### Working with the statistics plan

`statistics_plan.md` suggests "a journal of events as they happen: kind, tick, who or what, and an
amount". That should be this record, not a second one. Agree the struct with that plan's agent
before either side builds. The record is step 1 here and step 2 there.

**Built with the statistics (2026-10-16, `statistics_plan.md` "As built"):** the record as above (`EconomyEvent`,
`EVENT_*`, `Economy::Event`, the ring of 64), the four notices its first kinds. The journal is written inside
`Economy::Event`, so it and the telling cannot disagree. One addition: `events_by_kind`, a count per kind, since
felled trees fill 64 in half a minute at 4x and a reader that looks seldom must still know what it missed. The
cue table's signal name is `EconomyEventName`. Of step 1's events, all but `couple_housed` are made; `rock_broken`
is made too.

## Activity: the colony at work

### The count

Each frame, the view counts what is going on near the camera:

- **Who:** everyone in the published economy, by job and state (`EconomyWorker::job`, `state`, `pos`).
  A few hundred people is a short loop. Every few frames would do just as well.
- **Weight:** each person counts by how close they are to the view's centre: 1 near the middle,
  falling to 0 a little past the edge of the screen. So what is off screen fades out, and zooming out
  takes in more people at a lower level each.
- **Sorted into groups**, a number for each:

| Group | Who counts |
|---|---|
| `chopping` | a woodcutter felling (`WORKER_WORKING`), anyone clearing a tree for a road (`WORKER_CLEARING`) |
| `building` | `WORKER_BUILDING`, `WORKER_LAYING` |
| `farming` | `WORKER_SOWING`, and a picker at work |
| `water` | a water carrier drawing |
| `stone` | a stonecutter or a miner at work |
| `workshop` | `WORKER_MAKING`, a ropemaker at work |
| `range` | `WORKER_TRAINING` |
| `walking` | anyone on the move, split by what is underfoot: road, grass, snow, bridge |
| `talking` | grown-ups standing still out of doors with someone else close by: at the camp, a store, eating |
| `children` | `WORKER_PLAYING` |
| `winch` | someone riding a winch |
| `zombies` | zombies near, for a growl |

- **The count also gives a pan** for each group: the weighted average offset to the left or right
  of the view's centre.

### The beds

- **Each group is a looping sound in the cue table** whose gain and pan follow its numbers (`follow`
  with a `value` each). Silent at 0, audible at 1, full at about 4. The curve is in the table, so it is
  tuned without code.
- **The count sorts, the table only mixes.** Snow footsteps are a separate loop from grass footsteps,
  because the count splits `walking` by ground. No `when` conditions are needed on a bed.
- **Talking and footsteps are density sounds**, never one sound per person: a murmur that grows as
  people gather, and steps that thicken as more walk. A hundred people each with their own footsteps
  would use up the 32 voices on their own.
- **Close up, single blows come later.** At close zoom, the nearest one or two woodcutters could get
  separate axe blows, each on its own rhythm from the person's id, with a little jitter. There is no
  chopping clip to time them to, so the rhythm is the sound's own. This is a later step.

### The world

- **Water is positional**: the nearest stretch of river and the nearest waterfall, from the world's
  data, give `river` and `falls` numbers that two loops follow by distance. *Built 2026-10-16 with the areas,
  unexplored water faint: `areas_plan.md`.*
- **Hearths** crackle in winter: houses burning firewood near the camera, counted like people.
- **Nature is the music's.** Birds, crickets, frogs and the seasons' winds are already in the score's
  season sections, and the rift wind is in its `chasm` section. The cue table does not play them a
  second time. **The exception is birds you can see** (`birds_plan.md`, the user's 2026-10-17): their
  calls and wing bursts are placed and timed by what the flock does, so they are cue-table events,
  alongside the score's songbird beds. *Built 2026-10-18: `bird_call` and `birds_flush` on the
  `activity` bus, signalled by `SignalBirds` (`birds_plan.md`, step 3).*
- **The music's camera share is the same kind of count.** The `clouds` and `chasm` players are faded
  in by how much of the view is unexplored ground and chasm. That share is worked out in the same pass
  over the view, when the music goes in.

## View and UI sounds

- **Placed, erased, refused, selected, a chip or tool pressed**: played by the view the moment it
  happens, not on the next tick. A command waits for a tick (and for unpause), and a click must answer
  at once.
- **NEEDS ATTENTION**: the list is worked out each frame, so its sounds come from changes the view
  spots:
  - `issue_new`: a row whose kind and building were not in the list a moment ago;
  - `issue_red`: a row turning red (a deadline under two days).
  - A row that comes and goes must not chime each time. A row counts as new only after it has been
    gone for a few seconds, and the cue's `gap` keeps it rare.
  - For the first seconds after a load or a new map, rows are not new. The opening checklist is not
    an alarm.
- **The demo's screens** (`demo_plan.md`): the pop-up opening, menu moves, autosave, quick save and
  load, recording on and off, and a bug report written. All quiet and short.

## Where it runs

- **On the render thread**, beside the code that tells notices and deaths today. Each frame:
  1. signal the events made since the last frame;
  2. count the activity and set the parameters;
  3. set the listener from the camera;
  4. tick the cue system on a real-time clock.
- **Why a real-time clock.** The cue system counts how long a sound plays in ticks at a fixed rate. On
  the simulation's ticks at 4x, it would think a 2-second sound ended after half a second, so its
  groups, gaps and instance limits would all be wrong. Off the simulation, the sounds and their timings
  are the same at any speed. UI sounds still play while the game is paused, which the death pop-up
  needs.
- **What that gives up: sound that replays exactly.** Archer's cues run inside the tick, so a replay
  makes the same cue log and archer can keep a cue baseline. Chasm has no cue baselines, and sound
  never reaches the rules, so no state and no replay changes. A replay plays the same events at the
  same ticks, just not frame-exactly the same sounds. This is a departure from archer: open question 1.
- **Buses**, under master:
  - `world`: events at a place, water, hearths;
  - `activity`: the beds and the blows;
  - `ui`: clicks, alerts, the pop-up;
  - `music`: the score's players.

  Core's settings already name master, music and effects volumes (`demo_plan.md`, "Settings and
  Controls"). Effects is the parent of `world`, `activity` and `ui`.
- **Pause** fades `activity` out over a second or so (the people stand still). Water and hearths play
  on, and `ui` is never held. Archer's fade and hold is the mechanism (`SetBusPaused` with a fade).
- **Speed** changes nothing in the sound. At 4x more happens, so more events fire and the counts are
  higher. That is right.
- **The table** is `apps/chasm/assets/cues/chasm.json`, reloaded when its file changes, as archer's is.
  The sounds go in `apps/chasm/assets/sound/`, as `.ogg` like archer's (`ogg_vorbis_settings`: q-1,
  32 kHz cap).

## Core changes, flagged

Each is small and leaves archer as it is.

1. **A listener on the ground plane.** `CueSystem::SetListener(float x)` is archer's side view, and
   `distance` and `dx` read the payload's `x` only. Chasm needs:
   - `z` in the payload;
   - a listener with x, z, a height and a facing;
   - `distance` measured in 3D from a point above the view's centre, so zooming out makes everything
     quieter;
   - `dx` measured along the camera's right.

   With no `z` and no height, everything is computed as today.
2. **The cue system off the simulation thread.** No code changes: it already takes any clock and any
   one thread. Its header says "Everything is called on the simulation thread", which is the advice
   for archer's determinism. It should say one thread, and why archer uses the simulation's.
3. **The cue panel.** Archer's Cues tab (sliders that patch the file's text, play buttons) is app
   code in `ApplicationArcher`. Chasm wants the same tab. Moving it into core is the right home, for
   when Chasm's table is big enough to tune (step 5).

## The events

"Now" means the moment exists in the rules today, though most are not told yet. The name is the
signal the cue table sees.

### People

| Signal | Made by | | Sound | Also read by |
|---|---|---|---|---|
| `born` | lifelines | now | a soft bell at the house | notice, journal |
| `couple_housed` | `HouseFamilies`, a couple moving into a free house | now | a short warm motif | notice |
| `grew_up` | lifelines: a child become a young adult | built 2026-10-16 | the chime | notice, journal |
| `expecting` | `ChancesOfChildren`, a couple's chance come up | built 2026-10-16 | the chime | notice, journal |
| `died` (cause in `amount`) | needs, a zombie's bite | now | a low toll | the death line, then the demo's pop-up; journal; goal 1 |
| `colony_lost` | `TickGoals`, the tick the last person dies | built 2026-10-16 | the chime; a music stinger later | the end's page, journal |
| `old_age` | lifelines | planned (`lifeline_plan.md`) | none | journal |

### Work finished

The work before each of these is a bed.

| Signal | Made by | | Sound | Also read by |
|---|---|---|---|---|
| `tree_felled` | a woodcutter or a road's clearing (the prop turns to a log or a stump) | now | a creak and a crash at the tree | journal (trees felled) |
| `storey_raised` | `TickEconomy`, applying `economy.Raises()` | now | the last hammer blows | |
| `building_finished` (kind in `amount`) | the same, when the last storey stands; a field, bridge or tower too | now | a fuller version | journal; discoveries ("the first of each kind") |
| `line_built` | a road or wall plot laid | now | a soft thud | |
| `harvest` (yield in `amount`) | the fields' loop in `Economy::Tick` | now | a sheaf or a cart | journal, with the yield |
| `frost_took` | a field's crop lost to frost (`EconomyField::frost`) | built 2026-10-16, one a field | the chime, once a dawn; later a frosty crackle | notice (one line for all), journal |
| `crop_auto` | the countdown chose a crop (`Economy::CropDefaults`) | now | a quiet tick | |
| `rock_broken` | a stonecutter's rock gone | now | a crack | |
| `stock_reached` | an order for provisions or rope done | now | a soft chime | |
| `deposit_empty` | a mine's deposit dug out | being built (`stone_plan.md`, step 5) | | notice |

### Settling, orders and places

| Signal | Made by | | Sound | Also read by |
|---|---|---|---|---|
| `settled` | a settlement founded | now | a horn or a motif | journal |
| `tent_pitched` | a tent pitched | now | canvas | |
| `nothing_to_take` | today's `NOTICE_NOTHING_TO_TAKE` | now | an alert (`ui`) | notice line |
| `ran_out` | today's `NOTICE_RAN_OUT` | now | an alert (`ui`) | notice line |
| `no_tent` | today's `NOTICE_NO_TENT` | now | an alert (`ui`) | notice line |
| `seam_seen` | today's `NOTICE_SEAM_SEEN` | now | a discovery stinger | notice line; discovery |
| `gate_shut`, `gate_opened` | `ZONE_OP_GATE_SHUT` applied | now | a creak and a thud | |
| `arch_down` | a command that took an arch's floors | now (`arch_notices`) | a crumble | the amber line |

Not events: a winch ride is the `winch` bed. The ice thawing under someone posted across it is
looked at each second for the card (`Economy::IceHome`). It becomes an event only if the rules ever
mark someone cut off by the thaw.

### The calendar

| Signal | Made by | | Sound |
|---|---|---|---|
| `season` | the calendar, at a season's first dawn | now | the music's section changes |
| `snow`, `thaw` | the snow arriving and going | now | the wind lifts / dripping and cracking |
| `winter_survived` | the first dawn of the second spring, nobody dead | built as `goal_met` of the first goal (demo_plan.md step 7) | the chime; a stinger later |
| `chasm_seen` | exploration, the first sight of the chasm | built as `discovered` of the chasm's area (`areas_plan.md`, the cue table's `chasm_reveal`) | the war horn |

Dawn makes no sound: every 45 seconds would grate.

### Danger

Zombies are not in the demo.

| Signal | Made by | | Sound |
|---|---|---|---|
| `zombie_came` | a drop now; waves on the calendar later | debug now, waves planned | a growl; an alarm bell for a wave |
| `bitten`, `blow`, `zombie_down` | the fight | now | short sounds, close zoom only |
| `arrow_loosed`, `arrow_hit` | arrows in the rules (`EconomyArrow`) | now | a twang, a thunk |
| `wall_broken`, `alarm` | | planned (`zombies_plan.md`) | |

### The demo

All planned (`demo_plan.md`, steps 6 and 7).

| Signal | Made by | Sound | Also read by |
|---|---|---|---|
| `boulder_found`, `balcony_reached` (built 2026-10-16): the two firsts the discoveries needed an event for. The rest are read from events there already (`demo_plan.md`, "As built: discoveries") | the rules, journaled | the chime and a line ("Jelle De Wit has reached a balcony"); later a stinger | goals; which build tools show |
| `goal_met`, `goal_failed`, `demo_complete` (built 2026-10-16, `amount` the goal, or the goals met) | `TickGoals` | the chime; a stinger later | notice line, the end's page, journal |

### The view

| Signal | | Sound |
|---|---|---|
| `issue_new` | now (the panel) | a soft alert |
| `issue_red` | now | a sharper alert |
| `placed`, `erased`, `refused` | now | a place, a demolish, a dull "no" |
| `selected`, `chip`, `tool` | now | clicks |
| `popup`, menu moves | planned (demo step 1) | |
| `saved`, `rec_on`, `rec_off`, `report_written` | planned (demo steps 2 and 4) | a quiet tick |

The issues plan's "Later" rows (stores full, too little firewood before winter, a collector frozen)
use the same two alerts.

### Further out

From `gameplay_plan.md`: steam, a zeppelin built, launched and landed, an island reached, a second
village, the pyramid.

## Checking it

Sound is checked from what it decided, not by ear. The user listens.

- **`chasm_sound`**, an MCP tool:
  - the counts and parameters as they are now;
  - the cue log's last lines (what played, what was skipped and why);
  - the buses' levels;
  - a volume, where 0 mutes without changing the log, like `archer_sound`.
- **Every event reaches the table:** the cue log shows a `signal` line for each event made, with or
  without a row for it.
- **Events are rules,** so they are checked as rules are: the same events on the same ticks in two
  replays of one recording, and none told again after a load.
- **No state changed:** the trace's hash is the same with sound on and off.
- **Offline render** (`SoundSystem::InitialiseOffline`), as archer's sound tests do, where a level or
  a mix has to be measured.

## Steps

1. **The event record.** `EconomyNotice` becomes the event record, with the four notices as its first
   kinds and the ring at 64, told by the view as now. Agreed first with the statistics agent. Then
   the moments that leave no trace get their events: born, died, couple housed, tree felled, storey
   raised, building finished, harvest, settled. No sound yet. Checked by `chasm_economy` listing the
   last events, and by two replays making the same ones. *Built 2026-10-16 with the statistics, but
   couple housed (see "Working with the statistics plan").*
2. **Sound in the app.** `USE_SOUND`, the `chasm.json` cue table with its hot reload, the four buses,
   the render-thread tick on a real-time clock, the 2D listener in core, `chasm_sound`. Pause fades
   `activity`. *Built 2026-10-16, on the physics thread's every pass rather than the render thread
   (see "As built").*
3. **The first event sounds:** born, died, tree felled, building finished, seam seen as a discovery,
   and NEEDS ATTENTION's two alerts. The user picks or records the sounds.
4. **The beds:** the count, and `chopping`, `building`, `farming`, `walking`, `talking` first; then
   the water, `children`, `stone`, `workshop`, `range` and `winch`.
5. **The cue panel** in core, so Chasm's table is tuned live, as archer's is.
6. **With the demo:** the UI sounds, the pop-up, the discovery and goal stingers.
7. **With the music** (its own step, `apps/music/readme.md`): the score's players, the season as a
   section change, the `clouds` and `chasm` share from the same pass over the view, and `chasm_seen`'s
   horn. *Built 2026-10-16: `areas_plan.md`, "The music" - the chasm's first sight is the chasm area
   found.*
8. **Later:** single blows at close zoom, the rest of the events as their features land.

## Open questions

1. **Sound on the render thread, on a real-time clock?** It makes 4x and pause simple, and costs
   replay-exact sound, which Chasm does not test. The other way is archer's: inside the tick, with
   the cue system told the speed.
2. **Which events get told on the notice line**, beyond the four told now? A birth and a harvest could
   be; a felled tree should not be. Every event goes to sound either way, so this is only about the
   line.
3. **The sounds themselves.** Recorded by the user, found, or stand-ins first? The music's samples
   went the same way: stand-ins from the jungle library, then the user's own. *The user's own, from
   2026-10-16: the first ten are in `assets/sound/`, voices in progress.*

## As built (2026-10-16)

The user's first sounds arrived (a click, placing, a refusal, a notification, three axe blows, a hammer,
a chisel, a saw) with the request that the short ones be "randomised and timed after each other", as
archer's heartbeat is separate beats. So the first work sounds are BLOWS on clocks, not the looping beds
of "The beds" above. Built:

- **`USE_SOUND := 1`**, and `ApplicationChasmSound.cpp`. The makefile no longer excludes
  `sound/click.wav` from a baked build: an exclude matches the asset's name, and chasm's own click
  stands in front of the shared one under that name. *Since the sounds became .ogg chasm's click is
  `click.ogg`, the shared wav stood behind nothing and was packed for nothing, so it is excluded again
  (2026-10-17, `demo_plan.md`, "As built: the ship's pack").*
- **The table**, `assets/cues/chasm.json`, reloaded within a second of being saved (an mtime poll, as
  archer's). Buses `effects` > `world`, `activity`, `ui`; a `notice` group. Since the Settings screen
  (`demo_plan.md`, 2026-10-16) the table plays under a layer bus of its own, `sounds`
  (`CueSoundOutput`'s layer), so its buses are named `sounds/effects`, `sounds/activity` and so on. The
  effects volume is the layer's gain, which the table never sets.
- **Where it runs: the physics thread, once a PASS** (`UpdateSound`, first thing in `UpdateView`, before
  any of its early returns), not the render thread the plan named. A pass is real time - 4x is four
  ticks in one pass - so it gives everything open question 1 wanted from a real-time clock: a sound's
  timing is the same at every speed, and a click answers in a pause. And it is the thread the paint
  code and the HUD's clicks already run on, so they signal the cues directly, with no hand-over. The
  cue clock is the count of passes. This was the agent's choice: open question 1 is still the user's.
- **The listener on the ground plane, in core** (`CueSystem::SetListener(x, z, height, right_x,
  right_z)`, "Core changes" 1): over the view's centre at half the camera's distance up, `distance` in
  3D from there, `dx` along the camera's right. A payload with no `z` is measured along x as before,
  so archer, which never calls it, is unchanged. Five checks in `tools/cue_test.cpp`. The header's
  "simulation thread" wording is now "one thread", with archer's reason and chasm's ("Core changes" 2).
- **Events**: every new event in the ring is signalled by its `EconomyEventName`, with `x`, `z` (when
  it has a plot), `amount`, `person` and `building`; those older than half a day are passed over, as
  the notice line does. Only the four notices have rows so far (the notification chime, one at a time
  in the `notice` group). The rest wait for sounds: a row in the table each, no code.
- **The view**: `click` for any press on the HUD and a tool key; `placed` and `erased` when the rules
  take a player's zone command that placed or took away something (`zone_placed`, `zone_erased` -
  a chip's command is not one, its press already clicked); `refused` when the zone, order or crate
  refusal counts move. Answered in the tick, so a placement made in a pause is heard when the game
  goes on, together with the building appearing. `erased` has no row yet.
- **Work in hand** (`SignalWork`): a clock for each of the nearest four woodcutters felling (WORKING at
  a STANDING tree, or CLEARING) and the nearest four builders BUILDING, within 48 of the listener.
  An axe at the woodcutter's own tempo, fixed by his id (`SOUND_CHOP_*`); a builder in bouts with a rest
  after each - a hammer's 3 to 6 blows, a chisel's 2 to 4, or a saw's two strokes (`SOUND_HAMMER_*`,
  `SOUND_CHISEL_*`, `SOUND_SAW_SECONDS`, `SOUND_BUILD_*`). Every blow is a little early or late. Draws
  are `CueHash01`, never `rrand`. The clocks stand still while the world does, so a pause owes nothing.
  The rhythm is in code; what a blow sounds like, how loud and its pan are the table's.
- **Pause** (the key, the menu, the title, the statistics): `activity` fades out over 0.6 s and is held,
  and fades back in over 0.3 s. `ui` and `world` are never held.
- **`chasm_sound`** (MCP): the device, the table and its cues, volume (0 mutes without changing the
  log), `live`, `activity_held`, the listener, the work clocks, the voices playing, and the cue log;
  `op play` auditions a cue.

Checked in a muted instance on `village_year5`: one woodcutter's blows 48-51 passes apart, a chisel
bout's 15, a hammer bout's 22, each panned and turned down by where he stands; four house sites placed
and the refusals between them, thinned by their gaps; no blow while paused, a click still played, and
`activity` held until the game went on; a `tree_felled` row added to the running table was loaded within
a second and played for each tree felled after it, then taken out again.

The same day, after the user had heard about it ("already really good"), three more:

- **Every event has the notification for now** (the user's). The colony's own - born, died, settled, and
  the notice line's four - are heard wherever the view is, in the `notice` group, each waiting its turn
  (`busy: queue`, up to 3 s) rather than lost. What happens at a place - tree felled, rock broken, storey
  raised, building finished, harvest - is on `world`, heard near the view like a blow and panned with it,
  in a `place` group of one at a time with half a second between: at 4x the trees alone would chime
  every few seconds. A row's `sounds` is all that changes when an event gets its own. Three things the
  first run showed:
  - a place row fires only within 48 (`when` on `distance`): past that it played at gain 0, which still
    takes a voice and the group's turn from one that would be heard;
  - `building_finished` is made on the tick of the storey that finishes it, so it has priority 1 and
    `busy: interrupt` - the building is heard, not its last storey;
  - an event's place is a plot OR a position without one (Economy::Event reads (0, 0) as none): a
    harvest's field stands on cells and has `plot` -1, and played everywhere until `SignalEvents` read it.
- **`pitch_jitter` in core's table**: each playing's pitch up to that share either way, drawn from the
  trigger as the other draws are (salt 4); its length is counted at that pitch, and a row without it
  plays at exactly its pitch, so archer is unchanged. Four checks in `tools/cue_test.cpp`. The blows
  carry the worker's id as their instance, so two on one pass draw two pitches.
- **Ogg**: `tools/sound_ogg.py <app>` encodes every wav in the app's `assets/sound/` as `apps/music`'s
  publish does (oggenc2 `-q -1`, resampled only above 32 kHz), moves the wav to `art_source/<app>/sound/`
  as the source, and points the cue tables at the `.ogg`. Chasm's ten: 512 KB -> 65 KB. New recordings:
  drop the wav in `assets/sound/` and run it again.

Checked in a muted instance on `village_year5` at 4x then 1x: every blow its own pitch (chop 0.95-1.05,
hammer 0.92-1.07, chisel and saw within theirs), the event rows chiming only near the view with their
pan and distance, each finished building heard over its last storey, and the `.ogg` files decoding to
the same frames as the wavs. Births, deaths and settlements did not happen in the run.

Not yet:
- **Sounds of their own for the events**, a row for `erased`, and NEEDS ATTENTION's two alerts - step 3.
- **The beds** for walking, talking, farming, water and the rest - step 4. Chopping and building are
  blows now, which a crowd of hundreds would not be; the nearest-four rule is what keeps them in voices.
- The cue panel (step 5), menu and title sounds (step 6). Music (step 7) is built: `areas_plan.md`, "The
  music".

## Voices (2026-10-16)

The user's first speech: two voices, **leo** for any man and **leah** for any woman, each with eleven
lines in `assets/sound/speech/taunts/` - `<voice>_<line>_s<seed>`, the seed only the take the generator
made, kept as it is. Several lines for being picked out and for being sent, for variation; **okay** for
"any order that's not been handled yet, like picking berries, setting up tent. Anything" (the user's).

- **Who answers.** A grown-up - anyone drawn as one (`PersonFigure`), which is everyone who can take an
  order: a 6-year-old who has reached the young stage answers, a toddler does not. `voice` 0 is leo's,
  1 leah's (`SignalVoice`).
- **When.** PICKED (`picked`): clicked in the scene, again or not; the first of a box; Shift-added; his
  name pressed in a list or a card; MCP's select. ORDERED: when the rules TAKE the order, in its command
  - so a refused order is only the `refused` sound, never an "on it" followed by a refusal. Commands
  drain on every pass, paused or not (`Scene::BeginPass`), so an order given in a pause is answered in
  it. Each order is its own signal, as the events are: `sent` (to a plot, or onto a tower), `released`,
  `gather`, `take_tent`, `straight_on` (a chip turned off), `settle`, `pitch`, `forage` (`what`
  FORAGE_*). A line of its own for picking fruit is then a row's `sounds`, no code.
- **Who of a group** (`Say`, behind its own lock, since MCP's picks come from another thread): those asked
  on a pass, an order's answer over a pick's, in the order asked - a group's orders go out nearest first.
  One asked, one answers. MORE, and **two answer at once** (the user's, 2026-10-17): the first asked and
  the first after him in the other voice if the group has one - a man and a woman are two people, two
  men the same man twice - else the second. Each has a `side`: a pair's are -1 and 1, the one standing
  more to the left on the left; one alone has his place across the view, -1 to 1 over
  `SOUND_VOICE_SIDE_REACH` either side of its middle. Every row pans by `side`, to at most 0.3.
- **One answer at a time**: the `voice` group with **`busy: replace`**, new in core - like `interrupt` but
  cutting a line of the same priority too if it began on an earlier tick, so the answer heard is always
  to the last click. Orders have priority 1, so a pick never cuts an order's answer; an order cuts a
  pick's. The pair is the group's **`together: 2`**, also new in core: lines of one priority begun on one
  tick speak at once, past the busy rule and a row's own gap (two men are one row twice), and the next
  answer cuts both. Checks in `tools/cue_test.cpp` for each.
- **The table's rows** (`picked_leo`, `sent_leah`, ...), a pair per signal: picked -> hi, whatsup, yes,
  whereto, morning; sent -> headingover, onit, imonit; gather -> grabsupplies; take_tent and settle ->
  packingup; the rest -> okay. On a `voice` bus under `effects`, never held; not quieter further off
  (spoken to the player). Every row's gain is 0.8, the takes being levelled (below). A gap of 8 passes
  makes a double click one answer.
- **Trimmed**: `tools/sound_ogg.py` now walks subfolders, keeps them under `art_source/`, and cuts the
  silence off both ends of anything under `sound/speech/` (40 ms kept before the first sound, 80 ms
  after the last, faded). The takes started 0.16-0.62 s in, which read as lag after a click. 22 files:
  1.3 MB of wav -> 127 KB.
- **Levelled** (the user's, the same day): the takes ranged 8 dB - leo's morning and leah's whereto and
  okay the quietest, leah's 2.6 dB under leo's on the whole. The tool now brings every speech take's
  voiced part (the RMS of 20 ms windows within 20 dB of the loudest) to `SPEECH_LEVEL`, -18 dBFS,
  never past -1 dBFS at the peak - which held leah's packingup 0.6 dB short. Decoded from the shipped
  .ogg with stb_vorbis, all 22 sit within -17.4 to -18.4 dBFS. `--again speech/` made them again from
  the kept wavs, which stay as they came.

Checked in a muted instance on the startup world (no save loaded, so no autosave was spent): a man's and a
woman's picks, each cut by the next; a double click one answer; six picks of one man, no line twice
running; a toddler silent; an order, a release, a gather and its chip off, a tent and its chip off each
answered with its line; forage, pitch and settle refused there (no ripe fruit, no tent, no other
settlement) and heard only as `refused`; a group of four sent by a right click, one answer; an order in
a pause answered in it; a pick right after an order skipped. Not heard: forage, pitch and settle TAKEN -
the same path, their rows loaded.

`morning` is said at any hour, the user's call: Chasm has no time of day to see.

The pair, checked the next day in a muted instance: a group of two men and two women picked - a leo and a
leah line together at -0.3 and +0.3; three men - two different leo lines; a pick of one cut both of a pair,
and a pair cut one; the four sent by a right click - one each, either side; one sent - one line, near the
middle where he stood.
