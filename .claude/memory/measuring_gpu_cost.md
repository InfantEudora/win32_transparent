---
name: measuring-gpu-cost
description: "Frame timers cannot measure shader cost on this machine (driver forces vsync, wglSwapIntervalEXT is NULL); count work in-shader and read it back from a screenshot instead"
metadata: 
  node_type: memory
  type: project
  originSessionId: bffdf4db-124b-4934-90e6-1b635860316c
  modified: 2026-09-15T13:07:54.098Z
---

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

Related: [[testfx-bench-gotchas]], [[bomber-app]], [[running-app-is-user-driven]].
