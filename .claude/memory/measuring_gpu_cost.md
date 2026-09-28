---
name: measuring-gpu-cost
description: "CPU frame timers cannot measure shader cost here (driver forces vsync, wglSwapIntervalEXT is NULL); per-pass GPU timers now exist for whole-pass cost, in-shader counting for sub-pass questions"
metadata: 
  node_type: memory
  type: project
  originSessionId: bffdf4db-124b-4934-90e6-1b635860316c
  modified: 2026-09-27T20:07:44.241Z
---

**VSYNC WORKS NOW (checked 2026-09-27): the NULL-wglSwapIntervalEXT paragraph below is stale.** The log says "VSync: Enabled", and `renderer_timings {"vsync": false}` switches it (applied on the render thread). Measured on archer, visible window: 75 fps on (the 75 Hz monitor), 145-150 fps off, GPU-bound at ~6.1 ms. `renderer_timings` also now reports `prerender_us`, `scene_draw_us` (minus `renderer_us` = physics_mutex wait), `physics_us`, and `cpu_avg_us` per pass. `renderer_us` minus the sum of the passes' `cpu_avg_us` is CPU time spent outside every pass, and that is how the 8.4 ms quadratic `GetChild(i)` loop in `CullLights` was found. A minimised window is still paced to 60 fps whatever vsync says, so restore it with ShowWindow(h,4) to measure frame rate.

**SUPERSEDED IN PART since 2026-09-17: the renderer now has real per-pass GPU timers** - `Renderer::BeginGPUPass`, read out of the Engine panel's Performance section. For "what does this pass cost in milliseconds", use those, not what follows. See [[gpu-pass-timers]]. Everything below still holds for CPU timers, and the counting trick is still the better tool for sub-pass questions, where a whole-pass timer cannot separate the branch being changed from the rest of the pass.

**Do not try to measure a shader optimisation in this repo with a frame timer.** On this machine
`wglSwapIntervalEXT` resolves to NULL, so `Renderer::SetVSync` silently never runs, the driver
forces vsync on, and `GetVSync()` reports false while the frame time is quantised to the refresh
interval. `tmr_render_loop` therefore reports the refresh rate for every setting. `renderer_us`
(`Renderer::tmr_frame`, the whole of `DrawFrame`, which ends in the object-pick `glReadPixels` and
so does contain a GPU sync) does respond to load, but its frame-to-frame noise is larger than the
effect being measured whenever that effect covers less than most of the window.

**What works instead: count the work in the shader and read the count back off the screen.** Add a
debug view that outputs the sample or texture-fetch count encoded in a pixel — red carries
`count/SCALE`, blue is 1 as a marker so a script can find the pixels that shader wrote — then sum
red over the blue-marked pixels of a `screenshot`. Exact, deterministic, reproducible frame to
frame, and identical on any GPU. `apps/bomber`'s `debug_view 3` is a worked example.

The readback is honest: there is no sRGB anywhere in the pipeline and
`CaptureScreenshotIfRequested` does a plain `glReadPixels(GL_RGB, GL_UNSIGNED_BYTE)` off an
RGBA16F buffer, so `pixel/255` recovers what the shader wrote to within 1/510.

**Why:** measuring a GPU-side change by wall clock needs the GPU to be the thing the frame is
waiting for, and in these apps it usually is not. Counting sidesteps that entirely and measures the
thing the optimisation actually changed.

**How to apply:** before claiming any shader change is faster, decide whether the effect covers
enough of the window to move a frame timer. If not, instrument it and count. Pin the camera with
`camera_set` before *every* capture and check it with `camera_get` afterwards (see
[[engine-forward-is-minus-z]]), `sim_pause` rather than `bomber_lock_input` to freeze everything
including AI, and confirm only one app instance holds port 8765 - a second instance returns the
first one's window and every number is then from the wrong process.

Related: [[gpu-pass-timers]], [[testfx-bench-gotchas]], [[bomber-app]], [[running-app-is-user-driven]].
