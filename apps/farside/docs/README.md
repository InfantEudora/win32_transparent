# Farside design notes

*Working name.* A top-down space game with a perspective camera. The long arc comes from
**Starsector**: roam a galaxy, find a good place, settle it, and build a fleet from the colony. The
mood and scale come from **Starcom: Unknown Space**: lost in the middle of nowhere, in a very small
ship among large planets.

Talked through 2026-10-19. **Decided the same day:**

- **The waterline model**, in principle; the bench will give it its shape.
- **One sensor rule** for the player and the AI.
- **A continuous altitude.**

Sections marked *proposal* are still to discuss. **The scale bench is built** (last section); no game
is.

**The first goal is the scale, not a game.** Everything below depends on how big a planet is
against a ship and how much of it fits on screen. The first build is a bench that answers that by
feel (last section).

---

## The hook: a planet is a place, not a menu

In every game of this kind, a planet is a separate mechanic. You can't land on it as you fly; landing
opens a market screen (Escape Velocity, Endless Sky, Starsector), a text event (Starcom), or a
separate mini-game (Star Control 2's lander, Gravitar's planet screens). Whatever is "on" the planet
lives in that menu.

Here the planet is in the same space you fly in, and you land on it in the same view:

- **Where you land matters.** The planet's curvature hides things. Land on the far side of the
  planet and you can't see what is happening on the other side. A defence post there can't see you,
  so it can't shoot you.
- **Everything big is cover.** You can hide behind asteroids, moons, bigger ships and wrecks.
- **One continuous view.** There is no separate screen between flying and being on the ground.

### Games that touch this

I found no top-down game that lands on planets in the same view (searched 2026-10-19). The nearest
ones are 3D:

| Game | What it does with planets | What to take from it |
|---|---|---|
| Planetary Annihilation (2014) | RTS on round planets in a live solar system. Units fight on the ground, in the air and in an **orbital layer**; planets can be smashed into each other. | Fighting above the ground as well as on it. Its free camera that orbits the ball is the opposite of our fixed top-down one. |
| Spore, space stage (2008) | You fly a saucer from orbit down over a planet's cities in one continuous zoom. | The closest camera to ours: top-down over a ball. |
| Outer Wilds (2019) | Tiny planets in a compressed solar system, all one continuous space. What is on a far side, and when, is the game. | The scale is stylised and nobody minds. Rotation and timing as content. |
| Dyson Sphere Program (2021) | Small planets you build on and fly between with no loading screen. Day and night matter (solar power). | Day and night per site. A colony built on a small round planet. |
| Kerbal Space Program | Seamless landing. CommNet signals need line of sight, so a planet in the way cuts your signal. | The "far side" as a rule: relays, and knowledge needing line of sight. |
| Super Mario Galaxy | Planetoids small enough that the horizon hides what is round the bend. | A close horizon is readable and fun. |
| Gravitar (1982), Oids (1987), Solar Jetman (1990) | 2D. Gravity, and turrets on planets that shoot up at you. | The defence-post fantasy, but each planet is its own screen. |
| Highfleet (2021) | Radar finds the enemy and gives you away at the same time. | A model for active scans (later). |

---

## Geometry: a planet is a dome in the play field *(decided 2026-10-19, in principle)*

The camera looks straight down, and each planet spins on an axis pointing at it. The underside is
never seen. So **the underside is not modelled in gameplay at all**: what is playable is a dome.

A true 3D ball world needs a camera that orbits the planet, which is Planetary Annihilation's choice.
A fixed camera can't show the underside, and nobody can play on what they can't see.

The numbers below are where the bench starts. Building it will shape the model.

### The waterline

Ships fly over a plane, z = 0. **Every body sits partly sunk through that plane**: its centre is a
depth `d` below it. The plane works like a waterline:

```
                          camera, looking straight down
                                      |
  ceiling  . . . . . . . . . . . . . . . . . . . . . . . . . . . .  a flat lid above every dome

                                 _.-'''''-._
  low      .....................,-'   the cap   '-,..................  holds its height above the ground
  plane    =================./=================\.=================  z = 0, the waterline
                          /                       \
                         |  below the waterline:   |    drawn, and flown over as in
                         |  never reached           |    Starcom, but never landed on
                          \                       /
                           '._                 _.'
                              '-.._______..-'
```

- **Above the waterline is the cap.** You can reach it, land on it and build on it.
- **Below the waterline is scenery.** The outer band of the disc is still drawn, so the planet reads
  as a ball. Ships pass over that band as they do in Starcom.
