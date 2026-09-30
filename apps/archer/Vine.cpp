#include <math.h>

#include "Vine.h"
#include "PlaceHash.h"
#include "Stage.h"

/*
    The vines. See Vine.h for the pieces and docs/vine_plan.md for where this is going.

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
//And a growth's. The wander takes two per strand from VCH_WANDER up, so it is left room.
enum{ VCH_GROW_LENGTH = 16, VCH_GROW_THICK, VCH_BRANCH_COUNT, VCH_BRANCH_AT, VCH_BRANCH_SIDE,
      VCH_BRANCH_LENGTH, VCH_REST_SIDE, VCH_WANDER = 32 };

//--- Path -----------------------------------------------------------------------------------------

bool BuildVineSpline(const VinePath& path, Spline& out){
    out.points = path.points;
    out.up = path.up;
    //0.02 is a fiftieth of a tile at most, and far under any bend a hand-typed path has.
    return out.Build(0.02f);
}

static SplineDeformParams TrunkDeform(const Spline& spline, const VinePath& path, const VineParams& params,
                                      float grown = -1.0f){
    SplineDeformParams d;
    d.grown = grown;       //growing: only this far along - see SplineDeformParams::grown
    d.grow_tip_length = params.grow_tip_length;
    d.grow_tip_scale = params.grow_tip_scale;
    d.scale = params.tile_scale * path.thickness;
    d.twist = params.twist;
    //Never more than half the vine each: a short vine tapers from both ends to its middle rather
    //than asking for a taper longer than itself.
    float taper = params.taper_length;
    if (taper > spline.GetLength() * 0.5f){
        taper = spline.GetLength() * 0.5f;
    }
    //A rooted start is buried, so it keeps its full thickness to where it comes out.
    d.taper_start_length = path.f_rooted ? 0.0f : taper;
    d.taper_start_scale = params.tip_scale;
    d.taper_end_length = taper;
    d.taper_end_scale = params.tip_scale;
    /*
        SMOOTH, carrying the tile's own normals. vine_trunk and vine_curl are smooth-shaded in
        archer.glb (not one split normal between them, measured 2026-09-29), and this used to be
        f_flat_normals - written for the faceted placeholder - which threw those normals away and
        drew every triangle. The placeholder carries one normal per quad, so it stays faceted near
        enough, softened only at its joins. The seams are welded because a tile smoothed on its own
        leans its end rings toward its middle; see SplineDeformParams::f_weld_seams.
    */
    d.f_flat_normals = false;
    d.f_weld_seams = true;
    return d;
}

float VineRadiusAt(const Spline& spline, const VinePath& path, const VineParams& params, float s){
    SplineDeformParams d = TrunkDeform(spline,path,params);
    return params.tile_radius * d.scale * SplineDeformTaper(d,s,0.0f,spline.GetLength());
}

int BuildVineTrunk(const Spline& spline, const VinePath& path, const std::vector<vertex>& tile,
                   const VineParams& params, std::vector<vertex>& out, float grown){
    if (!spline.IsBuilt()){
        return 0;
    }
    return DeformAlongSpline(spline,tile,TrunkDeform(spline,path,params,grown),out);
}

