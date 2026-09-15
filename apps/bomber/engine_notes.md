# bomber: engine gaps and workarounds

Things `apps/bomber` had to work around, or would have been easier with. Kept next to the app that
hit them rather than in `docs/`, because every one of them is a note about *this* app's code as much
as about the engine — if one gets closed, the workaround here is what should be deleted.

Each entry says **what is missing**, **what bomber does instead**, and **what it costs**. Nothing
here is urgent; most cost a comment rather than a bug.

---

## 1. A node's transform is silently discarded, and its scale cannot even be read

Two halves of one problem, and the second is the one that bites.

`GLTFLoader::GetMeshFromNode` uses the node **only to find the mesh index**. Its translation,
rotation and scale are never applied to the vertices and are never handed to the caller either —
so a model that is only correct once its node transform is applied loads silently wrong. There is
no warning, because nothing in the loader considers it unusual.

`GetNodePosition` and `GetNodeRotation` are public and take a node name, so an app can at least ask
about two of the three. There is **no `GetNodeScale`**, so the third cannot be read at all without
going through `tinygltf` directly.

**What bomber does:** relies on an export convention — every node at scale 1, no rotation, and only
a Y translation that means "how high this piece stands". The user owns that convention and fixes
the file when it is broken.

**It has already happened once.** `wall_wood` was first exported with scale (5.53, 6.20, 7.47) and
a 60° rotation. The app would have drawn a 0.17-unit crumb lying on its side, with nothing in
stderr and nothing in the loader to say why. It was caught by **dumping the GLB's JSON chunk with a
throwaway script and printing every node's TRS and world size** — not by the engine, and not by
looking at the screen, where it would have read as "the new asset didn't load".

**Cost:** an export mistake becomes a visual puzzle rather than a message. Two fixes, either of
which would be enough: a `GetNodeScale` beside the other two accessors (about fifteen lines), or —
better — one `debug->Warn` in `GetMeshFromNode` when the node it is reading carries a non-identity
transform that is about to be thrown away. The second is three lines and turns a silent class of
art bug into a line of stderr naming the node.

---

## 2. `GLTFLoader::LoadGLTFFile` returns `void`

There is no way to find out whether the file was found and parsed. A missing or broken GLB shows up
only as every later `GetAssetsFromGLTF` failing to find its node.

**What bomber does:** calls it and carries on; `AddCellObject` logs and returns NULL per missing
asset, so a bad file gives a blank board and a screen of errors rather than a crash.

**Cost:** the failure is loud but not *early*, and it names the nodes rather than the file.

---

## 3. `Mesh` exposes its size but not its bounds

`Mesh::GetExtents()` returns `fmax - fmin` — the AABB's **size**. The centre and the min/max are
private, so an app cannot ask "how far below its own origin does this model hang?" and therefore
cannot place a model's feet on the ground from the mesh alone.

**What bomber does:** takes the **Y component of each node's own translation** out of the GLB and
uses it as the vertical offset, throwing X and Z away as Blender layout spacing. It works because
the artist positions the pieces to stand on the floor in Blender, which is a check they can actually
run.

**Cost:** the app depends on an export-time convention rather than on geometry. If a piece is
re-exported sitting at Y 0 when it needs lifting, it sinks, and the only symptom is that it looks
wrong. A `GetBounds` on `Mesh` would let `AddCellObject` align anything to the ground with no
convention at all.

---

## 4. No ambient or exposure control on the renderer

`shared_assets/shaders/default.frag` adds a flat `0.1f * albedo` ambient term. There is no
`Renderer` field for it, no exposure and no tone mapping, so the only way to lift the unlit side of
a surface is to add another light.

**What bomber does:** carries a **key sun plus a shadowless fill light** from the opposite side, as
`apps/breakout` and `apps/tetris` both do.

**Cost:** one extra light in every scene that wants to look lit, and the ambient colour is the
albedo rather than anything sky-like. A `Renderer::ambient` (a colour, not a scalar) would replace
the fill in all three apps.

---

## 5. `MESH_MODE_SHADER` volumes do not depth-sort against each other

`Renderer::CustomShaderPass` draws instances in whatever order the renderer collected them, and a
volume writes no depth. So where two volumes of the same mesh overlap on screen, they blend in
instance order and the seam is visible from some angles.

**What bomber does:** accepts it. The per-tile blast is 9 instances of one mesh and they do overlap.

