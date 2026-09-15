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
   - **Still to do: walking through it ends the level.** The agreed shape is a re-roll - the field
     regenerates on a new seed and the score and any upgrades carry over. `NewGame` already does all
     of it; what is new is carrying state across, and a camera that does something during the
     transition so the fiction of walking into a new room survives.
2. ~~**Score: coin and diamond.**~~ DONE. Coin 10, diamond 50, crystal 250 - 5x steps, so a
   crystal is the thing that happened this round rather than a few more coins. `MazeItemScore` is
   the one place that says so; a treasure added later needs a line there and nothing else.
3. **Enemies that hunt.** They have no awareness of the player at all today. The intent is not a
   perfect chase - a short sight line down a clear row, or a walk-toward-the-player bias inside a
   radius, is enough to make the board feel dangerous. Now that the walk clip exists it reads.
4. **Power-ups: more bombs, longer blast.** `MAZE_BLAST_RANGE` and the one-bomb limit are both
   constants. Making them per-player state is the change; the bomb list becoming a list is the only
   part with any weight to it.
5. **HUD.** Health, keys, score. Everything is in the ImGui debug panel today, which is not a HUD.
   This is the first thing in bomber to touch `UIOverlay` / `TextMesh` / `Sprite`.
6. **Player animation.** The character is rigged but slides. Walk and idle at minimum; place-bomb
   and death if they are cheap.
7. **Sound.** `USE_SOUND` is off and there is no wav. A bomb that makes no noise is the most
   conspicuous gap in how the game feels. Fuse, blast, pickup, chop, hit.
8. **Lives, game over, next level.** A run rather than a board: keep score across levels, re-roll the
   seed, maybe raise enemy count or lower the fuse.
9. **Second plant decor.** `plant` in the GLB is a different mesh from the `grass_plant` already in
   use. One line in the decor table. (`lilly` went in the same way, on water.) THE LAST UNUSED
   ASSET.

### Assets in the GLB with nothing using them yet

`plant`, and that is all - everything else in the file is in the game as of 2026-09-15.

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

### A table sized by an enum count will not tell you it is short

Adding `MAZE_TILE_DOOR` grew `MAZE_TILE_COUNT`, and two `static const char GLYPH[MAZE_TILE_COUNT]`
tables - one in `MapJson`, one in the test harness - quietly value-initialised their new last entry
to `' '`. The door then rendered as a NUL BYTE INSIDE A JSON STRING and as a hole in the printed
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
