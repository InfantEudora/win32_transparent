#ifndef _MARCHINGCUBES_H_
#define _MARCHINGCUBES_H_

#include "Mesh.h"
#include "type_vec3.h"
#include "type_vertex.h"

#include <vector>

/*
    Marching cubes: a sampled scalar field in, a triangle soup out.

    Written for apps/archer's terrain (see apps/archer/terrain_plan.md), but there is nothing
    about this file that knows what terrain is - it takes numbers in a box and returns the surface
    where they cross zero. apps/archer/Terrain.cpp is the half that knows about StageBlocks.

    --- THE SIGN CONVENTION, WHICH IS THE ONE THING TO GET RIGHT ---------------------------------
    NEGATIVE IS INSIDE THE SOLID, positive is outside, and the surface is where the field crosses
    `isolevel` (0 by default). That is the signed-distance convention, so a field can be written
    the way SDFs are written everywhere else and dropped straight in.

    Get it backwards and the mesh is inside-out: every triangle faces away from the camera, the
    renderer's GL_BACK culling removes all of it, and the result is an object that reports itself
    visible, in the scene, with a sane vertex count, and draws nothing. That failure looks exactly
    like a mesh that failed to build, which is why it is the first line of this comment and why
    MarchingCubesSelfTest checks it rather than trusting it.

    --- WHY A PRE-SAMPLED GRID RATHER THAN A CALLBACK --------------------------------------------
    The field arrives as a flat array of samples, like CreateMeshFromHeightmap's `heights`, not as
    a function pointer this file calls back into. Three reasons, in order of how much they matter:

      - IT MAKES THIS TESTABLE WITH NO ENGINE. MarchingCubesSelfTest fills an array with an
        analytic sphere and meshes it; there is no window, no GL and no app. That is the same
        trade apps/archer/Stage.h makes by naming no engine type, and it is worth as much here.
      - The caller can sample however it likes - in parallel, from a file, from a previous frame -
        without this file growing a threading story.
      - Gradients come for free. Smooth normals need the field's derivative at each corner, and
        with the samples in hand that is a central difference rather than six more callbacks per
        corner.

    The cost is nx*ny*nz floats held at once. For archer's test bay that is 92 KB and for the
    whole level 462 KB, so it is not a cost.

    --- NORMALS ---------------------------------------------------------------------------------
    SMOOTH, from the gradient of the field, not from the triangle. The gradient of a signed
    distance field IS the outward normal, so this is the accurate answer rather than an
    approximation of it, and it costs one central difference per cube corner.

    It does mean a deliberately sharp crease - a block's top edge, say - renders rounded over
    roughly one cell. That is a property of sampling the field at all, not of this choice: the
    crease is not in the samples to begin with. Shrink the cell or accept the round.

    Where the gradient is degenerate (a perfectly flat region of field, which happens inside a
    plateau) it falls back to the triangle's own face normal, because a zero-length normal turns
    into a black fragment and is hard to trace back here.

    --- THREAD ----------------------------------------------------------------------------------
    MarchingCubes is pure CPU and may run anywhere, including the physics thread.
    MarchingCubesMesh ends in Mesh::SetMeshData, which calls glNamedBufferData immediately, so it
    is RENDER THREAD ONLY - Application::Init or Application::PreRender, never RunSimulationTick.
    Same rule as core/Primitives.h, same reason.

    --- CONVENTIONS, matching core/Primitives.h --------------------------------------------------
      - Wound counter-clockwise seen from OUTSIDE the solid, so GL_BACK culling keeps the faces
        you can see. Verified by MarchingCubesSelfTest rather than asserted here.
      - Tangents are non-zero and perpendicular to the normal. There is no meaningful surface
        parameterisation on an iso-surface, so the direction is arbitrary but consistent.
      - uv is (0,0) and matid is 0 on every vertex. An iso-surface has no natural UV; the caller
        is expected to overwrite both if it wants them, which is exactly what Terrain.cpp does to
        split grass from soil. Use MarchingCubes (the vector form) when you intend to.
      - The mesh is a non-indexed triangle soup, three vertices per triangle, matching every other
        mesh in this engine (see the vertex-pull note in core/Mesh.h).
*/

/*
    Where the samples are and what they mean in world space.

    `nx/ny/nz` count SAMPLES, not cells - a 2x2x2 grid is one cell. Sample (0,0,0) sits at
    `origin` and sample (i,j,k) at origin + (i*cell.x, j*cell.y, k*cell.z).

    `cell` is per-axis on purpose. archer's play volume is 84 x 12 x 3, so the field barely varies
    in z and a z cell twice the size of the others halves the sample count and the vertex count
    for no visible difference. An isotropic grid would be spending most of its triangles on the
    flat front and back of a slab nobody is looking at edge-on.
*/
struct MCGrid{
    vec3 origin;
    vec3 cell = vec3(1,1,1);
    int  nx = 0;
    int  ny = 0;
    int  nz = 0;

    size_t Index(int x, int y, int z) const { return ((size_t)z * ny + y) * nx + x; };
    size_t SampleCount() const { return (size_t)nx * ny * nz; };
    vec3   Position(int x, int y, int z) const {
        return vec3(origin.x + x * cell.x, origin.y + y * cell.y, origin.z + z * cell.z);
    };
};

/*
    The mesher. `density` is grid.SampleCount() floats in grid.Index() order.

    Appends to `out` rather than clearing it, so several fields can be meshed into one buffer -
    which is what building a terrain bay out of several blocks' worth of grid would do.

    Returns false and logs on a malformed grid or a density array of the wrong length. An empty
    surface (the field never crosses the isolevel) is NOT an error and returns true having
    appended nothing; a caller that cares should check `out`.
*/
bool MarchingCubes(const MCGrid& grid, const std::vector<float>& density,
                   std::vector<vertex>& out, float isolevel = 0.0f);

/*
    The same thing, wrapped in a Mesh the caller owns - exactly like core/Primitives.h's makers,
    and with the same ownership rule: handing it to Object::SetMesh takes a reference and the
    Object's DeleteMesh drops it again.

    RENDER THREAD ONLY. Returns NULL and logs if the field produced no surface at all, because an
    empty Mesh is a thing that draws nothing and says nothing, which is the hardest kind of
    nothing to debug.
*/
Mesh* MarchingCubesMesh(const MCGrid& grid, const std::vector<float>& density,
                        float isolevel = 0.0f);

/*
    Meshes an analytic sphere and checks the result is a closed, correctly wound surface.

    This exists because the 256-entry triangulation table is copied data, and a single wrong digit
    in it produces a mesh that looks almost right - a hole a few cells wide somewhere on a
    silhouette, or one case's triangles wound backwards. Neither is reliably visible in a
    screenshot, and both are extremely annoying to find by looking at geometry.

    So it is checked the way anything else here is checked: by measurement. Four assertions, in
    increasing order of what they would catch:

      1. the surface is not empty
      2. every directed edge has exactly one matching reverse - which is what "closed and
         consistently wound" means, and is the check that a bad table row cannot survive
      3. every triangle faces away from the sphere's centre (the sign convention above)
      4. every vertex lies within one cell of the true sphere

    Costs about two milliseconds. Logs what failed and returns false; logs one line and returns
    true otherwise.
*/
bool MarchingCubesSelfTest();

#endif
