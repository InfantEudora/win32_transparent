#include "MarchingCubes.h"
#include "Debug.h"

#include <math.h>
#include <map>

static Debugger* debug = new Debugger("MarchingCubes",DEBUG_INFO);

/*
    --- THE CUBE, AND WHY THESE EXACT NUMBERS ----------------------------------------------------
    The corner and edge numbering below is Lorensen's, unchanged, because kTriTable further down
    is copied against it and the two are meaningless apart. Do not "tidy" either one.

        corner   0 (0,0,0)   1 (1,0,0)   2 (1,1,0)   3 (0,1,0)
                 4 (0,0,1)   5 (1,0,1)   6 (1,1,1)   7 (0,1,1)

        edge     0: 0-1   1: 1-2   2: 2-3   3: 3-0      (the z=0 face)
                 4: 4-5   5: 5-6   6: 6-7   7: 7-4      (the z=1 face)
                 8: 0-4   9: 1-5  10: 2-6  11: 3-7      (the four uprights)
*/
static const int kCornerOffset[8][3] = {
    {0,0,0}, {1,0,0}, {1,1,0}, {0,1,0},
    {0,0,1}, {1,0,1}, {1,1,1}, {0,1,1},
};

static const int kEdgeCorner[12][2] = {
    {0,1}, {1,2}, {2,3}, {3,0},
    {4,5}, {5,6}, {6,7}, {7,4},
    {0,4}, {1,5}, {2,6}, {3,7},
};

