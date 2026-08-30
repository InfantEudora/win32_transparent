## Doing
- [ ] Turret tracks the mouse-cursor target at a constant angular speed.

## Shooting
- [ ] No projectile/weapon system exists anywhere in the engine yet - this is new territory, not a port of something else.
- [ ] Decide: raycast hitscan vs. a physics projectile object. A projectile fits "flashy effects" (tracer, travel time) better than hitscan.
- [ ] Muzzle flash + tracer + impact effects. We have `ParticleEmitter` (used for `ShipCharacter`'s exhaust/laser) - look there first before inventing a new effect system.
- [ ] Fire from the turret's world position/forward, not the hull's - reuse `turret->GetWorldForward(STATE_ACCESS_PHYSICS)` like the aiming code does.
- [ ] Cooldown / rate of fire on `TankCharacter`.
- [ ] Ammo count, tied into the powerup pickup below.

## Terrain
- [ ] No terrain class uses reactphysics3d's heightfield collision shape yet - `IsoTerrain` is grid/tile based (isometric city building), not a driveable heightfield, so it's not a fit to reuse directly.
- [ ] Needs a new terrain class (heightmap -> mesh + `rp3d::HeightFieldShape`), separate from `IsoTerrain`.
- [ ] Figure out heightmap source: authored texture, procedural (we already have `RRandom` for noise/randomness elsewhere), or hand-sculpted in Blender and exported.
- [ ] Tank hull physics currently just clears `.y` to 0 in `TankCharacter::UpdatePhysicsState` (flat-ground assumption) - once real terrain exists this needs to sample ground height/normal instead, and probably tilt the hull to match slope.

## Powerups (ammo, etc.)
- [ ] Simple trigger-volume pickup, similar in spirit to `IsoCar`'s proximity trigger colliders (`COLLISION_CATEGORY_CAR_PROXIMITY`) and `ApplicationTileset::onTrigger` - but powerups should destroy/hide themselves on pickup rather than just flag proximity.
- [ ] Needs its own collision category bits so it doesn't get confused with hull/turret/projectile collisions.
- [ ] Respawn timer vs. one-shot pickup - decide per powerup type.

## Small props (plants, etc.)
- [ ] Should react to the tank driving over them - squish (scale/hide) or get shoved aside, not block movement.
- [ ] Could be a trigger-only collider (no physical blocking) that reacts on overlap the same way the powerup trigger does, just with a different response (squish/move instead of pickup).
- [ ] "Squish" could be as simple as a scale-down + fade, or knockback via `MoveBy`/physics impulse if it should look shoved rather than crushed.

## Big props (walls, large trees, etc.)
- [ ] Plain solid obstacles - static physics body, blocking collider, same shape as `IsoHouse`'s placement in `ApplicationTileset::PlaceHouse` (`AddBoxCollider` + `SetStatic(true)`).
- [ ] No special reaction needed, just needs to actually stop the tank (and probably projectiles) rather than clip through.
