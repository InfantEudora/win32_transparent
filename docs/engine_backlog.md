# Engine backlog

**Open work only.** Everything already done or decided against moved to
`docs/engine_backlog_done.md` — 70 closed items, with their verification notes intact, because
those notes are what a later regression gets checked against.

Most items are drawn from two runs in which an agent built a game on this engine as an audit of it:

- `docs/tetris_findings.md` — `APP=Tetris`, items 1-43.
- `docs/breakout_findings.md` — `APP=Breakout`, items 44-62, plus new notes on 15, 23, 25 and 39.
  Items 63, 64 and 65 came out of reviewing and working on that run rather than out of the report.

Item 67 was the first that came from neither: it is a design decision taken up front, before any of
the code it describes exists.

Items 68-77 are a third source — the **Android port** at `C:/code/android`, which took this engine
as its inspiration and got Tetris running on a tablet. Some are bugs it found in code it inherited,
some are things it had to build that this engine has no equivalent of. Two of them (73, 75) are
really one thread: that port has host **build tools** that link the engine core, which is a kind of
consumer this repo has never had, and it is what the optional-subsystem flags are for. See the
reference at the bottom for what it found that needed no item, and for how to do the merge itself.
That port is also why item 67 moved from band D to band C: most of it is written.

Items 79-82 are a fourth: **shipping Tetris**. It is close to a complete first game and to being
the example of what this engine is for, and the question of how small it can be while keeping what
makes it worth playing turned out to have a measured answer rather than an opinion. The reference
at the bottom is that measurement; the four items are what it suggests doing, in order.

Ordered by **implementation effort, not by importance** — that is what the bands are, and it is
deliberate: this list is read when someone has an hour free as often as when someone is deciding
what matters. Where an item is more important than its band suggests, it says so in its own text.

Numbers are stable. Nothing is renumbered, nothing is reused, and **28 was never assigned**. An
item that moves between bands keeps its number.

Status key: `[ ]` open · `[~]` partially done.

Last updated 2026-09-13: items 68-77 from the Android port, 79-82 from measuring `tetris.exe`
against the goal of shipping it, and 67 moved to band C. Item 78 came from the pinball work in
between. Item 80 and the size reference were then corrected after building openal-soft to check
them - the resampler tables are `.bss` and cost no file bytes, which the first version got wrong.
Band B's heading was restored at the same time — it was lost when item 41 closed, which left 43
and 61 stranded under a band A that says it is empty.

On 2026-09-14, later: **items 74 and 82 closed and moved**, and with them the optional-subsystem
flag family is complete. `USE_NET`, `USE_MCP` and `USE_IMGUI` join `USE_SOUND`; all three default
ON, because MCP and the debug panels are how work in this repo gets done and losing them by
accident would be a worse outcome than a larger binary. A shipped build turns them off on purpose.
Tetris end to end: **5,792,659 bytes in 23 files -> 3,397,632 in one, 41% smaller**, with no asset
folder, no port bound and no debug UI. Getting there also moved two thirds of `core/Application.cpp`
out of it - 3327 lines to 1157 - because that is how much of it was debug interface. The OCPP
protocol left `core/HTTPServer` on the way (every app had been linking a charge-point client).

On 2026-09-14: **item 75 closed and moved** - assets are packed by `tools/assetpack`, a separate
exe, and the engine's `DUMP_BINARYASSETS` self-dump is gone. Tetris now builds as a **single
4.58 MB file with no `shared_assets` folder**, 23% smaller than the exe-plus-tree it replaces, and
proven by running it alone in an empty directory. `apps/ui` bakes by default; every other app is
unchanged and opts in with `BAKE_ASSETS=1`. Plan and measurements in `tools/assetpack_plan.md`.
**Item 89 opened** out of it: `Application::Init()` is dead code and the graphics prologue it holds
is copy-pasted into all fourteen apps.

On 2026-09-14, later still: **items 90 and 91 opened**, out of `make ship` landing in `engine.mk`
(release + baked + `USE_MCP=0` + `USE_NET=0` + `USE_IMGUI=0`, named once rather than typed out) and
out of teaching `BinaryAsset::ListBinaryAssets()` to report the packed table rather than only the
disk one. Tetris **packs 20 assets and requests 16**; the four it never touches are the skybox pair,
`shaders/texture.comp`, and `fonts/consola.ttf` — that last only because `USE_IMGUI=0` drops the
code that loads it. Excluding them by hand became item 90, which bit immediately — the exclusion was
flag-blind, so `CONFIG=release BAKE_ASSETS=1` with ImGui still on died at startup with no
`consola.ttf` to load — and **item 90 closed the same day**, taking two further bugs with it that
only became visible once an exclusion mattered: the pack did not re-run when `ASSET_PACK_FLAGS`
changed, and `$(GENERATED)` had to become per-variant so `make ship` and the measurement build stop
reusing each other's table. Tetris now packs **16 and requests 16** — no dead weight in either
variant — and the ship exe is 2,984,448 bytes. Of the 413 KB that came off, **256,046 bytes is the
exclusions**; the rest is `bleep.wav` and `click.wav` being re-encoded by other work the same day,
and the two are kept apart in the closed entry because the combined figure flatters the exclusions
by about 60%. Item 91 is the general form — the packer
walks a directory when it should be reading the build — including why a runtime dump can verify the
pack but must never produce it. Its direction was settled the same day: the code declares what it
loads, `ASSET_NEEDS` next to the `LoadFile` call, planned in `docs/asset_declaration_plan.md`.

