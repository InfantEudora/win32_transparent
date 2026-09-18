### Bomerman-Like Game

Main goal is to have a simple playable game, using animation and custom shaders to achieve simple mechanics.

All the assets in this game are generated, and live in a single asset file. A one click export from the associateb Blender file.
Rule is that any asset offsets, size, should be in Blender. All scaled 1.0 and all materials loaded from the same file.

### Gameplay goal

The goal is to reach the exit (A doorway). The doorway is closed, and the key for the door is hidden under one of the tiles that can be bombed. These are hedges, or wooden walls that block the players way.

Enemies can cut down hedges, or they can attack the player. Each attack takes one of the player's lives. He starts with three.

---

## Where it stands

`Maze` owns every rule below and `make rules` tests them with no engine, no window and no GPU.
`ApplicationBomber` owns what they look like. Nothing in this list needs an asset that is missing.

### Built

- **The field.** 16x16, generated from a seed. Three zone styles blended across the board (pillars,
  labyrinth, plaza), ponds with directional bridges, reachability pruning, and a re-roll when a
  layout scores under `MAZE_MIN_PLAYABLE` so a seed can never produce a 14-cell board again.
- **Walking.** One `MazeWalker` per body, tile to tile, `MAZE_STEP_TICKS` per step. A held direction
  rolls into the next step, so movement is continuous rather than stop-start.
- **Bombs.** One at a time. `MAZE_FUSE_TICKS` fuse, four arms, `MAZE_BLAST_RANGE` tiles. A soft block
  is reached, burnt, and stops that arm there; a hard wall stops it before.
- **Destructibles.** Hedge and wood, both bombable, both leaving grass behind.
- **Buried pickups.** Health and shield, plus coin, diamond and crystal for score. Exactly one
  list per board (4 coins, 1 each of the rest) rather than a roll of one. A taken pickup shrinks
  away over 18 ticks instead of blinking out; coins and diamonds turn on the spot.
- **The worn shield.** `equipped_shield` is a child of the character while a shield is running, so
  it follows and turns with them for nothing and is freed with them.
- **The exit.** `MAZE_TILE_DOOR` in the border wall, placed by the generator, locked until the key
  is dug up. The archway and its swinging leaf follow the rule rather than the other way round.
- **Lilly pads**, on open water only - never on a bridged cell.
- **Player condition.** Health, shield, hit invulnerability, death, respawn on the same field.
- **Enemies.** Up to four, spawned away from the player, wandering. They cut hedges
  (`MAZE_CHOP_TICKS`), they hurt the player on contact, and they die to a blast. None of them is
  ever born somewhere it can do nothing - see the note at the bottom.
- **Animation.** Enemy walk, chop and death, cross-fading. The turd idles. Blending lives in
  `Object` now, so any object can have it.
- **The blast.** Raymarched through a 3D noise volume in a CustomShaderPass, clamped against the
  solid scene, with a point light so the walls around it light up.

### To build

Roughly in the order each one unblocks the next.

