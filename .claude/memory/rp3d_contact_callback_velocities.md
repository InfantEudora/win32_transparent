---
name: rp3d-contact-callback-velocities
description: "rp3d reports contacts before it solves them, so a contact handler that reads a body's velocity must filter to ContactStart - ContactStay ticks see the already-bounced velocity"
metadata: 
  node_type: memory
  type: project
  modified: 2026-09-10T11:27:30.995Z
  originSessionId: 8536c1be-600c-4964-8a20-e1b5bd5b6f80
---

`PhysicsWorld::update()` calls `reportContactsAndTriggers()` (which is what invokes the app's
`rp3d::EventListener::onContact`) BEFORE `integrateRigidBodiesVelocities` and
`solveContactsAndConstraints`. So on the tick a contact *begins* the callback sees pre-impact
velocities - but rp3d keeps reporting the same contact as `ContactStay` for every tick the shapes
still overlap, and on those later ticks the solver has already reversed the velocity.

An unfiltered handler that reads `GetVelocity()` therefore gets the incoming direction once and the
bounce direction two or three times after it. That was the actual cause of ApplicationShip's laser
knocks pushing the hinged door back toward the shooter (2026-09-10): the fix is
`contactPair.getEventType() == CollisionCallback::ContactPair::EventType::ContactStart`. A fast,
light projectile overlaps a target for several ticks, so the stay events always outnumber the good
one.

ApplicationShip's whole laser branch now gates on `ContactStart` up front, covering both the door
knock and the asteroid health hit (the latter had been draining several health per shot for the same
reason). Residual, and by design: a laser bolt is not destroyed by its impact, so it can bounce off
and land a genuine second `ContactStart` - measured at roughly 2 shots in 8 doing 2 damage.

The same shape applies to TRIGGERS: `onTrigger` gets `OverlapStart` / `OverlapStay` / `OverlapExit`,
and the ship stays inside a pickup's trigger volume for several ticks after touching it, so
ApplicationShip's pickup collection filters to `OverlapStart` AND sets a `f_collected` latch on the
Pickup (the ship has two colliders, so one touch can report two OverlapStarts in a single tick).

**How to apply:** In any `onContact` or `onTrigger` handler, decide the event type first. Direction or impulse from
a body's velocity - `ContactStart` only, no exceptions. Per-tick dwell effects (damage while
touching, rumble) can use stay events, but must not read a velocity for a direction.

Related: [[rp3d-local-fork-hinge-motor-patch]], [[threading-model]] (the callback runs inside the
physics step - queue anything that touches a body, don't apply it there).
