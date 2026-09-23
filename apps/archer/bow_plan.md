# Archer Bow Plan

Equipping a bow to the character, bending it as it draws, aiming it, and a quiet test range to
tune all three in — written so that whatever comes out of the range works unchanged in the game.

Everything measured below was read out of `apps/archer/assets/meshes/archer.glb` and the engine
source on 2026-09-22 rather than assumed. **§1-§4's placement numbers describe the export before
2026-09-23**, when the props were re-exported as their own assets (origin at the attach point,
facing +Z) and `Bow_Draw` and the sockets were dropped - they explain how the grip USED to be
recovered. What is current is §4, *The target*. The asset moved twice while this was being written, so
re-measure before trusting any number here: `python tools/gltf_clip_dump.py <glb>` for the clips.

---

## 1. What the asset already contains

More than half the job is already in the file, which changes where the interesting decisions are.

```
node  archer        mesh archer_mesh           23699 verts  skin archer_armature (65 joints)
node  bow           mesh ...quad.022              4 prims   skin None   morph target ['Drawn']
node  arrow         mesh ...quad.024              3 prims   skin None
node  socket_bow                                 no mesh   (removed in the 2026-09-23 re-export)
node  socket_arrow                               no mesh   (removed in the 2026-09-23 re-export)

clip  Standing_DrawArrow   1.067 s   in place, net yaw -29.2     (the CHARACTER's half)
clip  Bow_Draw             1.100 s   animates `bow`: translation + weights, 13 keys each
```

Three things follow from that and each one matters:

- **The bow and the arrow are separate nodes.** So the arrow can leave the bow without any mesh
  surgery, which is what `arrow_objects[ARROW_MAX_LIVE]` in the app is already waiting for.
- **The bow carries one morph target, `Drawn`.** One float bends the limbs and pulls the string,
  and the string cannot disagree with the limbs because they are the same shape key.
- **`Standing_DrawArrow` and `Bow_Draw` are a matched pair** — 1.067 s and 1.100 s, the character's
  half and the bow's half of one action, exported separately because they target different objects.

### The sockets are already in the hands — at frame 0 of the draw

This looks wrong until it is measured, and then it is exactly right. In BIND pose the rig is in an
A/T-pose with the hands out at x ±0.357, while the items sit centred in front of the body:

```
mixamorig:LeftHand    bind  ( 0.3573,  0.7452, -0.0055)
mixamorig:RightHand   bind  (-0.3574,  0.7457, -0.0038)
bow / socket_bow      rest  ( 0.1008,  0.4661,  0.2244)
arrow / socket_arrow  rest  (-0.0739,  0.4748, -0.1930)
```

So against the bind pose the bow is 0.51 from the nearest hand and the sockets look like arbitrary
markers. They are not. Evaluating `Standing_DrawArrow` with forward kinematics, at **t = 0**:

```
mixamorig:LeftHand    t=0   ( 0.101,  0.466,  0.224)   ==  bow   / socket_bow
mixamorig:RightHand   t=0   (-0.074,  0.475, -0.193)   ==  arrow / socket_arrow
```

Identical to three decimals, both of them. **The items were placed in the hands at the first frame
of the draw clip, and that pose is their rest transform.**

Two things fall straight out of that, and they were the two open questions:

- **The bow is in the LEFT hand; the right hand draws.** Not a choice to make - it is authored.
  (Path lengths over the clip agree: the left hand travels 1.94 with the bow, the right 1.53 pulling
  back, and the left forearm is the steadier of the two at 1.39.)
- **The grip offset IS in the file**, just expressed against a posed frame rather than against bind.
  Section 4 is about recovering it rather than inventing it.

---

## 2. Three mechanisms the engine already has

None of this needs new engine code, which is worth knowing before designing around imagined limits.

### Attaching to a bone works, and is the documented intent

`Bone : public virtual Object` (`core/skeleton/Bone.h`), animation poses bones by writing their
LOCAL Object transform (`ObjectAnimation.cpp:425` — `bone->SetPosition` / `bone->SetRotation`), and
`Object::GetWorldTransformScaleMatrix` composes through the parent chain (`Object.cpp:583`). The
renderer walks children of visible objects. So:

```cpp
archer_model->FindBone("mixamorig:LeftHand")->AttachChild(bow);
```

