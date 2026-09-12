# Engine backlog — closed items

Everything from `docs/engine_backlog.md` that is **done** (`[x]`) or **decided against** (`[-]`),
moved here on 2026-09-12 so the backlog itself is a list of work that is still open. Nothing has
been reworded: each entry is the text it carried when it was closed, including the verification
notes, which are the part worth keeping — several of these say exactly how a fix was proven, and
that is the record a later regression gets checked against.

Numbers are stable and are never reused. They run 1-66 across both files; **28 was never
assigned**.

Sources: `docs/tetris_findings.md` (the `APP=Tetris` run) and `docs/breakout_findings.md` (the
`APP=Breakout` run). See the open backlog for the split and for what is still outstanding.

Status key: `[x]` done · `[-]` decided against.

---

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

- [x] **15. `InputController::IsInputLive()`.** **Done 2026-09-12.** The underlying bug (item 29)
  was fixed long ago, but every app still wrote the focus gate by hand and had to get both halves
  right. One predicate owned by the engine retires the trap:

  ```cpp
  bool IsInputLive(){ return HasFocus() || HasSyntheticHolds(); }
  ```

  **The second half is the half that was got wrong.** Gating on focus alone looks obviously correct
  and silently breaks every scripted run: an MCP- or replay-driven session is precisely the case
  where the window is NOT in front, so a focus-only gate drops the input that automation exists to
  deliver. Scripted input does not come from the OS, so the reason for the gate does not apply to
  it — and `HasSyntheticHolds` deliberately stays true for the one extra tick that carries a
  release edge, so edge-triggered scripted actions survive too.

  **Converted the three apps that wrote the predicate out by hand** — `ApplicationBreakout`,
  `ApplicationTetris`, `ApplicationShip`. Pure refactor, no behaviour change: all three already
  meant exactly this. They also stop reading `main_window->f_has_focus` directly, which removes a
  second reason to know which of the two focus flags to consult.

  **Deliberately NOT converted, and the reasons are worth keeping:**
  - **`ApplicationTank` does something different on purpose.** Its HARDWARE handling sits behind
    `HasFocus()` and its scripted handling sits outside any gate at all, because its gamepad block
    writes the pedals unconditionally (`Brake(gp_r2)`, `Accelerate(0)`) and would otherwise fight a
    scripted drive. Both shapes are legitimate; the header says so.
  - **Cursor-driven work stays on plain `HasFocus()`** — picking, click-drag, mouse-look and wheel
    zoom in `Animation`, `IsoAnimation`, `Sim`, `Dozer`, `Tileset`, `Grid`, `Ship`. "Where is the
    mouse pointing" is meaningless while another application owns the pointer, and a scripted hold
    must not make a camera chase a cursor being used elsewhere. The rule, now stated on the
    declaration: **this predicate is about ACTIONS, not about the cursor.**

  **Still open, and noted rather than swept:** `ApplicationGrid::RunSimulationTick`,
  `ApplicationDozer` and `ApplicationTileset` gate whole functions on focus alone, which mixes
  action work with cursor work behind one test. Converting them would let scripted input reach
  apps that have none today, so it buys nothing now and would change behaviour in six apps nobody
  is asking about — worth doing the day any of them gets scripted, and cheap then because the
  predicate now exists.

  **A smell found in passing, not fixed:** `Window::f_has_focus` and `InputController::f_has_focus`
  are two flags for one fact, both driven from the same three window messages
  (`WM_ACTIVATE`/`WM_SETFOCUS`/`WM_KILLFOCUS`) in the same window procedure. They cannot disagree
  except before the first message arrives, where they differ in initial value (`false` vs `true`).
  `IsInputLive` reads the InputController's, so a converted call site no longer needs to know there
  are two.

  **Verified** in the standalone input harness: live while focused; not live once focus is lost
  with nothing scripted; live again while a scripted hold runs *even though the window is still
  unfocused*; and not live once the hold has finished. All twelve apps build, the Breakout bot
  still plays, and Tetris still takes scripted input while unfocused.

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

- [x] **19. A screenshot path that includes ImGui.** **Done**, and it was a capture-point problem
  rather than the compositing problem this entry assumed. ImGui does not draw into the default
  framebuffer: it draws into whatever is bound, and `Renderer::DrawFrame` leaves `resolve_fbo_id`
  bound when it returns. Both window paths then present that same buffer — the layered one via
  `CopyBufferToImage`'s `glReadPixels`, the plain one via `CopyBufferToBackBuffer`'s blit — so the
  panels were already landing in the buffer the screenshot reads. The capture simply ran too early,
  inside `DrawFrame`, before `Window::ImGuiRenderDrawData` had drawn them.

  So there are two useful moments to read one buffer, and `RequestScreenshot(f_include_ui)` picks
  between them: `CaptureScreenshotIfRequested(bool f_after_ui)` is now called at both, and services
  a request only at the point it asked for. The second call site is in `Application::DrawFrame`,
  after `ImGuiRenderDrawData` and before `SwapWindowBuffers` — it has to be between those two,
  because before ImGui there are no panels and after the swap the buffer's contents are no longer
  guaranteed.

  Default is **include_ui = true**, which makes the tool match the description it always carried
  ("what comes back is the window as it is now"). `include_ui: false` gives the old scene-only
  capture, for checking geometry or colour without panels in the way.

  Verified on Tetris: `include_ui: false` renders the board alone, `true` adds the Tetris panel with
  its score, tick counter, gravity rate, buttons, checkboxes and key legend — none of which was
  reachable by screenshot before.

  **This entry's premise is now out of date in one respect**, worth recording rather than quietly
  fixing: it said ImGui is the engine's only text rendering. `core/TextMesh.{h,cpp}` exists as of
  2026-09-11 (item 24, in progress) and Tetris already renders HOLD/NEXT/SCORE/LINES/LEVEL as scene
  geometry — they show up in an `include_ui: false` capture. The reason to include the UI is
  narrower than "text": it is that the debug panels, the inspector and the telemetry readouts exist
  nowhere but ImGui. That wording was corrected in `CLAUDE.md` and in the code comments.

- [-] **20. Dual-stack MCP bind.** **Decided against.** IPv4-only is deliberate — this endpoint is
  reached from the same machine and a dual-stack listener is extra surface for nothing. Closed by
  documenting `127.0.0.1` everywhere instead (item 4).

- [x] **21. `APP=Grid` did not build.** Pre-existing, from the animation rewrite and the `Object`
  encapsulation: `SetNextAnimation` → `SwitchToAnimation` (4 sites) and `->parent` → `->GetParent()`
  (4 sites). *Done by Dick.*

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


