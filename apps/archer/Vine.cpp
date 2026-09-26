#include <math.h>

#include "Vine.h"
#include "PlaceHash.h"
#include "Stage.h"

/*
    The vines. See Vine.h for the pieces and vine_plan.md for where this is going.

    Like Foliage.cpp, nothing in here includes an engine header beyond core's maths - `make rules`
    links it into stage_test.exe with Spline.cpp, SplineDeform.cpp and the type helpers, and it has
    to stay that way to stay testable like that.
*/

static const float VINE_PI = 3.14159265358979f;
//The golden angle, 2*pi*(1 - 1/phi). Successive leaves turned by it never line up with any
//earlier one, which is why a stem grown this way reads as natural and a regular turn does not.
static const float VINE_GOLDEN_ANGLE = 2.39996323f;

//The channels a station draws from, so no two decisions share a number.
enum{ VCH_SPACING = 0, VCH_COUNT, VCH_TURN, VCH_KIND, VCH_SCALE, VCH_LIFT, VCH_ROLL, VCH_SLIDE };

//--- Path -----------------------------------------------------------------------------------------

bool BuildVineSpline(const VinePath& path, Spline& out){
    out.points = path.points;
    out.up = path.up;
    //0.02 is a fiftieth of a tile at most, and far under any bend a hand-typed path has.
    return out.Build(0.02f);
}

static SplineDeformParams TrunkDeform(const Spline& spline, const VinePath& path, const VineParams& params){
    SplineDeformParams d;
    d.scale = params.tile_scale * path.thickness;
    d.twist = params.twist;
    //Never more than half the vine each: a short vine tapers from both ends to its middle rather
    //than asking for a taper longer than itself.
    float taper = params.taper_length;
    if (taper > spline.GetLength() * 0.5f){
        taper = spline.GetLength() * 0.5f;
    }
    d.taper_start_length = taper;
    d.taper_start_scale = params.tip_scale;
    d.taper_end_length = taper;
    d.taper_end_scale = params.tip_scale;
    //The reference model is faceted, flat-shaded low poly: bending tilts every face, and only a
    //normal recomputed from the bent corners knows by how much.
    d.f_flat_normals = true;
    return d;
}

float VineRadiusAt(const Spline& spline, const VinePath& path, const VineParams& params, float s){
    SplineDeformParams d = TrunkDeform(spline,path,params);
    return params.tile_radius * d.scale * SplineDeformTaper(d,s,0.0f,spline.GetLength());
}

int BuildVineTrunk(const Spline& spline, const VinePath& path, const std::vector<vertex>& tile,
                   const VineParams& params, std::vector<vertex>& out){
    if (!spline.IsBuilt()){
        return 0;
    }
    return DeformAlongSpline(spline,tile,TrunkDeform(spline,path,params),out);
}

int BuildVineOverlay(const Spline& spline, const VinePath& path, const std::vector<vertex>& overlay,
                     const std::vector<vertex>& trunk_tile, const VineParams& params,
                     std::vector<vertex>& out){
    if (!spline.IsBuilt()){
        return 0;
    }
    SplineDeformParams d = TrunkDeform(spline,path,params);
    if (!SplineDeformMeasure(trunk_tile,d.tile_start,d.tile_length)){
        return 0;
    }
    return DeformAlongSpline(spline,overlay,d,out);
}

//--- Leaves ---------------------------------------------------------------------------------------

quat QuatFromBasis(const vec3& x, const vec3& y, const vec3& z){
    //The columns of the rotation matrix are the images of the local axes: m[row][col].
    float m00 = x.x, m01 = y.x, m02 = z.x;
    float m10 = x.y, m11 = y.y, m12 = z.y;
    float m20 = x.z, m21 = y.z, m22 = z.z;
    float trace = m00 + m11 + m22;
    quat q;
    //Shepperd's method: divide by whichever of w, x, y, z is largest, so nothing is ever divided
    //by a number near zero.
    if (trace > 0.0f){
        float s = 0.5f / sqrtf(trace + 1.0f);
        q.w = 0.25f / s;
        q.x = (m21 - m12) * s;
        q.y = (m02 - m20) * s;
        q.z = (m10 - m01) * s;
    }else if (m00 > m11 && m00 > m22){
        float s = 2.0f * sqrtf(1.0f + m00 - m11 - m22);
        q.w = (m21 - m12) / s;
        q.x = 0.25f * s;
        q.y = (m01 + m10) / s;
        q.z = (m02 + m20) / s;
    }else if (m11 > m22){
        float s = 2.0f * sqrtf(1.0f + m11 - m00 - m22);
        q.w = (m02 - m20) / s;
        q.x = (m01 + m10) / s;
        q.y = 0.25f * s;
        q.z = (m12 + m21) / s;
    }else{
        float s = 2.0f * sqrtf(1.0f + m22 - m00 - m11);
        q.w = (m10 - m01) / s;
        q.x = (m02 + m20) / s;
        q.y = (m12 + m21) / s;
        q.z = 0.25f * s;
    }
    return q.normalize();
}