makes the bow ride the hand through every clip, with nothing else written.
`ApplicationBomber.cpp:989` already says so in a comment, having deliberately NOT done it for a
shield: *"Attaching a held item - a weapon, a lamp - is the case that would want a bone instead."*

**A physics object cannot be a child** — `AttachChild` and `AddPhysics` both call `debug->Fatal`
rather than allow it (`Object.h:506`). The held bow and the nocked arrow must carry no rigid body.
A loosed arrow is a different object and may have whatever it likes.

### Morph targets animate, end to end

`GLTFLoader` imports `ANIM_TARGET_PATH_WEIGHTS` (`GLTFLoader.cpp:1069`), the keyframe carries
`shapekey_weights` / `f_shapekeys` (`ObjectAnimation.h:171`), and the applier calls
`target->SetShapekey(index,weight)` (`ObjectAnimation.cpp:113`). `NUM_MORPH_FACTOR_SLOTS` is 4 and
the bow needs one.

### Clips bind to objects BY NAME, over the whole subtree

`Animation::LinkObjects(root)` calls `root->GetAllSubObjects()` and matches `object->name` against
`ObjectAnimation::target_name` (`ObjectAnimation.cpp:68`). The name comes straight off the glTF
node. **So an Object named `bow`, attached anywhere under the skeleton, is found and driven by
`Bow_Draw`** — including its shape key. That is a genuinely useful property and it is also a trap;
see the next section.

### Scenes switch properly

`Application::RequestActiveScene(Scene*)` is safe from any thread, records the request, and
`ApplyPendingSceneSwitch` does the swap on the physics thread at the top of a pass
(`Application.cpp:995`). Scenes are never freed, deliberately, so the render thread drawing the
outgoing scene for one more frame is benign rather than a use-after-free. Each `Scene` owns its
`camera`. A second scene is a first-class feature here, not something to bolt on.

---

## 3. The decision that matters: parent to a bone, do not play the bow's translation

`Bow_Draw` animates the bow's **translation**. That is the bow following the hand, baked, for that
one clip. It is tempting to just play it, and it would look perfect — for 1.1 seconds.

The problem is the other twenty-nine clips. Nothing animates `bow` in `Walking`, `Running_Fast`,
`Jump_ToAir`, `Hanging_Braced`, `Kick_Front` or any of the rest, so a bow driven by clip translation
**freezes in mid-air the moment the draw ends** and stays there while she runs off. Authoring bow
tracks into all thirty clips is the alternative, and it means every future clip has to remember to
carry them.

So:

> **Parent the bow to the hand bone and let the skeleton move it. Use the `Drawn` weight for the
> bend. Do not apply `Bow_Draw`'s translation channel.**

`Bow_Draw` does not become useless — it is the **reference for what the bend should look like**, and
`Standing_DrawArrow` remains the character's pose for drawing. What changes is that the bow's
POSITION comes from the rig, every frame, in every clip, for free.

**The trap:** because `LinkObjects` binds by name over the whole subtree, naming the attached object
`bow` is exactly what makes `Bow_Draw` grab its translation and fight the parenting. Either name the
attached Object something else (`bow_held`), or strip the translation channel. Naming it differently
is one line and cannot be forgotten later; stripping a channel is a thing someone re-adds by
re-exporting.

---

## 4. Equipping: recovering the grip

> **Corrected 2026-09-22.** An earlier draft of this section said the items *must* be re-parented to
> the hand bones in Blender. That was wrong, and it was wrong in the expensive direction: it asked
> for an asset round-trip to produce information the file already contains.

The grip is expressed against `Standing_DrawArrow` frame 0 rather than against bind (§1), so it has
to be recovered rather than read straight off the node. Recovering it is
`item_rest_world * inverse(bone_world_at_t0)`, and the answer is this:

```
bow   in mixamorig:LeftHand    translation (0, 0, 0)   rotation ( 0.4239, 0.3925, 0.8039,-0.1414)  196.25 deg
arrow in mixamorig:RightHand   translation (0, 0, 0)   rotation ( 0.7071, 0.0000, 0.0000, 0.7071)   90.00 deg
socket_bow                     identical to `bow`
```

**The translation is exactly zero for both.** The items were snapped to the hand bones, so an item's
rest position IS its bone's position at that frame, and all that separates them is a fixed rotation
- 90 degrees about X for the arrow, which is an axis-convention turn rather than an artist's nudge.

