# Modes: the tutorial, the demo and free play

Talked through with the user 2026-10-19. The demo's goals (`done/demo_plan.md` step 7) were built for one demo: two
goals hard-coded, a panel titled THE DEMO'S GOALS, and debug switches that silenced their notices but left the panel
up. The user wants it general: a simple TUTORIAL, a few STORY moments, and the goals, as parts of one thing - with
NEEDS ATTENTION (`issues_plan.md`) and the event journal (`statistics_plan.md`) beside it rather than duplicated by it.
Step 0 is built; the rest is proposed.

## Decided (the user, 2026-10-19)

- **Tutorial steps and story moments come one at a time**, never several open at once.
- **They only tell.** What changes the colony is the player's actions; no step adds people, goods or weather.
- **Three modes**, as the working shape (the user's "perhaps"): TUTORIAL, DEMO and FREE PLAY.
- **The goals panel is titled GOALS**, not THE DEMO'S GOALS, and **a goal switched off on the debug panel is not
  drawn either.** Built (step 0).

## How it stands

Four pieces, each sound, and written separately:

- **Events** (`Economy.h`, AN EVENT): made once by the rules in the tick; the last 64 kept for telling, the
  journaled kinds saved and hashed in the history. Read by the notice line, the sounds, the Statistics page, the
  death pop-up and end pages (`UpdateEnd`), and the discoveries.
- **Discoveries** (`ApplicationChasmExplore.cpp`): worked out from the journal and what is in sight, never saved of
  their own. They unlock tools and feed the goals.
- **Goals** (`ApplicationChasmGoals.cpp`): RULES. `TickGoals` runs every tick in either mode and journals
  `EVENT_GOAL_MET` / `_FAILED` / `_DEMO_COMPLETE` / `_COLONY_LOST`, so they are in the hash and the save.
- **Needs attention** (`ApplicationChasmIssues.cpp`): VIEW. Worked out every frame, never saved; its `ISSUE_NEXT`
  rows are already the tutorial in all but name, and come back whenever their condition does.

Where they tangle, to be undone on the way:

- The switches share one bit space with the goals (`CHASM_TRIGGER_DEATH` is `CHASM_GOALS + 0`): a third goal would
  renumber every end's switch.
- The goals' bookkeeping (`goal_told`, `f_complete_told`, `f_lost_told`) lives in `ChasmDiscoveries`.
- Three voices say what to do next: the goals panel (FIND THE CHASM, BUILD A WINCH), Needs attention (FIND A RIVER,
  BUILD A WATER COLLECTOR - and NEEDS STONE's FIND THE CHASM), and the discovery notice (NEW TOOLS: WATER, BRIDGE).
- An event is put into words twice, and the two have drifted: the notice line's chain in `ApplicationChasmTime.cpp`
  says a seam is "seen floatstone in a balcony's wall", `ChasmJournalText` "saw floatstone in the chasm's wall".
- An event's discovery is worked out twice: `UpdateDiscoveries` and the notice line's `discovery_of`.

## Proposed

### Three layers, not one system

1. **Events: what happened.** As they are. One function puts an event into words, for the notice line and the
   journal alike; one map says which discovery an event makes.
2. **Conditions: what is true now.** Needs attention, as it is, in every mode: problems and recurring hints.
3. **The track: what is told once, one step at a time.** The tutorial and the story. New.

The goals sit between 2 and 3 and stay as they are built: rules, journaled, deciding the end.

### A mode

Chosen on New game beside the map, saved with the colony - a save names it only when it is not the Demo, as
`GridSettings::kind` is written only when it is not the chasm, so every save today is byte for byte the same.

| Mode | Goals | Track | End |
|---|---|---|---|
| TUTORIAL | none | the tutorial's steps | its last step: Keep playing or Main menu |
| DEMO | the first winter, the chasm (today's) | a few story moments | today's: both goals decided, or the colony lost |
| FREE PLAY | none | none | the colony lost only |

- **The goals by mode**: a mode lists the `CHASM_GOAL_*` in play, and `TickGoals` journals only those. A goal's
  condition stays code (`ChasmGoalState`, `ChasmGoalNext`). `EVENT_DEMO_COMPLETE` is renamed `EVENT_GOALS_COMPLETE`,
  keeping its number 20.
- **The switches** get a bit space of their own: a bit a goal, then the ends, then the track's kinds.
- **The mode is a rule**, since it says which goals are journaled: changing it in a running colony is a debug edit
  and a recorded command, as `add_person` is.
- **Test maps and `chasm_generate`** are Free play.

### The track is the viewer's

Its steps only tell, so they stay out of the rules, the hash and the statistics. Worked out on the physics thread
from `UpdateView`, as `UpdateEnd` is, from the published state. Its place - the current step's id - is saved with
the colony and never hashed.

The reason it is the view's and not the rules': a step can wait for something the VIEWER did - the view turned, a
person selected, the Statistics page opened - which the rules never see, since keys are the viewer's and never
recorded. What that costs: a replay starts from the saved step and tells the colony's steps as the colony gets
there, but does not move past one that waits on the viewer.

### A step

One of three kinds:

- **TELL**: a pop-up over the game, paused under it (`OpenAsk`, a new `CHASM_ASK_STORY`): a heading, a few lines,
  Continue. Done when it is closed. The story's moments and the tutorial's explanations.
- **TASK**: a line on the panel at the top left - "BUILD A FIELD" - with where a click takes you (a tool, a
  building, a plot - a Needs attention row's `tool` / `building` / `plot`). Done when its condition holds.
- **NOTE**: the notice line, in amber, no pause. Done when told.

And **when it starts**: once the step before is done, and then, if it says so, on a condition or after a number of
game days. A TELL pauses the game, so a delay after it counts from when it is closed.

### Conditions: a small vocabulary in code

What a step can wait for or be done by, named in its file:

- `event`: an event of a kind made since the step began, by its name (`EconomyEventByName`), with a `kind` for
  `building_finished`;
- `placed` / `standing`: a building of a kind placed, or whole;
- `discovered`: a discovery (`ChasmDiscoveryName`);
- `day`: a date reached; `days`: so many days after the step began;
- `people`: at least so many;
- `goal`: a goal met;
- `issue`: a Needs attention row of that `kind` (`build_water`, `choose_crop`) no longer showing - so a task leans on
  a hint that already exists instead of copying its condition;
- `view`: something the viewer did - `turned`, `zoomed`, `selected_person`, `stats_opened`, `tool_taken`.

### Where the steps live

A file per mode, `assets/story/tutorial.json` and `assets/story/demo.json`, as the cue table is
(`assets/cues/chasm.json`): what is said is content, written and rewritten without a build, and reloaded while the
game runs. Baked into a ship build as any asset. Conditions come only from the vocabulary; a name the code does not
know is an error in the log, and a debug build's check. A step, for the shape:

```json
{ "id": "first_field", "kind": "task", "text": "BUILD A FIELD", "tool": "field",
  "done": { "issue": "build_field" } },
{ "id": "first_snow",  "kind": "tell", "after": { "event": "frost_took" },
  "heading": "The first frost", "text": "What was still growing in the fields is lost." }
```

### The tutorial and Needs attention

Needs attention stays as it is, in every mode. The tutorial explains once and points at its rows; while a TASK names
an `issue`, that row is not drawn a second time.

### Debug

The panel's Goals becomes MODES: the colony's mode (changed by a recorded command); every goal and end with its
switch, as today; the track's current step with Back, Skip and Restart - the viewer's, so no command; and a switch
for the track's pop-ups, for testing without them. `chasm_menu`'s `triggers` keeps working, and gains the track's
state and a skip.

## Steps

0. **The goals panel honours its switches and is titled GOALS** - BUILT 2026-10-19 (`DrawGoals`: a goal switched
   off has no row, no panel with none left). Compiled; not yet looked at in the game.
1. **One text per event, one event-to-discovery map.** Nothing told changes, apart from one wording where the two
   disagree today - the user's to choose.
2. **Modes**: the mode in the save and on New game; goals by mode; Free play; the switches' own bits; the demo's
   names made general. An A/B: every save in `saves/` loads and saves byte for byte the same.
3. **The track**: the three kinds, the vocabulary, the files, the saved step, the debug panel.
4. **The tutorial's steps** - the user's to write. A draft from Needs attention's rows to start from: a TELL of the
   landing; TASK turn the view (`view: turned`); TASK build a field; a TELL on choosing a crop; TASK find a river and
   build a water collector; TASK build a woodcutter; TASK build houses; TASK open the Statistics (`view:
   stats_opened`); a TELL that Needs attention will carry on from here, and the end.
5. **A few story moments in the Demo** - the user's: the first frost, the chasm first seen, the first death, the
   first zombie.

## Open (the user's)

- The tutorial on a map of its own (a fixed start), or on any?
- A TASK's line: at the top of the GOALS panel, or a panel of its own above it?
- Story in Free play too, or never?
- Which mode New game offers first.