**Cost:** known and accepted — the alternative (the single cross volume, `f_draw_cross`) composites
perfectly but has to be told its own arm lengths and marches a mostly-empty box. Both are in the
app and can be switched between, so the trade is visible rather than argued about. A back-to-front
sort of the instances of one custom-shader mesh, by distance to the camera, would close it.

---

## 6. Object picking is opt-in, and silently absent

`Application::CheckObjectSelection()` is protected and is **not** called by the engine. An app that
never calls it has no hover and no selection, the Inspector says "Nothing selected" forever, and
there is no warning anywhere to say the app never asked.

**What bomber does:** calls it from `UpdateView`, like every other app that wants an Inspector.

**Cost:** an hour, the first time. This one reads as *broken* rather than as *not enabled*, which is
what makes it worth writing down. Calling it from the base class's own `UpdateView` — or warning
once if an app has run a thousand passes and never called it — would make it undiscoverable-proof.

### 6b. …and possibly a second problem underneath it — UNCONFIRMED

While checking that the fix above worked, driving the pointer **synthetically** (`SetCursorPos` +
`mouse_event` from PowerShell) never produced a hover:

- `InputController::IsMouseOverWindow()` stayed **false** the whole time. It is set true by
  `WM_MOUSEMOVE` and only ever set false by `WM_MOUSELEAVE`, so false means the app has *never*
  seen a mouse-move message.
- `GetRelativeMousePosition()` returned **x 1431 where 933 was expected** — out by ~500 — while
  **y was exactly right**. It returns `mouse_position - window_state.window_position`, and
  `window_position` is only ever written from `WM_MOVE`, so a stale or never-received `WM_MOVE`
  would do this. Worth noting separately that it subtracts the value `WM_MOVE` carries rather than
  the client-area origin, which would put picking out by the title-bar height even when it is
  working.

**This is flagged, not diagnosed.** Every observation above comes from a synthesized pointer, and a
synthesized pointer is exactly the thing that might not generate the messages a real one does — so
the honest summary is "picking did not work under synthetic input, for two reasons that would also
break it under a real mouse, and a real mouse has not been tried since the fix".

`bomber_state` now reports `mouse.over_window`, `mouse.x`, `mouse.y`, `mouse.hovered_id`, `hovered`
and `selected` precisely so this can be settled with one real click and one tool call instead of a
screenshot of a panel. If a real click selects something, delete this section.

---

## 7. Scripted input and real input are indistinguishable downstream

`InputController::HoldKey` writes synthetic presses into the same `KeyState` a real key does, which
is exactly right for making scripted input exercise the real path. But it means nothing downstream
can ask "was this a person or a script?", and `HasSyntheticHolds()` only answers "is *any* scripted
hold running right now".

**What bomber does:** `f_lock_human_input` (the panel's *lock out keyboard/mouse* checkbox, and the
`bomber_lock_input` MCP tool) switches the camera and the reload key off outright, and accepts the
game's controls only while `HasSyntheticHolds()` is true.

**Cost:** a loophole — a key pressed during the exact ticks a scripted hold is running still gets
through. Good enough (nobody is playing during a scripted test) but not airtight. A per-`KeyState`
"this edge came from a synthetic hold" bit would make the lock exact, and would let the engine offer
the lock itself rather than every app rolling one.

---

## 8. `camera_set` cannot capture in the same call

The core MCP tools have no way to set the camera and take the screenshot atomically — `camera_set`
has no `include_screenshot`, unlike most of the other tools. With the app on screen and a person at
the mouse, the camera can move between the two calls, and the result is a **plausible-looking
picture of the wrong thing**, which is far worse than a failed call.

**What bomber does:** the input lock above, plus test scripts that read `camera_get` **before and
after** the screenshot and retry until both match.

**Cost:** cost two rounds of "fixing" a character facing table that was already correct. An
`include_screenshot` on `camera_set` would remove the whole class of error.

---

## 9. Blender's forward is +Z, the engine's is −Z

Not a bug and there is no export setting to change it — worth writing down because it is not
guessable and it silently turns models 180°.

`Object` forward is **−Z**: yaw 0 makes `object_get` report `world_forward` (0,0,−1), so yaw *t*
gives `(−sin t, 0, −cos t)`. A model exported from Blender faces **+Z**, the opposite. So the yaw
that makes a character *look* in direction D is the one that puts the engine's forward at −D — see
the `YAW` table in `ApplicationBomber::SyncView`.