- [x] **38. `GLTFLoader` produces inf/NaN tangents for any mesh with degenerate UVs.** It solves
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

  *Done 2026-09-12.* `core/GLTFLoader.cpp` grew a `SolveTangent` helper and both copies of the
  tangent block — skinned and non-skinned — now call it. It rejects a near-zero UV area, then
  Gram-Schmidts the result against the normal and checks the outcome is finite and long enough to
  normalise, because an area that is small but above the threshold can still overflow the divide.
  The fallback is `normal.orthogonal().normalize()`, which cannot produce a non-finite value even
  for a zero normal.

  **`vec3::orthogonal()` had to be fixed first, and this is the part worth remembering.** It read
  `float x = abs(x);` for each component — locals shadowing the members and initialised from their
  own indeterminate values. Nothing had ever called it, so it had never failed; it is exactly the
  function this fix reaches for, and it would have returned garbage silently. The build carries no
  `-Wall`, which is why a self-initialisation sat in a header untouched. `core/type_vec3.h`.

  Verified by running `APP=Breakout` and summing the new per-primitive warning across the run:
  **5,149 of 8,264 triangles (62.3%) across 94 primitives**, every one of them from
  `data/glyphs_unispace.glb` — an exact match for the number measured independently when the item
  was raised. No other mesh in the app reports a single degenerate triangle, which is the evidence
  that the threshold catches unwrapped extrusions without firing on legitimate geometry.

  The loader is also no longer silent: each primitive that used a fallback logs how many of its
  triangles did, because "this mesh was never unwrapped" and "my normal map is broken" are the two
  readings of the same symptom and only one of them is actionable.

- [x] **44. `Shader` has no `Setvec2`, `Setvec4`, `Setbool` or array setters.** The ranked-worst
  finding of the Breakout run, and the only missing feature that visibly changed what that app
  shipped. `core/Shader.h` offers exactly five setters: `Setint`, `Setfloat`, `Setvec3`, `Setmat3`,
  `Setmat4`.

  The consequence is that **the uniform API, not the GPU, is the binding constraint on what a
  custom shader can be**. The shield effect's rectangle travels as two `vec3`s with a wasted
  component each, and its three impact ripples are `ripple0`/`ripple1`/`ripple2` — three
  hand-written `Setvec3` calls kept in step by hand with three hand-written uniform declarations —
  because three is how many the author was willing to type twice. With an array setter it would
  have been a ring buffer of eight carrying per-impact intensity in the fourth component. The
  effect was *designed around the limit*, which is the part worth reacting to.

  Each addition is four lines and the same `glProgramUniform*v` call the existing ones already
  make:

  ```cpp
  void Setvec2(const char* name, const vec2& value);
  void Setvec4(const char* name, const vec4& value);
  void Setvec4v(const char* name, const vec4* values, int count);
  void Setfloatv(const char* name, const float* values, int count);
  ```

  **Settle the failure modes in the same sitting.** One mistake — naming a uniform that is not
  there — currently produces three different behaviours: `Setmat4`/`Setmat3` call `debug->Fatal`
  and so `exit(1)` (`core/Shader.cpp:352,361`), `Setint` logs an error, `Setfloat`/`Setvec3` log a
  warning. They should all be fatal or none should. Note the fatal one is on the setter
  `Renderer::CustomShaderPass` calls on somebody's shader without asking, which is what makes item
  62 sharp — so if the answer is "all fatal", 62 becomes mandatory rather than nice to have.

  *Done 2026-09-12.* `core/Shader.h` now offers `Setint`, `Setbool`, `Setfloat`, `Setvec2`,
  `Setvec3`, `Setvec4`, `Setmat3`, `Setmat4` and the array forms `Setintv`, `Setfloatv`,
  `Setvec2v`, `Setvec3v`, `Setvec4v`. The v-forms take a count of *elements*, not of floats.
  `vec2`/`vec3`/`vec4` are contiguous floats — the unions inside them alias `x` with `r`, they do
  not change the layout — so every form hands the address straight to GL with no repacking.

  **The failure modes were settled in the same sitting, and the answer is "none fatal".** Every
  setter routes through one private `Shader::UniformLocation`, so there is one policy in one place
  instead of one per setter, and it warns once per name and returns `false`. Three reasons the mild
  behaviour won: a missing uniform is not evidence of a bug, because GLSL strips a uniform that is
  declared but never used; the fatal setter fired on shaders the engine does not own, since
  `Renderer::CustomShaderPass` sets `mat_worldcam` on every registered custom shader every frame;
  and it is a render-loop call whose success depends on what the shader compiler chose to
  eliminate, which is a poor place for `exit(1)`. A caller that genuinely requires a uniform checks
  the returned `bool` at a site where the name still means something.

  **This closes item 62's sharp edge as a side effect** — a custom shader with its own vertex stage
  that does not use the camera matrix no longer kills the process on the first frame. 62 itself
  stays open: its remaining content is moving the contract documentation onto `AddCustomShader` and
  `CustomShaderPass`, which is still worth doing.

  Two things found on the way. The location was being held in a `GLuint` and compared against `-1`,
  which only worked because both sides converted to `0xFFFFFFFF`; it is a `GLint` now.
  `Setint` carried the comment `//Warn once.` and then warned on every call — at sixty frames a
  second against a stripped uniform — so the Shader keeps a set of names already reported, cleared
  on relink so a hot reload that introduces a missing uniform is still heard about.

  **`core/glad.h` was the real binding constraint, not `core/Shader.h`.** The hand-trimmed loader
  declared exactly the five `glProgramUniform*` entry points the five old setters used, so adding
  setters meant adding `glProgramUniform1f`, `1iv`, `2fv` and `4fv` to `glad.h`/`glad.cpp` as well.
  Worth knowing before the next "just add one setter" — and worth knowing that this loader does not
  check any entry point for NULL after `wglGetProcAddress`, so a function that fails to resolve
  crashes on first call rather than reporting itself.

  Verified on the GPU rather than by inspection: a temporary block set each new form against a
  purpose-built uniform in `shaders/breakout_shield.frag` and read it back with `glGetUniformfv` /
  `glGetUniformiv`. All round-tripped exactly — `Setvec2` 1.5/2.5, `Setvec4` 1..4, `Setfloatv`
  10/20/30/40, `Setvec4v` 1..12 across three elements (confirming an array setter writes the whole
  run from the base location), `Setintv` 77/88, `Setbool` 1 — with no GL errors. Setting a name
  that does not exist returned `false` three times and warned exactly once. The harness was removed
  afterwards; `APP=Breakout`, `Ship`, `IsoAnimation`, `Tetris` and `Tank` all build.

  **The shield effect itself was deliberately left in its old shape.** It is a working effect, not
  a demonstration of the API, so rewriting it was out of scope for this item — but the comments in
  `shaders/breakout_shield.frag` that explained the constraint now say the constraint is gone and
  that the natural shape is a `vec2` origin, a `vec2` size and one `vec4` array of ripples carrying
  intensity in the fourth component. Anyone editing that file should reshape it rather than adding
  a `ripple3`.