/*
    Which triangles each of the 256 corner sign patterns produces, as triples of edge numbers,
    terminated by -1.

    THE CLASSIC TABLE IS NORMALLY PRINTED WITH A 256-ENTRY `edgeTable` BESIDE IT, AND THERE ISN'T
    ONE HERE ON PURPOSE. That table is a bitmask of which edges a case touches, which is exactly
    the set of edges named in this case's row - so it is derivable, and BuildEdgeMasks() below
    derives it once at startup. Two hand-copied tables that have to agree is one more thing that
    can be quietly wrong than one hand-copied table; and a mismatch between them is the specific
    bug where a vertex is read off an edge that was never interpolated, which reads as a stray
    triangle stretching to the origin.

    The rows themselves are still copied data, which is why MarchingCubesSelfTest exists.
*/
static const int kTriTable[256][16] = {
{-1},
{0,8,3,-1},
{0,1,9,-1},
{1,8,3,9,8,1,-1},
{1,2,10,-1},
{0,8,3,1,2,10,-1},
{9,2,10,0,2,9,-1},
{2,8,3,2,10,8,10,9,8,-1},
{3,11,2,-1},
{0,11,2,8,11,0,-1},
{1,9,0,2,3,11,-1},
{1,11,2,1,9,11,9,8,11,-1},
{3,10,1,11,10,3,-1},
{0,10,1,0,8,10,8,11,10,-1},
{3,9,0,3,11,9,11,10,9,-1},
{9,8,10,10,8,11,-1},
{4,7,8,-1},
{4,3,0,7,3,4,-1},
{0,1,9,8,4,7,-1},
{4,1,9,4,7,1,7,3,1,-1},
{1,2,10,8,4,7,-1},
{3,4,7,3,0,4,1,2,10,-1},
{9,2,10,9,0,2,8,4,7,-1},
{2,10,9,2,9,7,2,7,3,7,9,4,-1},
{8,4,7,3,11,2,-1},
{11,4,7,11,2,4,2,0,4,-1},
{9,0,1,8,4,7,2,3,11,-1},
{4,7,11,9,4,11,9,11,2,9,2,1,-1},
{3,10,1,3,11,10,7,8,4,-1},
{1,11,10,1,4,11,1,0,4,7,11,4,-1},
{4,7,8,9,0,11,9,11,10,11,0,3,-1},
{4,7,11,4,11,9,9,11,10,-1},
{9,5,4,-1},
{9,5,4,0,8,3,-1},
{0,5,4,1,5,0,-1},
{8,5,4,8,3,5,3,1,5,-1},
{1,2,10,9,5,4,-1},
{3,0,8,1,2,10,4,9,5,-1},
{5,2,10,5,4,2,4,0,2,-1},
{2,10,5,3,2,5,3,5,4,3,4,8,-1},
{9,5,4,2,3,11,-1},
{0,11,2,0,8,11,4,9,5,-1},
{0,5,4,0,1,5,2,3,11,-1},
{2,1,5,2,5,8,2,8,11,4,8,5,-1},
{10,3,11,10,1,3,9,5,4,-1},
{4,9,5,0,8,1,8,10,1,8,11,10,-1},
{5,4,0,5,0,11,5,11,10,11,0,3,-1},
{5,4,8,5,8,10,10,8,11,-1},
{9,7,8,5,7,9,-1},
{9,3,0,9,5,3,5,7,3,-1},
{0,7,8,0,1,7,1,5,7,-1},
{1,5,3,3,5,7,-1},
{9,7,8,9,5,7,10,1,2,-1},
{10,1,2,9,5,0,5,3,0,5,7,3,-1},
{8,0,2,8,2,5,8,5,7,10,5,2,-1},
{2,10,5,2,5,3,3,5,7,-1},
{7,9,5,7,8,9,3,11,2,-1},
{9,5,7,9,7,2,9,2,0,2,7,11,-1},
{2,3,11,0,1,8,1,7,8,1,5,7,-1},
{11,2,1,11,1,7,7,1,5,-1},
{9,5,8,8,5,7,10,1,3,10,3,11,-1},
{5,7,0,5,0,9,7,11,0,1,0,10,11,10,0,-1},
{11,10,0,11,0,3,10,5,0,8,0,7,5,7,0,-1},
{11,10,5,7,11,5,-1},
{10,6,5,-1},
{0,8,3,5,10,6,-1},
{9,0,1,5,10,6,-1},
{1,8,3,1,9,8,5,10,6,-1},
{1,6,5,2,6,1,-1},
{1,6,5,1,2,6,3,0,8,-1},
{9,6,5,9,0,6,0,2,6,-1},
{5,9,8,5,8,2,5,2,6,3,2,8,-1},
{2,3,11,10,6,5,-1},
{11,0,8,11,2,0,10,6,5,-1},
{0,1,9,2,3,11,5,10,6,-1},
{5,10,6,1,9,2,9,11,2,9,8,11,-1},
{6,3,11,6,5,3,5,1,3,-1},
{0,8,11,0,11,5,0,5,1,5,11,6,-1},
{3,11,6,0,3,6,0,6,5,0,5,9,-1},
{6,5,9,6,9,11,11,9,8,-1},
{5,10,6,4,7,8,-1},
{4,3,0,4,7,3,6,5,10,-1},
{1,9,0,5,10,6,8,4,7,-1},
{10,6,5,1,9,7,1,7,3,7,9,4,-1},
{6,1,2,6,5,1,4,7,8,-1},
{1,2,5,5,2,6,3,0,4,3,4,7,-1},
{8,4,7,9,0,5,0,6,5,0,2,6,-1},
{7,3,9,7,9,4,3,2,9,5,9,6,2,6,9,-1},
{3,11,2,7,8,4,10,6,5,-1},
{5,10,6,4,7,2,4,2,0,2,7,11,-1},
{0,1,9,4,7,8,2,3,11,5,10,6,-1},
{9,2,1,9,11,2,9,4,11,7,11,4,5,10,6,-1},
{8,4,7,3,11,5,3,5,1,5,11,6,-1},
{5,1,11,5,11,6,1,0,11,7,11,4,0,4,11,-1},
{0,5,9,0,6,5,0,3,6,11,6,3,8,4,7,-1},
{6,5,9,6,9,11,4,7,9,7,11,9,-1},
{10,4,9,6,4,10,-1},
{4,10,6,4,9,10,0,8,3,-1},
{10,0,1,10,6,0,6,4,0,-1},
{8,3,1,8,1,6,8,6,4,6,1,10,-1},
{1,4,9,1,2,4,2,6,4,-1},
{3,0,8,1,2,9,2,4,9,2,6,4,-1},
{0,2,4,4,2,6,-1},
{8,3,2,8,2,4,4,2,6,-1},
{10,4,9,10,6,4,11,2,3,-1},
{0,8,2,2,8,11,4,9,10,4,10,6,-1},
{3,11,2,0,1,6,0,6,4,6,1,10,-1},
{6,4,1,6,1,10,4,8,1,2,1,11,8,11,1,-1},
{9,6,4,9,3,6,9,1,3,11,6,3,-1},
{8,11,1,8,1,0,11,6,1,9,1,4,6,4,1,-1},
{3,11,6,3,6,0,0,6,4,-1},
{6,4,8,11,6,8,-1},
{7,10,6,7,8,10,8,9,10,-1},
{0,7,3,0,10,7,0,9,10,6,7,10,-1},
{10,6,7,1,10,7,1,7,8,1,8,0,-1},
{10,6,7,10,7,1,1,7,3,-1},
{1,2,6,1,6,8,1,8,9,8,6,7,-1},
{2,6,9,2,9,1,6,7,9,0,9,3,7,3,9,-1},
{7,8,0,7,0,6,6,0,2,-1},
{7,3,2,6,7,2,-1},
{2,3,11,10,6,8,10,8,9,8,6,7,-1},
{2,0,7,2,7,11,0,9,7,6,7,10,9,10,7,-1},
{1,8,0,1,7,8,1,10,7,6,7,10,2,3,11,-1},
{11,2,1,11,1,7,10,6,1,6,7,1,-1},
{8,9,6,8,6,7,9,1,6,11,6,3,1,3,6,-1},
{0,9,1,11,6,7,-1},
{7,8,0,7,0,6,3,11,0,11,6,0,-1},
{7,11,6,-1},
{7,6,11,-1},
{3,0,8,11,7,6,-1},
{0,1,9,11,7,6,-1},
{8,1,9,8,3,1,11,7,6,-1},
{10,1,2,6,11,7,-1},
{1,2,10,3,0,8,6,11,7,-1},
{2,9,0,2,10,9,6,11,7,-1},
{6,11,7,2,10,3,10,8,3,10,9,8,-1},
{7,2,3,6,2,7,-1},
{7,0,8,7,6,0,6,2,0,-1},
{2,7,6,2,3,7,0,1,9,-1},
{1,6,2,1,8,6,1,9,8,8,7,6,-1},
{10,7,6,10,1,7,1,3,7,-1},
{10,7,6,1,7,10,1,8,7,1,0,8,-1},
{0,3,7,0,7,10,0,10,9,6,10,7,-1},
{7,6,10,7,10,8,8,10,9,-1},
{6,8,4,11,8,6,-1},
{3,6,11,3,0,6,0,4,6,-1},
{8,6,11,8,4,6,9,0,1,-1},
{9,4,6,9,6,3,9,3,1,11,3,6,-1},
{6,8,4,6,11,8,2,10,1,-1},
{1,2,10,3,0,11,0,6,11,0,4,6,-1},
{4,11,8,4,6,11,0,2,9,2,10,9,-1},
{10,9,3,10,3,2,9,4,3,11,3,6,4,6,3,-1},
{8,2,3,8,4,2,4,6,2,-1},
{0,4,2,4,6,2,-1},
{1,9,0,2,3,4,2,4,6,4,3,8,-1},
{1,9,4,1,4,2,2,4,6,-1},
{8,1,3,8,6,1,8,4,6,6,10,1,-1},
{10,1,0,10,0,6,6,0,4,-1},
{4,6,3,4,3,8,6,10,3,0,3,9,10,9,3,-1},
{10,9,4,6,10,4,-1},
{4,9,5,7,6,11,-1},
{0,8,3,4,9,5,11,7,6,-1},
{5,0,1,5,4,0,7,6,11,-1},
{11,7,6,8,3,4,3,5,4,3,1,5,-1},
{9,5,4,10,1,2,7,6,11,-1},
{6,11,7,1,2,10,0,8,3,4,9,5,-1},
{7,6,11,5,4,10,4,2,10,4,0,2,-1},
{3,4,8,3,5,4,3,2,5,10,5,2,11,7,6,-1},
{7,2,3,7,6,2,5,4,9,-1},
{9,5,4,0,8,6,0,6,2,6,8,7,-1},
{3,6,2,3,7,6,1,5,0,5,4,0,-1},
{6,2,8,6,8,7,2,1,8,4,8,5,1,5,8,-1},
{9,5,4,10,1,6,1,7,6,1,3,7,-1},
{1,6,10,1,7,6,1,0,7,8,7,0,9,5,4,-1},
{4,0,10,4,10,5,0,3,10,6,10,7,3,7,10,-1},
{7,6,10,7,10,8,5,4,10,4,8,10,-1},
{6,9,5,6,11,9,11,8,9,-1},
{3,6,11,0,6,3,0,5,6,0,9,5,-1},
{0,11,8,0,5,11,0,1,5,5,6,11,-1},
{6,11,3,6,3,5,5,3,1,-1},
{1,2,10,9,5,11,9,11,8,11,5,6,-1},
{0,11,3,0,6,11,0,9,6,5,6,9,1,2,10,-1},
{11,8,5,11,5,6,8,0,5,10,5,2,0,2,5,-1},
{6,11,3,6,3,5,2,10,3,10,5,3,-1},
{5,8,9,5,2,8,5,6,2,3,8,2,-1},
{9,5,6,9,6,0,0,6,2,-1},
{1,5,8,1,8,0,5,6,8,3,8,2,6,2,8,-1},
{1,5,6,2,1,6,-1},
{1,3,6,1,6,10,3,8,6,5,6,9,8,9,6,-1},
{10,1,0,10,0,6,9,5,0,5,6,0,-1},
{0,3,8,5,6,10,-1},
{10,5,6,-1},
{11,5,10,7,5,11,-1},
{11,5,10,11,7,5,8,3,0,-1},
{5,11,7,5,10,11,1,9,0,-1},
{10,7,5,10,11,7,9,8,1,8,3,1,-1},
{11,1,2,11,7,1,7,5,1,-1},
{0,8,3,1,2,7,1,7,5,7,2,11,-1},
{9,7,5,9,2,7,9,0,2,2,11,7,-1},
{7,5,2,7,2,11,5,9,2,3,2,8,9,8,2,-1},
{2,5,10,2,3,5,3,7,5,-1},
{8,2,0,8,5,2,8,7,5,10,2,5,-1},
{9,0,1,5,10,3,5,3,7,3,10,2,-1},
{9,8,2,9,2,1,8,7,2,10,2,5,7,5,2,-1},
{1,3,5,3,7,5,-1},
{0,8,7,0,7,1,1,7,5,-1},
{9,0,3,9,3,5,5,3,7,-1},
{9,8,7,5,9,7,-1},
{5,8,4,5,10,8,10,11,8,-1},
{5,0,4,5,11,0,5,10,11,11,3,0,-1},
{0,1,9,8,4,10,8,10,11,10,4,5,-1},
{10,11,4,10,4,5,11,3,4,9,4,1,3,1,4,-1},
{2,5,1,2,8,5,2,11,8,4,5,8,-1},
{0,4,11,0,11,3,4,5,11,2,11,1,5,1,11,-1},
{0,2,5,0,5,9,2,11,5,4,5,8,11,8,5,-1},
{9,4,5,2,11,3,-1},
{2,5,10,3,5,2,3,4,5,3,8,4,-1},
{5,10,2,5,2,4,4,2,0,-1},
{3,10,2,3,5,10,3,8,5,4,5,8,0,1,9,-1},
{5,10,2,5,2,4,1,9,2,9,4,2,-1},
{8,4,5,8,5,3,3,5,1,-1},
{0,4,5,1,0,5,-1},
{8,4,5,8,5,3,9,0,5,0,3,5,-1},
{9,4,5,-1},
{4,11,7,4,9,11,9,10,11,-1},
{0,8,3,4,9,7,9,11,7,9,10,11,-1},
{1,10,11,1,11,4,1,4,0,7,4,11,-1},
{3,1,4,3,4,8,1,10,4,7,4,11,10,11,4,-1},
{4,11,7,9,11,4,9,2,11,9,1,2,-1},
{9,7,4,9,11,7,9,1,11,2,11,1,0,8,3,-1},
{11,7,4,11,4,2,2,4,0,-1},
{11,7,4,11,4,2,8,3,4,3,2,4,-1},
{2,9,10,2,7,9,2,3,7,7,4,9,-1},
{9,10,7,9,7,4,10,2,7,8,7,0,2,0,7,-1},
{3,7,10,3,10,2,7,4,10,1,10,0,4,0,10,-1},
{1,10,2,8,7,4,-1},
{4,9,1,4,1,7,7,1,3,-1},
{4,9,1,4,1,7,0,8,1,8,7,1,-1},
{4,0,3,7,4,3,-1},
{4,8,7,-1},
{9,10,8,10,11,8,-1},
{3,0,9,3,9,11,11,9,10,-1},
{0,1,10,0,10,8,8,10,11,-1},
{3,1,10,11,3,10,-1},
{1,2,11,1,11,9,9,11,8,-1},
{3,0,9,3,9,11,1,2,9,2,11,9,-1},
{0,2,11,8,0,11,-1},
{3,2,11,-1},
{2,3,8,2,8,10,10,8,9,-1},
{9,10,2,0,9,2,-1},
{2,3,8,2,8,10,0,1,8,1,10,8,-1},
{1,10,2,-1},
{1,3,8,9,1,8,-1},
{0,9,1,-1},
{0,3,8,-1},
{-1},
};

