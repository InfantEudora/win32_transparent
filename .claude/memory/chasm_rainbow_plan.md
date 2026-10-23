---
name: chasm-rainbow-plan
description: "Chasm rainbow in the falls' spray - step 1 BUILT 2026-10-23 (box per sheet, angle-coloured, additive); sunlit-only next; how to aim a view at the bow"
metadata:
  node_type: memory
  type: project
  originSessionId: 67467a7d-9643-451a-b48e-a42a2e117002
  modified: 2026-10-23T12:29:58.237Z
---

Plan + as-built: apps/chasm/docs/rainbow_plan.md. A rainbow drawn as LIGHT in each fall's spray, not an object: a
box per WaterSheetShape (ApplicationChasmRainbow.cpp, assets/shaders/chasm_rainbow.frag), colour from the view
ray's angle to the antisolar point (primary 41.5, secondary 51.9), amount from a soft cloud marched against the
G-buffer, ADDED to the frame. Steps: 1 built; 2 sunlit spray only (f_lit + CalcShadow); 3 breathing on SimSeconds,
view inside a box.

**Why:** the user asked for "a rainbow around the waterfall"; a place-fixed arc would look like a sculpture as the
orbit camera turns. Chasm's sun is NW all year (az 305, 24-72 deg up) and the camera looks 34-77 deg down, so the
42 deg ring is on screen in most headings - the bow is seen with no cheating.

**How to apply:**
- To SEE it, solve the view: a view ray on the 41.5 deg cone round the antisolar point through a box's middle;
  over the chasm the orbit target keeps its height (plateau ~5.66), so target = M - d*s at that height. chasm_time's
  weather.rainbow.boxes gives each box's middle and current angle. debug 2 shows where the ring crosses a box.
- Core: CustomShaderSubPass now restores the blend function (glGet before the callback). Renderer.h's contract
  comment not yet updated (avoided a full rebuild while another agent built).
- The custom pass leaves GL_CULL_FACE disabled after chasm's water sheet (reset never re-enables it) - any later
  custom program draws both faces; additive shaders must keep one fragment per pixel themselves.

Related: [[chasm-waterfalls]], [[chasm-weather-plan]], [[measuring-gpu-cost]]
