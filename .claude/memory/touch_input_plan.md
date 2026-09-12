---
name: touch-input-plan
description: "On-screen buttons for Android are a THIRD input family (AddTouchButton next to AddKeyMap/AddGamePadMap), not picking and not ImGui; plan at docs/touch_input_plan.md"
metadata: 
  node_type: memory
  type: project
  originSessionId: 7db88354-8534-412a-aaa5-dbfc599c5480
  modified: 2026-09-12T22:30:45.369Z
---

Agreed direction 2026-09-13 for on-screen input buttons (Android, first tested in Tetris):
`InputController::AddTouchButton(rect, mapped_keycode)` as a third member of the
`AddKeyMap` / `AddGamePadMap` family, allocating synthetic system keycodes from a
`TOUCH_SYSKEY_BASE` of 0x20000 exactly as `GAMEPAD_SYSKEY_BASE` does. Rect list lives inside
`InputController` (not a parallel class — see the GamePadController lesson in
`InputController.h:357`). Edges only, never per-tick polling. Full plan, with four traps found in
the current code, in **`docs/touch_input_plan.md`**.

**Why:** picking is a 1x1 `glReadPixels` at the cursor (`Renderer.cpp:849`) and ImGui is
single-pointer, so both are disqualified by multi-touch, which a game pad layout must have.
Keycode-per-button is also what makes a recording replayable — it captures the action, not a
finger position, so it survives a layout change. Ties into [[deterministic-sim-plan]].

**How to apply:** nothing is implemented. Android support in the tree is one file
(`core/Debug_android.cpp`) plus a vendored `imgui_impl_android.cpp`; there is no Android build and
no 2D screen-space render pass at all. Steps 1-4 of the plan are all doable on Windows.
