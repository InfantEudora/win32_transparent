# Web Plan

A big spider web strung in a wooden frame, about twice her height. It blocks the way until she
shoots her way through. The big spider of `creature_plan.md` sits in it. Talked through
2026-10-01; steps 1-3 are built as a blockout the same day - see "Steps 1-3, as built". The reference render (2026-10-01) shows the look: two
posts and a cross-beam with iron brackets, vines up the posts, and a web of spokes and rings
filling the frame, with the spider in the middle.

**Blocked out first, in an area of its own**, to see what it looks like and how it breaks
before it goes anywhere in the level.

---

## Decided

- **The web is a spring net in the rules, modelled on the rope bridge,** not a mesh of rp3d rope
  links. The bridge (`StageBridge`, Stage.h:586-700; `StepBridge`, Stage.cpp:1038) is already
  a deterministic chain of points with springs that pull but never push, stepped in substeps,
  with planks that break. A web is the same thing in two dimensions: nodes, threads between
  them, threads that snap.
  - **Why not rp3d's rope:** the rope (`BuildRope`, ApplicationArcher.cpp:10411) is a single
    chain of box links with ball joints, and its skin handles exactly one chain (`RopeMesh`).
    Arrows don't touch rope links at all today (ResolveArrowsAgainstProps skips them). A web
    would be dozens of bodies and joints, outside the rules, so `make rules` couldn't prove it
    breaks. The rope's cut (`CutRopeJoint`, .cpp:10915) is a test tool, not a rule.
  - **Why the rules:** whether she can get through is gameplay. It has to replay, and it has to
    be testable without the engine.
- **The frame is solid scenery**: two posts and a beam as blocks or props. The web hangs from it.

---

## 1. The net (rules)

**Shape.** Like the reference: **spokes** from the frame to a hub, and **rings** spiralling
between the spokes. Nodes are where they cross, plus anchor nodes on the frame that never move.
A declaration gives the frame's opening, the number of spokes and rings, and the hub's position.
The rules build the nodes and threads from that, the way `BuildTrees` builds arms from a tree.

```
struct StageWeb{
    float x, y, w, h;      //the frame's opening, in the level plane
    int spokes = 12;
    int rings = 6;
    float hub_x, hub_y;    //the hub, often a little off-centre
};
```

**Simulation.** The bridge's step, in 2D (mostly; see below). Threads are springs that pull and
never push, with damping. The net sags a little and settles, it shivers when an arrow hits it,
and when threads snap it opens and the loose ends swing.

**Depth:** the net is in the level plane, so a hit pushes it a little toward the back. A small z
offset per node is enough for the shiver to read. It can stay 2D if that looks fine.

---

## 2. Arrows against the web

The rules already sweep each arrow from `prev_pos` to `pos` (`FlyArrow`, Stage.cpp:3917). The web
adds a segment-against-thread test for the threads in the frame's box.

**What a hit does. Decided default, open to change:**
- the thread or threads the arrow crosses **snap**;
- the arrow **flies on**, slowed by a set fraction per thread, so a shot through the dense middle
  loses more than one through the open edge;
- if it ends up too slow, it is **caught**: stuck in the web where it stopped, riding the net
  as it moves.
- A snapped thread puts a jolt into its neighbours, so the hole shudders.

Events: `WebThreadCut`, and `WebBreached` (below).

---

## 3. Blocking her, and the breach

While the web holds, it is a **wall** for her: the rules treat the frame's opening as a block.
**It is breached** once the threads crossing her body's box at the walk line are cut, so there's a
hole she fits through (her collider against the intact threads). Then the wall goes, and she walks
or jumps through the gap.

A web half shot away should look it: the hole is where the arrows went. So she can choose to
open it at the bottom to walk through, or higher up to jump through.

**`make rules` (TestWeb):**
- an arrow cuts exactly the threads it crosses;
- an intact web stops her;
- a breach at walk height lets her through;
- a hole too high or too small does not;
- everything is deterministic across a replay.

---

## 4. Drawing (app)

**Blockout:** threads as lines (the debug line mesh, like the wind view), nodes as dots, and the
frame as boxes.

**Real:** each thread as a thin tube along a Spline through its nodes, bent with `SplineDeform`
like the vines and the rope (core/SplineDeform.h). Or, since there are many short threads, one
mesh rebuilt from all the threads whenever the net moves. The thread count decides which;
measure it.