On 2026-09-14: **item 84 closed** - scripted holds now advance from inside the tick, so edge-triggered
SCRIPTED actions survive `sim_step` in every app. That wording said "edge-triggered actions" until
later the same day, when the on-screen buttons showed that REAL asynchronous edges still fall through
the same hole - which became item 88, **also closed on 2026-09-14**: an unread edge now survives a
non-ticking pass, so the original wording is true at last. Also on 2026-09-14: **item 67 closed** (on-screen buttons, driven
by the Win32 mouse as pointer 0) and **item 81 largely built** - `core/UIOverlay`, an SDF atlas baked
by the new `tools/fontbake`, and one screen-space pass that draws rounded rects and text together.
See `docs/ui_overlay_plan.md`.

On 2026-09-13: **item 69 closed** - `WasKeyPressed` exists and Tetris's four gameplay actions fire on the press edge - and **item 84 opened**, a pre-existing bug it turned up: edge-triggered scripted input is never delivered while single-stepping. And **items 68 and 79 closed** - the eight genuinely broken format strings
fixed, and `CONFIG=release` built - and **item 61 closed** and moved to the done list, along with
the soft-compile change it turned out to need. Both came out of building `apps/testfx`, the
fourteenth app and the first that exists to exercise the engine rather than to be a game — see
`docs/testfx_plan.md`.

And later the same day, **item 85 opened and closed**: libstdc++'s stream and locale machinery
is out of every app. It came out of `core/` and everything under `3rdparty/` first, which left
OpenAL as the only holder - and then OpenAL was replaced by miniaudio, which is C and references
none of it. `libs/libthirdparty.a` gained a build rule (`3rdparty/makefile`) on the way, having
**Item 86** was opened at the same time, out of giving `libs/libimgui.a` a build rule for the
same reason - it had none either, and turned out to be another `-Og` debug build. Rebuilding it
at `-Os` took 111 KB off every app, and doing so surfaced a local patch to the imgui submodule
that is uncommitted and would be reverted by a `git submodule update` without a word.

had none at all: it was a hand-built blob that a fresh clone could not reproduce, and it turned
out to have been compiled `-Og -g`. Every app is now between 3.49 and 3.82 MB; the 2.3 MB gap
between a sound app and a silent one is gone. **Item 80 closed with it** - its whole subject was
how to trim openal-soft, and it was overtaken rather than carried out. Both are in the done list.

On 2026-09-15: **items 78, 67, 81 and 24 closed and moved.** 78 and 67 were already ticked and
were simply still here. The other two are the interesting pair, and they close together because
they are one subject: **this engine now has two ways of rendering text, on purpose.**
`core/TextMesh` welds glyph meshes along a pen for text that lives in the world — lit, shadowed,
knocked over — and `core/UIOverlay` draws SDF glyphs and rounded rects through one distance
expression for text stuck to the screen. They share the pen arithmetic and none of the storage,
which is the seam item 24 identified before either half existed. Item 81's six steps
(`docs/ui_overlay_plan.md` §8) are all done including the Android half, and item 82 — which it
existed to unblock — closed a day earlier.

**What is deliberately NOT open as a result**: layout, wrapping, hit-testing, focus, widgets,
state. Both entries and `docs/ui_overlay_plan.md` §9 decline that rather than defer it, for item
24's own reason — it is "weeks rather than days" and a `SubmitPointer` rect list plus three draw
calls is all Tetris needs. A UI framework belongs in a new item, not in a reopened one.

Band A is empty again as a result; the heading stays, for the reason the 2026-09-13 note above
gives.

---

## Band A — minutes each, no risk

*Empty — item 78 closed 2026-09-15.* The heading stays: the last time one was dropped, items 43
and 61 ended up stranded under a band that no longer said anything about them.

## Band B — under an hour each

- [ ] **43. The field is rebuilt every frame with no dirty flag.** One geometry pass plus
  log2(size)+2 dispatches, all of it repeated whether or not anything moved. Its share of the
  90 us (see the reference at the bottom) was not measured separately from the march's, so that is
  the first thing to find out - but either way an app whose world changes rarely is paying for a
  map that did not change.

- [ ] **70. `Application::GetDisplayDPI()`.** Nothing in the engine can ask how large a pixel is,
  so every on-screen size is in pixels and means a different thing on every panel. Needed by
  anything laid out in physical units — item 67's buttons first, but any future HUD or UI layer
  has the same problem, and the engine's own ImGui panels are already fixed-pixel and would be
  unreadable on a dense screen.

  Windows returns 96 and **says so** — the value being nominal rather than measured is the
  interesting part of the API, and a caller that cannot tell the difference will ship a layout
  that is right on the developer's monitor only. The port's Android side reads
  `DisplayInfo::density` before the window exists, so the value is valid by `Init()`; the Win32
  side wants `GetDpiForWindow` once there is a window, with 96 as the honest answer before that.

