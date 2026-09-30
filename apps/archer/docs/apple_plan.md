# Apple Plan

Apples in a tree, for an archery game to aim at. Shoot just ABOVE an apple and its stem snaps: the
apple drops, bounces and rolls away. Shoot the apple ITSELF and it goes off with the arrow in it.
The low-hanging ones she can simply pick. Talked through 2026-09-30; nothing here is built yet.

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
| 1 | Rules + blockout | planned |
| 2 | Loose apples (rp3d) | planned |
| 3 | Picking | planned, waiting on the pick clip for the real animation |
| 4 | Cues | planned |
| 5 | Art | `apple_tree` in the export, in progress; apple mesh and pick clip not yet |
| 6 | Level use | planned |
