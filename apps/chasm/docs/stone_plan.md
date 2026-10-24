# Stone: rubble, the stonecutter, deposits and mines

Talked through with the user 2026-10-14. Stone is the first good that has to be dug out of the world,
and floatstone (`gameplay_plan.md`, "Floatstone") the second. Before this, floatstone was "hauled up by
winch" with nothing behind it, stone was a row in `goods_plan.md`'s table marked "later", the mountain
pockets held rock clusters "until resources exist" (`Forest.cpp`), and the scattered rocks were scenery:
generated, never saved, skipped by the woodcutter, hidden under whatever is painted over them.

The proposal went to the user in chat, in five pieces - loose stone, deposits, the mine, clearing rocks,
what stone buys - and the user took all five ("perfect") and answered its questions. The answers are
below, then the plan with them worked in. **Steps 1 to 5 are built** (Build order, and an "As built" section for
each); what stone buys is for later.

## Decided (the user, 2026-10-14)

- **Ordinary stone to start with, floatstone from seams.** The loose stone in the chasm is plain STONE,
  and it is how the colony gets going. FLOATSTONE comes out of SEAMS in the chasm's walls, dug by a
  mine. Two goods, as `goods_plan.md` already lists them.
- **Loose stone lies mainly at the foot of the wall**: on a balcony, against the cliff rising behind it.
- **Small stones are picked up; big ones are broken.** Small stones and rocks lying loose are picked up
  by the ordinary builders and taken to a store - the same thing as a crate, so the two are combined.
  The STONECUTTER'S HUT clears only the big stones: the rocks that are props today, and perhaps some
  bigger ones.
- **A pickup for crates.** There is no way yet to have a crate picked up on purpose (FETCH FIRST,
  `crates_plan.md`, was proposed and never built). The pickup here is that, for crates and loose
  stone alike: it takes the contents to a store.
- **Deposits run out; the mountain does not.** A floatstone seam on a balcony holds about 50. The
  mountain never runs out.
- **The stonecutter's hut is what stone is first for**: the buildings that make stone cost some. What
  else stone builds is for later.
- **Coal is not part of the zeppelin.** But there has to be a reason to go to the mountains, or a player
  skips them, and the same goes for the swamp. Valheim gives every biome a unique resource you need to
  progress; whether Chasm copies that fully is open (below, "A reason for every biome").

The second round, the same day:

- **Rubble is picked up when needed**, as crates are, with no order; PICK UP puts one at the front of the
  queue.
- **Rubble lies above and below**: along the rim as well as at the foot of the balconies' walls. The
  winch already waits for rope; stone need not wait for the winch too.
- **Boulders are in**: bigger rocks that block building until the stonecutter breaks them.
- **Coal, and what the swamp gives**: later.
- **Floatstone looks like crystal, a purple-ish blue** - nothing like the grey stone, so it reads at once.
- **The props are modelled in Blender by a worker window**, as most of the assets were.

The third round, 2026-10-15, after seeing step 4 in the game:

- **Rubble only at the balconies' level**, at the foot of their walls - none on the rim, which reverses "above and
  below". So the first stone waits for a winch, and the winch for rope.
- **A seam's crystals lie as piles, like rubble** (the user chose this of three readings: crystal outcrops dug by a
  mine, crystals broken like rocks, or piles picked up like rubble): floatstone lying at the seam's foot, picked up by
  the builders - and with PICK UP - as rubble is. The seams and the mine stay, for more.

## The chain

> hemp -> rope -> winch -> balcony
>
> rubble at the foot of the balconies' walls -> stonecutter's hut and first mine -> stone from rocks and the
> mountain, floatstone from the seams -> the zeppelin

The two meet at the balconies. Since 2026-10-15 all the rubble lies there, so the first stone needs rope and a
winch too; the first floatstone is a seam's crystal lying at its foot, and the rest needs a mine.

## The plan

The five pieces as proposed and taken, with the answers above worked in. Where something is still only
a proposal it says so.

### 1. Stone, and rubble at the foot of the wall

- **Two new goods**: `GOOD_STONE` and `GOOD_FLOATSTONE`, the next two numbers (9 and 10), both
  MATERIALS. Every store takes them by default, with a chip each on its card. STONE shows in the top bar
  once the colony holds some or a site wants it, as ROPE does. No house keeps either.
- **A pile of rubble is a crate**: an `EconomyCrate` with an origin of its own, `CRATE_RUBBLE`, holding
  stone. Everything a crate does, rubble then does with no new code:
  - villagers take from it wherever they take from a store (`SourceStock`), so a stonecutter's hut's
    or a mine's site takes its stone straight off the pile;
  - the builders, as caretakers, carry it to a store that takes stone, as their last errand;
  - building over it is refused; it is gone once empty; it is saved, hashed and replayed.

  Only its look differs: a RUBBLE prop - a few broken stones - instead of the crate. "One crate for
  everything" (`crates_plan.md`) still holds for crates; rubble is meant to read as stone on the ground.
- **Placed from the seed when a map is made**, into the crates list, as the finds will be
  (`crates_plan.md` step 4 - the two share the placing). Mainly along every balcony, on the plots at the
  FOOT OF ITS WALL: balcony ground with higher ground among its cells' corners, the pile pushed toward
  the wall within its plot. More thinly ALONG THE RIM: plateau ground with the chasm's drop among its
  cells' corners. Every so many plots, by a hash of the plot, each pile a few stone. One pile to a plot,
  as one crate to a plot. Never on a winch's landing.
- `PlotIsFlat`, the forest's rule that keeps props off any plot not flat all round, does not apply:
  rubble is a crate, placed by the economy, not a prop of the forest.
- **Found when explored**, as a find is. Seen from the rim, most balconies are in sight anyway.

### 2. PICK UP: crates and rubble taken to a store

- **A PICK UP chip on a crate's card, and on a pile's** (one card: a pile is a crate). It sends a
  recorded command that puts moving it AHEAD of the builders' other errands - the way a site is put
  first (`ZONE_OP_SITE_PRIORITY`). It replaces FETCH FIRST.
- **Builders only**, as moving crates is now. With no builders' hut, nobody picks up, and the card says so.
- **An order reaches past the settlement's land.** A caretaker's own errands stay within it (`Ours`,
  `settlements_plan.md`), but an order is the player's, as a post is. So PICK UP fetches a find far out,
  or rubble on a balcony beyond the settlement's land.
