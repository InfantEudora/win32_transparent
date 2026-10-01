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

## Redesign proposal (2026-10-01): 45 degrees, and a web that gives

**A proposal, not built.** It comes from the user's review of the blockout ("behaves quite well,
feels like an actual web") and the cross-cutting rule in `README.md` (gameplay 2D, world 3D).

**Decided by the user:**
- 4-6 full draws to get through;
- she bursts through the last few threads herself;
- caught arrows stay put;
- the web stands at about 45 degrees to her walk line, one post in front of her plane and one
  behind;
- the hidden slice wall goes. She should be stopped the way the bridge holds her: it gives,
  stretches and pushes back.
- Today the threads are a line mesh only (`UpdateWebView`). Real thread meshes are still step 4.

The numbers below are from a scratch prototype (the built web's nodes turned 45 degrees, her body
as a capsule, run at the web at her real run speed and accelerations). It is not in the repo.

### Geometry: the net in its own plane

- **The web keeps its 2D build in its own plane (u, v):**
  - u runs along the web, v is height (world y);
  - each node gains a third coordinate n, along the web's normal: how far it bulges out of its
    plane.
  - The plane is turned THETA (45) about Y, and placed so u = 0 is on her line at x = cx.
  - Local to world: x = cx + u cos(THETA) + n sin(THETA), y = v, z = -u sin(THETA) + n cos(THETA).
  - One post stands at u = -w/2 (in front of her plane, z > 0), the other at u = +w/2 (behind).
- **What crosses her plane is one vertical line, u = 0 (x = cx), from the floor to the top.** That
  is the whole of the web as the rules meet it. Everything off that line is looks, except that the
  net's springs carry forces along it.
- **The rules need:**
  - the nodes and threads (now 3D, still stepped in the rules, still cheap: 85 nodes, 155 threads);
  - cx and THETA;
  - her capsule.
- **Looks only:** the posts and the beam. The beam can stop being a block: at the crossing the
  web is 3.6 tall against her 3.2 apex, so she cannot jump over it there, and on either side of
  the crossing there is nothing in her plane to jump over.

### Arrows: one point, and a hole round it

- **An arrow flies in z = 0, so it meets the web's plane at one point:** where its segment crosses
  x = cx, at height y*. In the web's terms that is (u = 0, v = y*).
- **It cuts every whole thread within WEB_HOLE_RADIUS of that point** (3D distance, so a bulging
  web is cut where it really is). It is slowed by each, and caught if too slow, as now.
- **So every hole is on the line she has to pass through.** The rest of the web stays whole and
  reads as a web with holes punched through its middle, not as a net shot away top to bottom.
- **Hit feel:** a shot is now one *thwack* and a round hole, not a slash across the whole web.
  The slow and the catch work as today:
  - a hole of radius 0.30-0.45 cuts 7-13 threads, so a full draw (46) comes out at 13-24 u/s and
    flies on;
  - a weak tap is caught;
  - a shot through the hub (dense) is caught.
- **The forecast** (`PredictArrowImpact`) can learn about the web cheaply now: it is one plane
  crossing.

### Spring-back instead of the slice wall

- **Her body is a vertical capsule** in z = 0: radius ARCHER_HALF_W (0.35), feet to head.
- **Contact is against THREADS, not nodes:**
  - Each tick, in the web's substeps, every whole thread's point nearest her axis (in u, n) is
    pushed out of her radius by a stiff penalty spring with damping.
  - The push is shared between the thread's two ends by where along it that point is.
  - The reaction, projected on x, decelerates her.
  - (Nodes alone were tried first: she slipped between them once holes opened gaps, without
    stretching anything.)
- **Coupling to her movement:**
  - The web's force on her is applied in `TickWebs`, before she moves, as a change in vel.x. That is
    how the bridge hands her momentum.
  - Her run drive is untouched: she still accelerates toward ARCHER_RUN_SPEED at ARCHER_RUN_ACCEL
    (90) on the ground, 54 in the air.
  - So the most she can lean into the web is her drive, and the web pushes back harder the further
    it is stretched.
  - x only: nothing lifts or drops her, so landings, jumps and the ground test are unchanged.
  - Stable in the prototype with contact stiffness 2000 and damping 8 at 12 substeps. Her side is
    updated once a tick, which is stable because the web's stiffness on her (tens to low hundreds
    per unit) is small against her mass.
- **No hard constraint anywhere,** so she cannot get stuck: the worst case is being pushed back
  out.