1. **Win condition: key, door, exit.** HALF DONE.
   - ~~The key unlocks the door.~~ `MAZE_ITEM_KEY` sets `Maze::f_has_key`, and `MAZE_TILE_DOOR` is
     the one tile whose passability is a property of the GAME rather than of the cell - `IsPassable`
     tests the flag before it tests `IsBlock`. It stays inside the `IsBlock` range, so a blast still
     stops at it, an enemy cannot cut it, and no soft block is laid over it. The key is worth no
     points: all it does is open the way out.
   - ~~The door is placed by the rules.~~ `PlaceDoor` picks a border cell whose inward neighbour is
     open, preferring one `MAZE_DOOR_MIN_DIST` from the spawn. The view reads `maze.door_x/door_z`,
     so the special case that used to keep a brick off the door's cell is gone.
   - ~~The key is always diggable.~~ It is first in `MAZE_ITEM_ORDER` so a small board still buries
     it, and `AddItems` will not lay it under a block with no open cell beside it. That was the soft
     lock: a key nobody can reach looks exactly like a board you have not searched hard enough.
   - ~~Carrying state across a level.~~ Done by putting it on the walker - see below. Score, health
     and the key travel with the body, so the transition has nothing to marshal.
   - ~~**Walking through it ends the level.**~~ BUILT 2026-09-16. A HALLWAY. Stepping into the
     open exit builds a 3 x (4..8) corridor in front of the player, tiles popping in from below; the
     door shuts behind you, which is the COMMIT POINT - before it shuts nothing may be destroyed,
     after it shuts the old board is unreachable and unseeable and is replaced. A second door at the
     far end opens when you stand in front of it.
     - It is a separate `Hallway`, NOT a size parameter on `Maze` and not a second `Maze`: the two
       are never simulated at once (the app gets a phase), they share nothing but the walker, and
       `Maze` is already carrying enough bomberman-specific rules without also having to be a
       corridor. `MazeWalker` is a standalone 40-byte struct and `DirX`/`DirZ`/`DirOpposite` are
       already `static`, so the sharing is free.
     - Nothing can follow you in. Nothing can hurt you there. It is where the score tally goes.
     - Where the new board is BUILT is the part that will bite: the far door has to open onto its
       spawn. The neat answer is to re-anchor the moment the near door shuts - both doors closed,
       the old board gone, the hallway a sealed box with no external reference and no skybox, so the
       hallway, the player in it and `camera_target` can all be moved together and nobody can tell.
       The board then stays at the origin forever and the world never drifts. `CellCentre` is the
       lever either way.
     - The pop-in is a TWEEN, not a clip - a staggered per-cell rise, same shape as the pickup
       shrink in `TickPickupView`. No new asset: floor, `wall_brick`, and two more
       `wall_doorway`+`door` pairs running the clip that is already there.

   **What actually got built, and the one thing that bit.** `Hallway.h/.cpp`, 44 rules checks, and
   the app grows a phase - one of the two ticks, never both. The corridor keeps TWO FRAMES and that
   is the part worth knowing: `forward` is the frame it is BUILT in and turns at the commit;
   `control_forward` is the frame the player's HANDS are in and never does. With one frame, turning
   the corridor turned the controls with it - the camera rotates too, so nothing happens on screen,
   and the key that had been walking you forward walks you into the side wall. You stop dead half
   way down a corridor that looks completely normal. `bomber_state` reports both, and says which one
   to press.

   **The camera comes down with you, and the old board sinks away.** Both done the same day, and
   they solved each other: from the board's high angle you could see clean over a one-brick corridor
   wall, so the swap was visible. The camera came down behind and above the player IN THE CORRIDOR'S
   FRAME - turning with the corridor at the commit, so that rotation stayed invisible for free - and
   the walls filled the frame, which hid the swap and left the corridor big enough to read a score
   tally in. Coming out it eased back to exactly where the camera had been, saved rather than
   recomputed.

   **That framing was replaced on 2026-09-17, and the reason is worth keeping.** It turned the view a
   quarter or a half turn every single level, because a corridor runs whichever way the exit faced -
   and the keys are WORLD directions, so screen-up stopped being the forward key for the length of
   the walk. The corridor's rules had already been bent round this once (`control_forward`); the view
   had not. See **The camera** below for what replaced it.

   The old board does not blink out, it FALLS. `SinkBoard` is the corridor's pop-in run backwards,
   staggered by distance from the exit so the level collapses away behind you, and squared so it
   starts gently and accelerates - a linear drop reads like a lift. It starts at the SEAL and must
   finish before the commit throws the objects away, so the spread plus the fall has to stay inside
   `BOMBER_HALL_COMMIT_TICKS`. It is applied as a per-tick DELTA, which is safe only because
   `SyncView` returns early in the corridor and nothing else is writing those positions.

   **The dolly needed a new rule, not just a camera.** Bringing the camera down means a short
   corridor can be walked faster than the board takes to sink and the door takes to shut - so the
   player could reach a far door with nothing behind it. `Hallway::f_next_ready` is the app telling
   the rules the next board is standing there, and the far door will not open without it. That kept
   the 4..8 range instead of forcing a longer minimum.
2. ~~**Score: coin and diamond.**~~ DONE. Coin 10, diamond 50, crystal 250 - 5x steps, so a
   crystal is the thing that happened this round rather than a few more coins. `MazeItemScore` is
   the one place that says so; a treasure added later needs a line there and nothing else.
3. **Enemies that hunt.** They have no awareness of the player at all today. The intent is not a
   perfect chase - a short sight line down a clear row, or a walk-toward-the-player bias inside a
   radius, is enough to make the board feel dangerous. Now that the walk clip exists it reads.
4. **Power-ups: more bombs, longer blast.** `MAZE_BLAST_RANGE` and the one-bomb limit are both
   constants. Making them per-player state is the change; the bomb list becoming a list is the only
   part with any weight to it.
