#ifndef _PRIMITIVES_H_
#define _PRIMITIVES_H_

#include "Mesh.h"
#include "type_vec3.h"

/*
    Procedural primitive meshes - backlog item 16.

    Before this, the only cube in the engine was data/unit_cube.obj, and anything that wanted one
    in code hand-wrote it (ApplicationShip::BuildVolumeCube is 45 lines for exactly one cube). So
    a debug marker, a placeholder, a collider visualisation or a ground plane all needed either an
    asset file on disk or its own pile of vertex arithmetic.

    --- CONVENTIONS, all five the same ---------------------------------------------------------
      - CENTRED on the object's origin. A box of size (2,2,2) spans -1..+1 on every axis, and a
        cylinder of height 2 spans y -1..+1. That is what makes the matching collider a plain
        AddBoxCollider(size * 0.5f, vec3(), ...) with no offset to remember.
      - SIZES ARE FULL EXTENTS, not radii-style halves - except `radius`, which is a radius.
      - +Y IS THE AXIS OF REVOLUTION for sphere, cylinder and cone, matching the engine's Y-up
        world and AddCapsuleCollider (whose height is its Y axis too).
      - WOUND COUNTER-CLOCKWISE SEEN FROM OUTSIDE, so the renderer's default GL_BACK culling
        removes the faces pointing away. Every quad here is built from a corner plus two edge
        vectors u,v with cross(u,v) == the outward normal, which is what guarantees that.
      - NORMALS are smooth where the surface is (sphere all over, the curved flank of a cylinder
        or cone) and flat where it is (box faces, end caps), so a cylinder's silhouette rounds
        off while its lid stays a crisp edge.
      - TANGENTS point along +u (increasing azimuth for the curved surfaces), so normal mapping
        and anything else reading the tangent gets something sensible rather than a zero vector.
      - matid is 0 on every vertex, which is material slot 0 of whatever Object holds the mesh.

    --- THREAD ---------------------------------------------------------------------------------
    RENDER THREAD ONLY. These all end in Mesh::SetMeshData, which calls glNamedBufferData
    immediately (Mesh.cpp:51) - so Application::Init or Application::PreRender are fine, and
    RunSimulationTick is not: it runs on the physics thread, which may not touch GL.

    --- OWNERSHIP ------------------------------------------------------------------------------
    Each returns a `new Mesh` the caller owns, exactly like CreateMeshFromHeightmap. Handing it to
    Object::SetMesh takes a reference, and the Object's DeleteMesh drops it again.

    Every function returns NULL and logs on nonsense arguments rather than building a broken mesh.
*/

//Axis-aligned box. `size` is the full extent on each axis. UVs run 0..1 per face, so a
//non-cubic box has stretched texturing - bake a repeat into the material if that matters.
Mesh* MakeBox(const vec3& size);

//Flat rectangle in the XY plane facing +Z, the same orientation as a glyph or a billboard: x
//right, y up, normal at the viewer. Rotate the Object by -90 degrees about X to lay it down as
//a floor. Two triangles.
Mesh* MakeQuad(float width, float height);

//UV sphere. `segments` divides the azimuth (around +Y), `rings` divides pole to pole, so the
//triangle count is roughly segments * rings * 2 - the top and bottom rows are triangles rather
//than quads because a UV sphere's pole rows collapse to a point. Minimum 3 and 2.
Mesh* MakeSphere(float radius, int segments = 24, int rings = 12);

//Cylinder along +Y, spanning -height/2 .. +height/2. Smooth around the flank, flat end caps.
//`caps` off leaves an open tube, which is what you want when it is only ever seen from outside.
Mesh* MakeCylinder(float radius, float height, int segments = 24, bool caps = true);

//Cone along +Y: base of `radius` at -height/2, apex at +height/2. The flank normals are the
//true slant normals (radius and height both feed into them, so a squat cone is not shaded like
//a tall one), and each triangle's apex vertex takes the normal of its own segment's middle,
//which is the usual dodge for the fact that a cone tip has no single normal.
Mesh* MakeCone(float radius, float height, int segments = 24, bool cap = true);

#endif
