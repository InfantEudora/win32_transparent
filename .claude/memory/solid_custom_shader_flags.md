---
name: solid-custom-shader-flags
description: "moving an ordinary opaque mesh onto a custom shader needs Shader f_writes_gbuffer + f_lit + f_casts_shadow + f_solid, or it loses shadows/SSAO; chasm's winter ground is the example"
metadata:
  node_type: memory
  type: project
  originSessionId: f74358c8-36a9-48cf-a846-52036d5d2ea0
  modified: 2026-10-06T09:21:42.893Z
---

Added to core 2026-10-06 for chasm's winter ground (apps/chasm/ApplicationChasmSnow.cpp, shaders/chasm_ground.frag):
- `Shader::f_casts_shadow`: custom-shader meshes go into the sun's shadow map. Without it, only MESH_MODE_NORMAL meshes cast shadows.
- `Shader::f_solid`: the program draws in the COLOUR pass after the NORMAL meshes. The custom pass otherwise runs AFTER CompositeSSAO, so a solid surface drawn there loses its SSAO and can never match default.frag's frame.
- ComputeViewCull now culls custom-shader meshes too, when f_frustum_cull is on.

**Why:** with all four flags plus default.frag's exact path, cover 0 measured 0 differing pixels against the default shader. Missing `alpha_clip`/`f_normal_mapping` uniforms (the default shader gets them, custom ones don't) silently change alpha.

**How to apply:** for any "same surface, own colour" shader, copy the BuildGroundShader/SetGroundUniforms pattern. Re-tag the mesh after every SetMeshData, which resets mesh_mode to NORMAL. See [[chasm-game-plan]].