- **`d` sets how steep the cap's edge is, and how much of the planet is playable.** A gentle edge
  means approaching ships climb onto the planet instead of hitting a wall:

| `d` | cap edge at (from centre, on screen) | top above the plane | slope at the cap's edge |
|---|---|---|---|
| 0 (centred) | 1.00 R | 1.00 R | 90°: a wall |
| 0.5 R | 0.87 R | 0.50 R | 60° |
| **0.6 R** | **0.80 R** | **0.40 R** | **53°** |
| 0.7 R | 0.71 R | 0.30 R | 46° |
| 0.8 R | 0.60 R | 0.20 R | 37° |

- **`d` is set per body**, so one rule covers every kind:
  - a landable planet: `d` ≈ 0.6 R, a gentle cap;
  - a gas giant: sunk almost entirely, scenery you fly over;
  - a moon or asteroid: `d` near 0, a ball standing in the plane that gives cover all round;
  - a ship, station or wreck: sits in the plane.

### Altitude: continuous *(decided 2026-10-19)*

Steering stays 2D, and height is an input of its own. **The ship holds its height above the
ground**, which is the plane in open space and the cap over a planet. Flying onto a planet therefore
climbs the dome by itself, and flying low hugs it.

- **The range** runs from 0, which is landed, up to a **ceiling**: a flat lid about 2 ship lengths
  above the tallest dome. Over the top of a planet there is less room between the ground and the
  lid.
- **The trade-off is smooth.** Higher sees further and is seen from further away. Lower hides behind
  domes, moons, hulls and wrecks. Flying faster high and slower low over land (atmosphere) is a
  candidate tuning, not a rule.
- **Landing** means descending to 0 where the slope allows it. Landed things turn with the planet.
  Climbing back out costs energy, so landing is a commitment.

Three fixed layers were the alternative. They made "am I hidden?" easier to read, but lost the
smooth trade-off. A continuous altitude keeps the question readable in two ways:

- **Line of sight is symmetric.** If your sensors see a post, its line to you is the same line. So
  apart from range and sensor strength, whatever you can see can also see you. The HUD can mark the
  contacts that have you in sight right now.
- **Altitude shows from straight above** in the ship's shadow on the ground below it (shadow maps
  already exist), and in the zoom.

**The zoom follows the altitude** *(proposal)*. Flying low is the landed zoom; the ceiling is the
cruise zoom. Climbing to see more of the map then literally shows more of it, and you are seen by
more in return. The bench tries this against a free zoom on the mouse wheel.

---

## Line of sight is geometry: the far side

**Visibility is a 3D ray tested against the bodies.** Domes, moons and asteroids are spheres, and
those tests are analytic and cheap. Ships, stations and wrecks are capsules or boxes, tested with an
rp3d raycast. There are no sight-radius circles: whatever blocks the ray hides you.

**How far things see on a ball** (in ship lengths). The rule of thumb is that something at height
`h` sees about √(2Rh) along the surface. "Cap radius" is measured along the surface, from the top of
the cap to its edge, with `d` = 0.6 R:

| Planet R | cap radius | post (0.3 tall) sees a ship 0.5 up within | sees a ship 3 up within |
|---|---|---|---|
| 10 | 9.3 | 5.5 | 9.4 |
| 20 | 18.5 | 7.9 | 13.8 |
| 40 | 37.1 | 11.2 | 19.9 |

**So a defence post covers a sector, not a circle:**

- A post at the cap's edge guards the space on its own side, and is blind to a ship coming low over
  the dome.
- A post at the top of the cap sees only ships well above it; the plane around the planet is below
  its horizon.
- Where you put posts becomes the defence game, and the same rule holds for the player's own colony
  later: a cap with no blind approach takes several posts in the right places.

### Sensors decide what anyone knows *(decided 2026-10-19)*

**The player and the AI play by the same rule.** The camera sees the whole cap, so if it drew every
enemy on it, the far side would hide nothing from the person playing. So:

- **A contact is known only while one of your own sensors has line of sight to it.** That holds for
  the player, whose screen draws only those contacts, and for the AI, which acts only on them.
- **Structures, once seen, stay as last-seen ghosts.**
- **The AI remembers where it last saw you.** It has to search when it loses you, so it needs a
  search behaviour, not just a chase.

