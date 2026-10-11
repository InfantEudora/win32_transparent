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
#define FIELD_SITE_STAKE    MESH_SITE_STAKE     //a field site's corner stakes, above the ground (MeshBuild.h)
#define FIELD_SITE_CORD     MESH_SITE_CORD      //and the cord strung between them
#define FIELD_FLAG_POLE     1.3f    //a field put first: its pennant's pole over the stake (BuildingMesh's SITE_FLAG_*)
#define FIELD_FLAG_W        0.55f
#define FIELD_FLAG_H        0.34f
/*
    HEMP (goods_plan.md, the user's: a look of its own): from green on, not a low ridge but a row of tall
    plants standing about to a person's shoulder (PERSON_MODEL_SCALE draws one 0.86 tall) - each a narrow
    stalk widening to a crown of fan leaves under a four-sided point, low-poly like the bushes, so its faces
    catch the sun unevenly. Flat-topped, as first built, from above they read as a grid of tiles - hay bales
    once ripe. Every other row is set half a plant along, and each is a little off its row and a little
    taller or shorter than the next, so the field reads as plants rather than a grid.
*/
#define HEMP_CLUMPS         12      //along a row, across the whole cell
#define HEMP_STALK          0.45f   //a plant's foot, of its crown's width
#define HEMP_CROWN          1.15f   //its crown's width, of a ridge's (within the row's: CROP_FILL x this < 1)
#define HEMP_CROWN_BASE     0.6f    //of its height, where the crown is widest
#define HEMP_GREEN_HEIGHT   0.45f
#define HEMP_RIPE_HEIGHT    0.75f
#define HEMP_JITTER         0.12f   //a plant's height, up or down, of its stage's
#define HEMP_WANDER         0.12f   //off its row's middle, either way, of a row's width

