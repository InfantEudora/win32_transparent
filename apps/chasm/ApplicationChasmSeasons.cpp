#include "ApplicationChasm.h"
#include "Calendar.h"
#include "Debug.h"
#include "Palette.h"
#include "Seasons.h"
#ifdef USE_IMGUI
#include "imgui.h"
#endif
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

//--- The sun through the year ---------------------------------------------------------------------

/*
    THE SUN (docs/seasons_plan.md, "The sun"): each season's in its middle, and between two middles an ease from one to
    the next - so it is never still for long, and turns fastest across a season's first days, while the palette fades.
    High and as tuned in summer, low and pale in winter, golden in autumn, a shade cool in spring. The azimuth stays
    the reference's, so shadows fall the same way all year and only lengthen. Current tuning.
*/
static const ChasmSun sun_seasons[CALENDAR_SEASONS] = {
    //elevation, azimuth, kelvin, brightness
    {52.0f,CHASM_SUN_AZIMUTH,5700.0f,CHASM_SUN_BRIGHTNESS},     //spring
    {72.0f,CHASM_SUN_AZIMUTH,CHASM_SUN_KELVIN,CHASM_SUN_BRIGHTNESS},
    {38.0f,CHASM_SUN_AZIMUTH,4300.0f,CHASM_SUN_BRIGHTNESS},     //autumn
    {24.0f,CHASM_SUN_AZIMUTH,7000.0f,CHASM_SUN_BRIGHTNESS},     //winter
};
/*
    The sun turns in steps of this many degrees, not smoothly: a shadow map whose light turns a little every frame is
    drawn afresh every frame, and its edges crawl. A step moves a house's shadow by a tenth of a unit at most.
*/
#define SUN_TURN_STEP           0.25f
/*
    How low and high a held sun may go. Below about 18 degrees flat ground shades ITSELF all over: the shadow lookup's
    normal offset (lighting.glsl, CalcShadow) lifts a point by reach * sin, where a grazing light needs reach * sin / cos^2,
    so every tap of the filter lands under the ground's own depth - measured on seed 12, lit at 20, dark at 16, near and
    far alike. Straight overhead, the sun has no up to look along.
*/
#define SUN_ELEVATION_MIN       20.0f
#define SUN_ELEVATION_MAX       85.0f
//Under the storm at full: this share dimmer and this many kelvin colder; eased by this share a pass, so about two
//seconds, and a pan across the front is not a flicker. Current tuning.
#define SUN_STORM_DIM           0.30f
#define SUN_STORM_COOL          1500.0f
#define SUN_STORM_EASE          0.01f

ChasmSun ChasmSunOf(uint64_t tick, int* from, int* to, float* t){
    CalendarDate d = CalendarDateOf(tick);
    const float season_days = (float)CALENDAR_DAYS_PER_SEASON;
    const float year_days = season_days * (float)CALENDAR_SEASONS;
    //Days since spring's middle, the year round. The first spring has no winter before it: its own sun to its middle.
    float days = ((float)d.season + d.season_fraction - 0.5f) * season_days;
    if (d.year == 1 && days < 0.0f){
        days = 0.0f;
    }
    days = fmodf(days + year_days,year_days);
    int a = std::min(CALENDAR_SEASONS - 1,(int)(days / season_days));
    int b = (a + 1) % CALENDAR_SEASONS;
    float f = days / season_days - (float)a;
    f = f * f * (3.0f - 2.0f * f);
    const ChasmSun& s0 = sun_seasons[a];
    const ChasmSun& s1 = sun_seasons[b];
    if (from){
        *from = a;
    }
    if (to){
        *to = b;
    }
    if (t){
        *t = f;
    }
    ChasmSun out;
    out.elevation = s0.elevation + (s1.elevation - s0.elevation) * f;
    out.azimuth = s0.azimuth + (s1.azimuth - s0.azimuth) * f;
    out.kelvin = s0.kelvin + (s1.kelvin - s0.kelvin) * f;
    out.brightness = s0.brightness + (s1.brightness - s0.brightness) * f;
    return out;
}

