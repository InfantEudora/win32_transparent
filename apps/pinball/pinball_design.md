# Pinball — table design and build plan

A solid-state pinball machine on this engine, in the spirit of *3D Pinball for Windows — Space
Cadet*: one playfield, a space-mission rule set, ranks earned by completing missions, and a
multiball you have to work for.

This document is the **layout and asset plan**, written before any code exists. It answers four
questions in order:

1. What the machine physically *is* — levels, ramps, and every scoring feature, with coordinates.
2. What each of those is *built from*, given what this engine's physics can actually express.
3. What has to come out of Blender, under what names.
4. In what order to build it, and what the first (static, non-playable) milestone looks like.

Nothing here is settled. The coordinates are a first draft meant to be dragged around in Blender;
what is *not* negotiable is section 2, because those are measured properties of the engine rather
than taste.

> **This document is the guide; `Table.h` is the machine.** Stage 0 is built (2026-09-13), and
> putting the layout on screen moved ten of §1.5's coordinates — flipper pivots that left a gap
> half a ball wide, a slingshot lying across the lane beside it, two rows of features in the same
> square inch, and three things underneath a ramp. Every change is indexed and argued for at the
> top of `apps/pinball/Table.h`, which is where the built coordinates live. §1.5 below is left as
> written, because it is the reasoning that produced them and that is still worth reading.
>
> `tools/pinball_clearance.py` is what found the last of them. Run it after moving a ramp path, a
> feature or a post: "is this under that" is a question about a polyline, and three of the ten were
> invisible to anyone reading the numbers.

---

## 0. What the engine already gives us, and what it does not

Worth stating up front, because three of these decide the table's dimensions and one decides its
tick rate. Each was checked in the source rather than remembered.

**We have:**

- `reactphysics3d` with box, sphere, capsule and heightfield colliders (`core/physics/Physics.h`),
  per-body collision filtering, triggers (`SetTrigger`), and per-axis linear/angular locks.
- Hinge and slider joints in the rp3d headers (`3rdparty/reactphysics3d/constraint/`), both with
  **limits and motors** — that is the flipper and the plunger, exactly.
- Contact and trigger callbacks: an app derives from `rp3d::EventListener` and calls
  `main_scene->physics_world->rp_world->setEventListener(this)`. `ApplicationShip::onContact` and
  `ApplicationTileset::onTrigger` are the two worked examples.
- A deterministic tick, `sim_pause` / `sim_step`, and MCP tool registration — so every feature below
  can be tested by an agent with no hands on the keyboard.
- Sound (`core/SoundSystem.h`): 32 resident buffers, **16 simultaneous voices**, one-shot voices
  recycled oldest-first. Plenty for a pinball table, which is mostly short overlapping one-shots.
- `TextMesh` + `shared_assets/meshes/glyphs_unispace.glb` for the score display — Tetris and
  Breakout both already drive it.
- Point and cone lights with soft shadows (`core/Light.h`), which is what flashers and general
  illumination want.
- An equirectangular HDR environment loader (`CubeMap::LoadFromEquirectangular`), and
  `witsand_woolshop_2k.hdr` is already sitting in this folder waiting for it.

**We do not have, and the design works around it:**

| Gap | Consequence for this table |
|---|---|
| **No triangle-mesh collider anywhere in `core`.** Only box / sphere / capsule / heightfield. | Every collision surface on this table is a **primitive**, and the pretty Blender geometry is render-only. Curves become chains of thin boxes. See §2. |
| **No cylinder collider.** | Posts and pop bumpers are **capsules with their round caps buried** below the deck and above ball height, so the ball only ever meets the straight middle. |
| **rp3d 0.10 has no continuous collision detection.** Grepped for it; there is none. | A pinball is the single worst case for a discrete solver. Three mitigations, all of them in §2.2 — none is optional. |
| **`persistentContactDistanceThreshold` is 0.03** (`engine/PhysicsWorld.h:129`), in world units. | A real 27 mm ball would have a radius *half* the solver's own contact tolerance. The table is therefore modelled at **10× life size**. See §2.1. |
| **`core/physics/Physics.h` wraps only a ball-and-socket joint.** | Flippers, plunger, gates and the spinner create their joints directly on `physics_world->rp_world`, the way `apps/ship/HingedDoor.cpp` already does. That file is the template; copy its destructor discipline too (the joint holds raw body pointers, so it must be destroyed before either body). |
| One `RRandom` stream shared by the sim and the UI/MCP threads. | Known engine issue. Pinball's randomness is small (mission selection, a couple of light shows) — keep it to a **separate generator owned by the rules object**, and the table stays replayable regardless of how that item lands. |