5. ~~**HUD.**~~ BUILT 2026-09-17, on `UIOverlay` - the first thing in bomber to use it. See
   **The HUD** below. What is left of this item is the part that needs an engine change: the overlay
   can draw rounded boxes and ASCII and nothing else, so the icons are composed from rectangles.
6. ~~**Player animation.**~~ DONE 2026-09-17. Skinned skeleton, three clips - idle, walk and
   death - with the walk's rate solved against the step speed. See **The player's rig** below. A
   place-bomb clip is the only one left and is the least valuable of the four; the blast sells it.
7. ~~**Sound.**~~ BUILT 2026-09-17 - `USE_SOUND := 1` and the seven wavs in `assets/sound/` are
   wired. See **Sound** below, including the four clips the game still wants and does not have.
8. **Lives, game over, next level.** A run rather than a board: keep score across levels, re-roll the
   seed, maybe raise enemy count or lower the fuse.
9. **Second plant decor.** `plant` in the GLB is a different mesh from the `grass_plant` already in
   use. One line in the decor table. (`lilly` went in the same way, on water.) THE LAST UNUSED
   ASSET.

### Assets in the GLB with nothing using them yet

`plant`, and that is all - everything else in the file is in the game as of 2026-09-15.

---

## The camera

Built 2026-09-17. Two modes, the same shape as `apps/pinball`'s `PIN_SHOT_ORBIT`: everything that
can switch the camera switches to both, with no second thing to remember.

- **`BOMBER_CAM_GAME`** is the default and is **solved every pass** from a yaw, a pitch, a distance
  and a pivot - nothing integrates, so there is a framing to *return to* rather than only a position
  that has been pushed around. That is the whole design, and it is what let the transition stop
  saving and restoring a pose.
- **`BOMBER_CAM_FREE`** is the middle-mouse orbit, which used to be the only camera there was. It is
  a debugging affordance and is now behind <kbd>C</kbd>, the panel, and `bomber_camera`.

**The yaw never moves.** Yaw 0 puts the camera due south of the pivot looking north, so screen-up is
`MAZE_DIR_NORTH` - the forward key - and it stays that way on the board, through the corridor, and
onto the next level. Pitch and distance are free to move and do; neither is what a thumb reads.

**Pitch is not free, and the reason is not obvious from the number.** The board is SQUARE, the window
is 16:10, and the vertical FOV is 45. The board's width fills the frame, so its depth has to fit in
less - and the more overhead the pitch, the less that depth foreshortens and the more of it there is
to fit. *Going top-down spends board.* 58 degrees at 19 out is the most overhead the framing can be
and still show the whole 16x16; 62 at 17, tried first, cost a row and a half of playfield. Anything
further over wants a wider FOV, which is a different change.

**The pivot leans, it does not follow.** A quarter of the way from the middle of the board toward the
player, clamped at 2.5 units. The board is the thing being played and wants to stay in frame; a true
follow would centre the player and put half the board off screen.

**Under `bomber_lock_input` the game camera is off too.** It is solved every pass, so leaving it
running would overwrite an agent's `camera_set` on the next one. With the lock on the camera belongs
to MCP, exactly as it did when the orbit was the only camera. `camera_set` also does not stick in
GAME mode with the lock off - switch to `free` first.

### What it cost the corridor, which is where the real change is

Keeping one world yaw through a transition is impossible while the corridor is ROTATED at the commit:
either the camera turns with it (the old behaviour, and the bug) or the corridor visibly spins under
you. So the rotation went instead.

- **The board's entry border follows the corridor.** `Maze::NewGame` takes an `entry_dir` - the
  outward normal of the border walked in through - and derives `entry_x/entry_z` and `spawn_x/spawn_z`
  from it. `MAZE_SPAWN_X/Z` are gone; the spawn is a member. The commit is now a pure TRANSLATION,
  which is invisible to any camera that shares it, and the camera shares it.
- **`Hallway::control_forward` and `InputToLocal`'s second frame are gone.** They existed only to
  survive that rotation. One frame now, and the two functions are exact inverses.
- **`hall_camera_return`, `cam_return_pos` and `cam_return_target` are gone.** There is nothing to
  restore: the way back to the game framing is to stop being in a corridor.
- **The new board RISES.** `SinkBoard`'s curve read backwards, staggered out from the doorway you are
  about to come through. The old camera hid the swap by burying itself in the corridor; this one does
  not always, so the swap became something worth watching instead of something to hide - the level
  falls away behind you and the next one climbs out of the floor ahead. `EndHallway` finishes
  whatever is still in the air, because on a short corridor the rise can run out of time.

