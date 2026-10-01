# Web Plan

A big spider web strung in a wooden frame, about twice her height. It blocks the way until she
shoots her way through. The big spider of `creature_plan.md` sits in it. Talked through
2026-10-01; nothing here is built yet. The reference render (2026-10-01) shows the look: two
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

## Status

| # | Piece | State |
|---|---|---|
| 1 | Rules net + blockout | planned |
| 2 | Arrows | planned |
| 3 | Wall + breach | planned |
| 4 | Real drawing | planned; frame model not in the export yet |
| 5 | Spider in the web | planned (creature_plan) |
| 6 | In the level | planned |