- [x] **46. `Physics` did not expose the axis locks, and a flat game in a 3D solver needs them.**
  **Done 2026-09-12.** Eight one-line forwards on `core/physics/Physics`:

  ```cpp
  void SetLinearLockAxis(const vec3& factor);    vec3 GetLinearLockAxis();
  void SetAngularLockAxis(const vec3& factor);   vec3 GetAngularLockAxis();
  void SetLinearDamping(float damping);          float GetLinearDamping();
  void SetAngularDamping(float damping);         float GetAngularDamping();
  ```

  Getters as well as setters, because the rest of the class is symmetric that way
  (`GetVelocity`/`SetVelocity`, `GetBounciness`/`SetBounciness`) and because "what is this body's
  damping actually set to" is exactly the question item 47 makes people ask.

  **The capability was always in reactphysics3d. What was missing was any way to find out**, which
  is why these are forwards and not anything cleverer. `Object::GetRigidBody()` reaches the same
  calls and is the sanctioned escape hatch, so the cost this item carried was discoverability, not
  capability — and discoverability is what a wrapper is for.

  **`ApplicationBreakout` converted off the escape hatch.** Its power-up capsules were the case
  that found this: a capsule given a small push toward the camera drifted a little over a unit out
  of plane during its fall, passed the far face of the paddle's collider and sailed straight
  through a paddle sitting directly underneath it, generating no contact at all. It now reads

  ```cpp
  p->SetLinearLockAxis(vec3(1,1,0));      //pinned to the play plane
  p->SetAngularLockAxis(vec3(0,0,1));     //tumbling only about the axis facing the camera
  ```

  and the `rp3d::RigidBody*` and its `#include`-level knowledge are gone from that call site.

  **The header carries the why, not just the what** - that a flat game needs this on its first
  day, that the drifting axis is the one no readout shows on a flat-on camera, and the two-line
  recipe for pinning a body to a plane. That is the part that retires the item: the next flat game
  finds it by reading the class it is already using.

  **Damping is half of item 47, which stays open.** `AddBoxCollider` and `AddCapsuleCollider` set
  both dampings to 0.5 and box friction to 1.0 behind the caller's back while `AddSphereCollider`
  sets neither, so two bodies built the obvious way behave differently for reasons nothing states.
  These setters are the means to override that either way; whether those defaults should exist at
  all is still a decision to take, and changing them would alter the feel of every existing app.

  **Verified.** All twelve apps build. `tools/breakout_bot.py capsules` - the scenario that exists
  because of this bug - catches 7 power-ups across a run with a peak of 2 falling at once, which is
  the behaviour the escape-hatch version had. Nothing else in the repo used these calls, so there
  was nothing else to convert.

- [x] **47. `Add*Collider` set damping and friction behind your back, and asymmetrically.**
  **Decided and done 2026-09-12. The defaults are gone; the library's stand.** Dick's call, and the
  reasoning was the short one: none of those numbers was deliberate, so the engine should stop
  having an opinion and the setters from item 46 are there for anything that does.

  **What reactphysics3d actually defaults to**, which is the question that settled it:

  | | rp3d default | what `Add*Collider` used to force |
  |---|---|---|
  | linear damping  | `0.0` (`RigidBodyComponents.cpp:223`) | `0.5` on box and capsule |
  | angular damping | `0.0` (`:224`)                        | `0.5` on box and capsule |
  | friction        | `0.3` (`WorldSettings`)               | `1.0` on box only |
  | bounciness      | `0.5` (`WorldSettings`)               | never touched |

  So all four adders now set **only the density they were passed**, which is a parameter and
  therefore the caller's. The commented-out `setFrictionCoefficient(2)` / `setBounciness(0)` pairs
  in the sphere and capsule adders went too - same class of "did somebody mean this?" noise.

  **Why the old arrangement was worse than just being undocumented.** Damping is a property of the
  BODY and friction of the COLLIDER, so on a body with two box colliders the last one added set
  the damping for the whole thing. A shape swap from box to sphere silently changed how a body
  moved. And two bodies built the obvious way behaved differently with nothing anywhere saying so.

  **Two call sites converted rather than left to inherit.** `DozerCharacter` and `ShipCharacter`
  both already set linear damping to 0.5 themselves - through `rigidbody->setLinearDamping`, the
  raw escape hatch item 46's setters exist to replace. They now say `physics->SetLinearDamping(0.5)`
  next to the collider that used to hand it to them silently. Nothing else in the tree asked for
  damping at all.

  **What this changes, honestly.** Bodies slide further and tumble longer. Measured with a settle
  probe - step 900 ticks, snapshot every object, step 300 more, count what moved:

  ```
  Dozer     29 objects | 2 physics bodies still creeping (Beam 0.025 over 300 ticks)
  Tileset  200 objects | 0 still moving
  Ship     200 objects | 0 physics bodies still moving
  Tetris   200 objects | 0 still moving
  Breakout 108 objects | 0 at rest; after a game, debris creeps ~0.05 over 300 ticks
  ```

  (The "Main Camera" that shows up in that probe is view-owned, not a body.) Every scene still
  comes to rest and nothing slid off the world or fell through a floor - Dozer renders exactly as
  before. What used to stop dead now drifts to a halt, which is the library's behaviour and is now
  a thing an app can see and change.

  **The part nobody has measured, and it is the big one: box friction went from 1.0 to 0.3.**
  Floors are box colliders too, and rp3d combines the two surfaces, so every box-on-box contact in
  the engine is less than a third as grippy as it was. Nothing settles differently in the probes
  above, because static friction still holds a resting stack - but *pushing* things is Dozer's
  entire job, and how that now feels is a judgement rather than a measurement. Any app that wants
  the old grip asks for it: `SetFrictionCoefficient(1.0)`.

  **Not verified: `APP=Tank`**, which was being moved to its own place while this was done. Its
  hull is a box collider, so it loses 0.5/0.5 damping and its friction drops like everything
  else - worth a look when it lands, and worth doing before item 64's investigation, so that one
  is not chasing a change made here.

- [x] **48. A scalar axis had to be declared by mapping a fake key to it, and nothing said so.**
  **Done 2026-09-12, together with item 49 — they were one bug.** `InputController::GetAxis` and
  the `INPUT_EVENT_AXIS_SCALAR` applier both looked the action up in `keymap`, which only
  `AddKeyMap` wrote. `AddGamePadMap` filled a *separate* `gamepad_map` and created no `KeyState`,
  so the obvious one-liner

  ```cpp
  input->AddGamePadMap(0,INPUT_MY_STEER);   //looks complete, was not
  ```

  left `GetAxis` returning 0 forever and every scripted `HoldAxis` on that action dropped without a
  word. Apps worked around it by *also* calling `AddKeyMap(0,action)` to force a `KeyState` into
  existence — an idiom nothing wrote down.

  **Fixed by deleting the parallel table.** `GamePadMap` is gone; `KeyMap` absorbed it. A mapping
  now says which piece of hardware drives it with either a `system_keycode` (a key) or an
  `analog_index` (a stick), and `AddKeyMap` and `AddGamePadMap` are both one line over a shared
  `AddMapping`. So a gamepad axis gets an ordinary `KeyState` like everything else and is readable
  with `GetAxis`, drivable by `HoldAxis`, and recordable — which it never was.

  `AddGamePadMap` also gained `dead_zone` and `zero_offset` arguments. They used to be poked into
  the returned pointer, which is a pointer into a vector and so invalidated by the next mapping
  added; passing them means the caller keeps nothing. An out-of-range analog index is now refused
  and logged rather than stored.

  *This was Dick's call on the shape: "the gamepad map should just use keymap".*