`bomber_state` reports `camera` and `entry`; the corridor block reports one `forward` rather than two
frames. `make rules` grew `TestEntryFollowsTheCorridor` - all four entry sides over 40 seeds each -
and `TestHallTurning` became the test that a corridor's direction survives the whole walk.

---

## The HUD

Built 2026-09-17 on `UIOverlay`, drawn from `ApplicationBomber::DrawOverlay` - render thread, one
draw call, no ImGui. Top left: three life pips, then the key. Top right: the clock. A shield adds a
draining bar under the pips.

**The score is deliberately not on it.** What a board was worth is revealed on the walk OUT of it -
the corridor's HUD adds `TIME BONUS` and `SCORE` - because a number ticking up in the corner while
you play turns a bomberman board into a score attack. The clock IS shown, because that one is
something the player can still act on.

**Nothing is ever removed from the HUD, only dimmed.** A lost life is a dim pip, not a missing one,
so the row says how much is gone as well as how much is left; the key is dim until it is found,
which is what says there is one to find at all. The shield is the single exception - it is an event
rather than a slot, and a permanently empty bar would read as something broken.

**Everything is sized off one unit** (`BOMBER_HUD_UNIT` x window height), so a resize needs no
layout pass. The clock is the only place in the app that converts out of ticks, and it goes through
`Application::GetPhysicsTimestep` rather than a hardcoded 60 - this app sets 60 and the engine
default is 50, so a literal would have been wrong by a fifth from the day it was written.

**`PublishHUD` is why the render thread never touches `maze` or `hall`.** It runs at the top of
`SyncView`, before the corridor branch, and answers the one question the HUD would otherwise have to
ask for itself: which of the two currently owns the walker.

### The clock, which is a RULE

`Maze::level_ticks` runs from the moment a board is laid out until the player steps into the exit,
and `Maze::TimeBonus()` is what it is worth - one point per `MAZE_TIME_BONUS_TICKS_PER_POINT` under
`MAZE_TIME_PAR_TICKS`, so a perfect run pays 200, between a diamond (50) and a crystal (250).

- **A query, not a payment.** `TimeBonus()` does not touch the score and calling it twice gives the
  same answer; `ApplicationBomber::BeginHallway` is what pays it, onto the WALKER, at the tick the
  player steps into the exit - so the points travel with the body like everything else does.
- **A death costs time and nothing else.** The clock does not stop for a corpse or a respawn, which
  is the whole reason dying is worth avoiding on a board you have already solved.
- **Ticks throughout.** The rules have no idea what rate they are ticked at, and a bonus in seconds
  would silently change value if `SetPhysicsTPS` ever did.

`bomber_state` reports a `time` block (`level_ticks`, `par_ticks`, `bonus_now`) and the corridor
block reports what the last board actually paid. `make rules` grew `TestTimeBonus`.

### THE OVERLAY CANNOT DRAW A TEXTURE, and that is the next thing it wants

`UIOverlay` binds ONE texture for the whole batch - the R8 SDF font atlas - and every quad's UV
indexes into it, with untextured quads pointing at the atlas's solid texel. That is what buys one
draw call and no branch in `ui_overlay.frag`, and it is also why there is no way to put an image on
screen today. So the key here is four rounded rectangles rather than a sprite, and the lives are
circles rather than hearts.

What it would take, roughly in order of how much it changes:

1. **A second sampler and a per-quad source flag.** Cheapest, and it keeps one draw call only while
   there is exactly one extra texture.
2. **A texture ARRAY, with the layer as a vertex attribute.** Still one draw call for any number of
   images, at the cost of every image having to share one size and format.
3. **Batch splitting on texture change** - what ImGui actually does. Unlimited images, arbitrary
   sizes, and the draw call count becomes a property of what the app asked for.

None of them is large. (2) is the one that fits this codebase's taste: a fixed atlas of game icons
uploaded once at Init, drawn with everything else in a single call, and no draw-order surprises.

---

## Sound

Built 2026-09-17. `USE_SOUND := 1` in the makefile, seven clips, `SoundSystem` (miniaudio).

| clip | plays when | gain |
|---|---|---|
| `explosion.wav` → `blast` | `blast_count` rises | 0.85 |
| `chop.wav` → `chop` | an enemy puts its shears in a hedge | 0.55 |
| `pickup.wav` → `pickup` | `items_taken` rises and health did not | 0.70 |
| `health.wav` → `health` | `items_taken` rises **and** health went up | 0.75 |
| `death.wav` → `death` | `deaths` rises - the player only | 1.00 |
| `door_opening.wav` → `door` | the exit swings open; both corridor doors move | 0.75-0.80 |
| `block_shift.wav` → `level_shift` | `BeginHallway` - the level boundary | 0.80 |

