# Engine backlog

Work items drawn from `docs/tetris_findings.md` (the report from building `APP=Tetris` as an audit
of the engine), ordered by implementation effort rather than by importance. Numbers are stable —
new items get new numbers, nothing is renumbered — so they can be referred to in conversation.

Status key: `[x]` done · `[~]` partially done · `[ ]` open · `[-]` decided against.

Last updated 2026-09-11.

---

## Band A — minutes each, no risk

- [x] **1. `SoundSystem::AppendFile` ignored the WAV channel count.** `format` was computed and then
  `AL_FORMAT_STEREO16` passed unconditionally, so mono files played at double speed.
  `core/SoundSystem.cpp`. *Done by Dick.*
- [x] **2. `SoundSystem::Play` set the gain after `alSourcePlay`.** Each sound's attack played at
  the previous call's volume. `core/SoundSystem.cpp`. *Done by Dick.*
- [x] **3. `TCPServer` logged two `Info` lines per connection.** A scripted MCP session buried
  everything else in the log. Now `Trace`. `core/TCPServer.cpp`. *Done by Dick.*
- [x] **4. Docs said `localhost`, the server binds IPv4 only.** Decided: keep the IPv4-only bind
  (see item 20) and document it. `docs/mcp_server.md` gained a section with the measured 15 ms vs
  2,058 ms comparison; `core/TCPServer.cpp` and `core/MCPServer.cpp` carry the reason at the bind
  and the log line; every `localhost:8765` in the docs is now `127.0.0.1:8765`.
- [x] **5. `ApplicationUI.cpp` constructed `Renderer` twice**, leaking the first. It is the file
  people copy as a template. *Done by Dick.*
- [-] **6. `Object` has no `void* user_data`.** **Declined — documented instead.** The premise was
  wrong: the report said subclassing was impractical because `AssetManager::GetObjectFromAsset`
  would not build a custom type, but it takes an `optional_target`, and ten call sites across four
  folders already use it that way (`ShipCharacter`, `Asteroid`, `HingedDoor`, `Pickup`, `IsoCell`,
  `IsoWall`). `IsoCell` already solves the exact "which cell is this object" case with an
  `int3 coordinate` member, and `ApplicationGrid` does the reverse lookup with a `dynamic_cast`.
  A user-data pointer would also have no safe home here: `Destroy()` only marks, and the real
  delete happens later inside `Renderer::DeleteDestroyedObjects`, so an owning pointer would have
  no hook at which to free anything and the duplicate constructor would leave two objects owning
  one allocation. Documented at the top of `core/Object.h` and in the agent brief. If a raw field
  is ever genuinely wanted, it should be an opaque integer tag, never a pointer.
- [x] **7. `RRandom::GetInt(min,max)` had undefined behaviour.** `abs(GetInt())` is UB on
  `INT_MIN` (about one draw in four billion), and `imax - imin` overflowed for ranges wider than
  `INT_MAX`. Now done in `uint32_t`, which wraps instead of trapping and holds every possible
  distance. Note this changes the values produced for a given stream position.
- [x] **8. A commented-out `ApplicationGrid::RenderAnimationUI` was living in `core/Application.cpp`.**
  ~113 lines of dead app-specific code in the engine, referencing a removed API. *Done by Dick.*

## Band B — under an hour each

- [x] **9. `SoundSystem` played buffer 0 for an unknown handle.** `map_handles[name]` on a
  `std::map` default-constructs a missing key, so a typo silently played whatever was registered
  first — and grew the map on every call. Added a private `FindHandle` that returns -1 and logs
  the offending name; `Play`/`Pause`/`Rewind` become no-ops and `FinishedPlaying` returns true
  (reporting "still playing" would hang a caller waiting on it).
- [x] **10. `Renderer::viewport_x` existed but Y was hardcoded to 0.** Added `viewport_y`, wired
  into both `glViewport` calls. Documented as measured from the **bottom** of the window, because
  that is GL's origin and silently flipping one of four numbers would be worse than the mismatch.
- [x] **11. `Camera::SetupPerspective`/`SetupOrthographic`'s `width`/`height` are overwritten
  every frame** by `Renderer::DrawFrame`. Documented in `core/Camera.h` rather than removed: they
  still matter to anything reading `viewport.aspect` before the first frame (`GetPixelRay` does),
  and they keep the twenty-odd existing call sites self-documenting.
- [x] **12. `MoveObjectOverTicks` could not express a sequence.** A new motion replaced the one in
  flight, so a there-and-back (camera shake, a piece bouncing as it lands, a panel sliding out and
  back) lost one leg. Added `Scene::QueueObjectMotion`, which appends instead. Legs for one object
  run in submission order; different objects stay independent; `MoveObjectOverTicks` now clears
  the object's whole queue, so it is still the way to cancel.
- [x] **13. The gamepad D-pad was unreachable.** `AddGamePadMap` maps *analog* indices, and XInput
  delivers the D-pad, face buttons, bumpers, start/back and stick clicks as bits in `wButtons`.
  Each button now has a synthetic system keycode (`GAMEPAD_KEY_DPAD_LEFT`, `GAMEPAD_KEY_A`, …) and
  travels the ordinary keyboard path, so it gets edges, multi-mapping, the focus gate and
  recordability for free:

  ```cpp
  input->AddKeyMap(GAMEPAD_KEY_DPAD_LEFT,INPUT_MOVE_LEFT);
  input->AddKeyMap(VK_LEFT,INPUT_MOVE_LEFT);   //both on one action is fine
  ```

  The define *is* the mapping (`GAMEPAD_SYSKEY_BASE + XINPUT_GAMEPAD_*`), so there is no table to
  keep in step. Buttons release on focus loss and on unplug.