//Uniform in [-1, 1).
static float Signed(float u){
    return 2.0f * u - 1.0f;
}

//Is the point inside any live block? Blocks fill the slab's whole depth, so this is a 2D test.
static bool InsideBlock(const std::vector<StageBlock>* blocks, const vec3& p){
    if (!blocks){
        return false;
    }
    for (size_t i = 0; i < blocks->size(); i++){
        const StageBlock& b = (*blocks)[i];
        if (b.f_alive && p.x > b.Left() && p.x < b.Right() && p.y > b.Bottom() && p.y < b.Top()){
            return true;
        }
    }
    return false;
}

void ScatterVineLeaves(const Spline& spline, const VinePath& path, const VineParams& params,
                       const std::vector<StageBlock>* blocks, std::vector<VineLeaf>& out){
    float length = spline.GetLength();
    float s_from = params.leaf_end_margin;
    float s_to = length - params.leaf_end_margin;
    if (!spline.IsBuilt() || s_to <= s_from || params.leaf_spacing <= 0.01f){
        return;
    }
    //Hashed on (s, seed): a leaf is a function of where it is along its own vine, so lengthening
    //a vine at one end leaves every leaf already on it where it was.
    float seed = (float)path.seed;
    int cmin = (params.cluster_min > 0) ? params.cluster_min : 1;
    int cmax = (params.cluster_max >= cmin) ? params.cluster_max : cmin;

    //Start half a spacing in, jittered, so two vines of one length do not both open with a leaf
    //at exactly the margin.
    float s = s_from + params.leaf_spacing * 0.5f * Hash01(0.0f,seed,VCH_SPACING,0);
    float turn = 2.0f * VINE_PI * Hash01(0.0f,seed,VCH_TURN,0);
    int station = 0;
    while (s <= s_to){
        SplineFrame f = spline.FrameAt(s);
        float radius = VineRadiusAt(spline,path,params,s);
        //1 in the body of the vine, falling toward tip_scale at the ends.
        float body = (params.tip_scale < 1.0f) ?
                     (radius / (params.tile_radius * params.tile_scale * path.thickness) - params.tip_scale) /
                     (1.0f - params.tip_scale) : 1.0f;
        if (body < 0.0f){ body = 0.0f; }
        if (body > 1.0f){ body = 1.0f; }

        int count = cmin + (int)(Hash01(s,seed,VCH_COUNT,0) * (float)(cmax - cmin + 1));
        if (count > cmax){ count = cmax; }
        for (int j = 0; j < count; j++){
            //The cluster fans round the trunk from the station's turn, centred on it.
            float around = turn + params.cluster_fan * ((float)j - 0.5f * (float)(count - 1)) +
                           0.25f * Signed(Hash01(s,seed,VCH_TURN,j + 1));
            //A cluster's leaves spring from within a few centimetres of each other, not one point.
            float slide = 0.04f * Signed(Hash01(s,seed,VCH_SLIDE,j));
            float lift = params.leaf_lift + params.leaf_lift_jitter * Signed(Hash01(s,seed,VCH_LIFT,j));
            float roll = 0.5f * Signed(Hash01(s,seed,VCH_ROLL,j));
            float shrink = 1.0f - params.leaf_tip_shrink * (1.0f - body);
            float scale = params.leaf_scale * shrink *
                          (1.0f + params.leaf_scale_jitter * Signed(Hash01(s,seed,VCH_SCALE,j)));
            vec3 stem_on_axis = spline.PositionAt(s + slide);

            //The side it was dealt first, then the far side of the trunk if that one is buried.
            for (int attempt = 0; attempt < 2; attempt++){
                float a = around + (float)attempt * VINE_PI;
                vec3 out_dir = f.side * cosf(a) + f.normal * sinf(a);
                /*
                    The leaf's blade points outward and on along the vine, toward the growing tip;
                    its upper face is the same pair turned a quarter further, so the two stay square
                    to each other whatever the lift. Then a roll about the blade, so no two catch
                    the light alike.
                */
                vec3 fwd = f.tangent * cosf(lift) + out_dir * sinf(lift);
                vec3 up  = out_dir * cosf(lift) - f.tangent * sinf(lift);
                vec3 across = up.cross(fwd);
                up = up * cosf(roll) + across * sinf(roll);
                across = up.cross(fwd);

                vec3 stem = stem_on_axis + out_dir * (radius * params.leaf_seat);
                vec3 tip = stem + fwd * (params.leaf_length * scale);
                if (InsideBlock(blocks,tip)){
                    continue;
                }
                VineLeaf leaf;
                leaf.kind = (Hash01(s,seed,VCH_KIND,j) < 0.5f) ? VINE_LEAF_1 : VINE_LEAF_2;
                leaf.s = s + slide;
                leaf.position = stem;
                leaf.rotation = QuatFromBasis(across,up,fwd);
                leaf.scale = scale;
                out.push_back(leaf);
                break;
            }
        }

        turn += VINE_GOLDEN_ANGLE;
        station++;
        s += params.leaf_spacing * (1.0f + params.leaf_spacing_jitter * Signed(Hash01(s,seed,VCH_SPACING,station)));
    }
}

