---
name: background-work
description: "core/BackgroundWork.h (one below-normal worker + LatestResult<T>) built 2026-09-27 for archer's wind; results must never reach the sim mid-run - user's plan is a simple lock around sim-read areas"
metadata:
  node_type: memory
  type: project
  originSessionId: 59cf8fb8-ae6f-49e9-b97a-530bc0b0aaf5
  modified: 2026-09-27T19:30:02.480Z
---

`core/BackgroundWork.h` is the engine's generic "slow, may arrive late" mechanism, built 2026-09-27
for archer's wind rebuild (~430 ms, used to stall render AND physics because the leaves wait on
wind_mutex). One shared worker thread, below normal priority; `LatestResult<T>` does
request (latest wins) / Adopt on the owner thread / Get from any thread as `shared_ptr<const T>`.
Published results are never changed in place - a retune is copy + Set.

The user expects more heavy work to go this way (fluids etc.), so reuse it rather than starting
new threads.

**Determinism is the rule the user cares about most:** today only visuals read the wind, so adopting
whenever is fine. When the simulation starts reading an async result (the balance mechanic reading
wind), the user's idea is "a simple lock that we trigger in certain areas", so the readout never
overlaps the recalculation. Either way, adoption must not depend on when the worker finished, or
replays diverge. Rule 5 in the header says the same.

**Why:** the user agreed to this when the worker went in; see [[deterministic-sim-plan]].
**How to apply:** before an async result feeds anything in RunSimulationTick, raise how it gets
adopted (the lock, or a fixed tick) with the user. Related: [[wind-system-plan]], [[threading-model]].
