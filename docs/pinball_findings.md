# Pinball stage 0 — review findings

A review of the first build of `apps/pinball` (commit `d59ab93`, "Pinball start"), done on
2026-09-13, and what was changed as a result. The design it was built to is
`apps/pinball/pinball_design.md`; the layout it now has is `apps/pinball/Table.h`, whose header
carries the revision note. This document is the *why* behind that note, kept separately so the
header can stay about the numbers.

The short version: the first build was well structured and well argued, and its table did not
work as a table. A ball served from the plunger could not reach the play area at all, and
several features it could have reached were in positions a ball could not use. None of that was
visible from the coordinates, and the tool that was supposed to catch it was checking the wrong
thing. Everything below follows from that.

---

## 1. What was wrong

In order of how much it mattered.

**1.1 The ball could not get from the plunger into the play area.** The plunger lane feeds the
top orbit, whose inner rail is continuous, so the only way down into the playfield is the orbit's
left return lane. That lane was 0.35 centre-to-centre between the cabinet and the return rail,
which after the rail's own 0.14 thickness is 0.28 clear against a 0.27 ball — one ball exactly,
and the rail's rounded end closed it. Flooding the deck from the plunger reached the chute, the
orbit, and nothing else.

**1.2 The inlanes were sealed at the top and, where open, missed the flipper.** The slingshot was
built as a single face whose upper corner sat on the inlane divider, so the inlane was a pocket a
ball could only enter from below. The design's own clearance number for it — "0.387, 1.43 balls"
— was measured centre-to-centre; with the divider's and the sling's thicknesses taken off it was
0.24, under a ball. And the divider ended 0.65 outboard and 0.45 down-table of the flipper pivot,
so a ball that did come down the lane passed behind the bat into the drain.

**1.3 Both ramps were too narrow for a ball.** The rails stood *on* the floor's edges, half a
rail's thickness inboard "so their outer faces line up with the floor's" — leaving 0.18 clear
between them on a 0.46 floor.

**1.4 The right flipper rested in its flipped pose.** The mirrored bat was built along −X and
rotated by the same angle as the left one. That is a rotation through 180 + angle, not 180 −
angle, so at rest the tip pointed up-table. It is visible in the first build's own screenshots.

**1.5 The machine was the wrong shape.** The design took 132 × 58 cm as the playfield; that is a
cabinet's outer footprint, and a real playfield is about 107 × 51 cm. The deck came out 13.2 × 5.8
(2.28:1) against a reference artwork at 1.37:1, and the middle third of it held nothing but the
two ramps' return rails, which ran down the *centre* of the table as opaque 0.46-wide slabs and
covered most of the deck below them.

**1.6 A 0.43 strip along the cabinet was open** for the whole length of the table, because the
outlane guide started in the open rather than at the wall. A ball could wander down it and drain
without ever meeting an outlane.

**1.7 The "thin to look at, fat in collision" rule cannot apply to interior rails.** The design
asks for every static collider to be 0.6 thick as a tunnelling defence. An inlane is 0.40 clear
between two rails; fattening each by 0.23 a side closes it, and the same is true of every lane on
the table. `python tools/pinball_plan.py --collider 0.6` shows what remains under that rule: the
plunger lane. The 0.6 rule stands for the cabinet walls and anything with dead space behind it;
interior rails must collide at their visual thickness, which makes the swept-raycast tunnel guard
(design §2.2, mitigation 2) load-bearing rather than a safety net.

**1.8 The clearance script was a copy of the numbers.** `tools/pinball_clearance.py` carried its
own transcription of Table.h and the ramp paths, and checked only headroom under ramps. It could
not see 1.1–1.4 at all, and it would have drifted from the app the first time either changed.

Smaller things: the camera shots were hand-derived literals that went stale with any change of
table length; the cabinet was a quarter metallic and mirrored the (undrawn) HDR environment back at
any side-on camera as a bright blur; the orbit rail was chrome and read as a light fitting; the
upper flipper's comment still placed it "just up-table of the MISSION drop bank" on a wall the bank
had already left; the changes index counted "of 9" and "of 10" in different places; and
`SeedOrbitFromCamera` assigned `live_fov` twice.

What was **right**, and kept: Table.h as the single source with no engine types in it; the
`PIN_MIRROR_X` discipline; `TableBuilder`'s swept polylines as the one thing both art and colliders
come from; the feature list as flat PODs; the named camera shots with the free orbit as a mode;
the world-space labels that survive an `include_ui:false` screenshot; the MCP tools; the tilt
slider from day one; and the tunnelling arithmetic that picks 240 Hz.