**The frame:** posts, beam and brackets from the export, with vines up the posts (the vine system
already does this, `DeclareVines`). The frame model isn't in the export yet; the reference is
a render.

---

## 5. Where: the blockout area

**An area of its own**, as the user asked, to judge looks and breaking before placing one in the
level. The rope scene (Stage.cpp:1707, the "rope" level) is the natural home: it's already the
test scene for rope-like things, and has room. Or add a new small scene the same way. One web,
the frame, open ground either side, and a spot to shoot from at a couple of distances and
heights.

---

## 6. Sounds (cues)

- `web_hit`, a dull thrum;
- `web_snap`, per thread, quieter for later ones;
- `web_breach`, a tearing as the hole opens.

The big spider reacts to a hit on its web; see `creature_plan.md`.

---

## Order

1. **Rules net and blockout drawing** in the test area: build, sag and settle; the debug-line
   look.
2. **Arrows:** cut, slow, catch.
3. **Wall and breach,** and `TestWeb`.
4. **Real drawing:** threads as tubes, and the frame from the export once modelled.
5. **The big spider in it** (creature_plan section 5).
6. **Placement in the level.**

---

## Open questions

- **Does the web catch arrows,** or only slow them? Do caught arrows come back out?
- **Does touching the web do anything to her:** slow her down, stick her briefly, or wake the
  spider?
- **Other ways through:** fire (if there are fire arrows later), or a blade (the knife is
  mapped but unbuilt)?
- **Small decorative webs** in cave corners: view-only, no rules. Cheap atmosphere.
- **Thread count against cost** for the drawing, measured once the blockout exists.

---

## Steps 1-3, as built (2026-10-01)

**Where: a scene of its own, `Web`** (`STAGE_LEVEL_WEB`, `Stage::BuildWebLevel`; `scene_set Web`),
not the rope level. The rope level's whole floor is the run-up its pit tests sprint along (a web
there failed `TestRopePits`). A floor from x -14 to 20 with a 48-high wall at each end, so an
arrow stays in. The web stands at x 7 .. 10.6, with 8 units of floor behind it. She starts at x -2,
9 units off; there is the far end (-12, 19 off), the floor in front of it, and a one-way platform at
2.4 (x 2 .. 4) to shoot from higher up. A `WEB` sign names it.

**The frame.** The opening is twice her height square: 3.6 x 3.6 (`4 * ARCHER_HALF_H`). The BEAM is
a solid block over it, wider than the posts. Her apex is 3.2, so she can neither get onto it nor
over it. **Deviation: the posts are drawn, not built.** The web hangs in her plane, so two solid
posts would be a wall she could never pass, cut threads or not. They are timber boxes in
`BuildBlocks`.

