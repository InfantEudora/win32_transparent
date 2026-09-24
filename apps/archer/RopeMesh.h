#ifndef _ARCHER_ROPE_MESH_H_
#define _ARCHER_ROPE_MESH_H_

#include <vector>

#include "type_vec3.h"
#include "type_int3.h"
#include "type_vertex.h"

/*
    The rope, as one skinned mesh over the chain of rigid links rp3d swings - vine_plan.md step 4.

    The chain in ApplicationArcher::BuildRope is untouched: N links hanging from an anchor, jointed
    end to end. This builds what is DRAWN over it - the twisted middle deformed along the rope and
    the ring, collars and tassel set on it, all in one mesh, with every vertex weighted to the
    links it lies between. The app gives it one Bone per link and copies each link's transform
    onto its bone every tick; the skinning shader does the rest.

    --- THE BIND POSE ---------------------------------------------------------------------------
    Straight down from the anchor, which is how BuildRope lays the chain every time it builds it -
    on a restart too. So the bind never changes, and the mesh is built ONCE (render thread, GL)
    while the chain under it is rebuilt freely on the physics thread. Link i's bind transform is
    no rotation and its centre at anchor - (0, seg * (i + 0.5), 0), seg = length / links.

    --- THE WEIGHTS -----------------------------------------------------------------------------
    Each vertex is weighted by its distance down the rope, s, through a QUADRATIC B-SPLINE over the
    link centres: three links at most, summing to 1, the middle one 0.75 at a link's centre. That is
    what makes a chain of rigid boxes read as a rope. Two-link linear weights pass through every
    link centre but leave a crease at each one; the B-spline cuts the corners and the result is
    smooth wherever the chain bends. Past the ends the weights clamp: the top of the rope is wholly
    the first link, which pivots about the anchor, so it stays on the anchor; the tassel is wholly
    the last link and swings with it rigidly.

    No engine type beyond core's maths, so `make rules` builds and checks it with no GPU.
*/

struct RopeMeshInput{
    vec3  anchor;               //the top of the rope, world
    float length = 1.0f;        //down from the anchor
    int   links = 1;            //the rope's dynamic links, top first; link i is bone i

    //The twisted middle: a tile in SplineDeform's convention (along +Z, cross-section round the
    //axis, matching end rings), `tile_scale` to world. Required.
    const std::vector<vertex>* tile = NULL;
    float tile_scale = 1.0f;

    /*
        The pieces, each optional (NULL), each with its own scale to world, all in the rope's
        frame: +Z runs DOWN the rope, +Y toward the camera, so a piece authored like the tile sits
        on it the way it was modelled.
          ring    origin where it meets the rope's top, the ring above it along -Z
          collar  centred on the rope, placed at each distance in collar_at
          tassel  origin where it meets the rope's end, hanging below it along +Z
    */
    const std::vector<vertex>* ring = NULL;
    float ring_scale = 1.0f;
    const std::vector<vertex>* collar = NULL;
    float collar_scale = 1.0f;
    std::vector<float> collar_at;
    const std::vector<vertex>* tassel = NULL;
    float tassel_scale = 1.0f;
};

//Link i's centre in the bind pose.
vec3  RopeLinkBindCentre(const RopeMeshInput& in, int link);

//The weights for a point `s` down the rope: up to three links and their weights, summing to 1.
//Unused slots are link 0 at weight 0.
void  RopeWeights(float s, float seg, int links, int3& bones, vec3& weights);

//The whole rope in its bind pose, a triangle list for Mesh::SetSkinnedMeshData. False (and `out`
//empty) without a usable tile.
bool  BuildRopeMesh(const RopeMeshInput& in, std::vector<skinned_vertex>& out);

#endif
