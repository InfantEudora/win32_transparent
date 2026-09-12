# On-screen buttons: touch input as a third input family

Plan for driving the engine from on-screen controls on Android, and for testing them on Windows
before an Android build exists.

Written 2026-09-13 from a walk through `core/InputController.{h,cpp}`, `core/Renderer.cpp` and
`apps/tetris/`. Line numbers are as of that date. Nothing here is implemented yet.

**Status: proposed, nothing built.** Android support in the tree today is one file
(`core/Debug_android.cpp`, a `__android_log_print` variant of the logger) and a vendored
`3rdparty/imgui/backends/imgui_impl_android.cpp`. There is no Android build, no touch handling
anywhere, and no 2D screen-space render pass of any kind.

---

## 1. The shape of the answer

`InputController` is already source-agnostic, and it already documents how a new input family
gets added — the gamepad did exactly this in the commit that absorbed `GamePadController`. So
on-screen buttons should be a **third member of the `AddKeyMap` / `AddGamePadMap` family**, not a
UI feature that reaches into input:

```cpp
input->AddKeyMap(VK_LEFT,        INPUT_TETRIS_LEFT);
input->AddGamePadMap(0,          INPUT_TETRIS_LEFT);
input->AddTouchButton(rect_left, INPUT_TETRIS_LEFT);   //the new one
```

The test to hold any design to: **`ApplicationTetris::SetupInput` gains a handful of lines
(`ApplicationTetris.cpp:858`) and nothing else in the app changes.** `GatherInput`, DAS/ARR, the
HUD, the MCP tools, the snapshot — all untouched. If a proposal requires touching `GatherInput`,
it has put the seam in the wrong place.

The reason this works is that everything downstream of a `KeyState` already stopped caring where
input came from: edge detection, multiple-mappings-per-action counting, the focus gate,
`HoldKey`/`HoldAxis`, and recordability are all properties of the *mapping*, not of the hardware.
A touch button is just another thing that can hold a mapping down.

---

## 2. Two mechanisms that look plausible and are not

### 2.1 Object picking

The obvious idea — buttons as scene geometry, hit by the existing picking path — fails on
multi-touch, which is the one thing a game pad layout must have.

`Renderer.cpp:849` reads a **1×1 region at `GetRelativeMousePosition()`**, three times (object id,
normal, position), on the render thread inside `DrawFrame`. It is single-point by construction.
"Hold left while tapping rotate" is impossible without rewriting it into a multi-point readback,
and each additional point is another synchronous `glReadPixels` stall per frame.

It is also wrong in kind. Buttons would have to be real objects: lit, shadowed, occludable by the
board, moving with the camera, appearing in `object_list` and in the Inspector. Every one of those
is a thing you would then have to switch off.

`Camera::GetPixelRay` avoids the GPU readback and can do N points on the CPU, which fixes the
multi-touch half. But it still answers "is this pixel inside that rectangle" by intersecting a ray
against world geometry — a great deal of machinery for a 2D containment test, and it still puts
the buttons in the world.

### 2.2 ImGui, for the gameplay controls