- [x] **14. `material_slot` and `f_update_materials` were two idioms for one thing, and one silently
  won.** An object says what a slot holds either BY NAME (a loader records it; resolved into an
  index once the renderer's global list is complete) or BY INDEX (gameplay code that already knows
  it). Both arrays were public, 19 sites wrote them directly against 4 that used the setter, and an
  index written onto an asset-loaded object was silently replaced by the name lookup on the next
  frame — invisible only when the name happened *not* to resolve, which is what the Tetris app was
  accidentally relying on.

  **Fixed** by making the invariant enforceable instead of documented:
  - `material_slot`, `material_names` and the flag are now private — to subclasses too, since
    `IsoCell`/`HingedDoor`/`CraneCharacter` were writing them directly.
  - Symmetric setters, where setting either representation cancels the other:
    `SetMaterialSlot`/`GetMaterialSlot` (lowers the resolve flag) and `SetMaterialName`/
    `SetMaterialNames`/`GetMaterialName`/`GetMaterialNames` (raises it). `GetMaterialSlots()`
    returns the raw array for `Renderer`'s per-instance batch fill, which runs per object per frame.
  - `TakeMaterialNames` now raises the flag, which was the missing half: setting an index cleared
    the flag automatically while setting a name did not, so the flag had to be maintained by hand
    on one side only. That is why `IsoCell.cpp` carried **nine** hand-written
    `f_update_materials = true` lines — all now deleted, along with one in `IsoTerrain.cpp`.
  - `PickMaterials` lowers the flag: it *is* a resolve, just against the caller's list.
  - `ResolveMaterialNames` skips empty names explicitly rather than relying on no material being
    called `""`.
  - Renamed `f_update_materials` -> `f_resolve_material_names` and `UpdateMaterials()` ->
    `ResolveMaterialNames()`. The old names read as "my materials changed, re-upload them", which
    is a different operation and one the renderer does (`UpdateObjectMaterials`, `UploadMaterials`).

  Fixed in passing, all instances of the same bug: the engine's own Inspector wrote
  `&object->material_slot[i]` straight through `ImGui::DragInt`, so dragging a slot on any
  asset-loaded object was undone on the next frame; `IsoCell::SetTerrainType` set its slot *before*
  loading the asset that overwrites it; and `ApplicationAnimation`'s foot trackers and
  `ApplicationTileset`'s road marker set an index on an asset-loaded object. Those last two may
  visibly change colour — that is them starting to work.

  All 11 apps build. Tetris verified live: 25 pieces / 8 line clears, 0 displaced cubes, and a
  screenshot confirming all seven tetromino colours, the ghost marker and the frame render
  correctly.

  *Note: the earlier claim that the duplicate constructor copies only 3 of 4 material names was
  wrong — `Object.cpp:48-55` copies all four names and all four slots. No bug there.*
- [ ] **15. `InputController::IsInputLive()`.** The underlying bug (item 29) is fixed, but every
  app still writes the focus gate by hand and must get it right. One predicate owned by the engine
  retires the trap. *Later.*

## Band C — one to three hours each

- [x] **16. Primitive mesh generators** — **Done.** `core/Primitives.h` / `.cpp`: `MakeBox`,
  `MakeQuad`, `MakeSphere`, `MakeCylinder`, `MakeCone`. `data/unit_cube.obj` was the only cube in
  the engine, and hand-building one was 45 lines (`ApplicationShip.cpp:80-125`). Five shapes was
  Dick's call and is the right cut — it covers every debug marker, placeholder and collider
  visualisation without anyone reaching for a `.obj`.

  **Conventions, deliberately identical across all five**, because the value of a primitive set is
  not having to remember which one is the odd one out:
  - **Centred on the origin.** A box of size `(2,2,2)` spans -1..+1 on every axis; a cylinder of
    height 2 spans y -1..+1. That is what makes the matching collider
    `AddBoxCollider(size * 0.5f, vec3(), ...)` with no offset to get wrong.
  - **Sizes are full extents**, not halves. `radius` is still a radius.
  - **+Y is the axis of revolution** for sphere, cylinder and cone — the engine's up axis, and
    `AddCapsuleCollider`'s axis too.
  - **`MakeQuad` lies in XY facing +Z**, the same frame as a glyph or a billboard, so it drops
    straight into the text work (see `text_rendering_options.md` §4a). Rotate -90° about X for a
    floor.
  - **Wound counter-clockwise seen from outside**, so the renderer's default `GL_BACK` culling
    removes the away-facing half. Every quad comes from a corner plus two edge vectors `u,v` with
    `cross(u,v) == n`, which is the trick that makes that true by construction rather than by
    trial and error. The face table is lifted from `BuildVolumeCube`, the code this replaces.
  - **Smooth normals where the surface is smooth, flat where it is not** — a cylinder's flank
    rounds off while its lid keeps a hard rim. Cone flank normals are the true slant normals, so
    a squat cone is not shaded like a tall one, and each triangle's apex vertex takes its own
    segment's mid-azimuth normal (a cone tip has no single normal; sharing one averaged normal
    there is what makes tips look pinched).
  - Tangents along increasing azimuth, `matid` 0, NULL + a `debug->Err` on nonsense arguments.

  **Render thread only** — they end in `Mesh::SetMeshData`, which calls `glNamedBufferData`
  immediately (`Mesh.cpp:51`). `Application::Init` and `PreRender` are fine; `RunSimulationTick`
  is not. Each returns a `new Mesh` the caller owns, like `CreateMeshFromHeightmap`.

  **Verified** by a throwaway harness linking the real generator against a stubbed `Mesh` and
  `Debugger` (no GL needed), checking the thing that is actually easy to get wrong: summing
  `dot(p0, cross(p1-p0, p2-p0)) / 6` over every triangle gives the enclosed volume *only* if the
  mesh is closed and wound outward, so one number catches a flipped face, a missing cap or an
  inverted normal. `MakeBox(2,3,4)` gives exactly 24.000000; sphere/cylinder/cone come in at
  -0.7%/-0.29%/-0.29% of analytic, under it as an inscribed faceted solid must be. Plus: no
  degenerate triangles (the UV sphere's pole rows are emitted as triangles, not quads with a
  zero-area half), every vertex normal unit length, and the geometric face normal agreeing with
  the authored vertex normals everywhere (worst dot 0.9995).

  **Both existing cubes converted, and `data/unit_cube.obj` is now dead.**

  - `ApplicationShip::BuildVolumeCube` was 45 lines of face tables; it is now `MakeBox(vec3(1,1,1))`
    plus `mesh_mode = MESH_MODE_SHADER`. Both reasons its comment gave for not using the GLTF
    "cube" asset — asset meshes are shared by pointer so tagging one `MESH_MODE_SHADER` would
    affect every other user, and the box the shader marches has to agree with the mesh's real
    extents — are satisfied by a generator, which hands back a fresh unshared mesh built to the
    size asked for.
  - `ApplicationTetris` no longer touches the OBJ loader. It loaded `data/unit_cube.obj` only
    because the engine could not make a cube; now `Init` generates one `block_mesh` and every
    cube in the app shares it by pointer. `MakeCube` takes the mesh instead of the `AssetManager`,
    and the comment about switching off the name path went with it: a generated mesh carries no
    material names, so there is nothing for `ResolveMaterialNames` to overwrite the slot with.
    The `AssetManager` itself stays — the engine reaches for it unguarded in places (the asset
    list in the Scene panel, the `object_spawn` command handler), so an app dropping it would
    crash the engine, not itself. The mesh takes one reference of its own, the same way
    `AssetManager::AddNewAsset` does: a line clear destroys a lot of cubes at once, and
    `Object::DeleteMesh` frees the mesh when the last holder lets go.
  - `data/unit_cube.obj` now has no live references. The only mention left in code is inside the
    commented-out skybox block in `ApplicationGrid.cpp:332`, which would take `MakeBox` as-is if
    it were ever revived (the skybox pass does `glDisable(GL_CULL_FACE)`, so winding is moot
    there). The file can go whenever someone wants to delete it.

  **Verified on screen, not just compiled.** Both apps build with no warnings, and both were run
  and screenshotted through the MCP `screenshot` tool:
  - `APP=Tetris` — well frame, back panel, board cells, the falling piece with its shadow, the
    ghost, and all three next-previews, all correctly lit. Every cube in that picture is generated.
  - `APP=Ship` — the raymarched volume still renders, which is the strongest winding check
    available: that pass flips to `GL_FRONT` and keeps the *inside* faces, so an outward-wound
    cube that was actually inward would render nothing at all. Moving it to the origin showed the
    box at its `(20,6,20)` object scale with the ship inside it, confirming the unit-cube-at-±0.5
    contract makes an Object scale read directly as world units.