int BuildVineOverlay(const Spline& spline, const VinePath& path, const std::vector<vertex>& overlay,
                     const std::vector<vertex>& trunk_tile, const VineParams& params,
                     std::vector<vertex>& out, float grown){
    if (!spline.IsBuilt()){
        return 0;
    }
    SplineDeformParams d = TrunkDeform(spline,path,params,grown);
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

void ScatterVineLeaves(const Spline& spline, const VinePath& path, const VineParams& params,
                       const std::vector<StageBlock>* blocks, std::vector<VineLeaf>& out){
    if (!blocks){
        ScatterVineLeaves(spline,path,params,(const VineField*)NULL,out);
        return;
    }
    VineBlockField field(*blocks);
    ScatterVineLeaves(spline,path,params,&field,out);
}

void ScatterVineLeaves(const Spline& spline, const VinePath& path, const VineParams& params,
                       const VineField* field, std::vector<VineLeaf>& out){
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
                //Buried: in a box, or in the drawn rock, whichever the field is.
                if (field && field->Distance(tip) < 0.0f){
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

//--- Grown vines ----------------------------------------------------------------------------------

const VineSpecies& VineSpeciesFor(int kind){
    static VineSpecies table[VINE_SPECIES_COUNT];
    static bool f_built = false;
    if (!f_built){
        //The vine: hangs from an underside, swaying a little either way as it falls, comes to rest
        //on whatever floor it reaches and creeps a short way along it. A branch or two.
        VineSpecies& v = table[VINE_SPECIES_VINE];
        v.name = "vine";
        /*
            Roots: out of an underside and down a little way, wandering hard on a short wavelength
            - a root feels its way, it does not hang - with a fork near the tip, and quick: a
            normal arrow grows these every time it goes into an underside, so they are seen dozens
            of times and must never be a wait. The root tile is the placeholder octagon, 0.1 in
            radius, so 0.7 is 0.07. Doubled in every length on 2026-09-29 (the user: "the size of
            the roots and tufts can be double"), the turning rates halved to keep the same shape.
        */
        VineSpecies& r = table[VINE_SPECIES_ROOTS];
        r.name = "roots";
        r.length_min = 0.60f;
        r.length_max = 1.80f;
        r.step = 0.08f;
        r.point_spacing = 0.20f;
        r.gravity = 1.5f;
        r.wander = 1.6f;
        r.wander_wavelength = 0.70f;
        r.wander_depth = 0.5f;
        r.clearance = 0.0f;
        r.rest_length = 0.30f;
        r.branch_min = 1;
        r.branch_max = 2;
        r.branch_from = 0.45f;
        r.branch_to = 0.85f;
        r.branch_angle_deg = 45.0f;
        r.branch_length = 0.45f;
        r.thickness = 0.70f;
        r.thickness_jitter = 0.25f;
        r.branch_thickness = 0.6f;
        r.grow_ticks = 24;
        /*
            The creeper: the vine, hugging. Lies right against the face (a hair into it, so it
            beds in), climbs, wanders a little across the face, over the lip, across the top and
            down the far side if it is long enough. Longer than a hanging vine can be, since it is
            laid out along the level rather than dropped. A branch or two, which hug too.
        */
        VineSpecies& c = table[VINE_SPECIES_CREEPER];
        c = table[VINE_SPECIES_VINE];
        c.name = "creeper";
        c.length_min = 3.5f;
        c.length_max = 7.0f;
        c.gravity = 2.0f;
        c.wander = 1.0f;
        c.wander_wavelength = 1.4f;
        c.wander_depth = 0.0f;
        c.clearance = -0.03f;
        c.hug = 10.0f;
        c.hug_reach = 0.6f;
        c.hug_gap = 0.0f;
        c.climb = 2.5f;
        c.grow_ticks = 180;
        /*
            Bamboo: straight up and fast. Gravity negative turns the heading UP, so a cane struck
            into a top rises almost at once and one out of a wall comes out and bends up within half
            a unit. A slow, slight wander - a cane is not dead straight, but it never snakes. No
            branches: a clump is several canes (GrowBamboo), each one stalk. Its reveal is quicker
            than the vine's over a comparable length, which is most of what makes it read as bamboo.
        */
        VineSpecies& b = table[VINE_SPECIES_BAMBOO];
        b.name = "bamboo";
        b.length_min = 3.0f;        //over her head: she is 1.8
        b.length_max = 6.0f;
        b.step = 0.08f;
        b.point_spacing = 0.30f;
        b.gravity = -3.0f;
        b.wander = 0.25f;
        b.wander_wavelength = 2.5f;
        b.wander_depth = 0.4f;
        b.clearance = 0.0f;
        b.branch_min = 0;
        b.branch_max = 0;
        b.thickness = 0.8f;
        b.thickness_jitter = 0.12f;
        b.grow_ticks = 75;
        b.leaf_delay = 0.5f;
        b.leaf_unfold = 0.5f;
        f_built = true;
    }
    return table[(kind >= 0 && kind < VINE_SPECIES_COUNT) ? kind : VINE_SPECIES_VINE];
}

float VineGrowthFront(const VineSpecies& sp, float length, int ticks){
    if (ticks <= 0){
        return 0.0f;
    }
    if (sp.grow_ticks <= 0 || ticks >= sp.grow_ticks){
        return length;
    }
    float x = (float)ticks / (float)sp.grow_ticks;
    return length * (1.0f - (1.0f - x) * (1.0f - x));
}

float VineLeafOpen(const VineSpecies& sp, float s, float front){
    float unfold = (sp.leaf_unfold > 0.01f) ? sp.leaf_unfold : 0.01f;
    float k = (front - s - sp.leaf_delay) / unfold;
    if (k <= 0.0f){ return 0.0f; }
    if (k >= 1.0f){ return 1.0f; }
    return 1.0f - (1.0f - k) * (1.0f - k);
}

int VineGrowthSeed(const vec3& point, int arrow){
    return (int)(Hash01(point.x,point.y,arrow,(int)lroundf(point.z * 1000.0f)) * 1000000.0f);
}

float VineBlockDistance(const std::vector<StageBlock>& blocks, float x, float y){
    float best = 1e9f;
    for (const StageBlock& b : blocks){
        if (!b.f_alive){
            continue;
        }
        //The box's signed distance: outside, the distance to its nearest point; inside, minus the
        //distance to its nearest face.
        float qx = fabsf(x - b.x) - b.hw;
        float qy = fabsf(y - b.y) - b.hh;
        float ox = (qx > 0.0f) ? qx : 0.0f;
        float oy = (qy > 0.0f) ? qy : 0.0f;
        float inside = (qx > qy) ? qx : qy;
        float d = sqrtf(ox * ox + oy * oy) + ((inside < 0.0f) ? inside : 0.0f);
        if (d < best){
            best = d;
        }
    }
    return best;
}

//--- What a vine grows against ---

vec3 VineField::Normal(const vec3& p) const{
    const float h = 0.01f;
    float dx = Distance(vec3(p.x + h,p.y,p.z)) - Distance(vec3(p.x - h,p.y,p.z));
    float dy = Distance(vec3(p.x,p.y + h,p.z)) - Distance(vec3(p.x,p.y - h,p.z));
    float dz = Distance(vec3(p.x,p.y,p.z + h)) - Distance(vec3(p.x,p.y,p.z - h));
    float l = sqrtf(dx * dx + dy * dy + dz * dz);
    return (l > 1e-6f) ? vec3(dx / l,dy / l,dz / l) : vec3(0.0f,0.0f,0.0f);
}

float VineBlockField::Distance(const vec3& p) const{
    return VineBlockDistance(blocks,p.x,p.y);
}

VineLevelField::VineLevelField(const std::vector<StageBlock>& blocks,
                               const std::vector<const TerrainSurface*>& s) : surfaces(s){
    for (const StageBlock& b : blocks){
        if (!b.f_alive){
            continue;
        }
        bool f_melted = false;
        for (const TerrainSurface* surface : surfaces){
            if (surface && surface->Region().Contains(b)){
                f_melted = true;
                break;
            }
        }
        if (!f_melted){
            boxes.push_back(b);
        }
    }
}

float VineLevelField::Distance(const vec3& p) const{
    float d = VineBlockDistance(boxes,p.x,p.y);
    for (const TerrainSurface* surface : surfaces){
        if (surface && !surface->IsEmpty()){
            float t = surface->Distance(p);
            if (t < d){
                d = t;
            }
        }
    }
    return d;
}

//Smooth value noise in [-1, 1] along t: hashed values on the integers, smoothstepped between.
static float SmoothNoise(float t, float seed, int channel){
    float i = floorf(t);
    float f = t - i;
    float a = Signed(Hash01(i,seed,channel,0));
    float b = Signed(Hash01(i + 1.0f,seed,channel,0));
    float u = f * f * (3.0f - 2.0f * f);
    return a + (b - a) * u;
}

/*
    One strand, walked from `start` along `heading` for up to `length`, a point kept every
    point_spacing. See VineSpecies for what turns it. `channel` keeps each strand's wander its own.
*/
#define VINE_Z_HOLD         1.5f    //per unit: how hard the heading is turned back toward the start's depth
#define VINE_STUCK_STEPS    6       //this many steps in a row that barely move, and the walk gives up
#define VINE_MARCH_STEP     0.05f   //out of the drawn rock to its surface, this far at a time...
#define VINE_MARCH_STEPS    60      //...and at most this often: 3 units, past any belly or drip
static void WalkStrand(const VineSpecies& sp, float radius, const vec3& start, const vec3& heading,
                       float length, float seed, int channel, const VineField& field,
                       VineStrand& out){
    const float keep = radius + sp.clearance;
    const float step = (sp.step > 0.005f) ? sp.step : 0.005f;
    const vec3 down(0.0f,-1.0f,0.0f);
    vec3 p = start;
    vec3 h = heading;
    h.normalize();
    out.path.points.push_back(p);
    float walked = 0.0f;
    float since_point = 0.0f;
    float rested = -1.0f;           //distance crept since first coming to rest, -1 not yet
    int stuck = 0;
    //Which way to creep if it lands heading straight down - fixed for the strand, so it does not
    //dither between the two.
    float rest_side = (Hash01(start.x,seed,VCH_REST_SIDE,channel) < 0.5f) ? -1.0f : 1.0f;
    //A creeper that has come over onto a top stops climbing - see VineSpecies::hug. One grown out
    //of a top starts there.
    bool f_hugging = (sp.hug > 0.0f);
    bool f_climbed = f_hugging && (heading.y < -0.5f || field.Normal(start).y > 0.7f);

    while (walked < length){
        float t = walked / ((sp.wander_wavelength > 0.01f) ? sp.wander_wavelength : 0.01f);
        float wx = SmoothNoise(t,seed,VCH_WANDER + 2 * channel);
        float wz = SmoothNoise(t,seed,VCH_WANDER + 2 * channel + 1);
        //Across the screen, square to the heading - or plain +X while it points into the screen.
        vec3 side = h.cross(vec3(0.0f,0.0f,1.0f));
        if (side.length() < 1e-3f){
            side = vec3(1.0f,0.0f,0.0f);
        }
        side.normalize();
        vec3 turn = down * sp.gravity + side * (wx * sp.wander) +
                    vec3(0.0f,0.0f,1.0f) * (wz * sp.wander * sp.wander_depth - (p.z - start.z) * VINE_Z_HOLD);
        /*
            THE HUG: near a surface, lie on it and move along it. The bias - up while climbing,
            down once over a lip - is taken in the surface's own plane, so on a wall "up" climbs
            and on a top it is nothing, and the heading's persistence carries it across. The pull
            toward the surface is what bends it round a lip: past the corner the field's normal
            turns, and the strand turns with it. Wander in the surface's plane too.
        */
        if (f_hugging){
            float d = field.Distance(p);
            vec3 n = field.Normal(p);
            if (d < keep + sp.hug_reach && n.length() > 0.5f){
                /*
                    Over the lip only once it LIES on the top - the normal up AND the heading
                    mostly level. Just past a corner the normal already points up while the strand
                    is still rising into the air beside it; taken as "over" there, gravity pulled
                    it straight back down the face it had climbed (measured: 36 of 100 did).
                */
                if (n.y > 0.7f && h.y < 0.35f){
                    f_climbed = true;
                }
                vec3 bias = f_climbed ? down * sp.gravity : vec3(0.0f,1.0f,0.0f) * sp.climb;
                bias = bias - n * bias.dot(n);
                float pull = (d - (keep + sp.hug_gap)) / ((sp.hug_reach > 0.01f) ? sp.hug_reach : 0.01f);
                pull = (pull < -1.0f) ? -1.0f : ((pull > 1.0f) ? 1.0f : pull);
                vec3 across = n.cross(h);
                if (across.length() < 1e-3f){
                    across = side;
                }
                across.normalize();
                //On a top, across the plane is into the screen; a little of that is roundness, all
                //of it is a zigzag against the hold back to the walk line.
                across.z *= 0.3f;
                turn = bias + across * (wx * sp.wander) - n * (pull * sp.hug) +
                       vec3(0.0f,0.0f,1.0f) * (-(p.z - start.z) * VINE_Z_HOLD);
            }
        }
        h = h + turn * step;
        h.normalize();

        vec3 move = h * step;
        vec3 next = p + move;
        float d_here = field.Distance(p);
        float d_next = field.Distance(next);
        bool f_on_floor = false;
        //Held off the blocks: a step that would come nearer than `keep` slides along the face -
        //only one coming NEARER, so a strand can always leave the face it grew from.
        if (d_next < keep && d_next < d_here){
            vec3 n = field.Normal(next);
            if (n.length() > 0.5f){
                float into = move.dot(n);
                if (into < 0.0f){
                    move = move - n * into;
                }
                //Straight into a face: nothing left to slide on, so creep along it to one side.
                if (move.length() < 0.2f * step){
                    vec3 along(-n.y,n.x,0.0f);
                    float ad = along.dot(h);
                    if ((fabsf(ad) > 1e-3f) ? (ad < 0.0f) : (rest_side < 0.0f)){
                        along = along * -1.0f;
                    }
                    move = along * step;
                }
                next = p + move;
                //And out to `keep`, if the slide still grazes it.
                float d = field.Distance(next);
                if (d < keep){
                    next = next + n * (keep - d);
                }
                f_on_floor = (n.y > 0.7f);
                if (move.length() > 1e-6f){
                    h = move;
                    h.normalize();
                }
            }
        }
        stuck = ((next - p).length() < 0.1f * step) ? stuck + 1 : 0;
        if (stuck >= VINE_STUCK_STEPS){
            break;
        }
        p = next;
        walked += step;
        since_point += step;
        //Counted only while it lies on a floor: one that creeps off the end falls on, and hangs.
        //A creeper never rests - lying on a floor is what it does.
        if (f_on_floor && !f_hugging){
            rested = (rested < 0.0f) ? 0.0f : rested + step;
            if (rested >= sp.rest_length){
                out.f_rested = true;
                break;
            }
        }
        if (since_point >= sp.point_spacing){
            out.path.points.push_back(p);
            since_point = 0.0f;
        }
    }
    if ((out.path.points.back() - p).length() > 0.25f * sp.point_spacing){
        out.path.points.push_back(p);
    }
    out.length = walked;
    //Up for the frame: anything not along the first heading. +Z unless it starts into the screen.
    out.path.up = (fabsf(heading.z) > 0.9f) ? vec3(0.0f,1.0f,0.0f) : vec3(0.0f,0.0f,1.0f);
}

vec3 VineMarchOut(const VineField& field, const vec3& p, const vec3& normal){
    vec3 n = normal;
    if (n.length() < 1e-6f){
        return p;
    }
    n.normalize();
    vec3 surface = p;
    if (field.Distance(p + n * 0.02f) < 0.0f){
        for (int i = 0; i < VINE_MARCH_STEPS; i++){
            surface = surface + n * VINE_MARCH_STEP;
            if (field.Distance(surface + n * 0.02f) >= 0.0f){
                break;
            }
        }
    }
    return surface;
}

bool GrowVine(const VineSpecies& sp, const VineParams& params, const vec3& anchor, const vec3& normal,
              int seed, const std::vector<StageBlock>& blocks, VineGrowth& out){
    return GrowVine(sp,params,anchor,normal,seed,VineBlockField(blocks),out);
}

bool GrowVine(const VineSpecies& sp, const VineParams& params, const vec3& anchor, const vec3& normal,
              int seed, const VineField& field, VineGrowth& out){
    out.strands.clear();
    vec3 n = normal;
    if (n.length() < 1e-6f){
        return false;
    }
    n.normalize();
    float fseed = (float)seed;
    float thickness = sp.thickness * (1.0f + sp.thickness_jitter * Signed(Hash01(0.0f,fseed,VCH_GROW_THICK,0)));
    float radius = params.tile_radius * params.tile_scale * thickness;
    float length = sp.length_min + (sp.length_max - sp.length_min) * Hash01(0.0f,fseed,VCH_GROW_LENGTH,0);

    /*
        Out to the DRAWN surface first: the strike is on the box, and where the terrain has drawn
        a belly or a drip under it, that point is inside the rock. Marched out along the normal
        until the field is open. Against the plain boxes it already is, and nothing moves.
    */
    vec3 surface = VineMarchOut(field,anchor,n);
    /*
        The main strand. Its path begins a little INSIDE the surface, so the trunk comes out of the
        rock rather than starting on it; the walk begins just outside, heading out along the normal
        until the species bends it.
    */
    VineStrand main;
    main.path.seed = seed;
    main.path.thickness = thickness;
    main.path.f_rooted = true;      //out of the rock, full thickness - see VinePath::f_rooted
    /*
        A hanging plant starts out along the normal and lets gravity bend it. A creeper starts
        ALONG the face: up a wall (leaning off it a little), or across a top to a hashed side -
        and its leaves' frame stands out of the face, so they grow off the wall, not into it.
    */
    vec3 heading = n;
    if (sp.hug > 0.0f){
        vec3 up_along = vec3(0.0f,1.0f,0.0f) - n * n.y;
        if (up_along.length() > 0.3f){
            up_along.normalize();
            heading = up_along + n * 0.3f;
        }else{
            float side = (Hash01(0.0f,fseed,VCH_BRANCH_SIDE,99) < 0.5f) ? -1.0f : 1.0f;
            heading = vec3(side,0.0f,0.0f) + n * 0.2f;
        }
        heading.normalize();
    }
    //A creeper starts lying against the face at its keep; a hanging plant just off it.
    float lift = (sp.hug > 0.0f) ? fmaxf(radius + sp.clearance,0.02f) : 0.02f;
    WalkStrand(sp,radius,surface + n * lift,heading,length,fseed,0,field,main);
    if (sp.hug > 0.0f){
        main.path.up = n;
    }
    main.path.points.insert(main.path.points.begin(),surface - n * 0.1f);
    if (main.path.points.size() < 3){
        return false;
    }
    out.strands.push_back(main);

    //--- Branches: off the main strand's built curve, turned off its heading to one side ---
    Spline trunk;
    if (!BuildVineSpline(out.strands[0].path,trunk)){
        return true;
    }
    int span = sp.branch_max - sp.branch_min + 1;
    int count = sp.branch_min + ((span > 0) ? (int)(Hash01(0.0f,fseed,VCH_BRANCH_COUNT,0) * (float)span) : 0);
    if (count > sp.branch_max){ count = sp.branch_max; }
    for (int b = 0; b < count; b++){
        float frac = sp.branch_from + (sp.branch_to - sp.branch_from) * Hash01(0.0f,fseed,VCH_BRANCH_AT,b);
        float s_b = frac * trunk.GetLength();
        vec3 at = trunk.PositionAt(s_b);
        vec3 t = trunk.TangentAt(s_b);
        float a = sp.branch_angle_deg * (VINE_PI / 180.0f) *
                  ((Hash01(0.0f,fseed,VCH_BRANCH_SIDE,b) < 0.5f) ? -1.0f : 1.0f);
        //Turned about the view axis, so the branch leaves across the screen where it shows.
        vec3 dir(t.x * cosf(a) - t.y * sinf(a),t.x * sinf(a) + t.y * cosf(a),t.z);
        dir.normalize();
        VineStrand br;
        br.parent = 0;
        br.s_on_parent = s_b;
        br.path.seed = seed + 1 + b;
        br.path.thickness = thickness * sp.branch_thickness;
        br.path.f_rooted = true;    //out of its parent, as thick as it will be there
        float br_radius = radius * sp.branch_thickness;
        float br_length = length * sp.branch_length * (0.7f + 0.6f * Hash01(0.0f,fseed,VCH_BRANCH_LENGTH,b));
        WalkStrand(sp,br_radius,at + dir * (radius * 0.5f),dir,br_length,fseed,1 + b,field,br);
        //From inside the parent's trunk, like the main strand from inside the rock.
        br.path.points.insert(br.path.points.begin(),at);
        if (br.path.points.size() >= 3){
            out.strands.push_back(br);
        }
    }
    return true;
}

//The channels a spray of roots draws from, clear of a single growth's.
enum{ VCH_ROOT_COUNT = 64, VCH_ROOT_ALONG, VCH_ROOT_DEPTH, VCH_ROOT_LEAN };

bool GrowRoots(const VineSpecies& sp, const VineParams& params, const vec3& anchor, const vec3& normal,
               int seed, const VineField& field, VineGrowth& out){
    vec3 n = normal;
    if (n.length() < 1e-6f){
        return false;
    }
    n.normalize();
    //Along the surface: across the screen, and into it.
    vec3 across = (fabsf(n.z) < 0.9f) ? n.cross(vec3(0.0f,0.0f,1.0f)) : vec3(1.0f,0.0f,0.0f);
    across.normalize();
    vec3 depth = n.cross(across);
    float fs = (float)seed;
    int count = 2 + (int)(Hash01(anchor.x,fs,VCH_ROOT_COUNT,0) * 3.0f);     //2..4
    bool f_any = false;
    for (int i = 0; i < count; i++){
        //Spread a hand's width along the surface, each leaning a little out from the middle.
        float along = 0.36f * Signed(Hash01(anchor.x,fs,VCH_ROOT_ALONG,i));
        float into = 0.24f * Signed(Hash01(anchor.x,fs,VCH_ROOT_DEPTH,i));
        float lean = 0.35f * Signed(Hash01(anchor.x,fs,VCH_ROOT_LEAN,i)) + 0.6f * along;
        vec3 at = anchor + across * along + depth * into;
        vec3 dir = n + across * lean;
        VineGrowth one;
        if (!GrowVine(sp,params,at,dir,seed * 7 + 13 * (i + 1),field,one)){
            continue;
        }
        int base = (int)out.strands.size();
        for (VineStrand& st : one.strands){
            if (st.parent >= 0){
                st.parent += base;
            }
            out.strands.push_back(st);
        }
        f_any = true;
    }
    return f_any;
}

//And a clump of bamboo's.
enum{ VCH_CANE_COUNT = 72, VCH_CANE_ALONG, VCH_CANE_DEPTH, VCH_CANE_LEAN, VCH_CANE_TALL,
      VCH_NODE_TURN, VCH_NODE_SCALE, VCH_NODE_LIFT, VCH_NODE_ROLL };

bool GrowBamboo(const VineSpecies& sp, const VineParams& params, const vec3& anchor, const vec3& normal,
                int seed, const VineField& field, VineGrowth& out){
    vec3 n = normal;
    if (n.length() < 1e-6f){
        return false;
    }
    n.normalize();
    vec3 across = (fabsf(n.z) < 0.9f) ? n.cross(vec3(0.0f,0.0f,1.0f)) : vec3(1.0f,0.0f,0.0f);
    across.normalize();
    vec3 depth = n.cross(across);
    float fs = (float)seed;
    int count = 3 + (int)(Hash01(anchor.x,fs,VCH_CANE_COUNT,0) * 3.0f);     //3..5
    bool f_any = false;
    for (int i = 0; i < count; i++){
        //Where along the surface, -1 .. 1; the middle of the clump is its tallest, as a clump
        //grows outward from its oldest canes.
        float u = Signed(Hash01(anchor.x,fs,VCH_CANE_ALONG,i));
        float into = 0.35f * Signed(Hash01(anchor.x,fs,VCH_CANE_DEPTH,i));
        //Up a wall "along" is up the face, where canes spread less before they all turn up.
        float spread = (fabsf(n.y) > 0.5f) ? 0.45f : 0.25f;
        vec3 at = anchor + across * (spread * u) + depth * into;
        //Leaning a little outward from the middle, and a little either way.
        float lean = 0.08f * Signed(Hash01(anchor.x,fs,VCH_CANE_LEAN,i)) + 0.12f * u;
        vec3 dir = n + across * lean + depth * (0.1f * into);
        VineSpecies cane = sp;
        float tall = (1.0f - 0.4f * fabsf(u)) * (0.85f + 0.15f * Hash01(anchor.x,fs,VCH_CANE_TALL,i));
        cane.length_min *= tall;
        cane.length_max *= tall;
        //Each turns up at its own rate: out of a wall that fans them out, some reaching well clear
        //before they rise - one rate and they all bend alike into a single column. Off a top,
        //where they start up already, it changes nothing.
        cane.gravity *= 0.45f + 0.9f * Hash01(anchor.x,fs,VCH_CANE_TALL,i + 16);
        VineGrowth one;
        if (!GrowVine(cane,params,at,dir,seed * 11 + 17 * (i + 1),field,one)){
            continue;
        }
        for (VineStrand& st : one.strands){
            st.parent = -1;         //a cane is one stalk; the species grows no branches
            out.strands.push_back(st);
            break;
        }
        f_any = true;
    }
    return f_any;
}

void ScatterBambooLeaves(const Spline& spline, const VinePath& path, const VineParams& params,
                         float tile_length, int nodes_per_tile, const VineField* field,
                         std::vector<VineLeaf>& out){
    float length = spline.GetLength();
    float period = tile_length * params.tile_scale * path.thickness;
    if (!spline.IsBuilt() || period <= 1e-3f || nodes_per_tile < 1){
        return;
    }
    //The nodes where the deform lays them: the same count and stretch as DeformAlongSpline.
    int copies = (int)floorf(length / period + 0.5f);
    if (copies < 1){
        copies = 1;
    }
    float node = length / (float)(copies * nodes_per_tile);
    float seed = (float)path.seed;
    float radius = params.tile_radius * params.tile_scale * path.thickness;
    //The lower part is bare, as a cane's is; the last node is under the tip piece.
    int first = (int)ceilf(0.4f * length / node);
    int last = copies * nodes_per_tile - 1;
    float side = (Hash01(0.0f,seed,VCH_NODE_TURN,0) < 0.5f) ? 0.0f : VINE_PI;
    for (int k = (first > 1) ? first : 1; k <= last; k++){
        float s = node * (float)k;
        SplineFrame f = spline.FrameAt(s);
        float young = (float)(k - first) / (float)((last > first) ? (last - first) : 1);
        //One spray per node, alternating sides; near the top a second, the far way round.
        int sprays = (young > 0.6f) ? 2 : 1;
        for (int j = 0; j < sprays; j++){
            float around = side + (float)j * VINE_PI * 0.8f +
                           0.7f * Signed(Hash01(s,seed,VCH_NODE_TURN,j + 1));
            float lift = 0.9f + 0.25f * Signed(Hash01(s,seed,VCH_NODE_LIFT,j));
            /*
                Rolled a quarter turn about the blade, so the spray's face - it fans out across its
                own X - turns toward the camera. Unrolled, a spray leaning across the screen stands
                edge-on to it and reads as a wisp (seen 2026-09-30).
            */
            float roll = 0.5f * VINE_PI + 0.4f * Signed(Hash01(s,seed,VCH_NODE_ROLL,j));
            float scale = params.leaf_scale * (1.0f - 0.3f * young) *
                          (1.0f + params.leaf_scale_jitter * Signed(Hash01(s,seed,VCH_NODE_SCALE,j)));
            vec3 out_dir = f.side * cosf(around) + f.normal * sinf(around);
            //The vine leaf's frame: blade out and on toward the tip, face a quarter further.
            vec3 fwd = f.tangent * cosf(lift) + out_dir * sinf(lift);
            vec3 up  = out_dir * cosf(lift) - f.tangent * sinf(lift);
            vec3 across = up.cross(fwd);
            up = up * cosf(roll) + across * sinf(roll);
            across = up.cross(fwd);
            vec3 stem = f.position + out_dir * (radius * params.leaf_seat);
            if (field && field->Distance(stem + fwd * (params.leaf_length * scale)) < 0.0f){
                continue;
            }
            VineLeaf leaf;
            leaf.kind = VINE_LEAF_BAMBOO;
            leaf.s = s;
            leaf.position = stem;
            leaf.rotation = QuatFromBasis(across,up,fwd);
            leaf.scale = scale;
            out.push_back(leaf);
        }
        side += VINE_PI;
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
