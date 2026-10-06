#include "CropMesh.h"
#include "MeshBuild.h"

/*
    A field is one coarse cell, drawn by its four fine children one at a time - and its rows have to
    run straight on from one child into the next, and follow the cell's real outline, which the fine
    relaxation bends.

    So the rows are laid out in the COARSE cell's own unit square (u, v), once, and each child draws
    the part that falls in its quarter, mapped through its own bilinear. Child k of coarse cell c is
    fine quad 4c + k, (v_k, m_k, centre, m_k-1) (Grid.cpp, Subdivide): corner k of the coarse square,
    the midpoints either side of it, and the middle - a known quarter. Two children agree exactly
    along the edge they share, because both interpolate along it between the same two fine vertices.

    Each row is a low ridge, a roof shape two faces wide, on a dark earth base; the gap between ridges
    is the furrow. The sun lights one face of a ridge and not the other, which is what makes rows read
    at the game's zoom more than any colour would. An even number of rows puts a furrow on the line
    between children rather than splitting a ridge along its length.
*/

#define FIELD_LIFT          0.06f   //the base above the ground's relief, so it never sinks into it
#define CROP_ROWS           6       //ridges across a field (even - see above)
#define CROP_MARGIN         0.05f   //of the cell, bare at its edge: room for a fence (two fields side by side show twice this)
#define CROP_FILL           0.72f   //of a row's width the ridge covers; the rest is furrow
#define CROP_RIDGE_LIFT     0.02f   //a ridge's foot above the base
#define CROP_RIDGE_HEIGHT   0.16f   //its crest above the base

namespace {

//The ridges' palette column by crop (ZONE_CROP_*); the base is always earth. Ripe wheat and two
//greens - a patchwork, as in A Little Age, now of the player's choosing.
const int crop_columns[ZONE_CROP_COUNT] = {PAL_FIELD,PAL_LEAF_LIGHT,PAL_LEAF};

//Child k's quarter of the coarse square: its own corner 0 and the two directions its local s and t
//run in, as coarse (u, v). Its corners are corner k, then the midpoint of edge k, the middle, and the
//midpoint of edge k-1, so s runs toward corner k+1 and t toward corner k-1.
void ChildQuarter(int k, vec2& origin, vec2& s_dir, vec2& t_dir){
    static const vec2 corner[4] = {vec2(0.0f,0.0f),vec2(1.0f,0.0f),vec2(1.0f,1.0f),vec2(0.0f,1.0f)};
    origin = corner[k];
    s_dir = (corner[(k + 1) % 4] - corner[k]) * 0.5f;
    t_dir = (corner[(k + 3) % 4] - corner[k]) * 0.5f;
}

/*
    One child's view of the field: coarse (u, v) to a world position on the ground. The ground height
    follows the child's own two triangles (0,1,2) and (0,2,3), the split the base is drawn with, so a
    ridge's foot never drops below the base between the corners.
*/
class ChildMap{
public:
    ChildMap(const vec2* corners, const float* heights, int k) : p(corners), h(heights){
        ChildQuarter(k,origin,s_dir,t_dir);
    }

    vec3 At(const vec2& uv, float lift) const{
        //The quarter's axes are perpendicular and half a unit long, so local s and t are projections.
        vec2 d = uv - origin;
        float s = d.dot(s_dir) * 4.0f;
        float t = d.dot(t_dir) * 4.0f;
        vec2 xz = MeshBilinear(p,s,t);
        float y = t <= s ? h[0] + s * (h[1] - h[0]) + t * (h[2] - h[1])
                         : h[0] + t * (h[3] - h[0]) + s * (h[2] - h[3]);
        return vec3(xz.x,y + lift,xz.y);
    }

    //The quarter's range in coarse u and v.
    void Range(float& u0, float& u1, float& v0, float& v1) const{
        vec2 a = origin;
        vec2 b = origin + s_dir + t_dir;
        u0 = std::min(a.x,b.x); u1 = std::max(a.x,b.x);
        v0 = std::min(a.y,b.y); v1 = std::max(a.y,b.y);
    }

private:
    const vec2* p;
    const float* h;
    vec2 origin, s_dir, t_dir;
};

}

