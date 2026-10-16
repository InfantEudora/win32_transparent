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
/*
    The SEASONS' rows (docs/seasons_plan.md): each a full copy of the temperate row - which is summer -
    with its seasonal columns changed, so any cell can be retuned for a season in the PNG. The ground
    shader fades temperate cells between them; the broadleaf trees' stages move their leaves onto them.
*/
#define PAL_SPRING          4
#define PAL_AUTUMN          5
#define PAL_AUTUMN_RED      6       //autumn's second leaf colour, tree by tree; its ground is row 5's
#define PAL_WINTER          7       //where no snow lies
//Summer KEPT: the temperate row as loaded, written here by the app (the PNG's row is spare) and never
//faded - the broadleaves' green stage (Seasons.h), which changes only on each tree's own day.
#define PAL_SUMMER          8
//Not a biome either: crops' own colours, the same all year - no season fades them and no snow lies on them
//(chasm_ground.frag), since a field is empty by winter. The first are hemp's (goods_plan.md); goods made from
//crops have theirs here too.
#define PAL_CROPS           9
#define PAL_HEMP            0       //columns in the crops row: ripe hemp's stalks, an olive green
#define PAL_HEMP_HEADS      1       //and its crown, fan leaves and seed heads - a yellow-green, not wheat's gold
#define PAL_ROPE            2       //hemp rope at a ropewalk: a greyed tan, paler than timber, duller than straw
//Not a biome either: what is dug, the same in every biome and season and never snowed on (stone_plan.md), so a
//pile of it or a seam in a wall reads at once. The props (chasm_props.blend) are UV'd straight into it.
#define PAL_MINERALS        10
#define PAL_FLOATSTONE_LIGHT 0      //columns in the minerals row: floatstone's crystal, a purple-blue, light...
#define PAL_FLOATSTONE      1
#define PAL_FLOATSTONE_DARK 2       //...to dark
#define PAL_FLOATSTONE_GLINT 3      //a near-white lilac, for the odd facet catching the light
//Not a biome: colours that are the same in every one - the chasm's mist and the falls' foam (Mist.h).
#define PAL_EFFECTS         15
#define PAL_MIST_LIGHT      0       //columns in the effects row
#define PAL_MIST            1
#define PAL_MIST_DARK       2
#define PAL_FOAM            3
#define PAL_SWAMP_MIST      4       //the swamp's low haze: a pale grey-green, quieter than foam
#define PAL_VOID            5       //black: the chasm floor under the mist, which nothing uses (TerrainMesh.cpp)
#define PAL_WINDOW_LIT      6       //a warm house's window panes, its fire behind them (needs_plan.md step 3)
#define PAL_SMOKE           7       //chimney smoke: a light grey, between the mist and the foam

//Columns: materials.
#define PAL_GRASS_0         0       //four close shades, picked per triangle for the faceted look
#define PAL_GRASS_COUNT     4
#define PAL_LIP             4       //the plateau's edge along a cliff top
#define PAL_ROCK_0          5       //four strata
#define PAL_ROCK_COUNT      4
#define PAL_FLOOR           9       //unused since the floor became the void (PAL_VOID)
#define PAL_FLOOR_DARK      10
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
