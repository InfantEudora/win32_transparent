# Apple Plan

Apples in a tree, for an archery game to aim at. Shoot just ABOVE an apple and its stem snaps: the
apple drops, bounces and rolls away. Shoot the apple ITSELF and it goes off with the arrow in it.
The low-hanging ones she can simply pick. Talked through 2026-09-30. Steps 1-3 BUILT 2026-10-01,
on the real models rather than a blockout; see "As built" below.

As with every mechanic in this game, it is **blocked out first**: spheres for apples, a line for
each stem, a stand-in clip for the pick. It has to play before any art goes in. The `apple_tree`
model is already in the export and in progress, and it is not needed for any step before the last.

---

## Decided

- **The hanging apple is declared in the rules**, like the tree's arms and the spring plants
  (`plant_mechanics_plan.md`, "The split"). The rules decide whether an arrow hit the apple, the
  stem, or nothing. That is the gameplay, and it has to replay and be provable in `make rules`.
- **Once loose, an apple belongs to rp3d**, like every other thing that gets knocked around
  (crates, debris, knocked targets; Stage.h:18-40). Falling, bouncing, rolling and flying off
  with an arrow in it are exactly what rp3d is for, and nothing in the rules can roll.
- **An arrow in an apple rides it**, through the existing `StickArrowToProp` path
  (ApplicationArcher.cpp:11816). It already makes arrows ride a kicked crate.
- **Picking is the action button** (E / pad Y). Today that button only takes the rope, so
  picking is a second meaning for the same press, chosen by what is in reach.

---

## 1. The apple on its stem (rules)

**Declaration.** A small struct in `Stage.h`, declared per apple in `Stage.cpp` like the trees:

```
struct StageApple{
    float x, y;          //the apple's centre while hanging
    float stem = 0.25f;  //stem length: the twig it hangs from is at y + radius + stem
    float radius = 0.18f;
    int   tree = -1;     //the apple tree it belongs to, for drawing only, or -1
};
```

Declared per apple rather than generated from a tree, so the blockout can hang one exactly where
a test shot wants it, and a later layout can hang apples from anything: an apple tree, a bigtree
arm, a vine. A tree then only decides where its apples look like they grow.

**State**, in the rules, per apple: `HANGING`, `LOOSE` (handed to the app), `PICKED`. Restored
on restart like the rest of the level, so a replay starts with every apple back on its stem.

**The hit test.** Each tick, `FlyArrow` already sweeps each arrow from `prev_pos` to `pos`
against the blocks (`SegmentHitsBlock`, Stage.cpp:4003). The apple adds two tests to the same
sweep, taking whichever hit comes first along the segment:

- **Apple:** the segment against a circle (x, y, radius).
- **Stem:** the segment against the stem, from the apple's top to the twig, widened to a small
  capsule (`APPLE_STEM_HIT_R`, say 0.06). This is what "shot above the apple" means in numbers.
  It needs to be generous enough to be hittable at bow range, and the width is a tuning value.

Arrows already pass through tree arms, trunks and foliage (platforms are skipped, Stage.cpp:4014),
so nothing else in a tree gets in the way.

**What each hit does:**

