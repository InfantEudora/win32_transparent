#include "ApplicationChasm.h"
#include "Calendar.h"
#include "Debug.h"
#include "Palette.h"
#include "Seasons.h"
#include <algorithm>
#include <cmath>

static Debugger* debug = new Debugger("ChasmSeasons",DEBUG_ALL);

/*
    The broadleaves' stages, drawn (Seasons.h, docs/seasons_plan.md step 2). Every stage of every oak
    and bush kind is a mesh of its own, made here at load from the modelled props: its leaves - and a
    bush's own colour - moved onto a fixed season row, part of an oak's canopy left out as it thins,
    the modelled bare oak, and that bare oak budding. Each such mesh is drawn as one more set per
    chunk after the kinds' own (UploadForest), and a standing prop goes into the set of its kind's
    mesh for its stage. Its kind's own set then stays empty.
*/

//When each tree's stages come, in days of the year from spring's first dawn (0-40). Tuning.
#define FOREST_RED_SHARE            0.35f   //of the broadleaves, those that turn red rather than gold
#define FOREST_TURN_FROM            20.0f   //autumn's first day, from which the trees turn...
#define FOREST_TURN_SPREAD          5.0f    //...each on its own day over this many
#define FOREST_TURN_TO_THIN         2.5f    //days from a tree's turning to its thinning
#define FOREST_TURN_TO_BARE         4.5f    //...and to its being bare: the last bare by autumn's end
#define FOREST_BUD_FROM             1.0f    //spring: the first buds a day in, the snow going
#define FOREST_BUD_SPREAD           4.0f
#define FOREST_BUD_TO_GREEN         4.0f
#define FOREST_BUSH_DRY_FROM        30.0f   //bushes keep their leaves, which dry over winter's first days
#define FOREST_BUSH_DRY_SPREAD      3.0f
#define FOREST_BUSH_SPRING_SPREAD   4.0f    //...come new over spring's first
#define FOREST_BUSH_GREEN_FROM      10.0f   //...and darken to summer's over summer's first
#define FOREST_BUSH_GREEN_SPREAD    3.0f
#define FOREST_THIN_KEEP            0.45f   //of an oak's canopy, the share a thinning tree still has
#define FOREST_BUD_KEEP             0.35f   //...and the share a budding one has back
#define FOREST_STAGE_LOOKS_PER_DAY  4       //how often a day the stages are looked at again

const char* forest_bare_names[FOREST_OAK_KINDS] = {"tree_oak_a_bare","tree_oak_b_bare","tree_oak_c_bare"};
const char* forest_fruit_names[FOREST_FRUIT_KINDS] = {"bush_a_fruit","bush_b_fruit","tree_oak_a_fruit","tree_oak_b_fruit",
                                                      "tree_oak_c_fruit"};

int ForestFruitMesh(int kind){
    switch (kind){
    case PROP_BUSH_A:   return 0;
    case PROP_BUSH_B:   return 1;
    case PROP_OAK_A:    return 2;
    case PROP_OAK_B:    return 3;
    case PROP_OAK_C:    return 4;
    }
    return -1;
}

bool ForestFruitShows(int stage){
    return stage != FOREST_STAGE_THIN_GOLD && stage != FOREST_STAGE_THIN_RED && stage != FOREST_STAGE_BARE
           && stage != FOREST_STAGE_BUD;
}

int ForestSeasonal(int kind){
    switch (kind){
    case PROP_OAK_A: case PROP_OAK_B: case PROP_OAK_C:
        return FOREST_SEASONAL_OAK;
    case PROP_BUSH_A: case PROP_BUSH_B: case PROP_SHRUB:
        return FOREST_SEASONAL_BUSH;
    }
    return FOREST_SEASONAL_NONE;
}

const char* ForestStageName(int stage){
    static const char* names[FOREST_STAGES] = {"green","spring","gold","red","thin gold","thin red","bare","bud","winter"};
    return (stage >= 0 && stage < FOREST_STAGES) ? names[stage] : "?";
}

//0..1 from a prop (or a triangle) and a salt: the same every run, on every map.
static float ForestHash(size_t n, uint32_t salt){
    uint32_t h = (uint32_t)n * 2654435761u + salt * 0x9E3779B9u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return (float)(h & 0xFFFFFFu) / 16777216.0f;
}

