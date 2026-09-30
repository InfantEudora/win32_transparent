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

## The biome (2026-09-30)

A **biome** is a box of the level with its own dressing and air rules: `StageBiome` in
`Stage.h`, declared in `BuildMainLevel` the way zones are, but looks only. Outside every box it
is `BIOME_JUNGLE`, the level as it was. The cave's box is its inside (x -66..-40, y 0..9). It
fades into the jungle over the 6 units inside the mouth (`fade_right`), so the grass thins going
in rather than stopping on a line. `BiomeAt(biomes, x, y, &weight)` is the one lookup.

What each kind means is a row in each reader:

| reader | the cave |
|---|---|
| `FoliageBiomeFor` | no grass, no flowers; low ferns as sparse ground cover (open 0.15x, corners 0.5x), hardly a tall fern (15% kept) |
| `BoulderBiomeFor` | every corner gets its cluster; rubble strewn along the open floor, 0.6 a unit, 12% of it big, as if come down from the roof |
| `WindBlocks` | **still air**: the box is solid to the wind, so the flow goes over the roof and into the bay behind it. Leaves, streaks and fireflies steer by the same field, so they keep out |

In the jungle the multipliers are 1 and the extra draws are never made. `cave_test` checks that
every plant and rock outside the cave is exactly what it was, as well as the cave's own rules,
the still air (0.64 through the cave before, 0 now), and no jet over the roof.

The wind arrives over the far wall already at roof height, not rising from the ground: the wind
carries the level's end profile outward (Wind.h), so there is no open ground left of the cave
to rise from.

**Found, not changed:** two rock clusters in the bay at x -31 overlap. Clusters are never
checked against each other; it predates the cave.

## Props and ideas for the cave

What would make it read as a cave, roughly in order of what it costs:

- **Its own terrain materials.** The floor is the bay's grass-capped terrain. A cave floor wants
  bare rock and damp soil, with moss only at the mouth. Meshing the cave's blocks into a terrain
  object of their own (the bays' region split three ways) gives it its own three slots. The
  same move fixes the hazed back wall.
- **Stalactites under the roof, stalagmites under them.** Placement like the boulders: along an
  underside, hashed, with a clear-headroom rule over her walking line. The roof is already there
  to hang them from.
- **Drips.** A drip from a stalactite tip every few seconds: a falling speck, a ring where it
  lands in the stream, a soft plink cue with reverb. Cheap, and it sells stillness better than
  anything.
- **Glow.** Mushrooms or glow-moss as the cave's own lights: a foliage kind with emission, a few
  point lights grouped like the fireflies'. A natural first subject for the lighting pass.
- **The stream ends in a pool** under the far wall, with Bomber's Worley caustics thrown onto the
  roof above it as moving light.
- **Sound.** An `ambience` loop inside (low drone, water) that crossfades with the waterfall
  through the mouth. The waterfall loop's gain curve could dip behind the roof.
- **Bats or moths**, disturbed as she passes - the leaves' system with a flee rule.
- **Cobwebs and roots** in the upper corners; roots through the roof are already possible with
  the vine arrow's root growth.

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