**How to settle it without burning screenshots:** `object_get`'s `world_forward` is an instrument,
not a picture, and answers the engine half exactly. The art half needs exactly one screenshot: a
known yaw, a known camera side, and see whether you get the face, the back or a profile.

---

## 10. Metallic costs the diffuse term with nothing to reflect

Already documented at `material_t::metallic` in `core/Material.h`, listed here because bomber walked
straight into it: the first asset export had every material at metallic 0.6–0.71, and with
`f_render_skybox` off and no environment reflections there is nothing to pay it back. Untextured
materials with a dark base colour rendered essentially black.

**What bomber does:** nothing — reported it, and the artist lowered metallic in the export. Fixing
it app-side would mean silently overriding the exported materials, which is worse.

**Cost:** none now. Worth a one-line warning from `Renderer::AddMaterial` when metallic is high and
there is no environment, because the symptom (a black object) looks like a failed texture load.

---

## 11. There is nowhere for a test that is not an app

`engine.mk` builds one thing: an application, linked against all of `core/`. There is no convention
for a binary that is *not* an app — no `tests/` target, nothing that compiles a single translation
unit and runs it — so a piece of logic that could be checked in a second without a window has
nowhere to be checked from.

**What bomber does:** carries its own four-line `rules` target in `apps/bomber/makefile`, which
compiles `maze_test.cpp` against `Maze.cpp` alone — no core, no engine, no GPU — and runs it. It
checks the things that are statements about integers: a blast clearing a hedge and stopping at it,
an enemy cutting through one, a pickup staying buried until the block above it goes, the bridge
refusing a step across its rope side, 200 seeds all laying out a playable board, and two identical
games staying identical for 900 ticks.

**It earned its place immediately.** It found that seed 12 generated a board with **fourteen**
reachable cells and no room for a single enemy — a pre-existing weakness in the zone generator that
`reachable_cells` had been quietly reporting for a day without anyone reading it. The fix (re-roll a
layout that scores under `MAZE_MIN_PLAYABLE`) is in `Maze::NewGame`.

**Cost:** the target is hand-rolled and this app is the only one with one, so nothing keeps it
working and nothing runs it but a person who knows it is there. A `TEST_SRCS` convention in
`engine.mk`, building each named source on its own and running it, would cost about as much as this
one app's copy and would be available to the apps that have a `Field`, a `Table` or a `Maze` — which
is most of them. The precondition is already met everywhere it matters: it works here only because
`Maze.h` names no engine type.

---

## 12. `GetSkeleton` only loads the joints that hang off `joints[0]`

```cpp
Bone* root_bone = GetBone(skin->joints.at(0),bone_count,inv_binds,assetmanager);
skeleton->AttachChild(root_bone);
```

That is the whole of it: it takes the **first** joint and recurses through its **children**. Any
joint that is a *sibling* of the first rather than a descendant of it is never loaded, and glTF does
not require joints to form one tree — `skin.joints` is a flat list, and `skin.skeleton` (the common
root) is optional and absent here.

The enemy rig was first exported with `Hips`, `Head` and `Arm.R` all parented to the armature object
and none to each other. `Hips` had no children, so **one bone of three** loaded.

**The art was fixed** — it is now `Hips → Torso → Head, Arm.R`, one chain, and all four bones load.
That is the right rig regardless: with the bones as siblings, moving the hips would not have carried
the head, so the walk would have looked wrong even if all three had loaded. **The engine gap is
still open**, and is deliberately parked until something actually needs a multi-root rig.

**What it costs, in order of how much it hurts:**

1. **The mesh tears itself apart.** `default_skinned.vert` indexes
   `bone_data[gl_InstanceID * bone_count + bones.x]` with `bone_count = skeleton->num_bones` = 1,
   while the vertices carry bone indices 0..2. Indices 1 and 2 read past this instance's block into
   whatever is next in the buffer. On screen that is the head and arm stretched into long spikes
   across the board - which reads as a corrupt mesh, not as a rig that half-loaded.
2. **Two thirds of the animation silently does nothing** - see §13.
3. **Nothing says so.** `GetSkeleton` logs `Loaded %i bones` at **Trace** level only, so at the
   default log level a rig that loaded a third of itself is indistinguishable from one that worked.