int ForestStage(int kind, size_t prop, uint64_t tick){
    int what = ForestSeasonal(kind);
    if (what == FOREST_SEASONAL_NONE){
        return FOREST_STAGE_GREEN;
    }
    CalendarDate d = CalendarDateOf(tick);
    float day = (float)(d.season * CALENDAR_DAYS_PER_SEASON + d.day - 1) + d.day_fraction;
    //The first spring had no winter before it, as it has no snow: everything starts in leaf.
    bool f_first_spring = (d.year == 1 && d.season == SEASON_SPRING);
    float h = ForestHash(prop,1);
    bool f_red = ForestHash(prop,2) < FOREST_RED_SHARE;
    float turn = FOREST_TURN_FROM + FOREST_TURN_SPREAD * h;
    if (what == FOREST_SEASONAL_OAK){
        if (f_first_spring){
            return FOREST_STAGE_GREEN;
        }
        float bud = FOREST_BUD_FROM + FOREST_BUD_SPREAD * h;
        if (day < bud){
            return FOREST_STAGE_BARE;
        }
        if (day < bud + FOREST_BUD_TO_GREEN){
            return FOREST_STAGE_BUD;
        }
        if (day < turn){
            return FOREST_STAGE_GREEN;
        }
        if (day < turn + FOREST_TURN_TO_THIN){
            return f_red ? FOREST_STAGE_RED : FOREST_STAGE_GOLD;
        }
        if (day < turn + FOREST_TURN_TO_BARE){
            return f_red ? FOREST_STAGE_THIN_RED : FOREST_STAGE_THIN_GOLD;
        }
        return FOREST_STAGE_BARE;
    }
    if (day < (f_first_spring ? 0.0f : FOREST_BUSH_SPRING_SPREAD * h)){
        return FOREST_STAGE_WINTER;
    }
    if (day < FOREST_BUSH_GREEN_FROM + FOREST_BUSH_GREEN_SPREAD * h){
        return FOREST_STAGE_SPRING;
    }
    if (day < turn){
        return FOREST_STAGE_GREEN;
    }
    if (day < FOREST_BUSH_DRY_FROM + FOREST_BUSH_DRY_SPREAD * h){
        return f_red ? FOREST_STAGE_RED : FOREST_STAGE_GOLD;
    }
    return FOREST_STAGE_WINTER;
}

//The colours a stage moves: the leaves', and a bush's own.
static bool SeasonalColumn(int column){
    return (column >= PAL_LEAF_DARK && column <= PAL_LEAF_LIGHT) || column == PAL_BUSH;
}

/*
    `from`'s triangles into `out`, those of a seasonal colour moved onto `row` - and of those only a
    `keep` share, by each triangle's hash, so a thinner canopy keeps the faces a fuller one does.
    `f_leaves_only` leaves the rest (the trunk) out.
*/
static void SeasonCopy(const std::vector<vertex>& from, int row, float keep, bool f_leaves_only, std::vector<vertex>& out){
    for (size_t t = 0; t + 2 < from.size(); t += 3){
        int column = (int)std::floor(from[t].uv.x * PALETTE_COLS);
        bool f_season = SeasonalColumn(column) && (int)std::floor(from[t].uv.y * PALETTE_ROWS) == PAL_TEMPERATE;
        if (f_season ? (keep < 1.0f && ForestHash(t / 3,7) >= keep) : f_leaves_only){
            continue;
        }
        for (int k = 0; k < 3; k++){
            vertex v = from[t + k];
            if (f_season){
                v.uv = PaletteUV((int)std::floor(v.uv.x * PALETTE_COLS),row);
            }
            out.push_back(v);
        }
    }
}

