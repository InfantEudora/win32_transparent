#ifndef _CHASM_GARDENS_H_
#define _CHASM_GARDENS_H_

#include <stdint.h>
#include <vector>
#include "ChasmWorld.h"
#include "Zones.h"

/*
    A GARDEN'S PLANTING (docs/gardens_plan.md, the user's 2026-10-26): flowers along its walls, a bush here and there
    once it is more than one plot, and a cypress where it fills a cell - one a coarse cell at most.

    The view's alone: decided, like the forest, by a hash of the place, and never saved. And decided PER FINE QUAD, from
    that quad's four plots alone - a plot's change rebuilds the chunks of the quads round it (Zones::Touch) and no
    others, so planting that looked further afield could be left stale in a chunk nobody rebuilt. "A large garden" falls
    out of that: a quad whose four plots are all garden is the inside of one at least two plots by two.
*/

//A garden that STANDS: painted, built, nothing built on it - what is drawn as a lawn (ZoneMesh.cpp).
bool GardenStands(const ZoneState& z, int v);

//One of the forest's plants set in a garden: drawn in the forest's sets (RebuildPropChunk) as a prop of its kind is.
struct GardenPlant{
    int kind = 0;           //PropKind: flowers, a fern, a shrub, a bush
    vec3 pos;
    float yaw = 0.0f;
    float scale = 1.0f;
    uint32_t seed = 0;      //its own, in place of the index a forest prop is told apart by (its season's day)
};
//The plants of the garden quarters in `fine_quad`, added to `out`.
void GardenQuadPlants(const ChasmWorld& w, const ZoneState& z, int fine_quad, std::vector<GardenPlant>& out);

//The CYPRESS in the middle of a quad whose four plots are one garden on one ground (MeshCypress) - the one quad of its
//coarse cell that may have one - unless a tree or bush the garden kept stands on one of them: false for none.
bool GardenQuadCypress(const ChasmWorld& w, const ZoneState& z, int fine_quad, vec3* foot, float* tall, uint32_t* seed);

#endif