//--- Placeholders ---------------------------------------------------------------------------------

float VineTileRadius(const std::vector<vertex>& tile){
    float r = 0.0f;
    for (size_t i = 0; i < tile.size(); i++){
        float d = sqrtf(tile[i].pos.x * tile[i].pos.x + tile[i].pos.y * tile[i].pos.y);
        if (d > r){ r = d; }
    }
    return r;
}

static vertex MakeVert(const vec3& p, const vec3& n, const vec3& t, float u, float v){
    vertex o;
    o.pos = p;
    o.normal = n;
    o.tangent = t;
    o.uv = vec2(u,v);
    o.matid = 0;
    return o;
}

void MakeVinePlaceholderTile(std::vector<vertex>& out){
    out.clear();
    const int   SIDES = 8;
    const float LEN = 0.42f;
    const float R = 0.10f;
    const float SQUASH = 0.82f;     //flatter top to bottom, like the reference's trunk
    //An irregular octagon, the SAME at both end rings so the seams close; the middle ring is
    //turned a sixteenth and swollen a touch, which is what makes the facets read as hand-made
    //rather than as a pipe.
    static const float lumps[SIDES] = { 1.00f, 0.90f, 1.06f, 0.94f, 1.02f, 0.88f, 1.04f, 0.93f };
    const int RINGS = 3;
    vec3 ring[RINGS][SIDES];
    for (int r = 0; r < RINGS; r++){
        float z = LEN * (float)r / (float)(RINGS - 1);
        float offset = (r == 1) ? VINE_PI / (float)SIDES : 0.0f;
        float swell = (r == 1) ? 1.05f : 1.0f;
        for (int i = 0; i < SIDES; i++){
            float a = 2.0f * VINE_PI * (float)i / (float)SIDES + offset;
            float k = R * lumps[i] * swell;
            ring[r][i] = vec3(k * cosf(a),k * SQUASH * sinf(a),z);
        }
    }
    vec3 t = vec3(0.0f,0.0f,1.0f);
    for (int r = 0; r + 1 < RINGS; r++){
        for (int i = 0; i < SIDES; i++){
            int j = (i + 1) % SIDES;
            //Counter-clockwise round +Z, so this winding faces outward (the spline test checks
            //the same arrangement survives the deform).
            vec3 a = ring[r][i], b = ring[r][j], c = ring[r + 1][j], d = ring[r + 1][i];
            vec3 n = (a + b + c + d) * 0.25f;
            n.z = 0.0f;
            n.normalize();
            float u0 = (float)i / (float)SIDES, u1 = (float)(i + 1) / (float)SIDES;
            float v0 = a.z / LEN, v1 = c.z / LEN;
            out.push_back(MakeVert(a,n,t,u0,v0));
            out.push_back(MakeVert(b,n,t,u1,v0));
            out.push_back(MakeVert(c,n,t,u1,v1));
            out.push_back(MakeVert(a,n,t,u0,v0));
            out.push_back(MakeVert(c,n,t,u1,v1));
            out.push_back(MakeVert(d,n,t,u0,v1));
        }
    }
}