- **The breach falls out of it.** The slices, `TickWebWalls`, `WEB_STEP_OVER`, `StageBlock::web`
  and the `SegmentHitsBlock` skip can all go:
  - an intact web holds her pressed in;
  - with holes along the line, fewer threads take her push, each stretches further, and past
    WEB_BREAK_STRAIN of its length a thread SNAPS. That is the burst.
  - With few enough left she pushes through.
  - WebBreached becomes "she passed the crossing line".
- **Measured** (holes at heights 1.0, 0.4, 1.6, 0.7, 1.3, 1.9, 0.1, in that order; hole radius r):

| break strain | r | intact | gets through after |
|---|---|---|---|
| 0.5 | any | bursts at once | 0 holes |
| 0.6 | 0.36-0.40 | bursts at once | 0 holes |
| 0.7 | 0.30 | holds, 0.7 in | 7 holes (2 threads burst along the way) |
| 0.7 | 0.36-0.45 | holds, 0.7 in | 3 holes |
| 1.0+ | 0.30-0.45 | holds, barely in | 3-7 holes, no bursts |

- **The feel is lively, perhaps too lively:**
  - An intact web, run into at full speed, sends her back at 3-4 u/s.
  - A half-cut one, which lets her deeper, sends her back at 6-8 u/s: a trampoline more than the
    bridge.
  - Holding run, she rings in and out against it.
  - Jumping into an intact web, she is held 0.2 in and sent back at 5 u/s.
  - The levers are thread damping, contact damping, and a cap on the push-back speed
    (WEB_PUSHBACK_MAX, about 3 u/s). It should be tuned so she settles pressed in, as she settles
    on the bridge.

### Tuning to 4-6 shots

- **It is coverage, not count.** She gets through once holes cover her height (1.8) along the
  crossing line, near enough. So:
  - three perfectly placed holes of radius 0.36 or more already do it;
  - radius 0.30 needs seven.
  - A player does not place perfectly.
- **Proposed starting values:**
  - WEB_HOLE_RADIUS 0.32-0.34;
  - WEB_BREAK_STRAIN 0.65-0.68 (just inside the edge where an intact web holds - that edge is
    between 0.6 and 0.7 and sharp, so it wants a test pinning it);
  - contact radius 0.35, contact stiffness 2000, contact damping 8;
  - WEB_PUSHBACK_MAX about 3.
  - Expected: 4 well-spaced shots and a burst; 5-6 for a player shooting by eye.
- **If the strain edge proves too sharp to tune**, a burst rule that is easy to control: while she
  presses into it with fewer than WEB_BURST_THREADS (say 4) threads in contact, those threads snap.
  Simpler and very predictable, but less of a physical result.
- **Other levers:** spoke and ring count (denser means more threads per hole, harder to push
  through); WEB_SLOW_PER_THREAD and WEB_CATCH_SPEED (whether a hole-making shot flies on or is
  caught, which does not change the count).

### Drawing

- **The line mesh simply goes 3D,** through the local-to-world mapping above, and bulges where she
  pushes. The posts move to u = -w/2 and +w/2, so one stands in front of her and one behind. The
  beam goes diagonal, as a box turned THETA. All cheap: today's code with one transform in it.
- **Step 4, real threads:**
  - Each whole thread is a thin tube - or a camera-facing ribbon, cheaper and enough at this size -
    in ONE mesh rebuilt each frame from the snapshot: 155 threads x 6 sides x 2 rings is about
    2,000 vertices, a trivial upload.
  - A silk material: pale, slightly translucent, with a sheen along the thread.
  - Cut ends shown as the dangling pieces they already are in the sim.
  - The frame from the export when it is modelled (posts, beam, brackets, vines via `DeclareVines`).
  - `SplineDeform` tubes are unnecessary for straight segments between nodes.

### What it costs, and what is kept

- **Kept:**
  - the net builder (spokes and spiral);
  - the step, extended to 3D;
  - the floor rule;
  - catch and ride;
  - the Tarjan "held" analysis - now only to dim hanging strands in the drawing, and possibly for
    the burst rule above;
  - the events (web_cuts, web_hits);
  - the state hash;
  - the `Web` scene;
  - the snapshot, `archer_state` and the view plumbing;
  - most of TestWeb's structure.