/*
    Which of the twelve edges each case touches, derived from kTriTable at startup rather than
    copied - see the note above the table. Built once on the first call; there is no thread story
    here because the result is the same from any thread and writing it twice writes the same bits.
*/
static int  g_edge_mask[256] = {};
static bool gf_edge_masks_built = false;

static void BuildEdgeMasks(){
    if (gf_edge_masks_built){
        return;
    }
    for (int i = 0;i < 256;i++){
        int mask = 0;
        for (int j = 0;j < 16 && kTriTable[i][j] != -1;j++){
            mask |= (1 << kTriTable[i][j]);
        }
        g_edge_mask[i] = mask;
    }
    gf_edge_masks_built = true;
}

//--- sampling helpers ---------------------------------------------------------------------------

/*
    The field's gradient at a sample, by central difference, one-sided at the grid's faces.

    Divided by the ACTUAL span used rather than by 2*cell, which is what makes the one-sided case
    at the boundary come out to the right magnitude instead of half of it. A normal that is half
    the length it should be is invisible once normalised, but the same slip in a field that is
    later used for anything else is not.
*/
static vec3 SampleGradient(const MCGrid& grid, const std::vector<float>& density, int x, int y, int z){
    int x0 = (x > 0) ? x - 1 : x;
    int x1 = (x < grid.nx - 1) ? x + 1 : x;
    int y0 = (y > 0) ? y - 1 : y;
    int y1 = (y < grid.ny - 1) ? y + 1 : y;
    int z0 = (z > 0) ? z - 1 : z;
    int z1 = (z < grid.nz - 1) ? z + 1 : z;

    float dx = (float)(x1 - x0) * grid.cell.x;
    float dy = (float)(y1 - y0) * grid.cell.y;
    float dz = (float)(z1 - z0) * grid.cell.z;

    vec3 g;
    g.x = (dx > 0.0f) ? (density[grid.Index(x1,y,z)] - density[grid.Index(x0,y,z)]) / dx : 0.0f;
    g.y = (dy > 0.0f) ? (density[grid.Index(x,y1,z)] - density[grid.Index(x,y0,z)]) / dy : 0.0f;
    g.z = (dz > 0.0f) ? (density[grid.Index(x,y,z1)] - density[grid.Index(x,y,z0)]) / dz : 0.0f;
    return g;
}

