---
name: ship-orbit-camera-is-the-canonical-one
description: "copy the orbit camera from ApplicationShip/ApplicationTank, not from testfx - testfx's shorter version has three real bugs in it"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 8a9b69d2-99f4-4de3-a7fd-bee7ffca5edb
  modified: 2026-09-15T09:49:44.246Z
---

When a new app needs an orbit camera, **copy the block at the end of `ApplicationShip::UpdateView`
(the same one `ApplicationTank` has), not the shorter one in `apps/testfx`.** I copied testfx's
into [[bomber-app]] and the user caught it immediately.

testfx's version is missing three things, and all three are real:

1. **It reads `INPUT_MOUSE_X/Y` (the absolute cursor position) instead of `INPUT_MOUSE_DELTA_X/Y`.**
   The DELTA ones are raw, unaccelerated Raw Input movement - they keep reporting once the pointer
   is against the edge of the screen and are not bent by the pointer acceleration curve.
   `core/InputController.h` says outright that existing camera code still reads the cursor delta
   and that switching is a deliberate one-line change per call site.

2. **It reads the deltas INSIDE the middle-button gate.** InputController only clears the delta of
   a map that was actually read this pass, so a gated read lets movement pile up for the whole time
   the button is NOT held, and the first frame of a drag applies all of it at once. Drain them every
   pass, outside the gate.

3. **No `main_window->f_has_focus` gate and no `UIWantsMouse()` gate**, so dragging a slider in a
   debug panel also swings the camera.

Sensitivities to match the rest of the repo: `/50` for orbit, `/100` for pan, wheel accumulated into
a sum that dollies by `distance * sum / 50` and decays `/= 1.1` per pass so a flick coasts. Clamp
the resulting distance - a step proportional to distance is geometric in both directions, so
scrolling out compounds and a few seconds of it leaves the subject a speck on a black screen, which
looks exactly like a shader that stopped drawing.

**Verifying it without a human:** synthesize a real drag with `mouse_event` from PowerShell
(`SetCursorPos`, 0x0020 middle-down, 0x0001 moves, 0x0040 up) - keys and mouse reach these apps
only through Raw Input, so `PostMessage`/WM_KEYDOWN does nothing (`InputController::HandleMessage`
only traces those). Then check `camera_get`: a correct orbit leaves the distance to `camera_target`
unchanged to several decimals and, for a horizontal drag, leaves Y untouched.