---

## 1. The machine

### 1.1 Orientation, and why the table is flat

**+X right, +Y up out of the playfield, +Z toward the player (down-table, where the drain is).**
The playfield surface is the plane `y = 0`. Up-table is −Z.

The real cabinet is tilted about 6.5°. **We do not tilt the table — we tilt gravity.** Rotating the
whole machine about X is exactly equivalent to rotating the gravity vector the other way, since
every part of the machine rotates together, ramps included. Tilting gravity instead buys axis-
aligned coordinates for the entire build: a wall is a box with no rotation, the camera looks
straight down −Z, and "how high is this ramp" is just `y`.

```
    gravity = (0, -G·cos θ, +G·sin θ)        θ = 6.5°, G = 98.1
            = (0, -97.47, +11.11)
```

The tilt angle is the single most important feel parameter on the machine — it sets how fast the
ball comes down — so it gets a slider in the debug UI and an MCP setter from day one.

### 1.2 Scale: 1 world unit = 10 cm

The table is modelled **ten times life size**, and the reason is the 0.03 contact threshold above.

| | real | here |
|---|---|---|
| Playfield | 132 × 58 cm | **13.2 × 5.8 units** |
| Ball diameter | 27 mm | **0.27 units** (radius 0.135) |
| Ball radius ÷ contact threshold | 0.45 | **4.5** |
| Gravity | 9.81 m/s² | **98.1 u/s²** |
| Max ball speed | ~8 m/s | **~80 u/s** |

Gravity is scaled with the geometry so that *timings* match a real machine — a ball takes the same
number of seconds to cross the table as it would in life, which is what makes the thing feel right.

### 1.3 Tick rate: 240 Hz

`Application::SetPhysicsTPS(240.0f)` in `Init()`, the way `ApplicationBreakout` sets 60. The engine
default of 50 is nowhere near enough: at 80 u/s a ball moves 1.6 units per tick at 50 Hz and would
pass clean through the entire flipper assembly between two frames.

At 240 Hz it moves **0.33 units per tick** — still 2.4× its own radius, which is why §2.2 exists.
240 was chosen as the point where the remaining mitigations are cheap; the world here is a few
hundred static bodies and one to four dynamic ones, so the solver cost is not the constraint.

> The Engine panel's *Target Physics TPS* slider is clamped to 200. `SetPhysicsTPS` itself is not,
> so the app starts correctly — but touching that slider will silently drop the table to 200 Hz.
> Worth widening the clamp, or leaving a note next to it.

### 1.4 How many levels: three, plus a backglass

| Level | Height | What lives there |
|---|---|---|
| **L0 — the deck** | `y = 0` | Everything the ball rolls on by default: flippers, bumpers, targets, lanes, the plunger chute. Nine tenths of the playtime. |
| **L1 — ramps and habitrails** | `y = +0.4 … +0.9` | Two ramps the ball climbs, and the wire rails that carry it back down to the inlanes. Where the ball is *visibly* somewhere else. |
| **L−1 — the subway** | `y = −0.7` | Under-playfield tunnels. The three wormhole saucers swallow the ball here and a kicker spits it back out. Never seen; entirely about the *sound* and the delay. |
| **Backbox** | vertical, behind the table | Score, rank, mission text, and the animated light show. Not playable. |

Three is the right number. A fourth level buys nothing a second ramp does not, and each extra level
is another set of hand-built primitive colliders.

### 1.5 Feature list, with coordinates

> Ten of the coordinates below were moved once the table was on screen — see the note in the
> preamble. `apps/pinball/Table.h` is the built layout.

Playfield spans `X ∈ [−2.90, +2.90]`, `Z ∈ [−6.60, +6.60]`. The plunger lane takes the right-hand
strip, so the **play area proper is `X ∈ [−2.85, +2.25]`** and its centre line is at `X = −0.30`.
All coordinates below are `(X, Z)` on the deck unless a `y` is given.