This gives scouting a purpose, along with probes, sensor buoys and a relay satellite over the far
side (KSP's CommNet). Active scans that find more but reveal you (Highfleet) fit on top later.

---

## Rotation and light *(proposal)*

**First, the axis points straight at the camera (no tilt).** Sites on the cap turn around the
centre and never leave the view. The star lies in the plane, so each planet is lit from the side, as
in the Starcom screenshot. That gives the rotation two effects:

- **Every site gets day and night.** That affects solar power, how well things are seen, and lit
  windows on the night side.
- **Defence sectors sweep round.** A post guarding the east approach faces north a quarter-turn
  later. Approach from the side whose guard has turned away. This is the "a defence post can't shoot
  you" idea, made dynamic.

**A turn takes minutes** (try 6–12). At 10 minutes, the edge of an R = 20 cap moves at about 0.17
ship lengths a second, far below flight speed. So landed things are simply parented to the planet,
and flight doesn't need to turn with it.

**Tilt comes later, per planet.** With a slight tilt, sites near the waterline sink below it and
rise again once a turn: a base you can only reach part of the time.

**Moons and stations move on fixed orbits** (deterministic, counted in ticks), so they are moving
cover. A moon's shadow on a planet is an eclipse to hide in.

---

## Scale: the question this prototype exists to answer

**Starcom's screenshot, measured (2026-10-19):**

- The planet is about **20 ship lengths across**.
- The large ship at the top is about 1/8 of the planet; the station about 1/5.
- At that zoom the planet fills about 70% of the screen height, and the ship about 3%.

**Proposal: one world unit = one player-ship length**, and every other size is a multiple of it.
These are starting values for the bench, not rules:

| Thing | Size (ship lengths) | Why |
|---|---|---|
| player ship | 1 | the unit |
| drone, fighter | 0.3–0.5 | |
| defence post | 0.3 tall | small enough to hide behind a dome |
| capital ship, station | 4–8 | big enough to hide a low ship behind |
| asteroid | radius 0.3–4 | |
| wreck | 2–8 | |
| moon | radius 6–10 | |
| **landable planet** | **radius 20** (bench: 10–40) | twice Starcom's |
| gas giant | radius 50–80, mostly sunk | scenery |
| low flight | about 0.5 above the ground | |
| ceiling | about 2 above the tallest dome (about 10 at R = 20, `d` = 0.6) | |

**Why landable planets are twice Starcom's size.** Hiding on a cap only works if the cap is wider
than two sight ranges. At R = 10, a post and a low ship see each other within 5.5 ship lengths, and
the cap's radius is 9.3: there is barely anywhere to hide. At R = 20 it is 7.9 against 18.5, which
leaves room. Starcom never needed this because its planets are only backdrops.

**What that looks like on a 1440-pixel-high screen:**

| Zoom | Ship on screen | Screen height | An R = 20 planet |
|---|---|---|---|
| Landed or low | about 60 px | 24 ship lengths | The cap (32 across) is a third more than a screen; the whole disc (40) is 1.7 screens. |
| At the ceiling (cruise) | about 20 px | 72 ship lengths | The planet fills about 55% of the screen height: the Starcom shot. |

**The camera's field of view sets how much the dome bulges toward you.** The table shows how much
larger the top of a dome looks than the plane, with the screen 24 ship lengths high:

| FOV | dome 3 high | 6 high | 10 high |
|---|---|---|---|
| 20° | 1.05× | 1.10× | 1.17× |
| 30° | 1.07× | 1.15× | 1.29× |
| 40° | 1.10× | 1.22× | 1.44× |

Start at about 30°: wide enough that a planet reads as a ball, narrow enough that a ship at the
ceiling and one on the ground don't look like different sizes of thing.

**Surface detail.** At landed zoom, an R = 20 planet is about 7,500 pixels round. Texturing that
would take enormous textures, so the plan is detail computed in the shader from the sphere
direction. Chasm's terrain from a shader is prior art for this. A 2k cube map would be enough for an
R = 10 moon.

**Float precision is not a problem at these sizes.** A system 10,000 ship lengths across still
resolves a thousandth of a ship length.

---

## What already exists

- **`apps/ship`:** a top-down ship with handling that feels good, asteroids with colliders, lasers
  and explosions, and raymarched clouds with a cloud shadow map. Start from its ship and asteroids
  rather than from scratch.
- **`apps/sim`:** `StellarBody`, plus Perlin and Worley noise for a galaxy map. That is the layer
  after this one.
- **Core:** the deferred renderer and shadow maps, rp3d raycasts, deterministic ticks with replays,
  and MCP for driving and tuning a running app.
- **Built for the bench (2026-10-19):** a planet shader (a lit surface turning with the body, relief,
  clouds and an atmosphere rim), the halo, and the line-of-sight queries. Not built: night lights.

---

## The scale bench *(built 2026-10-19)*

A greybox app with no game in it, built to answer the scale question by flying it.

```bash
export PATH="/c/msys64/mingw64/bin:$PATH"
cd apps/farside && mingw32-make.exe -j8
./build/farside_nophysics.exe --minimized --mcp-port 8770 2>stderr.log &
```

**What is in it:**

- One planet sunk to `d` = 0.6 R, turning once in 10 minutes, with an atmosphere halo.
- A moon on a 6-minute orbit.
- An asteroid cluster, a capital-ship hull, a station and a wreck with debris.
- Two defence posts on the planet: one near the cap's edge, which turns with the planet, and one on
  the top.
- The ship, with continuous altitude, landing and take-off.

**Controls:** W/S thrust, A/D turn, R/F or the mouse wheel for altitude. Descend to 0, slowly, on
ground under 35° to land; climb to take off.

**The Bench panel** has every number as a slider, plus buttons that put the ship at comparison
spots:

| Spot | Where |
|---|---|
| `start` | In the plane, heading for the planet. |
| `low` | Just above the plane. |
| `landed` | On the far side of the cap from the edge post. |
| `landed_near` | Beside the edge post. |
| `cruise` | At the ceiling. |
| `starcom` | Framed so the planet fills 70% of the screen, as in the reference shot. |

**The debug picture:**

- Each post's line to the ship: green when it sees the ship, red when it doesn't.
- An orange outline around the region the chosen post can see, at the ship's current height.
- The waterline circles.
- A drop line from the ship to the ground.

The HUD reads the scale off live: the ship's length in pixels, how many ship lengths the screen is
tall, and the planet's share of the screen's height.

**MCP tools** (port 8770):

- `farside_status`: the bench's current state.
- `farside_set`: changes any setting, applies a spot, or places the ship by hand.
- `farside_hold`: flies the ship as a player would.

**Code:**

- `Field.h`: the geometry (ground, line of sight, push-out).
- `Flight.h`: the ship.
- `ApplicationFarside`: the scene, view and tools.
- `assets/shaders/farside_planet.frag`: the surface, lit by the engine's sun and shadows.
- `assets/shaders/farside_halo.frag`: the halo.

The ship and asteroid models are borrowed from `apps/ship/assets/meshes/ships.glb` through an extra
asset root; `main.cpp` says why it is not in `shared_assets`.

**First readings, at the defaults, in a 900-pixel-high window:**

| View | Screen height | Ship on screen | Planet on screen |
|---|---|---|---|
| Landed | 24 ship lengths | 38 px long | about 1.3 screens tall |
| `starcom` framing | 45 lengths at the ship | 20 px | 70% of the screen height |
| Cruise (ceiling) | 72 ship lengths | 13 px | under half the screen height |

Starcom's own ship is about 30 px at that window height, because its planet is half the size of
ours relative to the ship.

**The hiding rules came out as the doc predicted:**

- Landed about 20° from the pole, the ship is hidden from the top post.
- Climbing to 2.2 lengths there brings it over that post's horizon.
- The top post cannot see a ship flying low in the plane.
- The edge post sees only its own side.

**Still to decide, by flying it:**

- R, `d`, the camera and the ceiling.
- Whether the band below the waterline reads as part of the ball or as a strange skirt.

---

## Open questions

1. **Flight model.**
   - Starcom-style arcade with no gravity?
   - Or gravity wells and lander physics near a planet?
   - Leaning: arcade in space, plus a gentle pull and an energy cost to climb back out.
2. **Altitude input.** Two buttons, the mouse wheel (if the zoom follows altitude, the wheel is
   free), or partly automatic, such as sinking when slow over land?
3. **Landing.**
   - Automatic once you pick a spot, so the skill is choosing where?
   - Or piloted, so the skill is how?
   - Leaning: automatic.
4. **Combat.** A single ship in real time (Starcom), or fleets (Starsector)? The bench doesn't need
   either.
5. **Planet contents.** What is on a planet: bases, ruins, resources, colony sites? And how does the
   colony-to-fleet arc look when the colony is buildings on a cap?
6. **The galaxy layer.** Systems and travel between them. This comes after the bench, which is one
   system.
7. **The name.**