- **Changes:**
  - nodes go v2 to v3 with THETA and cx on the declaration;
  - the arrow test becomes point-and-radius - simpler than today's;
  - new contact and coupling, about 80 lines;
  - the wall code removed (slices, `TickWebWalls`, `StageBlock::web`, `web_slices_opened` and its app
    handling);
  - the view gets the transform, and the posts move;
  - TestWeb's wall and arrow checks are rewritten:
    - an arrow cuts exactly the threads within r of its crossing;
    - an intact web stops her and pushes her back;
    - fewer than N holes still hold;
    - N holes plus a burst let her through;
    - airborne, she is pushed back;
    - deterministic.
- **About half a day of work, plus tuning time in game.**

### Risks and open questions for the user

- **How lively should the push-back be?** Bridge-soft (settles) or a little springy? The
  prototype rings.
- **Which way does it face** - front post on her left or her right? It decides which side the
  holes read best from.
- **Should the web ever catch her** (a sticky web: held for a moment, struggling), or only push?
  Out of scope for now, but the contact is the place it would go.
- **Holes always on her line:** good for play, and it leaves the web's edges always whole. If the
  user wants shots that tear the edges too, the hole could be elongated along the arrow's slope.
- **The sharp strain edge** (0.6 bursts an intact web, 0.7 holds it) wants a test pinning it, so a
  change to stiffness or pretension cannot quietly make the web paper.
- **The spider** (creature_plan) sits at the hub, which is a little off her line - fine for the
  look, and it means a shot at the spider passes beside the hub, not through it.

---

## Step 3b, as built (2026-10-01)

The proposal above, built. The user's answers:

- push-back **soft and settling**, like the bridge;
- facing is **per web**: in the Web scene she walks right into it, so the left post stands behind
  her plane and the right one in front;
- **push only**, no grabbing;
- holes **on her line**;
- the spider at the hub, a little off her line.

### What is in the rules (Stage.h `StageWeb`, Stage.cpp "Webs")

- **Its own plane.**
  - The web's frame is u along it, v = world y, n its normal.
  - It is turned `angle_deg` about Y and crosses her plane along u = 0 at world x `cx`.
  - `StageWeb::World` is the one mapping. The app uses it for the threads and the frame boxes.
  - `AddWeb(cx, y, w, h, angle_deg, spokes, rings, hub_u, hub_v)` builds it. `p_built` keeps the
    settled build positions, which the burst count is measured on.
  - The Web scene's web is `AddWeb(8.8, 0, 3.1, 3.5, 45, 12, 6, 0.15, 0.25)`: 85 nodes, 155
    threads.
- **Facing.**
  - A positive angle puts the low-x end (u < 0) at -z.
  - The camera sits at z +26, so -z is behind her plane.
  - Checked in game: the left post's foot stands higher in the frame than the right's.
  - Pinned by a test: the left post is at (7.70, -1.10), the right at (9.90, +1.10).
- **Her against the threads (`TickWebs`).**
  - Her body is a vertical capsule of radius ARCHER_HALF_W, feet to head.
  - Each whole thread's point nearest her axis is pushed out of it. That point is found over the
    part of the thread within her height (`WebNearestOnSpan`): a spoke that comes nearest her axis
    above her head still passes through her lower down.
  - The push is a damped penalty (WEB_CONTACT_STIFFNESS 2000, WEB_CONTACT_DAMPING 8), shared by the
    thread's two ends.
  - The reaction along x goes onto her velocity before she moves, the bridge's order.
  - WEB_PUSHBACK_MAX = 3 u/s caps the push-back. "Back" means toward `web.side`, the side she was
    last clear of the web on, so it does not flip once she is pressed in past the line.
- **Her idle friction stands aside while a web pushes her (`Stage::WebPushing`).**
  - Without that, the capped push (less than ARCHER_RUN_FRICTION) left her pressed in for good once
    she let go: stuck in the web.
  - Now the web eases her back out at the cap or less.
  - Friction takes over again once she is clear, the slide's pattern.
- **The burst is an explicit rule.**
  - She bursts when she presses with |fx| >= WEB_BURST_PRESS (20) while WEB_BURST_THREADS (4) or
    fewer held threads cross her path.
  - "Her path" (`WebPathThreads`) is the held threads within ARCHER_HALF_W of the crossing line,
    between her feet and head. It is counted on `p_built`, so pushing threads aside does not empty
    it; only cutting does.
  - The burst cuts exactly those threads (`WebThreadCut::f_burst`) and fires `web_burst`.
  - Counting the threads that touch her instead burst an intact web at the first touch, when only
    one or two touch.