namespace {

//The ridges' palette cell by crop (ZONE_CROP_*) when ripe; the base is always earth. Ripe wheat
//and two greens - a patchwork, as in A Little Age, now of the player's choosing - and hemp, whose
//colours are in the crops row (Palette.h), the same all year.
const int crop_columns[ZONE_CROP_COUNT] = {PAL_FIELD,PAL_LEAF_LIGHT,PAL_LEAF,PAL_HEMP};
const int crop_rows[ZONE_CROP_COUNT] = {PAL_TEMPERATE,PAL_TEMPERATE,PAL_TEMPERATE,PAL_CROPS};

/*
    The ridges at each stage (FIELD_STAGE_*): how much of a row's width they cover, how high their
    crest stands, and their colour - tilled earth, then thin sprouts, then a dark green that every
    crop shares, then the crop's own colour at full height. The base and the furrows are the same
    earth throughout, so a field always reads as rows.
*/
const float stage_fill[FIELD_STAGE_COUNT] = {CROP_FILL,0.28f,CROP_FILL,CROP_FILL};
const float stage_height[FIELD_STAGE_COUNT] = {0.08f,0.10f,0.13f,CROP_RIDGE_HEIGHT};
const int stage_columns[FIELD_STAGE_RIPE] = {PAL_PATH,PAL_LEAF_LIGHT,PAL_BUSH};

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

//A field site's first cell still staked out - the one that flies its pennant while it is put first. -1 none.
static int FirstSiteCell(const ZoneState& z, uint32_t id){
    for (size_t c = 0; c < z.field.size(); c++){
        if (z.field[c] == id && !z.field_built[c]){
            return (int)c;
        }
    }
    return -1;
}

//Bare until it shows, sprouts for a while, green for most of its time, ripe near the end.
int FieldStageOf(float growth){
    if (growth < 0.10f){
        return FIELD_STAGE_BARE;
    }
    if (growth < 0.35f){
        return FIELD_STAGE_SPROUTS;
    }
    return (growth < 0.75f) ? FIELD_STAGE_GREEN : FIELD_STAGE_RIPE;
}

void BuildFieldCell(const ChasmWorld& w, const ZoneState& z, int fine_quad, int look, std::vector<vertex>& out){
    const Grid& g = *w.grid;
    const GridQuad& quad = g.fine.quads[fine_quad];
    int cell = quad.parent;
    int k = fine_quad - cell * 4;   //which child: fine quads 4c..4c+3 are coarse cell c's, in corner order
    const vec3 up(0.0f,1.0f,0.0f);

    float level = w.terrain->Height(quad.v[0]);
    vec2 p[4];
    float h[4];
    vec3 base[4];
    for (int i = 0; i < 4; i++){
        p[i] = g.fine.pos[quad.v[i]];
        h[i] = w.terrain->GroundHeight(p[i],level) + FIELD_LIFT;
        base[i] = vec3(p[i].x,h[i],p[i].y);
    }
    /*
        A cell still a site (fields_plan.md): staked out on the untilled ground - a stake at the coarse
        corner this child holds (its own corner 0), a little in, and a cord from it to the midpoints of
        the two edges it touches (its corners 1 and 3), where the children either side carry it on.
    */
    if (cell < (int)z.field_built.size() && !z.field_built[cell]){
        vec2 stake = p[0] + (p[2] - p[0]) * 0.08f;
        float ground = h[0] - FIELD_LIFT;
        const float top = ground + FIELD_SITE_STAKE;
        MeshPost(out,stake,0.04f,ground - 0.1f,top,PAL_TIMBER);
        /*
            A field put first (ZoneBuilding::f_priority) flies the red pennant a building site's scaffold
            does (BuildingMesh, SiteFlag) - once, from the first stake of its first cell still staked out.
        */
        uint32_t site = z.field[cell];
        if (k == 0 && site && site < z.buildings.size() && z.buildings[site].f_priority && FirstSiteCell(z,site) == cell){
            float y1 = top + FIELD_FLAG_POLE;
            MeshPost(out,stake,0.025f,top,y1,PAL_BARK_DARK);
            vec3 a(stake.x,y1 - FIELD_FLAG_H,stake.y), b(stake.x + FIELD_FLAG_W,y1 - FIELD_FLAG_H,stake.y);
            vec3 c(stake.x + FIELD_FLAG_W,y1,stake.y), d(stake.x,y1,stake.y);
            MeshQuad(out,a,b,c,d,vec3(0,0,1),PAL_ACCENT);
            MeshQuad(out,a,b,c,d,vec3(0,0,-1),PAL_ACCENT);
        }
        for (int e = 1; e <= 3; e += 2){
            vec2 to = p[e] + (p[2] - p[e]) * 0.08f;
            float y0 = ground + FIELD_SITE_CORD;
            float y1 = (h[e] - FIELD_LIFT) + FIELD_SITE_CORD;
            MeshCord(out,vec3(stake.x,y0,stake.y),vec3(to.x,y1,to.y),PAL_WALL);
        }
        return;
    }
    /*
        An archery range's cell (range_plan.md), built: trodden earth, as a lot's. Staked out the same as a field
        while it is a site (above). Its targets, bales and arrows are instances of their own
        (ApplicationChasmRange.cpp), not part of this mesh, so the props modelled for them drop in.
    */
    uint32_t on = z.field[cell];
    if (on && on < z.buildings.size() && z.buildings[on].kind == ZONE_KIND_RANGE){
        MeshQuad(out,base[0],base[1],base[2],base[3],up,PAL_PATH);
        return;
    }
    MeshQuad(out,base[0],base[1],base[2],base[3],up,PAL_EARTH);

    //What grows is the crop SOWN on it - the economy's, in the look - or with none, the field's starting
    //crop; which way the rows run in each cell is the cell's own.
    uint32_t hash = MeshHash((uint32_t)cell,0x0F1E1Du);
    uint32_t id = z.field[cell];
    int stage = look & 0x0F;
    int crop = ((look >> 4) & 0x0F) - 1;
    if (crop < 0){
        crop = (id && id < z.buildings.size() && z.buildings[id].crop < ZONE_CROP_COUNT) ? z.buildings[id].crop : ZONE_CROP_WHEAT;
    }
    stage = std::max(0,std::min(FIELD_STAGE_RIPE,stage));
    crop = std::max(0,std::min(ZONE_CROP_COUNT - 1,crop));
    int column = (stage == FIELD_STAGE_RIPE) ? crop_columns[crop] : stage_columns[stage];
    int row = (stage == FIELD_STAGE_RIPE) ? crop_rows[crop] : PAL_TEMPERATE;
    bool f_along_v = ((hash >> 16) & 1) != 0;
    bool f_clumps = crop == ZONE_CROP_HEMP && stage >= FIELD_STAGE_GREEN;

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
    const float half_ridge = row_width * stage_fill[stage] * 0.5f;
    const float ridge_height = stage_height[stage];
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
        if (f_clumps){
            /*
                Hemp's plants, laid out along the whole cell so they carry on from one child into the next; a
                child draws those whose middle is in its quarter - one on the line between two, in a row set half
                a plant along, reaches a little past it, where the child's map carries straight on. Each a stalk
                widening to its crown, then a point: four sides and four faces of the point, which from the
                game's camera are most of what is seen.
            */
            const float slot = (1.0f - 2.0f * CROP_MARGIN) / HEMP_CLUMPS;
            const float full = (stage == FIELD_STAGE_RIPE) ? HEMP_RIPE_HEIGHT : HEMP_GREEN_HEIGHT;
            const int crown_column = (stage == FIELD_STAGE_RIPE) ? PAL_HEMP_HEADS : PAL_BUSH;
            const float crown_w = half_ridge * HEMP_CROWN;
            const float crown_l = slot * 0.45f;
            const float stagger = (r & 1) ? 0.5f : 0.0f;
            const int plants = HEMP_CLUMPS - ((r & 1) ? 1 : 0);
            static const float side_a[4] = {-1.0f,1.0f,1.0f,-1.0f};    //the corners in order round a plant
            static const float side_c[4] = {-1.0f,-1.0f,1.0f,1.0f};
            for (int i = 0; i < plants; i++){
                float centre = CROP_MARGIN + (i + 0.5f + stagger) * slot;
                if (centre < along0 || centre >= along1){
                    continue;
                }
                uint32_t jitter = MeshHash((uint32_t)cell,(uint32_t)(r * HEMP_CLUMPS + i) + 0xC1u);
                float height = full * (1.0f + HEMP_JITTER * ((float)(jitter & 0xFF) / 127.5f - 1.0f));
                float across = mid + row_width * HEMP_WANDER * ((float)((jitter >> 8) & 0xFF) / 127.5f - 1.0f);
                vec3 foot[4], crown[4];
                for (int c = 0; c < 4; c++){
                    foot[c] = map.At(uv(centre + side_a[c] * crown_l * HEMP_STALK,across + side_c[c] * crown_w * HEMP_STALK),
                                     CROP_RIDGE_LIFT);
                    crown[c] = map.At(uv(centre + side_a[c] * crown_l,across + side_c[c] * crown_w),height * HEMP_CROWN_BASE);
                }
                vec3 apex = map.At(uv(centre,across),height);
                vec3 middle = (foot[0] + foot[1] + foot[2] + foot[3]) * 0.25f;
                for (int c = 0; c < 4; c++){
                    int n = (c + 1) % 4;
                    vec3 outward = (foot[c] + foot[n]) * 0.5f - middle;
                    MeshQuad(out,foot[c],foot[n],crown[n],crown[c],outward,column,row);
                    MeshTri(out,crown[c],crown[n],apex,outward + up,crown_column,row);
                }
            }
            continue;
        }
        vec3 foot_lo0 = map.At(uv(a0,lo),CROP_RIDGE_LIFT);
        vec3 foot_lo1 = map.At(uv(a1,lo),CROP_RIDGE_LIFT);
        vec3 foot_hi0 = map.At(uv(a0,hi),CROP_RIDGE_LIFT);
        vec3 foot_hi1 = map.At(uv(a1,hi),CROP_RIDGE_LIFT);
        vec3 crest0 = map.At(uv(a0,mid),ridge_height);
        vec3 crest1 = map.At(uv(a1,mid),ridge_height);
        //The two faces lean away from the crest: each wants the normal pointing out of the ridge and up.
        vec3 across_dir = foot_hi0 - foot_lo0;
        MeshQuad(out,foot_lo0,foot_lo1,crest1,crest0,up - across_dir,column,row);
        MeshQuad(out,crest0,crest1,foot_hi1,foot_hi0,up + across_dir,column,row);
        //A ridge's end, where the field's margin cuts it - only in the child that holds that end.
        vec3 along_dir = crest1 - crest0;
        if (a0 == CROP_MARGIN){
            MeshTri(out,foot_lo0,crest0,foot_hi0,along_dir * -1.0f,column,row);
        }
        if (a1 == 1.0f - CROP_MARGIN){
            MeshTri(out,foot_lo1,crest1,foot_hi1,along_dir,column,row);
        }
    }
}
