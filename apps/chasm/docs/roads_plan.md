# Roads, a walker, and gates

Step 10 on the building side (the generated chasm is a separate step, by another window, in
`grid_plan.md`). Talked through with the user 2026-10-05: **roads are painted on plots like
everything else**, and **the first walker is a debug walker** - one figure walking between two
plots by A*, to prove pathfinding and replay before there is any economy for it to serve. Gates come
after, because a gate is where a road meets a wall.

---

## Roads

**A road is a kind of ground**, `ZONE_GROUND_ROAD`, beside garden and town. So it is painted, erased,
saved and replayed by what is already there - `ground_paint` with the kind, the ground list in a
save - and needs no new command. It keeps the ground rules (flat all round, not wet, no field
touching) and adds two: no house on a road, no road under a house. A garden or town is the
*inside* of a boundary and a road is not, so a road beside a garden is outside its wall - which is
what lets a road run up to a wall and make a gate there (below).

**Drawn as a network, not as plots.** A road plot painted the way a garden is would be a blob, and a
line of them a string of blobs. So a road is drawn along the grid's fine EDGES, about a metre wide,
from a road plot's vertex toward each neighbouring road plot's. (First built as straight strips, each
cell drawing its own side; that zig-zagged along every staircase of edges, so it became curves - see
"As built".)

**Forest gives way** to a road and the verge either side of it.

## The walker

**Simulation state** (`Walkers.h`), the physics thread's alone, changed only by commands and the tick.
A walker has a home plot and a goal plot and walks between them, back and forth for ever, which is
all a debug walker needs and keeps one on screen while roads are painted under it.

- **The graph is the plots**: a fine vertex to each one a fine edge joins it to. An edge is closed
  across a cliff (the two ends on different levels), beside a river (either end wet), through a
  house (a house plot is only ever a walk's first or last), and **through a wall or palisade** -
  which is what a gate will open.
- **Cost** is the edge's length over the speed it is walked at: fastest on a road (both ends road),
  slower over open ground, slowest through a field. A* with the straight-line distance at road
  speed as its estimate, so it stays admissible; ties broken by plot index, so the path is the same
  every run.
- **Movement** is in ticks: one tick's worth of distance at the edge's speed, carrying any leftover
  into the next edge, so a walker's place is a pure function of the ticks it has walked.
- **When the zones change** every walker plans again from the plot it is walking to - a road painted
  in front of it is taken, a house painted on its path is walked round. With no path it waits where
  it is and tries again at the next change.
- **Saved** with its path, not re-planned on load: A* from a later point on a path may find a
  different path of equal cost, and a replay has to be the same walk.
- In the **state hash**, the trace's `walkers` part.

**Commands**: `CHASM_CMD_WALKER` - spawn (home, goal), clear all. Recorded like the zone commands.

**The view**: a small figure per walker, and in a debug build its path drawn as a line.

## Gates

- **In a wall**: where a road ENDS against a garden wall or a palisade - the road plot has one road
  neighbour and the enclosed plot lies ahead of it - the wall opens. The walker's graph opens that
  edge too, so a gate is the only way into a walled garden.
- **Under houses** (Townscaper's arch): a road plot between tall houses is bridged by their upper
  storeys, over a passage.

---

## Status

| Part | State |
|---|---|
| Roads | BUILT 2026-10-05 - see below |
| Walker | BUILT 2026-10-05 |
| Gates in walls | BUILT 2026-10-05 |
| Arches under houses | BUILT 2026-10-05 |

### As built

Files: `Zones.*` (the road kind and every rule below), `RoadMesh.*` (drawing), `Walkers.*` (the
walker's rules), `BuildingMesh.cpp` (arches), `BoundaryMesh.cpp` (gates), `ApplicationChasmRoads.cpp`
(the walker's commands, tick, hash, figure, panel and tools). Keys: **6** road tool, **7** walker tool
(click a home, then a goal; shift-click clears them all).

- **Roads curve.** A straight line across this grid is a staircase of edges, and drawn edge by edge it
  zig-zags. So a road vertex with two road edges is drawn as one quadratic Bezier from the first
  edge's midpoint to the second's, the vertex as its control point: straight through it is straight,
  at a corner it rounds it. One road edge (a dead end) or three and more (a junction): straight
  spokes to the midpoints and a round joint. Every piece ends at a midpoint square to its edge, so
  the pieces two vertices draw meet without a seam. A vertex is drawn by one cell only - its first
  plot quad - so a road across a chunk border is neither doubled nor lost.
- **The drag bridges diagonals.** A drag can step from one plot to the one diagonally across a cell,
  which a road (along edges) cannot join; the tool paints the corner between as well. `chasm_road`
  (MCP) lays a road along a line the same way, and reports the gates and arches on it.
- **Roads stop at gardens and towns** ("a garden or town is there"): a road dragged into a walled
  garden ends against its wall, which is where its gate opens. Erase the ground to run a road
  through it.
- **The forest clears a verge**: no prop's foot within 1.3 of a road's centre line (`RoadNear`, the
  same centre line the mesh draws), so trees neither stand on a road nor lean over it.
- **Painting reaches further than a plot.** A road vertex is drawn from its neighbours, a gate from
  where the road ends, an arch from the run it is in, so a ground or house change touches three rings
  of plots (`touch_road` in `Zones::Apply`), not one.
- **The walker** walks the plots by A* as planned, and is drawn through each plot on the same curve a
  road takes, so it rounds corners and keeps to a bending road; that is view only. Published every
  tick there is one. The figure is about two thirds of a storey, red-tunicked to be found at a glance.
  Its path is a cyan line in a debug build, red when it is stuck.
- **Gates**: `ZoneGateOf` - a dead-end road plot opens into the enclosed neighbour most nearly
  straight ahead, within about 70 degrees, one at most. The wall stops 0.62 short of the edge's
  midpoint on both cells' sides; a garden gate has two stone pillars, a palisade gate two tall posts
  and a crossbeam. The road runs on into the gateway (the gate counts as one of its edges, so it
  curves in) and one plot inside as a path. A walker crosses a wall at its gate and nowhere else.
- **Arches**: `ZoneArchStoreys` - a road plot with two road neighbours and every other neighbour a
  house of two storeys or more is a candidate; it is built over when the run of candidates it is in
  is at most THREE long. Two would have been Townscaper's hole through a row, but a road crossing a
  row steps along the grid's edges, so even a row two houses deep takes three plots; a longer run
  is a street, and covering it would make a tunnel. The house builder treats an arch as a house solid
  from one storey up to the lower of its neighbours: the roof runs straight across, a ceiling closes
  the passage, the houses either side get their ground-floor wall into it, always with a door.
- **Replay**: the walkers are in the save (paths and all) and in the trace's `walkers` part; the
  replay test (`tools/chasm_replay_test.py`) now paints a road, a walled garden with a lane to its
  gate, and three walkers - one living in the garden - and passes in debug and release.
- **Measured**: a short path plans in a few milliseconds.

**Open:**
- A* that FAILS floods everything reachable - tens of milliseconds on the plateau - and every stuck
  walker re-plans on every zone change. Fine for debug walkers; a real one wants a search budget or
  a connectivity map.
- No bridges, so rivers cut walkers off.
- The arch's passage is a rectangle; Townscaper's is round-topped.
- A gate has no doors.
- The road's colour is the town ground's (no free palette column).
- The walker figure moves at the tick rate: no interpolation between ticks.
