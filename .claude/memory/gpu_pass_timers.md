---
name: gpu-pass-timers
description: "Renderer has per-pass GL_TIME_ELAPSED timers (Engine -> Performance); scopes must never nest, and the first measurement showed the pick glReadPixels is ~89% of Renderer Time"
metadata:
  node_type: memory
  type: project
  originSessionId: b04427ef-0a9b-41e0-9b95-5e3552bac6f5
  modified: 2026-09-30T21:30:27.352Z
---

`Renderer::BeginGPUPass(GPU_PASS_*)` / `EndGPUPass` wrap each pass of `DrawFrame`, plus the
overlay and ImGui passes in `Application::DrawFrame`. Results land in ordinary `PerfTimer`s
(`PerfTimer::AddSample`, added 2026-09-17 by splitting the stats half out of `Stop()`), so they get
the same 60-sample rolling window as every CPU timer and read out of **Engine -> Performance**.
Built 2026-09-17; ported from the Android port's `GPUPassTimer` but simpler, because
`glGenQueries`/`GL_TIME_ELAPSED` are core GL 3.3 on desktop and needed no feature test - only six
entry points added to `core/glad.h` and `core/glad.cpp`. The same scopes also push
`glPushDebugGroup`, so a RenderDoc capture is labelled with the same names.

**Scopes must never nest or overlap.** Only one `GL_TIME_ELAPSED` query can be active per context,
so there is no whole-frame GPU timer and there cannot be one. The occluder field and its jump flood
are two ADJACENT scopes inside `RenderFieldPass`, not an outer and an inner. `BeginGPUPass` refuses
a nested Begin and logs which pass is still open; without that the symptom is a `GL_INVALID_OPERATION`
somewhere unrelated. The column therefore does not add up to the frame - untimed GL work is invisible,
and a gap between the total and the frame time is real work nobody has scoped yet.

**First measurement, `apps/ship`, 1680x900, 446 renderable objects:** Color 2.79 ms, Custom shaders
(the raymarched volume) 1.71 ms, MSAA resolve 0.23, Deferred G-buffer 0.20, Shadow map 0.17, ImGui
0.06, skybox/skinned ~0.001; total timed 5.15 ms against a 13.3 ms frame. **And `Pick Readback`
5.56 ms of a 6.23 ms `Renderer Time`** - the mouse-over `glReadPixels` in `DrawFrame` is ~89% of the
CPU-side renderer cost. It is timed with a CPU `PerfTimer` (`tmr_pick_readback`) on purpose: it is a
sync, so the cost is a stall in wall clock, and a GPU timer around it would report the near-zero time
the GPU spent. That readback went asynchronous the same day and now costs
85 us - see [[async-picking-readback]], including the two ways of getting it wrong that both
measured SLOWER than the blocking version.

**Why:** a CPU timer around a GL call measures submission, not execution, and this repo had already
concluded that twice from opposite directions - see [[measuring-gpu-cost]].

**How to apply:** for "what does this pass cost", read the table. For sub-pass questions ("is this
branch of the shader cheaper"), the in-shader counting trick in [[measuring-gpu-cost]] is still the
right tool - it measures work, not milliseconds, and is noise-free. A pass that did not run files a
zero, so toggling one off decays its average to zero rather than freezing it; the panel dims those
rows.

Also readable over MCP as `renderer_timings`, which is how to A/B without the panel.

**THEY FROZE WHEN THE GPU WAS THE BOTTLENECK - FIXED 2026-09-30.** The ring was two queries per
pass, and a sample was skipped when the reused one was not ready, so once the GPU ran more than 2
frames behind the LATE passes stopped updating (archer's Color read exactly 11838 us for minutes;
7924 us for a 20 ms frame at the bridges). Now `Renderer::GPU_QUERY_RING` = 6, results are
collected oldest-first in `EndGPUFrame` and never waited for, and a pass that ran
`GPU_STALE_FRAMES` (30) frames without a fresh result is `f_stale`: renderer_timings `stale` per
pass and `gpu_total_stale`, "(stale)" in the panel, `uncapped_fps` 0. Checked against a temporary
build whose reads waited: within ~5%. Numbers taken GPU-bound BEFORE this fix are suspect. Check
`nvidia-smi` first anyway: the user sometimes runs an image generator on the same GPU.

Related: [[async-picking-readback]], [[per-app-build-layout]], [[raymarch-volume-stage-plan]], [[android-port-merge]].