**What bomber does:** logs the bone count next to the clip names in `BuildEnemies`, and warns when a
clip has tracks that bound to nothing.

**The fix, cheapest first:** `GetSkeleton` already has `skin->joints` in hand - walking the whole
list and attaching any joint whose parent is not itself a joint would handle flat and multi-root
rigs both. Failing that, one `debug->Warn` when `bone_count != skin->joints.size()` turns a corrupt
mesh into a line of stderr. There is already a bone-count check at `Renderer.cpp:359`, but it
compares the skeleton against *itself*; the useful comparison is against what the **mesh** expects,
which is the one number that predicts the out-of-range read.

---

## 13-14. CLOSED: the animation state machine moved from `PlayerCharacter` into `Object`

Both of these were the same gap seen from two sides, and both are fixed. Kept as one short entry
rather than deleted outright because the shape of the fix is worth knowing about.

**What was wrong.** `Object::ApplyAnimation` handled exactly one state, `ANIMATION_STATE_LOOPING`.
`TransitionToAnimation` set `ANIMATION_STATE_TRANSITION_START`, which `Object` had no branch for, so
asking a plain `Skeleton` to blend left it in a state nothing advanced and it froze on the pose it
had. Separately, `ApplyInterval` skips the root track on the grounds that `SampleRootMotion` will
pose it, and only `PlayerCharacter::ApplyAnimation` called that - so naming a root bone on anything
else silently stopped that bone animating. The whole ~200-line machine lived in `PlayerCharacter`,
tangled with `ProcessInputState()` and the root-motion `MoveBy`/`RotateBy`.

**What it is now.** The state machine lives in `Object::ApplyAnimation` - looping, one-shots that
hold their last frame, `auto_continue_to`, crossfades and the rewind. Two virtuals are what a
character overrides:

- `ApplyRootMotion(delta)` - base does nothing. The delta is still **computed** either way, because
  computing it is also what poses the root bone; only the world movement is optional. That is the
  half that used to go missing.
- `LoadDefaultPose()` - base puts every `Bone` below the object back to its reference pose.

`PlayerCharacter::ApplyAnimation` is now `ProcessInputState()`, a call to the base, and its own
layering (blink, head/hips look, foot trackers).

**One thing deliberately NOT fixed:** retargeting a transition to a *third* clip while one is still
running. `TransitionToAnimation` still rewinds when asked to go back where it came from and pauses
otherwise. Out of scope by agreement - the simple cases (play it once, let it finish, blend A to B)
are what this buys.

**Watch out for one consequence in app code:** while a blend runs, `CurrentAnimationName()` is still
the clip being *left*. Code that decides "am I already playing X?" has to check `NextAnimationName()`
too, or it re-requests the same transition every tick for the whole blend.

---

## 15. A clip that drives nothing is indistinguishable from a clip that is playing

`Animation::LinkObjects` binds each track to a bone **by name** and already counts the ones it
matched:

```cpp
debug->Info("Animation: Linked %i objects from %s to animation %s\n",count,...);
```

It prints the count it found but never the count it *wanted*, so `Linked 1 objects` for a
three-track clip looks like success. Downstream, `ApplyInterval` skips unbound tracks silently.

The result is a failure mode with no symptom anywhere in the engine: the clip is "playing", the time
index advances, `CurrentAnimationName()` returns the right string, and nothing moves.

**What bomber does:** walks the tracks itself after `AddAnimation` and warns, naming the bones that
bound to nothing.

**Fix:** one line - compare `count` against `object_animations.size()` in `LinkObjects` and warn on
a mismatch, naming the unmatched targets. Every app that loads a clip wants it, and none of them
should have to write it.

---

## Gotchas that are not gaps

- **`sim_step` takes `num_ticks`, not `ticks`.** An unknown argument is ignored and the tool steps
  1, so a filmstrip script silently films the wrong ticks. `ticks_advanced` in the reply is the
  value to trust, and the tool's own description says so.
- **A volume needs a `sun_intensity` scale.** A surface turns radiance into pixels through a BRDF
  and an NdotL; a volume integrates it over density and step length. The same sun brightness does
  not land in the same place — `apps/ship`'s `raymarch_volume.frag` carries the uniform and says
  why. Dropping it made the fireball render white.
- **`Scene::AddObject` during a tick.** Safe from `RunSimulationTick` (and from a command handler,
  which is where `RebuildField` runs); not safe from an `UpdatePhysicsState` override.
