#include "TableBuilder.h"

#include "Debug.h"
#include "type_helpers.h"

#include <math.h>

static Debugger* debug = new Debugger("TableBuilder",DEBUG_INFO);

//Two points closer than this are the same point. Generous on purpose: the cost of dropping a
//genuinely distinct point 1 mm from its neighbour is nothing, and the cost of keeping a
//near-duplicate is a segment whose direction is numerical noise.
#define PIN_PATH_EPSILON        0.0005f

//--- Building a path ----------------------------------------------------------------------------

void AppendPoint(PinPath& path, const vec3& p){
    if (!path.empty()){
        vec3 d = p - path.back();
        if (d.length() < PIN_PATH_EPSILON){
            return;
        }
    }
    path.push_back(p);
}

void AppendXZ(PinPath& path, float x, float z, float y){
    AppendPoint(path,vec3(x,y,z));
}

void AppendArc(PinPath& path, const vec3& centre, float radius,
               float start_degrees, float end_degrees, int segments){
    if (radius <= 0.0f || segments < 1){
        debug->Err("AppendArc: radius %.3f and %i segments make no arc\n",radius,segments);
        return;
    }
    float a0 = toradians(start_degrees);
    float a1 = toradians(end_degrees);
    for (int i = 0; i <= segments; i++){
        float t = (float)i / (float)segments;
        float a = a0 + (a1 - a0) * t;
        AppendPoint(path,vec3(centre.x + cosf(a) * radius,
                              centre.y,
                              centre.z + sinf(a) * radius));
    }
}

void AppendBezier(PinPath& path, const vec3& from, const vec3& control, const vec3& to, int segments){
    if (segments < 1){
        debug->Err("AppendBezier: %i segments make no curve\n",segments);
        return;
    }
    for (int i = 0; i <= segments; i++){
        float t = (float)i / (float)segments;
        float u = 1.0f - t;
        //The standard quadratic form, written out rather than looped, because three terms is
        //shorter than the loop that would generalise it.
        AppendPoint(path, from * (u * u) + control * (2.0f * u * t) + to * (t * t));
    }
}

float PathLength(const PinPath& path){
    float total = 0.0f;
    for (size_t i = 1; i < path.size(); i++){
        total += (path[i] - path[i - 1]).length();
    }
    return total;
}

//--- Sweeping -----------------------------------------------------------------------------------

static vertex MakeVertex(const vec3& pos, const vec3& normal, const vec3& tangent, const vec2& uv){
    vertex v = vertex();
    v.pos     = pos;
    v.normal  = normal;
    v.tangent = tangent;
    v.uv      = uv;
    v.matid   = 0;      //slot 0 of whichever Object ends up holding this mesh
    return v;
}

//Corners in counter-clockwise order seen from OUTSIDE the solid, split along a->c. The same
//convention core/Primitives.cpp uses, and the reason every quad below is written as a corner plus
//two edges whose cross product is the outward normal.
static void PushQuad(std::vector<vertex>& out,
                     const vertex& a, const vertex& b, const vertex& c, const vertex& d){
    out.push_back(a); out.push_back(b); out.push_back(c);
    out.push_back(a); out.push_back(c); out.push_back(d);
}

/*
    One cross-section of the swept solid, at one point along the path.

    `left` is the horizontal perpendicular, already mitred - so it is NOT unit length at a corner,
    it is unit length divided by the cosine of the half-angle, which is exactly what keeps the
    solid a constant width around a bend. `normal` is the unit version of the same direction, kept
    separately because shading wants the direction and the offset wants the length.
*/
struct PinStation{
    vec3 corner[4];     //0: -left/bottom  1: +left/bottom  2: +left/top  3: -left/top
    vec3 normal;        //unit horizontal perpendicular, pointing +left
    vec3 tangent;       //unit path direction
    float distance;     //arc length from the start of the path, for the U coordinate
};