---

## 2. What changed

**The table is 9.2 long.** Deck 5.8 × 9.2, cabinet 6.9 × 10.4 — 1.51:1, against the artwork's
1.37 and a real machine's 2.08. The play area keeps its true 5.10 width, so every bat, lane and
bumper is still full size; only the spacing between the rows of features came down. The window is
800 × 1200 to match.

**Every lane was re-cut so a ball fits, with margin.** Measured *clear*, after thicknesses:

| lane | clear | balls |
|---|---|---|
| inlane | 0.40 | 1.48 |
| outlane | 0.41 | 1.52 |
| orbit return | 0.41 | 1.52 |
| orbit at apex | 0.43 | 1.59 |
| plunger chute | 0.51 | 1.89 |
| ramp mouth | 0.46 | 1.70 |
| flipper tips at rest | 0.34 | 1.27 |

The slingshots are triangles whose outer side defines the inlane; the divider bends inboard to end
a ball's width up-table of the pivot so the inlane delivers onto the bat; the outlane guide starts
at the cabinet wall; the upper flipper's pivot sits under the centre of the return lane so the ball
lands on its root; the ramps' rails stand outside the floor.

**The ramps climb along a wall and return along the same wall**, as the artwork draws them — up
the inside, a U-turn at the top, down the outside over the lane below (the return lane and upper
flipper on the left, the plunger chute and drop bank on the right), ending short of the inlane
dividers so the ball drops into the lane's mouth. Once airborne a ramp is four chrome wires
(`MakeTube`), not a slab, so from above it hides almost nothing. Nest guides and the spinner's own
posts went; the ramp mouths, the saucer rims and the standups bound the pop nest, and the spinner
hangs from the left ramp's rails. The standups face the player from below the pops, because every
wall on this table now has a ramp or an orbit lane over it.

**The camera shots are solved, not typed.** `MakeShot` takes the nearest and farthest points a
shot must hold, an elevation and a fov, and derives the camera as the intersection of the two
bounding rays, then backs off along its own axis if the subject does not fit the window's aspect.
The table shot is 26° off vertical so the machine keeps its proportions on screen.

**The repeated parts come from Blender.** `tools/pinball_parts_blender.py` runs headless and
models the fifteen parts of design §3.2 — tapered bats with the pivot at the origin, domed bumper
caps, torus rubbers, bevelled targets, a chamfered saucer rim, the plunger — and exports
`apps/pinball/assets/meshes/parts.glb` with the app's material names. `LoadParts` picks it up if
it exists; every part has a primitive fallback and the log names anything missing. The static
machine stays procedural until the layout stops moving.

**The plan tool replaces the clearance script.** `tools/pinball_plan.py` reads the running app's
`pinball_layout` — every builder records what it built into a plan the tool draws — fattens every
shape by the ball's radius, floods from where the plunger serves, and reports what the ball
cannot reach, first for a bare ball and then for one 1.4 diameters across. It also walks each
ramp centreline for intersections and for air over the deck beside anything a ball visits. It
draws the result to `apps/pinball/build/plan.png`: green reachable, red not, yellow reachable but
tight. It has no copy of the numbers, so it cannot drift.

---

## 3. What is verified

With the app running the current layout:

```
python tools/pinball_plan.py
  clear - every feature is reachable and nothing is roofed over.
```

That is 36 shapes and 33 named features, all reached by a 1.4-diameter ball from the plunger,
with the ramps carrying the flood from mouth to exit. The four camera shots and the orbit camera
were screenshotted over MCP and looked at; the modelled parts load with the extents Blender
reports for them, which is the check that Blender's Z-up came through as the engine's Y-up.

Not verified, because nothing moves yet: any of this under physics. A 0.34-radius U-turn at the top
of each habitrail, a 0.46 drop off its end, and a ball coming off the orbit onto the upper
flipper's root are the three things stage 3 should look at first.

---

## 4. For the next stages

- **Stage 1 must implement the tunnel guard before anything else collides.** Interior rails at
  0.14 give a ball at 90 u/s three ticks inside a rail; nothing but the swept raycast stops it.
- **Run `tools/pinball_plan.py` after moving anything.** It is the difference between a layout
  and a drawing of one. `--collider <t>` previews a collider thickness; `--json` replays a saved
  layout without the app.
- **`parts.glb` is a starting point, not a ceiling.** Open it in Blender, improve a part, export
  with the same node names — or extend the script, which is what keeps it reproducible. The
  bumper skirt, lamp domes and a proper plunger housing are the obvious additions.