**`Maze` did not change, and that is the design.** Every event above is already visible from the
outside - the counters `Maze` keeps and the walker state `SyncView` reads - so the sound layer is a
DIFF against what it saw last tick (`BomberSoundWatch`), exactly like `drawn_field_version`. Sound
stays on the view side of the one-way rule, and `make rules` goes on testing a class that cannot
make a noise. An events struct out of `Maze::Tick` (which `apps/breakout` has) is the right move the
moment something wants a sound that leaves **no trace in the state** - see the shield hit below.

**`ResetSoundWatch` is not optional.** `NewGame` puts every counter back to 0, so a watch carried
across a level boundary would sit above the new board's counters and stay silent until they caught
up - on `blast_count` that is a level or two of soundless explosions. It is called from
`RebuildField`, which is the one place a board is ever laid out.

**One thread ever calls `Play`.** `SoundSystem` has no lock, and every call site is downstream of
`RunSimulationTick`. The panel's `sound` checkbox therefore sets a bool and plays nothing itself.

**`block_shift.wav` was the one judgement call** - it is the only clip whose name does not name an
event in this game. A destructible block breaking was the other reading; the level boundary won
because it happens ONCE, and four hedges going up together with a 1.08 s tail apiece would be mud
under an explosion that is already playing. One line to move if that is wrong.

**`bomber_state.sound`** reports `enabled`, `device` and `playing` - the live voice count, which is
the only way to confirm from outside the process that an event made a noise. With `sim_pause` and
`sim_step` it is an exact test: step the tick that should sound, then read it.

### What the game still wants and has no clip for

Roughly in order of how much it is missed:

1. **A bomb being placed, and its fuse.** The biggest gap by far - you press the key and nothing
   happens for two seconds. A placement thunk plus a fuse tick (looping, `SOUND_KEEP`, stopped when
   `f_bomb` clears) would carry the whole wait.
2. **The player being hurt but not killed.** Walking into an enemy costs a life in silence, which is
   the worst thing in the game to not hear. Note a hit absorbed by a SHIELD changes no counter at
   all, so that one is the case that would finally want an events struct out of `Maze`.
3. **An enemy dying.** It would be `death.wav` today, deliberately not played: an enemy can only die
   to a blast, so it always lands on the same tick as a 1.87 s explosion, up to four at once. It
   wants a short clip of its own, not this one.
4. **Footsteps.** It is a walking game and the walker is silent. Cheap: one clip on the tick a step
   starts, alternating two names so consecutive steps do not cut each other off.

Lower down: a shield expiring, the key being found (distinct from an ordinary pickup - it is the
thing that ends the level), and anything at all in the corridor other than the doors, which is
where the score tally is going to want a note.

---

## The player's rig

Skinned 2026-09-17. `character_armature`, 27 bones, MIXAMO names (`mixamorig:Hips` and the rest) -
which is a different rig from the enemy's four hand-named bones, and nothing in the code cares:
`Animation::LinkObjects` binds by string against whatever the skeleton has.

**It is built ONCE, in `Init`, by `BuildCharacter`** - the same rule the enemies and the turds keep,
and the reason this moved at all. It used to be made in `RebuildField` out of the `AssetManager`,
which is fine for a static mesh and is not for one that uploads a skin: `RebuildField` runs on the
PHYSICS thread. So the character is no longer destroyed and remade per field, `shield_worn` survives
with it, and neither pointer can be left dangling by a restart.

**It came out of the static-asset list at the same time.** `GetAssetsFromGLTF` still naming
`character` would not have failed - it would have loaded the skin a second time as a plain mesh, said
"Loading a skinned mesh as normal mesh", and kept an unused copy for the life of the process.

**The clip list is PROBED, not assumed.** `BuildCharacter` asks `GLTFLoader::GetAnimationNames`
which of the three it wants the file actually has and loads those. That earned itself on the day it
was written: the rig arrived with only an idle, then only a walk, then all three - because **Blender
will not export an action unless it is stashed on an NLA track**, and an action that is merely
present in the file is not in the export. A fixed list would have logged an error every launch for
each clip that had not made it out yet.