- [x] **17. A hook that runs once per simulated tick.** **Done.** `RunLogic` was documented as the
  per-tick gameplay hook but was called on every pass of the physics loop, which keeps spinning
  while paused so a key can unpause it; the pause and single-step counters were honoured one
  function later in `Scene::UpdatePhysics`. So gameplay written the obvious way kept playing while
  paused, and single-stepping advanced it by loop iterations rather than by ticks. `RunLogic` was
  *replaced*, not supplemented, by two virtuals with honest names — `UpdateView()` every pass of
  the loop (camera, picking, editor) and `RunSimulationTick()` exactly once per tick that actually
  runs. Replacing rather than adding is the point: all 11 apps say `override`, so removing the base
  declaration is a compile error at every site and forces each body to be triaged into view work or
  tick work, instead of leaving them silently on the wrong one. Fixes the residual race noted in
  item 33 for free, because one decision per pass is then made in one place and handed to both
  animation and physics. See item 35 for what this opens up afterwards.

  **As built.** `Scene::BeginPass()` is the new first call of every pass: it drains the command
  queue, services the pause key, and decides once whether this pass ticks (`IsTickingThisPass()`).
  The physics loop then runs `UpdateAnimations` + `RunSimulationTick` + `UpdatePhysics` only on a
  pass that ticks, and `UpdateView` on every pass - *after* the tick, so view code sees the state
  the pass produced rather than the previous one's, which the old `RunLogic` could not (it ran
  before the step, leaving every camera a tick stale).

  Triage of the eleven apps, using "if running it twice for one tick would change the outcome, it
  is simulation":
  - `Tetris` opened with a hand-written copy of the pause predicate to work around the old
    behaviour; that workaround is deleted. Its F1 panel toggle moved to `UpdateView`, so it now
    works while paused - it was chrome sitting behind the gameplay gate.
  - `OCPP` has no simulation at all. Everything it does is websocket traffic paced by
    `GetTickCount()` against real charger backends, so it has only an `UpdateView` - the one place
    in the repo where a real-millisecond duration is the right unit.
  - `Grid` and `Tileset` are mostly editors: only character movement / the driven car are
    simulation, and placement tools stay on the view side so they keep working while paused.
  - `UI`'s override was an empty body, so it is gone; the base class default is identical.
  - `Animation`, `IsoAnimation`, `Dozer`, `Sim`, `Ship`, `Tank` split down the middle.

  Behaviour deliberately preserved rather than fixed in passing: several apps gate keyboard control
  on `ImGui::GetIO().WantCaptureMouse` only because that check sat above them in the old single
  function. A character stopping because the cursor is over a panel is almost certainly unwanted,
  but each is flagged in a comment rather than silently changed. Two comments in `ApplicationTank`
  that misattributed this hook to the "main"/"frame" thread were corrected - it has always run on
  the physics thread.

  Verified: all 11 apps build. Live on Tetris - paused for 1.5 s with zero tick advance, then steps
  of 1/5/20/48 landing exactly, and 48 ticks moving the piece exactly one gravity cell as its tool
  documents. Live on Ship - `UpdateView` eases the camera back from a yank to (60,60,60) at its 0.04
  lerp per pass, so the every-pass hook is demonstrably running and doing its work.

  `UpdateView` running *while paused* was the one case left unobserved here, for want of a pause
  tool in an app with an observable view. Item 34 supplied it and it is now measured: with Ship
  paused, the camera yanked to (60,60,60) eased itself back to (0.09,20.06,0.09) over five samples
  while `tick` stayed at 252 throughout. The view hook runs on non-ticking passes.