**Bottom — the flipper end**

| # | Feature | Position | Notes |
|---|---|---|---|
| 1 | Drain / outhole | `(−0.30, +6.50)` | Trigger. Ball is removed and re-served to the plunger. |
| 2 | Left flipper | pivot `(−1.05, +5.35)` | Length 0.80, rest −32°, up +32°, hinge about +Y. |
| 3 | Right flipper | pivot `(+0.45, +5.35)` | Mirror of 2 about `X = −0.30`. |
| 4 | Left slingshot | face `(−1.95, +4.15)` → `(−1.35, +4.95)` | Trigger + impulse along the face normal. |
| 5 | Right slingshot | mirrored | |
| 6 | Left inlane / outlane | divider `X = −1.80`, `Z ∈ [+3.6, +5.8]` | Inlane rollover trigger at `(−1.55, +4.60)`. |
| 7 | Right inlane / outlane | mirrored | |
| 8 | Ball-save kickers | `(−2.50, +5.70)`, `(+1.90, +5.70)` | Outlane triggers that fire an up-table impulse **only while the save is lit**. |

**Right edge — the launcher**

| # | Feature | Position | Notes |
|---|---|---|---|
| 9 | Plunger | `(+2.575, +6.30)`, travel 0.9 along −Z | Slider joint, spring-return, motorised pull-back. |
| 10 | Launch chute | `X = +2.575`, `Z: +6.3 → −5.40` | Walled lane; divider wall at `X = +2.25`. |
| 11 | Skill-shot rollovers | `(+2.575, −3.00 / −4.20 / −5.00)` | Three triggers; releasing at the right moment lights the top one. |
| 12 | One-way gate | `(+2.35, −5.30)` | Hinge with asymmetric limits — into the orbit, never back. |

**Top — the orbit and the wormholes**

| # | Feature | Position | Notes |
|---|---|---|---|
| 13 | Top orbit (horseshoe) | `(+2.30, −5.40)` → `(0, −6.20)` → `(−2.30, −5.40)` | The signature shot. A polyline of thin boxes; see §2.3. |
| 14 | "FUEL" rollover lanes | `(−1.50 / −0.30 / +0.90, −5.00)` | Three lanes; complete the word for a bonus multiplier step. |
| 15 | Wormhole saucers ×3 | `(−1.90, −4.40)`, `(−0.30, −4.90)`, `(+1.30, −4.40)` | Hole triggers → subway. Hold a ball here to lock it for multiball. |

**Middle — the scoring cluster**

| # | Feature | Position | Notes |
|---|---|---|---|
| 16 | Pop bumpers ×3 | `(−1.35, −2.55)`, `(+0.15, −2.55)`, `(−0.60, −3.45)` | Skirt radius 0.42, collider radius 0.30. |
| 17 | Bumper nest guides | around 16 | Two angled walls that keep the ball rattling. |
| 18 | "MISSION" drop targets ×3 | `X = −2.30`, `Z = −1.20 / −0.60 / 0.00`, facing +X | Drop 0.30 below the deck when hit; reset as a bank. |
| 19 | Standup targets ×2 | `(+1.75, −1.90)`, `(+1.75, −0.60)` | Static; pure switches. |
| 20 | Spinner | `(−2.05, +1.60)` | Free hinge in the left ramp mouth; scores per revolution. |
| 21 | Gravity-well saucer | `(+1.55, +1.20)` | Eject saucer; starts the selected mission. |
| 22 | Posts and rubbers | ~8, scattered | Capsules with rubber bounciness. |

**L1 — ramps**

| # | Feature | Path | Notes |
|---|---|---|---|
| 23 | Left ramp | entry `(−2.05, +2.00, 0)` → crest `(−2.30, −1.00, +0.85)` → habitrail curving right and back down-table → drop into left inlane at `(−1.65, +4.30)` | Spinner (20) sits in its mouth. |
| 24 | Right ramp | entry `(+1.45, +2.20, 0)` → crest `(+1.70, −0.60, +0.70)` → loops over the bumper nest → right inlane `(+1.05, +4.30)` | The "re-entry lane". |

**L−1 — subway**

| # | Feature | Path | Notes |
|---|---|---|---|
| 25 | Subway junction | all of 15 drain to `(−0.30, −3.80, −0.70)` | |
| 26 | Subway kicker | exits at the left inlane `(−1.55, +4.10)` | Fires after a deliberate ~0.4 s delay; the delay is the drama. |

