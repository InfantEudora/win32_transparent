---
name: minimized-focus-leak
description: "FIXED + verified 2026-10-18: WM_ACTIVATE is per-thread activation, not the foreground; InputController::HasFocus now asks GetForegroundWindow + !IsIconic (core). Other apps get it on their next build"
metadata:
  node_type: memory
  type: project
  originSessionId: 5b3cd70d-8db0-4bb3-bbc1-b3b9cc4ab6df
  modified: 2026-10-18T11:11:14.143Z
---

Sightings: 2026-10-11 a --minimized chasm had input_focused true with VS Code in front and obeyed the user's typing
(new map, winch tool, unpause, pans); 2026-10-18 a minimised test chasm left on the title quit by itself ("Quit, asked
and answered") and autosaved; the user confirmed it "caught me a few times". Raw input is RIDEV_INPUTSINK, so every
key on the machine reaches every running app; InputController's focus flag is the only gate.

CAUSE, reproduced 2026-10-18 (scratchpad focus_probe.ps1 - SetForegroundWindow on the minimised game via
AttachThreadInput, no keys sent): Windows REFUSED to bring it forward (foreground lock), yet input_focused went false ->
true, and stayed true after the front was handed back. WM_ACTIVATE reports the thread's own ACTIVE window, not the
system FOREGROUND; a refused/queue-local activation sends WA_ACTIVE, and since the window never had the foreground,
no WA_INACTIVE ever follows. The flag was set from messages only.

FIX (written 2026-10-18, core): InputController::HasFocus() on Win32 asks the OS each call - GetForegroundWindow() ==
focus_window && !IsIconic - and corrects the flag (SetFocused, so a loss still releases held keys). SubmitSystemKey,
SubmitAxisDelta, the GetAsyncKeyState fallback and the gamepad gate all use HasFocus(). Messages also tightened:
WM_ACTIVATE with the minimised high word is not focus, WM_SETFOCUS not while iconic, WM_SIZE minimised drops focus and
restored re-checks the foreground; same rules for Window::f_has_focus (Window.cpp; message-based still, so a hover
tip can still follow the mouse in the queue-activation case - cosmetic, clicks are gated by HasFocus).

VERIFIED with the fixed chasm (copies on 8772, probe scripts in that session's scratchpad): activated while minimised ->
in front but input_focused FALSE; restored + in front -> TRUE; SW_MINIMIZE left it the FOREGROUND window while iconic
(a second way the old flag stayed true) -> FALSE. The user typed in VS Code during the run; nothing reached the copy.
Only chasm was rebuilt; archer and the rest pick it up on their next build (scripted input/replays are untouched).

**Why:** CLAUDE.md promises --minimized is safe at a shared desk; the user wants it fixed.
**How to apply:** if keys leak again, check sim_pause's input_focused against GetForegroundWindow; the probe script
shape (activate a minimised game, read input_focused, give the front back) reproduces the old bug in a second.
Related: [[running-app-is-user-driven]], [[chasm-test-instance-autosaves]].