void BuildFieldCell(const ChasmWorld& w, const ZoneState& z, int fine_quad, std::vector<vertex>& out){
    const Grid& g = *w.grid;
    const GridQuad& quad = g.fine.quads[fine_quad];
    int cell = quad.parent;
    int k = fine_quad - cell * 4;   //which child: fine quads 4c..4c+3 are coarse cell c's, in corner order
    const vec3 up(0.0f,1.0f,0.0f);

    float level = terrain_levels[w.terrain->level[quad.v[0]]].height;
    vec2 p[4];
    float h[4];
    vec3 base[4];
    for (int i = 0; i < 4; i++){
        p[i] = g.fine.pos[quad.v[i]];
        h[i] = w.terrain->GroundHeight(p[i],level) + FIELD_LIFT;
        base[i] = vec3(p[i].x,h[i],p[i].y);
    }
    MeshQuad(out,base[0],base[1],base[2],base[3],up,PAL_EARTH);

    //What grows is the field's crop, the player's choice for the whole field (docs/buildings_plan.md);
    //which way the rows run in each cell is the cell's own.
    uint32_t hash = MeshHash((uint32_t)cell,0x0F1E1Du);
    uint32_t id = z.field[cell];
    int crop = (id && id < z.buildings.size()) ? z.buildings[id].crop : ZONE_CROP_WHEAT;
    int column = crop_columns[std::max(0,std::min(ZONE_CROP_COUNT - 1,crop))];
    bool f_along_v = ((hash >> 16) & 1) != 0;

    ChildMap map(p,h,k);
    float u0, u1, v0, v1;
    map.Range(u0,u1,v0,v1);
    //Work in (along, across): `along` is the way a row runs, `across` the way rows are stacked.
    float along0 = f_along_v ? v0 : u0;
    float along1 = f_along_v ? v1 : u1;
    float across0 = f_along_v ? u0 : v0;
    float across1 = f_along_v ? u1 : v1;
    auto uv = [f_along_v](float along, float across){
        return f_along_v ? vec2(across,along) : vec2(along,across);
    };

    const float row_width = (1.0f - 2.0f * CROP_MARGIN) / CROP_ROWS;
    const float half_ridge = row_width * CROP_FILL * 0.5f;
    float a0 = std::max(along0,CROP_MARGIN);
    float a1 = std::min(along1,1.0f - CROP_MARGIN);
    if (a1 <= a0){
        return;
    }
    for (int r = 0; r < CROP_ROWS; r++){
        float mid = CROP_MARGIN + (r + 0.5f) * row_width;
        //A row lies wholly in one child across the rows (an even count keeps it off the middle line).
        if (mid < across0 || mid > across1){
            continue;
        }
        float lo = mid - half_ridge;
        float hi = mid + half_ridge;
        vec3 foot_lo0 = map.At(uv(a0,lo),CROP_RIDGE_LIFT);
        vec3 foot_lo1 = map.At(uv(a1,lo),CROP_RIDGE_LIFT);
        vec3 foot_hi0 = map.At(uv(a0,hi),CROP_RIDGE_LIFT);
        vec3 foot_hi1 = map.At(uv(a1,hi),CROP_RIDGE_LIFT);
        vec3 crest0 = map.At(uv(a0,mid),CROP_RIDGE_HEIGHT);
        vec3 crest1 = map.At(uv(a1,mid),CROP_RIDGE_HEIGHT);
        //The two faces lean away from the crest: each wants the normal pointing out of the ridge and up.
        vec3 across_dir = foot_hi0 - foot_lo0;
        MeshQuad(out,foot_lo0,foot_lo1,crest1,crest0,up - across_dir,column);
        MeshQuad(out,crest0,crest1,foot_hi1,foot_hi0,up + across_dir,column);
        //A ridge's end, where the field's margin cuts it - only in the child that holds that end.
        vec3 along_dir = crest1 - crest0;
        if (a0 == CROP_MARGIN){
            MeshTri(out,foot_lo0,crest0,foot_hi0,along_dir * -1.0f,column);
        }
        if (a1 == 1.0f - CROP_MARGIN){
            MeshTri(out,foot_lo1,crest1,foot_hi1,along_dir,column);
        }
    }
}