So: **attach to the bone, set the local rotation, leave the translation at zero.** No asset change.

### Why not re-parent in Blender anyway

It would make the relationship explicit in the file instead of inferred, and remove the load-time
dependency on one clip. Both are real. Against that:

- **It is an asset round-trip to obtain a number that is already obtainable**, and the number is
  about as simple as a transform gets.
- **It walks into an untested loader path.** `GetSkeleton` walks `skin.joints`; a MESH node parented
  under a joint may or may not survive that, and §9 lists it as an open question. Re-parenting
  trades a measured, known-good situation for one nobody here has run yet. That is the wrong way
  round for a step whose whole purpose is to de-risk.

If a socket BONE is added later for other gear - and it probably should be, since
`INPUT_ARCHER_KNIFE` is mapped with no rules behind it and a quiver is obvious - it makes this
simpler still, because a bone always exports as a joint and needs none of the above. That is an
argument for doing it when the second item arrives, not now.

### The fragility this leaves, and the cheap guard against it

The grip is defined by `Standing_DrawArrow` frame 0. Re-pose that frame - add a wind-up, change the
stance - and the grip silently moves with it.

**The zero translation is what makes that checkable.** It is not a coincidence, it is the signature
of the item having been snapped to the bone, so it is an invariant worth asserting at load:

> for each equipped item, `|item_rest_world - bone_world_at_reference_frame|` must be under a
> small epsilon; log loudly if not.

If that ever fires, the authoring assumption has broken and the message says so - rather than the
bow appearing somewhere odd and the search starting at the attachment code. Same shape as the
terrain's `worst_dip`: one number, measured at build time, that can only mean one thing.

### The target: props as their own assets, sockets in the rig (agreed 2026-09-23)

Everything above RECOVERS placement the file never declared, and the recovery has already needed
two patches: the position is checked at frame 0 of `Standing_DrawArrow`, the bow's rotation is
re-taken at its LAST frame because the hand turns 50 degrees and the exported bow does not, and
the nocked arrow still points backwards at full draw (§9). Each is a symptom of the same thing -
the props were placed in one pose of one clip rather than attached to anything.

Compare the animations, which are measured too, and correctly: stride speed, foot phase, contact
frames and the kick's strike are properties of motion the artist really did author, so reading
them is reading the asset. A grip recovered from one frame is not that - it is guessing at a
relationship that belongs in the file.

**The convention, for the bow, the arrow and every prop after them (knife, quiver):**

1. **DONE 2026-09-23 (export of 19:24).** Each prop is a mesh with its own origin, and **where it
   stands in the Blender scene is ignored** - the same rule as every asset here. The export still
   carries the props in `archer.glb`, beside the character; that is fine, since the node transform
   is not read. `socket_bow`, `socket_arrow` and the `Bow_Draw` clip are gone from the file.
2. **DONE.** **Origin at the attach point.** The bow's origin is the grip (the handle straddles
   it); the arrow's is its nock end, and it runs z −0.017 .. 0.519 (the fletching reaches a little
   behind the nock).
3. **DONE.** **Props face the way the character faces.** In Blender that is toward the camera in
   front view, **Blender −Y, which the glTF exporter writes as +Z** - the rig's own forward. So the
   arrow's point is at +Z, the bow's string is behind its grip at −Z, the limbs run along Y.
   (An earlier draft of this item said −Z, reasoning from the engine's camera forward. What
   matters is that prop and character agree, and in the file both face +Z.)
4. **Socket bones in the rig** - `socket_bow` as a child of `mixamorig:LeftHand`, `socket_nock`
   as a child of `mixamorig:RightHand` - posed so a prop at identity inside the socket sits right
   in the hand. The socket's orientation IS the grip, authored once, animated by the rig in every
   clip. **Add them at the END of `skin.joints`** (65 → 67) so no existing bone is renumbered -
   the renderer lays bone matrices out by that index (§9) - and check the bone-matrix capacity.
5. **In Blender, parent the bow to its socket while animating the draw.** That is what fixes the
   50-degree problem where it starts: the artist sees the bow tip over as the wrist turns and
   corrects the wrist, instead of the code choosing which frame to be wrong in.

