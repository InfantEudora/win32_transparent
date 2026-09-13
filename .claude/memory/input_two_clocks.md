---
name: input-two-clocks
description: "The engine has TWO input clocks - UpdateView/BeginPass run every physics pass, RunSimulationTick/GatherInput run only on ticking passes - and one set of edge flags serves both; this is what made scripted edges undeliverable under sim_step (backlog item 84, fixed 2026-09-14)"
metadata: 
  node_type: memory
  type: project
  originSessionId: 3570a9cc-102e-4ec0-b564-6a6c00ef4850
  modified: 2026-09-13T22:14:21.277Z
---

Found while fixing backlog item 84 on 2026-09-14. Not written down anywhere before, and it took
sorting every edge read in the repo to see it.

**The physics loop runs PASSES, not ticks.** `Application::PhysicsThreadFunction` does
`UpdateInput()` → `BeginPass()` → *(if it ticks)* `UpdateTickInput/UpdateAnimations/RunSimulationTick/UpdatePhysics`
→ `UpdateView()` → `NextInput()`. Free-running, every pass ticks. Paused or single-stepping, most
passes do not - and `NextInput()` clears the per-key edge flags at the end of **every** pass either
way.

**So there are two clocks, and every edge reader belongs to one of them:**

| hook | runs | edge reads |
|---|---|---|
| `UpdateView`, `Scene::BeginPass` | every pass | 33 sites - the pause key, object picking, UI toggles |
| `RunSimulationTick`, `GatherInput` | ticking passes only | 18 sites - gameplay actions in 7 apps |

`f_was_pressed` / `f_was_released` serve both and are cleared per pass, which suits the first group
and not the second. That is the whole of item 84: a scripted `HoldKey` used to advance when
`sim_tick` had *changed*, checked from `ApplyPendingEvents` before `BeginPass` drained the command
queue - so under `sim_step` the edge landed on a pass that did not tick and was cleared unseen.
Level input (`IsKeyDown`) was fine throughout, because `f_isdown` survives a pass boundary and an
edge flag does not. **That contrast is the diagnostic**: if scripted level input works and scripted
edge input does nothing, this is the shape.

**The fix was timing, not flags** - `AdvanceSyntheticHolds` now runs from
`InputController::ApplyTickInput`, called inside `if (f_tick)`. Two traps worth keeping:

- **"Clear edge flags only on ticking passes" breaks the pause key.** `BeginPass` computes
  `f_tick_this_pass` *after* servicing pause, so the pass that pauses does not tick; the
  `INPUT_PAUSE` edge would survive to the next pass and unpause again immediately.
- **Scripted holds can no longer drive anything `BeginPass` reads**, because they now advance after
  it. That is `INPUT_PAUSE` and nothing else, and nothing scripts it - it is only ever a real key.

Splitting the flags in two (per-pass and per-tick) is still the more honest model and is the thing
to reach for if [[deterministic-sim-plan]]'s record/replay needs the clocks named. It was measured
at 18 call-site changes across 7 apps and deliberately not done.

See also [[threading-model]] for the mutex side of the same loop.