- [x] **49. The gamepad's polled path and the scripted path never met.** **Done 2026-09-12, with
  item 48.** A real stick was polled into `analog_values` and read with
  `GetNormalizedAnalogValue`; a scripted axis arrived as an event and landed in `KeyState::fvalue`,
  read with `GetAxis`. The two never met, so an app wanting both had to add them by hand — and a
  recording of a run driven by a real gamepad would have contained none of the stick's motion,
  because the polled path never became an event.

  **Fixed:** `PollGamepad` now submits each mapped analog as an ordinary
  `INPUT_EVENT_AXIS_SCALAR`, so a thumb and a script land in the same `KeyState` and there is one
  place to read an axis from, whoever is driving it. `GetNormalizedAnalogValue` is now literally
  `GetAxis` and is kept only as a name four apps call.

  **The one subtlety, and it is load-bearing: events are submitted only when the value CHANGES.**
  A gamepad is polled, so it has a value every tick whether or not anyone is touching it; an event
  stream is not. A centred stick writing 0.0 every tick would stamp over whatever else was driving
  that action — which in practice means every scripted `HoldAxis` silently stops working the moment
  a controller happens to be plugged in. Reporting only transitions lets the two coexist (last
  writer wins, and a stick nobody is touching is not a writer) and keeps a recording small and
  honest: what gets written down is the thumb moving, not fifty identical samples a second of it
  resting. The disconnect and lost-focus paths zero the values and then submit, so a stick held at
  full deflection when the cable is pulled reports its way back to centre instead of staying
  latched.

  `ApplicationBreakout` lost its workaround: the `AddKeyMap(0,…)` declaration line, and the
  two-reads-added-together in `GatherInput` (which could also sum a stick and a script past full
  deflection) are now one `GetAxis` call.

  **Verification.** A standalone harness drives `InputController` directly with no window, no GL
  and no real controller — 17 checks, all passing: a gamepad-only mapping has a `KeyState` and
  reports as analog; `HoldAxis` on it reaches `GetAxis` and expires back to 0; a key mapping and a
  gamepad mapping on one action share a state with exactly one of them analog; the
  `AddKeyMap(0,x)` idiom still works unchanged (that is the path `ApplicationTank` uses for its
  five vehicle axes); `GetNormalizedAnalogValue` and `GetAxis` agree exactly; and an out-of-range
  index is refused. All twelve apps build, and `tools/breakout_bot.py play` still plays the game
  through scripted steering (five bricks broken, three balls alive) with a controller connected.

  *Kept from biting: a first in-game test looked like a regression — the paddle snapping to the
  wall — and was neither. `ApplicationBreakout` has mouse control on by default, Raw Input is
  registered `RIDEV_INPUTSINK` so it keeps delivering while unfocused, and a scripted hold opens
  the app's focus gate; so accumulated mouse movement reached the paddle. That is item 59, and it
  is the reason item 59 is worth doing.*

- [x] **50. A rules layer could not use `RRandom`.** **Done 2026-09-12.** `core/RRandom.h` included
  `Texture.h`, which includes `glad.h`, so asking for a seeded integer pulled in the whole OpenGL
  loader. The cost landed exactly where it hurt most: a rules layer — the part of a game
  deliberately written with no engine types in it — could not include the engine's own reproducible
  generator at all. `tetris/Playfield.h` and `breakout/Field.h` had each written the same nine-line
  xorshift32 that `RRandom::Generate` already uses, both citing this include.

  **Nothing in the class ever needed a Texture.** A Texture carries a GL id, a storage format, an
  upload path and a width and height; the noise needs bytes and a length. The 2D shape in
  particular carries no information — this is noise, so any `w*h` whose product is the buffer size
  is as correct as any other. So the member is now a plain `uint8_t* buffer` with a size, and
  `GetSquareSide()` derives a square on demand for anyone uploading it, instead of storing a shape
  that means nothing. `GetBuffer()`/`GetBufferSize()` expose the bytes for the GPU-or-network case
  the class was always built around.

  **The dependency was inverted rather than deleted**, which is the part worth keeping: an image
  still has to be decoded somewhere, and `Texture` already does exactly that. So `RRandom` lost its
  loader entirely — no `File.h`, no `stb_image`, no GL — and gained `UseNoise(data,size)`, which
  points an instance at bytes somebody else owns (not copied, never freed here). `Texture` gained
  `LoadIntoRRandomNoiseFile(filename,target,depth_in,rrand)`, which loads once and hands the pixels
  over, so one decode feeds both the GPU and the simulation with no duplicated loading code.
  **Texture knows about RRandom; RRandom knows nothing about textures, files or GL.** *That
  direction was Dick's call and it is the right one — the first cut had RRandom decoding its own
  PNG with stb_image, which removed the glad dependency but duplicated loading code that already
  existed.*

  `LoadFromTexture` is gone rather than renamed. Nothing in the tree ever called it; every app uses
  `Generate`.

  **Two things had to be added before the rules layers could actually switch over**, both found by
  doing it rather than by planning it:
  - **`SetSeed(uint32_t)`.** Both games re-seed on `NewGame`, and `RRandom`'s seed was
    constructor-only. It refills a private buffer in place at its existing size and rewinds the
    cursor (so the same seed always deals the same stream), moves only the start offset on a
    buffer from `UseNoise` (the noise there belongs to somebody else), and otherwise records the
    seed for the next `Generate`. Note an earlier `SetSeed` was *removed* under item 22 for writing
    a field nothing read; this one is its opposite and acts wherever there is something to act on.
  - **A one-time error when drawing from an empty generator.** `Get_uint8` returned 0 forever and
    silently, which from the outside looks like a generator that always rolls the minimum. It now
    says so once.

  **Both rules layers converted.** `TetrisRandom` and `BreakoutRandom` are deleted; each game owns
  an `RRandom` sized once in its constructor (`TETRIS_RANDOM_BYTES` 65536, `BREAKOUT_RANDOM_BYTES`
  16384) and re-seeded per game by `NewGame`. One conversion trap worth recording: `Roll` took a
  *percentage* in both hand-rolled generators and takes a *chance in 0..1* in `RRandom`, so
  `Roll(9)` would clamp to 1.0 and quietly turn every brick in Breakout into a prize rather than
  one in eleven. It is now `Roll(0.09f)`, with a comment saying why.

  **Left open deliberately:** the stream is finite and wraps, because it is a buffer. That is now
  documented at the top of `core/RRandom.h` with the arithmetic needed to size one (`GetInt` spends
  four bytes), and both games are sized orders of magnitude above what they draw — but it is a real
  property of the design and a future rules layer should know it. Also still true, and still the
  residual under item 22: a single shared stream only replays if every draw happens on the
  simulation thread in tick order, which is why each rules layer owns its own instance rather than
  borrowing `Application::rrand`.

  **Verification.**
  - The generated stream is **byte-identical to before the change** for seeds 1, 2 and 3, checked
    against a standalone replica of the old xorshift fill — so no existing app's behaviour moved.
    `Generate(4096)` and `Generate(64,64)` agree, `GetSquareSide()` gives 256 for 65536, and an
    8-byte buffer demonstrably repeats after 8 draws.
  - `RRandom.h` now pulls in `type_vec3.h`, `type_vec2.h` and `type_helpers.h` and nothing else;
    `RRandom.o` links against `Debug` and `type_helpers` alone.
  - **All twelve apps build.**
  - `APP=Tetris` run: seed 7 deals `T / S,L,I` on three consecutive restarts and seed 99 deals
    something else, so `SetSeed` genuinely re-seeds; screenshotted with piece, ghost, next queue,
    text labels and field shadows all correct.
  - `APP=Breakout` run: `tools/breakout_bot.py determinism` reproducible on every field checked,
    and the level builder reports 8 prizes out of 66 bricks (≈12%, against the intended one in
    eleven) — which is the check that the `Roll` unit conversion landed, since getting it wrong
    gives 66 or 0.