Tempting: `imgui_impl_android.cpp` is already vendored, and the Tetris HUD is already ImGui
(`ApplicationTetris::RenderTetrisHUD`). But ImGui is single-pointer at its core (`io.MousePos`
plus a `MouseDown[]` array for one cursor's buttons) — the same disqualifier as picking.

**ImGui stays the right answer for menus, pause, restart and the F1 debug panels**, none of which
need two fingers. The split is worth making explicit rather than trying to force one mechanism to
cover both.

---

## 3. What to build

A rect list inside `InputController`, converting pointers into synthetic system keycodes.

**In `InputController`, not beside it.** This is the codebase's own stated preference: the comment
at `InputController.h:357` records that the old separate `GamePadController` was wrong precisely
because it was "a parallel input class with its own keymap that two apps polled by hand." A
`TouchController` that apps polled by hand would be the same mistake with a different noun.

### 3.1 Surface

| Call | Thread | What it does |
|---|---|---|
| `AddTouchButton(rect, mapped_keycode)` | setup | One entry in the rect list, one ordinary `KeyMap`. Allocates its own synthetic system keycode internally. |
| `SubmitPointer(id, x, y, down)` | any (the Android input thread) | Hit-tests, tracks which pointer id owns which button, calls the existing `SubmitSystemKey`. |
| `GetTouchButtons()` | render | The rect list, so something can draw it. Input does not draw. |

`AddTouchButton` allocating the keycode itself — from a `TOUCH_SYSKEY_BASE` of `0x20000`, the same
trick as `GAMEPAD_SYSKEY_BASE` at `InputController.h:42` — means the app never sees the number,
and two touch buttons bound to one action get distinct keycodes and therefore correct
`f_isdown` counting for free.

`SubmitPointer` is safe from any thread for the same two reasons the raw-input thread is:
`SubmitSystemKey` takes `state_mutex`, and `keymap` is built during setup and only read
afterwards. The rect list must follow that same discipline — **built in `Init()`, never mutated**.
An orientation change or a window resize is exactly the case that would break it, so a layout
rebuild needs either the lock or a rule that it happens only on the thread that hit-tests.

### 3.2 Edges, never levels

One `KEY_DOWN` on press, one `KEY_UP` on release. Nothing polls "is a finger inside this rect."

This is the load-bearing decision. With edges, the rate at which the touch layer runs is
irrelevant — `KeyState` holds the action down between the two events, and DAS/ARR keeps counting
in ticks on the physics thread where it already lives. Poll instead and the rect list and the live
pointer positions both have to be shared across threads, and button feel becomes a function of
frame rate. The only frame-rate dependency left with edges is up to one frame of latency on the
edge itself, which is what the raw-input thread already has.

### 3.3 Drawing is a separate, deferrable decision

The panel owns rects and emits keycodes; it has no rendering opinion. There is no 2D screen-space
pass in the engine today — Tetris's labels are extruded `TextMesh` geometry in the world, and
`Sprite`/`SpriteSheet` exist with no renderer path that draws them — so building one is real work
that this plan deliberately does not depend on.

- **Day one:** an `ImDrawList` on a fullscreen background window. A few lines, no new render path.
  Single-pointer ImGui is irrelevant here because ImGui is only *drawing* the rectangles;
  `SubmitPointer` does the hit-testing.
- **Later:** a proper ortho overlay pass with the sprite atlas, if the buttons want to look like
  anything.
- **Never, for some apps:** a swipe-only layout draws nothing and works exactly the same.

---

## 4. Why this is the version that records and replays

This is the part that matters for the deterministic-sim direction, and it is the strongest
argument for keycode-per-button over anything coordinate-based.

Because the panel emits ordinary key events carrying a synthetic system keycode, a recording
captures `TOUCH_KEY_2 down @ tick 412` — **the action, not the finger position**. That replays on
any screen size, any dpi, and any subsequent change to the button layout.

Route touch through picking, or record raw pointer coordinates, and the recording becomes
layout-dependent: move a button by ten pixels and every replay made before the move is wrong, with
nothing to indicate it. Same reason the MCP server was made a "player" that submits events rather
than a thing that pokes state.

---

## 5. Four traps found in the current code

### 5.1 A touch button must own a system keycode

Not optional, and the failure is silent.

In `ApplyPendingEvents`, a key event whose `value` is 0 falls through to `GetByMappedKey()`
(`InputController.cpp:242`), which returns the **first** mapping for that action. A button that
submitted `{KEY_DOWN, INPUT_TETRIS_LEFT, 0}` would therefore share `f_held` with `VK_LEFT`'s
mapping. Press the touch button, then press and release the arrow key, and the action releases
with a finger still on the screen — `f_isdown` went to 1 and back to 0 across one physical key
because both were writing the same mapping's `f_held`.

Invisible on a phone with no keyboard. Not invisible on Windows, which is where this gets tested.

One piece of luck worth noting: `PollDevices` already skips any mapping with
`system_keycode >= GAMEPAD_SYSKEY_BASE` from `GetAsyncKeyState` (`InputController.cpp:161`), and
`0x20000 >= 0x10000`, so touch keycodes are correctly excluded from the polling path on day one.
The comment there says "a gamepad button, not a key" and would want to say "a synthetic keycode."

### 5.2 There is no `WasKeyPressed`

`InputController` exposes `IsKeyDown` and `WasKeyReleased`, and nothing else. Tetris fires rotate,
hard drop and hold on the **release** edge (`ApplicationTetris.cpp:1007-1010`).

On a keyboard nobody notices. On a touchscreen, "the piece rotates when I lift my finger" reads as
lag. Adding a symmetric `f_was_pressed` is mechanically trivial — `Tick()` already clears
`f_was_released` for every state at `InputController.cpp:765`, so it is one more flag cleared in
the same loop — but *which* edge each action fires on is a decision about feel and should be made
deliberately, per action, rather than discovered on a phone.

### 5.3 Size buttons in millimetres, not in screen fractions

A button that is 12% of the screen width is a pinhead on a small phone and a dinner plate on a
tablet, simultaneously. Layout wants **per-corner anchoring plus a physical size** ("bottom-left,
inset 6 mm, 9 mm square"), which means the rect list is in physical units and the platform layer
has to supply dpi. Normalised 0..1 coordinates are the trap that looks resolution-independent and
is not.

The same layout then has to survive a 1200×900 desktop window, which is what makes it testable
before any device exists.

### 5.4 The focus gate needs an Android owner

`SubmitSystemKey` drops key-*downs* while `!f_has_focus`. `f_has_focus` defaults to `true` and is
written only from `WM_ACTIVATE` / `WM_SETFOCUS` / `WM_KILLFOCUS` in `HandleMessage`, so on an
Android build with no Win32 message pump it would sit `true` forever.

Wire `SetFocused(false)` to the activity's `onPause`. `f_release_all_keys` then does the right
thing already — every held mapping is released on the next `ApplyPendingEvents`. Without it, a
button held when the user takes a call stays latched, and the piece keeps moving left in a
backgrounded app.

---

## 6. Testing on Windows first

The whole point of putting the seam at `SubmitPointer` is that the Android build is not on the
critical path. `SubmitPointer(0, x, y, down)` driven from the existing mouse messages exercises
the rect list, the keycode allocation, the edge emission and the Tetris bindings — everything
except genuine multi-touch — on the desktop build, today.

`WM_POINTER` gives real multi-touch on Windows if the second finger needs testing before a device
is available. Worth knowing it exists; probably not worth doing.

---

## 7. What this leaves room for

**Virtual thumbsticks** are the same mechanism with `INPUT_EVENT_AXIS_SCALAR` instead of key
edges: a pointer captured by a stick region emits a scalar as it moves. Two rules carry over from
the gamepad: emit **on change only** — `SubmitAnalogAxes` does this, and the reason is that
stamping a value over the action every frame silently defeats every scripted `HoldAxis` — and
report the way back to centre when the finger lifts, so nothing stays latched. Breakout's
`INPUT_BREAKOUT_STEER` and Tank's drive/steer axes are the actions that would want this.

**Gestures** — swipe to move, flick to hard-drop, tap to rotate, which is how phone Tetris usually
plays — are a *separate recogniser* that emits the same keycodes into the same pipeline. Keeping
that a second component matters: the panel stays a dumb hit-tester and does not grow a gesture
state machine inside it. A recogniser's thresholds are the one place wall-clock time is arguably
correct (a real finger's flick is a wall-clock flick), and its output is still just an edge on a
tick, so recording and replay are unaffected either way.

---

## 8. Order of work

1. `TOUCH_SYSKEY_BASE`, `AddTouchButton`, the rect list, `SubmitPointer`. Core only, no app
   changes, nothing drawn. Fix the `PollDevices` comment while there.
2. Drive `SubmitPointer` from the desktop mouse. Bind Tetris's six actions in `SetupInput`.
   Verify with the mouse that the board responds and that DAS/ARR still feel right.
3. Draw the rects with `ImDrawList`. This is the first point at which anything is visible.
4. Decide the press-vs-release edge question (§5.2) with the buttons in front of you, not before.
5. Physical-unit layout and dpi (§5.3) — needs the platform layer, so it lands with the Android
   port rather than before it.
6. Android: `SubmitPointer` from the motion-event path, `SetFocused` from the lifecycle.

Steps 1–4 are all on Windows and all independent of the Android port.