**The net** (`StageWeb`, Stage.h; `AddWeb`, `StepWeb`, `TickWebs` in Stage.cpp):
- 12 spokes and a 6-turn spiral from a hub 0.15 right and 0.25 up of the middle: 85 nodes (12 of
  them anchors on the opening's edge) and 155 threads.
- The spokes are laid half a step off the axes, so a level shot crosses them rather than running
  along one.
- The bridge's step over a net: `WEB_SUBSTEPS` 12, node mass 0.02, thread stiffness 300 over its
  length, thread damping 0.6, air 3/s.
- Threads are hung at `WEB_PRETENSION` 0.95 of their length and settled for 300 ticks at build. It
  starts still and taut, sagging 0.02 at the hub. One hub segment ends up 3% loose, from the
  spiral's pull; the test allows 5%.
- The opening's bottom is the ground (`WEB_FLOOR_FRICTION`): cut pieces fall to it and lie there.
  Before that, a hub shot free fell for ever.

**Arrows** (`ArrowThroughWebs`, called from `TickArrows` up to whatever block the flight meets that
tick):
- Every whole thread the segment crosses snaps, nearest first, and takes `WEB_SLOW_PER_THREAD`
  (12%) of the speed.
- An arrow left slower than `WEB_CATCH_SPEED` (8) is caught: stuck where it met that thread,
  riding the nearer moving node. A caught arrow weighs `WEB_ARROW_MASS` on the node, so it droops.
- A snap kicks both ends along the flight (`WEB_SNAP_KICK` 2.5). The springs carry the shudder to
  the neighbours.
- A full draw (46) is caught after 14 threads: through the dense middle, yes; low through the edge,
  no. A weak tap is caught after 9.
- Arrows pass through the wall's slices (`StageBlock::web`, skipped by `SegmentHitsBlock`).
- The forecast (`PredictArrowImpact`, for the swoosh) does not see webs.

**Wall and breach** (`TickWebWalls`, after the arrows):
- The opening is 12 invisible SOLID slices of 0.3.
- A band of her height plus `WEB_PASS_MARGIN` (7 slices, 2.1), within a jump of the floor, opens
  when no HELD thread crosses it above `WEB_STEP_OVER` (0.35): she steps over threads that low.
- **Deviation, both rules:**
  - The plan said "the threads crossing her body's box are cut". Taken literally, a hole never
    opens: no shot reaches the last hand-widths over the floor, where the spokes end, and every
    strand left hanging would still block.
  - HELD means part of the net that spans the frame: the frame's 2-edge-connected part, with every
    anchor counted as one node (`WebHeldThreads`). A piece hanging from one thread, or from
    nothing, does not stop her.
- A band that opens takes its slices with it and leaves any below as a step, so a high hole is
  jumped into. Slices never come back. `WebBreach` is reported per band; `f_first` is WebBreached.
- What makes a low breach reachable in practice: a web cut across its middle springs back, and its
  lower part crumples onto the bottom anchors, flat on the floor, below `WEB_STEP_OVER`.

**Events:**
- `web_cuts` (`WebThreadCut`: web, thread, arrow, point);
- `web_hits` (per arrow: threads snapped, caught);
- `web_breaches`;
- `web_slices_opened` (the app switches those colliders off in `BreakBlocks`).

**State hash:** each web's nodes, cut flags, caught arrows and breach, only where a level has a
web. The main level's hash is unchanged, and so is `archer_test` (`state same`, `same`).

**Drawing** (`BuildWebView` / `UpdateWebView`): one line object, shared into every scene like her
arrows and fed from the snapshot.
- Spokes are pale, the spiral a shade darker, hanging strands dim.
- Nodes are crosses, anchors amber.
- `archer_state` has `webs`: threads, cut, held, caught, breached, hub.

**`TestWeb`** (stage_test, 32 checks):
- the build: counts, anchors on the frame, settled and taut;
- an intact web stops her, and she cannot jump it;
- an arrow cuts exactly the threads it crosses, every tick of its flight, and nothing else;
- held exactly when slowed under `WEB_CATCH_SPEED`, and rides the net;
- the edge shot flies on, slowed per thread; a snap jolts it; a weak shot is caught;
- cut free, it lies on the ground;
- a hole too small does nothing; a hole at walk height breaches it once, and she walks through;
- a hole higher up leaves a step, still stops her walking, and she jumps through;
- a hole above her reach (a 7-tall web) opens nothing;
- eight real full draws from x 3.5 breach it, and she gets through;
- deterministic, and the hash sees it.

**Checked in game** (debug, `--minimized --mcp-port 8768`):
- She stops at the face (x 6.65) and cannot jump it.
- A weak tap is caught after 9 threads.
- Twelve full draws breach it, and she gets through to the far wall.
- Screenshots: intact, half cut (the lower half crumpled onto its anchors, one strand hanging,
  the arrows that went through lying in the floor beyond), and breached.

**Not done, next:**
- Cues: `web_hit`, `web_snap`, `web_breach` have events to hang on, but no signals or table rows
  yet.
- Restoring a web from a recording's start state (`RestoreRecordingState`): a web-scene recording
  started mid-cut would replay on a whole web.
- Step 4, the real drawing.

---

## Status

| # | Piece | State |
|---|---|---|
| 1 | Rules net + blockout | built 2026-10-01: own `Web` scene, debug-line look, posts drawn not built |
| 2 | Arrows | built: cut, slow, catch, jolt; not in the arrow forecast |
| 3 | Wall + breach | built: held threads, step-over 0.35, high holes leave a step; `TestWeb` |
| 4 | Real drawing | planned; frame model not in the export yet |
| 5 | Spider in the web | planned (creature_plan) |
| 6 | In the level | planned |
