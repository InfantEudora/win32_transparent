---
name: use-physics-flag
description: "USE_PHYSICS exists as of 2026-09-18; it is the one flag that changes the SHARED core objects, so it follows CONFIG's separate-tree pattern, not USE_MCP's _none-twin pattern"
metadata: 
  node_type: memory
  type: project
  originSessionId: 95694e8a-7416-4886-a7d1-84478edf5bc6
  modified: 2026-09-18T16:32:21.285Z
---

`USE_PHYSICS` (engine.mk, added 2026-09-18, backlog 73 closed) drops ReactPhysics3D from a build.
`apps/bomber` is the first app with `USE_PHYSICS := 0`; `ocpp`, `sim`, `testfx` and `ui` are the
other four of fifteen that create no `PhysicsWorld`.

**It is switched differently from every other flag in the family, and this is the thing to
remember.** `USE_MCP`/`USE_IMGUI` swap a `_none` translation unit precisely so no `-D` reaches
core. That cannot work here: `USE_PHYSICS` changes *headers* (the type of `Object::physics`, of
`Scene::physics_world`, and which methods `Object` declares), which every TU including `Object.h`
sees. So `-DUSE_PHYSICS` goes **above** the `CORE_CFLAGS` line and the shared core tree gains a
suffix — `build/core/<config>_nophysics/` — exactly the way `CONFIG` already handles debug vs
release. Four trees coexist. **No `.buildflags` stamp was needed** (backlog 72 does not become
load-bearing), because both the shared objects and the app's own are separated by directory.

Flipping the flag therefore **rebuilds core** rather than relinking. See [[per-app-build-layout]].

**Why it is worth it, measured, because the obvious objection is wrong:** `--gc-sections` does
*not* reclaim an unused physics engine. Bomber never created a `PhysicsWorld`, yet 88 of rp3d's
~94 archive members were in its shipped exe — the core objects it links *name* those symbols
whether or not the app calls them, and the linker collects on reachability, not behaviour. Actual
saving 1.20 MB of the 7.11 MB ship exe (49% of non-asset content) and 12 MB of the debug exe.

A `-Wl,-Map` sum **over-predicted by 457 KB** (1.72 MB predicted vs 1.26 MB actual) — treat a
map-derived figure as an upper bound, not an estimate.

Two latent bugs fell out, both masked by `Object.h` -> `Physics.h` pulling things in transitively:
`core/Window.h` used `std::function` without including `<functional>`, and `core/Application.h`
included `ObjectCollider.h` (which holds an `rp3d::Collider*`) although nothing in that header
names the type — dragging rp3d's headers into all fifteen apps.

The remaining half of backlog 73 is now **item 94**: `engine.mk` still has no notion of a target
that is not an app, which is what a host build tool would need. See [[android-port-merge]] for the
port's `sprite_packer.exe`, the consumer that motivated the flag.