- **Stem cut.** The apple goes `LOOSE` with a small push in the arrow's direction
  (`APPLE_CUT_PUSH`, a fraction of the arrow's velocity), so it lands with some roll rather
  than dropping dead. The arrow **flies on**: it cut a stem, it did not stop. Event
  `StageEvents::AppleCut`.
- **Apple hit.** The apple goes `LOOSE` carrying the arrow's momentum
  (`APPLE_HIT_TRANSFER`, like props' `arrow_speed_transfer`). The arrow is **stuck** in it at the
  hit point. Event `StageEvents::AppleHit`.

**Decided default, open to change:** a clean stem cut is the good shot, because the apple
survives to be collected. A hit on the apple is the clumsy one: the apple goes flying,
probably out of reach. See the open questions for what apples are worth.

---

## 2. Loose: falling, rolling, flying off (app, rp3d)

**Hand-off.** On `AppleCut` or `AppleHit`, the app creates the apple's rp3d body at the hanging
position with the event's velocity, the same way `SpawnDebris` makes a body on a rules event.

**Body.** A sphere, which archer has never used: every body today goes through
`MakePlanarBody` with a box (ApplicationArcher.cpp:1338). `Physics::AddSphereCollider` already
exists in core (core/physics/Physics.h:74), so this is a sphere variant of `MakePlanarBody`:
locked to the XY plane, **but free to turn about Z**, which is what lets it roll rather than
slide. Tuning to find in the blockout: bounciness (a little bounce off the ground), friction,
and angular damping so it rolls to a stop rather than forever.

**What it collides with.** The level's static block bodies and props, like any prop. One-way
platforms have no body (ApplicationArcher.cpp:1457), so an apple falls through tree arms. That
is right for an apple dropping out of its own tree.

**The arrow in it.** On `AppleHit`, `StickArrowToProp` stores the arrow's offset in the apple's
frame, and the per-tick sync carries it along, so the arrow tumbles with the apple.

**Feeding back to the rules.** A loose apple on the ground has to be pickable (section 3), so the
rules must know where it is. The precedent is `RefreshObstacles`, which hands the rules props as
boxes each tick (ApplicationArcher.cpp:11228). Loose apples go the same way, as positions, at the
tick boundary. rp3d replays bit-exact (docs/replay_determinism_plan.md), so this stays
deterministic.

**How long a loose apple lives:** see the open questions. The blockout can simply keep them.

---

## 3. Picking (rules + animation)

**Which apples.** A hanging apple she can reach standing (its bottom within `APPLE_PICK_REACH`
of her feet), and, if wanted, a loose apple lying at her feet. The reach is measured off the
pick clip's hand position once that exists, not guessed.

**The press.** On `f_action_pressed`, the rules look for a rope point in reach first
(`FindRopePoint`, Stage.cpp:2633), then for an apple in reach. On an apple, she stops, faces it
and runs a pick window of `APPLE_PICK_TICKS`. The apple becomes `PICKED` at a set tick inside
that window (the hand closing), not at the press. Event `StageEvents::ApplePicked`.

This is the kick pattern: the rules own the timing (`KICK_TICKS`), the clip is played to fit
the window (Puppet.cpp:389-409), and the app warns when a re-export moves the clip's length.

**Animation.** A new `ARCHER_CLIPS` row and an `ACTION_PICK` branch in `Puppet::Choose`.
**The pick clip is not in archer.glb yet.** The export's only grab-like clip is `Close_LeftHand`.
The blockout runs on a stand-in (`Standing_DrawArrow`'s reach, or `Stand_ToKneel` for a low
one) until the real clip is exported. It needs its name, and whether it covers high and low
reaches or only one.

---

## 4. Sounds (cues)

These are signals in `archer.json`, like everything else (`cue_plan.md`):

- `apple_cut` for the stem snap
- `apple_hit` for the thunk of an arrow going in
- `apple_bounce` on landing and bounces, by impact speed
- `apple_roll` as a loop scaled by roll speed, if it earns its place
- `apple_pick`

---

## 5. Testing

**`make rules`**, a `TestApples` in stage_test:
- an arrow through the stem cuts it, and flies on;
- an arrow through the apple hits it, and is stuck;
- an arrow passing just clear of both touches neither;
- the stem-versus-apple boundary is where the constants say;
- a hanging apple in reach is picked at the right tick of the window, and one out of reach is not;
- the rope still wins the press over an apple;
- restart puts every apple back.

**In the app**, over MCP with `sim_pause`/`sim_step`:
- a stem cut drops the apple, which bounces and comes to rest rolling;
- a hit carries the apple off with the arrow in it;
- a picked apple is gone;
- screenshots at each.

**archer_test** is not expected to change unless apples go somewhere the recording passes.
The test bed is in the blockout half, which the recording never reaches.

---

## Order

1. **Rules and blockout.** `StageApple`, the hit test, the three events, and `TestApples`.
   Draw apples as spheres and stems as lines. A small test bed in the blockout half: a stub
   trunk with apples at a few heights, one of them low enough to pick. New blocks go LAST
   (dressing is seeded by block index).
2. **Loose apples.** The sphere body, the hand-off, the arrow riding it, and bounce/roll tuning
   in the app.
3. **Picking**, on a stand-in clip, including loose apples on the ground if wanted.
4. **Cues.**
5. **Art.** `apple_tree` around the test bed's apples, an apple mesh (not in the export yet),
   and the real pick clip.
6. **Use in the level**, once it plays well blocked out.

---

## As built (2026-10-01)

Steps 1-3, plus the part of step 5 the export already allowed: `apple` and the tree (exported as
`tree_1`; the old `apple_tree` node is gone) are drawn instead of spheres and boxes. The code is
`StageApple`/APPLES in Stage.h, `ArrowThroughApples`/`TickPick`/`BuildOrchard` in Stage.cpp, the
APPLES block and `BuildApples`/`HandleApples` in ApplicationArcher, and `ACTION_PICK` in the Puppet.

### Where: the terrain bay

**In the main level's terrain bay**, between the waterfall and the hill (x -32 .. -16), not in the
blockout half. The blockout half is a grey slab and reads as nothing, let alone a forest; the bay is
dressed (grass, the waterfall, the stream, the rock walls). And archer_test never goes there:
measured on the recording, her x stays in -6 .. 28, and her arrows fly right. A scene of its own
was the other choice, but the extra levels get no terrain or foliage, so it would have been grey
boxes again.

**Three trees, straddling her line** (`Stage::apple_trees`, looks only): the big one (scale 1.5)
with its trunk 0.9 behind her line and its canopy reaching out over it; a small one (1.1) with
its trunk 1.2 IN FRONT of her line, so she passes behind it; and one past the waterfall (1.25),
behind. From the game camera that reads as walking through a grove: something behind her,
something in front, canopy over her. No blocks were added, so nothing the dressing is seeded by
moves.

**Fourteen apples hang from the canopies' undersides.** Each twig is 0.12 inside the lowest
leaves above its (x, z). That was read off `tree_1` placed exactly as the app places it, because
laid out by eye, four of the first thirteen stalks went up into the air beside a canopy.

### The depth rule

**The hit test is 3D geometry.** The apple is a sphere at (x, y, z) and the stem is a vertical
capsule of APPLE_STEM_HIT_R above it, both tested against the arrow's real segment. So an arrow at
z 0 hits an apple only where it really passes through it, and whether an apple is a shot follows
from where it hangs. There is no flag:

| hangs | what it is | in the orchard |
|---|---|---|
| on her line, \|z\| <= APPLE_ON_LINE_Z (0.04) | **shootable**: stem and apple both cross her plane | 5, at 2.1 .. 2.8 up |
| off it, within APPLE_PICK_REACH_Z (0.65) and APPLE_PICK_REACH_UP (2.25) | **picked, never shot** | 4, at her chest, shoulder and overhead |
| deeper, or higher | **looks** | 5 |

**Nothing hangs between the first two rows** (stage_test checks this). An apple 0.04 to 0.25 off
the line would be hittable over only part of its outline. Measured: one 0.12 off the line is cut
by it in a circle 0.147 across, not 0.19. An arrow through the rest of its outline would pass behind
or in front of it, and the z-buffer shows exactly that. The orchard simply hangs none there.

**Nothing hangs in her way.** An apple on her line hangs clear of her head (its bottom above 1.8),
and one lower hangs at least 0.4 to her side; stage_test checks both. The small tree's canopy is
too low for an apple on her line to clear her, so it is the picking tree.

The rule was checked from the game camera, and with the camera pulled in to 9-12:
- a stem shot meets the stalk, and the arrow is past the canopy a tick later;
- a body shot has the shaft visibly in the apple's skin all the way to where it lands;
- an arrow at z 0 never visibly passes a hand's width from an apple it counts.

### The numbers

| | value | why |
|---|---|---|
| APPLE_RADIUS | 0.19 | `apple` is 0.104 across its body, x model_scale 1.82; the app re-measures it and warns past 0.03 |
| APPLE_STEM_LEN | 0.30 | apple top to twig; the model's own stem is the first 0.10, a drawn stalk the rest |
| APPLE_STEM_HIT_R | 0.06 | the stalk drawn is 0.025 wide |
| APPLE_CUT_PUSH / _HIT_TRANSFER | 0.04 / 0.20 | of the arrow's velocity: a cut apple leaves at about 1.8 u/s, a hit one at about 9 |
| pick reach | 0.75 across, 0.65 deep, 2.25 up | from her centre line at her feet |
| low pick | centre under 0.80 up | an apple at her feet |
| pick windows | high 48 ticks, close at 19; low 40, close at 36 | fitted to the stand-ins, below |
| loose body | mass 0.2, bounce 0.3, friction 0.8, angular damping 2.5 | at 0.35 a roll was still going at 0.7 u/s three seconds on |

### What it does, measured in the game

These are paused and stepped, release build, port 8769. She stands at x -25.5 under the big tree
and shoots its apple at (-23.83, 2.45).
- **Aimed 30-40 degrees, a hit.** The apple goes off at (7.3, 4.9) with the arrow in it, flies
  through the canopy, lands about 6.5 units on by the hill, and rolls back from it. The arrow
  stays in it until it ages out (ARROW_STUCK_TICKS, 10 s).
- **Aimed 42-44 degrees, a cut.** The arrow flies on and the apple drops at (1.3, 1.1). It lands
  in 37 ticks, bounces to 0.47, then rolls: 0.73 u/s on landing, 0.15 u/s three seconds later,
  then rests.
- **A pick overhead.** She stands under the apple at (-23.03, 2.12, -0.45), which glows gold, and
  "E pick" shows at the bottom of the screen. On the press she plants and plays the stand-in reach.
  The apple goes on tick 19, the count reads 1, and she is back on Idle at 48.
- **A pick off the ground.** She stands by the cut apple: a low pick, on Stand_ToKneel, taken at
  the close.

### Picking

Picking is the kick's arrangement. The press, the window and the hand's close are all the rules'
(`pick_ticks`, PickTicks/PickClose):
- **The rope keeps the press.** FindRopePoint runs first; the apple is the press's second meaning.
- **She stops and turns to the apple**, and the bow is put by: the reaching hand is the string hand.
- **A jump mid-reach ends the pick**, and the apple stays where it is.
- **At the close, the apple is taken if it is still there**, within APPLE_PICK_SLACK of her
  reach, since a lying one may have rolled.

Hanging or lying, she reaches for whichever is nearest. Loose apples are handed to the rules
before each tick (`SetLooseApple`), the way the obstacles are.

**The clips are stand-ins, swappable in two lines** (PUPPET_PICK_CLIP_HIGH/LOW in Puppet.h). Both
come back `f_placeholder`, so the panel lists the pick as wanted:
- **Standing_DrawArrow for a reach.** Her right hand rises over her shoulder to 1.57, 25 ticks
  into its 1.07 s. That is up but backwards, and short of an overhead apple by about half a unit:
  the apple is taken out of the air above her hand.
- **Stand_ToKneel for a low pick.** Its hands never get below 0.69, since it holds a rifle, so
  she crouches over the apple rather than reaching it.

**The prompt and the highlight.** The apple a press would take is drawn in a lit copy of its
material, a warm gold glow, for as long as she can start a pick. A small "E pick" card shows at
the bottom middle of the screen. Both go while she picks.

### What an apple is worth: a placeholder

What an apple is worth is still open (see the open questions). For now there is a count, bottom
left on the HUD (`Stage::apples_picked`, emptied by a restart), and an `apples` field in
archer_state with each apple's state and position. A hit plays the arrow's thunk (`arrow_hit`),
until apples have sounds of their own.

### The replay hash, and archer_test

**The apples go into the hash only once one has been touched**, meaning an arrow freed it or a
pick reached for it. Untouched, the hash is exactly what it was before there were any (stage_test
checks this). The trees and the hanging apples are visual-only Objects, and a loose apple's body
exists only once it is freed. So archer_test, which never meets them, still reads **`state same`
(1735 ticks) and `ALL SAME`** on the release build. It still proves that adding the apples moved
nothing else; no re-baseline was needed.

### Tests

`TestApples` in stage_test covers section 5's list:
- a stem shot cuts and the arrow flies on;
- a body shot hits, and the arrow is stuck in the skin;
- shots just over the twig, just under the apple, and a hand's breadth in front of it touch
  nothing;
- the stem starts exactly at the apple's top (0.02 either side);
- off the plane, an apple is hit only where the arrow really goes through it;
- a lob down onto the stem cuts it rather than hitting the apple;
- a pick is taken at the close tick, not at the press;
- out of reach across or above, nothing happens;
- a lying apple is a low pick, and she turns to it;
- the rope still wins the press;
- a pick plants her, and a jump mid-reach ends it;
- a restart puts every apple back;
- the orchard's layout rules above (the depth rows, nothing in her way, every apple on a tree).

### About the export

- **The tree is `tree_1`.** The old `apple_tree` node is gone, so the code names `tree_1`. If it is
  renamed back, the change is one line in ApplicationArcher.cpp (APPLE_TREE_NODE).
- **Both models are clean.** Neither carries a node rotation or scale. `apple`'s origin is its
  body's centre; `tree_1`'s is the trunk's foot, with the roots 0.13 below it, which is fine on the
  ground.
- **The apple measures 0.211 across at her scale** with its leaf, 0.188 for the body; the rules
  use 0.19.
- **`bigtree_segment.001` is in the export beside `bigtree_segment`**, and the game uses neither
  name for it. It looks like a duplicate left over in Blender. The startup log lists it among the
  unused meshes.

### Open points for the user

- **What an apple is worth.** The count is a placeholder.
- **The pick clip:** its name and length, and whether it covers high and low reaches. Until it
  exists, the reach is backwards and short, and the low pick holds a rifle.
- **Rolling down a slope.** There is no ramp with a body in the bay, so apples roll on the
  flat, or off the hill's edge when one is cut over it. The ramps (StageRamp) have no rp3d body at
  all: an apple, and a crate, would fall through one.
- **She passes through an apple she jumps into.** Hanging apples do not collide with her, and on
  her line they hang above her head, but a jump under the big tree goes up through one.
- **At the game's camera distance the apples read as red dots and the stalks hardly at all**, so a
  stem shot is aimed by the apple's position rather than by the stalk. A closer camera in
  the orchard, or a thicker stalk, would help; not changed.
- **Loose apples stay** until picked or a restart, with no cap. One that falls off the world is
  not reaped (none can, in the bay).

---

## Open questions

- **What is an apple worth?** A count on the HUD? Archery points (a stem cut scoring like a
  bullseye)? Something for her vitals, like the grape idea in `vine_plan.md`:511, which calms
  her? Or a thing she carries or throws? This decides whether a hit apple is lost, or just less
  good.
- **Do loose apples stay?** For good, until restart, or rolling off the level? A cap on how
  many are loose at once, like arrows' ring buffer?
- **Pick from the ground too,** or only off the tree?
- **Anything else that frees an apple:** a kick against the trunk shaking the low ones down, or
  landing hard on an arm?
- **Can a rolling apple do something?** Roll into a pressure plate or down a ramp, or be food
  for something. That's for when a level wants it.
- **The pick clip:** its name, its length, and high or low.

---

## Status

| # | Piece | State |
|---|---|---|
| 1 | Rules + blockout | **built** 2026-10-01: the 3D hit test, the three events, TestApples; on the real models, in the terrain bay |
| 2 | Loose apples (rp3d) | **built**: sphere bodies, the arrow riding a hit one, bounce and roll tuned |
| 3 | Picking | **built** on stand-in clips (Standing_DrawArrow high, Stand_ToKneel low), with the glow and the "E pick" prompt; waiting on the pick clip |
| 4 | Cues | a hit plays `arrow_hit`; the apple's own five not yet |
| 5 | Art | `tree_1` and `apple` in the export and drawn; the pick clip not yet |
| 6 | Level use | the terrain bay's three trees are the test bed; the worth of an apple is still open |
