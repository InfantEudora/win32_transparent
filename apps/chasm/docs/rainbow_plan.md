# Rainbows in the falls' spray

Agreed with the user 2026-10-23, and **step 1 built the same day** - see "As built" at the end. The falls have spray drifting off their lower third and foam churning where they
meet the mist (`waterfall_plan.md`); the sun should make a rainbow in it, the way a real gorge does when the sun is
at your back.

**All of it is view**, like the water and the weather: a function of the sun, the camera and where the spray is.
Nothing saved or hashed, replays untouched, the rules never ask.

## The idea: light in the spray, not an object

A rainbow is not a thing at a place. It is the light that water drops send back at about 42 degrees from the
point opposite the sun (the ANTISOLAR point), so it moves with the eye: walk along a fall and it slides through the
spray with you. Built as an arc mesh at each fall it would look like a sculpture the moment the camera orbits, and
would not answer the sun at all.

So it is drawn the way it happens: a small volume over each fall's spray, coloured by the angle between the view
ray and the antisolar direction. Where that angle is near 42 degrees and the ray passes through spray, the pixel
gets the bow's colour; everywhere else the volume adds nothing.

- **Primary bow**: 40.6 degrees (violet, inside) to 42.4 (red, outside).
- **Secondary bow**: 50.4 (red, inside) to 53.6 (violet, outside), the colours reversed, a good deal fainter.
- **Between them** (Alexander's dark band) the spray sends back less light than inside the primary, so the inside
  of the primary is a little brighter. Drawn as a faint white glow inside the primary and nothing between.
- **Tinted by the sun** - `sun->color` and `sun->brightness` - so a storm, which already dims the sun
  (`UpdateSun`), dims the bow with it, and winter's cold sun makes a colder one.
- **Added, not blended over**: a rainbow is extra light, and it shows best against the dark shaded gorge.

## Why it will be seen in Chasm

Measured on paper with Chasm's own numbers (2026-10-23): the sun stands in the north-west all year (azimuth 305)
at 24 degrees (winter) to 72 (summer); the play camera has a 40 degree field of view and looks 34-77 degrees down.
Looking down into a gorge with the sun behind you is exactly when real waterfalls show their bows, below you.

At 55 degrees down, the 42 degree ring crosses the screen in 5 to 8 of the 8 compass headings, depending on the
season; it is missing only facing west or north-west, into the sun. It passes through the middle of the screen in
winter facing east or south-east at 50-69 degrees down, in autumn facing south at any pitch, and in summer almost
anywhere. So a bow placed by the physics, with no cheating, will be seen, and it slides through the spray as the
player turns the view.

## What is there already

- **The spray's place**: `WaterMeshData::shapes`, one `WaterSheetShape` per sheet drawn (falls and springs), its
  `foot` the fall the foam and spray are posed from (`Mist.cpp`, `FoamBall`, `SprayBall`): spray from the sheet's
  face out to about 3 units in front of it, from the mist's top (`CHASM_MIST_TOP`) up to about 18 over it, most of it
  low; foam out to about 5.5, up to 7.
- **A volume on a box**: the custom-shader pass, as bomber's blast (`bomber_explosion.frag`) and the ship's clouds
  use it - a box tagged `MESH_MODE_SHADER`, one instance per volume, the ray's march clamped to the G-buffer so the
  ground and the mist's puffs cut it off.
- **The sun's shadow** for a custom shader: `Shader::f_lit` hands over the shadow matrix, and `lighting.glsl`'s
  `CalcShadow` answers "is this point in the sun".
- **Frozen falls**: `CalendarSnowAt(shape.origin, SnowFrontNow())`, as `UpdateMist` asks before posing foam.

What is missing in core is small: `CustomShaderSubPass` puts back the cull face, the depth mask and the depth test
after each custom shader, but not the blend function, so a shader that switches to adding would leave every later
one adding. It now puts back the blend function too.

## Steps

1. **The bow in the spray.** One box per sheet over its spray, a soft cloud in it marched in a few steps and
   clamped to the G-buffer; the colour from the angle - primary, secondary, the glow inside; the sun's colour and
   brightness; added to the frame (core: the blend function put back after each custom sub-pass). No bow for a
   frozen fall, none with the water's view off. Its tuning as named constants, with a Rainbow section in the
   Weather panel to try them live.
2. **Only sunlit spray.** `f_lit` and `CalcShadow` along the march: where the chasm's wall shades the foot, no bow,
   and the wall's shadow line cuts it. Summer's high sun then gives the most of it.
3. **Life.** The cloud breathes on `SimSeconds` - a slow noise in its density - so the bow shimmers as spray does
   and holds still in a pause. A view inside a spray box (low over a balcony beside a fall) still draws it.

### Later

- Rain, when there is rain: a bow over the whole chasm's mist after a shower.
- A fogbow - the same angle, white - on the mist blanket itself.

## As built

**Step 1, 2026-10-23.** `ApplicationChasmRainbow.cpp` and `assets/shaders/chasm_rainbow.frag`; built with the water
(`BuildWaterScene`) and posed with the foam (`UpdateMist`), so nothing in `ApplicationChasm.cpp` changed.

- **The box**: one per `WaterSheetShape`, `RAINBOW_ACROSS` (1.8) of the fall's widths across, from `RAINBOW_GAP`
  (0.3) off the sheet's face at the foot to `RAINBOW_OUT` (9) out, from `RAINBOW_BELOW` (3) under the mist's top to
  `RAINBOW_ABOVE` (20) over it. The cloud in it is thickest across the middle, just off the sheet and low; 12 steps.
- **The look**: the primary, the secondary at `RAINBOW_SECONDARY` (0.4), a white glow inside the primary
  (`RAINBOW_INNER`, 0.12); `RAINBOW_STRENGTH` 0.45 and `RAINBOW_WIDTH` 2 - first tried at 0.25 and 1.5, real-bow
  thin, and too faint to find at play distance.
- **The knobs**: a Rainbow section in the Time panel between Sun and Weather - strength, width, and three views (the
  bow; the spray's cloud in white; the bow as if all the box were spray, which shows where the ring crosses it).
  `chasm_time`'s `weather` `{rainbow: {strength, width, debug}}` sets the same, and its status lists each box with
  the view ray's angle at its middle - 41.5 there is the bow through the spray's heart.
- **Core**: `Renderer::CustomShaderSubPass` puts the blend function back after each custom program, read before its
  callback. The contract's comment in `Renderer.h` still names only the cull face, depth mask and depth test; it was
  left so as not to rebuild every app while another agent was building, and wants the blend function added the next
  time that header is touched.
- **Found on the way**: the sub-pass reset sets `glCullFace(GL_BACK)` but never enables culling again, so after the
  falls' sheets turn it off every later custom program in Chasm draws both faces. Left alone - turning it back on
  would change how those draw. The bow does not depend on it: it keeps one fragment a pixel by where the face lies on
  the ray (the face the ray comes in by, or from inside the box the one it goes out by).

**Checked** on seed 12 (four falls, spring sun, 52 degrees up): views solved to put the 41.5 degree cone through a
box's middle (the target stays at the plateau's height over the chasm) - the bands in the right order, red outside;
the bow rising out of the foam in front of the sheet; from a steep view with the fall hidden under the rim, the bow
still showing out of the gorge. Frozen falls drop theirs: at full snow cover two of the four, the two the front
reaches. Cost: the custom pass 53.6 us without, 75.3 us with (twice each), the colour pass unchanged; no GL errors or
shader warnings.
