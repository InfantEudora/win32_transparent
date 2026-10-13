# Waterfalls: one surface from the river to the mist

Agreed with the user 2026-10-12. The river and its fall were drawn as two things with a gap between
them (the user's screenshot of seed 12): the river stopped on the plot outlines, the sheet started on the
layout's smooth rim line somewhere else, narrower, with streaks of another size, speed and direction, and
a flat light band at its top that read as an edge. Requirement: **no visible seam**; they may stay
separate meshes.

## Why it looked like two things

- **Where the edge is.** The river's surface (`WaterMesh.cpp`) is laid over the plateau part of each cell,
  so it ends in a staircase at plot size. The sheet started at `TerrainFall::lip`, the river's centre line
  crossing the layout's rim LINE - up to a cell either side of the real, jagged rim. The sheet's top
  floated out past the water in places and dived under it in others; the wall's notch showed between.
- **How wide.** The sheet was 92% of the river's width and `SheetEdgeCut` took up to 28% more off each
  side, right up to the lip: 66-92% of the river, grey bank showing either side of its top.
- **The pattern.** The river's UVs are across and along in world units, flowing 2.2 units a second; the
  sheet's were across and 0..1 down to the mist, flowing 0.32 sheet lengths a second. Different streaks
  either side of the joint, and the top 3.5% a flat light band.
- **Shape.** One quad across, four rows down, a flat normal per triangle: every bend a step in shade.

## Stage 1: one surface (agreed)

- **A BRINK per fall**: a straight line along the rim, just past the furthest rock under the water. It is
  as wide as the water there - out to where the bank's ground is surely above the surface, so the river's
  ordinary water beside it is hidden under the bank.
- **The river runs to the brink.** Near a fall the water is no longer laid on the plot outlines but out to
  the brink, over the drop where the jagged rim falls short of it - as water overhangs a real lip. Nothing
  pokes through: the brink is past every rock in its width.
- **The sheet is welded on**: its top row is exactly the points the river's polygons end on along the
  brink - same positions, same UVs, same (upward) normal - so there is no gap and no T-junction. More rows
  in the curl, smooth normals down the fall so the curl rolls instead of banding, the curl a ballistic arc
  from the brink, and pushed further out wherever a lower column would stand in its way.
- **One pattern across the joint.** The sheet's uv.x is the river's across, carried down; its uv.y carries
  on the river's along in FLOW TIME - the river's along at the brink plus 2.2 x the seconds the water takes
  to get there, falling faster and faster. The shader draws the river's streaks on both, so they pour over
  the edge unbroken and stretch out as the water speeds up. The light at the curl comes from the normals;
  the foam at the foot from world height, not uv.y.
- **Ragged edges grow with the drop**: none at the brink, full a third of the way down.
- **Springs** get the new sheet, still starting inside their mouth, which hides its top.

View only: no rules, no hashes, nothing saved. `WaterMesh.cpp/.h`, `chasm_water.glsl`,
`ApplicationChasmWater.cpp`.

## Stage 2: upgrades (agreed, after stage 1)

- **Bowed curtain**: the middle stands further out than the edges, so it reads as a mass of water.
- **Strands**: lower down, the edges and a few holes break into strands moving with the flow.
- **Wet rock**: the wall behind and beside the fall a shade darker and cooler.
- **A second, inner layer**: narrower, a little behind, streaks out of step - depth for a few dozen
  triangles.
- **Spray**: a thin drift of light puffs up the lower third, besides the foot's foam; a short white churn
  where the brink's ends meet the bank rock.

## As built (2026-10-12)

Both stages. View only: the grid hash, the rules and saves are untouched; `chasm_check` passes on seed 12.

- **The brink** (`WaterMesh.cpp`, `MakeBrink`): its ends where the water's edge distance passes
  `BRINK_DRY_EDGE` (0.6, past which the bank's ground, bump and all, is above the surface), measured on
  lines from `BRINK_BACK` (6) upstream out to the brink itself; its line `BRINK_ROCK_CLEAR` (1.2) past the
  outline of every plateau plot between them - the walls' turf and strata stand out up to 1.14.
- **The river near a fall**: within `BRINK_BESIDE` (4) of the brink's ends and from `BRINK_BACK` upstream
  on, the river's own pieces are left out - beside the brink the bank covers them, and laid to the plot
  outlines they stood out of the cliff face as thin blue slivers wherever the wall under the turf sits
  back. Between the ends the APRON lays every cell's every piece (whole quad, or the four quarters the
  terrain stands up) clipped to the strip, out to the brink line. Clipping works a crossing from an
  edge's ends in a fixed order, so neighbouring pieces, and the sheet, share points to the bit.
- **The sheet** (`Sheet`): its top row is the apron's points on the line - the river's positions, UVs and
  upward normal. 14 rows: a thrown arc near the brink (`FALL_THROW`) easing to `FALL_CURL` (2.6) out,
  then straight into the mist; any row whose distance out falls inside a column's span below is pushed
  past it (`FindRocks`) - on seeds 1, 2, 3, 5, 7, 12, 39 and 144 none was. Normals from the grid itself.
  uv.y is the river's along plus 2.2 x the seconds to get there at `FALL_GRAVITY` (6), so the streaks
  are the river's and stretch about tenfold by the foot.
- **Stage 2**: the bow (`FALL_BOW`, 18% of the half-width); the inner layer (`FALL_INNER_*`: from 0.8
  down, 0.7 nearer the wall, 80% wide, streaks 23.7 along out of step); strands - gaps opening from 12
  down, `SheetCut` in `chasm_water.glsl`; churn at the brink's ends over its first few units of drop
  (the brink itself stays the river's colour, so the joint shows nothing); wet rock in
  `chasm_ground.frag` (`Wetness`, up to `WATER_MAX_SHEETS` 12, from `WaterMeshData::shapes`); spray -
  `SprayBall`, 14 a sheet, on the foam's set.
- **Springs** get the same sheet from a lead row at the back of the mouth; their brink is the front of
  the mouth's floor, found by the same rule as a fall's.
- **Found on the way** (`TerrainMesh.cpp`, `Fan`): a balcony 8 out in front of seed 12's fall was drawn
  as a pale slab of river bank - a river's line runs on 10 past its lip, and the bank colour asked only
  how far the channel would lower the ground there, not whether the ground was the plateau it cuts.
  Now plateau only.
- **Measured** (debug, seed 12): water 7,386 river and 2,922 fall vertices, built in 26 ms (26-100 ms on the
  seeds above, once a map). The wet rock's loop in the ground shader is within the GPU timers' noise:
  colour pass 1,613-1,623 us with it against 1,576-1,587 without, and the other way round on a
  second try.

**Open:**
- The camera never goes below the rim, so the foot is only ever seen from high over the chasm.
- A sheet that passes just behind a balcony or a broken column goes on down behind it; landing on it
  with a splash (`rivers_plan.md`'s open item for springs) would read better.
- Spray uses the foam's opaque puffs; small ones up the sheet can read as snowballs.