Mesh* MakeSweptBox(const PinPath& path, float half_width, float y_below, float y_above,
                   bool f_cap_ends){
    if (path.size() < 2){
        debug->Err("MakeSweptBox: a path of %zu points is not a path\n",path.size());
        return NULL;
    }
    if (half_width <= 0.0f || (y_below + y_above) <= 0.0f){
        debug->Err("MakeSweptBox: cross-section %.3f x %.3f has no area\n",
                   half_width * 2.0f, y_below + y_above);
        return NULL;
    }

    const int n = (int)path.size();
    const vec3 up = vec3(0,1,0);

    //--- Per-segment directions, then per-station mitres ----------------------------------------
    std::vector<vec3> seg_dir(n - 1);
    std::vector<vec3> seg_left(n - 1);
    for (int i = 0; i < n - 1; i++){
        vec3 d = path[i + 1] - path[i];
        seg_dir[i] = vec3(d).normalize();
        /*
            The perpendicular is taken in the HORIZONTAL plane, deliberately, even where the path
            climbs: a ramp rail stands vertically out of its floor, it does not lean outward as the
            ramp rises. Flattening d first is what gives that. A path that went straight up would
            have no horizontal direction at all and is caught below.
        */
        vec3 flat = vec3(d.x,0.0f,d.z);
        if (flat.length() < PIN_PATH_EPSILON){
            debug->Err("MakeSweptBox: segment %i is vertical; a swept wall needs a heading\n",i);
            return NULL;
        }
        flat.normalize();
        seg_left[i] = up.cross(flat);       //unit, horizontal, 90 degrees to the left of travel
    }

    std::vector<PinStation> stations(n);
    float distance = 0.0f;
    for (int i = 0; i < n; i++){
        //A station between two segments takes the average of both; the ends take their one.
        vec3 left_a = seg_left[max(i - 1,0)];
        vec3 left_b = seg_left[min(i,n - 2)];
        vec3 mitre  = left_a + left_b;
        if (mitre.length() < PIN_PATH_EPSILON){
            //The path doubles back on itself. There is no sane join here; keep going straight
            //rather than emitting a degenerate cross-section, and say so, because it is almost
            //certainly a typo in the path rather than a shape anybody wanted.
            debug->Err("MakeSweptBox: path reverses at point %i; join will be wrong\n",i);
            mitre = left_b;
        }
        mitre.normalize();

        /*
            Miter scale: the offset has to grow by 1/cos(half-angle) or the solid pinches on the
            inside of every bend. Clamped at 3, because a hairpin would otherwise throw a spike
            right across the table - a pinched corner is a cosmetic flaw, a spike is a wall in the
            middle of the playfield.
        */
        float cosine = mitre.dot(left_b);
        float scale  = (cosine > 0.34f) ? (1.0f / cosine) : 3.0f;

        PinStation& s = stations[i];
        s.normal  = mitre;
        s.tangent = seg_dir[min(i,n - 2)];
        if (i > 0){
            distance += (path[i] - path[i - 1]).length();
        }
        s.distance = distance;

        vec3 offset = mitre * (half_width * scale);
        vec3 below  = up * y_below;
        vec3 above  = up * y_above;
        s.corner[0] = path[i] - offset - below;
        s.corner[1] = path[i] + offset - below;
        s.corner[2] = path[i] + offset + above;
        s.corner[3] = path[i] - offset + above;
    }

    //--- Emit ------------------------------------------------------------------------------------
    std::vector<vertex> verts;
    verts.reserve((size_t)(n - 1) * 24 + 12);

    const float height = y_below + y_above;
    for (int i = 0; i < n - 1; i++){
        const PinStation& a = stations[i];
        const PinStation& b = stations[i + 1];
        //V runs across the cross-section in world units, so a texture applied to a rail and to a
        //ramp floor is at the same scale on both without anyone tuning a repeat factor.
        const float ua = a.distance;
        const float ub = b.distance;

        //-left flank. Outward normal is -normal, and the normal is taken PER STATION so the
        //flank shades smoothly around a curve instead of faceting.
        PushQuad(verts,
            MakeVertex(a.corner[0],-a.normal,a.tangent,vec2(ua,0.0f)),
            MakeVertex(b.corner[0],-b.normal,b.tangent,vec2(ub,0.0f)),
            MakeVertex(b.corner[3],-b.normal,b.tangent,vec2(ub,height)),
            MakeVertex(a.corner[3],-a.normal,a.tangent,vec2(ua,height)));

        //+left flank.
        PushQuad(verts,
            MakeVertex(a.corner[1],a.normal,a.tangent,vec2(ua,0.0f)),
            MakeVertex(a.corner[2],a.normal,a.tangent,vec2(ua,height)),
            MakeVertex(b.corner[2],b.normal,b.tangent,vec2(ub,height)),
            MakeVertex(b.corner[1],b.normal,b.tangent,vec2(ub,0.0f)));

        //Top. Flat-shaded across the width on purpose - the crease where a rail's top meets its
        //flank is a real edge and should stay crisp.
        PushQuad(verts,
            MakeVertex(a.corner[3],up,a.tangent,vec2(ua,0.0f)),
            MakeVertex(b.corner[3],up,b.tangent,vec2(ub,0.0f)),
            MakeVertex(b.corner[2],up,b.tangent,vec2(ub,half_width * 2.0f)),
            MakeVertex(a.corner[2],up,a.tangent,vec2(ua,half_width * 2.0f)));

        //Bottom. Buried under the deck for a wall and facing the player for a ramp, so it is worth
        //having rather than leaving the solid open.
        PushQuad(verts,
            MakeVertex(a.corner[0],-up,a.tangent,vec2(ua,0.0f)),
            MakeVertex(a.corner[1],-up,a.tangent,vec2(ua,half_width * 2.0f)),
            MakeVertex(b.corner[1],-up,b.tangent,vec2(ub,half_width * 2.0f)),
            MakeVertex(b.corner[0],-up,b.tangent,vec2(ub,0.0f)));
    }

    if (f_cap_ends){
        const PinStation& s = stations[0];
        vec3 nrm = -s.tangent;
        PushQuad(verts,
            MakeVertex(s.corner[0],nrm,s.normal,vec2(0.0f,0.0f)),
            MakeVertex(s.corner[3],nrm,s.normal,vec2(0.0f,height)),
            MakeVertex(s.corner[2],nrm,s.normal,vec2(half_width * 2.0f,height)),
            MakeVertex(s.corner[1],nrm,s.normal,vec2(half_width * 2.0f,0.0f)));

        const PinStation& e = stations[n - 1];
        PushQuad(verts,
            MakeVertex(e.corner[0],e.tangent,e.normal,vec2(0.0f,0.0f)),
            MakeVertex(e.corner[1],e.tangent,e.normal,vec2(half_width * 2.0f,0.0f)),
            MakeVertex(e.corner[2],e.tangent,e.normal,vec2(half_width * 2.0f,height)),
            MakeVertex(e.corner[3],e.tangent,e.normal,vec2(0.0f,height)));
    }

    Mesh* mesh = new Mesh();
    mesh->SetMeshData(verts.data(),(int)verts.size());
    return mesh;
}

Mesh* MakeWallStrip(const PinPath& path, float height, float thickness, bool f_cap_ends){
    //Sits ON the path: nothing below, all of it above.
    return MakeSweptBox(path,thickness * 0.5f,0.0f,height,f_cap_ends);
}

Mesh* MakeRibbon(const PinPath& path, float width, float thickness){
    //Centred ON the path in plan, hanging BELOW it in height - so the path itself is the surface
    //the ball rolls on and a ramp's centreline can be quoted as "where the ball is".
    return MakeSweptBox(path,width * 0.5f,thickness,0.0f,true);
}