/*
    A black body's colour at `kelvin`, 0..1 a channel: Tanner Helland's fit, good to a few percent from 1000 K to
    40000 K - which is all a sun's warmth needs. Only ever used as a ratio against CHASM_SUN_KELVIN's.
*/
static vec3 BlackBody(float kelvin){
    float t = std::max(10.0f,std::min(400.0f,kelvin / 100.0f));
    float r = (t <= 66.0f) ? 255.0f : 329.698727446f * powf(t - 60.0f,-0.1332047592f);
    float g = (t <= 66.0f) ? 99.4708025861f * logf(t) - 161.1195681661f : 288.1221695283f * powf(t - 60.0f,-0.0755148492f);
    float b = (t >= 66.0f) ? 255.0f : (t <= 19.0f) ? 0.0f : 138.5177312231f * logf(t - 10.0f) - 305.0447927307f;
    auto unit = [](float v){
        return std::max(0.0f,std::min(255.0f,v)) / 255.0f;
    };
    return vec3(unit(r),unit(g),unit(b));
}

vec3 ChasmSunColour(float kelvin){
    //The colour matched against the reference (grid_plan.md step 4): what CHASM_SUN_KELVIN shines.
    const vec3 tuned(1.0f,0.95f,0.86f);
    vec3 k = BlackBody(kelvin);
    vec3 r = BlackBody(CHASM_SUN_KELVIN);
    vec3 c(tuned.x * k.x / std::max(r.x,0.01f),tuned.y * k.y / std::max(r.y,0.01f),tuned.z * k.z / std::max(r.z,0.01f));
    float top = std::max(c.x,std::max(c.y,c.z));
    return (top > 0.0f) ? c * (1.0f / top) : tuned;
}

vec3 ChasmSunDirection(float elevation, float azimuth){
    const float rad = 3.14159265f / 180.0f;
    float e = elevation * rad;
    float a = azimuth * rad;
    //North is -z and east +x.
    return vec3(sinf(a) * cosf(e),sinf(e),-cosf(a) * cosf(e));
}

//PHYSICS THREAD, from UpdateView: the sun turned to the date's, or the one held, and lit as it.
void ApplicationChasm::UpdateSun(){
    if (!sun){
        return;
    }
    sun_dated = ChasmSunOf(calendar_shown.load());
    ChasmSun now = SunNow();
    //Under winter's storm (weather_plan.md, D) a shade dimmer and colder, and clear again as it passes. A held sun is held.
    sun_storm += (weather_storm.load() - sun_storm) * SUN_STORM_EASE;
    if (!f_sun_held){
        now.brightness *= 1.0f - SUN_STORM_DIM * sun_storm;
        now.kelvin += SUN_STORM_COOL * sun_storm;
    }
    float e = roundf(std::max(SUN_ELEVATION_MIN,std::min(SUN_ELEVATION_MAX,now.elevation)) / SUN_TURN_STEP) * SUN_TURN_STEP;
    float a = roundf(now.azimuth / SUN_TURN_STEP) * SUN_TURN_STEP;
    if (e != sun_turned[0] || a != sun_turned[1]){
        sun_turned[0] = e;
        sun_turned[1] = a;
        //As far out as BuildScene put it: FollowSun's shadow box is sized from there.
        sun_offset = ChasmSunDirection(e,a) * sun_offset.length();
    }
    sun->color = ChasmSunColour(now.kelvin);
    sun->brightness = std::max(0.0f,now.brightness);
}

