# Seasons, drawn

Agreed with the user 2026-10-08. Until now the only sign of the year turning was winter's snow
(`gameplay_plan.md`, P1): spring, summer and autumn looked the same. This plan gives each season its
own colours and lets the broadleaf trees turn, drop their leaves and bud again.

**All of it is view.** Every stage below is a function of the calendar's tick (`Calendar.h`), like the
snow, so nothing is saved or hashed, replays are untouched, and the rules never read it.

## Decided (the user, 2026-10-08)

- **The seasons happen everywhere at once.** Not from the north like the snow: the snow cover is kept
  for something else later - making winter's temperature differ between the biomes.
- **Grass fades smoothly** from one season's colours to the next; **leaves change tree by tree**, each
  on a day of its own, so a forest turns gradually.
- **Bare trees are modelled in Blender** (`art_source/chasm/chasm_props.blend`), like the props they
  belong to - props are placed freely and work differently from the generated buildings that follow
  the tiles.

## The pieces

### Seasonal palette rows

The palette (`Palette.h`, `assets/textures/palette.png`) gets a row per season beside the temperate
one, each a full copy of it with the seasonal columns changed - so any cell can be retuned for a
season in a paint program, with no code:

| Row | Season | Grass | Leaves (for the trees' own stages) |
|---|---|---|---|
| 0, temperate | summer | as today | as today |
| 4 | spring | fresh, brighter | pale new green (budding) |
| 5 | autumn | yellowed, olive | gold and orange |
| 6 | autumn, red | as row 5 | red and russet - a second autumn colour, tree by tree |
| 7 | winter | dull grey-green, where no snow lies | dried brown: a bush's in winter |
| 8, summer kept | - | - | the temperate row AS LOADED, written there by the app (the PNG's row is spare): the broadleaves' green, which must never fade |

Pines keep their colours (row 7 may darken them a shade). The desert's and the swamp's rows do not
change with the seasons yet; the swamp freezing in winter is its own open item (`gameplay_plan.md`).

### Grass and ground: a smooth fade

The app rewrites the palette texture's **temperate row on the GPU** as a blend of two season rows:
the season before and the one now, eased over the season's first 4 days (`GROUND_SEASON_FADE_DAYS`,
tuning). Everything drawn on the temperate row - grass, ground, pines, gardens and hedges - changes
colour with it, at no cost per pixel. The leaf columns keep summer's, for the crops that borrow them
(`CropMesh`); a ripe crop's colour is the bush column's, so it does fade. The PNG on disk is
untouched. Snow still wins over all of it: the ground shader moves snowy cells to the frozen row as
before, and now lets snow lie on the season rows too, for leaves still on a tree when it comes.

*Built first as a blend in the ground shader: that sampled the palette twice a fragment and cost
about 15% of the colour pass (4.99 to 5.75 ms, seed 12 at play zoom, debug) - where the row rewrite
costs a 128 KB upload a few times a second during a fade, and nothing otherwise.*

**The first spring has no winter before it**, so it starts on spring's colours rather than fading in
from winter's - as it starts with no snow.

### Flowers

The flowers in the ground cover show in spring and summer and are gone in autumn and winter.

### Broadleaf trees and bushes: a stage each, plant by plant (built)

The oaks (`tree_oak_a/b/c`) and the bushes (`bush_a/b`, `shrub_a`) go through stages, each on days of
its own from a hash of the prop, so a forest turns over several days rather than at a stroke
(`Seasons.h`, `ApplicationChasmSeasons.cpp`; days counted from autumn's or spring's first dawn, all
current tuning):

| Stage | Oaks | Bushes | The mesh |
|---|---|---|---|
| green | summer, until it turns | the same | as modelled, its leaves on row 8 |
| turning | from autumn day 0-5 | the same | leaves on row 5 - or row 6 for 35%, red |
| thinning | 2.5 days after turning | - | 45% of the leaf triangles kept, by each triangle's hash |
| bare | 4.5 days after turning, all winter | - | the modelled bare oak |
| dried | - | winter day 0-3 on | leaves on row 7 |
| budding | spring day 1-5, for 4 days | - | bare, plus 35% of the canopy on row 4 |
| spring | - | spring day 0-4 to summer day 0-3 | leaves on row 4 |

**The first spring starts in leaf**, as it starts with no snow.

Seed 12, measured 2026-10-08: a few trees turned on autumn's second day, most gold or red by the
fourth, gaps opening by the sixth, nearly all bare by the eighth; budding through spring. Every stage
of every kind is its own mesh (36) and so its own set per chunk - 65 sets a chunk where there were
29, nearly all empty at any one time: the colour pass, the shadow map and the frame were the same in
summer, autumn and winter (1.9, 2.4 and 16.7 ms at play zoom, debug). A chunk's sets are rebuilt
when one of its props has moved on a stage, looked at four times a game day.

**Why meshes and not the shader:** the sun's shadow map is drawn with core's own depth program, not
with a custom one, so leaves a shader dropped in the colour pass would still cast their full shadow.
A tree's stage is therefore which mesh draws it. The turning and thinning meshes are made from the
modelled oak when it loads (its leaf UVs moved to another row; part of its leaf triangles left
out); the bare one is modelled. The forest is already drawn as sets per chunk and kind, rebuilt when a
tree is felled - a stage change rebuilds the same way.

## Steps - all three built 2026-10-08

1. **Palette rows, the grass fade, flowers in season.** The rows in `palette.png` (first colours by
   code, then the user's to tune), the temperate row's season blend, the flowers hidden out of season.
2. **Stages per plant**: green, turning (two colours), thinning, dried, spring and budding - the
   per-plant timing and the sets' rebuild. Without a bare model in the file an oak stands as its
   trunk alone in winter (and says so in the log).
3. **Bare oaks, modelled**: `tree_oak_a_bare`, `_b_bare`, `_c_bare` in `chasm_props.blend`, built by
   `apps/chasm/tools/blender_chasm_props.py --add` (`bare_broadleaf`): the leafy oak's TRUNK copied
   from the .blend as it stands, so the two match whatever is edited, and in place of the crown four
   heavy limbs (three on the slim oak C) leaving the trunk at different heights and reaching out to
   about the canopy's edge, each with a side branch and a twig - 82-106 triangles, bark-coloured. The
   user tunes or remodels them in Blender and re-exports; the game only needs the names.

## Ice (2026-10-08)

The user, after a recording where Nora the water carrier sat at home with no water to carry: her collector
was under the late snow, and a collector fills only off the snow (Economy.cpp) - a rule nothing showed.
Now the water freezes where the snow lies, and the collector's card says why it gives nothing.

- **The view** (`shaders/chasm_water.glsl`, ICE; uniforms from `SetIceUniforms`, ApplicationChasmSnow.cpp):
  north of the snow's front, with the ground's own ragged edge formula for formula, the rivers and pools
  are still ice - flat PLATES (a jittered-cell pattern, about three units across) in three pale blues, dark
  seams between, a few plates snowed over, and snow drifted onto it along the banks. Smooth noise was tried
  first and read as a blur; plates match the terrain's low-poly look. The FALLS freeze too, their streaks
  stopped and paled into a sheet of ice, so no frozen river pours over the rim, and a frozen fall churns
  no foam at its foot. No snow on the map: the water's own path, unchanged.
- **The card**: a water collector under the snow says "Frozen: no water can be collected until the thaw",
  from `EconomyFrozen` - the economy's own test at the collector's first plot, so the card and the rule
  cannot disagree. The ice's ragged edge can stray a few units either side of the rule's line, as the
  snow on the ground does.
- Checked on `chasm_water_nora`: frozen at its start (front at z -25, the collector at -46), the line gone
  1100 ticks on (front at -58), when Nora is fetching again.

## Crossing the ice (the user's, 2026-10-12)

"Make water crossable in winter, only when instructed by posting." The ice was already there, drawn and in the
rules (above), but nobody could walk it.

- **The posted cross frozen water**: someone under an order, a guard. Villagers going about their work do not,
  and neither does a settlement's territory (`MapFlood`). Frozen is the line the ice is drawn by and a collector
  stops at (`UnderSnow`): `Walkers::SetSeason`, set from the tick before a walk is planned.
- **An order onto the water itself is refused** ("on the water"): the thaw would leave him standing in the river.
- **The thaw is a risk, with a warning** (the user's choice over refusing an icy route near the thaw). A walk
  planned over the ice keeps it if the thaw comes on the way; one still across afterwards is cut off until a bridge
  or the next winter, and waits as anyone cut off from home does. The warnings:
  - the walk tip, before the right click: "41 units to walk - over the ice, it thaws in 6 days";
  - the person's card, while posted with no way home but over the ice: "The way home is over the ice - it thaws
    in about 6 days", red on its last day (`Economy::IceHome`, `ice_home`, looked at each second;
    `EconomyIceDays` steps a day at a time to the first one the snow is gone from the plot).
- **The walk tip plans the posted walk** for anyone selected, since a right click posts them: it showed "No way
  there" over the ice, and over a castle's decks, where the order would go.
- **Cut off, said so**: someone waiting "at home" away from the house's door - a river, a wall, or the ice gone from
  under the way back - showed "At his tent"; the card now says "Cut off from home, looking for a way back"
  (`Doing`, `CARD_CUT_OFF_DISTANCE`). A camp's people at their tents read "At the tent".
- *Checked* on `camp_3` (debug, a copied exe, paused and stepped): in winter (day 34) a posted route found the way
  over the lake north of the settlement in 28 plots, a villager's none. An order onto the water: "on the water".
  Marit ordered over it walked out on the ice; from her first steps her card read "The way home is over the ice -
  it thaws in about 8 days", 7 at her post, 1 on day 41; on day 43 it had thawed, the line was gone, and released
  she stayed on the far bank: "Cut off from home, looking for a way back". Bente at home still read "At home".
  Not looked at on screen: the walk tip's wording (it needs the mouse, and a minimised window draws no overlay).
