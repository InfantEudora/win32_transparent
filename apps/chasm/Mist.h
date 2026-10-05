#ifndef _CHASM_MIST_H_
#define _CHASM_MIST_H_

#include <stdint.h>
#include <vector>
#include "type_vertex.h"
#include "type_fmat4.h"
#include "Grid.h"
#include "Terrain.h"
#include "TerrainMesh.h"

/*
    THE MIST AND THE FOAM (grid_plan.md step 9): simple shapes, the way A Little Age draws its fog
    of war and archer draws its waterfall's foam - lumpy, flat-shaded puffs, packed close.

      - The MIST fills the chasm: a puff over every few floor vertices, sized and raised by hash and
        a slow noise, so the blanket's top rolls a few units either side of CHASM_MIST_TOP. The
        floor and the foot of every wall are under it; the shard and the walls stand out of it. A
        dark grey, a shade lighter on top - the sun and the walls' shadows do the rest.
      - The FOAM churns at the foot of each fall, where it meets the mist: lighter puffs that swell,
        rise and shrink away, one after another.

    Both move slowly - the mist breathes and bobs, the foam rises - and both are a function of the
    simulation's clock alone (seconds = tick * step), so nothing is stepped or kept: a paused game
    holds them still, and the same tick always looks the same. VIEW ONLY: built with the world, read
    by nothing in a tick, never saved.
*/

#define CHASM_MIST_TOP      -46.0f  //where the blanket's top rolls about
#define MIST_VARIANTS       3       //puff shapes, each in its own shade (lightest first)
#define FOAM_PER_FALL       32

struct MistPuff{
    vec3 pos;
    float radius;
    float phase;        //0..1, where in its slow cycle it starts
    float period;       //seconds
    uint8_t variant;
};

struct MistData{
    std::vector<MistPuff> puffs;
    std::vector<std::vector<int>> chunk_puffs;  //per terrain chunk, so a set is culled with its chunk
    float build_ms = 0.0f;
};

void BuildMist(const Grid& g, const Terrain& t, const TerrainMeshData& mesh, MistData& out);

//A puff of unit radius, flat-shaded, in palette cell (column, row) - `variant` picks the lumps.
void BuildPuffMesh(uint32_t variant, int column, int row, std::vector<vertex>& out);

//A mist puff's transform at `seconds`.
fmat4 MistTransform(const MistPuff& p, double seconds);

//Foam ball `slot` of fall `f` at `seconds`: where it is and its radius, 0 between lives.
void FoamBall(const TerrainFall& f, int fall_index, int slot, double seconds, vec3& pos, float& radius);

#endif
