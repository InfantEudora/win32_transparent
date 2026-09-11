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

- [ ] **16. Primitive mesh generators** — `MakeBox` / `MakeQuad` / `MakeSphere`. `data/unit_cube.obj`
  is currently the only cube in the engine, and hand-building a cube is 45 lines
  (`ApplicationShip.cpp:80-125`). ~30 lines each, zero risk, removes a file dependency from every
  app. *Later.*
- [ ] **17. A hook that runs once per simulated tick.** `RunLogic` is documented as the per-tick
  gameplay hook but is called on every pass of the physics loop, which keeps spinning while paused
  so a key can unpause it; the pause and single-step counters are honoured one function later in
  `Scene::UpdatePhysics`. So gameplay written the obvious way keeps playing while paused, and
  single-stepping advances it by loop iterations rather than by ticks. Proposal is a second,
  **additive** virtual (`RunSimulationTick`) so no existing app changes behaviour. **Open for
  discussion.**
- [ ] **18. `Scene::ReadAtTickBoundary(fn)`** — the read-side mirror of `SubmitCommand`. An MCP
  tool handler holds no lock, so reading scene state races the physics thread. Without this every
  app grows its own hand-maintained snapshot that drifts. *Later.*
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
  (layout, hit-testing, focus) is weeks rather than days. *Later.*

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
- [ ] **31. `Camera::GetPixelRay` is not viewport-offset aware.** It divides by
  `viewport.width/height` (the viewport's size) but takes a pixel coordinate relative to the
  *window*, so with a non-zero `viewport_x`/`viewport_y` the caller must subtract the offset
  itself. Pre-existing — `viewport_x` has always had this — and item 10 does not make it worse, but
  it should either take viewport-relative coordinates or subtract the offset itself. *Later.*
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