- [x] **18. `Scene::AtTickBoundary(fn)`** — the read-side mirror of `SubmitCommand`. **Done.** An
  MCP tool handler holds no lock, so reading scene state races the physics thread. Without this
  every app grows its own hand-maintained snapshot that drifts.

  **Named `AtTickBoundary`, not `ReadAtTickBoundary`.** Two of the things that needed it are writes:
  `camera_set` was writing the camera from the MCP thread, and `object_move` calls
  `Scene::MoveObjectOverTicks`, which edits the `object_motions` vector that `AdvanceObjectMotions`
  is iterating on the physics thread. That one is the worst of the set — a write race, not a read
  race — and it has no SimCommand form, so the lock is the entire mechanism. A name saying "read"
  would have been wrong at two of its call sites on day one.

  It takes `renderer->physics_mutex`, which the physics thread holds across a whole pass. So it does
  not merely make a read atomic: it lands the read *between* ticks, and `GetPhysicsTick()` inside it
  names the tick that produced the state being read. Two rules, both documented on the declaration
  and both deadlocks if broken — fn must not wait on the physics thread (`SubmitCommandAndWait`,
  `StepPhysicsAndWait`, polling the step/motion counters) and must not wait on the render thread,
  which rules out `MaybeAttachScreenshot`, since the render thread takes this same mutex to draw.

  Converted: `object_list`, `object_get`, `object_set_transform`, `object_move`, `object_spawn`,
  `sim_command`, `camera_get`, `camera_set`, plus `ApplicationTank`'s `GetVehicleTelemetry` and
  `GetCraneTelemetry` — that app's numbers are what `tools/baseline_rp3d_*.json` diff against, so a
  torn read there reports a wheel load that never occurred and invalidates the comparison. All its
  callers were checked to be lock-free first; the mutex is not recursive.

  Two helpers keep the resolve/submit/read-back tools honest: `Application::ResolveObjectIdArg`
  returns an **id**, never a pointer, because the pointer is only valid while the lock is held and
  every one of those handlers then submits a command and waits with it released;
  `ObjectJsonAtTickBoundary` re-resolves that id afterwards and reports cleanly if the object is
  gone. `ApplicationTetris` keeps its per-tick snapshot, which stays the right call for state every
  tool call reads: `AtTickBoundary` is correct but stops the simulation for as long as it runs.

  `camera_set` also became all-or-nothing while it was being converted. It used to apply `position`,
  then discover `look_at` was malformed and return an error, leaving the camera half-moved by a call
  that reported failure. Arguments are now parsed before the boundary and applied inside it.

  Verified on Tetris and Tank. Functionally: every converted tool round-trips, including
  spawn → set_transform → move → destroy, and reads against a destroyed id return an error instead
  of crashing. Against deadlock, which is the real risk of putting a lock in an MCP handler: six
  threads hammering a mix of `object_list`, `object_get`, `camera_get`, `object_set_transform` and
  `sim_step` managed 1871 calls free-running and 1840 paused with no hang, and a second run mixing
  `tank_telemetry`/`crane_telemetry` with the core reads managed 2254. `object_set_transform` is the
  one that matters there — it locks, unlocks, waits on the physics thread, then locks again.

  One false alarm worth recording: `camera_set` appeared to do nothing on Tetris. That app's
  `UpdateCameraShake` unconditionally re-asserts the camera position from `camera_target` every
  tick, so the write was overwritten by the next tick rather than lost. Confirmed by pausing first,
  where it moves exactly as asked. Same shape as the `SyncBoardView` false alarm under item 32.
- [ ] **19. A screenshot path that includes ImGui.** Capture happens inside `Renderer::DrawFrame`
  (`core/Renderer.cpp:801`); ImGui is drawn afterwards into the default framebuffer. Since ImGui is
  the engine's only text rendering, everything written in words is invisible to the one client that
  cannot look at the monitor. *Later.*
- [-] **20. Dual-stack MCP bind.** **Decided against.** IPv4-only is deliberate — this endpoint is
  reached from the same machine and a dual-stack listener is extra surface for nothing. Closed by
  documenting `127.0.0.1` everywhere instead (item 4).
- [x] **21. `APP=Grid` did not build.** Pre-existing, from the animation rewrite and the `Object`
  encapsulation: `SetNextAnimation` → `SwitchToAnimation` (4 sites) and `->parent` → `->GetParent()`
  (4 sites). *Done by Dick.*

## Band D — half a day to a day each

- [x] **22. `RRandom` could not be seeded per instance.** Done, in two passes. Dick made the
  buffer per-instance, initialised `state`, removed the no-op `SetSeed` and gave the constructor a
  seed. Then: a destructor (the per-instance buffer was leaking), `LoadFromTexture` now shares one
  process-wide buffer and uses the seed as this instance's **starting offset** (it also stopped
  ignoring its `filename` argument, which had been hardcoded to `data/textures/noise.png`), and
  `Generate` gives the instance a **private** buffer filled by an inline xorshift32 instead of
  `srand`/`rand` — no global CRT side effect, and the same seed now yields the same bytes on any
  toolchain, which is what handing a seed to another machine requires. `Get_uint8` reads then
  advances, so a seeded offset is genuinely the first byte drawn. The class header documents the
  recommended shape: **one instance per game, created once in `Init()`**.

  Still true and worth remembering: a single shared stream only replays if every draw happens on
  the simulation thread in tick order. A draw from the UI or an MCP tool thread shifts the stream
  for the simulation regardless of how it is seeded. The class is not thread safe by design, and
  now says so.
- [ ] **23. `Destroy()` / reap policy.** `Object::Destroy()` only marks; only 2 apps of 11 call
  `Renderer::DeleteDestroyedObjects()`, so everywhere else a destroyed object leaves its rigid body
  in the physics world forever. Needs a decision about *where* reaping safely happens, not just a
  call added. *Later.*
