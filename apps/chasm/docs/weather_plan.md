# Weather: winter rolls in

Agreed with the user 2026-10-18, and **all six steps built the same day** - see "As built" at the end for what
changed on the way, what was checked and what it costs. Winter should arrive with wind and snow, seen and heard: the trees, static today,
sway a little in autumn; the snow's front brings a gust you can see go through them; snow falls over the screen,
lightly at first, densest as the front comes into view, then dies out to a still, serene winter. The score's `gust`
stinger goes with it. Rain should be able to use the same machinery later.

**All of it is view**, like the snow and the seasons (`seasons_plan.md`): a function of the calendar's tick and of
where the camera looks. Nothing saved or hashed, replays untouched, the rules never ask.

## Decided (the user, 2026-10-18)

- **Snow falls wherever the front moves** - autumn's last two days included, when it edges into the far north - so
  snow never lands with nothing falling.
- **Only near the front.** No faint snowfall elsewhere: a colony the front never reaches sees none.
- **The stinger plays once per storm**, as the front reaches the view.
- **In a pause the weather stands still** - the flakes hang in the air, the trees hold their bend - as the water and
  the mist do (the user's, after trying it, 2026-10-18: "clearer"). It was first built to go on in a pause, from a
  wrong claim of the plan's that the water and the mist do; they hold still, on `SimSeconds`.
- **All four suggestions** are in: the chimney smoke (A), the spindrift (B), the score following the storm (C), the
  light dimming under it (D). Autumn leaves and spring rain stay "later".

## What is there already

- **The front** (`Calendar.h`): edges into the far north over autumn's days 8-10 (to a quarter of its reach), sweeps
  south over winter's days 0-4 and holds there. That is about 400 units in 180 s at 1x - 2 to 3 units a second, so a
  play-zoom view (50-70 units deep) is crossed in about half a minute, under ten seconds at 4x.
- **Tree sway, in core**, built for archer: `material_t::wind_flex` and `wind_mode`, `Renderer::SetWindField` (a grid
  of velocities in an SSBO, its mapping in the header), `WindBend` in `default.vert`. Chasm's forest is drawn through
  `default.vert` (the ground shader's program), and so is the sun's shadow map, so a bent tree's shadow bends too.
  **But the field lives in archer's play plane, X/Y** - `SampleWind(world.xy)`, pushed along x. Chasm's ground is X/Z.
- **`INSTANCE_MOTION_FALL`** in core, "rain or snow" - unused by any app.
- **The `gust` stinger** is in the score (`assets/music/chasm.json`, `stingers`), not in the cue table. The cue table
  sends a stinger with its `music` action, as `chasm_reveal` sends `reveal`. A stinger plays the moment it is posted
  (`MusicEngine.h`), so it can start on the same pass as the gust it goes with.
- **The score's winter** already has a `snow wind` bed, and a `cold strings` bed heard only toward tense. Nothing in
  the game moves the score's suspense yet (`ApplicationChasmMusic.cpp`).

### What comes from archer's wind, and what does not

Archer's wind (`apps/archer/docs/wind_plan.md`) is two layers, and only one of them means anything seen from above.

- **Reused as it is: the sway, in core.** The bend by height squared, the swing about the root (`WindSwing`), the
  flutter phased by where each plant stands, the shadow bending with it, and nothing to pay with no field set. Chasm
  uses it unchanged but for the plane (piece 1).
- **Not reused: the model, `apps/archer/Wind.{h,cpp}`.** Its stream function is potential flow over and round the
  level's blocks, with eddies shed in the lee of cliff corners. All of that happens in the vertical plane, which a
  view from above looks straight down through, and it is built from archer's `StageBlock`s.
- **Copied, turned onto the ground: its gusts and waves.** `WindField::GustFactor` - fronts travelling with the wind,
  a sharp rise and a long tail, scheduled from the tick by a hash - and the few slow sine waves drifting with the
  wind (`Waves`). About forty lines, and the feel wanted at the front. Copied rather than shared: two apps for now.
  If a third wants them, they move into core.
- **Later**: archer's drifting leaves (`Leaves.cpp`) are the pattern for autumn leaves.

## The weather, as one function

`WeatherAt(tick, view)` - a small engine-free file like `Calendar.cpp`, with a test - gives everything below one
number to agree on: the **storm** at the view, 0..1, plus the wind's direction and any gust running.

    storm = active(tick) x near(d)          d = the view's z - the front's z at the view's x

- **near(d)**: 0 far ahead of the front (WEATHER_AHEAD, ~150 units south of it), rising slowly as it comes, 1 while
  the front is in view (within ~25 either side), falling back to 0 by WEATHER_BEHIND (~100 units into the snow).
  This is "starts slowly, densest when the front is on screen, then dies out" - for whatever the camera looks at.
- **active(tick)**: 1 while the front moves - autumn's days 8-10 and winter's days 0-4, through the turn of the
  season between them - easing out over winter's days 4-5 once it holds, so a colony just short of the snow's reach
  gets its weather, and then its calm.
- **The wind blows from the north**, the way the front travels. Snow, wind, the trees, the smoke, the light and the
  score all read the same `storm`, so they rise and fall together.
- **Gusts** (archer's shape): bands of stronger wind, ~20 units across, rolling south through the view at ~12 units a
  second, a few seconds each, hashed from the tick (the same tick, the same gusts). Several during the storm; **one
  big gust when the front itself reaches the view** - the one the stinger goes with, once per storm.
- **Autumn's breeze**: light, eased in over autumn's first days, with an occasional soft gust - the score's autumn
  already has a `gusts` bed.

All of these numbers are current tuning, named constants, not rules.

## The pieces

### 1. Core: the wind field on the ground

`SetWindField` gains a `plane` argument, `WIND_PLANE_XY` by default (archer, unchanged) or `WIND_PLANE_XZ`, carried
in the header's unused `size[3]`. With X/Z, `default.vert` samples the field at `world.xz`, pushes along
`(wind.x, 0, wind.y)`, and puts the flutter's cross-sway across the wind on the ground rather than "through the
slab". **A change to core**: small, defaults unchanged; archer's sway is looked at once afterwards (by the user, as
with any core change).

### 2. The trees sway

- **A sway material**: a renamed copy of the palette material with `wind_flex` set, as `chasm_palette_puff` already
  is (`ApplicationChasm.cpp`), given to the pines', oaks' (every stage, bare included) and bushes' sets in
  `UploadForest`. The palette material itself is shared with the terrain, which must not move. A tree and a bush
  are ten times apart in height and the bend goes with height squared, so the bushes probably want a copy of
  their own.
- **Fruit bends with its plant**: the fruit sets take their plant's sway material. The bend is a function of the
  vertex, its set's origin and the flex, so fruit drawn at the plant's own transform moves exactly with it.
  Anything else drawn on a tree has to do the same.
- **The field**: baked each frame on the render thread over the view's ground, a coarse grid (~48 x 48), from
  `WeatherAt` - autumn's breeze, the storm band at the front, the gusts rolling through it.
- **Spring, summer and the calm after the storm: no field at all** (`ClearWindField`), so the trees stand still
  and cost nothing.
- **Birds**: a crow perched on a crown that sways would hang in the air beside it. A gust flushes the birds from
  the trees it reaches, and none settle in a tree that is swaying (`birds_plan.md`).

### 3. Snow falls

A chasm custom shader (`assets/shaders/chasm_snow.vert/.frag`) over **one mesh of a few thousand flakes**, built
once, each flake with a random seed in its vertices. The vertex shader does the rest:

- **A box that follows the camera**, from the ground to just short of the lens, so the snow always fills the
  screen. The flakes still live in the world: they parallax as the view pans, turn as it orbits, and fall behind
  roofs and trees (depth tested, not written).
- **Each flake falls** at its own speed, wobbles, and **drifts with the wind**, so a gust visibly drives the snow
  sideways across the screen.
- **Density is a uniform**: flakes whose seed is above it collapse to nothing. The same mesh goes from a few flakes
  to a blizzard with no rebuild, and the flakes come and go one at a time, so the ramp is smooth.
- **Flakes fade near the camera**, so none smears across the lens, and take the sun's colour, so they warm and
  cool with it.
- **Rain later**: the same mesh and shader with another kind - faster, streaked along their fall, blue-grey.

*Why not core's `INSTANCE_MOTION_FALL`:* an instance set needs a transform and motion per flake, and it can neither
follow the camera, take the wind, nor thin out without a rebuild. The shader gets all four for nothing. If archer
ever wants weather, the shader moves to `shared_assets`.

### 4. Sound

- **The gust stinger**: the view signals `gust` once per storm, with the big gust, and a cue table row sends the
  score's `gust` stinger. Data, not code: the row can be retuned or swapped without a build.
- **The score follows the storm (C)**: the land player's suspense rises with `storm` and eases back after, so
  winter's `snow wind` and `low wind` swell and the `cold strings` come in toward tense; the serene winter is the
  score at calm. The first thing in the game to move suspense - the Music panel's hold still wins over it.

### 5. With the storm (A, B, D)

- **A. Chimney smoke leans with the wind** in the storm and rises straight up in the still air after - the classic
  calm winter morning, and it shows the wind without a tree in view. The hearth smoke's lean is a fixed drift per
  puff today (`INSTANCE_MOTION_LIFE`, `ApplicationChasmHearth.cpp`).
- **B. Spindrift along the front**: snow blown low off the snowed side, along the front where it crosses the view, so
  the front shows the gust even over open fields - built as a band of the snow's own flakes on the ground (puffs were
  tried first; "As built").
- **D. The light dims under the storm**: the sun a shade dimmer and colder while the storm is overhead
  (`UpdateSun` takes the storm on top of the season's sun), back to winter's low, clear sun as it passes, so the
  serene view arrives as a clearing. A held sun stays held.

### 6. A Weather panel, and the MCP

Under Time, beside the Sun: the storm at the view, the front's distance, the wind, a **hold** for storm and wind,
and **gust now**. `chasm_time` returns `weather` and takes it to hold, as it does `sun` - for screenshots and
tuning without waiting for winter.

## Steps

1. **Core: the field on the ground** (piece 1).
2. **`WeatherAt`** with its test, **the panel and the MCP** (piece 6) - so everything after can be held and shown.
3. **The trees sway** (piece 2): the sway materials, the field bake, autumn's breeze, the front's band and the
   gusts, the birds flushed. Measured: the colour pass and frame time on seed 12 at play zoom, storm held at full,
   against the seasons' figures.
4. **Snow falls** (piece 3). Measured the same way.
5. **Sound** (piece 4): the gust stinger, then the score's suspense.
6. **With the storm** (piece 5): the smoke, the spindrift, the light.

The wiki's season pages get a line where the weather shows.

## Later

- **Autumn leaves**: the same particles, leaf-coloured, blown from the turning oaks in autumn's gusts - archer's
  `Leaves.cpp` is the pattern.
- **Spring rain** at the thaw: the front retreats north over spring's first five days, and could carry rain with it
  as winter's front carries snow.

## As built (2026-10-18)

**Files.** `Weather.{h,cpp}` - the storm, the wind and the gusts, engine-free. `ApplicationChasmWeather.cpp` - the
storm at the view and the big gust (physics thread), the weather clock, the wind bake, the sway materials, the snow,
the spindrift, a perched bird's bend, the Weather panel and `chasm_time`'s `weather`. `assets/shaders/chasm_snow.*`.
The smoke's lean in `ApplicationChasmHearth.cpp` (`LeanSmoke`), the light in `UpdateSun`, the suspense in
`UpdateMusic`, the `winter_gust` row in `assets/cues/chasm.json`, `BirdPose::tree/ride` in `Birds.h`.

**Core** (step 1): `Renderer::SetWindField` takes a `plane`, `WIND_PLANE_XY` by default; `default.vert`'s
`WindBendGround` is the ground's bend, slower than archer's (2.3 and 3.9 rad/s, a phase lag of 0.15 a unit up the
trunk - archer's 4 a unit ran waves up a ten-unit tree). Archer passes nothing new and takes the old path.

**Changed on the way:**
- **The pause.** The weather clock is real seconds while the game runs and stands still in a pause, so the flakes,
  the sway, the gusts and the spindrift hold as the water, mist and smoke do - but at 2x and 4x it keeps real time,
  where `SimSeconds` would have the snow fall four times as fast. The snow's THICKNESS still eases on real time, so a
  view panned onto the front in a pause fills with snow standing in the air. The big gust is not called in a pause: it
  waits, stinger and all, until the game goes on. (Built first to go on in a pause; changed the same day.)
- **The wind blows from the north-west**, not the north: the game's camera looks north, and a tree leaning straight at
  it hardly seemed to move. 0.8 west to 1 north (`WEATHER_WIND_X/Z`), the sun's quarter.
- **The sway is set per MODELLED height** (`WEATHER_SWAY_SHARE` 0.055 over the kind's mesh top): trees are drawn at
  about 0.7 of that, so the first tuning (0.025) hardly showed. Now about a twentieth of a tree's height in autumn's
  breeze, a fifth in the storm, near half as the big gust goes through.
- **The spindrift is the snow's own program**, not puffs: solid white puffs moved as the smoke is read as snowballs
  rolling about. It is a second program on `chasm_snow.vert` (`snow_band`), drawn in flat cells (a cell's size is now
  its instance's scale) laid on the ground along the front's line, its flakes racing across low and drawn out.
- **Birds ride the sway** as well as being flushed: `SwayOffset` works `WindBendGround` through on the CPU for a
  perched bird's tree, so it sits on a crown that moves rather than in the air beside it.
- **No rules test** for `WeatherAt`: chasm has no `make rules` target. Checked through `chasm_time` instead (below).

**Checked** on `camp_3` and `village_year5`, play zoom, a muted debug instance:
- The storm at the camp by date: 0.04 on autumn's last day, 0.27 at winter's first dawn, 0.58 on day 2, full on day 3
  as the front's line crosses the view, 0.5 on day 5 as the front stops, gone by day 7.
- The big gust fires once, as the front's line comes within 25 of the view (logged at 22 and at 25 on two runs); the
  music took `stinger gust - cue winter_gust`; the wind at the view went 6.9 to 12.7 a second and back over about two
  seconds; every settled flock in view went up. A jump straight past the front fires none.
- A diff of a held storm against none: only the trees, the bushes and their shadows move - ground, grass and rocks
  stand still.
- Suspense 0.75 in the storm and 0.20 (the score's own) after; the sun 1.26 against 1.70 and 8031 K against 6736 K.
- The village's smoke streams sideways in the storm and stands straight up on winter's day 7.

**Cost** (debug, `camp_3` at play zoom, eight alternations of a held storm on and off, medians): the shadow map, the
G-buffer and the colour pass unchanged within noise (2.85/2.90, 1.06/1.10, 1.80/1.80 ms); the custom pass 43 against
39 us; `PreRender` 0.38 against 0.28 ms - the bake (0.15 ms on a 32 x 32 grid), the snow's cells, the spindrift and
the smoke. With no wind there is no field (`ClearWindField`) and nothing is spent.
