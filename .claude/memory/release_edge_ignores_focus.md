---
name: release-edge-ignores-focus
description: "A click or keypress made OUTSIDE the window still fires anything watching WasKeyReleased, because raw input is RIDEV_INPUTSINK and key-up is honoured unfocused; affects ~58 release-edge reads across 12 apps"
metadata: 
  node_type: memory
  type: project
  originSessionId: dced72db-1306-463d-bccf-457b06014017
  modified: 2026-09-17T15:29:54.677Z
---

Found 2026-09-17 building bomber's title screen: clicking *next to* the window started the game.
Two engine behaviours combine, and both are deliberate, which is why it does not look like a bug.

- Raw input is registered **`RIDEV_INPUTSINK`** (`core/RawInput.cpp`), so it is delivered whether
  or not the app is in the foreground.
- `InputController::SubmitSystemKey` drops a key **down** while unfocused but **always honours the
  up** - "so anything already held can still release." Losing focus *also* runs the
  `f_release_all_keys` sweep, which raises `f_was_released` for anything held.

Clicking outside gets the down in before the focus change is processed, then the up (or the sweep)
supplies the release. **So anything firing on a release edge is reachable from outside the window.**
Press edges are safe - `f_was_pressed` needs the down, which the focus gate drops.

**The fix pattern** (see `ApplicationBomber::RunSimulationTick`, the title-screen gate):

```c
bool f_clicked = input->WasKeyReleased(INPUT_CLICK_LEFT);   // read UNCONDITIONALLY
if (f_clicked && input->IsInputLive()){ ... }
```

- The read stays **outside** the focus test. An unread edge is deliberately *kept* across passes
  (backlog 88, see [[input-two-clocks]]), so gating the read parks the stray release and spends it
  the instant focus returns - "starts on the click that focused the window", which is worse.
  Consuming and discarding is what throws it away.
- `IsInputLive()` (= `HasFocus() || HasSyntheticHolds()`) rather than plain `HasFocus()`, so a
  **scripted** click still works with the window in the background - which is how these apps are
  driven over MCP most of the time.

**This is not specific to one app.** `WasKeyReleased` has ~58 call sites across 12 apps; in-game
actions mostly get away with it because the window is focused in practice. Not fixed centrally: the
obvious lever is stopping the focus-loss sweep raising `f_was_released`, but a key genuinely held
when you alt-tab *should* read as released, so the trade-off is real and was left to the user.
