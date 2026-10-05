#ifndef _CHASM_PALETTE_H_
#define _CHASM_PALETTE_H_

#include "type_vec2.h"

/*
    THE PALETTE: every colour in Chasm is a cell of one texture, assets/textures/palette.png
    (README.md, "No textures: one palette"). A vertex picks its colour with its UV, so generated
    geometry needs no texture work and everything draws with one material.

    32 x 16 cells. A ROW is a biome - so a biome is a row change and nothing else - and a COLUMN is
    a material within it. The PNG is the source and is edited by hand; tools/make_palette.py only
    wrote the first one, and must agree with the columns below if it is ever re-run.

    Sampled at cell centres with nearest filtering and no mipmaps (Texture::Create2D), so a cell is
    exactly one colour at any distance. Row 0 is the TOP of the image: the engine reads images V=0
    at the top row (core/Primitives.h, MakeQuad's note).
*/

#define PALETTE_COLS        32
#define PALETTE_ROWS        16

//Rows: biomes.
#define PAL_TEMPERATE       0
#define PAL_DESERT          1
#define PAL_FROZEN          2
#define PAL_SWAMP           3
//Not a biome: colours that are the same in every one - the chasm's mist and the falls' foam (Mist.h).
#define PAL_EFFECTS         15
#define PAL_MIST_LIGHT      0       //columns in the effects row
#define PAL_MIST            1
#define PAL_MIST_DARK       2
#define PAL_FOAM            3
#define PAL_SWAMP_MIST      4       //the swamp's low haze: a pale grey-green, quieter than foam

//Columns: materials.
#define PAL_GRASS_0         0       //four close shades, picked per triangle for the faceted look
#define PAL_GRASS_COUNT     4
#define PAL_LIP             4       //the plateau's edge along a cliff top
#define PAL_ROCK_0          5       //four strata
#define PAL_ROCK_COUNT      4
#define PAL_FLOOR           9
#define PAL_FLOOR_DARK      10      //chasm floor at the foot of a wall
#define PAL_EARTH           11      //the map's cut edge (the skirt)
#define PAL_FIELD           12
#define PAL_PATH            13
#define PAL_WATER           14
#define PAL_HAZE            15      //the background
//Props - trees, rocks, bushes - modelled in art_source/chasm/chasm_props.blend, UV'd into these.
#define PAL_PINE_DARK       16
#define PAL_PINE            17
#define PAL_PINE_LIGHT      18
#define PAL_LEAF_DARK       19
#define PAL_LEAF            20
#define PAL_LEAF_LIGHT      21
#define PAL_BARK            22
#define PAL_BARK_DARK       23
#define PAL_STONE_LIGHT     24
#define PAL_STONE           25
#define PAL_STONE_DARK      26
#define PAL_BUSH            27
#define PAL_ACCENT          28      //flowers, berries - a spot of colour
//Buildings.
#define PAL_WALL            29      //plaster
#define PAL_ROOF            30
#define PAL_TIMBER          31

inline vec2 PaletteUV(int column, int row){
    return vec2(((float)column + 0.5f) / PALETTE_COLS,((float)row + 0.5f) / PALETTE_ROWS);
}

#endif