#ifdef USE_IMGUI
//RENDER THREAD with physics_mutex held, under the Time panel: the date's sun, the one shining, and a hold on it.
void ApplicationChasm::RenderSunPanel(){
    if (!ImGui::CollapsingHeader("Sun",ImGuiTreeNodeFlags_DefaultOpen)){
        return;
    }
    int from = 0;
    int to = 0;
    float t = 0.0f;
    ChasmSun dated = ChasmSunOf(calendar_shown.load(),&from,&to,&t);
    ImGui::Text("the date's: %.1f deg up, %.0f K, x%.2f",dated.elevation,dated.kelvin,dated.brightness);
    ImGui::TextDisabled("  %s's to %s's, %.0f%% of the way",CalendarSeasonName(from),CalendarSeasonName(to),t * 100.0f);
    ChasmSun now = SunNow();
    vec3 c = sun ? sun->color : ChasmSunColour(now.kelvin);
    ImGui::ColorButton("##suncolour",ImVec4(c.x,c.y,c.z,1.0f),ImGuiColorEditFlags_NoTooltip,ImVec2(14.0f,14.0f));
    ImGui::SameLine();
    ImGui::Text("shining: %.2f deg up, az %.0f, %.0f K, x%.2f",sun_turned[0],sun_turned[1],now.kelvin,now.brightness);
    bool f_hold = f_sun_held;
    if (ImGui::Checkbox("hold the sun##sun",&f_hold)){
        if (f_hold && !f_sun_held){
            sun_held = sun_dated;
        }
        f_sun_held = f_hold;
    }
    if (!f_sun_held){
        return;
    }
    ImGui::SliderFloat("elevation##sun",&sun_held.elevation,SUN_ELEVATION_MIN,SUN_ELEVATION_MAX,"%.1f deg");
    ImGui::SliderFloat("azimuth##sun",&sun_held.azimuth,0.0f,360.0f,"%.0f deg from north");
    ImGui::SliderFloat("warmth##sun",&sun_held.kelvin,2500.0f,10000.0f,"%.0f K");
    ImGui::SliderFloat("brightness##sun",&sun_held.brightness,0.0f,3.0f,"%.2f");
    //Each season's own, from the table: its middle in the second year, past the first spring's rule.
    ImGui::TextDisabled("as in:");
    for (int k = 0; k < CALENDAR_SEASONS; k++){
        ImGui::SameLine();
        char label[24];
        snprintf(label,sizeof(label),"%s##sunseason",CalendarSeasonName(k));
        if (ImGui::Button(label)){
            sun_held = ChasmSunOf((uint64_t)CALENDAR_YEAR_TICKS + (uint64_t)k * CALENDAR_SEASON_TICKS + CALENDAR_SEASON_TICKS / 2);
        }
    }
}
#endif

#ifdef USE_MCP
json ApplicationChasm::SunStatusJson(){
    auto r = [](float v, float by){
        return std::round(v * by) / by;
    };
    ChasmSun now = SunNow();
    vec3 c = sun ? sun->color : ChasmSunColour(now.kelvin);
    //As it shines: under the storm too (UpdateSun), which a held sun is not.
    float storm = f_sun_held ? 0.0f : sun_storm;
    return json{{"held",f_sun_held},{"elevation",r(sun_turned[0],100.0f)},{"azimuth",r(sun_turned[1],100.0f)},
                {"kelvin",r(now.kelvin + SUN_STORM_COOL * storm,1.0f)},
                {"brightness",r(sun ? sun->brightness : now.brightness,100.0f)},{"storm",r(storm,100.0f)},
                {"colour",json::array({r(c.x,1000.0f),r(c.y,1000.0f),r(c.z,1000.0f)})},
                {"dated",json{{"elevation",r(sun_dated.elevation,100.0f)},{"kelvin",r(sun_dated.kelvin,1.0f)},
                              {"brightness",r(sun_dated.brightness,100.0f)}}}};
}

//chasm_time's `sun`: any of elevation, azimuth, kelvin, brightness holds the sun there, from the date's for the rest;
//held false lets it go back to the date's.
void ApplicationChasm::SteerSun(const json& s){
    if (!s.is_object()){
        return;
    }
    auto it = s.find("held");
    if (it != s.end() && it->is_boolean() && !it->get<bool>()){
        f_sun_held = false;
        return;
    }
    if (!f_sun_held){
        sun_held = sun_dated;
        f_sun_held = true;
    }
    auto num = [&s](const char* key, float& out){
        auto i = s.find(key);
        if (i != s.end() && i->is_number()){
            out = i->get<float>();
        }
    };
    num("elevation",sun_held.elevation);
    num("azimuth",sun_held.azimuth);
    num("kelvin",sun_held.kelvin);
    num("brightness",sun_held.brightness);
    sun_held.elevation = std::max(SUN_ELEVATION_MIN,std::min(SUN_ELEVATION_MAX,sun_held.elevation));
    sun_held.kelvin = std::max(1000.0f,std::min(40000.0f,sun_held.kelvin));
}
#endif