### 1.6 Bells and whistles — the rule set

The physical table above is worth nothing without these, and they are the part that is pure code —
no assets, no colliders, and fully testable by an agent through MCP.

- **Score, with a bonus multiplier 1×–5×** stepped by completing the FUEL lanes (14).
- **Ranks.** Cadet → Ensign → Lieutenant → Captain → Commodore → Admiral, earned by completing
  missions. This is the progression that made Space Cadet memorable and it costs almost nothing.
- **Missions.** The drop target bank (18) selects one; the gravity-well saucer (21) starts it. Each
  mission is a small goal with a timer — "hit both ramps", "three bumpers then the orbit", "lock a
  ball". A mission is a tiny state machine and they are trivially unit-testable.
- **Multiball.** Three balls locked in the wormholes (15) release together. Needs the physics to
  hold up with four dynamic balls, which it will.
- **Ball saver.** Timed from the launch; lights the outlane kickers (8).
- **Skill shot** on the plunger (11).
- **Nudge and tilt.** Left / right / forward nudge applies an impulse to every live ball and a small
  camera shake; three warnings and the fourth tilts the table dead. Nudge is the one input a pinball
  game cannot do without, and it is also the easiest thing to get subtly wrong, so it goes in early.
- **Extra ball**, awarded by rank.
- **Lights.** General illumination, per-feature lamps, and flashers on the ramps and bumpers.
- **The backglass**, with score and mission text on `TextMesh`.

---

## 2. What everything is built from

This is the section that is not a matter of taste.

### 2.1 The rule: render meshes and colliders are different objects

Because `core` has no mesh collider, **nothing from Blender is ever a collision surface.** Every
feature is two things that happen to occupy the same place:

- a **render object** — the Blender mesh, arbitrarily detailed, no physics at all;
- a **collider object** — one or more boxes, spheres or capsules, invisible, that the ball actually
  touches.

This is not a workaround to be apologised for; it is how real pinball simulations are built, and it
is *better* than a mesh collider, because the collision shape can be simpler and more forgiving than
the art. A 0.6-thick invisible box behind a slender-looking rail is exactly what §2.2 needs.

The debug UI gets a **"show colliders"** toggle from day one — `PhysicsWorld::SetDebugRendering`
already exists — because a mismatch between art and collider is the bug this whole approach invites,
and it is invisible without it.

### 2.2 Tunnelling: three mitigations, all required

With no CCD and 0.33 units of ball travel per tick, this is the one thing that will ruin the game.

1. **Thick walls.** Every static collision box is at least **0.6 units thick** — twice the worst-case
   per-tick travel. The outer walls genuinely are that thick; interior guide rails are thin *to look
   at* and fat in collision, hidden behind the art. Costs nothing and handles the ordinary case.
2. **A swept tunnel guard on each ball**, once per tick in `RunSimulationTick`, before the world
   steps. Cast `PhysicsWorld::Raycast` from the ball's previous centre to its predicted next centre;
   if it crosses a static collider, place the ball at the hit point offset by its radius along the
   normal and reflect the velocity by hand. `Raycast` is already there and takes an exclude-body, so
   this is perhaps forty lines. It is the safety net that makes the whole thing trustworthy, and
   it is also cheap: one raycast per ball per tick.
3. **Speed clamp.** Cap ball speed at 90 u/s. A pinball never legitimately travels faster than a hard
   flipper shot, and an uncapped solver occasionally produces a body that has been squeezed by two
   constraints and is doing 400.

Verification is concrete and belongs in the MCP layer from the start: `pinball_place_ball` can put a
ball anywhere with any velocity, so "fire the ball at every wall on the table at 90 u/s from both
sides and assert it never ends up outside the cabinet" is a test an agent can actually run.

### 2.3 Curves: the polyline wall builder

The top orbit, the ramp rails and the bumper nest are all curves, and a curve has to become a chain
of boxes. Rather than hand-placing sixty boxes, the app gets one helper — call it
`TableBuilder::AddWallPolyline` — that takes a list of 2D points, a height, a thickness and a
material, and emits one box per segment, each rotated to the segment and overlapping its neighbours
slightly so there is no gap for the ball to catch on.