- [ ] **24. World-space text.** No bitmap font, no text mesh, one 13px ImGui font. `SpriteSheet`
  already loads an atlas — it just cannot reach the world. Pairs with item 19.
  **Options compared in `docs/text_rendering_options.md`** (2026-09-11): seven methods with
  pros/cons, how glyph selection actually works, and what is already in the tree. Headline: the
  vendored `imstb_truetype.h` is complete stb_truetype 1.26 and does bitmap baking **and SDF
  generation** (`stbtt_GetGlyphSDF`) **and** outline extraction, so the three most attractive
  options need no new dependency. Recommendation is an SDF atlas, one mesh per string, glyph
  selected by baked UVs, drawn in the custom shader pass — outline/shadow/glow then cost a
  `smoothstep` each, and one atlas serves every size. Copy the header out of `3rdparty/imgui/`
  first: a font system that includes from ImGui's folder has not achieved an ImGui-less build.
  Build the text primitive before any UI layer — it is useful on its own, and the UI framework
  (layout, hit-testing, focus) is weeks rather than days.

  **The GEOMETRY half is done** (2026-09-11), which is option D of that note rather than the
  recommended B, because Dick had already exported one mesh per glyph to
  `data/glyphs_unispace.glb` from `tools/blender_glyph_meshes.py`. `core/TextMesh.h` bakes a
  string into a single `Mesh` — glyph triangles copied along a pen and welded into one vertex
  buffer. `APP=Tetris` uses it for its captions, its three stats and its game-over banner, so the
  board no longer says anything through ImGui.

  Two things about its shape are worth keeping when the SDF atlas arrives:
  - **There is no atlas in it, and that is not an omission.** An atlas is texture bookkeeping.
    What a layout needs from a font is *metrics*, and for a monospaced set that is two floats
    (`advance` 0.509167, `line_height` 1.0, from `fonts_glyphs.json`). The two paths will share
    the *layout* and none of the storage; letting “atlas” into this API would have welded them.
  - **The builder loads nothing.** It takes a `GlyphSet` and depends on `Mesh` alone;
    `LoadGlyphSetFromGLB` is the only part that knows what glTF is and lives in its own
    translation unit. An SDF path is a second loader and a second builder, not a rewrite.

  Still open: the SDF/quad path itself, and everything above the primitive — layout, hit-testing,
  focus. Also still true that `imstb_truetype.h` must be copied out of `3rdparty/imgui/` before it
  is used, or an ImGui-less build has not been achieved. *Later.*

## Band E — multi-day, strategic

- [ ] **25. Record and replay** (step 7 of the deterministic-sim plan). Unblocked by item 22, but see the thread-ordering caveat there. Tetris
  is a better test case than a vehicle: 200 cells of `int8_t` either match or they do not, with no
  float tolerance to argue about. *Later.*
- [ ] **26. Scene save/load.** `BuildSceneFromJSON` restores name/position/rotation of asset-backed
  objects and nothing else. *Later.*
- [ ] **27. Finish the skeletal animation system.** `ObjectAnimation.cpp:108` still has a
  `debug->Fatal` for any clip carrying a scale track. The probe was deliberately never started.
  *Later.*

## Found since

- [x] **29. `HasSyntheticHolds()` was false on the exact tick a scripted release is readable.**
  A hold is erased in the same `AdvanceSyntheticHolds` call that emits its key-up, so the focus
  gate the engine's own design recommends silently dropped **every** edge-triggered scripted action
  (rotate, fire, hard drop) while held ones kept working. Defeats any MCP- or replay-driven test.
  *Fixed in core by the Tetris agent; `ApplicationShip.cpp:1264` had the same gate and was one
  tapped control away from the same bug.*
- [x] **30. `Renderer::AddMaterial` returned the wrong index for an existing name.** Returned
  `materials.size()-1` rather than the matching index, so adding under a taken name silently
  handed back somebody else's material. *Fixed in core by the Tetris agent.*
- [x] **31. `Camera::GetPixelRay` was not viewport-offset aware.** It divided by
  `viewport.width/height` (the viewport's size) but took a pixel coordinate relative to the
  *window*, so with a non-zero `viewport_x`/`viewport_y` every pick was skewed by the offset.
  Fixed: `Camera::viewport` gained `px_offset_x`/`px_offset_y`, written by `Renderer::DrawFrame`
  next to `width`/`height`, and `GetPixelRay` subtracts them first. Those two are deliberately in
  **cursor space** (top-left origin, like the mouse) rather than `Renderer::viewport_y`'s
  bottom-origin convention; they are named differently so the mismatch reads as intentional, and
  `DrawFrame` does the one flip. *Fixed by the Tetris agent — logged in `docs/tetris_findings.md`
  §5.4, checks in `tools/camera_ray_test.cpp` (11 pass, headless, no GL).*

  Two things found while doing it, both worth knowing:
  - **The orthographic branch ignored the camera's orientation entirely** (there was a `TODO`
    saying so): it hardcoded `vec3(-w*zoom*aspect, 0, h*zoom)`, mapping screen-right to world -X
    and screen-up to world +Z, which is correct only for a camera looking straight down -Y. Now
    built from the camera's own basis, like the perspective branch. No existing app is affected —
    the only orthographic *scene* camera in the repo is Tetris's (Ship's are shadow/cloud cameras,
    Sim's `SetupOrthographic` is commented out and its `CAMERA_TYPE_ORTHOGRAPHIC` branch is a
    mouse-wheel zoom, not a pick).
  - **The object-id picking path needs no equivalent fix and should be left alone.**
    `Renderer::DrawFrame`'s `glReadPixels(mouse.x, height - mouse.y, ...)` reads a *window* pixel
    from a *full-window-sized* G-buffer, into which the scene is rasterised at its true window
    position — so it is offset-correct by construction. Only the normalisation in `GetPixelRay`
    ever needed to know where the rectangle was.