**The shield hangs off the skeleton's ROOT, not off a bone.** A bone would make the bubble bob and
lean with the body; the root keeps it centred on the tile, which is what it protects. A held item -
a weapon, a lamp - is the case that would want a bone instead, and that is a different attach.

**The facing table did not need touching.** `BOMBER_WALKER_YAW` was tuned against the old mesh and
the new rig shares its convention - measured with `object_get`'s `world_forward` in all four
directions, which is the honest instrument for this (see the note on the table itself). A model that
came in facing the other way would have shown up there and nowhere else.

### The three clips, and how each is driven

| clip | when | how |
|---|---|---|
| `Character_Idle` | 4.50 s | standing still - `TransitionToAnimation`, rate 1.0 |
| `Character_Walking` | 1.04 s | `step_ticks > 0` - `TransitionToAnimation`, **rate 1.56** |
| `Character_Death` | 2.62 s | `!f_alive` - `SwitchToAnimation`, rate 1.0, NOT looped |

**The walk's rate is solved, not dialled in.** The clip is an IN-PLACE walk - every translation
track in it is flat - so it says how the legs move and nothing about how far that carries you. The
board says that: one tile per `MAZE_STEP_TICKS`. `BuildCharacter` ties the two together by asserting
the one thing that makes a walk read as walking - **one cycle is two footfalls, and a footfall is a
tile** - so `rate = duration / (2 * MAZE_STEP_TICKS * timestep)`, which comes out at 1.56 here. At
rate 1.0 the character covers three tiles per cycle and visibly skates. Derived from the clip's own
duration, so re-exporting a longer cycle or changing the walk speed keeps the feet on the ground
with nothing to remember.

**The rate is per-OBJECT, not per-clip**, so it is set on every switch. Leaving it alone would play
the idle and the death at the walk's 1.56x, and a body that falls over at one and a half times speed
reads as a glitch rather than a death.

**Death is switched to, not blended into** - the same call and the same two reasons as the enemy's:
blending into a death softens the one moment that should read as sudden, and blending out of it
would be a corpse standing back up. `SwitchToAnimation` also rewinds, which this clip needs and the
looping two do not: a second death would otherwise open on the last frame of the first, already flat
on the floor.

`bomber_state.character` reports `clip` and `walk_rate`, which the enemies have had all along.

### What the rig still wants

- **A place-bomb clip**, if it is cheap. The blast already sells it, so this is the lowest-value one
  of the four and the reason it was left.

---

## Shipping

`mingw32-make.exe ship -j8` works as of 2026-09-17 and produces
`build/bomber_baked_nomcp_nonet_noimgui_release.exe` - **7.02 MB, one file, nothing beside it**.
Against the loose release that is 3.24 MB of exe plus about 8 MB of assets in several hundred files.

It refused before this only because the app had never declared **what** to bake. The machinery was
always wired: `make ship` runs `tools/assetpack` and links the blob. Two lines were missing.

- `ASSET_ROOTS := assets $(ROOT)/shared_assets` - THE SAME TWO ROOTS main.cpp declares, in the same
  order, and nothing checks that those two lists agree. Read them together when either changes.
- `main.cpp`'s roots are now inside `#ifndef ASSETS_BAKED`, as tetris's and ui's are. A baked build
  consults the table before the search path, so a declared root can only be reached by a name that
  is going to fail anyway - leaving it in would hide an incomplete bake behind a disk that happens
  to have the file.

**1.1 MB of the shared tree is excluded**, each one checked rather than guessed - nothing in `core/`
or this app names any of them outside a comment: `fonts/CascadiaMono.ttf`, `fonts/mono_sdf.png`,
`meshes/glyphs_unispace.glb`, and the four shared wavs (`bleep`, `click`, `floop`, `hax`) that belong
to breakout.

**THE FOUR WAVS ARE NAMED ONE AT A TIME AND THAT IS NOT VERBOSITY.** `--exclude` matches the ASSET
NAME, and an asset name is what it resolves to across ALL roots - so `--exclude "sound/*"` would
silently take this app's own seven with the shared four, and the failure would be a shipped build
that runs in silence.

Verified by copying the exe alone into an empty directory with no `assets/` and no `shared_assets/`
anywhere above it: 31 assets, 6.9 MB raw to 4.8 MB in the blob, every one served **from memory** -
`LoadFile` never touched the disk once - the character with its three clips, the seven sounds, and
the board all up.

---

## Decisions still open

### What "the next level" actually is

The door is on the edge and going through it leads to the next room. Two readings, and they are very
different amounts of work:

- **Re-roll the board.** The door closes behind you, the field regenerates on a new seed, score and
  upgrades carry over. `NewGame` already does all of this; the only new part is carrying state
  across. Cheap, and it is what almost every game in this genre actually does.
- **A literal second room.** Two boards alive at once, a camera that travels between them, walkers
  that exist in one of them. Much bigger: `Maze` is a single fixed 16x16 grid down to the array
  declarations, and every rule reads it directly.

Recommending the first. The fiction of walking through a door into a new room survives a re-roll
perfectly well as long as the camera does something during the transition.

### How the door opens - DONE, and it worked first time

Built 2026-09-15. `ApplicationBomber::BuildDoor` puts the archway on tile (8,0) - the middle of the
north border - with the leaf attached under it, and `Door_Opening` swings it. There is an "Open the
door" button on the panel and a `bomber_door` MCP tool; `bomber_state` reports a `door` block.

**No armature, no skin, no bones.** `LoadAnimation` builds one track per target NODE and never asks
whether that node is a joint, and `AddAnimation` links tracks by name against the object AND its
children. A rigid prop goes through exactly the path a character does, minus the skin. This is the
first thing in the project to use it.

What made it work, in the order it matters:

- **The clip goes on the ARCHWAY, not on the leaf.** `Scene::UpdateAnimations` walks
  `renderer->objects`, which holds what was handed to `Scene::AddObject`; a child is DRAWN through
  its parent but is not in that list, so `ApplyAnimation` would never run on the leaf. Same shape as
  a `Skeleton`: the skeleton is the animated object and the bones are what move.
- **The split is what makes a clip legal at all.** A track writes an ABSOLUTE local transform. On a
  single door object it would overwrite the placement this app gives it. With the leaf a child, the
  clip owns a transform nothing else touches.
- **Position keys buy the hinge.** The leaf's origin is not on its hinge edge, so the clip swings it
  97 degrees AND slides it by (-0.33, 0, -0.31) to keep the hinge still. Rotation alone would spin
  it about its middle. Measured against the .glb, every frame the engine sampled matched the file.
- **The leaf's object `name` must equal the node name**, because `LinkObjects` binds by string
  comparison. It logs how many it bound, and `BuildDoor` warns per unbound track.
- **Node TRS is discarded on load** (`engine_notes.md` §1), which cost nothing here only because the
  two nodes share an origin in the .glb. The parenting is rebuilt in the app.

**Scale keys are fine.** An earlier version of this note said they were fatal, on the strength of the
`debug->Fatal("TODO: Implement animation scaling")` in `ApplyIntervalOnto`. Nothing ever sets
`f_scale`, so that line is unreachable and scale channels are silently dropped - which is just as
well, since every clip in this file already has one and the enemies have been animating for days.

**Shutting animates too, as of the same day.** `Object::SetAnimationRate(-1)` runs the one clip
backwards, so there is no second clip to author and reversing mid-swing reverses from mid-swing.
That is `engine_notes.md` §16, now closed, and it is the mechanism anything else with an open and a
shut state should use.

**What the rules still do not know:** the door is scenery. `Maze` has no door tile, so walking into
it does nothing and the archway is a view-only special case in `RebuildField` (the cell gets no
brick). That special case is the thing that disappears when item 1 above is built.

### Enemies are not born in a box

Measured before the fix, over 200 seeds and 800 enemies: **15 spawned with no exit at all** and
another **14 spawned next to a hedge they could never cut**. Both are now 0, and it took two changes
rather than one, which is the part worth remembering:

- `PlaceEnemies` will not use a cell with nothing to do from it - no neighbour it can step into and
  no hedge it can cut. It asks exactly what `TickEnemies` will ask a tick later rather than
  approximating it with "is it surrounded".
- **That filter alone was not enough.** A walled-in enemy used to reverse when it found a dead end,
  and `back` is the opposite of `facing` - so it flipped between the same two directions for ever
  and never turned to look at the other axis. One sealed in by three walls and a hedge would stand
  facing the wall for the whole round with the hedge it could have cut beside it. It now turns to
  face something it can work on. That is a rule and not just a spawn check, because a blast can wall
  one in after the board has started.

The soak in `make rules` is what proves it: 2000 ticks with nobody touching the controls, and every
enemy on every board has moved by the end.

### Whatever else needs one animation