- [-] **51. A rules layer cannot ask what the tick rate is.** **Declined 2026-09-12 — documented
  instead, and one app fixed.** The item asked for a way for a rules layer to obtain the simulation
  rate, because a game with a *continuous* position (a ball swept across a field, as against a grid
  game that only counts ticks) needs the timestep, and a header with no engine types in it cannot
  call `Scene::GetPhysicsTimestep()`.

  **The premise was already wrong in the app it came from.** `ApplicationBreakout.cpp` reads
  `SetPhysicsTPS(BREAKOUT_TPS)`, taking the constant from `breakout/Field.h`. There is no
  duplication there and nothing that can drift: the rules declare the rate, the app applies it.

  **And that is the correct arrangement, not a workaround, because the rules OWN the rate.** Their
  constants are denominated in it. Tetris's gravity table is the NES frame counts, so 60 Hz is not
  a preference the engine grants — it is what makes that table mean what it says. A rules layer
  asking the engine "what rate am I running at?" has the dependency backwards; it should be telling
  the app. One direction, one source of truth.

  **The inability to ask is the same fact from the other side, and it is a feature.** A rules layer
  that is a pure function of (previous state, this tick's input) is what makes a game replayable. A
  rate fetched at runtime would be one more input to record.

  **The item's own suggested fix — passing `dt` into `Tick()` — would be actively worse.** The
  engine deliberately keeps the timestep CONSTANT for the life of a run
  (`Application::GetPhysicsTimestep`; `physics_time_factor` scales how OFTEN ticks run and never
  how long one is) precisely so that a recorded run replays. Handing the rules a per-tick `dt`
  reopens exactly the door that was closed on purpose, and buys nothing a constant does not already
  give.

  **What WAS a real defect, and is fixed: `ApplicationTetris` had the rate as a literal.**
  `SetPhysicsTPS(60.0f)` sat in the app while `Playfield`'s gravity table silently depended on it,
  so changing one without the other would have left every number in that table quietly meaning
  something else with nothing to catch it. Now `TETRIS_TPS` lives in `tetris/Playfield.h` next to
  the rules that are denominated in it, `ApplicationTetris::Init` passes that, and the gravity
  table's own comment names it as the reason the constant is 60. `APP=Tetris` builds and runs.

  The reasoning above is recorded at the `TETRIS_TPS` definition rather than only here, because the
  person who needs it is the one writing the next rules layer, and what they will be reading is a
  rules layer.

- [x] **53. The `LoadFile` / `BinaryAsset` ownership question, decided.** **Done 2026-09-12.**
  **`LoadFile` lends; it does not give.** What comes back belongs to the file layer, stays valid
  for the life of the process, and nobody frees it. If a caller wants bytes it can keep or change,
  the caller copies them.

  **The reasoning, which is the part worth keeping.** `LoadFile` is the one thing that decides
  *where* the bytes come from — a file on disk, the `BinaryAsset` cache, an asset baked into the
  executable, tomorrow perhaps an archive. It cannot know which of those the caller wanted a
  private copy of. The interim fix made it copy on every path so that "the caller owns it" was
  true everywhere; that made it allocate on behalf of five callers who never asked and never
  freed. Deciding where bytes come from and deciding who owns them are different jobs, and only
  the first one belongs to `LoadFile`.

  So: one file, one buffer, one owner. A second load of the same name returns the *same pointer*
  as the first, not a duplicate. `StoreBinaryAsset` now **adopts** the disk read instead of
  copying it, so the first load stopped copying too, and `LoadFile` is thirty lines shorter than
  the version it replaces.

  **The three real owners this had to unpick**, because inverting the contract turns a surviving
  `free()` into heap corruption rather than a leak:

  - `Texture` freed `file_data` in three places (destructor, the overwrite branch of
    `LoadFromFile`, and the atlas-merge finalise). All three now drop the pointer instead.
  - `WaveFile::~WaveFile` freed it. Now nulls it; `header` and `wav_data` are offsets into the
    same buffer and go the same way.
  - **`ImGui` was the hidden third owner.** `AddFontFromMemoryTTF` takes ownership by default and
    frees the buffer with the atlas — which would have freed the cache's buffer while the cache
    went on handing that pointer out. `Window` now sets `config.FontDataOwnedByAtlas = false`,
    which is also what ImGui itself prefers since 1.92: it no longer copies for that flag and it
    requires the data to outlive the atlas. A buffer that lives as long as the process is exactly
    that. Nothing in the compiler or the API would have caught this one.

  The other five callers (`Shader`, `GLTFLoader`, `OBJLoader`, `HTTPServer`, `Application`) needed
  no change and simply stopped leaking.

  **`bypass_cache` is gone, replaced by a function that says what it is.** A flag that flips who
  owns the return value is the trap this item is about, in miniature. Its one caller wanted a
  *fresh* read of a file that changes, and got a second ownership rule as a side effect. That is
  now `ReadFileToString(filename, out)` — caller-owned by construction, re-reads every call, and
  **returns false instead of calling `Fatal`** when the file is missing.

  **That last part fixed a live remote-kill.** `HTTPServer` served `/modes.json` and `/style.css`
  through `LoadFile`, whose failure path is `debug->Fatal` — which exits the process. `data/modes.json`
  does not exist in this repo, so any browser requesting it **killed the app**, and the 404 branch
  sitting right there in the handler could never run. Both handlers now read non-fatally.
  Demonstrated against a running `APP=Tileset`: `GET /style.css` -> `200`, `GET /modes.json` ->
  `404 {"error":"not found"}`, app still answering MCP afterwards.

  It also took the HTTP worker threads off the cache. `HandleHTTPConnection` spawns a thread per
  client, and those threads were calling `LoadFile` — so a client connection could `push_back` onto
  `file_assets` while the render thread was walking it. They now touch nothing shared.

  `FindBinaryAsset` — declared, never defined, never called — deleted.

  **Verified.** All twelve apps build. A 31-check harness over the file layer with no window and no
  GL (`file_owner_test.cpp`, linking `File`/`BinaryAsset`/`WaveFile`/`Debug`) passes: four loads of
  one wav return one pointer, four `WaveFile`s over that wav construct and destruct without
  disturbing it, two live at once share it, a different file gets a different buffer, and
  `ReadFileToString` reads independently of all of it. In-app: `APP=Tetris` starts clean with
  click.wav and bleep.wav each loaded twice and parsed correctly both times, and its ImGui panel
  renders in Consola — the font atlas proving it is happy with a buffer it does not own.
  `APP=Breakout` (15 files, 7 cache hits) and `APP=Tileset` (which exercises the atlas-merge path
  four times) both start clean.

  **What this does NOT fix, and it matters for item 61:** the cache still has no way of being told
  that a file it holds is stale, so `LoadFile` can never see an edit. Measured, not assumed — the
  harness writes a file, loads it, rewrites it, loads it again and gets the original bytes back.
  `ApplicationShip::ReloadVolumeShader` is commented "recompiles from disk" and does not: it goes
  through `LoadFile`, gets the bytes read at start-up, and recompiles an identical program. Hot
  reload has never picked up an edit. See item 61.

- [x] **54. `BinaryAsset::GetBinaryAsset` returned a pointer into a growing vector.**
  **Done 2026-09-12.** `BinaryAsset::file_assets` is a `std::deque<BinaryAsset>` instead of a
  `std::vector<BinaryAsset>`. One word, one `#include`, and the whole class of failure is gone:
  a deque's `push_back` invalidates iterators but **not references or pointers to elements already
  in it**, and nothing is ever erased from this container, so a pointer handed out by
  `GetBinaryAsset` stays good for the life of the process.

  **The window was real, not theoretical — the original entry undersold it.** That entry said
  `LoadFile` uses the returned pointer immediately and does not keep it, so the exposure was one
  function call. True, but it hid the fact that stores keep arriving *after* hits. Read off a plain
  `APP=Tetris` start-up, with the escape codes stripped:

  ```
  LoadFile: File shaders/default_skinned.vert is 6018 bytes
  Got BinaryAsset shaders/deferred.frag from cache        <- pointer handed out here
  LoadFile: File shaders/ssao_compute.comp is 3618 bytes  <- ...and a push_back here
  Got BinaryAsset shaders/default.vert from cache
  LoadFile: File shaders/default.frag is 24883 bytes
  Got BinaryAsset data/sound/click.wav from cache
  LoadFile: File data/sound/bleep.wav is 336440 bytes
  ```

  Every cache hit in a normal run is followed by more stores. The pointers survived only because
  each one was already dead by the time the next `push_back` reallocated. So the code was correct
  by *timing*, which is a property you cannot read off the page — and `LoadFile` is exactly the
  function someone rearranges (it was rearranged this month, for the mismatched-free bug above it).

  **Why a deque and not the alternatives.** A `std::vector<BinaryAsset*>` fixes it too but buys a
  second allocation per asset and a delete nobody is going to write. A `std::list` gives the same
  guarantee and costs a node and a pointer chase per element for a container that is only ever
  appended to and walked front to back. A deque is the vector with the one property this code needs
  bolted back on, and the only thing it gives up — contiguity — is something nothing here ever used:
  the three operations on `file_assets` are range-for, `push_back` and `size()`.

  **The header carries the reasoning at the member**, because the container choice is the fix and a
  future tidy-up would otherwise read `std::deque` as an odd preference and "simplify" it back.
  `GetBinaryAsset`'s `return &asset;` points at it from the other end.

  `#include <vector>` is gone from `BinaryAsset.h` along with the member; all twelve apps still
  build, so nothing was leaning on it transitively.

  **Verified.** All twelve apps build. `APP=Tetris` runs, and the four cache hits above are the
  path this item is about — the two sound files among them are `SoundSystem` being handed the same
  wav twice, which is the arrangement that found the heap corruption in `LoadFile` in the first
  place.

- [x] **55. `Physics::SetTrigger` / `IsTrigger` dereferenced a collider that may not exist.**
  **Done 2026-09-12.** Both now guard on `body && body->last_collider`, exactly as `SetBounciness`
  and `GetBounciness` immediately above them already did — they were the only two methods in the
  file that reached through `last_collider` without checking.

  **The asymmetry between them is deliberate.** `IsTrigger` returns `false`: a body with no
  collider genuinely is not a trigger, so there is a truthful answer to give and nothing to report.
  `SetTrigger` logs an error, because a set that silently does nothing leaves the caller believing
  it has a trigger, and the symptom — contacts still being generated — surfaces somewhere else
  entirely, looking like a physics bug rather than an ordering mistake.

  **It was a real crash, not an inferred one.** The original entry said *"inferred from reading;
  the Breakout app used no triggers, so it did not fire."* Confirmed by building the same 9-check
  harness twice, once against the guarded file and once against a copy with only these two guards
  stripped:

  ```
  guarded:    9 checks, 0 failed
  unguarded:  Segmentation fault (exit 139)
  ```

  The window is genuinely easy to hit: `AddPhysics` deliberately creates a body and leaves the
  colliders to the caller, so every body is in this state between those two calls.

- [x] **56. The deferred G-buffer's clear values cannot be tested for emptiness.**
  **Done 2026-09-12 — documented, which is what the item asked for.** Stated at the
  `TEXUNIT_GBUFFER_*` defines in `core/Renderer.h`, because that is what a custom shader author is
  already looking at: **depth is the channel that answers "is there anything here"** — cleared to
  1.0, outside the range any fragment can write, so `depth < 1.0` means geometry and nothing else
  does — and **position and normal are only meaningful once depth has said yes.** With the working
  pattern written out, since two shaders in the repo already use it.

  **The item had the specifics wrong, and the truth is worse.** It said position was cleared to
  `(1,0,0,0)`. Position is `COLOR_ATTACHMENT0` and is cleared to `(0,0,0,0)` — **the world
  origin**, which is not merely a plausible place for geometry but, in a game built around the
  origin, where all of it is. `(1,0,0,0)` is the *normal* buffer, where it is a unit +X normal and
  just as legal. Nor is `w` an escape: `deferred.frag` writes the material's alpha there.

  Reading an attachment number off that clear block wrongly is easy — see item 66, which is what
  came of checking. The block now names every attachment and its clear value.

- [x] **57. `Mesh::custom_shader_index` defaulted to 0, so a forgotten tag drew with the wrong
  shader.** **Done 2026-09-12.** The default is now **-1, meaning not assigned**, and
  `Renderer::RenderUniqueMeshes` skips such a mesh and says so once, naming the mesh id.

  The old default saved an app with exactly one custom shader from tagging its mesh, and charged
  every app with two: forgetting a tag silently drew the mesh with the **first** registered shader.
  A mesh drawn by the wrong shader looks like a shader bug and gets hunted as one; a mesh that
  draws nothing and explains itself in the log is found in a minute.

  **One app was relying on the old default and had to be converted** —
  `ApplicationIsoAnimation` registered its indicator shader, discarded the returned index and left
  the test plane untagged, with a comment saying it was leaning on the default being 0. It now
  keeps the index and tags the plane. That app *is* the case the old default existed to serve, and
  converting it cost two lines.

  **Verified both ways.** All twelve apps build; `APP=IsoAnimation` still draws its indicator arc
  on the tiled plane and logs no warning. Then, deliberately, with the tag commented out again:

  ```
  [ err ] Renderer : Mesh id 1 is MESH_MODE_SHADER but has no custom_shader_index - set it
                     to what Renderer::AddCustomShader returned, or it will not be drawn
  ```

  Exactly once, not once per frame, and the mesh vanished instead of borrowing a shader. Tag
  restored afterwards.

- [x] **58. `SetCollisionCategoryBits` had to be called after the colliders existed, and nothing
  said so.** **Done 2026-09-12, by removing the ordering requirement rather than documenting it** —
  Dick's call: re-apply the bits when a collider is added.

  **The filter is now a property of the BODY.** `Physics` remembers the category and mask, every
  `Add*Collider` stamps them onto the collider it just made, and the setters still reach every
  collider that already exists. Set them before the shape, after it, or twice — it no longer
  matters. `Object`'s setters keep a copy for the clone path and delegate to `Physics`, and
  `Object::AddPhysics` hands the stored value to the body it creates, so setting a filter on an
  object that has no body yet works too. That closes both orderings, not just the one the item
  named.

  **The defaults had to change from 0 to rp3d's own, and that is the interesting part.** `0` is
  not "unset" — it is a real filter meaning *in no category, collides with nothing*. Re-applying a
  stored 0 to every collider would have made every body in the engine non-colliding, so the fix
  is only safe once `Object` and `Physics` start out holding `0x0001`/`0xFFFF`, which is exactly
  what rp3d gives a collider it creates (`Body.cpp:90`, `RigidBody.cpp:685`). A body that never
  touches these is therefore bit-for-bit as it was.

  **It also fixes a latent bug in the clone path.** `Object`'s copy constructor already called
  both setters with the source's stored bits — so cloning an object that had never set them
  stamped category 0 / mask 0 onto the clone, giving it a body that collided with nothing. Nobody
  had noticed because the apps that clone (Dozer especially) all set their filters on the source
  first. Worth knowing that this changes behaviour for any clone of an unfiltered source: it used
  to be a ghost and is now solid.

  **Verified.** All twelve apps build. An 8-check harness over `Physics` with no window and no GL
  proves the mechanism from both ends: filter-then-shape now lands on the collider (the order that
  used to be silently wrong), shape-then-filter still works, a second and third collider added
  later inherit the same filter, changing it afterwards reaches all three, and a body that never
  asks keeps rp3d's defaults.

  In-app, the risk was the clone-default change, so `APP=Dozer` — the heaviest cloner in the
  repo — was stepped 600 ticks: 29 objects, nothing below y=-5, the lowest being the floors at
  their designed -4.7. `APP=Breakout`, whose gameplay filters are load-bearing, plays normally.

  *A note on that last one, because it looked like a regression and was not:*
  `tools/breakout_bot.py capsules` caught 3 power-ups where an earlier run of the same scenario
  caught 7. Three runs on the same unchanged build gave **3, 0 and 7**. The scenario is dominated
  by run-to-run variance — which is item 25's finding, that MCP-driven input is not tick-aligned —
  so its power-up count is not a regression test and should not be quoted as one.

- [x] **59. A mouse delta accumulated across a pause arrived as one jump.** **Done 2026-09-12.**
  `InputController::Tick` cleared a delta only if something had read it that pass (`f_processed`),
  and a relative axis ACCUMULATES (`INPUT_EVENT_AXIS_RELATIVE` adds). So anything that stopped the
  readers banked movement without limit: a paused simulation above all, since no tick runs and so
  no gameplay reads, but equally an app whose input gate returns early. The whole pile then arrived
  on the first read after the resume and whatever it drove teleported.

  **The conditional clear was not a mistake, which is why the fix is not simply to clear
  unconditionally.** A delta is not necessarily consumed by the simulation: the render thread reads
  mouse deltas for camera mouse-look (`Application::UpdateUICameraControls` -> `GetDelta`) at
  framerate, and `Tick()` runs at the END of a physics pass specifically so that window has opened
  (see the comment at its call site in `Application::PhysicsThreadFunction`). Clear immediately and
  mouse-look reads zeroes.

  **Fixed by giving the grace a bound instead of leaving it open-ended.** `KeyState` gained
  `delta_unread_passes`; `Tick` clears the delta when it is read, and otherwise ages it and drops
  it past `INPUT_DELTA_GRACE_PASSES` (2, about 40 ms at 50 Hz). Several frames of grace for the
  render thread, and a hard cap on how much travel a pause can bank.

  **Verified** in the standalone input harness: 50 passes of +10 with nobody reading leaves 20
  banked rather than 500; a delta is still readable on the pass it arrived, is cleared once read,
  and still survives one unread pass - which is the mouse-look case. Live in `APP=Breakout`, the
  app the symptom was found in: paused, then scripted steering, the paddle now travels smoothly
  instead of snapping to the wall on the first step.

  *Note the diagnosis in `docs/breakout_findings.md` §7 (and an earlier note of mine) said the
  movement arrived because Raw Input keeps delivering while unfocused. That part is wrong:
  `SubmitAxisDelta` drops deltas outright when unfocused. The accumulation is purely the
  unbounded-grace bug above, and it happens while FOCUSED and not reading - which a pause
  guarantees.*

- [x] **62. The custom-shader pass's contract is documented in the wrong place.** **Done
  2026-09-12.** Both halves are now closed.

  **The dangerous half was item 44's**, and is recorded there: `CustomShaderPass` sets
  `mat_worldcam` on every registered custom shader every frame, and `Shader::Setmat4` on a missing
  uniform used to go through `debug->Fatal` → `exit(1)`. GLSL strips a declared-but-unused
  uniform, so a custom shader with its own vertex stage that happened not to use the camera matrix
  killed the process on the first frame — no window, and a message only on stderr. Every setter
  now warns once and returns `false`.

  **This half was the documentation, and the item's own description of where it lived was out of
  date.** It said the contract was explained inside `Renderer::UploadCloudShadow`, about a
  different function; by the time this was picked up the pass mechanics had already been moved
  onto `CustomShaderPass` itself. What was actually missing was anything at all on
  `AddCustomShader` — which is the function an app author calls, and therefore the one they read.

  **So the contract is stated once, on `AddCustomShader` in `core/Renderer.h`**, covering what a
  custom shader is handed and what is expected of it:

  - the two lines that tag a mesh (`mesh_mode` *and* `custom_shader_index`), and that -1 means
    untagged since item 57, so an untagged mesh is skipped with a message rather than drawn by
    whichever shader was registered first;
  - what the engine sets on your shader every frame (`mat_worldcam`, `eye_position`) and what it
    pointedly does not (the shadow matrices, the cloud-shadow and field uniforms) — so `vshadow`
    is meaningless in a custom shader that reuses `default.vert`, which is a convenience and not a
    requirement;
  - that a missing uniform is a warning and not a death, and why that is the ordinary case rather
    than a mistake;
  - what is bound when it runs: the three G-buffer texture units, **with a pointer to the
    `TEXUNIT_GBUFFER_*` block for item 56's trap** — depth is the only channel that can say
    whether anything was drawn, and reaching for position first is the mistake everyone makes —
    and SSBOs 0/1/2/4 (instance, material, light, bone), which `InitSSBO` binds once and which
    stay bound;
  - `uniform_callback` for your own uniforms, and that it may change cull face, depth mask and
    depth test, all restored after each sub-pass;
  - that indices never move, and that a hot reload replaces the entry at its index rather than
    registering a second copy — which would leave every tagged mesh pointing at the stale shader.

  `CustomShaderPass`'s own comment keeps the pass mechanics (why it runs last, why `DeferredPass`
  moved before the colour pass) and points at the contract instead of restating it. The
  three-line comment that used to sit above `AddCustomShader` is folded in, so there is one place
  and not two.

  **Cleanup: one stale claim, left behind by item 44.** `UploadCloudShadow`'s comment still said
  *"Setmat4 goes through debug->Fatal if the uniform is missing, so it is only called when there
  is a map to point at"* — the first clause has not been true since item 44, and it was the stated
  reason for the second. The guard is still right, for a better reason (there is nothing
  meaningful to hand over), and the comment now says that, with the history in brackets. Swept for
  others: `Shader.cpp`'s long block also mentions the fatal behaviour but in the past tense,
  describing why the policy changed, so it is history rather than a stale claim.

  **Verified.** All twelve apps build. `APP=Breakout` renders identically to the reference capture
  — its shield is a custom shader reading `gbuffer_depth` and `gbuffer_position`, so it exercises
  the contract being described — with no warnings and no GL errors.