- [ ] **72. Nothing records which feature flags an object was built under.** Today this is
  harmless and that is luck: `USE_SOUND` is the only per-app flag, no source has an
  `#ifdef USE_SOUND` in it, and flipping it only changes *which core sources are linked*, which
  make does track. **Items 73 and 74 both end that**, because both add real `#ifdef`s — so this
  wants doing with them rather than after them.

  The hazard is that make compares timestamps and has no dependency on a **variable's value**.
  Build with a flag on, rebuild with it off, and every object whose source did not change is
  reused as compiled under the opposite setting. On the port that produced a link that succeeded
  and an APK that died at `dlopen` naming a mangled symbol, which reads like a missing library
  rather than a stale build.

  **Item 79 closing on 2026-09-13 removed the other half of this item and narrowed what is
  left.** Debug-vs-release was the one setting that *did* reach the shared core objects, and it
  was solved without a stamp: each configuration got its own object tree, so nothing can go stale
  and nothing needs wiping. That is the better answer wherever it applies, and it applies whenever
  a flag selects between whole builds. A stamp is only needed for a flag that varies **per app**
  within one configuration - which `USE_MCP` and `USE_IMGUI` (items 74 and 82) are, because two
  apps built the same way can still disagree about them.

  Note the engine already answers half of this, structurally and better: `engine.mk`'s "line
  between shared and per-app flags" exists precisely so a per-app `-D` can never reach the shared
  core objects. What is unguarded is an app's **own** objects under `apps/<name>/build/`. So the
  stamp needed here is per-app and small — write `USE_SOUND=$(USE_SOUND) USE_MCP=...` to a
  `.buildflags` file in the app's object dir, compare it at parse time, and `rm -rf` that
  directory when it differs. Only the app's own objects; `build/core` must not be wiped by this,
  and by the rule above it never needs to be.

