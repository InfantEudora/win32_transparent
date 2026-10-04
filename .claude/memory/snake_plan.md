---
name: snake-plan
description: "archer snakes - decisions 2026-10-04 (drop from branches, 1-3 arrows, knock-off, fear overhead) and step 1 BUILT (taller tree x 80, crown 20.2, StageSnakeBranch); plan in apps/archer/docs/creature_plan.md section 4"
metadata:
  node_type: memory
  type: project
  originSessionId: a628fe61-5f4a-4d8b-a5c2-d9ab8076281f
  modified: 2026-10-04T10:28:31.174Z
---

Snakes for archer, apps/archer/docs/creature_plan.md section 4 (see [[archer-app]], [[spider-leg-shape-keys]]).

Decided with the user 2026-10-04: snakes are GAMEPLAY (rules, `Stage`), live on branches in front
of / behind her line and DROP on her where a branch crosses z 0; killed by 1-3 arrows of any kind
(hp 1-3, size follows hp); a hit on a branch KNOCKS IT OFF; a snake overhead raises her FEAR first.
Bamboo arrow pinning creatures (even from below) / blocking their path: later, not now. Branches
are snake-only: she cannot stand on or climb them, they collide with nothing.

Step 1 BUILT 2026-10-04: blockout tree x 80 now 20.2 tall, arms 2.5R 5.0L 7.5R 12.7R 15.2L 17.7R,
crown = top_width 9 at 20.2; `StageSnakeBranch` (v3 points, radius 0.09) in Stage::snake_branches,
5 branches (3 cross her line: x 87.8 over high slab, 73.4 over low slab, 82 over crown; 2 perches
behind); app draws them as bark boxes; stage_test TestTree climbs to the crown + TestSnakeBranches.
archer_test baselines rewritten (world/physics changed, `her` + sounds same).

**Why:** the user wants blockout-first, rules-owned creatures that replay bit-exact.

Step 2 BUILT 2026-10-04: `StageSnakePath` (rules-own polyline: points/ups/dist, At(s)), ONE ROUTE PER
BRANCH tip -> branch top -> helix down the trunk to the ground (no graph/forks); Stage::BuildSnakePaths;
magenta in the path view; TestSnakePaths. The helix must start at the branch TOP's height or the
joint has a vertical step the up lies along. archer_test NOT rewritten: the terrain session's slope
blocks moved world/bodies (her + sounds same) - that baseline is theirs to rewrite.

Step 3 BUILT 2026-10-04: StageSnake (4 snakes, hp 2/1/3/1 on branches 0-3), PATROL only: runs/pauses,
glide (accel), tip..trunk_reach; a TURN SWAPS THE HEAD END (body stays) - blockout simplification;
TickSnakes in Stage::Tick, hash group `snakes`, Reset, recording state "snakes" (11 fields);
SyncSnakes spheres (visual-only, plant_objects, swapped in SwapLevel); TestSnakes. Replay-vs-replay
proved by setting baselines aside, --write, re-run, restore.

**How to apply:** next is step 4 (WATCH/DROP/LANDED), then arrows (hit knocks off a branch), GROUND/CLIMB. Arrows hit only within SNAKE_ON_LINE_Z of z 0 (the
apples' rule). Steps listed at the end of section 4.