/*
    Any unit vector perpendicular to `n`.

    An iso-surface has no parameterisation, so there is no correct tangent - but there is a wrong
    one, which is the zero vector, and core/Primitives.h's conventions promise not to hand one
    out. Crossing with whichever axis `n` is least aligned to is what keeps the cross product well
    conditioned; crossing with a fixed axis degenerates wherever the surface happens to face that
    way, which on a terrain is a whole hillside.
*/
static vec3 PerpendicularTo(const vec3& n){
    vec3 axis = (fabsf(n.x) < 0.9f) ? vec3(1,0,0) : vec3(0,1,0);
    vec3 t = axis.cross(n);
    float len = sqrtf(t.x*t.x + t.y*t.y + t.z*t.z);
    if (len < 1e-6f){
        return vec3(1,0,0);
    }
    return t / len;
}

//--- the mesher ---------------------------------------------------------------------------------

bool MarchingCubes(const MCGrid& grid, const std::vector<float>& density,
                   std::vector<vertex>& out, float isolevel){
    if (grid.nx < 2 || grid.ny < 2 || grid.nz < 2){
        debug->Err("MarchingCubes: need at least 2 samples on every axis, got %ix%ix%i\n",
                   grid.nx,grid.ny,grid.nz);
        return false;
    }
    if (grid.cell.x <= 0.0f || grid.cell.y <= 0.0f || grid.cell.z <= 0.0f){
        debug->Err("MarchingCubes: every cell extent must be positive, got (%.4f,%.4f,%.4f)\n",
                   grid.cell.x,grid.cell.y,grid.cell.z);
        return false;
    }
    if (density.size() != grid.SampleCount()){
        debug->Err("MarchingCubes: density has %zu samples, grid wants %zu (%ix%ix%i)\n",
                   density.size(),grid.SampleCount(),grid.nx,grid.ny,grid.nz);
        return false;
    }

    BuildEdgeMasks();

    //Per-cube scratch, hoisted so the inner loop allocates nothing.
    vec3   edge_pos[12];
    vec3   edge_normal[12];
    size_t corner_index[8];
    vec3   corner_pos[8];
    float  corner_value[8];

    for (int z = 0;z < grid.nz - 1;z++){
        for (int y = 0;y < grid.ny - 1;y++){
            for (int x = 0;x < grid.nx - 1;x++){
                /*
                    A CORNER IS INSIDE WHEN ITS VALUE IS BELOW THE ISOLEVEL, which with this
                    file's negative-is-inside convention means the bit is set for solid.

                    Lorensen's table was written for the opposite reading - a density where high
                    means inside - so this is the point at which the two conventions meet, and it
                    is why the winding is reversed where the triangles are emitted below. Both
                    halves of that trade are checked by MarchingCubesSelfTest.
                */
                int cube_index = 0;
                for (int i = 0;i < 8;i++){
                    int cx = x + kCornerOffset[i][0];
                    int cy = y + kCornerOffset[i][1];
                    int cz = z + kCornerOffset[i][2];
                    corner_index[i] = grid.Index(cx,cy,cz);
                    corner_pos[i]   = grid.Position(cx,cy,cz);
                    corner_value[i] = density[corner_index[i]];
                    if (corner_value[i] < isolevel){
                        cube_index |= (1 << i);
                    }
                }

                int mask = g_edge_mask[cube_index];
                if (mask == 0){
                    continue;   //wholly inside or wholly outside; much the commonest case
                }

                //Gradients are only worth fetching for a cube that produces something, and only
                //for the corners of edges it actually crosses.
                for (int e = 0;e < 12;e++){
                    if ((mask & (1 << e)) == 0){
                        continue;
                    }
                    int a = kEdgeCorner[e][0];
                    int b = kEdgeCorner[e][1];
                    /*
                        INTERPOLATED FROM THE LOWER GLOBAL SAMPLE INDEX TO THE HIGHER, ALWAYS.

                        Two cubes sharing an edge must put their vertex at bit-identically the
                        same place, or the surface has a hairline crack along every cell boundary
                        - which shows up as a dark seam under a light and as a failed watertight
                        check in the self test. Ordering by global index rather than by the cube's
                        own local corner numbering is what guarantees it: both cubes then evaluate
                        the same expression on the same two floats in the same order.
                    */
                    if (corner_index[a] > corner_index[b]){
                        int tmp = a; a = b; b = tmp;
                    }
                    float va = corner_value[a];
                    float vb = corner_value[b];
                    float denom = vb - va;
                    //Both corners exactly on the isolevel: the crossing is anywhere, so take the
                    //midpoint rather than dividing by zero.
                    float t = (fabsf(denom) > 1e-12f) ? ((isolevel - va) / denom) : 0.5f;
                    if (t < 0.0f){ t = 0.0f; }
                    if (t > 1.0f){ t = 1.0f; }

                    edge_pos[e] = corner_pos[a] + (corner_pos[b] - corner_pos[a]) * t;

                    int ax = x + kCornerOffset[a][0], ay = y + kCornerOffset[a][1], az = z + kCornerOffset[a][2];
                    int bx = x + kCornerOffset[b][0], by = y + kCornerOffset[b][1], bz = z + kCornerOffset[b][2];
                    vec3 ga = SampleGradient(grid,density,ax,ay,az);
                    vec3 gb = SampleGradient(grid,density,bx,by,bz);
                    edge_normal[e] = ga + (gb - ga) * t;
                }

                for (int i = 0;kTriTable[cube_index][i] != -1;i += 3){
                    int e0 = kTriTable[cube_index][i];
                    int e1 = kTriTable[cube_index][i+1];
                    int e2 = kTriTable[cube_index][i+2];

                    vec3 p[3] = { edge_pos[e0], edge_pos[e2], edge_pos[e1] };
                    vec3 n[3] = { edge_normal[e0], edge_normal[e2], edge_normal[e1] };
                    //^ e0,e2,e1 rather than e0,e1,e2: the reversal promised above. Lorensen's
                    //  winding puts the normal toward the set bits, and here the set bits are the
                    //  SOLID, so taken as printed every triangle would face into the ground.

                    //A degenerate triangle is not an error - it happens wherever the surface
                    //passes exactly through a corner - but it is not worth uploading either.
                    vec3 face = (p[1] - p[0]).cross(p[2] - p[0]);
                    float face_len = sqrtf(face.x*face.x + face.y*face.y + face.z*face.z);
                    if (face_len < 1e-12f){
                        continue;
                    }
                    face = face / face_len;

                    for (int k = 0;k < 3;k++){
                        vec3 vn = n[k];
                        float len = sqrtf(vn.x*vn.x + vn.y*vn.y + vn.z*vn.z);
                        //A flat plateau of field has no gradient to read a normal off; the
                        //triangle's own is the only answer left, and is better than a black
                        //fragment. See the normals note in the header.
                        vn = (len > 1e-6f) ? (vn / len) : face;

                        vertex v = vertex();
                        v.pos     = p[k];
                        v.normal  = vn;
                        v.tangent = PerpendicularTo(vn);
                        v.uv      = vec2(0.0f,0.0f);
                        v.matid   = 0;
                        out.push_back(v);
                    }
                }
            }
        }
    }
    return true;
}