- **Without the chip, rubble is picked up when needed**, as crates are today (the user's): a site's
  carrier takes its stone straight off a pile, and the builders move what lies to a store as their last
  errand. PICK UP only moves a pile or a crate to the front of the queue.

### 3. Clearing rocks: the stonecutter's hut

- **A woodcutter's hut for stone**: `ZONE_KIND_STONECUTTER`, painted by the player, built with wood and
  stone, one stonecutter chosen by strength (`gameplay_plan.md`'s skills table puts quarrying there).
  Its card can stop work, like the other workplaces.
- **Rocks become state, as trees are.** `prop_state` already keeps a byte for every prop, rocks
  included, saved and hashed. A rock is STANDING, BROKEN (its stone lies there in pieces, drawn as
  rubble, as a felled tree's log lies) or CLEARED. Those are the tree's values 1 and 2 under names of
  their own; states are never renumbered.
- **A round is the woodcutter's** (`TickWorker`, `FindTree`, `WoodNear`), nearly line for line: within
  the hut's reach, broken stone lying first, then the nearest standing rock; breaking takes a while at
  strength 5, faster when stronger (`EconomySkillFactor`); a second pile if one lies close; delivered to
  a store, or into the hut. A rock gives stone by its size: the small rocks a little, `rock_c` more, a
  cluster most.
- **Rocks do not block building** (proposed), so that nothing waits for a stonecutter before the colony
  can have one. A rock under a building, garden, lot or field is gone, as a fruit bush built over is gone
  for good - nobody breaks it.
- **BOULDERS** (agreed - the user's "perhaps some bigger ones"): a few rocks bigger than any today
  that DO block. Nothing is built on a boulder's plot until it is broken, and it gives the most stone. The
  hut then clears land as well as making stone.

### 4. Deposits

- **A DEPOSIT is ground that holds a good to be dug.** Deposits are generated from the seed and the
  grid, like the forest, and never saved. What has been taken from each is saved (by deposit, as
  `prop_state` is by prop) and hashed. One kind of record serves every dug good: stone, floatstone,
  coal later.
- **Floatstone seams, in the chasm's walls.** On a balcony's wall - the cliff from the balcony up to the
  rim - a run of a few plots along its foot holds a seam of about 50 floatstone (the user's figure). At
  most one a balcony to start with. Later on the islands too, when zeppelins reach them.
- **The mountain is one deposit that never runs out**: a mine anywhere against it gives stone.
- **Outcrops in the pockets**: the rock clusters placed there "until resources exist" become outcrops, a
  stone deposit on flat ground. A pocket is mountain, so they never run out either.
- **Floatstone is crystal**, a purple-ish blue (the user's): faceted chunks where grey stone is rounded,
  so a seam, a pile or a load is never taken for stone.
- **Seen in the wall.** A seam is drawn as a band of its own colour across the wall's strata
  (`TerrainMesh.cpp`, a stratum every `TERRAIN_STEP`) along its run, so the player spots it from the rim.
  Worked out, the band goes back to the wall's own colour. It shows once its wall has been explored.
- **The tool** lists deposits: id, kind (seam, mountain, outcrop), good, plots, held and taken.

### 5. The mine

- **One building, two looks**: `ZONE_KIND_MINE`, one plot, one storey. Dug into a wall, it is a timber
  portal against the cliff with a spoil heap; on an outcrop, an open pit.
- **Where: the winch's rule turned over.** `WinchIsBuildable` wants a balcony BELOW among its cells'
  corners. A mine wants ground HIGHER than its own among them - the wall rising behind it - with a
  deposit in that wall: a seam's run, or the mountain. Or it stands on an outcrop's plot. Its own plot is
  buildable ground: a balcony, the plateau at the mountain's foot, a pocket. Never on a winch's landing.
  With the winch, it is the only building allowed against a cliff. Refused elsewhere with a reason: "no
  seam in this wall", "a mine is dug into a wall or an outcrop".
- **It costs wood and stone**, and waits for stone the way a winch site waits for rope: staked out, its
  card saying what is missing, and NEEDS ATTENTION listing MINE: NEEDS STONE, pointing at the nearest
  rubble its carriers can reach - or, with none left, at a stonecutter's hut to build.
- **The miner**: a job chosen by strength. While the miner works, the mine digs at strength's pace and
  holds a load and a little more, as a water collector fills. When a load is ready, the miner carries it
  to the nearest store that takes it - up the winch, from a balcony. Stoppable from its card.
- **Worked out**: a seam's mine with nothing left in its deposit stops, NEEDS ATTENTION says MINE: WORKED
  OUT, and the miner goes free. The building stands until erased.

### 6. What stone buys - later

First, the stonecutter's hut and the mine (the user's). Ideas noted for later, none agreed: stone walls
that zombies take longer to break (`zombies_plan.md` step 4, walls breaking); stone storeys for towers
and stores; houses that keep warm on less wood. Floatstone is the zeppelin's.

## A reason for every biome (liked, details later)

The user, 2026-10-14: coal won't be part of the zeppelin, but there needs to be a reason to go to the
mountains, otherwise you'd skip them - and the same goes for the swamp. In Valheim each biome has a
unique resource you need in order to progress; whether to copy that fully is not decided.

A softer version, which the user liked ("a good one"); coal and the swamp's resource are left for later.
**Only the chasm is a hard gate** (floatstone, for the
crossing). Each other biome answers a problem that grows on the colony over time, so a colony can put
off going there, but not for ever:

- **The mountain is what lasts.** Trees don't grow back yet and the plateau's rocks run out. The
  mountain's stone never does, and coal heats a winter that the shrinking forest no longer can. By the
  second or third year the mountain is where building and heat come from, which gives the settlement
  on the mountain side (`settlements_plan.md`) its reason.
- **The swamp is worked in winter.** It freezes and no zombies come (`gameplay_plan.md`, "The threat"),
  so winter is when it can be worked. What it gives is open: peat (a fuel, which overlaps coal), reeds
  for thatch, or bog iron - iron ore really does form in bogs - for tools that speed other work up.
  The dangerous season and the harvest season are then one place at two times of year.
- **The desert** is the pyramid's, and perhaps what the pyramid is built with.

## Build order

Not the pieces' order: each step here is playable on the one before, as a colony would meet them.

1. **(BUILT 2026-10-14, "As built: step 1" below)** **Stone and floatstone as goods; rubble as crates**, placed from the seed at the foot of the balconies'
   walls and along the rim, drawn as rubble; in the tool. Tested: every seed gets its piles, the same seed
   always the same ones; with a builders' hut, the builders carry the rim's rubble to a store, and with a
   winch the balcony's too; a site takes stone off a pile.
2. **(BUILT 2026-10-14, "As built: step 2" below)** **PICK UP** on a crate's and a pile's card. Tested: a picked-up crate is moved before a site is built;
   one outside the settlement's land is fetched.
3. **(BUILT 2026-10-14, "As built: step 3" below)** **The stonecutter's hut**, and rocks as state. Tested: a hut built with balcony stone breaks the rocks
   in its reach, nearest first; a save and load keeps every rock's state.
4. **(BUILT 2026-10-14, "As built: step 4" below)** **Deposits**: seams, the mountain, outcrops; drawn in the strata,
   found by exploring, in the tool.
5. **(BUILT 2026-10-15, "As built: step 5" below)** **The mine**: its ground rule, its cost and NEEDS ATTENTION row, the miner, worked out. Tested: a mine
   on a balcony seam digs it out and its floatstone goes up the winch; one against the mountain never
   stops.
6. Later: what stone buys.

Each with two replays of one recording compared against each other, and the wiki pages for what it adds.

## Current tuning

First guesses, the current tuning rather than rules. Each becomes a named constant beside the
`ECONOMY_*` ones.

| What | Value |
|---|---|
| Stone in a rubble pile | 3-6 |
| Rubble along a balcony's wall | one plot in three (`RUBBLE_WALL_CHANCE`), 3-6 stone a pile - measured on seed 12, 4-37 piles a balcony, not the 6-8 guessed: a wall's foot is longer than assumed |
| Rubble along the rim | none, since 2026-10-15 (the user's) - it was one plot in twelve, 2-4 stone a pile |
| A seam's crystal lying | a pile on every plot of its foot, 1-3 floatstone each (`RUBBLE_FLOATSTONE_MIN`, `_MAX`) - seed 12, 20 piles, 41 floatstone |
| Floatstone in a balcony's seam | 50 (the user's, `DEPOSIT_SEAM_FLOATSTONE`) |
| Seams a balcony | at most one, never under the mountain: about half the balconies off it (`DEPOSIT_SEAM_CHANCE`), a run of 4 plots (`DEPOSIT_SEAM_PLOTS`) - seed 12, 5 of 17 |
| Outcrops | a pocket's flat plots, 6% each (`DEPOSIT_OUTCROP_CHANCE`, the old rock clusters' share), none within 8 of another (`DEPOSIT_OUTCROP_APART`) - seed 12, 7 and 8 in its two pockets |
| The mountain, the pockets' outcrops | never run out |
| Stonecutter's hut | a storey's wood, and 5 stone a plot (`ECONOMY_STONECUTTER_STONE`); one storey high |
| Breaking a rock | 2 s a stone of it at strength 5 (`ECONOMY_BREAK_SECONDS`): a boulder 24 s |
| Boulders | 30% of the open land's rocks (`FOREST_BOULDER_CHANCE`) |
| Mine | a storey's wood, and 15 stone |
| Stone from a rock | `rock_a` and `rock_b` 2, `rock_c` 4, a cluster 6, a boulder 12 |
| A mine's pace | a stone every 8 s at strength 5 - five or six a day, so a seam lasts about a season |
| Stonecutter's reach | as a woodcutter's |

## Open

- **Coal**, later (the user's): where exactly (seams in the pockets' cliffs?) and what burns it.
- **The swamp's resource**, later, and how far "a reason for every biome" goes (above).
- **Floatstone on the islands**: `gameplay_plan.md` says both "without end" (Terraces) and "scarce"
  (Floatstone). To settle with the zeppelin.
- **PICK UP for many at once** - a box over a balcony's piles - if one chip a pile is tedious.
- **Models**: modelled by a worker window 2026-10-14, for the user to tune - "The props", below - with
  floatstone's colours in a palette row of their own (`PAL_MINERALS`). The stonecutter's hut and the
  mine's own walls and roof are buildings, drawn in code (`BuildingMesh.cpp`'s `kind_looks`), not modelled.
- **How much rubble** (step 1, measured): the balconies' walls hold three or four times the guess above,
  and rubble seldom lies on the camp's land at the start (seed 12: none within 40 of the camp, 3 piles
  within 80), so the first stone usually waits for the land to grow, a winch, or PICK UP. Tuning, the user's.
  The user, 2026-10-14: the amount is fine as it is. 2026-10-15: none on the rim at all, so the first stone always
  waits for a winch - and with it the stonecutter's hut, and every site on a boulder.
- **A picked-up crate nobody can reach** (step 2) is tried again at every builder's look: a route search from
  the crate to a store that fails each time. From a balcony with no winch it fails at once - the balcony is
  small - but a pile across the chasm would search the whole far side. If that ever shows in the timings,
  remember the failure until the zones change. Its card says only that nobody is on the way.
- **A mine against the mountain from the plateau** (step 4, measured): the plateau meets the mountain on a gentle
  slope - on seed 12 the ground behind its foot rises a median 0.1, more than 1.5 on only 64 of 556 plots, nearly
  all round the pockets. "A wall rising behind it" holds round the pockets' floors and hardly anywhere else.
  **Decided (the user, 2026-10-15): a portal into the slope** - a mine stands at the plateau's foot of the mountain
  too, dug in as the worker's `mine_portal` is drawn; not only round the pockets.
- **A PICK UP far out can outlast a builder's water** (found 2026-10-15): with all the rubble on balconies, a pile can
  be several days' walk away there and back, past a full water bar (`NEEDS_WATER_FULL`, 4 days). On seed 12 the
  builder sent to seam 4's crystal, about 140 from the camp, lifted it and died of thirst on the way back on day 5 -
  and his load went with him, since the dead do not put theirs down yet (crates_plan.md, planned).
  **Decided (the user, 2026-10-15): refused on distance alone**, with a wide margin - built, "As built: PICK UP's
  reach" below.
- **The clouds over an unexplored balcony** (found 2026-10-15, "As built: step 4"): the play camera can settle on one
  and look under the clouds' lid. **Left as it is (the user, 2026-10-15)**: it may be just right. Closing it would
  take a wall of clouds down the drop, so the clouds' depth does not give the chasm away.
- **What is dug from a deposit, and a seam going back to rock** (step 4): left to the mine, the first thing to dig.
  The terrain's mesh is built once a map, so a worked-out seam's band needs its chunk rebuilt then.

## As built: step 1 (2026-10-14)

Files: `Goods.h` (`GOOD_STONE` 9 and `GOOD_FLOATSTONE` 10, MATERIALS), `Economy.h` (`CRATE_RUBBLE`, the
`RUBBLE_*` tuning, `PlaceRubble`; the CRATES notes), `Economy.cpp` (their names, `PlaceRubble`, called in
`FollowZones` on a new colony's first tick after the camp's supplies; the tool's origin "rubble"),
`ChasmHud.cpp` (stone a cool grey, floatstone a purple-blue), `ApplicationChasm.*` (`LoadProps` picks the
rubble's mesh), `ApplicationChasmCrates.cpp` (rubble's own instance set), `ApplicationChasmSelect.cpp` (the
card, a pair of outlines for rubble), `ApplicationChasmEconomy.cpp` (the goods in `store_allow`'s text);
the wiki's `stone.md` (new) and `crate.md`.

- **Rubble is a crate** (`CRATE_RUBBLE`), so everything else - taken from for sites, moved by the builders
  on their own land, saved, hashed, refused to build over - is the crates' code, unchanged.
- **Where** (`PlaceRubble`): every plot in index order; a BALCONY plot with a higher corner among its
  cells' (the foot of a wall), or a PLATEAU plot with a lower one (the rim); by `Mix` of the seed and the
  plot, `RUBBLE_WALL_CHANCE` or `RUBBLE_RIM_CHANCE`; at `RUBBLE_TOWARD` of the way to the nearest such
  corner; only where `EconomyCrateGround` holds. A loaded save keeps its own crates (`Restore` clears
  `f_new_colony`), so a save from before this has no rubble.
- **Drawn** with the worker's `rubble_a`, which reached the GLB the same day; without it, the rock cluster
  at 0.35 stands in. `rubble_b` is not used yet. The selection's outline is a copy of the same mesh.
- **Not yet**: rubble is not hidden under the clouds - finding by exploring is crates' step 4. A load
  dropped on a pile goes into it, and the pile is still "Rubble".
- **Measured** (debug, seed 12): 114 piles, 470 stone - 85 piles and 381 stone at the walls of the
  balconies (in five runs of 4 to 37 piles), 29 and 89 on the rim. Seeds 3, 7, 21, 33, 45 and 58: 89-139
  piles, 348-584 stone; the nearest pile to the camp 25 to 234 away. Every pile, picked at its point, is
  on its own plot.
- **Tested** (debug, minimised, port 8769):
  - seed 12 generated twice: the same rubble, pile for pile (id, plot, point, stone);
  - pile 1's card: "Rubble", 5 stone, "Fallen from the chasm's walls"; selected in play mode, drawn at
    the foot of its wall with its outline;
  - a crate of 7 stone spawned by the camp, with a store and a builders' hut placed: a builder carried it
    into the store, and the top bar read STONE 7;
  - saved and loaded: all 114 piles the same;
  - an 856-tick recording with a stone crate spawned and carried off, replayed twice: every tick's hash
    equal, nothing changed between ticks, all 114 piles there at the end of both.

## The props (2026-10-14)

Ten props added to the Export collection of `art_source/chasm/chasm_props.blend` by a worker window, for the
user to tune, and to `assets/meshes/chasm_props.glb`. They are generated by `tools/blender_chasm_props.py` (its
"Stone" section, authoring row 7 at y -35), as the range's props and the fruit are. Previews:
`art_source/chasm/previews/stone_props.png` - a lineup at game scale from the game's camera, beside a man,
`crate_a`, `rock_c`, `rock_a` and `rock_cluster_a`, with the portal set in a mock wall - and
`stone_props_close.png`, close-ups.

| Prop | w x d x h | Tris | Look |
|---|---|---|---|
| `rubble_a` | 0.69 x 0.44 x 0.25 | 98 | a heap: three broken stones, one lying on them, wedges round them, chips at the edge |
| `rubble_b` | 0.76 x 0.54 x 0.20 | 80 | a flatter spill of the same, nothing on top |
| `boulder` | 1.50 x 1.60 x 1.18 | 96 | big facets, split by a crack whose two parts lean apart, chips at its foot |
| `stone_blocks` | 0.61 x 0.39 x 0.42 | 60 | six cut blocks: three side by side, two across them, one on top |
| `floatstone_a` | 0.44 x 0.41 x 0.52 | 84 | six crystal shards fanning out of one root, as it grows in a seam |
| `floatstone_b` | 0.42 x 0.40 x 0.16 | 40 | four broken crystals lying in a heap: a pile of it, or a load |
| `mine_portal` | 1.50 x 1.26 x 1.44 | 173 | two posts and a lintel, a lean-to of three planks, a black mouth, rock round it, rails out |
| `mine_cart` | 0.23 x 0.36 x 0.27 | 48 | a timber tub with an iron rim, heaped with stone, on four wheels |
| `quarry_crane` | 1.76 x 0.90 x 1.86 | 134 | a derrick: mast, two stiff legs on sills, a boom, a cut block hanging, a windlass |
| `outcrop` | 2.20 x 1.62 x 0.66 | 103 | three beds of rock stepping up to one side, the lowest coming out of the ground |

Sizes in game units at scale 1; w is along x, d along the glTF z. Every face is flat shaded, one palette cell.

- **Colours.** The grey stone is the rocks' three cells (24-26), the top facets lightest; the cut blocks' ends are
  the plaster cell (29), the palest thing on them. Floatstone has cells of its own (below). The timber is the
  timber, bark and path cells (31, 22/23, 13 for cut ends), the rails and the cart's rim the dark stone, the
  crane's lines the crops row's rope, and the mine's mouth the effects row's black.
- **Floatstone's colour: palette row 10**, `PAL_MINERALS` in `Palette.h` - columns 0-3 a purple-blue crystal, light
  `b09cf6`, mid `7a66de`, dark `4836a0`, and a near-white lilac glint `e8e2ff`. Row 0 has no free column, so it
  could not be a row-0 column like the other props'. Off the biome and season rows, no season fades it and no snow
  lies on it (`chasm_ground.frag`), so floatstone reads the same in every biome and month. Painted by
  `tools/make_palette.py --minerals`; no other cell changed. The seams' band in the walls (piece 4) can take the
  same cells. A crystal's facets are coloured by how squarely they face a fixed light from the sky and the camera,
  so neighbouring facets step from dark to glint whatever the sun does.
- **Origins** are the middle of the base, as the other props'. Two exceptions: `boulder` and `outcrop` sit on the
  middle of their footprint, because they fill a plot and their chips and ledges would pull the base's average
  0.25 and 0.43 off to one side; and `mine_portal`'s is the middle of its mouth's sill.
- **The portal faces glTF +Z** (Blender -Y), as `range_target` does - so the engine's forward, -Z, points into
  the wall. The rails run 0.61 out along +Z; the rock round the frame runs back to -0.65, in the walls' near-rim
  strata colours (columns 5, 6 and 8), to sink into an uneven wall. The mouth's black back is 0.16 behind the sill:
  set the portal with the wall's face between 0.16 and about 0.6 behind it. Against the mountain (the frozen row's
  grey rock) the rock would want its UVs moved to that row, as `biomes.png` shows the props; the black must not move.
- **The cart** runs along its length on the portal's rails: its wheels are at the rails' gauge, and its base is
  the wheels' bottom, so on the rails it stands 0.036 up.
- **The boulder against the rocks**: `rock_a` at scale 1 is already 1.81 x 1.47 x 0.98, and the forest draws rocks
  at 0.9-1.5. So "bigger than any rock" is in height and bulk - 1.18 high, a third taller than `rock_a` - and the
  crack, not in footprint, which stays inside a plot.
- **The crane's boom** reaches along +x, its tip 1.43 out from the origin, which lies between the mast and the
  feet of its legs.
- **Exported with `blender_chasm_props.py --add --merge`** (new): the ten exported alone and appended to the GLB as
  it stood. A whole export would have changed what ships: the .blend has been edited since the GLB was made (the
  woman's armature and mesh, and `Idle`), and the collection's own exporter also cuts `Idle` and `Shoot` to 15 and
  16 channels. Compared node by node with the GLB before: all 80 nodes and the three clips byte for byte the same
  (its binary chunk unchanged at the front), the ten added. `blender_chasm_props_check.py --verify` passes all
  ten. The failures it still lists were there before: the figures, `tree_willow_a` and `twig_a` (nodes unchanged),
  and two `Icosphere` objects that are not among the GLB's nodes.

## As built: step 2 (2026-10-14)

PICK UP. Files: `Economy.h` (`EconomyCrate::f_pick_up`, `SetPickUp`, `MoveCrate`'s `f_picked`), `Economy.cpp`
(the builder's errand in `TickWorker`, `MoveCrate`, `SetPickUp`), `ChasmSave.cpp` (`"pick_up"`, written only when
set), `ApplicationChasm.h` (`CHASM_CMD_PICK_UP`, `OVERLAY_HIT_PICK_UP`, `SubmitPickUp`),
`ApplicationChasmEconomy.cpp` (the command, the hash, `chasm_economy` op `pick_up` and each crate's `pick_up`),
`ApplicationChasmSelect.cpp` (the chip on `CrateCard`, its click, `pick_up` in the tool's actions); the wiki's
`crate.md` and `stone.md`.

- **A flag on any crate**, rubble or not: `f_pick_up`, set and cleared by a recorded command
  (`CHASM_CMD_PICK_UP`: subtype the crate, value[0] on or off) from the card's **Pick up** chip, lit while set,
  or `chasm_economy` op `pick_up`. Saved and hashed. Refused only for a crate no longer there.
- **The builder's errand**, in `TickWorker`: after `KeepTowers` and `MakeProvisions`, before `FindSiteWork` -
  `MoveCrate(..., true)`, which looks only at flagged crates and skips the settlement check on the crate, not on
  the store. The rest is moving a crate as it was: a load at a time, the nearest of the nearest few he can walk
  to and on to a store from, others' loads already on the way counted off. The flag stays until the crate is
  gone. Unflagged, a crate is the last errand as before - so "picked up when needed", the user's.
- **The card**: "No builders to pick it up: build a builders' hut", in amber, while set with no builder in
  the colony.
- **Tested** (debug, seed 12, minimised, port 8769):
  - rim pile 62 (3 stone, 76 from the camp, on no settlement's land) put PICK UP with no builders: the chip
    lit, the amber line on the card; crate 999999 refused, "no such crate";
  - a store and a builders' hut placed: Jelle, the builder, walked out to it, lifted it, and the store held 3
    stone; no other pile was touched;
  - a balcony pile with no winch, and a rim pile across a river, flagged: nobody went - no way there;
  - saved and loaded: the flag kept;
  - a fresh seed 12, a store and a builders' hut, then a 6,877-tick recording of pile 62 put PICK UP and
    fetched, replayed twice: every tick's hash equal, nothing changed between ticks, the pile gone and 3
    stone stored at the end of both.

## As built: step 3 (2026-10-14)

The stonecutter's hut, rocks as state, and boulders. Files: `Zones.h`/`.cpp` (`ZONE_KIND_STONECUTTER` 13: name,
one storey, stoppable, drawn as a building), `BuildingMesh.cpp` (its look: stone walls under slate or dark
shingle), `Forest.h`/`.cpp` (`PROP_BOULDER`, appended so no kind moves; `ForestIsTree`, `ForestIsRock`,
`ForestRockStone`, `FOREST_BOULDER_CHANCE`; boulders placed), `Economy.h`/`.cpp` (`WORKER_JOB_STONECUTTER` 8,
`CRATE_BROKEN`, the tuning, `rocks`, `FindRock`, `BreakRock`, boulders in `SiteTreesStanding`, `EconomyBouldersOn`,
the hut's stone in `StoreyGoods`, a store beside it taking stone), `ApplicationChasm.*` (the tool on **M**, the
panel, `chasm_paint` and `chasm_tool`, a broken rock not drawn as a stump), `ApplicationChasmTime.cpp` (the build
bar, the jobs list), `ApplicationChasmSelect.cpp` (names, a pick icon, what he is doing, the hut's card, the boulder
lines on site cards, "Broken from a rock"), `ApplicationChasmIssues.cpp`, `ApplicationChasmWorkersView.cpp` (he
swings at a rock as a woodcutter at a tree), `ApplicationChasmEconomy.cpp` (`chasm_economy` op `rocks`); the wiki's
`stonecutter.md` (new), `stone.md` and `crate.md`.

- **Breaking a rock leaves rubble, not a new kind of state.** A rock is STANDING or CLEARED (`PROP_STATE_STUMP`'s
  value; the plan's BROKEN step is the rubble crate instead). `BreakRock` clears it, puts its stone
  (`ForestRockStone`) in a crate where it stood - `CRATE_BROKEN`, drawn and named as rubble - or, where that plot is
  no open ground (a boulder under a site), on the nearest that is (`CratePlotNear`); calls `Felled`, so a site or wall
  that waited goes up; and sets him `WORKER_LIFTING` at that crate, so the carrying is the crates' code. The rest of
  the pile lies for him next time, for sites, for builders and for PICK UP.
- **His round** (`FindRock`), from home while a store of his has room for stone, by the walk from his hut
  (`Walkers::MapFlood`) within `ECONOMY_STONECUTTER_REACH`: a boulder on a site of his settlement's, then any crate
  holding stone, then the nearest rock - by the walk, the first of the nearest four he can route to. The rocks are a
  list of their own (`rocks`), looked through whole: the trees' buckets are left to trees, so a woodcutter never
  goes for a rock and a road never waits on one.
- **A boulder blocks building**: `SiteTreesStanding` counts standing boulders on a site's plots or cells, so a site
  waits for one as for a tree. Roads and walls do not.
- **Not done**: the hut keeps no stone (a store next to it takes only stone, as beside a woodcutter's takes wood);
  no NEEDS ATTENTION row for a site waiting on a boulder with no stonecutter - its card says it; no price tag for
  the hut beyond its cost.
- **Tested** (debug, seed 12, minimised, port 8769):
  - the boulder asset loaded (30 of 30 prop kinds); 12 rocks within 70 of the camp, 4 of them boulders;
  - a store and a stonecutter's hut placed: Jelle (strength 9) took the job, broke the nearest rock (2 stone into
    the store), then a rock_c (4), then a boulder - its 12 stone left as rubble where it stood, a load lifted;
  - a fresh map, a boulder's spot explored, a house site painted on it in play mode: its card "1 boulder to break
    first - a stonecutter's work"; with the hut placed, the stonecutter broke that boulder FIRST, before the rock
    nearer his hut; its rubble on the next plot (the site's is no open ground), 7 stone left after his load, its card
    "Broken from a rock, spring 2"; the house went up;
  - that run recorded (3,483 ticks) and replayed twice: every tick's hash equal, nothing changed between ticks;
  - saved and loaded: the boulder still broken, the rubble still there;
  - a hut placed in play mode: "It takes 8 wood, 5 stone".

## As built: step 4 (2026-10-14)

Deposits. Files: `Deposits.h`/`.cpp` (new: `Deposit`, `DepositData`, `BuildDeposits`, the `DEPOSIT_*` tuning),
`ChasmWorld.h` (`deposits`; built in `RegenerateGrid` after the terrain, before the mesh and the forest),
`TerrainMesh.*` (the band: `COLUMN_MINERAL`, `WallColumn` told the face's lower plot, `num_seam_triangles`),
`Forest.*` (`PROP_OUTCROP` and `PROP_FLOATSTONE` appended - the worker's `outcrop` and `floatstone_a`; outcrops where
the pockets' rock clusters stood, a crystal at each seam), `Economy.h` (`NOTICE_SEAM_SEEN`, `SeamSeen`),
`ApplicationChasm.h` and `ApplicationChasmExplore.cpp` (`seam_seen`, `LookForSeams`), `ApplicationChasmTime.cpp` (the
notice's words), `ApplicationChasmEconomy.cpp` (`chasm_economy` op `deposits`, the notice's kind); the wiki's
`deposit.md` and `floatstone.md` (new), `stone.md` and `exploration.md`.

- **Generated, part of `ChasmWorld`**: deposit 0 is the mountain, then the seams by balcony, then the outcrops by plot,
  each roll a `Mix` of the seed with a salt of its own. Never saved; the same seed gives the same deposits.
- **A seam** (`DEPOSIT_SEAM`). A balcony's FOOT is its plots with a higher plot across a grid EDGE - a wall face over
  them; the mesh draws walls on cells' edges, never across a diagonal. A plot with walls on opposite sides, as on a
  crack's floor one plot across, is no foot. Each balcony with a foot of `DEPOSIT_SEAM_PLOTS` or more rolls
  `DEPOSIT_SEAM_CHANCE`; its run starts at the foot plot with the lowest roll and grows by the foot plot next to it
  nearest the start, **along one wall only** - a plot joins if its way to its wall is within about 45 degrees of the
  start's (`SEAM_SAME_WALL`) - with up to 8 starts tried. `wall` is the higher plots across those edges, `band` the
  stratum just above the foot. The first version grew round a notch's corner and showed a plot's worth of band from
  any side, and put one seam in a crack; the one-wall rule fixed both.
- **The band** (`WallColumn`): a face whose lower plot is a seam's and whose stratum is its band takes a cell of the
  minerals row (`PAL_MINERALS`), by a hash of the face - mostly the middle two, the odd light one and glint - so it
  reads as facets. **The crystal**: `floatstone_a` at 1.7 to 2.1, on the run's plot nearest its middle, a fifth of
  the way to its nearest wall plot.
- **The mountain** (`DEPOSIT_MOUNTAIN`): its foot is every plateau plot off the mountain, not wet, with a mountain
  corner among its cells'. What a mine there wants of the ground is step 5's - see Open.
- **Outcrops** (`DEPOSIT_OUTCROP`): pocket plots flat all round (the forest's rule) and not touching the mountain,
  `DEPOSIT_OUTCROP_CHANCE` each, none within `DEPOSIT_OUTCROP_APART` of an earlier one. The forest stands `outcrop` on
  the plot's point at 0.9 to 1.1 and nothing else there; the pockets' rock clusters are gone. Not a rock: the
  stonecutter leaves it.
- **Found by exploring**: band and crystal are drawn from the start, under the clouds. A seam is SEEN once any of its
  foot plots is explored - from the rim, the air over a balcony is what is looked at. `LookForSeams` runs after every
  look that cleared something and tells the one who looked: "... has seen floatstone in a balcony's wall", in amber
  with the other notices. Which seams are seen is not saved: after a reset or a load it is worked out again from the
  exploration, telling nothing.
- **Not yet, for the mine**: what has been dug from a deposit (saved, hashed), and a worked-out seam drawn as rock
  again - nothing can dig, so nothing could test either.
- **Measured** (debug, seed 12): 30 balconies, 9 seams, 208 triangles of crystal; 15 outcrops, 7 and 8 in the two
  pockets; the mountain's foot 556 plots, the ground behind it rising a median 0.1, more than 1.5 on 64; built in 1.2
  ms.
- **Tested** (debug, minimised, port 8769):
  - seed 12 generated twice: every deposit the same, field for field;
  - looked at from the chasm's side: the band across a notch's back wall, purple-blue facets, the crystal at its
    foot among the rubble; an outcrop's beds of rock on a pocket's meadow; all 32 prop kinds loaded;
  - Jelle sent to the rim plot above seam 6, 118 from the camp: on the way, at tick 4,580, a `seam_seen` notice for
    him (read through the tool; the words on screen not looked at), and seam 6 two plots explored; that run recorded
    (4,867 ticks) and
    replayed twice: every tick's hash equal, nothing changed between ticks, the same notice at the same tick in both;
  - saved and loaded after it: no notice told again;
  - in play mode after the load, the seam clear of clouds.
- **Found while testing, not changed**: the play camera's orbit point settles on a balcony whether or not it is
  explored (`ViewGroundHeight`), so zoomed in over an unexplored balcony the camera sits under the clouds' lid and
  the balcony shows - a seam, rubble and all. For the user: treating unexplored ground as `ViewGroundHeight` treats
  the floor, in play mode, would keep the point on the rim's height there.

## As built: the third round (2026-10-15)

The user's tuning after seeing step 4 (Decided, the third round). Files: `Economy.h`/`.cpp` (`PlaceRubble` on balconies
only, the seam's crystal piles, `RUBBLE_FLOATSTONE_MIN`/`_MAX`, `EconomyCrateCrystal`; the rim's constants gone),
`Deposits.h`/`.cpp` (seams never under the mountain), `Forest.*` (the seam's crystal prop, `PROP_FLOATSTONE`, gone
again), `ApplicationChasm.*` and `ApplicationChasmCrates.cpp` (`floatstone_b` for a crystal pile, `CrateLook`),
`ApplicationChasmSelect.cpp` (its outlines, its card), `ApplicationChasmEconomy.cpp` (a seam's `piles` in op
`deposits`); the wiki's `stone.md`, `floatstone.md`, `deposit.md` and `crate.md`.

- **No rubble on the rim**: `PlaceRubble` looks at balcony plots only. The first stone needs a winch now.
- **A seam's crystal, lying**: every plot of a seam's foot gets a pile, whatever the wall's roll, of
  `RUBBLE_FLOATSTONE_MIN` to `_MAX` floatstone instead of stone. It is rubble (`CRATE_RUBBLE`), so everything a pile
  does it does: builders carry it off as their last errand on their land, first with PICK UP from anywhere. A pile
  holding floatstone (`EconomyCrateCrystal`) is drawn with the worker's `floatstone_b` at `CHASM_CRYSTAL_SCALE` and
  outlined the same; its card is titled "Floatstone", "Fallen from a seam in the wall". The fixed crystal at a
  seam's middle is gone - the piles are the crystal now; the band stays to show where the seam runs.
- **Seams off the mountain**: four of seed 12's nine seams lay on balconies under the mountain, where nobody walks,
  builds or puts a crate down - so they had no piles, and could never have had a mine; they were the purple bands on
  the grey walls the user saw. A foot plot is now off the mountain and dry; `balconies` counts those off it.
- **Measured** (debug, seed 12): 17 balconies off the mountain, 5 seams; rubble 97 piles, every one on a balcony -
  77 of stone (340), 20 of floatstone (45).
- **Tested** (debug, minimised, port 8769):
  - seed 12: every rubble pile on a balcony (97 of 97, checked plot by plot), each seam's four plots with a crystal
    pile of 1 to 3;
  - a crystal pile's card: "Floatstone", "Fallen from a seam in the wall"; drawn as crystal heaps along the band's
    foot, the stone rubble beside them grey;
  - a store and a builders' hut at the camp and a winch on the rim over seam 4 - seam 3's rim was "too steep" for
    one on all four of its plots - and one crystal pile put PICK UP: Jelle walked out, went down the winch, lifted it
    and carried it up into the store, 1 floatstone stored; recorded (12,745 ticks) and replayed twice: every tick's
    hash equal, nothing changed between ticks, the floatstone stored at the end of both. The builder was kept
    watered by a recorded `set_needs` every so often: unwatered, he died of thirst on the way back (see Open).

## As built: PICK UP's reach (2026-10-15)

The user's answer to the builder who died of thirst: "If we have a metric for the amount of days he can walk without
water, we can refuse it on distance alone. With quite a safety margin." Files: `Economy.h` (`ECONOMY_PICK_UP_WATER`,
`EconomyCrate::f_too_far`, `WalkSeconds`, `PickUpLook`), `Economy.cpp` (`MoveCrate`, `WalkSeconds`, `Reset`),
`ApplicationChasmSelect.cpp` (the card's line), `ApplicationChasmEconomy.cpp` (`too_far` on each crate in the tool);
the wiki's `crate.md`, `stone.md` and `floatstone.md`.

- **The metric is walking time**: the walk from where the builder stands to the crate plus the crate's walk on to the
  store that would take it, each by the route he would take (`WalkSeconds`: `RouteTo`'s route, its length over its
  speeds - so roads, fields and a winch count as walked). Past `ECONOMY_PICK_UP_WATER` of a full water bar - half of
  `NEEDS_WATER_FULL`, 2 days, about 144 units of open ground there and on to the store - he turns it down and looks
  at the next. Only PICK UP is held to it: a crate's ordinary errand is on his own land, where it never applied.
  "Distance alone" as the user said: what water he has left is not asked; the margin is for that.
- **The card**: a crate put PICK UP that a builder turned down at his last look (`f_too_far`), with nobody on the way,
  says "Too far to fetch: over 2 days there and to a store", in amber. The mark is for the card only - not saved,
  not hashed, read by no rule - so after a load it shows again at the next look. A crate with no way to it at all is
  marked the same: it is out of reach either way.
- **Remembered, not worked out twice a second** (`PickUpLook`): an idle builder looks every `ECONOMY_LOOK_TICKS`, and
  each far crate cost two route searches a look. His verdict on a crate is kept while he stands where he did, the
  zones' version is the same and the same store would take it; any change and it is worked out again - to the same
  answer, so the remembered one never changes an outcome, and it is not saved. This also covers half of the Open
  item on a picked-up crate nobody can reach: once a store can be walked to from it, its failed search is kept too.
- **Tested** (debug, seed 12, minimised, port 8769): a store and a builders' hut at the camp, a winch over seam 4;
  three crates put PICK UP: one of 3 stone dropped 50 from the store, one 121 from it, and seam 4's crystal, about
  140 away and down the winch. Jelle fetched the near one - 3 stone stored - then, home again, turned the other two
  down: both marked too far, both cards "Too far to fetch: over 2 days there and to a store", nobody on the way.
  That run recorded (4,369 ticks) and replayed twice, with the verdicts remembered: every tick's hash equal, nothing
  changed between ticks; at the end of both, 3 stone stored, the near crate gone, the far one and the crystal lying.

## As built: step 5 (2026-10-15)

The mine. Files: `Zones.h`/`.cpp` (`ZONE_KIND_MINE` 14: its name, one storey, stoppable, no house body,
`MineIsBuildable`; a drag makes one a plot), `Deposits.h`/`.cpp` (the mountain's foot marked in `of_plot`;
`DepositDigWay`), `Economy.h`/`.cpp` (`WORKER_JOB_MINER` 9 by strength; `ECONOMY_MINE_SECONDS`, `_STONE`, `_FACE`;
`dug` by deposit, saved and hashed; `EconomyDepositLeft`, `GoDig`, the dig in `TickWorker`, `DugOut`, `MineCarrying`;
no place once dug out; its stone in `StoreyGoods`; a store beside it takes stone and floatstone), `ChasmSave.cpp`
(`"dug"`), `TerrainMesh.*` (a seam's faces into `TerrainSeamBand`, crystal and rock), `ApplicationChasmMines.cpp`
(new: the props, and the bands shown by what is left), `ApplicationChasm.*` (the tool on **O**, the props loaded,
`PropHidden` keeps an outcrop under its mine in sight), `ApplicationChasmSelect.cpp` (names, the miner's doings and
pick badge, the mine's card), `ApplicationChasmIssues.cpp` (NEEDS STONE, MINE: WORKED OUT, NOBODY AT THE MINE),
`ApplicationChasmTime.cpp` (the bar's MINE, MINERS), `ApplicationChasmWorkersView.cpp` (he swings while digging),
`BuildingMesh.cpp`, `ApplicationChasmEconomy.cpp` (`dug` and `left` in op `deposits`, the hash); the wiki's `mine.md`
(new), `deposit.md`, `floatstone.md`, `stone.md` and `stonecutter.md`.

- **Where** (`MineIsBuildable`): on the plot's deposit (`DepositData::At`, which now marks the mountain's foot too) -
  a seam's foot, the mountain's foot, the plateau's gentle foot included (the user's, 2026-10-15), or an outcrop.
  The rock it digs into is left out of the ground rule - a seam's higher corners and the balcony's lip behind it,
  the mountain's own corners - and the rest is its own level, dry, off the mountain, within `ZONE_RISE_HOUSE`, with no
  field; never a winch's landing. Refused: "no seam in this wall" (a wall's foot with no deposit), "a mine is dug
  into a seam's wall, the mountain or an outcrop" (anywhere else), "a mine is one plot".
- **The miner, simpler than the plan's "fills like a collector"**: nothing waits at the mine. From home (`GoDig`),
  while its deposit has some left and a store of his takes the load whole, he walks to the rock -
  `ECONOMY_MINE_FACE` before it along `DepositDigWay` - digs `ECONOMY_MINE_SECONDS` a unit at his pace, at most what
  is left, and the load comes off the deposit (`dug`) into his hands and goes to the nearest store that takes it
  (`deliver`: a store, else he holds it). He swings at it as the stonecutter does.
- **Dug out**: at home with nothing left, `GoDig` calls `DugOut` - the mine's place goes and `AssignJobs` frees him.
  Not while he carries its last load: an ended job takes its load with it (`AssignJobs`' "workplace gone"), and the
  first run lost the seam's last five that way. `MineCarrying` keeps the place until he has set it down, here and
  in `Derive`.
- **The band** (`TerrainSeamBand`): a seam's faces kept out of the terrain's chunks, twice - crystal, and the
  stratum's own rock - the view showing one by `EconomyDepositLeft`. Dug out, the rock: nothing rebuilt.
- **The look** (`UploadMines`): `mine_portal` at `MINE_PORTAL_TOWARD` along the dig way, its -Z into the rock, and
  `mine_cart` out on its rails; on an outcrop `quarry_crane` beside it, its boom over the beds.
- **NEEDS ATTENTION**: a site short of stone - more wanted than the stores and the rubble on the colony's land hold -
  "<KIND>: NEEDS STONE", pointing at the first link missing: a winch to the rubble, then a stonecutter's hut (unless
  that is the site), else rubble further out to pick up; "MINE: WORKED OUT"; "NOBODY AT THE MINE".
- **Not done**: a spoil heap; stone and floatstone carried as a sack, as the crops are; the plan's "holds a load and
  a little more" (it keeps nothing); NEEDS STONE pointing at a pile (a row points at a building or a tool).
- **Tested** (debug, seed 14 - no seed's new colony has a seam on its land until a winch reaches one; seed 14's are
  37 to 46 from its camp - minimised, ports 8769 and 8770):
  - the rule: a mine on the plateau by the camp refused, "a mine is dug into a seam's wall, the mountain or an
    outcrop"; on the winch's landing, "a winch lands there"; on seam 6's next foot plot it stood;
  - a water collector, a store at the camp (three storeys, floatstone only - one plot's room filled with the
    camp's wood, and the miner rightly waited), a winch over seam 6 and the mine: Kaat (strength 9) took it, walked
    out, went down the winch, dug, and carried 5 floatstone up to the store, a round about every 34 s at speed 3;
    15 stored, 35 left, saved;
  - that save with 40 dug and the store emptied, loaded and recorded: two more loads, the seam at 50 dug and 0
    left, the last 5 carried in (10 stored) before the miner was freed; the mine's place gone, NEEDS ATTENTION
    "MINE: WORKED OUT", its card "A floatstone seam - worked out: nothing is left to dig"; saved and loaded: 50 dug;
  - on a fresh instance (2026-10-16) with the last-load fix: recorded (11,602 ticks) and replayed twice - every
    tick's hash equal, nothing changed between ticks, both ending at 50 dug, 10 stored, no miner. (Earlier tries
    were spoiled by the user browsing seeds in the test windows: a seed key regenerates the map mid-replay.)
  - the dug-out seam seen from its balcony: the band gone to the wall's rock, the portal against it, mouth out, its
    cart on the rails, the seam's crystal piles still lying (no builders in the test);
  - seed 6, whose camp's land reaches the mountain's foot 34 away (no seed of 1-16 but 6 and 7 has it on its land at
    the start): the nearest foot plot taken at once, a store for stone at the camp, the miner dug three loads of 5,
    the mountain's `left` "endless" throughout; a mine on an outcrop accepted, its crane over the beds.
- **The mountain's portal** (found in that screenshot): the asset's rock round its frame is the walls' temperate
  strata, brown against the mountain's grey - the props' note foresaw it. `UploadMines` makes a copy of the portal
  with those cells (columns 5, 6, 8) moved to the frozen row for mines against the mountain. On the plateau's gentle
  foot the portal stands as a block a little out from the slope - "a portal into the slope", as decided.
- **In play** (found 2026-10-24 - every test above was debug, which builds over a crate and loses it): a seam's foot has a
  floatstone pile on every plot, and play refuses a crate's plot, so a mine on a seam waits for one plot's pile to be
  picked up. Kept as the rule; the ghost now shows it - yellow, "FLOATSTONE LIES THERE: PICK IT UP FIRST" - and the mine,
  which had no ghost at all, is its plot's tile (`crates_plan.md`, "As built: the ghost goes yellow over a crate").

## As built: PICK UP by anyone free (2026-10-18)

The user, testing: "the pickup command on a crate is only done by builders. This should be any villager that is idle
ready to pick up work." Idle here is what it is everywhere else in the economy: no job (`WORKER_JOB_NONE`) - the
people who carry for sites and lay roads. Files: `Economy.cpp` (`TickWorker`'s gate, `MoveCrate`'s header, the
`PickUpLook` cap), `Economy.h` (the comments), `ApplicationChasmSelect.cpp` (the card's line),
`ApplicationChasmEconomy.cpp` and `ApplicationChasm.h` (the tool's words, comments); the wiki's `crate.md`,
`stone.md`, `floatstone.md`, `construction.md` and `person.md`.

- **Who**: a builder as before - after the towers and provisions - and now anyone without a job, at the same place
  in his look: after his house's errands, before carrying for a site. So a PICK UP takes the jobless off building
  for as long as it lasts, as it takes the builders; it is the player's order. Children, babies, mothers with a
  baby and the posted never go - each is stopped earlier in the look.
- **Unchanged**: an UNFLAGGED crate is still the builders' last errand alone (crates_plan.md, the user's); the reach
  (`ECONOMY_PICK_UP_WATER`), the store in his own settlement, others' loads on the way counted off.
- **The card**: "Nobody free to pick it up: no builders, and everyone has a job", in amber, while set with no builder
  and nobody jobless and unposted. It used to say "No builders to pick it up".
- **The remembered verdicts** (`PickUpLook`) were emptied past 64; with every jobless person keeping one for each of
  the four crates he looks at, a colony of a dozen idle would have emptied it every look and planned two routes a
  crate each time. The cap is now 64 plus four a person.
- **Tested** (debug, seed 12 fresh, minimised, port 8769): seven jobless at the camp, no builders' hut; a store
  painted by the camp and two crates of 3 stone put down about 20 from it (98 and 99):
  - 1,500 ticks with neither flagged: nobody went to either;
  - crate 98 put PICK UP: Bente Veen, no job, set out at her next look, lifted it whole and carried it into the store
    (3 stone); nobody else went, one load being all of it; crate 99, unflagged, lay untouched for the rest of the run;
  - that run recorded (3,701 ticks) and replayed twice with the start-of-tick check: every tick's hash equal, nothing
    changed between ticks, both ending with 98 gone, 99 lying, the store at 3 stone and 6 wood as recorded.
  - Not tried in the game: the card's "Nobody free" line (a colony with nobody jobless), and a jobless person turning
    a crate down as too far - the same `MoveCrate` code as a builder's, nothing in it asks his job.