- [x] **65. A scripted axis hold could not change its value.** **Found and fixed 2026-09-12 while
  verifying 48/49/59.** `AdvanceSyntheticHolds` emits an axis's value only on the tick it starts
  (`if (!h.f_started)`), and `AddSyntheticHold` deliberately left `f_started` alone when
  re-asserting an existing hold - correct for a BUTTON, where re-emitting would be a second
  key-down, and wrong for an AXIS, whose value IS the entire content of the event.

  So re-asserting a live hold with a *different* value updated `h.value`, emitted nothing, and left
  `KeyState::fvalue` on the old value for as long as the hold kept being refreshed. The command was
  accepted and silently did nothing.

  **This is the ordinary case for a scripted player, not an edge case:** hold steer at +1, then at
  -1 before the first hold expires. Both of this repo's bots steer exactly that way, and it reads
  as a control that has stuck - a paddle pinned against one wall refusing to come back, which is
  how it was found.

  **Fixed** in `AddSyntheticHold`: an axis re-assert whose value differs clears `f_started` so the
  new value is emitted. A button is untouched, and re-asserting the same value still just extends
  the hold without re-emitting.

  **Verified** in the harness (reverse mid-hold reaches `GetAxis`; re-asserting the same value
  keeps it) and live: the Breakout paddle now tracks +1/-1/+1/-1 cleanly across the full arena,
  where before it travelled right and then refused to come back.