The same polyline then drives the **render** mesh (extruded into a strip) so art and collider come
from one source and cannot drift apart. That single helper is probably the highest-leverage piece of
code in the whole app.

### 2.4 Feature-by-feature

| Feature | Render | Collider | Motion |
|---|---|---|---|
| Playfield deck | Blender mesh, playfield art texture | **one** box, 0.8 thick, top face at `y = 0` | static |
| Outer walls / cabinet | Blender | 4 boxes, 0.6 thick | static |
| Guide rails, orbit, lane dividers | extruded polyline | `AddWallPolyline` (§2.3) | static |
| Ball | `MakeSphere` or Blender, chrome material | sphere `r = 0.135`, bounciness ~0.4, low friction | dynamic, sleeping **disabled** |
| Flipper | Blender (tapered bat) | 2 boxes, or 1 box + 1 capsule at the tip | dynamic body + **hinge joint with motor**, limits at ±32°. `apps/ship/HingedDoor.cpp` is the template. |
| Plunger | Blender (rod + knob) | box | dynamic + **slider joint**, motor pulls back, spring returns |
| Pop bumper | Blender (cap + skirt) | **capsule**, caps buried above and below the ball's band | static collider + trigger ring; impulse applied on contact |
| Post / rubber | `MakeCylinder` render | capsule, caps buried | static, high bounciness |
| Slingshot | Blender | one angled box + a trigger box just in front | static; impulse on trigger |
| Drop target | Blender | box | **kinematic**, dropped with `Scene::MoveObjectOverTicks` — no joint needed, and it steps and replays correctly |
| Standup target | Blender | box | static + contact callback |
| Spinner | Blender (flat vane) | thin box | dynamic + **free hinge**, no motor; count revolutions from `getAngle()` |
| One-way gate | Blender | thin box | dynamic + hinge with asymmetric limits and a weak return spring |
| Saucer / hole | Blender (rim) | trigger sphere, plus a lip wall ring | trigger; ball is teleported to the subway |
| Subway | nothing (never seen) | nothing | pure code: a timer and a scripted path |
| Kicker / ejector | Blender | trigger box | impulse |
| Ramp (L1) | Blender (floor + two rails) | floor: chain of boxes along the climb; rails: `AddWallPolyline` | static |
| Habitrail | Blender (two wires) | a **half-pipe of boxes**, or simply two rails plus a floor | static |
| Backglass | Blender quad + `TextMesh` | none | render only |
| Lights | — | — | `Light` / `ConeLight`, one per flasher, GI as a few point lights |

---

## 3. The Blender assets

### 3.1 Where they go

The asset convention (`core/File.h`, `docs/asset_layout_plan.md`) is that an asset is named
`<category>/<file>` and resolved against the app's own root first, then `shared_assets`. So:

```
apps/pinball/assets/meshes/
apps/pinball/assets/textures/
apps/pinball/assets/sound/
apps/pinball/assets/shaders/     (if any)
```

`witsand_woolshop_2k.hdr` is at `apps/pinball/assets/textures/` and resolves. It is a good pick for
this: a woolshop interior gives the chrome ball and the metal rails something with structure to
reflect, which is most of what sells a pinball at this scale.

It is doing that job *without being the sky*, which is worth knowing because it is not obvious from
the renderer. `Renderer::UploadCubeMap` binds the cubemap to its own texture unit and leaves it
there; only `DrawSkyBox` is gated on `f_render_skybox`. So stage 0 runs with `f_use_reflections`
**on** and `f_render_skybox` **off**: the metal reflects a room that is never drawn, and the machine
sits against black like the artwork does. `f_use_reflections` is off by default — if the rails ever
render dark, that is the first place to look, because metallic gives up its diffuse term and gets
nothing back when there is no environment.

### 3.2 The .glb files

Follow the `tank.glb` / `ships.glb` pattern: **one file per group, many named nodes inside**, loaded
with `gltfloader.LoadGLTFFile("meshes/table.glb")` and then pulled out by name through the
`AssetManager`.

**`meshes/table.glb`** — the static machine. One node per named object:

```
deck                  the playfield surface, UV-unwrapped flat for the art texture
cabinet_left/right/front/back
apron                 the panel below the flippers
rail_orbit            the top horseshoe
rail_inlane_left/right
rail_chute            the plunger lane divider
rail_bumper_nest
ramp_left             floor + rails as one mesh
ramp_right
habitrail_left        the wire return
habitrail_right
backbox               the backglass housing
backglass             a quad, its own material, for the art
```

**`meshes/parts.glb`** — everything that moves or repeats, modelled at the origin facing +Z:

```
ball
flipper_bat           pivot at the origin
plunger_rod
plunger_knob
bumper_cap
bumper_skirt
bumper_body
post                  a cylinder; the posts are all this one mesh, scaled
rubber                a torus for the rubber ring
target_drop
target_standup
spinner_vane
gate_flap
saucer_rim
lamp_dome             the insert lamps in the playfield
```

**Modelling notes that will save a round trip**

- **Origins matter.** `flipper_bat` must have its origin *at the pivot*, `spinner_vane` at its
  rotation axis, `gate_flap` at its hinge. The engine rotates about the object origin; a bat with a
  centred origin will swing around its middle and look broken.
- **+Z is forward, +Y is up** — the same convention as the rest of this repo. Apply all transforms
  before export.
- **One material per visually distinct surface**, named, because materials come across by name.
  Expect roughly: `playfield_art`, `chrome`, `plastic_red`, `plastic_blue`, `rubber_black`,
  `wood_cabinet`, `lamp_lit`, `lamp_dark`, `backglass_art`.
- **Keep the deck a single flat quad-ish mesh** with a clean UV layout covering `[0,1]²`. The art is
  a texture, not geometry, and that is what makes the playfield cheap to iterate on.
- **No collision geometry in the .glb.** It would not be used (§2.1). If it helps to model the
  colliders as visual boxes while laying the table out, keep them in a separate Blender collection
  and export their transforms as a text dump instead — that dump can be pasted straight into the
  table-building code.

### 3.3 Textures

| Texture | Size | What it is |
|---|---|---|
| `textures/playfield_art.png` | 2048 × 4096 | The whole painted deck: lane arrows, target labels, mission names, inserts, the artwork. The single most important asset on the machine. |
| `textures/playfield_roughness.png` | 1024 × 2048 | The deck is lacquered and glossy; the wear paths are not. Optional, but it is what stops the deck reading as a flat sticker. |
| `textures/backglass_art.png` | 2048 × 1024 | The backbox art. |
| `textures/cabinet_side.png` | 1024 × 2048 | Cabinet flank art. |
| `textures/plastics.png` | 1024 × 1024 | An atlas for the coloured plastic overlays around the bumpers and ramps. |
| `textures/witsand_woolshop_2k.hdr` | — | Already here. Environment / reflections. |

The playfield art is best generated once the *layout is frozen* and not before — it has to line up
with the feature coordinates in §1.5 to within a couple of millimetres or the whole thing looks
wrong. A neutral grey deck with painted-on debug markings is the right thing for stages 0–3.

### 3.4 Sound

`shared_assets/sound/` has four one-shots that will stand in fine for early work. The table
eventually wants its own: flipper up, flipper down, bumper, slingshot, ramp roll, target, drop-target
bank, saucer swallow, subway kicker, ball drain, launch, tilt warning, tilt, and a mission jingle or
two. Fourteen short wavs, well inside the 32-buffer limit, and the 16-voice ceiling is generous for
a table where the worst case is a bumper nest going off during multiball.

---

## 4. Build order

Each stage ends somewhere worth looking at, and each is independently verifiable over MCP.

**Stage 0 — the static scene.** *This is the one you asked for first.* **DONE, 2026-09-13.**
`apps/pinball/` gets a `makefile`, `main.cpp` and `ApplicationPinball` that do nothing but load
`table.glb` and `parts.glb`, place the parts at the coordinates in §1.5, set up the camera, the HDR
environment and the lighting, and render. No physics, no ball, no rules. The point is to **see the
layout** and start moving things around — because every number in §1.5 is a guess until it is on
screen.

