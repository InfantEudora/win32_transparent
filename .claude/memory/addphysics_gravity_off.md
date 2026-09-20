---
name: addphysics-gravity-off
description: "Object::AddPhysics returns a body with gravity DISABLED; SetStatic(false) does not turn it on, and the symptom looks like a solver explosion"
metadata: 
  node_type: memory
  type: project
  originSessionId: 1f02e4da-fa81-4634-9800-ae5b6cfa5ba7
  modified: 2026-09-20T16:22:39.945Z
---

`Object::AddPhysics(world)` hands back a body that is STATIC **with gravity off**. Calling
`SetStatic(false)` makes it dynamic but leaves the gravity flag alone, so a "dynamic" prop built
the obvious way never falls. Fix: `physics->SetGravityEnabled(true)`.

**Why:** the failure does not look like missing gravity. Nothing drifts gently upward, so the
scene looks perfect until something is touched. With no gravity there is no weight on the floor,
so no normal force, so **no friction** - anything shoved once slides forever, and anything given
a scrap of upward velocity leaves the level and keeps going. In apps/archer this read as the
character "flinging targets across the map": boards ended up at x -116, then at y +113 doing
2 units/s, then at y -658 doing 157. Three separate wrong theories were chased (collision masks,
deep penetration against a kinematic body, kick tuning) before anyone read `gravity: false` off
the `object_get` MCP tool.

**How to apply:** when a rigid body behaves as though the solver is exploding, or props never come
to rest, call `object_get` on one and read `physics.gravity` FIRST - it is one call and it rules
out the whole class. Any new app that builds dynamic bodies needs `SetGravityEnabled(true)`
alongside `SetStatic(false)`; see `MakePlanarBody` in [[archer-app]], which does both in one place
so no caller can forget. Related: [[rp3d-contact-callback-velocities]].