**What the code does now (items 1-3):** `Bow::Build` reads no node transform at all. The bow hangs
off `mixamorig:LeftHand` with zero offset - its origin IS the grip - and a grip rotation chosen so
it stands as authored (upright, facing her forward) at the last frame of `Standing_DrawArrow`.
That rotation is the one piece still taken from a clip, and item 4 is what removes it. The old
zero-offset assert is gone, since there is no snapped placement left to check.

**The nocked arrow is a CHILD OF THE BOW, at the string's nock - no look-at needed.** An earlier
draft of this section proposed a look-at from the draw hand toward the grip. The new export makes
it unnecessary: both props face +Z, so **inside the bow the arrow's rotation is identity**, and
the only thing that moves is the nock - which the bow's own `Drawn` shape key already knows:

```
Drawn moves 20 string vertices (the middle of the string), nothing else
nock at draw 0    bow-local (-0.0005, 0.0030, -0.0938)
pulled by         (0.0000, 0.0000, -0.2719) at draw 1      0.0 deg off -Z
```

`Bow::MeasureNock` reads that off the mesh at load (the vertices the key moves furthest, averaged)
and `Bow::SetDraw` puts the arrow's origin at `rest + pull * draw01` - the same number that bends
the string, so the two cannot drift. It follows the bow through every clip and will follow it
through the aim override for free, because it is part of the bow. (Core gained one read-only
accessor for this, `Mesh::GetMorphVertices`.)

Two numbers are logged at every start, each meaning one thing:
- **the pull's angle off −Z** - 0.0 now; anything over 5 degrees means the bow was exported not
  facing her forward, and the arrow would sit crooked on the string;
- **the nock gap** - how far `mixamorig:RightHand` is from the nock at full draw: **0.058 rig,
  0.117 world**. Nothing corrects this (the hand does not hold the arrow any more), which is
  exactly why it is printed: it is how well the draw pose and the bow agree.

**Still to come from item 4:** with a socket bone the bow's grip stops depending on one frame, and
the flight arrow's axis-and-tip search in `ApplicationArcher::BuildFlightArrowMesh` becomes a
fixed rotation (+Z to +X). The search still finds the right end today - the tip is the end
farther from the origin, and the origin is the nock.

---

## 5. Driving the draw from the rules, not from clip time

This is the same principle the terrain work turned on, and it is worth stating in the same terms:
**the simulation is authoritative and the visual is pinned to it**, never the reverse.

`Stage` already owns the whole bow: `bow_mode`, `draw_ticks`, `aim_deg`, `facing`, `BOW_DRAW_TICKS`
(36 ticks = 0.6 s), `BOW_MIN_POWER`, and `PredictArc()`. `Stage.h` is explicit that the arc drawn on
screen IS the arrow's flight, tick for tick, because *"a promise drawn on screen has to be kept."*

So the bend is:

```cpp
bow->SetShapekey(0, draw_ticks / (float)BOW_DRAW_TICKS);
```

and **not** the clip's own weight track. The reasons are not stylistic:

- `Bow_Draw` is 1.100 s and `BOW_DRAW_TICKS` is 0.600 s. They already disagree by 83%. Playing the
  clip means the bow reaches full bend nearly half a second after the shot reaches full power.
- `draw_ticks` is the number that sets the arrow's speed. Deriving the bend from it means a
  fully-bent bow and a full-power shot are the same fact, and cannot drift when either is tuned.
- A draw that is interrupted — she gets hit, she grabs a ledge, the player taps rather than holds —
  is a state the rules already handle. A clip playing on its own timeline is a second state machine
  that has to be kept in agreement with the first.

The clip is still worth having as the thing an artist tunes the *shape* of; the runtime just reads
the shape at a time the rules choose, rather than letting the clip run the clock.

### Where the arrow leaves

