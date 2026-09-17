---
name: async-picking-readback
description: "Picking reads the G-buffer through pixel pack buffers; the two things that make it work are a fence (never map unfenced) and reading each attachment in its EXACT native format"
metadata:
  type: project
---

`Renderer::ReadPickingAsync` (2026-09-17) replaced the blocking mouse-over `glReadPixels`.
Measured on `apps/ship` in the Engine panel, same metric before and after: **Pick Readback
5557 us -> 85 us, and Renderer Time 6228 us -> 782 us.**

**Two things had to be right, and getting either wrong made it SLOWER than the blocking version
it replaced.** Both cost a build-and-measure cycle to find, so they are worth knowing before
touching this again:

1. **Never map a pixel pack buffer without checking a fence first.** `glMapNamedBufferRange`
   blocks until the GPU has finished writing that buffer, which is the exact stall the PBO exists
   to remove. A two-slot ping-pong is NOT enough on desktop - with vsync the driver runs a frame
   or two ahead, and the naive version measured **6.65 ms, worse than the 5.56 ms synchronous
   readback**. Each slot now carries a `glFenceSync` polled with `glGetSynciv(GL_SYNC_STATUS)`,
   and there are three slots. Poll, never wait; a slot that is not ready is overwritten and that
   frame simply contributes no hover update.

2. **Read each attachment in the format it actually holds.** Position and normal are `RGBA16F`.
   Asking `glReadPixels` for `GL_RGB`/`GL_FLOAT` is a channel-count and type conversion with no
   GPU path, so the driver converts on the CPU, which means it needs the pixels NOW, which means
   it waits for the GPU. **That single read cost 4.8 ms while the exact-format `R32I` read beside
   it cost 31 us.** Reordering them did not move the cost - it followed the mismatched read, not
   the position in the sequence. Both are now `GL_RGBA`/`GL_HALF_FLOAT` with a `HalfToFloat` in
   Renderer.cpp doing the unpack: 15 us. Anyone changing a deferred attachment's internal format
   must change its read to match.

**The one-frame lag has a trap of its own.** The object id buffer holds
`instancedata_t::objectindex`, an index into `renderable_objects` *as it was when the pixel was
drawn* - and culling rebuilds that vector every frame, so resolving a stale index against the
current one names a different object whenever the camera moves. `picking_id_snapshot` keeps the id
list each in-flight read belongs to. The tidier fix is for the shader to write the real object id
instead of the index; that changes what `objectindex` means for every app and was deliberately not
bundled in.

**Also: unbind the pack buffer.** A buffer left bound to `GL_PIXEL_PACK_BUFFER` silently redirects
the next `glReadPixels` anywhere in the engine - `CaptureScreenshotIfRequested` is the one that
would hit, and it would return an empty image with no error.

**How to apply:** verify with `renderer_timings` over MCP (see [[gpu-pass-timers]]), not with frame
time - frame time here is vsync-bound and moved by things unrelated to this. Correctness is checked
by hovering a flat floor and reading "Normal at mouse" in the Engine panel's Input section: it must
be a clean (0, 1, 0).

Related: [[gpu-pass-timers]], [[measuring-gpu-cost]], [[android-port-merge]].