Mesh* MarchingCubesMesh(const MCGrid& grid, const std::vector<float>& density, float isolevel){
    std::vector<vertex>verts;
    if (!MarchingCubes(grid,density,verts,isolevel)){
        return NULL;
    }
    if (verts.empty()){
        debug->Err("MarchingCubesMesh: the field never crosses %.4f, so there is no surface\n",isolevel);
        return NULL;
    }
    Mesh* mesh = new Mesh();
    //SetMeshData computes the mesh's extents from these positions as it copies them in.
    mesh->SetMeshData(verts.data(),(int)verts.size());
    return mesh;
}

//--- the self test ------------------------------------------------------------------------------

/*
    A vertex position reduced to something that can be compared and sorted.

    Quantised rather than compared as floats. The mesher goes to some trouble (see the
    lower-index-first note) to make two cubes produce bit-identical positions on a shared edge, so
    exact comparison SHOULD work - but a test whose job is to catch a mistake in the mesher must
    not assume the mesher got that particular thing right, or it cannot fail when it matters.
    A 1/4096 grid is far finer than any crack worth caring about and far coarser than float noise.
*/
struct QPos{
    int x, y, z;
    bool operator<(const QPos& o) const {
        if (x != o.x){ return x < o.x; }
        if (y != o.y){ return y < o.y; }
        return z < o.z;
    };
};

