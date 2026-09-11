---
name: scene-addobject-during-tick
description: "Scene::AddObject push_backs renderer->objects, which Scene::UpdatePhysics is iterating with a range-for - so calling it from an UpdatePhysicsState override is capacity-dependent UB"
metadata: 
  node_type: memory
  type: project
  modified: 2026-09-10T11:46:26.831Z
  originSessionId: 8536c1be-600c-4964-8a20-e1b5bd5b6f80
---

`Scene::UpdatePhysics` walks the scene as `for (Object* object:renderer->objects)` and
`Scene::AddObject` is a `push_back` onto that same `std::vector<Object*>`. So any
`UpdatePhysicsState()` override that adds an object to the scene invalidates the loop's iterators
whenever the push_back reallocates. It usually does not reallocate - the vector's capacity is
already well past its size after a scene has loaded (the Ship scene alone holds ~470 objects) -
which is exactly why this has never visibly broken.

Known in-loop callers as of 2026-09-10: `AsteroidExplosion::UpdatePhysicsState` adds a fragment
particle every tick of an explosion (via `ParticleEmitter::EmitParticles` ->
`target_scene->AddObject`) and 2-3 fragment asteroids when the morph completes. The other
`EmitParticles` call sites in the Ship app are safe by accident of where they are called:
`ShipCharacter::MoveForwardBy` (exhaust) and `ShootLaser` both run from `RunSimulationTick`, which is before
the object loop, not inside it. `ApplicationTank.cpp` has its own call sites that were not checked.

**How to apply:** Prefer a staging vector drained at the top of `RunSimulationTick` (see ApplicationShip's
`pending_asteroid_explosions`) over calling `Scene::AddObject` from inside `UpdatePhysicsState`. A
real fix would be either an index-based loop over a snapshot of the size, or a deferred-add queue
inside `Scene` itself; the user has not asked for either, so don't do it unprompted - but don't add
new in-loop callers either.

Related: [[threading-model]], [[deterministic-sim-plan]] (the command queue is NOT the answer here -
this is same-thread re-entrancy, not a thread crossing), [[rp3d-contact-callback-velocities]].
