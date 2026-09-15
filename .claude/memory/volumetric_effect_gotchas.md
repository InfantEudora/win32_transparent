---
name: volumetric-effect-gotchas
description: "three things that cost screenshots when writing a raymarched volume in this engine - sim_step's arg name, sun_intensity's units, and box-relative vs feature-relative shape terms"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 8a9b69d2-99f4-4de3-a7fd-bee7ffca5edb
  modified: 2026-09-15T09:01:57.651Z
---

Three traps hit while building the explosion in [[bomber-app]]. Each looked like a shader bug and
none of them was one.

1. **`sim_step` takes `num_ticks`, not `ticks`.** An unknown argument is ignored and the tool
   defaults to 1, so a filmstrip script that asks for tick 30 silently shows tick 5 - and the
   effect looks like it is not animating. Check `ticks_advanced` in the reply, which is the value
   the tool's own description says to trust.

2. **A volume needs a `sun_intensity` scale on the sun's brightness.** A surface turns radiance
   into pixels through a BRDF and an NdotL; a volume integrates it over density and step length,
   so the same number does not land in the same place. Without the scale, a sun at brightness 3.2
   made the scattered sun ~5x brighter than the fire itself and the whole fireball rendered white
   with a pink edge. `apps/ship`'s `raymarch_volume.frag` carries this uniform and says why; copy
   it, do not drop it.

3. **Shape terms must be relative to the FEATURE, not the box.** Noise scale, shell thickness and
   rim softness expressed in box units mean a completely different-looking effect at the two ends
   of one blast, because the fireball is a tenth of the box early and most of it late. The first
   version had less than one noise cell across the young ball, so it rendered as a smooth white
   marble.

**How to verify any of this:** `sim_pause`, detonate, then `sim_step` to an exact tick and
screenshot - frame N is reproducible because the effect is driven by the tick counter. The shader's
`f_show_box` debug views are the quick discriminator: 1 reads out the marched interval (is the box
being intersected as a slab, and is the G-buffer depth clamp shortening it at walls?), 2 reads out
the G-buffer the shader is handed.