- [x] **32. An object motion occupied `ticks + 1` ticks, not `ticks`, and the header said otherwise.**
  `AdvanceObjectMotions` tested `ticks_done >= ticks_total` at the top of the loop, so the
  completion branch — final `SetPosition(target)`, then erase — ran for **every** motion on the call
  after the last interpolating one, not just physics-driven ones. For a plain object that extra call
  bought nothing (`factor` already reached 1.0) and actively harmed: it wrote the motion's target
  back over whatever else had moved the object that tick. That is what caused the Tetris
  line-collapse bug (`docs/tetris_findings.md` §4.11), where cubes stayed a row low for the rest of
  the run.

  **Fixed** by making the retire point depend on `f_kinematic`, which is what the header always
  claimed. A kinematic motion still finishes at the top of the following call — it must, because the
  velocity its last tick set has not been integrated yet. A plain object now finishes at the bottom
  of the call that did its last interpolation. The shared finalisation moved into one lambda so the
  two paths cannot drift, and the two conditions are mutually exclusive by construction (`f_kinematic`
  is tested on both), since a motion driving a body also reaches the non-physics branch whenever
  `delta_time` is not positive.

  Verified against a standalone replica of the retire logic: a plain motion of N now takes N calls
  (was N+1), a kinematic one still takes N+1, and three queued legs of 5 take 15 rather than 18.
  Then live: 45 pieces / 16 line clears through `tools/tetris_bot.py`, and all **200** cell cubes
  checked against their grid slots afterwards — 0 displaced. `object_move` still lands exactly on
  target. All 11 apps build.

  `ApplicationTetris.cpp:641` asks for `TETRIS_COLLAPSE_TICKS - 1` to dodge the old behaviour. That
  is now unnecessary, though harmless — it just retires a tick earlier than it needs to.


- [x] **33. Animation advanced while the simulation was paused.** `Scene::UpdateAnimations` had no
  pause check and is called on every pass of the physics loop, which keeps spinning while paused so
  a key can unpause it - only `UpdatePhysics`, two calls later, honoured the pause and step
  counters. Harmless when a clip only posed bones; not since the root-motion rewrite, because
  `PlayerCharacter::ApplyAnimation` calls `RotateBy`/`MoveBy` from the extracted root delta. So a
  paused simulation's characters kept walking, and single-stepping advanced animation by loop
  iterations rather than by the steps requested. Gated on the same predicate `UpdatePhysics` uses.
  Deliberately does not consume the step counter - `UpdatePhysics` still does that.

  The residual race this originally left - another thread calling `StepPhysics` between the two
  separate checks, so one step ran its physics without its animation frame - **is now fixed** by
  item 17's `Scene::BeginPass`. There is one decision per pass and both stages read it.

  Verification status, now that item 34 exists. Still not observed directly: a bone freezing. No
  app starts a clip on its own - `Object::SwitchToAnimation` is reachable only from the Inspector
  UI, and the Animation app's character sits in T-pose until driven - so `sim_step` has nothing
  animating to step. What *has* been measured is the gate itself: exactly 0 ticks while paused and
  exactly N per step. That is stronger evidence than it was, because `UpdateAnimations` no longer
  evaluates its own predicate - it reads the same `f_tick_this_pass` that physics reads - so
  "physics froze" and "animation froze" are now one fact rather than two that could disagree. A
  core `object_animate` tool (switch an object to a named clip) would close it properly and is
  cheap; not done, since nothing else needs it yet.

- [x] **34. Core has no generic pause/step MCP tools.** **Done.** `ApplicationTank` and `ApplicationTetris`
  each rolled their own (`tank_pause`/`tank_step`, `tetris_pause`/`tetris_step`) over
  `Scene::PausePhysics`/`StepPhysics`, which are core facilities every app has. Nothing app-specific
  about them. This has a concrete cost: item 33 could not be verified live, because no app pairs a
  pause tool with a visibly animating object. It then blocked verification a second time on item 17,
  where the un-observed case was `UpdateView` running while paused.

  **As built.** `sim_pause` (`paused`) and `sim_step` (`num_ticks`, `include_screenshot`) in
  `RegisterCoreMCPTools`, both returning the simulation clock — `tick`, `paused`, `pending_steps`,
  `timestep`. `sim_step` also returns `ticks_advanced`, which is the value to trust: short of
  `num_ticks` means the wait timed out and the remainder is still queued, and a caller that assumed
  otherwise would be reading the wrong state.

  The waiting logic is now `Application::StepPhysicsAndWait`, shared rather than copied —
  `tank_step` and `tetris_step` each carried their own copy of the same poll loop and now call it.
  Those app tools stay: they return vehicle telemetry and board state *with* the step, which is more
  useful in those apps than a bare clock.

  `Scene::f_paused` became `std::atomic<bool>`, for the same reason `pending_physics_steps` already
  was: an MCP handler holds no lock, so `sim_pause` writes it while the physics thread reads it.
  Benign on x86 as a plain bool, but the fix is one word and these tools are core now.

  **Bug found by the new tools, and fixed.** `sim_pause` reported `pending_steps: 1` on a
  free-running scene, and pausing then advanced one tick before freezing. `StepPhysics` documented
  itself as "a no-op while not paused" but actually accumulated regardless, and `UpdatePhysics` only
  ever decrements on a paused tick — so a step queued against a running simulation sat there
  forever and spent itself as a burst of extra ticks the moment someone paused. Both halves closed:
  `StepPhysics` now genuinely no-ops while running, and `PausePhysics(false)` discards whatever is
  still queued, since a step is a request to advance a *stopped* simulation. The decrement in
  `UpdatePhysics` is now guarded, because a concurrent resume can zero the counter mid-tick and an
  unguarded decrement would leave it at -1.

  Verified: pause freezes the engine tick *and* the game; steps of 1/5/20/48 land exactly, with
  `tetris_state.game_ticks` advancing by the same amount — so a step is a whole tick, not just a
  physics step. The leak fix was tested discriminatingly: 440 steps still queued mid-`sim_step`,
  resumed, queue went to 0 and the next pause did not burst. Tools confirmed registered in Ship and
  Animation, neither of which had any pause tool before.