---

## Appendix: the interim assessment of item 22 (`RRandom`)

Kept because it records the state between the two passes of that fix, and because its conclusion —
that a shared stream only replays if every draw happens on the simulation thread in tick order —
is still live, and is carried forward in the open backlog under items 25 and 50.

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

- [x] **66. The G-buffer clear addressed the wrong buffers, and the code carried a workaround for
  the symptom.** **Found and fixed 2026-09-12 while documenting item 56.**

  `glClearNamedFramebufferfv`'s third argument is a **draw buffer index, not an attachment
  number** — it indexes the list handed to `glNamedFramebufferDrawBuffers`. That list is
  `{ATTACHMENT0, ATTACHMENT1, ATTACHMENT3}`, so it skips ATTACHMENT2 and the indices shift:

  ```
  draw buffer 0 -> ATTACHMENT0  position
  draw buffer 1 -> ATTACHMENT1  normal
  draw buffer 2 -> ATTACHMENT3  object id      <- an INTEGER texture
  draw buffer 3 -> nothing, the list has three entries
  ```

  The clear block read them as attachment numbers. So the **integer** object id texture was being
  cleared by the **float** call at index 2 with `(1,0,0,0)`, and the integer clear meant for it,
  aimed at index 3, addressed a draw buffer that is not set and silently did nothing.

  **The codebase had already recorded the symptom without finding the cause.** In `DrawFrame`'s
  readback:

  ```cpp
  //Somehow, -1 reads back as 3F800000
  if ((id_pixeldata[0] != 0x3F800000) && (id_pixeldata[0] != -1)){
  ```

  `0x3F800000` is the IEEE-754 bit pattern of `1.0f`. That is precisely the float clear landing in
  an integer texture. The "somehow" was this.

  Fixed by clearing draw buffer 2 with `glClearNamedFramebufferiv`, and the readback now just
  tests `!= -1`. The mis-aimed `fv` at index 3 is gone. Note this was never touching the SSAO
  texture on ATTACHMENT2, which is not a draw buffer in this pass.

  **No visible bug was being caused** — both sentinel values were excluded by the workaround, so
  picking behaved — which is exactly why it survived: the cost was a magic number nobody could
  explain and a type-mismatched clear whose result the spec does not define, working only because
  this driver does the obvious thing.

  **Verified.** All twelve apps build. With the workaround removed, a wrong mapping would spam
  "Read back object index 1065353216 is out of bounds" on the first frame; sweeping the mouse
  across five points of the `APP=Ship` viewport, including empty regions, produces **zero** such
  errors and zero GL errors from the debug callback. Positively: clicking the ship selects
  `Ship #22` in both the scene tree and the Inspector, so the id buffer still reads real objects.
  `APP=Breakout` renders unchanged, custom shield shader included — it is the thing that reads
  this G-buffer.