//RENDER THREAD, from LoadProps: every seasonal kind's stage meshes, and which set draws each stage.
void ApplicationChasm::BuildSeasonLooks(){
    for (int k = 0; k < PROP_KIND_COUNT; k++){
        for (int s = 0; s < FOREST_STAGES; s++){
            season_look_set[k][s] = k;
        }
    }
    season_look_mesh.clear();
    season_look_kind.clear();
    season_look_stage.clear();
    std::vector<vertex> verts;
    for (int k = 0; k < PROP_KIND_COUNT; k++){
        int what = ForestSeasonal(k);
        Mesh* own = (what != FOREST_SEASONAL_NONE && f_props_loaded[k]) ? assetmanager->GetMeshFromAsset(prop_asset_names[k]) : NULL;
        if (!own || own->GetVertices().empty() || own->GetVertices().size() % 3 != 0){
            continue;
        }
        const std::vector<vertex>& leafy = own->GetVertices();
        auto add = [&](int stage, std::vector<vertex>& v){
            Mesh* m = new Mesh();
            m->SetMeshData(v.data(),(int)v.size());
            m->Retain();        //held for good - see object_setmesh_releases_old
            UseGroundShader(m);
            season_look_set[k][stage] = PROP_KIND_COUNT + (int)season_look_mesh.size();
            season_look_mesh.push_back(m);
            season_look_kind.push_back(k);
            season_look_stage.push_back(stage);
        };
        auto copy = [&](int stage, int row, float keep){
            verts.clear();
            SeasonCopy(leafy,row,keep,false,verts);
            add(stage,verts);
        };
        copy(FOREST_STAGE_GREEN,PAL_SUMMER,1.0f);
        copy(FOREST_STAGE_GOLD,PAL_AUTUMN,1.0f);
        copy(FOREST_STAGE_RED,PAL_AUTUMN_RED,1.0f);
        if (what == FOREST_SEASONAL_BUSH){
            copy(FOREST_STAGE_SPRING,PAL_SPRING,1.0f);
            copy(FOREST_STAGE_WINTER,PAL_WINTER,1.0f);
        }else{
            copy(FOREST_STAGE_THIN_GOLD,PAL_AUTUMN,FOREST_THIN_KEEP);
            copy(FOREST_STAGE_THIN_RED,PAL_AUTUMN_RED,FOREST_THIN_KEEP);
            //Bare: the modelled bare oak, whose trunk is this one's (blender_chasm_props.py) - or, with
            //none in the file, the trunk alone.
            Mesh* bare = assetmanager->GetMeshFromAsset(forest_bare_names[k - PROP_OAK_A]);
            std::vector<vertex> bare_verts;
            if (bare && !bare->GetVertices().empty() && bare->GetVertices().size() % 3 == 0){
                bare_verts = bare->GetVertices();
            }else{
                debug->Warn("No '%s' in chasm_props.glb - %s stands as its trunk alone in winter\n",
                            forest_bare_names[k - PROP_OAK_A],prop_asset_names[k]);
                SeasonCopy(leafy,PAL_SUMMER,0.0f,false,bare_verts);
            }
            verts = bare_verts;
            add(FOREST_STAGE_BARE,verts);
            //Budding: bare, with a thin canopy of new leaves back on it.
            verts = bare_verts;
            SeasonCopy(leafy,PAL_SPRING,FOREST_BUD_KEEP,true,verts);
            add(FOREST_STAGE_BUD,verts);
        }
        //A stage this kind has no mesh for is drawn green.
        for (int s = 0; s < FOREST_STAGES; s++){
            if (season_look_set[k][s] == k){
                season_look_set[k][s] = season_look_set[k][FOREST_STAGE_GREEN];
            }
        }
    }
    debug->Info("Seasons: %i stage meshes for the broadleaves, %i sets a chunk\n",(int)season_look_mesh.size(),ForestSetCount());
}

/*
    RENDER THREAD, from UploadForest: the chunks holding a prop whose stage has moved since its
    chunk's sets were built, marked in `dirty` - looked at FOREST_STAGE_LOOKS_PER_DAY times a game day,
    so a forest turning rebuilds a chunk only when one of its trees has.
*/
void ApplicationChasm::ForestStagesMoved(const ForestData& f, std::vector<uint8_t>& dirty){
    uint64_t tick = calendar_shown.load();
    int64_t clock = (int64_t)(tick / (CALENDAR_DAY_TICKS / FOREST_STAGE_LOOKS_PER_DAY));
    if (clock == forest_stage_clock){
        return;
    }
    forest_stage_clock = clock;
    for (size_t i = 0; i < f.props.size() && i < forest_prop_stage.size(); i++){
        int kind = f.props[i].kind;
        if (ForestSeasonal(kind) != FOREST_SEASONAL_NONE && forest_prop_stage[i] != (uint8_t)ForestStage(kind,i,tick)){
            dirty[forest_prop_chunk[i]] = 1;
        }
    }
}