- [ ] **35. Transform ownership, once `UpdateView` runs at framerate.** Item 17 keeps `UpdateView` on
  the physics thread under the lock, so it changes nothing here. Moving it to the render thread —
  which is where camera work belongs, since a camera should be smooth at display rate and not at
  50 Hz — raises three things, in increasing order of difficulty:
  1. **Writes.** An `Object`'s local transform needs exactly one writer. The mechanism already
     exists and is per-object: `Object::UpdatePhysicsState` writes the transform only `if
     (physics)`, and recurses into children without writing theirs. So the rule is simply *`physics`
     set means the tick owns the transform and the view may only read it; `physics` null means the
     view owns it and the tick already never touches it*. A camera that should follow a physics body
     is therefore **parented to it, not given a body of its own** — the parent's local transform
     stays tick-owned, the camera's local offset stays view-owned, and `GetWorldTransformScaleMatrix`
     composes the two. That covers chase and cockpit cameras with no new machinery. Worth an assert
     in the view path rather than a type restriction, so the genuinely physical camera stays possible.
  2. **Reads, and judder.** The view reading tick state at framerate is *safe* under `physics_mutex`
     (the render thread already holds it across `Renderer::DrawFrame` and `DrawImGuiUI`) but it is
     not *smooth*: a 50 Hz sim sampled at 144 Hz gives a chase camera a target that jumps 50 times a
     second, and camera smoothing will show it. Wants the previous tick's pose kept on `Object` and
     an interpolation alpha in `DrawFrame`. This is the real work of the item, larger than (1).
  3. **Feedback into the sim.** Camera-relative controls ("forward" meaning camera-forward) make a
     view-owned, framerate-driven orientation into simulation input, which breaks replay (item 25).
     Whatever the sim reads must be sampled at a tick boundary. Cheap to get right if it is designed
     in, nasty to retrofit.

  Nothing in the repo attaches physics to a `Camera` today, so all of this is latent. Note
  `Application::UpdateUICameraControls` is already a render-thread camera writer, called from
  `DrawImGuiUI` under the lock — the pattern half-exists and is correctly synchronised. *Later.*
- [x] **36. Two instances silently shared the MCP port, and it bound every interface.** **Done.**
  `TCPServer::Start` set `SO_REUSEADDR`, which on Windows does not mean what the same name means on
  Unix: it permits binding a port another socket is **already listening on**, with no error, and
  which of the two a given connection reaches is indeterminate. Both copies logged `TCP Server
  started on port 8765` and `netstat` showed two `0.0.0.0:8765` listeners. This is worse than a
  failure — on 2026-09-11 a scripted test of the item-34 step-counter fix ran against a stale
  instance that had bound first, and passed for the wrong reason; the test had to be rebuilt to
  discriminate. Now `SO_EXCLUSIVEADDRUSE`, so the second bind fails with `WSAEADDRINUSE` and says
  so in words; the app still runs, only its MCP server is unavailable.

  The same bind used `INADDR_ANY`, so the MCP endpoint was reachable from the LAN — while its own
  source comment said "everything that talks to this server is on the same machine" and the startup
  line printed `http://127.0.0.1:8765/mcp`. Loopback is now an opt-in on `TCPServer`
  (`SetLoopbackOnly`) that `MCPServer` sets, rather than a blanket change: `ApplicationOCPP` serves
  real chargers on 9090 and `ApplicationTileset` serves a browser that need not be on this machine,
  and both must stay on every interface.

  Verified: one `127.0.0.1:8765` listener where there were two on `0.0.0.0`; second instance refused
  with the new message; 9090 still on `0.0.0.0`; and kill-and-restart-immediately survived four
  cycles with live MCP connections each time, which was the risk worth checking — exclusive bind
  can in principle collide with sockets left in `TIME_WAIT`.

- [x] **37. `HTTPServer` read requests with a single 4095-byte `recv`.** **Done.** It took whatever
  one `recv` returned and treated that as the entire request. A `recv` returns one TCP segment's
  worth of what has arrived, so a request with more header than that — or merely one split across
  segments, which is legal at any size and is what a real network does rather than loopback — was
  truncated and misparsed into a 404 or a failed WebSocket upgrade. Intermittent, and it looks like
  the client's fault. Now loops to the `\r\n\r\n` that ends the headers, with a 64 KB cap.

  **Not an MCP bug**, contrary to how this was first reported: `MCPServer::HandleHttpConnection` is
  a separate handler on a raw `TCPServer` and already did this correctly, header loop and
  `Content-Length` body read included. The broken one serves `ApplicationOCPP`'s charger endpoint
  and `ApplicationTileset`'s web UI. Headers only is the right fix there — nothing downstream
  consumes a body.

  Verified discriminatingly, on the second attempt: a `/status` GET with 30 KB of padding passed
  both before and after, because the request line sits in the first 4095 bytes either way. The test
  that actually separates them is a WebSocket upgrade with `Sec-WebSocket-Key` pushed to offset
  6083, past the old buffer — now `101 Switching Protocols`.
---

## Assessment of item 22 (`RRandom`)

The change made on 2026-09-11 — constructor takes a seed, `state` initialised to 0, `SetSeed`
removed, `Generate` calls `srand(seed)` before filling — fixes the two worst problems:

- **`state` was uninitialised.** It was read and advanced by `Get_uint8`, so a fresh `RRandom`
  started at an indeterminate offset that differed between runs and between builds. That is gone.
- **`SetSeed` was a no-op** that wrote a member nothing read. Removing it is better than leaving a
  function whose name promises reproducibility and delivers none.

Three things remain, and they are why this is `[~]` and not `[x]`:

1. **`rnd_texture` is still `static`, and `Generate` only fills when it is NULL.** So the *first*
   instance to call `Generate` decides the buffer contents for the whole process. A later
   `RRandom(99)` calling `Generate` runs `srand(99)` — perturbing the global CRT `rand()` for
   everything else — and then skips the fill entirely. Per-instance seeding still does not work.