- [ ] **83. rp3d's twist friction has no lever arm, so a small rolling body freezes against any
  wall.** The contact solver applies a torque about each contact normal bounded by the friction
  coefficient times the normal impulse - as a torque, with no radius in it. A ball rolling along a
  wall spins about exactly that wall's normal, so for a 0.135 pinball resting against a 0.14 rail
  on a slope that should have rolled it into the drain, the wall's twist bound (~1.2) dwarfed the
  torque the roll needed (~0.1) and the ball sat there for ever (`docs/pinball_findings.md` §5).
  The pinball app works round it with **zero friction on every steel rail** (friction mixes as a
  geometric mean, so the ball's own value cannot bring it back), which is defensible for steel
  but wrong for rubber. The fix belongs in the fork (`C:/code/reactphysics3d`, see
  `rp3d_local_fork_hinge_motor_patch`): scale the twist bound by the manifold's contact radius,
  or expose a per-material twist coefficient so a rail can have sliding friction without it.
  Anything in this engine that rolls a small body against a wall will hit this.

## Band C — one to three hours each

- [ ] **89. `Application::Init()` is dead, and the graphics prologue it holds is copy-pasted into
  all fourteen apps. Take the port's `InitGraphics()` split instead.**
  Found 2026-09-14 while closing item 75's first half. `Init()` is virtual, `FrameThreadFunction`
  calls `app->Init()` (`core/Application.cpp:211`), and **every app overrides it without chaining to
  the base**, so `Application::Init()` has no caller in any build. That is why the dump call site
  inside it could never have worked, and it is why nobody noticed.

  **The base is not a base, it is a stale copy.** Its body — construct the renderer at window size,
  `renderer->Init()`, `SetVSync(true)`, load `shaders/default.*`, `new Scene()` and wire it — has
  drifted from all fourteen live versions. Measured: `new Renderer(...)` and
  `default_shader = new Shader("shaders/default.vert","shaders/default.frag")` appear in **14 of 14**
  apps; `renderer->Init()` **with no pipeline argument appears only in the dead base**, every live
  app passing one or defaulting differently; `SetVSync` in **7 of 14**, so the other seven take
  whatever the driver gives them and that varies by machine; and `main_scene = new Scene()` in
  **none**, because apps build their scene through a factory (`CreateEmptyScene`, `CreateMainScene`,
  `CreateHandTestScene`).

  **So the fix is NOT to make apps chain to it.** A base whose prologue hard-codes the pipeline,
  VSync and the scene is one every app would have to call and then undo half of, which is worse than
  none. The split wanted is the opposite: a **non-virtual `InitGraphics()` that owns the prologue and
  calls the virtual `Init()` hook inside itself**, leaving `Init()` to mean only "the app's own
  setup".

  **The port has already built exactly that, and been through its two non-obvious lessons** —
  `C:/code/android/android_core/Application.cpp:155` (Android) and `:950` (its Windows counterpart).
  Both are worth taking verbatim rather than rediscovering:

  - **The `main_scene` post-condition goes AFTER `Init()`, not before.** Checking first makes the
    legal case — an app that creates its scene *in* `Init()` — fatal, and the port records Tetris
    dying on that line every launch after a completely successful renderer bring-up. Still fatal
    after, and worth being: every line below dereferences it, and the message names the one thing
    that has to be true by then.
  - **Wiring the scene is defaults, not overwrites.** `main_scene->renderer`/`camera`/
    `inputcontroller` are filled in only where still NULL, because an app's `Init()` may have built
    its own scene with its own camera (Tetris's is orthographic) and the prologue must not stamp
    over it. The dead base here assigns unconditionally, which is the bug that would have appeared
    the moment anyone made an app chain to it.

  **One real defect falls out of doing this**: `renderer->Init()` returns `bool` and **13 of the 14
  apps ignore it** — `apps/ship` is the only one that checks and Fatals. A pipeline that fails to
  initialise therefore gives a black window and no message in thirteen apps. A prologue that owns
  the call gets the check right once, for all of them.

  **Do it with the Android merge, not before.** Deleting the dead `Init()` on its own is a
  ten-second change and the wrong one: it throws away the evidence of what the prologue was meant
  to be, immediately before the merge that wants to reinstate a shared one. See the merge notes at
  the bottom of this file and `docs/asset_layout_plan.md`.

- [ ] **42. Skinned meshes do not cast into the field.** `RenderFieldPass` draws
  `MESH_MODE_NORMAL` only; a skinned mesh would need its own variant of `shaders/field.vert`
  applying the bone transforms, exactly as `default_skinned.vert` does. A skinned character
  receives field shadows but does not cast one. Nothing that uses the field has skinned geometry
  yet.

- [ ] **64. `tank_drive` does not drive the tank, and the recorded baselines say it used to.**
  Found on 2026-09-12 while smoke-testing the input merge, and **confirmed pre-existing**: the same
  test against a clean worktree at `0b3778e`, before any of today's changes, produces identical
  numbers.

  ```
  tank_reset, settle 60 ticks, then tank_drive {direction:"forward", amount:1.0, duration_ms:3000}
    step0 gas=0.00 fwd_speed=-0.00 pos=[-4.19,-0.01,-0.36]
    step1 gas=0.00 fwd_speed=-0.00 pos=[-4.20,-0.01,-0.37]
    step2 gas=0.00 fwd_speed=-0.00 pos=[-4.20,-0.01,-0.38]
  ```

  Same with `tank_step` instead of `sim_step`, and same free-running with no pause at all: the hull
  creeps a couple of centimetres and `gas_pedal` never leaves 0. The rp3d baseline recorded from
  this same scenario had the tank reaching its top speed, so something between that recording and
  now broke it. (That baseline has since been deleted as stale - see `docs/mcp_server.md` - so the
  evidence it used to work is the write-up rather than the file.)

  **Not the input layer.** That was the first suspicion and it is ruled out: the path this uses is
  `AddKeyMap(0,INPUT_AXIS_DRIVE)` + `HoldAxis` + `GetAxis`, and the harness written for items 48/49
  exercises exactly that and passes.

  Where to look next, in order:
  - `ApplicationTank`'s scripted block applies `ThrottleInput(ax_drive)` **only when non-zero**, so
    either `GetAxis(INPUT_AXIS_DRIVE)` really is 0 in the app (different from the harness — how?)
    or `controlled_vehicle` is not the vehicle being measured.
  - `window_focus` is false throughout an automated run, which skips the hardware block. The
    comment there says `TankCharacter` re-asserts an idle brake at the end of every tick and that
    the gamepad block is what normally clears it — so an unfocused scripted drive may be fighting a
    brake that nothing releases. That would explain a tank that creeps instead of accelerating, and
    it is the single most likely cause.
  - `gas_pedal` in the telemetry may itself only be written inside the focused block, in which case
    the 0 is a reporting artefact and only `forward_speed`/position are evidence. Worth settling
    first, because it decides whether the other two lines of enquiry are even needed.

  Band C is a guess: the fix may be one line once the cause is known, but the cause is not known.
  *Whoever picks this up: `tools/vehicle_mcp.py` already drives this scenario, so the before/after
  is one command each side of the fix. This wants doing BEFORE the rp3d baselines are re-recorded,
  or the new baseline will bake in a tank that does not drive.*

- [ ] **73. `USE_PHYSICS`, so an app can opt out of ReactPhysics3D.** `USE_SOUND` already
  establishes the convention (`?= 0`, opt in per app, drop the sources and the `-l` when off).
  Everything that links core today links rp3d whether or not it wants it, because `Object.h`
  includes `Physics.h` and `Object` has a physics member.

  **The case for this is host tools, not apps.** Counting the apps undersells it badly: only three
  of the twelve never touch physics (`ocpp`, `sim` and `ui`, each of which calls `UpdatePhysics` on
  an empty world and nothing else), and the ones that look like they would not all do — Tetris and
  Breakout use static bodies for their walls, `tileset` calls rp3d overlap queries directly. Twelve
  games mostly want a physics engine, which is not surprising.

  A **build tool** is a different kind of consumer, and it is the one this flag exists for. The
  port has two, and the difference between them is the whole design:

  - `pack_assets.exe` links a **thin hand-listed slice** — `File.cpp`, `BinaryAsset.cpp`,
    `Debug.cpp`, `Debug_win32.cpp` and miniz, and that is all. No `Object`, so no flag needed and
    no question to answer. See item 75.
  - `sprite_packer.exe` is the one that forces the issue. It is a real Windows GUI built on
    `core/Application` — window, ImGui, `Renderer`, `Scene`, `Object`, `GLTFLoader` — because that
    is the whole point of a tool that shows you what it is packing. Its object directory is
    indistinguishable from an app's. A sprite packer has no use whatsoever for a physics engine,
    a sound backend, or a JSON-RPC server that lets an agent drive it, and its makefile
    accordingly sets **`USE_PHYSICS ?= 0`, `USE_MCP ?= 0`, `USE_SOUND ?= 0`** — the flag family is
    that tool's entire relationship to the engine.

  This repo has no such tool yet: `tools/` is Python plus one stray `camera_ray_test.cpp` with no
  build rule. So the second half of this item is that **`engine.mk` has no notion of a target that
  is not an app.** Its `CFLAGS` unconditionally carry `-lreactphysics3d -limgui -lsetupapi -lhid
  -lopengl32 -lgdi32 -lws2_32 -lcrypt32`, which is the right default for a game and wrong for
  anything else. A tool needs either that list to become opt-in the way the sources already are,
  or its own small makefile that includes only what it wants — the port took the second road for
  both of its tools and it worked.

  **The mechanism is not a copy of the sound flag, and that is the rest of the work.** Dropping
  `SoundSystem.cpp` from the source list was enough for sound. Physics needs real `#ifdef`s in
  `core/Object.{h,cpp}` — guard the include; make `physics` a `void*` when off so every
  `if (physics)` test and `Object`'s general shape survive unchanged; compile out only
  `GetPhysics()`, `AddPhysics()` and `GetRigidBody()`, whose signatures name types that no longer
  exist; leave everything else (`ResetPhysics`, `SetMass`, `Get/SetVelocity`, `UpdatePhysicsState`,
  the transform setters, the copy constructor) declared and turn it into a no-op. `AttachChild`
  needs no guard at all — it only tests `newchild->physics` for truthiness, which compiles against
  a `void*`.

  **And that runs straight into `engine.mk`'s "line between shared and per-app flags".** A
  `#ifdef` in `Object.h` means `-DUSE_PHYSICS` has to reach `Object.o`, which lives in the shared
  `build/core` — the exact thing that block forbids, and for a good reason: whichever target built
  first would win and every other one would silently link objects compiled for someone else. Note
  a host tool makes this *worse* than the app case, because a tool and a game genuinely do want to
  disagree within one working tree, so "make the flag repo-wide" is not the cheap way out here
  that it would be for apps alone. That leaves physics-dependent core sources compiling per target
  rather than into `build/core` — correct, and it means `build/core` stops meaning "all of core".
  **Do not just add the `-D` to `CORE_CFLAGS`.** Whichever way it goes, item 72's stamp has to
  land with it.

  What it is worth once done: link time and exe size for three apps, a tool that does not build a
  94-source physics library to pack sprites, and `Object.{h,cpp}` stopping its drift from the
  port's copy. On the port the flag was worth 94 rp3d sources against 0 and a 27.6 MB shared
  object against 17.9 MB — a different toolchain and link model, so read the ratio, not the
  numbers.

- [ ] **76. `Renderer(int w, int h)` — a renderer has no size.** `Renderer::width`/`height` are set
  once from the window and then read by eight call sites, all of them doing the same thing:
  `camera->SetupPerspective(renderer->width, renderer->height, ...)`. The port declined to take
  this constructor and the reasoning is right — a renderer draws to several targets at several
  sizes (the shadow map at 2048, the deferred target, the occluder field at 512, a half-res pass if
  anyone ever wants one), so there is no single size it can meaningfully be constructed with. The
  **window** owns the display size; each render target picks its own. That a `Window` takes `(w,h)`
  is fine and stays.

  Half the answer is already in the tree: `GetViewportWidth()`/`GetViewportHeight()` exist and fall
  back to `width`/`height`, so the renderer already distinguishes "the thing I am currently drawing
  into" from "the size I was built with". The work is deciding what those eight camera call sites
  should ask — the window, the viewport, or the specific target — and then removing the
  constructor argument rather than leaving both spellings available.

  Band C is for the mechanical part; the decision is the hard half and is small only if it is made
  first. **This one will surface as a real conflict the moment the two cores are merged**, which is
  the reason it is on the list now rather than when it annoys someone.

- [ ] **77. Per-instance UV sub-rects, so a `SpriteSheet` can reach the world.** The port added
  `vec2 uv0`/`uv1` to `Object` and packs them into the per-instance data the renderer uploads, which
  is what lets one atlas texture serve many objects each showing a different sprite. `SpriteSheet`
  and `Sprite` are both already in core here and already know their sub-rects; there is simply no
  path from a `Sprite` to a drawn object, which is the same gap item 24 names from the text side.

  The cost is that `instancedata_t` grows, and that struct's layout is **repeated by hand in
  several shaders** — the same care `Material.h` documents for its own struct, where `emissive` was
  appended last precisely so no existing offset moved, and the same cost item 41 flags for
  `light_t`. So: append, never insert, and grep the shaders. Two floats per instance for every
  object in the scene whether or not it is a sprite is the other half of the price; worth checking
  whether it belongs on `instancedata_t` at all or wants its own path.

- [ ] **86. The ImGui alpha-compositing patch has no home, and reverting it silently costs a
  link dependency.** `3rdparty/imgui` is a submodule, and `backends/imgui_impl_win32.cpp` in it
  is locally modified: `ImGui_ImplWin32_EnableAlphaCompositing()` is short-circuited with an
  early `return;` and its body commented out. That is deliberate and it should stay - the
  function calls `DwmEnableBlurBehindWindow`, which fights the layered-window alpha this whole
  project is named after.

  **The problem is only where it lives.** The change is uncommitted *inside the submodule*, so
  it exists on one machine and nowhere else. `git submodule update` reverts it without saying
  anything, a fresh clone never has it, and neither failure is a build error - the first symptom
  is DWM blur appearing behind the window, which reads as a rendering bug rather than a lost
  patch.

  **And it is not only cosmetic: the patch is also removing a link dependency.** Diffing the
  compiled objects, the unpatched backend additionally references

  ```
  DwmEnableBlurBehindWindow   DwmGetColorizationColor   DwmIsCompositionEnabled
  ```

  which is `-ldwmapi`, an import library nothing in this engine links today. (It also pulls
  `CreateRectRgn` and `DeleteObject`, but those are gdi32, which is linked anyway.) So a build
  that lost the patch would fail to link rather than merely look wrong - which is the better of
  the two outcomes, but only by accident, and only for that half of it.

  **The fix is the one already used for reactphysics3d**: a fork with a branch carrying the
  patch, and `.gitmodules` pointing at it. See the memory note on the rp3d fork for the shape.
  Failing that, the change is four lines and could be carried as a `.patch` file applied by
  `3rdparty/makefile` - the Android port already does exactly that for openal-soft's
  `0001-guard-empty-HRTF_DATA_TARGETS.patch`, so there is precedent in the family.

  Whatever is chosen, `3rdparty/makefile` already warns about it at the imgui flags block and
  says to check `git -C imgui diff` is non-empty before rebuilding that library. That is a
  reminder, not a fix.

## Band D — half a day to a day each

- [ ] **26. Scene save/load.** `BuildSceneFromJSON` restores name/position/rotation of asset-backed
  objects and nothing else. *Later.*

  *Breakout note:* felt as the absence of a level format — that app's five layouts are hand-drawn
  C++ tables because inventing a scene format was not the assignment. Any game past its first one
  wants this, and a level file is a smaller and better-specified problem than a general scene
  format, so it may be worth splitting out.

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

- [ ] **39. A column is one slab, so it fills in its own gaps.** R and A are the extremes of
  everything in a column, so a falling piece above a stack merges with it and light cannot pass
  between the two. Exact for anything extruded from the ground, conservative for everything else,
  and hard to see in a fixed top-down view - but it is the reason this is not a general shadow
  technique. Two more channels would carry a second slab if an app ever needs it.

  *Breakout note (2026-09-12):* a second app used the field — a point light riding the ball,
  described as the best-looking thing in the game for two lines of setup — and this never bit,
  because the ball sits at the same depth as the bricks. Its author flagged that as **luck rather
  than design**: the moment something passes in front of something else along the field axis, the
  slab merges them. Still the first thing to fix if the field is ever used by a scene with depth.

- [ ] **40. The march still has a step floor, and it is load-bearing.** The distance field is zero
  everywhere directly above an occluder, so a ray running along the top of a wall would step by
  nothing, stall, and report "unoccluded" - the wrong answer in the place with the most geometry.
  `min_step = dist / field_shadow_steps` stops that, at the price of degrading to the old even
  spacing for such rays. The real fix is a max-mipmap pyramid over the height channel, which would
  give a conservative vertical bound to go with the horizontal one; the 2D distance alone cannot,
  because a neighbouring column one texel away may rise to just under the ray.

- [ ] **91. The packer is directory-driven, so what ships is a hand-maintained guess.** **Full plan
  in `docs/asset_declaration_plan.md`** — direction agreed 2026-09-14 (step 3 below, `ASSET_NEEDS`),
  and the mechanism is verified rather than assumed: a custom COFF section carries the names through
  mingw g++, `objcopy` reads them back out of the `.o`, and `-Wl,--gc-sections` — which `engine.mk`
  already passes — drops them from the shipped exe, so a declaration costs nothing to ship.
  `tools/assetpack` walks `ASSET_ROOTS` and subtracts `--exclude` globs. That makes the shipped set
  a property of **the tree plus a hand-written list**, when what anyone actually wants is a property
  of **the code**: the assets this build of this app can ask for. Three sources of truth, and only
  the third is authoritative. Item 90 — closed 2026-09-14, text in `docs/engine_backlog_done.md` —
  is what happens when they disagree, and it is worth reading first: it cost three fixes, two of
  which were bugs nobody could see until an exclusion mattered.

  The measurement that motivated this, taken 2026-09-14: Tetris packed 20 assets and, run to
  shutdown, requested 16 of them. The four it never touched were `shaders/skybox.vert`,
  `shaders/skybox.frag` and `shaders/texture.comp` — which it does not draw — plus
  `fonts/consola.ttf`, which it does not load *in a `USE_IMGUI=0` build*. Nothing in a makefile or
  in the asset tree could have told you the fourth one.

  **The obvious fix is banned, and it is worth writing down why.** Having the app dump the names it
  loaded and feeding that back as the pack list is exactly `DUMP_BINARYASSETS`, removed on
  2026-09-14 — see the note on `assets[]` in `core/BinaryAsset.h`. It could only ever bake what one
  session happened to reach, which is a different question from what the app can ask for, and only
  one of the two has a reproducible answer. A menu never opened, a sound only a tetris plays, a
  shader behind a toggle: all absent, and nothing would say so. A runtime manifest is a **verifier**
  here, never an input.

  Three steps, cheapest first, each useful alone:

  1. **Run the baked build in the build, and diff.** `BinaryAsset::ListBinaryAssets()` now reports
     both tables and, at shutdown, which packed assets were never requested
     (`apps/tetris/main.cpp`). So: bake everything with no exclusions, run to shutdown, and compare
     what was requested against what was packed. Over-inclusion shows up as a list of names to
     look at; **under**-inclusion shows up as the `LoadFile failed` fatal in item 90, which is
     louder. This is the cheap regression net and it needs no new machinery — only somewhere to
     run it.
  2. **Cross-check the pack against the code.** Scan the translation units the build actually
     compiles for asset-name literals and warn when the pack and the code disagree. Flag-aware for
     free, because `USE_IMGUI=0` drops `core/WindowImGui.cpp` and its `"fonts/consola.ttf"` with
     it. A warning rather than the pack list, because of the caveats below.
  3. **Let the code declare what it needs.** The dependency on `fonts/consola.ttf` belongs next to
     the `LoadFile` at `core/WindowImGui.cpp:64`, not in fourteen app makefiles. Something like
     `ASSET_NEEDS("fonts/consola.ttf")` registering into a link-time section, with the packer
     reading the registrations out of the objects that were actually linked. That is reproducible,
     needs no run, and is flag-aware by construction — a dropped translation unit takes its
     declarations with it.

  **Two things no static scan can see**, which is why step 2 warns rather than decides. Names built
  at run time (`"shaders/" + name`) are invisible. And shaders have their own reference graph:
  `core/Shader.cpp` implements a nested GLSL `#include`, so `apps/ship/assets/shaders/*.frag` pulls
  in `density.glsl` by name with no C++ literal anywhere. Both are visible to step 1 and to the
  runtime listing, which is the other half of why the verifier keeps its job even once step 3 lands.

## Band E — multi-day, strategic

- [ ] **25. Record and replay** (step 7 of the deterministic-sim plan). Unblocked by item 22, but see the thread-ordering caveat there. Tetris
  is a better test case than a vehicle: 200 cells of `int8_t` either match or they do not, with no
  float tolerance to argue about. *Later.*

  *Breakout note (2026-09-12), and it changes the specification.* That run measured determinism
  directly (`tools/breakout_bot.py determinism`, same seed and same scripted input twice). Score,
  bricks remaining, shield charge to four decimal places, saves, power-ups caught and the whole
  board match **exactly** every time, and the ball's velocity is bit-identical. The ball's
  *position* matches on some runs and is a couple of ticks of travel out on others.

  The cause is the finding: **an MCP-driven input is not tick-aligned.** `HoldAxis` starts on
  whichever tick the physics thread happens to be on when the call lands, so two runs of one script
  begin their holds one or two ticks apart, and the same script then produces the same trajectory
  sampled at a different point along it. So the simulation is reproducible and the *harness* is
  not, and no amount of seeding fixes it.

  The consequence for this item: **the recording must capture the tick an input landed on, not
  merely the input.** Also see item 49 — a run driven by a real gamepad would today record none of
  the stick's motion at all, because the polled path never becomes an event.

- [ ] **27. Finish the skeletal animation system.** `ObjectAnimation.cpp:108` still has a
  `debug->Fatal` for any clip carrying a scale track. The probe was deliberately never started.
  *Later.*

---

## Reference: what `tetris.exe` is made of

Context for items 79-82, which came out of one question — how small can a shipped Tetris be while
keeping the features that make it worth shipping. Measured 2026-09-13 against
`apps/tetris/build/tetris.exe` as built that day (`-Og -g`, `USE_SOUND := 1`, MCP and physics both
in), bucketed by symbol origin with `nm -S`. Item 80's OpenAL figures come from a different and
better method - building the library three ways and linking each against a probe that calls exactly
the engine's fifteen AL functions - because a shared library's marginal cost inside a big binary is
not something symbol bucketing can settle.

**The headline is that the debug info is nine tenths of the file** (item 79, closed 2026-09-13):

```
as built                      55.07 MB
after `strip`                  5.37 MB
CONFIG=release (-DRELEASE -O3 -s)   5.68 MB
```

The third line is the one that now exists. It is *larger* than the stripped debug build, by
310 KB, which is `-O3` inlining exactly as the item predicted it might; the release build is
still 9.7x smaller than what this engine shipped for its whole life before that day. `-Os` was
measured at the same time and comes in at 5.29 MB - 400 KB, 7%, below `-O3` - which is not
enough to be worth a third configuration on a real-time engine, and is recorded here so the
question is not reopened without a reason.

**And the 5.37 MB that is left.** Bucketed by symbol origin with `nm -S`, splitting what is in the
file from what is only `.bss` — which matters, because `.bss` occupies no bytes on disk and an
earlier version of this table conflated the two:

| component | in the file | `.bss` (RAM only) | notes |
|---|---|---|---|
| ReactPhysics3D | 1108 KB | 13 KB | the debris tray; a real feature, gated by `f_debris_enabled` |
| OpenAL | ~975 KB | **873 KB** | no longer linked at all — replaced by miniaudio, item 85 |
| libstdc++ / mingw CRT | 919 KB | | `-static-libstdc++ -static` is deliberate |
| **ImGui** | **624 KB** | 22 KB | one debug window in Tetris — items 81, 82 |
| engine `core/` | 251 KB | | the whole engine, and the smallest interesting line here |
| nlohmann/json | 173 KB | | MCP only — item 74 |
| tinygltf | 156 KB | | |
| stb_image | 59 KB | | |
| miniz | 8 KB | | |

Sized symbols account for 4.66 MB of the 5.37; the remainder is headers, padding, import tables and
unsized data. Treat the rows as proportions, not as a budget that must sum.

**Two of those rows moved on 2026-09-13 — see item 85.** The `libstdc++` line included about
735 KB of stream and locale machinery that was being held by a single unreachable code path,
tinygltf's glTF serializer; compiling it out took `ocpp_release.exe` from 4.23 MB to 3.53 MB and
zeroed the 669 locale symbols in it. The `tinygltf` row drops with it: that object's own code and
data went from 182,616 to 156,764 bytes, though that is the object measured directly rather than
the table's method. `tetris.exe` keeps the machinery and most of that row, because OpenAL still
references it — about 608 KB of the 2.3 MB by which a sound app then exceeded one without. That
gap is now closed too: OpenAL was replaced by miniaudio. Item 85, in the done list.
The table above has not been re-bucketed since; treat the `libstdc++` and `tinygltf` rows as
pre-change figures until someone re-runs `nm -S`.

**Four things worth taking from that table.**

The engine itself is **251 KB**. Everything else is libraries, which is the expected shape for a
small engine and is also the reason size work here is almost entirely about which libraries a build
carries rather than about the code anyone writes.

**`.bss` is not file size, and OpenAL is where that bites.** Its three bsinc resampler tables are
873 KB of the 947 KB `.bss` and cost nothing on disk in the library we ship today. They *do* cost
file bytes in openal-soft 1.25.2, where they became `.data` — see item 80, which is the reason
updating that library is a 1.4 MB regression rather than an improvement.

**ImGui is fourth, not first.** The instinct to remove it is right for shipping reasons — a game
should not carry its own inspector — but as a size lever it is worth less than the release build by
two orders of magnitude, and less than physics or OpenAL. Do it because the result is a clean
example of the engine, not because of the megabytes.

**Two of the four big items are features, and that is the real question.** Physics is the debris
tumbling out of the well and OpenAL is the sound; together they are about 2.1 MB of the file, and
they are two of the better things about the game. "As small as possible while keeping the core
features" has to decide what counts as core before the ordering means anything.

**Suggested order, cheapest and least controversial first:** ~~item 79 (the release build, ~50 MB,
one `ifeq`)~~ **- done 2026-09-13, 55.07 MB to 5.68 MB**, then ~~item 80 (OpenAL)~~ **- done
2026-09-13 by replacing it outright, see item 85: -2.06 MB from every app that has sound, and the
sound/no-sound size split is gone** - then item 74 (`USE_MCP`, ~200 KB and a server a shipped game
should not have), then items 81 and 82 (the SDF pass and `USE_IMGUI`, ~600 KB and the only one
that is real design work).

---

## Reference: merging the Android port

Context for items 67-77, all of which came from `C:/code/android` — a port that took this engine
as its inspiration, reached a working renderer, and has Tetris running on a tablet. Its own list of
what belongs back here is `.claude/memory/upstream_merge_candidates.md` in that repo; the items
above are that list checked against this tree, so where the two disagree, the items are the later
reading.

**Three of its findings needed no item, and two of those are worth knowing anyway.**

- **`app_name` is the debug panel's ImGui title**, so an app whose own HUD is
  `ImGui::Begin("Tetris")` with `app_name = "Tetris"` gets one merged window positioned by
  whichever drew last. It cost the port an hour and it reads as a layout bug rather than a name
  collision. **Already fixed here** and not by anyone who knew about it: the docked-panel rework
  (step 6 of the deterministic-sim plan) split that window into `Scene`, `Inspector` and `Engine`
  and `app_name` no longer exists in `core/`. Recorded because it is the kind of thing that comes
  back the moment someone adds a window named after the app.
- **`Object::SetPickable(bool)`** is listed there as an addition to take. It is not — this tree has
  had `SetPickability(bool)` at `Object.cpp:137` all along. Same thing, different spelling, and
  the port adopting this name costs it nothing and restores `Object.h` to verbatim for free. The
  general lesson for the merge: check for the engine's name before adding one.
- The 1/PI Lambert normalisation, `SetWritableDataDirectory`, `-Wl,--no-undefined`, the
  `Window_android` / `InputController_android` split and `SetupScene()` being non-pure-virtual are
  all Android-only and stay there.

**The merge method, which is the part that actually worked.** Ten local adaptations in
`ApplicationTetris.cpp` survived a ~900-line upstream diff by copying the upstream file **wholesale**
and then re-applying each adaptation from a script that **asserts on its anchor** — so a change
that invalidates an adaptation fails loudly instead of the adaptation silently vanishing. That is
worth reaching for in either direction, and it is the difference between a merge that is reviewable
and one that is a diff nobody reads.

**Diff with `--strip-trailing-cr`.** This repo is CRLF in the working tree (`core.autocrlf=true`,
`.gitattributes` says `text eol=lf`) and the port is LF. Without the flag a 43-line difference
renders as 2,775 lines and the real change is invisible inside it.

---

## Reference: the occluder field

Context for items 39-43, which were split across bands above. Unchanged from when the field was
added on 2026-09-11.

Point lights cast shadows without a cube map. `Renderer::EnableFieldShadows` builds a single
top-down `RGBA16F` texture per frame holding, per world column, the height of the highest surface
(R), the lowest (A), and the 2D distance to the nearest occupied column (G, jump-flooded by
`shaders/field_jfa.comp`). `CalcFieldShadow` in `shaders/default.frag` sphere-traces the segment
from a receiver to a point light against it, stepping by that distance and using it again for the
penumbra estimate. The map mentions no light, so N lights cost N marches against one texture
rather than N shadow maps. `APP=Tetris` was the first user - see
`ApplicationTetris::SetupFieldShadows`, and the Engine panel's Renderer section for the live
controls. `APP=Breakout` is the second, for a point light that rides the ball.

Measured on the Tetris board at 512x512, vsync off, by alternating `f_field_shadows` every 300
frames inside one process: **349.5 us per frame with it on against 259.3 us off**, so about 90 us
for the geometry pass, eleven jump-flood dispatches and the per-fragment march together. That is
CPU-side frame time (`tmr_frame`), which for dispatches is submission rather than execution, so
treat it as an upper bound on what it costs the frame, not a GPU profile.
