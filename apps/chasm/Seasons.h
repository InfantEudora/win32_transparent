#ifndef _CHASM_SEASONS_H_
#define _CHASM_SEASONS_H_

#include <stddef.h>
#include <stdint.h>

/*
    THE BROADLEAVES THROUGH THE YEAR (docs/seasons_plan.md, step 2): every oak and bush goes through
    STAGES, each on days of its own from a hash of the prop, so a forest turns over several days
    rather than at a stroke. View only - a function of the calendar's tick, like the snow - so
    nothing is saved and the rules never ask.

    A stage is which MESH draws the prop (ApplicationChasmSeasons.cpp), not a shader's doing: the
    sun's shadow map is drawn with core's own depth program, so a canopy a shader hid would still
    cast its shadow. Every stage's leaves sit on a fixed palette row - green on PAL_SUMMER, never on
    the temperate row the seasons fade - so a tree changes only when its own day comes.
*/
#define FOREST_STAGE_GREEN      0   //summer's leaves
#define FOREST_STAGE_SPRING     1   //a bush's new leaves
#define FOREST_STAGE_GOLD       2
#define FOREST_STAGE_RED        3   //autumn's second colour, for a share of them
#define FOREST_STAGE_THIN_GOLD  4   //an oak losing its leaves: part of its canopy left
#define FOREST_STAGE_THIN_RED   5
#define FOREST_STAGE_BARE       6   //an oak in winter: the modelled bare oak
#define FOREST_STAGE_BUD        7   //an oak in spring: bare, with a thin canopy of new leaves
#define FOREST_STAGE_WINTER     8   //a bush in winter: its leaves dried
#define FOREST_STAGES           9

#define FOREST_SEASONAL_NONE    0
#define FOREST_SEASONAL_OAK     1
#define FOREST_SEASONAL_BUSH    2
//What a prop kind does through the year: FOREST_SEASONAL_*.
int ForestSeasonal(int kind);
//The modelled bare oak of each oak kind, PROP_OAK_A on, in chasm_props.glb (blender_chasm_props.py).
#define FOREST_OAK_KINDS        3
extern const char* forest_bare_names[FOREST_OAK_KINDS];
//The FRUIT drawn on a ripe plant (foraging_plan.md): a mesh for each kind that bears, in chasm_props.glb
//(blender_chasm_props.py), drawn over the plant at its own transform.
#define FOREST_FRUIT_KINDS      5
extern const char* forest_fruit_names[FOREST_FRUIT_KINDS];
//...which of them is a prop kind's, -1 for a kind that bears none; and whether a plant at `stage` shows it: not an
//oak thinning or bare, whose canopy the fruit would hang outside of - the rules' fruit is there all the same.
int ForestFruitMesh(int kind);
bool ForestFruitShows(int stage);
//The stage prop `prop` (an index into ForestData::props) of `kind` is at on `tick`; GREEN for a kind
//that has none.
int ForestStage(int kind, size_t prop, uint64_t tick);
const char* ForestStageName(int stage);

#endif