void MakeVinePlaceholderLeaf(std::vector<vertex>& out){
    out.clear();
    //Broad for its length: at 0.30 x 0.15 it read as a thorn at the camera's distance.
    const float L = 0.36f;
    const float HW = 0.12f;
    const float CUP = 0.025f;       //the edges curl up off the midrib
    const float THICK = 0.008f;
    //Stem, the two widest points, the tip, and the midrib at the widest point.
    vec3 S  = vec3(0.0f,0.0f,0.0f);
    vec3 Lf = vec3(-HW,CUP,0.42f * L);
    vec3 R  = vec3( HW,CUP,0.42f * L);
    vec3 T  = vec3(0.0f,0.02f,L);
    vec3 M  = vec3(0.0f,0.0f,0.42f * L);
    //Upper face: four triangles, each counter-clockwise seen from +Y.
    vec3 top[4][3] = { { S,M,R }, { S,Lf,M }, { M,T,R }, { Lf,T,M } };
    vec3 t = vec3(0.0f,0.0f,1.0f);
    for (int k = 0; k < 4; k++){
        vec3 n = (top[k][1] - top[k][0]).cross(top[k][2] - top[k][0]);
        n.normalize();
        for (int c = 0; c < 3; c++){
            out.push_back(MakeVert(top[k][c],n,t,top[k][c].x / HW * 0.5f + 0.5f,top[k][c].z / L));
        }
    }
    //Underside: the same, a hair lower and wound the other way, so the leaf has a back to be seen
    //from - the renderer culls back faces, and a leaf is seen from both sides.
    vec3 down = vec3(0.0f,-THICK,0.0f);
    for (int k = 0; k < 4; k++){
        vec3 a = top[k][0] + down, b = top[k][2] + down, c = top[k][1] + down;
        vec3 n = (b - a).cross(c - a);
        n.normalize();
        vec3 tri[3] = { a,b,c };
        for (int q = 0; q < 3; q++){
            out.push_back(MakeVert(tri[q],n,t,tri[q].x / HW * 0.5f + 0.5f,tri[q].z / L));
        }
    }
}

