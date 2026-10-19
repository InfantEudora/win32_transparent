#ifndef _CHASM_MUD_CRACKS_H_
#define _CHASM_MUD_CRACKS_H_

#include <stdint.h>
#include <vector>
#include "type_vec2.h"

/*
    THE CHASM FROM CRACKED MUD (GRID_KIND_CRACKS): testfx's chasm_cracks.frag moved into the generator, to see
    in the game what the shader's shape makes of a map - the terrain only, no rivers, mountain or biomes.

    The ground is Voronoi PLATES, each decided whole by its seed. The ground was cut once, closed; then every
    plate east of a wandering spine slid east and every one west of it slid west, so the two walls match like a
    torn sheet. Plates straddling the spine stayed where they were, a step down, as islands; some whole plates
    on the rim slumped two steps (balconies); pieces of rim plates broke away (fringe); borders between plates
    that went the same way opened into side cracks, wider and deeper nearer the chasm.

    A function of position and seed alone, sampled once per plot by Terrain::BuildHeights, which then joins
    saddles and fills pits as on any map - so a crack thin enough to fall apart into separate plots goes. The
    tuning is the user's from the bench (2026-10-18), named after the shader's uniforms in MudCracks.cpp; the
    hash and noise are ChasmLayout.cpp's and the shader's, bit for bit. Pixel for pixel it is not the bench's
    picture - a GPU rounds division its own way, and the shader's map follows its window's shape - but the same
    settings give the same shape, and the same seed the same bits in every build.
*/
class MudCracks{
public:
    //For the map rectangle lo..hi (x, z); north is -z, and the rift comes in from the south (+z) edge.
    MudCracks(uint32_t seed, const vec2& lo, const vec2& hi);

    //Steps below the plateau at a world point (x, z): 0 the plateau, TERRAIN_FLOOR_STEPS the floor.
    int Steps(const vec2& world) const;

private:
    struct Plate{
        vec2 c;         //its seed, in map space, before anything moved
        int grp = 0;    //which way it went (MudCracks.cpp, G_*)
    };
    struct Vor{
        vec2 c;         //the nearest seed
        int id[2] = {0,0};
        int grp = 0;
        float b = 1e9f;         //distance to the nearest border
        int nid[2] = {0,0};     //the cell across that border
        float b_rim = 1e9f;     //distance to the nearest border with a plate of another group: the rim
    };

    uint32_t seed;
    //The shader's map space is x east from the west edge, y north from the south edge.
    float west;
    float south;
    float width;
    float depth;
    //The plates every point of the map can reach, made once: each is asked for dozens of times per plot.
    int plate_x0 = 0;
    int plate_y0 = 0;
    int plate_w = 0;
    int plate_h = 0;
    std::vector<Plate> plates;

    uint32_t Salt(uint32_t k) const { return seed * 0x9E3779B1u + k; }
    float SpineX(float y) const;
    float OpenFull(float y) const;
    vec2 Warp(const vec2& q) const;
    vec2 SeedOf(int ix, int iy, float size, uint32_t salt) const;
    int CellGroup(const vec2& c, int ix, int iy) const;
    Plate MakePlate(int ix, int iy) const;
    Plate PlateAt(int ix, int iy) const;
    Vor Voronoi(const vec2& w, bool f_plates) const;
    bool OnRim(int ix, int iy, const vec2& c, int grp) const;
    uint32_t EdgeHash(const int a[2], const int b[2], uint32_t k) const;
};

#endif
