#ifndef _SPLINE_H_
#define _SPLINE_H_

#include <vector>
#include "type_vec3.h"

/*
    A 3D curve through a list of points, with a frame along it that does not flip.

    Built for sweeping things along - a vine's trunk, a rope, leaves at intervals - and for moving
    things along a path in time. See apps/archer/vine_plan.md §2 for why it is shaped like this.

    --- WHAT IT IS ------------------------------------------------------------------------------
      * CUBIC HERMITE SEGMENTS, one per pair of neighbouring points, with the tangents picked by
        CENTRIPETAL Catmull-Rom (alpha 0.5). The curve passes through every point, and centripetal
        is the parameterisation that cannot cusp or loop between two points set close together -
        uniform Catmull-Rom does, and a hand-typed path always has a pair like that somewhere. The
        two ends get phantom points reflected through the end, so the curve leaves them straight.
      * AN ARC-LENGTH TABLE, because everything that places things along a curve asks by DISTANCE
        ("a leaf every 0.3") and a cubic's parameter is not distance. Positions are evaluated
        exactly at the parameter the table maps a distance to, never lerped out of the table.
      * ROTATION-MINIMISING FRAMES (double reflection, Wang et al. 2008), carried along the table.
        Not Frenet: a Frenet normal points at the centre of curvature, so it flips at every
        inflection and is undefined on a straight run - a helix wrapped around an S-bend would
        jump to the other side of the trunk at the middle. A rotation-minimising frame turns only
        as much as the tangent forces it to.

    The frame's axes line up with a tile authored along +Z: `side` is local X, `normal` local Y,
    `tangent` local Z, and they are right-handed in that order (side = normal x tangent), so a mesh
    mapped through them keeps its winding.

    Only core's maths types - no GL, no Object - so a test can link it with nothing else.
*/

struct SplineFrame{
    vec3 position;
    vec3 tangent;       //unit, along the curve
    vec3 normal;        //unit, rotation-minimising; starts as `up` projected off the tangent
    vec3 side;          //normal x tangent

    //A point in the frame's plane, `x` along side and `y` along normal.
    vec3 Place(float x, float y) const { return position + side * x + normal * y; };
    //A direction given in (side, normal, tangent) coordinates, into the world.
    vec3 Rotate(const vec3& v) const { return side * v.x + normal * v.y + tangent * v.z; };
};

class Spline{
public:
    std::vector<vec3> points;
    //Which way the first frame's normal points, as nearly as the first tangent allows. A vine
    //lying on the ground wants +Y so the top of its tile faces up.
    vec3  up = vec3(0.0f,1.0f,0.0f);
    //0.5 is centripetal, 0 uniform, 1 chordal. Leave it at 0.5 unless there is a reason.
    float alpha = 0.5f;

    /*
        Fits the segments and fills the distance table. Call after changing `points`, `up` or
        `alpha`; nothing below is valid until it returns true. `sample_step` is the table's spacing
        in world units - it sets how closely a distance lands on the curve and how finely the frame
        is carried round a bend. False with fewer than two points.
    */
    bool  Build(float sample_step = 0.02f);
    bool  IsBuilt() const { return !segments.empty(); };

    int   GetNumSegments() const { return (int)segments.size(); };
    float GetLength() const { return length; };

    //--- By parameter: u in [0, GetNumSegments()], the integer part choosing the segment ----------
    vec3  Evaluate(float u) const;
    vec3  Derivative(float u) const;        //d/du, not unit length

    //--- By distance: s in [0, GetLength()], clamped ----------------------------------------------
    float ParamAtDistance(float s) const;
    float DistanceAtParam(float u) const;
    vec3  PositionAt(float s) const;
    vec3  TangentAt(float s) const;
    SplineFrame FrameAt(float s) const;

    //The distance along the curve of the point on it nearest to `p` - a hand finding the rope.
    float ClosestDistance(const vec3& p) const;

    //One cubic Hermite segment at t in [0,1], and its derivative. Public because a keyframe track
    //with in/out tangents (glTF CUBICSPLINE) is exactly this and has no need of the rest.
    static vec3 Hermite(const vec3& p0, const vec3& m0, const vec3& p1, const vec3& m1, float t);
    static vec3 HermiteDerivative(const vec3& p0, const vec3& m0, const vec3& p1, const vec3& m1, float t);

private:
    struct Segment{
        vec3 p0, m0, p1, m1;
    };
    struct Sample{
        float u = 0.0f;
        float s = 0.0f;
        vec3  position;
        vec3  tangent;
        vec3  normal;
    };
    std::vector<Segment> segments;
    std::vector<Sample>  samples;
    float length = 0.0f;

    //The sample at or before distance s, so s lies in [samples[i].s, samples[i+1].s].
    size_t SampleBefore(float s) const;
    size_t SampleBeforeParam(float u) const;
};

#endif