Same question, and now a proven answer on both sides. Anything with a shape to its motion - a door,
a lid, a chest - belongs in Blender, because the unskinned path costs nothing beyond an
`AddAnimation` call and a rate covers forwards, backwards and parked. Anything that is a straight
fade, a bob or a spin is a tween in the app: the coins turning and the pickups shrinking are both
arithmetic against the tick counter in `ApplicationBomber::TickPickupView`, and a .glb round trip to
change their speed would be worse than a constant.

### What lives on the walker, and what lives on the board

Settled 2026-09-16, and it is the shape the level transition needs. `MazeWalker` carries what is
true of a BODY and of whoever owns it - where it is, which way it faces, whether it is mid-step or
mid-swing, `health`, `shield_ticks`, `invuln_ticks`, `f_has_key`, `score`. `Maze` keeps the board and
the STATISTICS (`deaths`, `items_taken`, `blocks_destroyed`, `blocks_cut`, `enemies_killed`).

The walker is the thing that travels, so everything on it carries across a level boundary for
nothing. That is the whole reason for the split, and it fixed two live bugs on the way:

- **The exit was open to everyone.** `Maze::f_has_key` belonged to the game, so `IsPassable`
  answered the door for whoever asked - and `TickEnemies` asks, through `CanEnter`. Measured: **35
  of 60 boards** ended up with an enemy standing in the player's exit, blocking it and costing a
  life on contact. Now `IsPassable` answers for the CELL and calls a door shut for everybody, and
  `CanEnter(walker, ...)` asks the walker for a key. An enemy can never have one.
- **The player's body vanished the instant they died.** There were two death clocks one letter
  apart - `MazeWalker::death_ticks` for the corpse and `Maze::dead_ticks` for the respawn wait - and
  the player's death set only the second, while the view draws a walker while
  `f_alive || death_ticks > 0`. One countdown now; whoever does the killing says how long it runs
  (`MAZE_DEATH_TICKS` for an enemy, `MAZE_RESPAWN_TICKS` for the player).

**The two reset sites now read differently on purpose.** `player = MazeWalker()` appears in
`NewGame` and in the respawn. The first is the blunt reset and keeps nothing - a new board is a new
lock. The second names what a death does NOT cost: the key and the score. **Anything earned that
moves onto `MazeWalker` later needs a line in that second site and a line in the test**, or it will
be wiped silently on every death with nothing to say so.

### A table sized by an enum count will not tell you it is short

Adding `MAZE_TILE_DOOR` grew `MAZE_TILE_COUNT`, and two `static const char GLYPH[MAZE_TILE_COUNT]`
tables - one in `MapJson`, one in the test harness - quietly value-initialised their new last entry
to `'\0'`. The door then rendered as a NUL BYTE INSIDE A JSON STRING and as a hole in the printed
board, and nothing anywhere said so.

Every table in this app indexed by a `MazeTile` / `MazeItem` / `MazeDecor` is the same shape and the
same risk. They are all in one place near the top of `ApplicationBomber.cpp` for that reason, and
the two glyph tables now spell the enum name against each row so the next addition is obvious.
There are six of them: `BOMBER_TILE_ASSET`, `BOMBER_BLOCK_ASSET`, `BOMBER_DECOR_ASSET`,
`BOMBER_ITEM_ASSET`, `BOMBER_ITEM_SPINS`, and `GLYPH`/`ITEM_GLYPH` in `MapJson`.

### Testing affordances that now exist

Worth knowing before writing another test by hand:

- `bomber_give` lays a pickup on the player's tile and lets `TickItems` collect it, so the score
  ladder and the worn shield can be checked in a second rather than by playing a board until one
  turns up. It does NOT make one appear on screen - the field's pickup objects are built per cell
  when the board is laid out.
- `bomber_state` reports `field.items_shrinking`, which is the only evidence the shrink ran: it is
  over in 18 ticks and a screenshot has to land inside that window.
- `bomber_door` plus `door.rate` and `door.leaf_moved` - the leaf's own local transform, which
  nothing but the clip ever writes.
- `object_list` is capped at 200 entries and a laid-out board is four hundred objects, so an
  unfiltered call quietly returns only the front of it. Use `name_filter`.
- `scratchpad/bomberdrive.py` PLAYS the board: it reads the map out of `bomber_state`, paths over it
  with soft blocks costing eight (a hedge is a door that takes one bomb), respects the bridges'
  grain at both ends of a step exactly as `Maze::CanEnter` does, and digs where the path says to.
  The greedy version it replaced could not get out of the spawn corner. Anything that needs the
  player to GET somewhere - the level transition, enemies that hunt, the HUD - wants this.
