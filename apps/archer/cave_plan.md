# The cave

Left of the terrain bay, through a mouth where the bay's left-hand wall used to be. Built
2026-09-29 as the place to experiment with lighting: enclosed on every side but the camera's.

## What it is

All in `Stage::BuildMainLevel`, under `ARCHER_TEST_BAY`. The constants are `ARCHER_CAVE_X_MIN`
(-66) and `ARCHER_CAVE_ROOF_Y` (9) in `Stage.h`.

| piece | where | notes |
|---|---|---|
| mouth lip | x -40.6..-39.4, y 6.0..9.2 | the old left wall's block, same index. Its underside clears her head at the top of a jump, taking off from the bay's mound (5.8) |
| floor | x -66..-40, top 0 | its own block, appended last. The bay's floor is untouched, so the bay's bank, notch, pines and hand-laid vines are exactly as they were |
| roof | x -66..-40, y 9..11 | deep, z -6..1.5, so it runs back into the bank |
| far wall | x -66..-64, y 0..11 | as deep; the wind leaves it out as the level's end wall |
| zone "Cave" | x -68..-40 | arrive at (-44, 0.3) |

- The terrain regions start at `ARCHER_CAVE_X_MIN`, so the cave melts with the bay. Floor and
  far wall mesh with the ground; roof and lip with the island, above the split.
- **The bank closes it.** A block that reaches back into the bank (`Back()` at or behind the
  wall's face) raises the wall behind it to `roof_rise` over its top. Nothing else in the level is
  that deep, so nothing else moved.
- **The stream runs in**, to x -62. The water is laid out from its own ground, so the ridges on
  the cave floor's bank step back for it too.
- `stage_test`'s `TestCave`: pieces in place, the floors meet, she walks and jumps in without a
  bonk, the far wall stops her, the zone names it, the bank stands over the roof all along.

## For the lighting

- The sun is nearly overhead, leaning left, so the roof shades the floor. **The cool fill light
  from the camera side casts no shadow**, so it still lights the inside. It is the first thing
  to decide about.
- The back wall is the bank, in its HAZED materials (`*_back`), tinted toward the painted
  backdrop so it recedes. Inside the cave that reads as distance, not a wall a few units away.
  A per-region material, or a darker bank behind the cave, is the likely answer.
- Zones already exist for this (`StageZone`, cue_plan section 8). Entering "Cave" is the
  natural trigger for a lighting change.
- Ideas from water_plan.md: Bomber's Worley caustic net as light on the roof, off the stream;
  fireflies or glow plants as the cave's own lights; a shaft of light down through the mouth.