- **`table.glb` waits for the layout to freeze**, as design §3.3 already says of the playfield
  art. When it does, the ramp paths in `ApplicationPinball.cpp` and `pinball_layout`'s plan are
  the geometry to model against.
- The Engine panel's *Target Physics TPS* slider is clamped to 200 while the table runs at 240;
  touching it silently slows the table. Already noted in the design; still open.

---

## 5. Stage 1 — ball, flippers, plunger (2026-09-13)

Built the same day, on the re-laid table. What exists now:

- **Colliders for everything static**, made in the same builder that draws each thing, from the
  same path: `AddSweptColliders` puts one box per path segment (tilted where the path climbs, so
  it is also the ramp floor's collider), `AddBoxCollider` and `AddPostCollider` do the boxes and
  the capsules. Cabinet, deck, an invisible glass at 1.10, every rail and divider, the slingshot
  triangles, posts, bumper bodies, targets and the ramp climbs all collide. Their *switches* are
  stage 2; only the habitrail wires (stage 3) have no collider, so a ball that climbs a ramp
  drops off its crest.
- **The ball**: sphere, mass 1, restitution 0.12, friction 0.12, a little linear damping as
  rolling resistance, sleeping disabled, gravity the tilted vector. On the open deck it rolls at
  7.8 u/s², which is a solid sphere under 6.5°.
- **Flippers and plunger** in `apps/pinball/Mechanisms.h`: hinge joints with motors (rest-to-up in
  10 ticks, back in 20) and a slider with a motorised pull and a spring return, on the
  `HingedDoor` pattern. The tip rests flush with the front wall, the ball against the wall.
- **The tick**: held keys read every tick; the speed clamp; the tunnel guard (a raycast along the
  tick's travel above one radius per tick, reflecting by hand); drain and *escape* detection,
  both re-serving. Z and / (or the arrows, or the shoulder buttons) flip, space plunges, R serves.
- **Tools**: `pinball_telemetry`, `pinball_flipper`, `pinball_plunger`, `pinball_place_ball`
  (a SimCommand) and `pinball_run`, which steps a paused table and returns the ball's path. The
  panel has the readouts and live tuning for the flippers, plunger and ball.

**Verified**, all with the simulation paused and stepped, so every number is reproducible:

| test | result |
|---|---|
| flippers, hold 30 ticks | both hands 64° at tick 10, held, rest at tick 20 after release, mirror-exact |
| full plunge | 28 u/s; round the orbit, down the return lane, onto the upper flipper's face, out along it |
| flipper shot from a resting ball | 49 u/s off either bat, mirror-exact |
| 90 u/s at the walls, 8 directions | 0 escapes; the guard intervened 11 times, as designed |
| 5 s unattended after a plunge | orbit, return lane, upper flipper, mid-table, drain, re-serve; 0 escapes |

**What the engine taught us**, each fixed where it bit:

- `toradians()` in `core/type_helpers.h` did not parenthesise its argument, so
  `toradians(up - rest)` was `up - rest/180*pi`: a 101-radian hinge limit and a flipper that spun
  like a propeller. **Fixed in core**; every existing call passed a single identifier, so nothing
  else changes. The only core change in this stage.
- **rp3d's twist friction** - a torque about the contact normal, bounded by friction times the
  normal impulse with no lever arm - froze a ball resting against the outhole funnel on a slope
  that should have rolled it into the drain. A ball rolling along a wall spins about exactly that
  wall's normal. Steel rails now have zero friction, which zeroes the twist; friction is mixed as
  a geometric mean so the ball's own value does not reintroduce it. Worth an engine note: any
  small rolling body against a wall will hit this.
- **rp3d takes the larger of two restitutions.** The ball is 0.12 so that the table's surfaces
  decide; the plunger tip is 0.9, which is what turned a 17 u/s launch into 28.
- A **light bat** cannot hit hard in an impulse solver, because the motor's torque cap is per tick
  and a contact lasts one or two: the bat coasts on its inertia during the hit. Mass 2 with the
  torque scaled to match is the honest model of a solenoid that keeps shoving.
- Two layout faults only a moving ball could show: the orbit needed an **outer guide** (a plunged
  ball ran up the straight cabinet wall past the inner rail's end into the corner), and a ramp's
  side must reach **down to the deck** (a ball beside the climb jammed under the rising rail).

**Open for stage 2 and 3**: switches and impulses on the slings and pops; the drain as a trigger;
the wires as capsule chains; the ramp crest hand-off; feel tuning with the sliders - the defaults
are a first answer, not a measured one.
