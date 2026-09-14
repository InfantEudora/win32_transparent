# tools/uitest — verifying the 2D overlay and the on-screen buttons

Six scripts that check what `docs/ui_overlay_plan.md` built: the SDF font asset, the overlay's
distance-field maths, and the touch buttons driven end to end through the Win32 message pump —
plus one that checks the engine rule those buttons depend on (backlog item 88).

Unlike everything else under `tools/`, these are **scripts rather than a built exe** — there is
nothing to compile. They drive a running app over MCP and read pixels back, because that is the
only place the answers actually live.

```bash
#no app needed
python tools/uitest/check_fnt.py                       # validates shared_assets/fonts/mono_sdf.fnt

#starts and stops apps/tetris ITSELF - nothing else may be on the port
python tools/uitest/item88_test.py                     # edge input survives a paused pass

#with apps/tetris running (see CLAUDE.md - one app at a time, it owns port 8765)
bash tools/uitest/touch_test.sh                        # the six gameplay buttons
bash tools/uitest/hud_test.sh                          # new game / pause / mute
python tools/uitest/check_overlay.py <screenshot.png>  # the overlay's shader, against the maths
```

Each exits non-zero on failure and prints one line per check.

---

## What each one is actually for

**`check_fnt.py`** reads the baked `.fnt` the way the engine does and checks the header agrees with
itself and with the pixels — magic, `grid * cell == atlas`, file length, the solid texel reading
255, space having no ink. The two that matter most are the last pair: **`A` must have no ink below
`origin_y` and `g` must have some.** Those pin the sign of the pen origin, and getting it wrong
shifts every string by a line in a way no single-glyph screenshot can show. `tools/fontbake` runs
the same header validation before writing, so this is the independent reader that proves the file
survives the trip.

**`check_overlay.py`** is the interesting one. It does not look for "a soft edge somewhere" — it
recomputes the rounded-box signed distance field on the CPU for each pixel along a diagonal through
a rounded corner, predicts the colour through both quads (fill, then outline, straight alpha over
the background), and compares with what the GPU produced. When that agrees to the byte it has
simultaneously verified the distance field, the pixel-space antialiasing ramp, the outline band,
the blend mode and the atlas's solid texel. It measured zero channel error when it was written.

Give it a screenshot with the overlay visible; the constants at the top describe the panel it
expects, so they need updating if the thing being measured moves.

**`touch_test.sh` / `hud_test.sh`** press the on-screen buttons by posting real `WM_LBUTTONDOWN` /
`WM_LBUTTONUP` messages, so every press travels the same path a human click does —
`PostMessage` → `WndProc` → `InputController::HandleMessage` → `SubmitPointer`. Nothing reaches
into the input system directly. `click.ps1` is the one that posts them.

**`item88_test.py`** is about the ENGINE rather than the buttons; it just uses a button as the only
convenient source of a genuinely asynchronous input event. It checks that an edge-triggered press
arriving while the simulation is paused reaches the next stepped tick, **and** that a chrome action
read every pass still fires exactly once rather than repeating — the two halves of item 88's rule,
and the second is what a careless fix breaks.

Deliberately **not** built on `tetris_input`: that uses `HoldKey`, the scripted path item 84 already
fixed, so a test written on it passes with or without item 88 and proves nothing. It is also the
only script here that starts and stops the app itself, which is what lets it guard the port.

---

## Two things that will waste an afternoon if they are not known

Both were found the hard way and are the main reason these scripts are worth keeping.

### The app must consider itself focused — and do NOT use SetForegroundWindow to arrange it

`SubmitSystemKey` drops key-**downs** while `!f_has_focus` — deliberately, because a click in a
background window has no business driving the game. The pointer still hit-tests and the button
still lights up; only the key is discarded. So the symptom is a log showing flawless hit-testing
and a game that does not respond, which reads exactly like a broken button.

The obvious fix is `SetForegroundWindow`, and it is the wrong one twice over. It **yanks the window
in front of whoever is at the keyboard**, several times per check — obnoxious in itself, and it then
races whatever that person is doing. And it is unreliable by design: Windows refuses foreground
changes requested by a background process under a pile of conditions, so it works most of the time
and silently fails the rest, which showed up as a suite that passed one run and failed three checks
on the next.

So `click.ps1` posts **`WM_ACTIVATE`** itself and never touches the real foreground. That is the
same message `InputController::HandleMessage` listens for to set `f_has_focus`, so it drives the
real code path rather than reaching past it, and it leaves the desktop alone.

It does not weaken what is being tested: that the focus gate works is proven separately, by presses
being dropped when it is not set.

**Still run these while you are not using the machine.** The keyboard is *polled*
(`GetAsyncKeyState`), not message-driven, so anything typed while a suite runs can land on a mapped
key and move the piece. That is the one remaining way a human can distort a result, and no amount
of care inside the harness can prevent it.

### Level-triggered and edge-triggered actions used to need testing differently — item 88

**This split is gone, and the way it went is the point of writing it down.**

Movement (`IsKeyDown`) always survived being applied on a non-ticking pass. Rotation
(`WasKeyPressed`) did not: while the simulation was paused the physics loop kept running
non-ticking passes, each draining the event queue, so a press raised its edge on a pass that did
not tick and `NextInput` cleared it before any tick saw it. So `touch_test.sh` checked movement
**paused and single-stepped** and rotation **unpaused**, and asserted the limitation at the end on
purpose, so that a fix would fail here loudly rather than let the suite quietly keep testing around
a bug that no longer existed.

**It worked exactly as intended.** Item 88 closed on 2026-09-14 and that check failed on the next
run. Both movement and rotation are now checked paused and stepped, which is strictly better:
deterministic and timing-free, with no sleeps racing the simulation.

The rule now is that an edge is cleared once something has **read** it, or once a **ticking** pass
has been and gone — so an unread edge waits for the tick that wants it, while chrome read every
pass from `UpdateView` is consumed immediately and still fires once. Both halves are checked:
`touch_test.sh` ends by stepping again after a rotation and asserting the piece does **not** turn a
second time, which is what a fix that kept edges instead of consuming them would break.

Full record, including the before-and-after measurement, is item 88 in
`docs/engine_backlog_done.md`.

### A stale app makes every one of these lie

Every app binds port 8765, and a second one starts perfectly happily while its server silently
fails to bind — so a leftover instance answers instead and the suite reports on **the wrong build**.
This is the failure that looks like a regression in the thing you just fixed, and it cost a
confused round during item 88: a leftover control build made a working fix look broken.

`touch_test.sh` now refuses to start if `tetris.exe` on disk is newer than the running process (the
exe cannot be relinked while it runs, so that combination means the rebuild is not what is
running), and `item88_test.py` — which starts the app itself — refuses to start if anything is
already answering on 8765.

---

## Maintenance

The button coordinates are hardcoded for a **1200x900** window (`apps/tetris` resizes itself to
that in `Init`) and are derived in comments from the same constants
`ApplicationTetris::LayoutTouchButtons` uses. If the layout or the window size changes, the
coordinate block at the top of each suite is what needs updating — and `LayoutTouchButtons` is the
thing to read to work out the new numbers.
