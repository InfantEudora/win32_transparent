#ifndef _PINBALL_TABLE_BUILDER_H_
#define _PINBALL_TABLE_BUILDER_H_

#include <vector>

#include "Mesh.h"
#include "type_vec3.h"

/*
    Curves, on a table that cannot have one.

    core has no triangle-mesh collider - box, sphere, capsule and heightfield, and that is the
    whole list. So every collision surface on this machine is a primitive, and the top orbit, the
    ramp rails, the lane guides and the bumper nest all have to become CHAINS of them
    (pinball_design.md 2.1 and 2.3). Hand-placing sixty rotated boxes is not a plan.

    This is the one helper that stops that: a path goes in, and the same path drives both the
    render mesh and - from stage 1 - the chain of colliders underneath it. That is the only thing
    keeping the art and the collision surface from drifting apart, and a mismatch between them is
    precisely the bug this whole render-and-collider-are-different-objects approach invites.

    --- PATHS ------------------------------------------------------------------------------------
    A PinPath is just a std::vector<vec3> of world-space points. The Append* calls below build one
    up in place, so a curve reads as the sequence of moves that make it:

        PinPath orbit;
        AppendArc(orbit, vec3(0,0,-2.494f), 3.706f, -128.4f, -51.6f, 24);

    Angles are DEGREES, measured in the XZ plane from +X toward +Z - so -90 is straight up-table
    and +90 is straight down-table. That is the same sense as atan2(z,x), which is what a reader
    checking a coordinate will reach for.

    --- THREAD -----------------------------------------------------------------------------------
    The Make* calls end in Mesh::SetMeshData, which calls glNamedBufferData immediately. RENDER
    THREAD ONLY - Init() or PreRender(), never RunSimulationTick(). Same rule as core/Primitives.h,
    and for the same reason.

    --- OWNERSHIP --------------------------------------------------------------------------------
    Each Make* returns a `new Mesh` the caller owns, exactly like MakeBox. Handing it to
    Object::SetMesh takes a reference and the Object's DeleteMesh drops it again.
*/

typedef std::vector<vec3> PinPath;

//--- Building a path ----------------------------------------------------------------------------

//Add one point. Consecutive duplicates are dropped rather than producing a zero-length segment,
//because a zero-length segment has no direction and every join downstream would be garbage.
void AppendPoint(PinPath& path, const vec3& p);

//A convenience for the common case: a point on the deck, given as the (x,z) the design document
//quotes, at height y.
void AppendXZ(PinPath& path, float x, float z, float y = 0.0f);

/*
    A circular arc in the XZ plane, at constant height.

    `segments` is how many SEGMENTS the sweep becomes, so the number of points added is
    segments + 1 (less one if the path already ends at the arc's start). Sample it finely enough
    that the chord error stays well under the ball's radius: a 3.7-unit arc swept 77 degrees in 24
    segments has a chord sag of about 0.006 units, which is a twentieth of the ball's radius and so
    invisible both to the eye and to the ball.
*/
void AppendArc(PinPath& path, const vec3& centre, float radius,
               float start_degrees, float end_degrees, int segments);

//A quadratic Bezier through `from` -> control -> `to`, for the joins that are not circles. Cheaper
//to steer by eye than an arc when all that matters is that the curve is smooth and goes roughly
//where it is pointed - which is most of the habitrail.
void AppendBezier(PinPath& path, const vec3& from, const vec3& control, const vec3& to, int segments);

//Total length of the path in world units. Used for UVs, and useful on its own when deciding how
//many collider boxes a curve is worth.
float PathLength(const PinPath& path);

//--- Turning a path into geometry ---------------------------------------------------------------

/*
    The one primitive everything else here is made of: the rectangular cross-section below, swept
    along the path.

        half_width  horizontal half-extent, perpendicular to the path
        y_below     how far the solid extends BELOW the path's own y
        y_above     how far it extends ABOVE it

    Joins are mitred, so a curve sampled into twenty segments is one continuous solid with no
    slivers at the corners and no gap for a ball to catch on. Side normals are averaged across each
    join, so a swept curve shades smoothly rather than reading as a row of facets - which matters
    here more than usual, because most of what this builds is chrome and chrome shows every crease.

    The miter is clamped: a path that doubles back on itself would want an infinitely wide join,
    and clamping produces a slightly pinched corner instead of a spike across the table.
*/
Mesh* MakeSweptBox(const PinPath& path, float half_width, float y_below, float y_above,
                   bool f_cap_ends = true);

//A wall or guide rail standing ON the path: `thickness` wide, `height` tall, sitting on the path's
//own y. What the orbit, the lane dividers and the ramp rails are.
Mesh* MakeWallStrip(const PinPath& path, float height, float thickness, bool f_cap_ends = true);

//A flat ribbon CENTRED on the path: `width` across, `thickness` of slab hanging below it, so the
//path is the surface the ball actually rolls on. What a ramp floor is.
Mesh* MakeRibbon(const PinPath& path, float width, float thickness);

/*
    A round wire swept along the path: what a habitrail is made of, and the reason the airborne
    parts of the ramps no longer cover the deck.

    The first build drew every ramp as an opaque 0.46-wide slab from mouth to inlane, and two of
    them covered the middle third of the table. A real habitrail is two or four wires a ball rides
    between, and seen from above it hides almost nothing - which is the entire point of building
    ramps out of wire on a machine whose playfield is the art.

    `segments` is the ring's resolution; 10 is plenty for a wire this thin. Unlike MakeSweptBox
    there is no mitre - a circle cannot be mitred by scaling one axis - so a tube wants a path
    sampled finely enough that consecutive headings differ by a few degrees, which every path here
    already is.
*/
Mesh* MakeTube(const PinPath& path, float radius, int segments = 10);

//A copy of `path` shifted sideways by `amount` (positive to the LEFT of travel) and up by `rise`.
//How one ramp centreline becomes its rails and its wires without anyone typing the rails out.
PinPath OffsetPath(const PinPath& path, float amount, float rise = 0.0f);

#endif