> Built from **procedural primitives instead of the two `.glb` files**, which do not exist yet.
> Waiting for them would have meant nobody saw the layout until after it was modelled, and the
> model wants the layout settled first — §3.3 says as much about the playfield art and it is just
> as true of the geometry. Every feature is a plain `Object` with a `Mesh`, so swapping in a named
> node from `table.glb` later is one line each. `apps/pinball/TableBuilder.h` is the polyline
> helper §2.3 asks for; the orbit, every rail and both ramps come off it, and stage 1's collider
> chains will read the same paths.
>
> Also here already, because they cost little and the later stages need them: the tilt slider and
> `pinball_tilt`, the *show colliders* toggle, and a **fixed camera with named shots** that eases
> between them (answer 2) — `table` is the artwork framing, and `upper` / `lower` / `machine` are
> there both as the effect-move mechanism and as a way to inspect a corner of the layout without
> hand-flying anything. Feature names are painted on the deck as world-space `TextMesh` geometry,
> so they survive an `include_ui:false` screenshot and the layout can be reviewed by something with
> no eyes on the monitor. MCP: `pinball_layout`, `pinball_camera`, `pinball_tilt`, `pinball_labels`.
>
> A fifth shot, **`orbit`**, is a free debug camera rather than a framing: middle-drag to swing
> round the machine, shift+middle-drag to slide the pivot, wheel to zoom, or place it exactly with
> `pinball_camera`'s `yaw` / `pitch` / `distance` / `pivot`. It exists because **a height
> relationship cannot be judged from above** — a ramp that clears a bumper cap by 0.13 and a ramp
> resting on that cap look identical in every fixed shot, and this table has ten of those
> relationships. Entering it lights a lamp at the camera and drops a plain dark card behind the
> machine, both of which it takes away again on the way out; without them a side-on view is a black
> band, because the key light rakes down the deck and the window is transparent behind it.

**Stage 1 — ball and flippers.** Deck collider, outer walls, one ball, two flippers on hinge motors,
the plunger. Tilt slider, tunnel guard, speed clamp. The MCP tools `pinball_flipper`,
`pinball_plunger`, `pinball_place_ball` and `pinball_telemetry`. **The whole game lives or dies on
how the flippers feel**, and the flippers are cheaper to tune when they are the only thing on the
table. Expect to spend real time here.

**Stage 2 — the scoring geometry.** Pop bumpers, slingshots, drop targets, standups, the spinner,
all the rails and lanes, every switch reporting through a `pinball_switch_log` tool. Still no rules:
the table scores nothing, it just registers hits. An agent can now fire balls at things and confirm
each one is reachable and each switch fires.

**Stage 3 — the third dimension.** Both ramps, the habitrails, the saucers and the subway. This is
where the tunnel guard earns its keep, because a ball on a ramp is the fastest it ever goes.

**Stage 4 — the rules.** Score, bonus, missions, ranks, multiball, ball saver, skill shot, nudge and
tilt. All in a `Pinball.h` / `Pinball.cpp` pair that holds **no engine types**, the way
`tetris/Playfield.h` and `breakout/Field.h` do — so the rules can be exercised with no window open
and the mission state machines can be tested exhaustively.

**Stage 5 — the presentation.** Playfield art, backglass, insert lamps, flashers, the sound set, the
score display, camera work.

---

## 5. Open questions

Worth settling before stage 0, because each one moves numbers in §1.5:

1. **Name and theme.** The layout above is a Space Cadet homage but the mission names, ranks and art
   are ours to write. What is the machine called?
2. **Camera.** Fixed 3/4 view of the whole table, like the original? Or a camera that follows the
   ball and can be pulled back? The original's fixed view is part of its character, but this engine
   can do better and it costs nothing to support both.
3. **Two flippers or four?** Many machines have a second, upper-left flipper feeding a ramp. It is
   one more hinge joint and it makes the table considerably more interesting — but it also needs a
   shot worth making with it.
4. **How faithful to Space Cadet?** Feature-for-feature, or the same *feel* with a layout that suits
   a 3D renderer better — real ramps that read as ramps, for instance, where the original was
   essentially flat.

## 6. Answers

1. The ranks can stay the same. There is artwork useable as a style guide, not layout. It names it Orbit Outpost. A rocket with moon base theme.
2. A fixed camera like the original definetely to have a view like the artwork image. On special effects, or when entering an area, it would move there with some extra perspective. Definitely a nice idea.
3. 2 + 1 flipper will be more interesting.
4. Doesn't need to be an exact copy. Same feel with a 3D layout and 3D features. The goal is to use and explore as many of the engine's exposed features.