- **Strain is kept as a backstop.**
  - A thread past WEB_BREAK_STRAIN (1.0) snaps (an arrow -1 cut, not a burst).
  - An intact web pressed for five seconds snaps nothing (tested). Pressed into a three-shot hole in
    game, one thread went.
- **Arrows: one point and a radius.**
  - An arrow crosses the web at one point on the line x = cx.
  - Every whole thread within WEB_HOLE_RADIUS (0.30, in the web's own frame, where the threads are
    now) is cut.
  - Each cut thread takes WEB_SLOW_PER_THREAD (12%) of the arrow's speed.
  - Each torn end is kicked ONCE per hit. A kick per thread sent a torn-out hub, with the arrow
    riding it, eight units away (measured at 10 u/s).
  - An arrow left slower than WEB_CATCH_SPEED is caught on the rim of its hole: the nearest node
    that moves and still has a whole thread. The nearest torn end could be a piece the hole had
    cut loose, which falls.
  - WEB_CATCH_SPEED went from 8 to 12. The weakest loose leaves at about 24 u/s, not
    ARROW_SPEED_MIN 16 (BOW_MIN_POWER is 0.25), so at 8 a tap through 7 threads flew on. Now a tap
    is caught after 6 threads and a full draw after 11 (the middle).
- **Breach.**
  - Once clear (no contacts), on the other side of the line from `web.side`.
  - Pressed in past the line and pushed back out is not through.
- **Removed:**
  - the slice wall (slices, `TickWebWalls`, `StageBlock::web`, `web_slices_opened` and the app's
    BreakBlocks loop for it);
  - the beam block in BuildWebLevel;
  - `WEB_STEP_OVER`/`WEB_PASS_MARGIN`;
  - the SegmentHitsBlock web skip.
- **The state hash** takes webs only when there are any (3D p, v, the catch offsets, side, contacts,
  push). The main level hashes as before, and `cue_replay` reports `state same`/`same`.

### Measured (rules, `TestWeb` and a scratch probe)

| situation | result |
|---|---|
| intact, run into it | held, 24 threads across her path; she stops 0.1 short of the line, pushed back at most 1.5 u/s, still within 0.005 |
| let go after pressing | eased out at the cap (3.0 u/s), clear of it, x 8.26 |
| jump into it | held, pushed back at most 2.1 u/s, no burst |
| 1 hole (r 0.30) on her line | held, 17 threads across |
| 2 holes | held, 11 across, still within 0.004 (was 0.031 at r 0.33) |
| 3 holes | 4 across: burst, through |
| full draws from x 5.0, aims 0 / -8 / +8 / -14 | 7, 6, 16 (caught), 4 threads: through on the 4th, with a burst in the rules run |
| weak tap (nock + 1) | 7 threads, caught, rides a tied node |

In game (debug, port 8768, paused and stepped):

- **intact:** a flat web in the frame, left post behind;
- **after three shots:** the middle torn out, the hub on the floor, the caught arrow hanging on the
  hole's left rim;
- **pushing against it:** pressed in at x 8.87, held by two low threads, steady within 0.005, and
  eased back out to 8.26 when the hold ended;
- **fourth shot (-14):** she ran through a clear hole without touching a thread. In game the three
  shots plus one strain snap left nothing across her path, so no burst was needed. In the rules run
  of the same aims, the burst finished it.

### `TestWeb` (stage_test.cpp), rewritten

- build counts;
- anchors on the opening's edges, in the web's plane;
- the prop's 3.1 x 3.5;
- facing;
- settled and taut;
- intact holds her (pressed, capped, settled, nothing snaps in five seconds);
- let go and she is eased out;
- a jump is held;
- one and two holes hold, a third leaves <= WEB_BURST_THREADS and she bursts through, and the burst
  tears exactly the threads across her path;
- an arrow tears exactly the threads within the radius;
- a full draw flies on, slowed per thread;
- a snap kicks each end once;
- a weak tap is caught on a tied node and rides it;
- cut free, it lies on the floor;
- four to six full draws get her through, and the arrows caught on the way stay in the opening;
- determinism, and the hash sees a different shot.

### The prop, for step 4 (not drawn yet)

- The `spider` node in archer.glb is one mesh: 8,207 vertices, the `bones` texture.
- Its main axis runs 47.8 degrees from +x toward -z: its low-x end at (x -0.84, z +0.70), in FRONT.
  That is the opposite of what the Web scene needs, so placing it means mirroring it or turning it
  by about 180 degrees about Y.