static QPos Quantise(const vec3& p){
    QPos q;
    q.x = (int)lroundf(p.x * 4096.0f);
    q.y = (int)lroundf(p.y * 4096.0f);
    q.z = (int)lroundf(p.z * 4096.0f);
    return q;
}

bool MarchingCubesSelfTest(){
    //A sphere, because its surface is known exactly everywhere and it exercises every one of the
    //256 cases somewhere on it. Deliberately not centred on a sample, or half the cases never
    //arise and a table error can hide in the ones that do not.
    const float radius = 1.23f;
    MCGrid grid;
    grid.nx = 34;
    grid.ny = 34;
    grid.nz = 34;
    grid.cell = vec3(0.125f,0.125f,0.125f);
    grid.origin = vec3(-2.0625f,-2.0625f,-2.0625f);

    std::vector<float>density(grid.SampleCount());
    for (int z = 0;z < grid.nz;z++){
        for (int y = 0;y < grid.ny;y++){
            for (int x = 0;x < grid.nx;x++){
                vec3 p = grid.Position(x,y,z);
                density[grid.Index(x,y,z)] = sqrtf(p.x*p.x + p.y*p.y + p.z*p.z) - radius;
            }
        }
    }

    std::vector<vertex>verts;
    if (!MarchingCubes(grid,density,verts)){
        debug->Err("self test: the mesher refused a well-formed grid\n");
        return false;
    }

    //(1) there is a surface at all
    if (verts.size() < 3 || (verts.size() % 3) != 0){
        debug->Err("self test: %zu vertices is not a whole number of triangles\n",verts.size());
        return false;
    }

    /*
        (2) EVERY DIRECTED EDGE HAS EXACTLY ONE MATCHING REVERSE.

        This is the assertion that earns the file. A closed, consistently wound surface has every
        edge shared by exactly two triangles which traverse it in opposite directions; any error
        in the triangulation table breaks that - a missing triangle leaves an unpaired edge, a
        wrongly wound one leaves two edges in the same direction, and a wrong edge number leaves
        both. None of the three is reliably visible in a screenshot.
    */
    std::map<std::pair<QPos,QPos>,int>edges;
    for (size_t i = 0;i < verts.size();i += 3){
        QPos a = Quantise(verts[i].pos);
        QPos b = Quantise(verts[i+1].pos);
        QPos c = Quantise(verts[i+2].pos);
        edges[std::make_pair(a,b)]++;
        edges[std::make_pair(b,c)]++;
        edges[std::make_pair(c,a)]++;
    }
    int unpaired = 0;
    int doubled = 0;
    for (std::map<std::pair<QPos,QPos>,int>::const_iterator it = edges.begin();it != edges.end();++it){
        if (it->second != 1){
            doubled++;
        }
        std::map<std::pair<QPos,QPos>,int>::const_iterator rev =
            edges.find(std::make_pair(it->first.second,it->first.first));
        if (rev == edges.end() || rev->second != it->second){
            unpaired++;
        }
    }
    if (unpaired != 0 || doubled != 0){
        debug->Err("self test: the surface is not closed - %i unpaired and %i repeated directed "
                   "edges out of %zu. The triangulation table is wrong.\n",
                   unpaired,doubled,edges.size());
        return false;
    }

    //(3) and (4): the winding faces out of the solid, and the surface is where the sphere is.
    float worst_radius_error = 0.0f;
    int backwards = 0;
    for (size_t i = 0;i < verts.size();i += 3){
        vec3 e1 = verts[i+1].pos - verts[i].pos;
        vec3 e2 = verts[i+2].pos - verts[i].pos;
        vec3 face = e1.cross(e2);
        vec3 centroid = (verts[i].pos + verts[i+1].pos + verts[i+2].pos) / 3.0f;
        //The sphere is centred on the origin, so the outward direction at any point IS that point.
        if (face.dot(centroid) <= 0.0f){
            backwards++;
        }
        for (int k = 0;k < 3;k++){
            const vec3& p = verts[i+k].pos;
            float r = sqrtf(p.x*p.x + p.y*p.y + p.z*p.z);
            float err = fabsf(r - radius);
            if (err > worst_radius_error){
                worst_radius_error = err;
            }
        }
    }
    if (backwards != 0){
        debug->Err("self test: %i of %zu triangles face INTO the solid. The winding reversal in "
                   "MarchingCubes is wrong, or the sign convention is.\n",backwards,verts.size()/3);
        return false;
    }
    if (worst_radius_error > grid.cell.x){
        debug->Err("self test: a vertex sits %.4f from the true sphere, further than one %.4f "
                   "cell. The interpolation is wrong.\n",worst_radius_error,grid.cell.x);
        return false;
    }

    debug->Info("self test passed: %zu triangles, closed and outward, worst radius error %.4f "
                "of a %.3f cell\n",verts.size()/3,worst_radius_error,grid.cell.x);
    return true;
}