`BOW_SHOULDER_UP` (0.35) and `BOW_SHOULDER_FWD` (0.40) said where the arrow is born, relative to the
archer's centre. Once there is a real bow those numbers and the visible nock have to agree, or the
aim arc starts in mid-air beside the bow. (**Done 2026-09-23**, and the constants are now the
arrow's anchor and length - `BOW_NOCK_FWD`/`BOW_NOCK_UP`/`ARROW_LENGTH`; see §8 item 6.)

**Fix it by moving the model, or by changing those two constants once and re-running `make rules`.
Not by having `Stage` read a bone.** `Stage.h` names no engine type on purpose, and the moment it
reaches into the skeleton the rules test stops testing the game. Same trade as pinning the terrain's
top face to the collider rather than moving the collider to the terrain.

---

## 6. Aiming

`aim_deg` runs -85..+85 relative to facing. The visible bow has to point along it.

**The full mechanism now lives in `animation_plan.md`, *Step 3 — aim pitch*** - it is a pose
problem, so it sits with the other pose steps. In short: a post-pose override in
`ArcherModel::ApplyAnimation`, spread over the spine and arms, rotating by `aim_deg` minus the draw
pose's own measured aim (−2.2 degrees, level), about the world play-plane normal, faded in by an
`aim_weight` only while drawing, and checked every tick against `Stage::AimDirection()`.

Two details that will bite, both resolved there:

- **The axis is the WORLD play-plane normal, converted into each bone's parent space.** Not a
  model-space axis: the model is yawed (`model_yaw_deg: 90`), the draw clip carries −29.2 degrees
  of net yaw, and at full draw the hand line is 9 degrees off her forward. Rotations about one
  shared world axis also add exactly however they are split over bones, which is what makes the
  check tight.
- **Facing left mirrors it - and that is only the sign of that world axis.** `aim_deg` is already
  relative to facing, so flipping the axis sign with `facing` is the whole of the mirroring. (An
  earlier draft read this as needing more; it does not, once the axis is in world space.)

`Bone::IKExtend(target, depth, decay, chain)` exists and is the upgrade path — worth it only when
the bow hand needs to actually land on something, such as a two-handed grip that must stay welded to
the bow while the spine twists.

---

## 7. The test range

A second `Scene` with its own camera, walk-only, enclosed, a few targets. The engine supports this
properly (§2), so the question is not "can we" but "what has to be shared so nothing drifts".

### What makes the logic transfer

Not the scene. **Where the code lives.**

> Put the equip/bend/aim code in a `Bow` beside `Puppet`, reading **only** `bow_mode`, `draw_ticks`,
> `aim_deg` and `facing` off `Stage`. If it cannot see anything else, it cannot know which scene it
> is in, and it transfers by construction rather than by discipline.

That is the whole guarantee, and it is the same shape as `Stage.h` naming no engine type.

### What the range actually differs in

Three things, each of which is worth having on its own:

| difference | how | also useful for |
|---|---|---|
| level content | its **own `Stage` instance** with a range level | `make rules` can assert the range layout too |
| camera | a per-scene camera mode — fixed framing or a wide deadzone | cutscenes, a boss arena |
| walking only | an **input mask** in `GatherInput` zeroing jump/kick/rope | menus, cutscenes, stun |

`Stage` names no engine type, so a second instance costs nothing and is independently testable. The
input mask is worth doing as a mask rather than a second controller: "the game is not accepting
these inputs right now" is a state this game will want anyway.

### Camera

The main camera trails with a lerp and drags the sun with it (the shadow ortho is 22 units and the
level is 84). A range camera wants to be still: pick a fixed look-at for the range and let her walk
across the frame. That also makes screenshot comparison over MCP meaningful, which the terrain work
showed is worth a lot — but note the camera distance is a member the mouse wheel writes, so bracket
any measured screenshot with `camera_get`.

---

## 8. Build order

1. ~~**Decide the bow hand.**~~ **Answered by §1: the bow is in the LEFT hand, the right draws.**
   It is authored, not a choice — the items sit exactly on the hands at `Standing_DrawArrow` t = 0.
2. ~~**Blender: re-parent the items.**~~ **Not needed** — see the correction in §4. The grip is
   zero translation plus a fixed rotation, recoverable from the file. No asset change is blocking.
3. ~~**Equip and follow.**~~ **DONE 2026-09-22** — `apps/archer/Bow.{h,cpp}`, called from
   `ApplicationArcher::BuildBow`. The grip comes out of the file exactly as §4 predicted:

   ```
   Equipped 'bow_held'     on mixamorig:LeftHand:  grip (0.4239,0.3925,0.8039,-0.1414)  offset 0.00000
   Equipped 'arrow_nocked' on mixamorig:RightHand: grip (0.7071,0.0000,-0.0000,0.7071)  offset 0.00000
   ```

   Both quaternions match the offline calculation to four decimals, which also confirms the
   engine's rotation convention is the one §4 assumed. The bow stays welded to the hand across a
   run cycle.

   **The zero-translation assert earned its place on the first run**, reporting 0.53759 instead of
   0. Not a bad grip: `BuildArcherModel` has already scaled the skeleton to stand
   `ARCHER_MODEL_HEIGHT` tall (about 2.02x) by the time `BuildBow` runs, so `GetWorldPosition` was
   being compared against `GetNodePosition`, which is in the rig's own units - 0.101 against
   0.101 x 2.02, which is exactly the 0.53759. **Rotation escaped it only by luck**: a uniform
   scale does not rotate anything and the skeleton happened to be unrotated at that moment.
   `Bow::Build` now neutralises the skeleton's whole transform for the measurement and restores it
   after, which removes all three components at once and cannot be broken by moving the call.
4. ~~**The range scene.**~~ **DONE 2026-09-23** — a second `Scene` ("Range") with its own
   `Stage` (`STAGE_LEVEL_RANGE`), switched with the `scene_set` MCP tool or the Engine panel's
   Scenes list. Two departures from §7, both deliberate:
   - **Not walk-only.** The input mask was dropped; the range's one rule is a lower top speed,
     `ARCHER_RANGE_RUN_SPEED` 3.0 against 9.0, which is Running_Slow's own pace. Every verb still
     works, so the two levels show the same character with two feels - including the jump, which
     from a jog is a different jump.
   - **Not a still camera.** It follows her slowly (`RANGE_CAMERA_SMOOTH`), in both camera modes.
   Also on the range: floating targets in an arch that take gravity on their third hit, crate
   pyramids, and the hit counter. The arrows in flight are the file's arrow now, not boxes -
   `ApplicationArcher::BuildFlightArrowMesh`.
5. ~~**`SetShapekey(0, draw_ticks / BOW_DRAW_TICKS)`**~~ **DONE 2026-09-22** —
   `ApplicationArcher::SyncBow`, beside the other `Sync*` calls on the physics thread
   (`SetShapekey` writes one float and touches no GL). Verified at full draw: `draw_ticks` 36 of
   36, string pulled back, limbs flexed. **Hiding the nocked arrow on loose: DONE 2026-09-23** -
   hidden the tick an arrow is loosed, shown again when the next draw starts (a one-tick tap ends
   with the hand empty). See `f_arrow_nocked`. It mattered once the flying arrows became the real
   mesh: the one left in her hand read as a second arrow.

   **This needed a core engine change first**, and it is the reason this step was not the one-liner
   it looks like. The app died on startup with

   ```
   fatal GLTFLoader : We don't support sparse accessors in GLB files yet.
   ```

   A glTF accessor may store only the elements that DIFFER from a base, as indices plus values, and
   **Blender exports morph targets that way** - the `Drawn` key's four accessors declare 30, 125,
   336 and 50 elements and override 1, 1, 1 and 20 of them, because a bow bends at a handful of
   vertices. All four carry **no `bufferView` at all**, which is legal and means the base is
   implicitly zero. `GLTFLoader.cpp` had a `//TODO: Make it do` and a `Fatal` there.

   Implemented as `GLTFLoader::ResolveVec3Accessor`, which materialises an accessor - sparse or not
   - into a dense array, plus `GetMorphVertexAt` to read from it. The morph path resolves each
   attribute once instead of indexing a bufferview per vertex, because a sparse accessor has no
   single bufferview to index. An absent attribute now yields zeros rather than an error, which is
   correct: a morph target is a DELTA, so zero is its identity.

   This is a **core** change and every app links it. Nothing else in the tree used morph targets,
   so nothing else can have regressed, but it is worth knowing it is there.
6. ~~**Aim override from `aim_deg`**~~ **DONE 2026-09-23** — animation_plan.md, *Step 3 — aim
   pitch*: 0.00 degrees between the drawn bow and `aim_deg` across ±85, both facings. The nocked
   arrow is on the string for the whole draw and only then (`SyncBow`).

   **Where the arrow leaves: DONE the same day - the arrow is a SEGMENT.** It has an anchor (the
   nock), a direction (the aim) and a length, and its first sweep starts at the anchor. In Stage:
   `BOW_NOCK_FWD` 0.11 / `BOW_NOCK_UP` 0.54 (the nocked arrow's origin, measured over aim
   −80..+80 with the override on; it moves about 0.2 across that range) and `ARROW_LENGTH` 1.05
   (nock to point, 0.519 x 2.020 - the app warns at start if the mesh disagrees).
   `MuzzlePosition()` = `AnchorPosition()` + length * aim; `Loose` leaves the anchor in
   `prev_pos`, and the first step of `TickArrows`, of `PredictArc` and of the app's prop raycasts
   all sweep from it - so the arc is still the flight, tick for tick.
   - Measured: the loosed arrow spawns within **0.03** of where the nocked arrow's tip was (it
     used to appear 0.57 ahead of it).
   - Pressed against the test bay's 0.5-wide pillar, a level shot sticks in the **near face**
     (x −34.57 against a face at −34.55). The new rules check was run against the OLD code first
     and failed as expected - the arrow buried itself 0.29 inside the pillar.
   - Left as it is: before the loose, the nocked arrowhead still pokes through a post she is
     pressed against. That is the pose - the bow is a meter ahead of her chest - not the rules.