//--- The level's vines ----------------------------------------------------------------------------
/*
    Placed by hand for now (agreed 2026-09-24); a generator walking the blocks comes later and will
    produce the same VinePath. They sit at z about +-1.1, off the archer's lane at z 0 and inside
    the slab's +-1.5, so they dress the level without ever being in the way. The one by the start
    is in FRONT (+z): behind, the crates beside the step hid it.

    The centre line is laid a radius off the surface it lies on (the placeholder is 0.1), a little
    less so it beds in: on the ground at y 0.07, up a face 0.08 short of it.
*/
void DeclareVines(int level, std::vector<VinePath>& out){
    out.clear();
    if (level != STAGE_LEVEL_MAIN){
        return;
    }
    //By the start: creeping along the ground to the step (x 5..9, top 1.8), up its face and over.
    //The first thing on screen, and it exercises the frame round two corners.
    VinePath a;
    a.seed = 1;
    a.points = { vec3(1.60f,0.07f,0.95f), vec3(2.70f,0.09f,1.20f), vec3(3.90f,0.07f,1.05f),
                 vec3(4.80f,0.12f,1.15f), vec3(4.92f,0.70f,1.20f), vec3(4.92f,1.40f,1.10f),
                 vec3(5.05f,1.87f,1.15f), vec3(5.75f,1.88f,1.25f), vec3(6.50f,1.87f,1.10f) };
    out.push_back(a);

    //Over the lip of the high ledge (x 44..48, top 4.2) and hanging down its left face with a free
    //end - the decorative cousin of the rope, in the one place a hanging vine is expected.
    VinePath b;
    b.seed = 2;
    b.points = { vec3(47.10f,4.27f,-1.10f), vec3(46.00f,4.28f,-0.95f), vec3(44.90f,4.27f,-1.15f),
                 vec3(44.08f,4.25f,-1.05f), vec3(43.90f,3.85f,-1.00f), vec3(43.88f,3.20f,-1.10f),
                 vec3(43.94f,2.55f,-1.00f) };
    out.push_back(b);

    //Sagging under the one-way platform (x 21..26, underside 2.5) - a drape, the shape a curve is
    //for and a box is not.
    VinePath c;
    c.seed = 3;
    c.thickness = 0.8f;
    c.points = { vec3(21.20f,2.45f,-1.20f), vec3(22.30f,2.05f,-1.25f), vec3(23.50f,1.88f,-1.15f),
                 vec3(24.70f,2.05f,-1.25f), vec3(25.80f,2.45f,-1.20f) };
    out.push_back(c);

#if ARCHER_TEST_BAY
    /*
        THE CAVE - the terrain bay, in front of its back wall (Backdrop.h). Four big long vines
        hanging into it onto the island (x -34..-18, top 11.25, z -1.5..1.5) and the stones under
        it, two BEHIND her and two IN FRONT, so the cave has depth on both sides of her walking line.

        Resting points sit 0.2 over the top they lie on: at thickness 1.5 .. 1.8 the trunk is 0.25 ..
        0.30 in radius (the tile is 0.083 at scale 1, drawn at her 2.02), so it beds into the grass.
        The ones in front keep to z 2 and more where they hang - past the slab's front lip (1.68)
        and its rounding - and come back onto a top only inside its depth. Those two start at y 33+,
        above any frame the camera shows here: begun at 20 their tapered tops hung in mid-air on
        screen at the camera's widest. A vine that starts
        hanging straight down takes up = +Z: the default +Y would be parallel to its tangent, and
        the frame has nothing to start from.
    */
    //Behind: off the back wall's crest (its front face z -4.5), a long fall onto the island's step
    //(x -30.5..-27.5, top 12.25), down its right face and along the island's back rim.
    VinePath d;
    d.seed = 4;
    d.thickness = 1.8f;
    d.points = { vec3(-29.20f,16.30f,-6.60f), vec3(-29.10f,16.20f,-4.90f), vec3(-29.00f,15.60f,-3.70f),
                 vec3(-29.10f,14.30f,-2.90f), vec3(-29.00f,13.10f,-1.90f), vec3(-28.80f,12.45f,-1.20f),
                 vec3(-27.90f,12.45f,-1.15f), vec3(-27.30f,12.05f,-1.20f), vec3(-27.10f,11.45f,-1.20f),
                 vec3(-26.00f,11.45f,-1.25f), vec3(-24.80f,11.45f,-1.20f) };
    out.push_back(d);

    //Behind: over the island's left end, a sag under it onto the small stone (x -31..-29, top
    //6.8), and off that stone's right edge with a free end.
    VinePath e;
    e.seed = 5;
    e.thickness = 1.5f;
    e.points = { vec3(-32.60f,11.45f,-1.15f), vec3(-33.70f,11.45f,-1.15f), vec3(-34.55f,11.00f,-1.10f),
                 vec3(-34.50f,9.30f,-1.20f), vec3(-33.20f,7.70f,-1.35f), vec3(-31.60f,7.10f,-1.30f),
                 vec3(-30.60f,6.98f,-1.15f), vec3(-29.50f,6.98f,-1.15f), vec3(-28.85f,6.55f,-1.10f),
                 vec3(-28.80f,5.30f,-1.05f), vec3(-28.90f,4.30f,-1.00f) };
    out.push_back(e);

    //In front: down from out of frame, over the island's front lip, and on down onto the stone at
    //x -20.5..-18.2 (top 4.8, front 1.2), hanging off its right edge. She walks behind all of it.
    VinePath f;
    f.seed = 6;
    f.thickness = 1.8f;
    f.up = vec3(0.0f,0.0f,1.0f);
    f.points = { vec3(-25.40f,34.00f,2.70f), vec3(-25.10f,27.50f,2.65f), vec3(-24.80f,21.00f,2.60f), vec3(-24.20f,17.50f,2.50f), vec3(-23.60f,14.00f,2.35f),
                 vec3(-23.00f,11.90f,1.90f), vec3(-22.30f,11.45f,1.35f), vec3(-21.50f,11.35f,2.10f),
                 vec3(-21.10f,10.00f,2.25f), vec3(-20.70f,8.20f,2.25f), vec3(-20.20f,6.30f,2.00f),
                 vec3(-19.80f,5.20f,1.40f), vec3(-19.40f,4.98f,0.95f), vec3(-18.70f,4.98f,1.00f),
                 vec3(-18.00f,4.60f,1.30f), vec3(-17.90f,3.50f,1.40f) };
    out.push_back(f);

    //In front: down from out of frame onto the stone at x -15.3..-13.3 (top 7.2, front 0.8), and
    //off its right edge.
    VinePath g;
    g.seed = 7;
    g.thickness = 1.5f;
    g.up = vec3(0.0f,0.0f,1.0f);
    g.points = { vec3(-16.80f,33.00f,2.60f), vec3(-16.60f,26.50f,2.55f), vec3(-16.40f,20.00f,2.50f), vec3(-16.00f,14.00f,2.40f), vec3(-15.60f,9.50f,2.00f),
                 vec3(-15.30f,7.80f,1.30f), vec3(-14.80f,7.38f,0.65f), vec3(-13.80f,7.38f,0.65f),
                 vec3(-13.10f,6.90f,1.00f), vec3(-13.00f,5.60f,1.20f), vec3(-13.10f,4.40f,1.30f) };
    out.push_back(g);
#endif
}
