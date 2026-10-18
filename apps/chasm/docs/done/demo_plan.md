# Demo Plan: menus, goals, and recording for testers

**FINISHED 2026-10-18.** The demo went out to testers as Chasm 0.0.2 (commit `7de4696`) and ran on
all ten of their machines, two of them Linux, with nothing reported. Moved to `docs/done/` then; it is
kept as the record of what was decided and built, and nothing more is added to it. The next demo's
work is finishing the defence (`../defence_plan.md`, `../zombies_plan.md`).

**Not done, and not lost** - picked up by a later plan if it is still wanted:

- From "Other things toward a first shippable demo": the wiki shipped as Help and opened from the
  menu (7), a performance pass on the release exe (9), a tester README in the ship folder (10), and a
  first screen saying what the game is and what the goal is (11).
- From the diorama ("As built: the diorama's smoke and people", *Not yet*): children, an archer's
  arrows, a working clip of its own (new animation, the user's), and a thicker smoke column.
- Option C of the backdrop, an authored terrain, left open for scenarios and tutorials.

Proposed and decided 2026-10-15 (see "Decided" below). All seven steps are built (see the "As built"
sections at the end). The aim is to get Chasm to
testers as the ship build (`make ship`: no ImGui, no MCP, no debug panels), so the game can be tested
without the debug menu. Statistics were split out into a project of their own: `statistics_plan.md`.
(The other plans named below are in `docs/`, one folder up, as this one was when it was written.)

## What the user asked (2026-10-15)

- A small menu and a statistics page.
- Borrow from archer's menu. The menu needs a separate scene.
- Escape closes the game at once today. Every way out should go behind an "are you sure?".
- Save and load already work. Add record and replay, so testers can record a bug.
- Other things worth building toward a first shippable demo.
- The main menu over a close-up of a small terrain with a complete town on it: a custom-built one
  rather than a generated map. Work out if and how that fits the generated world, and whether it
  needs the same save state.

## Decided (the user, 2026-10-15)

1. **The backdrop is a baked diorama** (option B below), livened up with smoke particles and perhaps
   a villager at work. "Good plan."
2. **No zombies in the demo yet.** Its goals are two that can be met today: **get through the winter
   with no deaths**, and **reach the chasm**.
3. **Save names are automatic.**
4. **Always recording**, from each autosave: yes.
5. **Statistics become a separate project**, for a different agent: `statistics_plan.md`. The user's
   requirement: after loading a save, the statistics are the same as when it was saved, and where
   they sum up events and numbers, the sums agree.
6. **"Reach the chasm" means building a winch and lowering someone down.** More generally, finding a
   material (floatstone) or building something is a DISCOVERY that can be used as a condition. "We'll
   need this anyway if we want to limit our building options to show only things we discovered."
7. **A death pauses the game behind a pop-up,** the same pop-up as the "are you sure" on Escape: "A
   villager died", with Continue or Main menu. That is also what happens when the first goal fails.
8. **New game offers four maps** (the user, 2026-10-16), each a seed and a size that the user and
   Claude choose by hand, rather than a random seed.

## What there is today

- **The ship build** starts straight into a new map of the default seed and flies to the camp, and
  that is all. There is no new game, no save and no load: saving and loading exist only in the
  debug panel's Save header and the `chasm_save` / `chasm_load` tools.
- **Escape closes the window.** That is core's default (`Window::f_escape_closes_window`), which
  archer turns off. The window's X and Alt+F4 send WM_CLOSE, which `Window.cpp` hands to
  DefWindowProc, so they close it with no question asked. Core has no hook for asking first.
- **A save is the seed plus edits** (`ChasmSave.h`): the grid settings (seed and size), buildings,
  grounds, the date, the economy and the exploration. `world_hash` catches a save that the
  generator no longer turns into the same world. Files go in `saves/` under the working directory.
- **Recording is core and works in every build.** F9 starts and stops it, F10 replays the last one.
  - Chasm records commands only: every key belongs to the viewer (`SetupInput`).
  - A recording's start state is a save (`CaptureRecordingState`), so a replay starts where the
    recording began.
  - Files go in `recordings/` under the working directory.
  - A tester can use it today, but blind: nothing on screen says it is recording, and there is
    nowhere to send the file.
- **Core's menu pieces, made for archer** (`docs/menu_plan.md`):
  - `UIMenu` and `UIMenuDraw`: buttons, choices, sliders and toggles, driven by keys, mouse or pad.
  - `UIControls`: the Controls screen, built from the keymap.
  - `Settings`: a typed store in `settings.json` beside the exe (full screen, MSAA, volumes).
  - `Window::SetFullscreen`.
  - There is no text field.
  - Archer's title is a Scene of its own (`RequestActiveScene`, `OnActiveSceneChanged`), with the
    level kept loaded under it.
- **Scenes.** Only the active scene is simulated and drawn, and `main_scene` *is* the active scene.
  Chasm uses `main_scene` 134 times, and every one means the world: objects added, `AtTickBoundary`,
  the input controller. Archer keeps a separate `world_scene` pointer for this.
- **Generating a map is quick.** The default 50k-cell map takes about half a second in a debug
  build: grid 170 ms, levels 108, mesh 134, forest 27, water 27, sight 15. A load generates it again.
- **Statistics today** cover only the present: the people (ages, jobs, health), the dead (who, day,
  cause), stocks, buildings, fields and trees felled. There is no history. Notices keep the last 8,
  unsaved; deaths are the only events kept.
- **A tester's problems go unseen.** Crash reports are debug-only (`CrashReport.h`), and all
  logging goes to stderr, which a double-clicked exe throws away.

## The screens

```
start --> TITLE ---- Continue / New game / Load ----> GAME --- Escape ---> PAUSE MENU
            Replay a recording                         ^                     Resume
            Settings, Controls                         +---------------------Save, Load
            Quit (asks)                                                      Statistics
                                                                             Record / Report a bug
                                                                             Settings, Controls
                                                                             Main menu
                                                                             Quit (asks)
```

Main menu loses nothing, so it doesn't ask: the game stays loaded under the title, and Continue goes
back to it.

### The title

- **A scene of its own** (the user's call), with its own camera, light, music section and menu. The
  game's view is untouched and the game stays loaded under it, so Main menu and Continue are instant,
  as in archer.
- **Buttons:** Continue (the newest save or autosave; greyed out with none), New game, Load, Replay a
  recording, Settings, Controls and Quit. The version and build date go in a corner.
- **The backdrop:** see below.

### The pause menu

- **Over the frozen game, not a scene of its own.** The world stays drawn and dimmed, and the
  calendar is paused, as the speed button's pause does. Stopping the world needs no scene switch,
  and a scene switch would stop drawing it.
- **Buttons:** Resume, Save, Load, Statistics, Start/Stop recording, Report a bug, Settings,
  Controls, Main menu and Quit.

### Escape

Escape steps back one thing at a time, as in most builders:
1. the tool in hand, a box drag, or the right-click menu;
2. then the selection;
3. and only with none of those, the pause menu.

In the pause menu it goes back to the game, and on a sub-screen it goes back to the menu.

### Every way out asks first

- **The question.** Quit, and Load or New game during a game, all ask "Quit Chasm? 6 minutes since
  the last save", with Save and quit / Quit / Cancel.
- **One pop-up for everything that stops play** (the user's): the game pauses, and a box with a line
  of text and two or three buttons sits over it. It is UIMenu's buttons in a panel. The same pop-up
  asks before quitting, tells of a death ("A villager died": Continue / Main menu, see "The demo's
  goals"), and tells of a goal met.
- **The window's X and Alt+F4 ask too.** That needs core: an opt-in `Window::f_confirm_close`.
  With it set, WM_CLOSE raises a "close asked" flag instead of closing, and the app shows its
  question. Every other app is unchanged. It is a small core addition, flagged here as CLAUDE.md
  asks.

### Save and load

- **Names are automatic** (decided): a save is named for what it is, "Colony 1 - year 1, spring 3".
  No text field is needed.
- **Saving:** a new save, or over one in the list.
- **The list** reads each file for its name, game date, people and the real date it was written.
  That all comes from the JSON, so no new format is needed.
- **Autosave** at the first dawn of each season and on leaving the game, rotating through three
  files. Continue takes the newest of every save.
- **Quick save** on F5 and quick load on F8 (F9 and F10 are recording's).
- **A save from another version** that no longer generates the same world (`world_hash`) says so
  plainly, instead of loading onto the wrong plots. Saves are not migrated (README.md).

### Settings and Controls

- **Settings:** core's (full screen, MSAA, master/music/effects volume), plus Chasm's own:
  - autosave on or off;
  - the HUD's scale;
  - pan and turn speed.
- **Controls:** `UIControls` over Chasm's keymap, with rows the app names: pan, turn, the tools,
  speed, select.

## The title's backdrop: a town in close-up

**Where the world comes from:**
1. The seed and the size go into `GenerateChasmLayout`, which places the rims, balconies, rivers,
   springs and cracks.
2. From the layout come the terrain, deposits, mesh, forest, mist and water. These are all free
   functions over immutable data (`RegenerateGrid`).

A save stores the seed, the size and the edits.

| | How | Same save state? | Cost | The catch |
|---|---|---|---|---|
| **A. Live showcase** | An ordinary save of a town built by hand (`saves/menu_town.json`, baked into the ship exe), loaded into *the* world. The camera does a slow orbit and the people walk. | Yes: it is a save. | Almost nothing new. | Not a separate scene. The title and a game can't both be loaded, so Main menu means saving and replacing the game, and the title costs a map's generation. |
| **B. Baked diorama** | The same save, loaded once in debug. The ground, buildings and props within a radius of the town are written out as one mesh asset, which the title scene shows. | The *source* is an ordinary save. The diorama is geometry and needs no save state. | An exporter and a loader, plus the `world_scene` rename. | Static: no one walks unless a few people are placed. It must be re-exported when the look changes. |
| **C. Authored terrain** | The save carries its own layout (rift, balconies, rivers) instead of one generated from the seed. Everything after the layout is unchanged. | The format gains an optional `layout` block, and the world hash covers it. | Saving a `ChasmLayout`, and authoring one: by hand at first, an editor later. | The most work. It is also what scenarios, tutorials and a hand-made demo map would need. |

**A small map** is already a setting: `GridSettings` carries the size (`target_fine_cells`), so a map
of 3-5k cells needs no new code. But the layout generator is tuned for full maps and drops what
doesn't fit, so a spike would have to show what a small seed gives. Neither A nor B needs one: a
close-up never shows the map's edge.

**Decided: B**, a separate scene with a baked diorama exported from an ordinary save, livened up
(the user's):
- **Smoke:** the hearths' chimney smoke - instanced particles on core's INSTANCE_MOTION_LIFE, with
  `smoke_material` (`ApplicationChasmHearth.cpp`) - placed at the diorama's chimneys, which the
  exporter writes down as points.
- **A villager at work:** a villager model in the title scene, playing a clip in place, on a spot the
  exporter marks. The rig has Walking, Idle and Shoot today (`ApplicationChasmWorkersView.cpp`), so a
  working clip - chopping, sawing, carrying - is new animation work for the user; Idle or a walk
  along a fixed path will do until then.

Why B:
- the title is up at once, before any map is generated, and the game's world generates while the
  title shows (as archer's loading progress does), so Continue and New game are quick;
- the game stays loaded under the title, so Main menu loses nothing;
- the diorama never runs the rules, so it can't touch saves or replays, and only its source is a
  save, in the one format there is;
- C stays open for later: an authored layout is just a different source for the same export.

**Groundwork either way:**
- `world_scene` for every world use of `main_scene`. This is mechanical and changes no behaviour,
  but without it a second scene would collect the world's objects.
- A check of what waits on the world's tick boundary. An MCP tool that calls `AtTickBoundary` on a
  world that isn't ticking (the title showing) would wait forever.

## Statistics: a project of its own

Split out (the user, 2026-10-15) into `statistics_plan.md`, for a different agent. This plan needs
two things from it: the Statistics page in the pause menu, and the summary on the end screens. Until
it lands, the end screens show what is true now: days survived, people, the dead.

## The demo's goals

No zombies yet (the user). Two goals, both doable today:

1. **Through the winter with no deaths.**
   - Met at the first dawn of the second spring with nobody dead.
   - Failed at the first death before then.
   - Both can be worked out from a save as it stands: the dead are saved, with the tick each died
     on. So a load shows the same, with no new state.
2. **Reach the chasm: build a winch and lower someone down** (the user's). Met by two discoveries:
   a winch standing on the rim, and the first person lowered by it to a balcony.

### Discoveries

The user's: "finding the material floatstone, or building something should be an event that can be
used as a condition. We'll need this anyway if we want to limit our building options to show only
things we discovered."

- **What a discovery is:** something the colony has done or found for the first time, kept for
  good. Goals are conditions over discoveries. Later, the build tools shown are too: a tool appears
  once what it needs has been discovered.
- **Two kinds, by whether the save already shows it:**
  - **Read from the save.** No new state. A floatstone seam *seen* is already worked out again from
    the saved exploration after every load (`stone_plan.md`, "Found by exploring": `LookForSeams`,
    `NOTICE_SEAM_SEEN`).
  - **A moment that passes,** such as the first person lowered by a winch, or the first winch built
    (it may be erased later). These were to be saved flags. Since the statistics' journal keeps every
    such event, saved and hashed, they are read from it instead (the user, 2026-10-16: "As built:
    discoveries").
- **Areas found** are the first of the read-from-the-save kind to be built (2026-10-16, `areas_plan.md`):
  each river, waterfall and biome, `EVENT_DISCOVERED` in the journal, worked out again from the exploration
  after a load.
- **For the demo:** a winch built, someone lowered to a balcony, floatstone seen. Next, the first of
  each building kind, which is what limiting the build tools needs. The list grows as goals and
  unlocks need it.
- **The statistics journal** (`statistics_plan.md`) can list discoveries as events: the tick each
  was made is kept for that. The demo doesn't wait for statistics to have them.
- **Shown:** a discovery made pops up like a notice ("Jelle De Wit has reached a balcony").
  `chasm_economy` lists them for agents.

### On screen

- **A small goals panel:** each goal with in progress, met, or failed.
- **A death** (the user's): the game pauses, and the one pop-up says who died and of what ("A
  villager died: Kaat De Wit, of thirst"), with Continue or Main menu. Several deaths on one tick
  share one pop-up. A death in the first winter also marks the first goal failed, and says so on the
  same pop-up. Continue plays on toward the second goal.
- **Both goals met:** "Demo complete", with Keep playing or Main menu.
- **Everyone dead:** "The colony is lost", with Load, New game or Main menu. With nothing left to
  lose, Load and New game don't ask.

### The rules

- **Discoveries and the end are rules**, so they run in the tick and a replay ends the same way.
- **The pause for a pop-up is the viewer's.** It stops the ticks and changes no state, like the speed
  button's pause.
- **`chasm_economy`** reports the goals and the discoveries for agents.

## Recording and replay for testers

- **On screen:**
  - "REC" and the time in a corner while recording.
  - "Replay a recording" on the title lists the recordings with date, length, colony and build.
  - While replaying, a bar with speed, pause and stop. The view stays free: Chasm's keys already
    belong to the viewer.
- **Always recording** (decided): every autosave starts a fresh recording, so a tester who hits a
  bug can report it afterwards: the autosave plus the recording since then is the whole run. Only
  commands are recorded, so files stay small: 12 KB for a 3,073-tick test on 2026-10-14.
- **Report a bug** (pause menu, and F12) writes a folder `reports/<date>_<time>/`, ready to zip and
  send, holding:
  - the autosave and the recording up to now;
  - a screenshot;
  - the log;
  - `settings.json`;
  - the build's version.

  There is no text field, so the tester describes what happened in their message.
- **A recording names its build.** A replay on another build may diverge from the original, and
  says so.
- **Core additions, flagged:**
  - a log file beside the exe, holding all of stderr, in every build;
  - a crash dump in release builds (`MiniDumpWriteDump`), where today there is nothing.

## Other things toward a first shippable demo

In rough order of importance:

1. **Goals and an end** (decided, above): through the winter with no deaths, and reach the chasm.
2. **Autosave and Continue** (above). Testers' sessions end abruptly.
3. **The version everywhere:** the title, saves, recordings and reports, plus a plain message for a
   save from another version.
4. **The log file and the release crash dump** (above).
5. **Write files beside the exe.** `saves/`, `recordings/` and `reports/` go wherever the working
   directory happens to be, so a tester's shortcut started elsewhere would scatter them. Write them
   next to the exe instead, as Settings does (`GetExecutableDirectory`).
6. **New game options:** the seed (random, or one of the last few). No zombies in the demo
   (decided); the debug drop stays debug-only.
7. **Help.** The wiki (`build/wiki.html`) is already written page by page, mechanic by mechanic.
   Ship it beside the exe and open it from the menu.
8. **Sound.** The score drafted for Chasm (`apps/music`, `chasm.json`: seasons, clouds, the chasm),
   with a title section of its own, and the first effects.
9. **A performance pass** on the release exe: a colony of 100 at 4x, its frame time, and the tick
   rate it reaches ("RUNNING AT" shows it).
10. **A tester README** in the ship folder: the keys, the goal, and how to report a bug.
11. **A first screen** saying what the game is and what the goal is. NEEDS ATTENTION already teaches
    the next steps after that.

## Steps, in order

1. **Every way out asks first:** the pause menu's skeleton (Resume, Quit), Escape one step at a time,
   the one pop-up, and core's opt-in close flag.
2. **Save and load:** the screens, autosave, Continue, quick save, the version and the
   other-version message.
3. **Settings and Controls:** core's, plus Chasm's own settings.
4. **Recording for testers:** REC, always recording, Report a bug, the replay screen, and core's log
   file (and crash dump).
5. **The title scene:** the `world_scene` groundwork; the title with its menu over a plain picture;
   then the diorama exporter and its loader, from `saves/menu_town.json`; then its smoke and its
   villager.
6. **Discoveries:** the saved flags and the read-from-the-save ones, set by the rules, with the
   discovery pop-up and `chasm_economy`'s list.
7. **The demo's goals and end:** the goals panel, the death pop-up, and the two end screens - their
   summary from what is true now, until `statistics_plan.md` provides its page.

Steps 1-5 change nothing the rules do. Steps 6 and 7 add rules (discoveries, the end) and saved
state, checked by save-and-load and by replay as usual. Statistics run alongside, as their own
project.

## As built: the title, the pop-up and the diorama (2026-10-15)

Step 5's first two slices, started at the user's request; the smoke and the villager are next.

- **The title is a scene of its own** (`ApplicationChasmTitle.cpp`), with its own camera, sun and
  menu: Continue and Quit for now. The game stays loaded under it.
  - Builds without ImGui (the ship build) start on it. A debug build starts in the world, as before,
    with the title an Escape away.
- **No `world_scene` rename.** The 134 uses of `main_scene` stay: Chasm's entry points skip the world
  while the title shows (`TitleShowing`): `UpdateView`, `RunSimulationTick`, `PreRender`,
  `DrawOverlay` and the panels. So world code only runs while the world is the active scene, and its
  `main_scene` always means the world. That kept the change out of files other agents were editing.
- **Escape** (`INPUT_CHASM_MENU`) steps back: the tool in hand, then the selection, then the title.
  On the title it asks to quit. Enter (`INPUT_CHASM_CONFIRM`) confirms in a menu.
- **The one pop-up** (`OpenAsk`, `UpdateAsk`, `DrawAsk`): "Quit Chasm?" with Quit / Cancel, Cancel
  focused. Over the game it pauses the world and resumes it after, if it was running.
- **Every way out asks.** Core `Window::f_close_asks_app` (opt-in, default off) turns WM_CLOSE (the
  X, Alt+F4) into `f_close_asked`, which the app turns into its question; a yes quits by
  `RequestQuitAll`. `f_escape_closes_window` is off for Chasm.
- **A scene switch ends a recording or replay** (core, `ApplyPendingSceneSwitch`). Always recording
  (step 4) will have to start a fresh recording on the way back from the title.
- **The diorama: core `SceneCapture`** (`core/SceneCapture.h`, new, opt-in). It writes what a scene
  draws within a horizontal radius to bytes, and loads them into another scene:
  - normal and custom-shader meshes, cut by triangle and baked into world space;
  - materials by name;
  - custom shaders by their two files;
  - instance sets kept to the radius, with their motion. `Object::GetInstanceMotionPad` was added
    for this.
  Skinned and line meshes, lights and parent links stay behind.
- **Chasm's side:**
  - `chasm_diorama` op capture x,z,radius writes `assets/dioramas/<name>.scap`, without the cursor's
    marks, outlines or debug views. A ship build bakes that folder in.
  - op load swaps it into the title without a restart.
  - Both run on the render thread (`ServiceDiorama`), only with the game showing.
  - The title loads `menu_town` at start, and its camera circles it once every three minutes at
    about 29 degrees (`UpdateTitleView`). Its sun's shadow box covers it.
- **`chasm_menu`** reads the screen, the question and the menus, and clicks an item, goes back, or
  closes the window as the X does, for testing minimised.
- *Checked* (8769, debug):
  - The title, the question and the X behave as described: Cancel focused, the game paused under
    the question and resumed after, Continue back to the game.
  - A capture of `village_year5` round (244, -28), radius 32: 105 objects, 27,021 triangles, 1,060
    instances, 4.5 MB. Its houses, fields, roads, palisade, range, river and forest are lit and
    shadowed as in the game.
- **Notes:**
  - The diorama's colours follow the season of the world loaded, because the palette is shared.
  - `menu_town.scap` is `village_year5` for now, until the user builds a town for it.
  - An MCP argument of the wrong type (a string for `include_ui`) aborts the app: core's JSON is
    built without exceptions. That is a core hazard found while testing, not something this work
    changed.

## As built: the diorama's smoke and people (2026-10-16)

The third slice of step 5, at the user's request ("the season following the game is fine; this capture is
as good a town as any").

- **Markers in a capture** (`core/SceneCapture.h`, format version 2; version 1 still loads). A capture
  can carry points the app names, each with its own data (`SceneCaptureMarker`: kind, point, data). They
  are handed back on load, for what the app brings to life where a frozen mesh can't.
- **Smoke from every home, warm or not:**
  - The capture writes a `chimney` marker at each chimney top and camp fire within the radius
    (`HearthFixtures`, given no heat).
  - On load, one set of puffs rises from them (`SetSmoke`), the hearths' own: the same mesh, material,
    LIFE motion and constants.
  - Both functions were taken out of `UploadHearth`, which now calls them. The game's smoke is
    unchanged.
- **People:** a `person` marker for each figure drawn at the moment of the capture. It holds where he
  stands and faces, his clip and how far through it, what he carries, whether he has his bow, and for
  a walker the rest of his way while it stays inside the diorama, sampled on the ground.
  - On load, each is built as the game builds him: `BuildPersonSkin`, taken out of `UploadWorkers`.
  - Each pass (`UpdateTitleView`), a walker goes there and back along his way at his speed, and an
    archer's bow follows his hands (`PlaceBow`, also taken out of `UploadWorkers`, with no arrow in
    hand).
  - The capture now leaves out the people's own objects (`Worker `, `Crowd`), the range's arrows and
    the zombies, so nothing they held hangs in the air.
- **`chasm_menu` op view** points the title camera by hand: a centre, a distance and a pitch, still
  turning. It's for looking closer and trying framings.
- *Checked* (8769, debug):
  - `village_year5` with its range set working again (in the running copy only; the save was not
    written), and stepped until Dirk De Wit was on the line, then captured round (244, -28), radius 32.
    That gave 11 markers: 4 chimneys and 7 people, 4.3 MB.
  - In the title: the smoke rising over the houses; four villagers by the stores, on the road and by a
    field, one carrying a sack; one walking (moved 2.5 units in 2 s); Dirk at the target with bow and
    quiver, his pose changing through the Shoot clip.
  - Back in the game, its people still drawn.
- **Not yet:**
  - Children: their figures aren't rigged yet, so they are left out.
  - The arrow in an archer's hand and arrows in flight.
  - A working clip of its own (chopping, carrying): new animation for the user. Shooting is the one
    the rig has today.
  - The smoke is the hearths' as it is: thin puffs that swell as they rise. A thicker column for the
    title would be a tuning of its own.

## As built: the diorama's clouds and birds (2026-10-18)

- **Clouds round the rim** (the user's: "not by exporting it from a running game, but reusing the same
  clouds ... just at the rim of the diorama"). `BuildDioramaClouds` in `ApplicationChasmExplore.cpp` runs
  whenever a diorama loads. It works with any capture, `menu_town` as it is, and needs no re-capture.
  - The ring is the world's own puffs: `CloudPuff`, the recipe taken out of `UploadClouds`, which now
    calls it.
  - The bank starts `CLOUD_RIM_INSIDE` in from the rim, small and sunk as at an explored edge. It is at
    full size past the rim and runs `CLOUD_RIM_OUTSIDE` beyond it, thinning at its far edge.
  - The puffs breathe on the title's clock.
  - Each sits on the rim's ground on its side: the highest captured `Terrain` vertex in that sector of a
    band just inside the rim. Over the chasm there is no such ground, so the bank lies at the capture's
    level.
  - Seen round `menu_town`: 330 puffs, every sector with ground. The town is a clearing in the cloud bank
    from every side as the camera turns, and the cut edge is hidden.
  - The near side of the ring is below the frame at the title's framing, so only the far half shows.
- **A flock over the town:** `birds_plan.md` step 5. Captures now carry `perch` markers, and `menu_town`
  needs capturing again for the flock to land.

## As built: the pause menu, saves and New game (2026-10-16)

Steps 1 and 2, at the user's go-ahead, in `ApplicationChasmMenu.cpp` (new) with the title's file.

- **The pause menu** is drawn over the frozen game: the world is dimmed and the HUD put away, as under
  the Statistics page.
  - Escape opens it once nothing is in hand or selected (`EscapeStep`). Escape in it goes back to the
    game.
  - Buttons: Resume, Save, Load, Statistics, Main menu, Quit. Statistics opens the stats page over it,
    and closing the page comes back to it. Main menu goes to the title with the colony kept under it.
  - Settings, Controls and recording come with steps 3 and 4.
- **The title** has Continue, New game, Load and Quit.
  - Continue goes back to the colony in play. With none (the ship build at start) it loads the newest
    save of any kind, and it is greyed out with no saves at all.
  - A debug build still starts in the world, with a colony in play.
- **New game** lists four maps (`CHASM_STARTS`), each a name, a seed and a size, shown as "small",
  "medium" or "large".
  - **Placeholders until the user and Claude choose them:** seed 12 at 50k cells (the map every test
    has used), seed 3 at 35k, seed 27 at 50k, seed 41 at 80k.
  - A new game generates the map, places the camp and the first dawn of spring at once
    (`EnsureZonesWorld`), and puts the view on the camp.
- **Load** lists every save, newest first, seven to a page: its title, its people and when it was
  written. A save without a title (from the tools or the panel) is listed by its file name.
- **Save** lists "New save" and the saves to save over; saving over one asks first. Autosaves and the
  quick save aren't in this list, as they are written by themselves.
- **Save names are automatic:** "Seed 27 - year 1, summer 1".
  - The file is `save_<date>_<time>.json`.
  - The title, kind (save, auto or quick), colony and build go in a `meta` block. `ChasmSaveToJson`
    doesn't write it, so a replay's start state is unchanged.
  - The colony's name is the start's name. A save without one is named "Seed N".
- **Autosave**, rotating through `autosave_1` to `autosave_3` (the oldest is written over):
  - at the first dawn of each season (`UpdateAutosave`), but not while replaying, and not when a load
    or a tool lands mid-year;
  - on Quit;
  - on leaving a colony for a load or a new game, never into the slot being loaded.
  - "Autosaved" shows at the bottom for three seconds.
- **Changed from the plan: the questions don't offer "Save and quit".** Since leaving autosaves, the
  quit question is "Quit Chasm? The colony is saved first, as the autosave", with Quit / Cancel.
  Loading or starting over a colony in play asks "Leave this colony?" the same way. Nothing is lost
  either way, so there is no time-since-the-last-save line.
- **Quick save:** F5 saves to `quicksave`, and F8 loads it with no question. Both keys are the
  viewer's and are not recorded.
- **A save from another build is refused if its map has changed.** The save's map is generated from
  its settings and its hash compared before anything is replaced. A mismatch says "This save cannot
  be loaded: it was made by <build>, whose map is not this build's", and the colony in play stays.
- **Loading** waits a frame behind "Loading ..." or "Making the map ...", so the word is on screen
  while the map generates (about half a second in debug).
  - After it, no menu is left up, nothing is in hand or selected, and the view is on the colony
    (`FrameColony`): the person nearest the middle of its people, or its camp.
  - `ApplyCamera` now always moves the game's camera, because a colony is entered from the title,
    where the title is the active scene.
- **Saves go next to the exe** (`ChasmSaveDirectory`): `apps/chasm/saves/` for an exe in `build/` (as
  before, now regardless of the working directory), and `saves/` beside a ship exe. Recordings still
  go to the working directory, until step 4.
- **`chasm_menu`** reads all of this: the screen (title, game, pause), the list open, the question
  with its text, the busy word, the last note, and whether a colony is in play. It gains op `pause`,
  and op `click` reaches whichever menu is in front.
- *Checked* (8769, debug, and a build without panels):
  - Pause, Save (a new save with its meta) and Load.
  - Main menu, then New game on seed 3 and on seed 27: asked first, autosaved, the view on the
    camp.
  - Loading `village_year5` from the title, with the view on the village.
  - A save with a wrong hash refused, with the colony kept.
  - Statistics from the pause menu and back.
  - The quit question cancelled, then quit: autosaved, and a clean exit.
  - A season turning autosaved into the oldest slot.
  - The build without panels starts on the title with no colony in play, and Continue loaded the
    newest save.
  - F5 and F8 weren't checked: no tool presses keys on a minimised window.

## As built: recording for testers (2026-10-16)

Step 4, taken before step 3 at the user's go-ahead: there is little to put in Settings until there is
sound. The three core changes were agreed with the user first.

- **Core, the log file** (`Debugger::OpenLogFile`, opt-in per app).
  - Every line logged also goes to a file, with the time of day and without the colours, flushed as
    it is written.
  - The last run's file is kept as `<name>_previous.log`. A second copy running at the same time
    writes `<name>_2.log`. The file stops at 32 MB.
  - Chasm opens `chasm.log` beside the exe (`main.cpp`).
- **Core, crash reports in release builds** (`core/CrashReport.cpp`). The unhandled-exception filter
  is now installed in every build.
  - A release crash writes `crash_<date>_<time>.dmp` (a minidump; dbghelp is loaded only then) and
    the `.txt`, with the failing thread's stack as `exe+offset`, walked with the x64 unwind tables.
  - A release link now also writes `build/<exe>.map` (`engine.mk`, `RLINK`), and
    `tools/crash_map.py` names each frame from it.
  - Checked with a small program that crashes on purpose: the stack came back as
    `Deeper(int*)`, `Deep(int*)`, `main`.
- **Core, recording requests.**
  - `RequestRecordingStart(stem)` names the file in place of the app.
  - `RequestRecordingRestart(stem)` saves the recording running and starts the next on the same
    pass.
  - `DrawInputRecordingBadge` is virtual: Chasm draws its own, because the core badge sat on its
    top bar.
  - Defaults are unchanged for every other app.
- **Always recording** (`ApplicationChasmRecord.cpp`).
  - A fresh recording `chasm_auto_<date>_<time>.rec` starts on entering a colony, at every
    autosave, on coming back from the title, and after a replay ends.
  - The ten newest are kept. A person's own recordings (F9, `input_record`) are never deleted.
  - Quit waits for the recording to be saved before going.
  - On in a build without the panels (the tester's build). In a debug build it is a checkbox in the
    Save header, because it would take over an agent's `input_record`.
  - A recording's start state now carries the save's `meta`: title, colony and build.
  - Replaying a recording made by another build says so.
- **REC** and the minutes recorded show in the top-left corner.
- **Report a bug** (the pause menu, F12) writes `reports/<date>_<time>/` beside the saves, and then
  shows the folder's path with "Open the folder" (Explorer) or OK. The folder holds:
  - the recording up to now, which starts from the last autosave and so carries it;
  - `now.json`, the colony at that moment;
  - a screenshot of the game, without the menu;
  - `chasm.log`, plus the last run's log if there is one;
  - any crash report from the last two days, and `settings.json` if there is one;
  - `report.txt`, saying what each file is.

  The files are gathered on a worker thread, because both the recording's save and the screenshot
  wait on other threads.
- **Replay a recording** is on the title. It lists Chasm's recordings, newest first, with title,
  length and date. Over a colony in play it asks first, like a load.
  - While a replay runs, a bar top left shows how far it has got, with STOP. Pause and speed are the
    calendar's.
  - Commands made during a replay were already dropped by core, so looking round can't change it.
- **A rules fix found on the way** (`Economy::Restore`). A save made before a new colony's first
  tick lost that tick: no supplies, no rubble, no history. Every new game's first recording starts
  at that moment.
  - Such a save is now recognised by calendar 0 and an empty journal, and its first tick follows
    the zones again, as a fresh start does.
  - Saves from later in a game are unaffected. A recording made before the fix that starts at
    calendar 0 now replays with the supplies landing.
- *Checked* (8769, debug and without panels):
  - A new colony recorded from its first tick: REC on screen.
  - Report a bug wrote the folder, with the recording, now.json, the screenshot, the log and the
    page.
  - The report's recording replayed from the title. A short one played out and recording started
    again; a long one was stopped with STOP.
  - A run of 603 ticks from a new game, then its recording replayed to the same tick: the same
    economy digest. Before the fix the replay had no supplies.
  - The release build compiles and writes its map.
  - Not checked by hand: F12 (no tool presses keys on a minimised window), and "Open the folder".

## As built: Settings (2026-10-16)

Step 3, once sound and music were in (the user: "pretty much the same as archer's"; no key mapping yet,
and it is not meant to be played with a controller, so there is no Controls screen).

- **Settings is on the title and in the pause menu**, with archer's five settings:
  - anti-aliasing: Off, 4x or 16x;
  - master volume;
  - music;
  - sound effects;
  - start full screen. Alt+Enter, core's, still switches full screen straight away.
- **The store is core `Settings`**, `settings.json` beside the exe, read from disk only.
  - It is read in the constructor (`LoadSettings`), so "start full screen" is known before the
    window is made.
  - Each change is saved as it is made.
  - The volumes default to 1.0, so a first run sounds the way the cue table and the score were
    mixed.
- **Applied by the view only** (`ApplySettings`, every pass from `UpdateView`), so a recording
  replays the same at any setting.
  - The master goes to `sound_volume`, only when a setting changes, so `chasm_sound`'s volume still
    works.
  - Music goes to the score's `music` bus, over its three players. The music's own code sets only
    those three players' buses.
  - Sound effects go to the cue table's new layer bus.
  - Anti-aliasing goes to `Renderer::RequestAASamples`, only when it differs.
- **The cue table now plays under a layer bus of its own, `sounds`** (`CueSoundOutput`'s layer, as
  archer has; the music agent suggested it).
  - The table's buses are now `sounds/effects`, `sounds/activity` and so on.
  - The effects volume is the layer's gain, which the table never sets. A gain on the table's own
    `effects` bus would be undone by each table load and by any duck on it.
- **`chasm_menu`** reads the settings as saved, as applied, and the buses' gains read back.
- *Checked* (8769, the tester's build):
  - Settings from the title and from the pause menu.
  - The music, effects, master and anti-aliasing clicked: saved to `settings.json` and applied, the
    buses read back at 0.85, 0.55 and 0.35 and the renderer at 16 samples.
  - A restart started with them.
  - The table's sounds were playing on `sounds/ambience`.
  - Start full screen saved; it takes effect at the next start that isn't `--minimized`.
  - The test's `settings.json` was deleted afterwards.

## As built: discoveries (2026-10-16)

Step 6, with the user's answers of the same day:

1. **Read from the journal, not saved as flags** ("Yes, might as well"). The statistics' journal is saved,
   hashed and never trimmed, and already held a building finished with its kind, a seam seen and an area
   found, each with its tick. So a discovery has no saved state of its own.
2. **What unlocks what** (the user's):

   | Discovery | Brings |
   |---|---|
   | a river (or a waterfall) found | WATER, BRIDGE |
   | the chasm found | WINCH |
   | a boulder the stonecutter can break | STONECUTTER |
   | floatstone seen | MINE |
   | a builders' hut | WALL, RANGE, TOWER |

   Everything else is on the bar from the landing.
3. **Notices for a crop lost to frost, someone growing up, and a couple expecting** (the user's: "that's
   a good one. Someone growing up, and a couple getting pregnant").

What was built:

- **`ChasmDiscoveries`** (`ApplicationChasm.h`), `DISCOVERY_*`: river, chasm, boulder, floatstone,
  builders' hut, winch, balcony. The last two are the second goal's (step 7) and bring no tool.
  `UpdateDiscoveries` (`ApplicationChasmExplore.cpp`) works them out on the physics thread whenever the
  journal grows, more is seen or the zones change. It publishes them as `discovered_mask` and a snapshot.
  A load works them out before it returns, so a load while paused shows the right bar.
- **Three sources:**
  - the journal, with the tick and who;
  - what is seen without an event: the landing's first look, which tells nothing, dated to the history's
    start;
  - **a building standing whole** of a kind that needs one: a collector or bridge shows a river, a winch
    the chasm, a stonecutter's hut a boulder, a mine floatstone, and a builders' hut, range or tower the
    builders. That covers debug-mode buildings, which are never sites and so never journaled, and saves
    from before the journal (`casm_edge` had a builders' hut and a winch, and they were locked until this
    was added).
- **Two new events for the two firsts nothing else marked**, looked for by `LookForFirsts` on the
  exploration's ticks until each is in the journal:
  - `EVENT_BOULDER_FOUND`: a standing boulder on a settlement's territory (`EconomyState::settlement`),
    on explored ground. That is "one he can mine": territory is the land walked to and built on. It is
    credited to whoever is nearest.
  - `EVENT_BALCONY_REACHED`: anyone whose ground is a balcony's (`TERRAIN_BALCONY`). Only a winch takes
    anyone down.
- **Three new events for the notices**: `EVENT_FROST_TOOK` (a field, its crop), `EVENT_GREW_UP` (child to
  young adult, when they go to work), `EVENT_EXPECTING` (in `ChancesOfChildren`). All five are journaled,
  appended (13 to 17), named for the cue table (`frost_took`, `grew_up`, `expecting`, `boulder_found`,
  `balcony_reached`) with a chime row each, and have a line on the statistics page.
- **Play mode only** (`ToolsLocked`; debug mode builds anything):
  - a locked tool is left off the build bar, the rest closing up;
  - its key does nothing, and one in hand is put down;
  - `chasm_tool` refuses it, and so does a `chasm_paint` with `play: true` for its kind;
  - NEEDS ATTENTION never offers it: FIND A RIVER (WATER n DAYS) stands in for BUILD A WATER COLLECTOR, and
    NEEDS STONE says FIND THE CHASM before a winch, and skips the stonecutter's hut while it is locked.
- **NEW**: a tool a discovery has just brought has an amber dot and lit text until it is first taken up.
  What a colony has on entering it (`discoveries_epoch`) is not new.
- **The notice line** tells the new events, and a discovery says what it brings: "ANOUK WEVER HAS FOUND A
  RIVER - NEW TOOLS: WATER, BRIDGE". It stays 8 s, not 5. The first builders' hut and the first winch
  finished are told for this. The frost is one line for every field it took that dawn ("THE FROST TOOK THE
  CROPS ON 4 FIELDS"). A couple shares one surname when they have it.
- **`chasm_economy`**: `discoveries` (each made or not, tick, person, tools) and `tools_locked`.
- **Wiki**: a new `discoveries.md`; `events.md` and `statistics.md` updated.

Checked (debug, port 8769, play mode):

- **New seed-12 colony.** No river or chasm in sight at the landing, so WATER, BRIDGE, WINCH, MINE, WALL,
  RANGE and TOWER were off the bar, with FIND A RIVER · WATER 6 DAYS. A boulder was found on tick 10, which
  brought STONECUTTER, marked NEW.
- **The river.** Anouk Wever found a river on tick 1580 while picking. The notice said so with WATER and
  BRIDGE, both marked NEW, and the hint became BUILD A WATER COLLECTOR.
- **The builders' hut**, a play site: while it stood unfinished, a play `chasm_paint` wall and
  `chasm_tool tower` were refused ("not discovered yet: builders_hut"). Finished on tick 2490, it
  brought WALL, RANGE and TOWER, with its notice.
- **`casm_edge`** (a save from before the journal): its river and chasm were read from the exploration;
  its builders' hut, winch and stonecutter's hut from the zones. Only MINE stayed locked, and nothing was
  marked NEW.
- **The balcony.** Jelle De Wit, sent by order to rubble on a balcony, went down the winch: "JELLE DE WIT
  HAS REACHED A BALCONY".
- **The frost** (`set_day` 29, stepped into winter): four events on one tick, one line on the notice line,
  and "The frost took a field of beans" in the statistics' journal.
- **Expecting**: "BENTE VEEN AND DOLF DE WIT ARE EXPECTING A CHILD" on her couple's day.
- **Grew up**: Teun De Wit on tick 27000, journaled and turned picker. Its notice was not seen: the first
  boulder was found on the same tick, and the line shows the newest.
- **A replay** of 3,000 recorded ticks (a builders' hut placed in play, the boulder, the river) ended in
  the same state, discoveries and events included.
- **Without MCP and ImGui**, as `make ship` builds it, everything compiles.

Not checked by a run: floatstone seen bringing MINE, and the chasm found by a person rather than read
from a save. Both go through the same journal events as the river.

## As built: the goals and the end (2026-10-16)

Step 7, as planned under "The demo's goals", with one addition (the first point below).

- **The end comes once both goals are decided**, not only when both are met. The second goal can't fail,
  so a colony that lost someone before the second spring would otherwise never reach an end. Then it is
  "DEMO COMPLETE" with how each goal went: the journal says "Demo complete, 1 of 2 goals". **For the user
  to overrule** if only a full success should end the demo.
- **The goals are rules** (`ApplicationChasmGoals.cpp`, `TickGoals`, after the exploration on every
  tick). Each state is worked out from what is saved:
  - Through the winter: failed by a death before `CALENDAR_YEAR_TICKS`, the first dawn of the second
    spring; met at that dawn.
  - The chasm: met by the discoveries `winch` and `balcony`.

  Each outcome is told once as an event, appended (18 to 21): `EVENT_GOAL_MET`, `EVENT_GOAL_FAILED`,
  `EVENT_DEMO_COMPLETE` (`amount` the goals met) and `EVENT_COLONY_LOST`. They are journaled, and
  `UpdateDiscoveries` reads them back, so a load never tells one again. `CHASM_GOAL_*` are in `Economy.h`.
- **The goals panel**, top left in play mode, looks like NEEDS ATTENTION. Each goal shows what it wants
  next, or MET or FAILED:
  - goal 1: "23 DAYS TO GO";
  - goal 2: FIND THE CHASM, then BUILD A WINCH, then LOWER SOMEONE DOWN.

  The top left is now a stack: the goals, then REC or the replay bar, then the selected colony's name.
  The REC pill and the colony name used to overlap there.
- **The death pop-up**: `CHASM_ASK_DEATH`, the one pop-up, which pauses the game as the others do.
  - It lists the dead of those ticks, four by name, then "and n more".
  - It adds "The first goal is failed: not everyone lived to see the second spring." when the death
    failed it.
  - Continue plays on. Main menu goes to the title, losing nothing, as the pause menu's does.
  - The pop-up's text may now run to several lines (`DrawMenuPanel`).
- **The end's page is the statistics page** (the user's, 2026-10-16), in an end mode (`stats_end`):
  - its heading is DEMO COMPLETE, or THE COLONY IS LOST in red;
  - it has no close mark, and Tab does not close it;
  - a foot under the columns shows each goal's outcome and the buttons in a row: Keep playing and Main
    menu, or Load, New game and Main menu. Escape keeps playing after a complete demo; a lost colony is
    left only by its buttons.
- **Who opens them**: `UpdateEnd`, in `UpdateView`, reads the fresh events as the notice line does. The
  pop-ups are play mode's only: debug mode, which agents drive, is never stopped. An end waits for the
  death pop-up in front of it to close, and a lost colony skips the death pop-up.
- **A lost colony has nothing left to lose**:
  - Load and New game ask nothing (`Choose`);
  - nothing autosaves it, not leaving, quitting or a season's dawn (`Autosave`);
  - the quit question doesn't promise a save.
- **The notice line** tells "GOAL MET: REACH THE CHASM" and "GOAL FAILED: ...".
- **The rest:**
  - the cue table has a chime row for each new event (stingers later, `sound_plan.md`);
  - the statistics journal has a line for each;
  - `chasm_menu` reports the `death` question, `end_page` (and its buttons, clicked by item), `goals`,
    `demo_complete` and `colony_lost`.
  - A fix in `ChasmStatsDays`: with nobody alive, the days of food and water are 0. They had shown the
    whole store as about 200 days on a lost colony's chart.

Checked (debug, port 8769, play mode, `casm_edge`):

- **The panel**: 24 days to go, LOWER SOMEONE DOWN.
- **A death** (a recorded needs edit): "A villager died - Bente Veen, of thirst", with the first goal
  failed on the same pop-up, the panel saying FAILED, and the notice line.
- **Continue, then someone sent down the winch**: GOAL MET: REACH THE CHASM, then DEMO COMPLETE (1 of 2)
  with Keep playing focused. Keep playing went back to the game.
- **Both met**: on a fresh load, the chasm reached first (a notice only, the panel MET), then `set_day` 39
  and stepped to tick 90000. The first goal was met at the dawn, and DEMO COMPLETE showed both MET.
- **The colony lost**: everyone's needs emptied. "13 villagers died - ... and 9 more", Continue, then the
  last death opened THE COLONY IS LOST with Load, New game and Main menu. New game opened the map list
  with no question, and entering a map left the autosaves untouched.
- **The death pop-up's Main menu** went to the title, and Continue came back with the first goal FAILED.
- **A replay** of 2,500 recorded ticks with a death and a balcony reached ended the same: died and goal
  failed on 37785; balcony, goal met and demo complete on 39090, in both.
- **Without MCP and ImGui**, it compiles.

## As built: the version (2026-10-16)

The user's: "We'll start with just 0.0.1. But we're not following semver or anything like that". Shown with the
git hash and the build date, since the old "debug build, Oct 16 2026" in the title's corner was hardly visible.

- **`ChasmVersion.h/.cpp`**: the number, `CHASM_VERSION` in the `.cpp`, raised by hand; the commit, from
  `git describe --always --dirty=+`, so "210d9e4+" means built with changes not yet committed; and the day.
- **The makefile passes the commit and the day to that one file.** It writes `version.stamp` in the object
  folder only when either differs from the last build's, and the object depends on it. So the file
  recompiles on a new commit or a new day, and a build with nothing changed still has nothing to do. The
  old date came from `__DATE__` in the title's file, which was only as new as that file's last edit.
- **Shown:**
  - in the window's title: "Chasm 0.0.1";
  - under the title's logo: "VERSION 0.0.1";
  - on a panel in the bottom right of the title and the pause menu: "CHASM 0.0.1 - 210d9e4+ - 2026-10-16 -
    DEBUG BUILD" (`DrawHudBuild`), so a tester's screenshot names its build.
- **`BuildName()`**, which saves, recordings and bug reports carry, is now "0.0.1 (210d9e4+, 2026-10-16, debug)".
  A recording from another build still says it may replay differently. `chasm_menu` reports it as `build`.

## As built: the interface's size (2026-10-16)

The user's: the interface "scales bigger when we go fullscreen. But the size is quite nice in the windowed mode".
Every HUD size was the window's height over 810, the height Chasm opens at: 1.33 at 1080 and 1.78 at 1440. And
the menus, laid out in screen heights, grew the same. Of the options laid out (never bigger than windowed; grow
more slowly; follow Windows' display scaling; a setting), the user chose growing more slowly, with a setting.

- **`HudUiScale`** (`ChasmHud.h`), the one place it is worked out: the **square root** of the height over
  `HUD_UI_HEIGHT` (810), times the setting, never under 0.5. That gives 1.15 at 1080, 1.33 at 1440 and 1.63 at
  2160. The HUD, the statistics page (its close mark and the end's buttons too) and every menu use it.
- **The menus**: `UIMenuLayout::scale`, new in core, multiplies an item's width, height and gap but not where
  the column starts. It defaults to 1, so archer's menus are unchanged. Chasm sets `HudMenuScale` on all its
  menus each pass (`ReadMenuInput`) and each frame (`DrawOverlay`), so a menu just opened is drawn at its size.
- **The setting**: Settings > **Interface size**, 75%, 85%, 100%, 115%, 130% or 150%. It is `ui_size` in
  `settings.json` and applies at once. Clicking along it moves the rows under the mouse as they shrink or grow.
- Not changed: how near the cursor a person or a crate is picked (`PlayPickUnder`, `PERSON_NEAR_PX`). That goes
  with the size of the world on screen, which does grow with the window.

Checked in a test window: 810 high, 1.0, as before; 1421 high, 1.32 (it was 1.75), and the HUD and pause
menu shown at that; Interface size stepped to 85%, saved, and the menus shrank at once. The user's
`settings.json` was put back after.

## As built: the ship's pack (2026-10-17)

The user, looking at what `make ship` bakes in: `fonts/consola.ttf` and `sound/click.wav` beside `click.ogg`.
Every packed file was checked against what loads it. Nine more `--exclude`s in the makefile, each with its reason:

- **`fonts/consola.ttf`**, 254 KB: ImGui's font, loaded by core's `WindowImGui.cpp` alone. Excluded only when
  `USE_IMGUI` is off, as apps/tetris does - a baked build that still has ImGui dies at startup without it.
- **`sound/click.wav`**, 39 KB: the SHARED click. Chasm's own became `click.ogg`, which the cue table plays, so the
  shared wav no longer stood behind one of its name.
- **Seven shaders**, 9 KB: the skybox's two (none is drawn), the field shadows' three (only from
  `Renderer::EnableFieldShadows`, never called here), `texture.comp` and `noise3d.comp` (named by nothing in core or
  here). Using any of them later means taking its line out.

The pack went from 120 files and 3.86 MB to 111 and 3.56 MB. Kept, though nothing calls for them yet: four of the
score's stingers no cue plays (low hit, swell, gust, harp fall, 62 KB - the score's to drop, in apps/music), and
`lowres_composite.frag` and `render_upscale.frag` (3 KB), which only the debug panels and MCP can switch on.

A baked build now lists its packed assets as it exits (`main.cpp`, `BinaryAsset::ListBinaryAssets`), each marked if
the session never asked for it, and anything it read off a disk instead. *Checked*: the ship exe started to the title
with nothing failing to load; it asked for 105 of the 111 files there, the other six being the outline shaders (made
on the first selection) and the two above. Not seen: the exit listing itself - the exe was stopped, since the title's
close asks first.

## Open questions

**The four starts** stay as they are for now (the user, 2026-10-16), who changes `CHASM_STARTS` by
hand when an interesting seed turns up.

Everything else asked so far was answered on 2026-10-15 (see "Decided"). **Limiting the build tools**
was settled on 2026-10-16: tools appear as they are discovered (see "As built: discoveries").