- Posts at u about +/-0.9; the beam's top at y 2.03.
- At model_scale 1.82 that is an opening of about 3.1 x 3.5, which is where StageWeb's w and h
  come from.

### Sounds, as built (2026-10-01)

The user's three files, `assets/sound/web_hit`, `web_snap` and `web_breach`. They are 24 kHz
stereo, so they were encoded as-is with the music readme's settings (`oggenc2 -q -1`; no
resampling under 32 kHz). The wavs are removed, as the other SFX's were. The rows are in
`assets/cues/archer.json`; the signals come from `ApplicationArcher::SignalCues`.

| signal | when | cue |
|---|---|---|
| `web_hit` x, speed, threads, caught | an arrow tears a web (`WebHit`, which now carries the arrow's speed as it met the web); nothing through an existing hole | `web_hit`, gain by speed 23..46 -> 0.6..1.0 |
| `web_snap` count, x | threads snapping in a tick - an arrow's, strain, a burst - ONCE per web however many went | `web_snap`, gain by count 1..12 -> 0.45..1.0, gap 3 |
| `web_burst` x | she tears the last threads herself | `web_burst` plays `web_breach` |
| `web_through` x, first | out the far side, clear of it | `web_through` plays `web_breach`, first time only |

- All of them are on the effects bus, with distance falloff and panning by `dx` like `arrow_hit`.
- The burst and the through are a few ticks apart (3 in game). Both are in the `web` group (gap 60),
  so the tear is heard once. Through a clear hole there is no burst, and the through plays instead.

Checked:

- **Cue log in the Web scene, four full draws from x 5:**
  - each shot plays `web_hit` and `web_snap` on its hit tick, the snap at 0.75 / 0.70 / 1.00 (16
    threads, caught) / 0.60;
  - `arrow_hit` follows when the arrow lands beyond the web; the caught one has none;
  - running in: `web_snap` at 0.50 and `web_burst` on one tick, and `web_through` 3 ticks later
    logged `skip (group busy)`.
- **Strain:** pressing into a three-shot hole gave a lone strain snap at 0.45.
- **Through a clear hole:** with no burst, `web_through` played.
- **Levels, by an offline mix** (SoundSystem::InitialiseOffline, scratch tool), loudest 10 ms:

  | sound | level |
  |---|---|
  | footsteps | -21 to -27 dB |
  | a lone strain snap | -22.7, footstep level |
  | the burst, snap and tear | -17.2 |
  | an arrow landing beyond (gain 0.63) | -13.2 |
  | a full-draw web hit (file x gain) | about -12.4 |
  | the kick's land | -10.6 |

  The web hit comes 50 ms after `arrow_leave` (-3.6) at this distance and sits under its tail. The
  gains keep the user's files' own balance. If the tear should stand out more, `web_burst` and
  `web_through` gain 1.5 puts it level with an arrow landing; the table reloads while the game runs.
- `make rules` passes. `cue_replay` (archer_test, main level) is `same`.

### Left open

- The frame is still boxes; step 4 replaces them with the prop.
- `arrow_swoosh` is forecast on the arrow's next impact, which ignores webs: a shot through the web
  swooshes onto where it lands beyond, and a caught one has no swoosh. Webs in the arrow forecast
  would fix both.
- WEB_PUSHBACK_MAX 3 is soft as asked. Raise it if, played, the web feels dead.
- Shots that miss her line (well above her head) tear holes that change nothing for her. That is
  intended, and it is also the only way to waste a shot.

---

## Status

| # | Piece | State |
|---|---|---|
| 1 | Rules net + blockout | built 2026-10-01: own `Web` scene, debug-line look, posts drawn not built |
| 2 | Arrows | built: cut, slow, catch, jolt; not in the arrow forecast |
| 3 | Wall + breach | built, then REPLACED by 3b (the slice wall is gone) |
| 3b | Redesign: 45 degrees, spring-back, burst | built 2026-10-01 - see "Step 3b, as built"; through in 4 full draws; `TestWeb` rewritten |
| 4 | Real drawing | planned; the `spider` prop is in archer.glb (opening 3.1 x 3.5, faces the wrong way for the Web scene) |
| - | Sounds | built 2026-10-01 - see "Sounds, as built": web_hit, web_snap, web_breach through the cue table |
| 5 | Spider in the web | planned (creature_plan) |
| 6 | In the level | planned |