2. **Every instance now starts at `state = 0` on a shared buffer, so two instances return the
   identical sequence.** Deterministic, which is progress, but perfectly correlated: a simulation
   generator and a UI generator that believe they are independent will hand out the same numbers.
   That is arguably a worse failure mode than the old indeterminate one, because it looks fine.
3. **Buffer contents come from the CRT's `rand()`**, so they are not reproducible across a
   different toolchain or platform even for the same seed.

The cheap fix for (1) and (2), which keeps the shared-texture design intact — that design is
deliberate, so the same noise can be uploaded to the GPU or sent over the network — is to make the
seed choose the **starting offset** rather than the contents:

```cpp
RRandom::RRandom(int seed){
    this->seed = seed;
    state = 0;          //Generate() sets the real start once the buffer size is known
}
```

…with `Generate` (and `LoadFromTexture`) ending in `state = seed % rnd_texture->img_data_sz;`. Two
instances with different seeds then read different parts of one shared buffer, which is exactly
what the class was always trying to be. Fixing (3) as well means replacing the `rand()` fill with a
small inline PRNG (xorshift32 is nine lines), which also removes the `srand()` side effect on the
rest of the process.

Worth doing before item 25: replay cannot be built on a generator whose stream depends on which
object happened to construct first.

---

## Added 2026-09-11, while building the text meshes

- [ ] **38. `GLTFLoader` produces inf/NaN tangents for any mesh with degenerate UVs.** It solves
  tangents per triangle from the UVs and divides by the UV triangle's signed area
  (`core/GLTFLoader.cpp`, the `f = 1.0f / (deltaUV1.x * deltaUV2.y - ...)` lines), with no check
  that the area is non-zero. Any triangle whose three UVs are collinear — which includes every
  face of an untextured extrusion — divides by zero and the vertex ships a tangent of `inf` or
  `NaN`. It is silent: nothing logs, and the mesh loads.

  Measured on `data/glyphs_unispace.glb`: **5,149 of 8,264 triangles, 62% of the file**, because a
  Blender text object gives its extruded sides no UV area at all. Not hypothetical, and not rare
  either — it will be true of most modelled-but-not-unwrapped geometry.

  Nothing visibly broke, which is the interesting part: `shaders/default.frag` only reaches for
  the tangent on a material carrying a normal map, so a NaN sits in the buffer until the day
  somebody assigns one. `core/TextMesh.cpp` sidesteps it by writing its own tangent (+X, which is
  correct for a plane facing +Z) rather than carrying the glyph's over, so the text path is not
  waiting on this.

  The fix is a guard: when the UV area is near zero, fall back to any vector perpendicular to the
  normal rather than dividing. Half a dozen lines in one place. *Small, but it should be done
  before anyone normal-maps an imported mesh, because the symptom — black or exploded shading on
  some triangles and not others — points nowhere near the loader.*

---

## Added 2026-09-11, with the occluder field

Point lights cast shadows without a cube map. `Renderer::EnableFieldShadows` builds a single
top-down `RGBA16F` texture per frame holding, per world column, the height of the highest surface
(R), the lowest (A), and the 2D distance to the nearest occupied column (G, jump-flooded by
`shaders/field_jfa.comp`). `CalcFieldShadow` in `shaders/default.frag` sphere-traces the segment
from a receiver to a point light against it, stepping by that distance and using it again for the
penumbra estimate. The map mentions no light, so N lights cost N marches against one texture
rather than N shadow maps. `APP=Tetris` is the first user - see
`ApplicationTetris::SetupFieldShadows`, and the Engine panel's Renderer section for the live
controls.

Measured on the Tetris board at 512x512, vsync off, by alternating `f_field_shadows` every 300
frames inside one process: **349.5 us per frame with it on against 259.3 us off**, so about 90 us
for the geometry pass, eleven jump-flood dispatches and the per-fragment march together. That is
CPU-side frame time (`tmr_frame`), which for dispatches is submission rather than execution, so
treat it as an upper bound on what it costs the frame, not a GPU profile.

What is still approximate, in the order it is worth caring about:

- [ ] **39. A column is one slab, so it fills in its own gaps.** R and A are the extremes of
  everything in a column, so a falling piece above a stack merges with it and light cannot pass
  between the two. Exact for anything extruded from the ground, conservative for everything else,
  and hard to see in a fixed top-down view - but it is the reason this is not a general shadow
  technique. Two more channels would carry a second slab if an app ever needs it.

- [ ] **40. The march still has a step floor, and it is load-bearing.** The distance field is zero
  everywhere directly above an occluder, so a ray running along the top of a wall would step by
  nothing, stall, and report "unoccluded" - the wrong answer in the place with the most geometry.
  `min_step = dist / field_shadow_steps` stops that, at the price of degrading to the old even
  spacing for such rays. The real fix is a max-mipmap pyramid over the height channel, which would
  give a conservative vertical bound to go with the horizontal one; the 2D distance alone cannot,
  because a neighbouring column one texel away may rise to just under the ray.

- [ ] **41. One light radius for the whole renderer.** `field_light_radius` sets how fast every
  shadow edge softens, because `light_t` has no size field to read it from. Adding one is the
  natural next step the moment two lights in a scene want different softness.

- [ ] **42. Skinned meshes do not cast into the field.** `RenderFieldPass` draws
  `MESH_MODE_NORMAL` only; a skinned mesh would need its own variant of `shaders/field.vert`
  applying the bone transforms, exactly as `default_skinned.vert` does. A skinned character
  receives field shadows but does not cast one. Nothing that uses the field has skinned geometry
  yet.

- [ ] **43. The field is rebuilt every frame with no dirty flag.** One geometry pass plus
  log2(size)+2 dispatches, all of it repeated whether or not anything moved. Its share of the
  90 us above was not measured separately from the march's, so that is the first thing to find
  out - but either way an app whose world changes rarely is paying for a map that did not change.