7. ~~**The nocked arrow**~~ **DONE 2026-09-23** — not the look-at first planned: with the props
   re-exported to the convention, the arrow is a child of the bow at identity rotation, placed at
   the string's nock measured off the `Drawn` shape key (§4, *The target*). Verified in-app at
   draw 2, 17 and 36 of 36: nocked on the string at every fraction, pointing through the grip, the
   head just past the bow hand at full draw. Fixes §9's backwards arrow.

   **What the screenshots also showed, neither caused by this step:**
   - **The string is pulled before a hand reaches it.** The bend is linear in `draw_ticks`, but
     `Standing_DrawArrow` spends its first half with the right hand up by her head (fetching the
     arrow) and only reaches the string at the end - so at half draw the string is half back with
     no hand on it. Worth fixing by making the VISIBLE bend follow the hand: measure, per keyframe
     of the draw clip, the right hand's distance from the bow's grip, and map the bend from that,
     while the rules keep `draw_ticks` for power. The same measure-per-keyframe shape as the kick.
   - **Outside the draw the bow is held sideways** - low and horizontal at idle - because its grip
     is one rotation chosen for full draw. That is item 4's to fix (the socket bone), as expected.
8. **Blender: props as separate assets** — **done** (items 1-3 of §4, *The target*). **Socket
   bones in the rig** (item 4) — **added 2026-09-23 (export of 20:09)**: `socket_bow` under
   `mixamorig:LeftHand`, `socket_quiver` under `mixamorig:Spine`, 67 joints. Findings:
   - **No baking needed for the engine to pick them up.** The exporter writes channels for every
     bone in every clip, so all 28 clips carry both sockets - at their rest value, zero deviation.
     They ride the hand and the spine through the hierarchy. A clip only needs keys on a socket to
     move it RELATIVE to its bone (e.g. countering the draw's wrist turn), and since every clip
     carries the channels, such keys cannot leak into the next clip.
   - **The axis rule is confirmed on real data.** The bow, posed on its socket with a constraint
     in Blender, exported at exactly `socket * (+90 deg about X)` - 0.00 degrees apart. That turn
     is `BOW_SOCKET_AXIS_FIX` in `Bow.h`.
   - **The sockets' orientation did not make it into the file.** In the REST pose `socket_bow` is
     exactly axis-aligned (bow up (0,1,0), front (0,0,−1)) - a fresh bone, not a posed one - so
     the posing was done in pose mode and not exported. In the game at full draw the bow's up is
     (0.93,0.33,0.17) and its front (0.32,−0.94,0.12): turned 90 degrees, arrow pointing at the
     floor. The quiver's socket disagrees with where the quiver object was placed by about 80
     degrees, and in the game it lies horizontally through her waist.
     **Fix, Blender side:** pose each socket so its prop (on Copy Transforms) looks right, then
     `Pose > Apply > Apply Selected as Rest Pose` on the two socket bones only (they deform
     nothing and have no children, so nothing else moves), clear their pose transforms, and
     re-export. Judge the bow at the LAST frame of `Standing_DrawArrow`.
   - **Done and verified (export of 20:26),** rest taken from the last frame of the draw. At full
     draw the bow's up is (−0.14,0.99,−0.05) and its front (0.11,0.07,0.99) - upright and forward
     within about 8 degrees - the nock is 0.048 rig from the drawing hand, and both props match
     their sockets in the export to 0.00 degrees. The quiver sits diagonally on her back and
     follows her through the draw. **What is left is the expected cost of one rotation for every
     clip:** at idle the bow is held horizontal, limbs along her forward axis, and the nocked arrow
     points at the floor. Two independent answers: key the socket in the clips where it matters
     (idle, the run cycle), and/or show the nocked arrow only while drawing.
9. **Code: attach to sockets** — **done 2026-09-23** for the bow and the quiver (`Bow::Build`):
   zero offset plus `BOW_SOCKET_AXIS_FIX`, nothing measured; the full-draw grip stays as the
   fallback for a file with no `socket_bow`. `Bow::Build` logs the bow's up and front at full
   draw, which is the number to read after a re-pose. Still to do: delete the fallback once the
   sockets are settled, and simplify `BuildFlightArrowMesh` to the declared axis.
---

## 9. Open questions

Recorded because they are genuinely unknown, not rhetorical:

- **Does `GetSkeleton` load a mesh node parented under a joint?** It walks `skin.joints`; a non-joint
  child may be skipped. If it is, the bow has to be loaded separately with `GetMeshFromNode` and
  attached by hand — which is fine, and is what step 3 does anyway. Worth knowing before blaming the
  export.
- **Adding the socket bones changes `skin.joints` from 65 to 67** (two sockets, now planned - §4,
  *The target*). A bone's index is its position in `skin.joints`, and the renderer lays bone
  matrices out by that index. Adding them at the end should be harmless; adding one in the middle
  renumbers everything after it. Check the bone-matrix capacity before assuming 67 fits.
- **The bow has 4 primitives and 4 distinct materials; the arrow has 3.** `NUM_MATERIAL_SLOTS` is 4,
  so the bow is exactly at the limit as its own Object and has no room for a fifth. This is a real
  argument for keeping the bow a separate Object rather than ever merging it into the skinned mesh,
  where it would have to share four slots with `elf_archer_material` and `shirt`.
- **`Standing_DrawArrow` carries -29.2 degrees of net yaw.** Whatever the other clips needed for yaw
  handling, this one needs too, and a draw that slowly rotates her is what it looks like when it
  does not get it.
- ~~**The bow bends but the character does not draw it.**~~ **Addressed 2026-09-22, temporarily.**
  `Puppet::Choose` now selects `CLIP_DRAW` in the idle branch only - drawing while standing still -
  so the pose and the bend agree. It is fitted to the rules' window at 1.778x (1.067s clip into
  BOW_DRAW_TICKS' 0.600s) and holds its last frame, which is the full-draw pose.

  **THIS IS A STAND-IN FOR THE MASK LAYER AND SHOULD BE DELETED WHEN THAT LANDS**, not extended to
  cover running. Standing_DrawArrow is whole-body, so it is only correct while she is standing;
  the branch is confined to the idle case precisely so a draw at a run behaves exactly as it did
  before and nothing that worked can regress. See the TEMPORARY note in `Puppet::Choose`.

- **THE NOCKED ARROW POINTS THE WRONG WAY AT FULL DRAW** - back over her shoulder rather than
  forward along the bow. This is the one thing in the slice that looks wrong, and the cause is
  structural rather than a bad number: the arrow's grip is derived from ONE frame (the reference
  pose, §4) and then held rigid, while a real hand RE-GRIPS the arrow during the draw - it is
  pinched differently when pulled from the quiver than when nocked on the string.

  A rigid parent cannot express that. **FIXED 2026-09-23 by parenting the arrow to the bow** (§4,
  *The target*), at the string's nock as measured off the shape key. That was the first of the two
  candidates listed here, rejected at the time because "it cannot follow the string" - it can, once
  the nock's movement is read off the same shape key that moves the string.
- **`GLTFLoader`'s debugger is constructed at `DEBUG_WARN`**, so everything it logs at Info - the
  morph target list, the new sparse-accessor line - is invisible by default. Worth knowing before
  concluding from a silent log that a path did not run; it is why the sparse fix had to be
  confirmed by looking at the bent bow rather than by reading stderr.
