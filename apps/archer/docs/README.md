# Archer design notes

The plans in this folder each cover one mechanic. This page holds the rules that cut across all of
them, then indexes the plans.

---

## Gameplay is 2D; the world is 3D

**Her movement, collisions and arrows live in one plane** (z = 0, the walk line). The rules
(`Stage`) are a 2D platformer, which is what keeps them simple, deterministic and testable in
`make rules`.

**Everything else is free to use depth, and should.** The camera is a perspective camera, not an
orthographic or isometric one. So depth reads clearly on screen: a prop in front of her walk line,
behind it, or crossing it at an angle is plainly in front, behind or diagonal. That means:

- **Props and set pieces do not have to sit flat in her plane.** A frame, a web, a fallen log or
  a doorway can stand at an angle across the walk line, with one end in front of her and one
  behind, and she passes through the point where it crosses her plane.
- **She can walk in front of or behind things** that only look like they're in the way. Trunks,
  rocks and dressing already stand behind her line (`STAGE_TREE_Z`, `z_front_max`).
- **The rules still see only the plane.** A 3D-placed object's gameplay is its cut through
  z = 0: the line, point or band where it crosses her plane. The rules declare it in those terms;
  the app draws the full 3D object from the same numbers.
- **Behaviour can be 3D too, as long as it stays deterministic.** A web can bulge out of the plane
  when she pushes it, and a bridge can sway in depth. Mostly that's looks (the bridge's sway is
  looks-only), but it may feed back into the plane where that's the point.

**When a mechanic is planned flat in her plane only because the rules are 2D, question it.** A
flat, in-plane object usually looks like a wall. The web's first blockout is the example that
prompted this note (`web_plan.md`).

---

## The plans

| Plan | What |
|---|---|
| `animation_plan.md` | her clips, the Puppet seam |
| `apple_plan.md` | apples to shoot down, pick and roll |
| `bow_plan.md` | the bow, aiming, arrow kinds |
| `bridge_crumble_plan.md` | rope bridges, snapping bridges, crumbling rock |
| `cave_plan.md` | the cave, its lighting and biome |
| `creature_plan.md` | spiders, snakes, creature paths |
| `cue_plan.md` | the sound/event layer and replay baselines |
| `plant_mechanics_plan.md` | climbable tree, spring plants, thin branch |
| `strawman_plan.md` | the kicking dummy |
| `terrain_plan.md` | marching-cubes terrain from the blockout |
| `vine_plan.md` | vines, growth, the rope |
| `vitals_plan.md` | exertion, fear, breathing, heartbeat |
| `water_plan.md` | the waterfall |
| `web_plan.md` | the breakable spider web |
| `wind_plan.md` | the wind field and what it moves |
