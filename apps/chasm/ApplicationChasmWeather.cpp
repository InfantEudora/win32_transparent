#include "ApplicationChasm.h"
#include "Calendar.h"
#include "Debug.h"
#include "Climate.h"
#include "Weather.h"
#ifdef USE_IMGUI
#include "imgui.h"
#endif
#include <algorithm>
#include <cmath>

static Debugger* debug = new Debugger("ChasmWeather",DEBUG_ALL);

/*
    THE WEATHER, drawn and heard (docs/weather_plan.md; the rules of it in Weather.h). Winter's storm travels with
    the snow's front: the trees bend in its wind, the snow falls thickest where the front is in view, and the score's
    `gust` stinger plays once as the front reaches the view. Autumn has a breeze. View only.

      PHYSICS THREAD (UpdateWeather): the storm at the view, and the big gust - decided, signalled to the cue table,
      and counted, so the render thread rolls it through the trees on the same pass the stinger starts.
      RENDER THREAD (UploadWeather): the weather clock; the wind over the view, baked for core's wind field on the
      ground (Renderer::SetWindField, WIND_PLANE_XZ) which bends every material with wind_flex; the snow.

    THE SWAY is core's (default.vert, WindBendGround): a tree bends from its foot by height squared, so each swaying
    kind has its own copy of the palette material with wind_flex set from its own height - every tree then leans
    the same share of its height in the same wind. Fruit takes its plant's, so it moves with it.

    THE SNOW is one mesh of flakes, drawn once per lattice CELL round the camera (an instance each): the flakes fall
    and drift inside their cell, so they stay put in the world as the camera moves and cells come and go only where
    the snow has already faded out. Density is a share of each cell's flakes, by rank (shaders/chasm_snow.vert). THE
    SPINDRIFT is the same flakes on the same program again, in flat cells along the front's line on the ground.
*/

//The view's ground the wind is baked over: this many of the camera's distances round its centre, on a grid at most
//this many nodes across. Current tuning.
#define WEATHER_VIEW_REACH      1.3f
#define WEATHER_GRID_MAX        32
//The big gust starts this share of the reach upwind of the view's centre, so it is seen coming.
#define WEATHER_BIG_LEAD        0.7f
//The view counts as out of the storm - the big gust may come again - below this.
#define WEATHER_REARM           0.05f
/*
    Every swaying kind's tip leans this share of its MODELLED height for each unit a second of wind (core's wind_flex
    is per height squared, so it is this over the kind's height). The forest draws trees at about 0.7 of that
    (Forest.cpp, TREE_SCALE), and a tree drawn smaller leans less, so a tree leans about 0.04 of its height: a twentieth
    in autumn's breeze, a fifth in the storm, near half as the big gust goes through. Current tuning.
*/
#define WEATHER_SWAY_SHARE      0.055f

/*
    Snow blown off the front (weather_plan.md, B; current tuning): a band along its line where it crosses the view, of
    flat cells on the ground, the flakes racing across low and drawn out - the snow's own program (chasm_snow.vert,
    snow_band). Solid puffs, as the hearths' smoke is, were tried first and read as snowballs rolling about.
*/
#define SPINDRIFT_CELL          4.0f    //the band's cells: this long along the line and this deep across it...
#define SPINDRIFT_HIGH          1.2f    //...this high off the ground...
#define SPINDRIFT_INTO          2.5f    //...reaching this far into the snow from the line
#define SPINDRIFT_CARRY         1.0f    //of the wind, how fast its flakes go: all of it, as they are not falling
#define SPINDRIFT_FALL          0.3f    //units a second
#define SPINDRIFT_SIZE          0.045f
#define SPINDRIFT_STREAK        0.12f
#define SPINDRIFT_WIND_MIN      2.0f    //none in less wind than this on the line...
#define SPINDRIFT_WIND_FULL     7.0f    //...at its thickest from this

//The snow (current tuning): CELL units a side, FLAKES to a cell; seen within REACH of a point AHEAD along the view.
#define SNOW_CELL               16.0f
#define SNOW_FLAKES             140
#define SNOW_REACH              36.0f
#define SNOW_AHEAD              24.0f
#define SNOW_FALL               1.6f    //units a second
#define SNOW_SIZE               0.07f   //a flake's half width
#define SNOW_DRIFT              0.6f    //of the wind, how fast the flakes go with it
#define SNOW_GUST_LAG           0.5f    //seconds of a gust's extra wind a flake is carried by
#define SNOW_STREAK             0.05f   //longer along their motion by this per unit a second of it
#define SNOW_NEAR               2.0f    //faded out this close to the lens
#define SNOW_EASE_S             1.0f    //the snow's thickness follows the storm over about this long
/*
    The snow's and the spindrift's drift wrap at this many of their cells, and each flake goes with it at a share of
    the wind in steps of one over it (chasm_snow.vert, snow_drift_steps): a wrap then carries every flake a whole number
    of cells, which wrapping it back into its cell undoes. Wrapped at ONE cell, with shares anywhere from 0.85 to 1.15,
    each wrap jumped every flake up to 0.15 of a cell at once - the snow stuttering every few seconds (2026-10-19).
*/
#define SNOW_DRIFT_STEPS        32

namespace {

//The smallest power of two at least `raw`: the wind's nodes stay put in the world while the view pans and zooms
//between steps, so a tree between two nodes sees the same wind.
float SnapStep(float raw){
    float s = 1.0f;
    while (s < raw){
        s *= 2.0f;
    }
    return s;
}

//0..1 from a flake's number and a salt.
float FlakeHash(uint32_t n, uint32_t salt){
    uint32_t h = n * 2654435761u + salt * 0x9E3779B9u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return (float)(h >> 8) / (float)(1u << 24);
}

}

//--- Building ----------------------------------------------------------------------------------------------------

/*
    RENDER THREAD, from Init after LoadProps: the swaying kinds' materials - the trees and the bushes - each the palette
    with wind_flex for its own height. With no wind field set they draw exactly as the palette does.
*/
void ApplicationChasm::BuildSwayMaterials(){
    std::fill(sway_material,sway_material + PROP_KIND_COUNT,-1);
    Material* palette = renderer->GetMaterial(palette_material);
    if (!palette){
        return;
    }
    int made = 0;
    for (int k = 0; k < PROP_KIND_COUNT; k++){
        bool f_sways = ForestIsTree(k) || ForestSeasonal(k) == FOREST_SEASONAL_BUSH;
        Mesh* mesh = f_props_loaded[k] ? assetmanager->GetMeshFromAsset(prop_asset_names[k]) : NULL;
        if (!f_sways || !mesh || !mesh->HasBounds() || mesh->GetBoundsMax().y <= 0.1f){
            continue;
        }
        Material m = *palette;
        m.name = std::string("chasm_palette_sway_") + prop_asset_names[k];
        m.glsl_material.wind_flex = WEATHER_SWAY_SHARE / mesh->GetBoundsMax().y;
        m.glsl_material.wind_mode = 0;
        sway_material[k] = renderer->AddMaterial(m);
        made++;
    }
    debug->Info("Weather: %i swaying kinds\n",made);
}

int ApplicationChasm::PropMaterial(int kind) const {
    return (kind >= 0 && kind < PROP_KIND_COUNT && sway_material[kind] >= 0) ? sway_material[kind] : palette_material;
}

//RENDER THREAD, from BuildScene: the snow's and the spindrift's programs, each with one cell of flakes.
void ApplicationChasm::BuildWeatherScene(){
    snow_shader = new Shader("shaders/chasm_snow.vert","shaders/chasm_snow.frag");
    snow_shader->uniform_callback = std::bind(&ApplicationChasm::SetSnowUniforms,this);
    snow_shader_index = renderer->AddCustomShader(snow_shader);
    /*
        Each flake six corners of one quad, all carrying the flake: where it starts in the cell (0..1), and in the
        normal its RANK - drawn when below the density, so a thin snow is the same flakes thinned - its size and
        speed, and a phase; two more phases in the tangent. The ranks are spread evenly, so a density is a count.
    */
    static const float corners[6][2] = {{-1.0f,-1.0f},{1.0f,-1.0f},{1.0f,1.0f},{-1.0f,-1.0f},{1.0f,1.0f},{-1.0f,1.0f}};
    std::vector<vertex> verts;
    verts.reserve(SNOW_FLAKES * 6);
    for (uint32_t i = 0; i < SNOW_FLAKES; i++){
        vertex v;
        v.pos = vec3(FlakeHash(i,1),FlakeHash(i,2),FlakeHash(i,3));
        v.normal = vec3(((float)i + 0.5f) / (float)SNOW_FLAKES,FlakeHash(i,4),FlakeHash(i,5));
        v.tangent = vec3(FlakeHash(i,6),FlakeHash(i,7),0.0f);
        v.matid = 0;
        for (int c = 0; c < 6; c++){
            v.uv = vec2(corners[c][0],corners[c][1]);
            verts.push_back(v);
        }
    }
    //The same flakes twice: a mesh is drawn by one program, and the spindrift's is the snow's with uniforms of its own.
    spindrift_shader = new Shader("shaders/chasm_snow.vert","shaders/chasm_snow.frag");
    spindrift_shader->uniform_callback = std::bind(&ApplicationChasm::SetSpindriftUniforms,this);
    spindrift_shader_index = renderer->AddCustomShader(spindrift_shader);
    auto flakes = [&](Mesh*& mesh, Object*& set, int program, const char* name){
        mesh = new Mesh();
        mesh->num_materials = 1;
        mesh->SetMeshData(verts.data(),(int)verts.size());
        mesh->mesh_mode = MESH_MODE_SHADER;
        mesh->custom_shader_index = program;
        set = new Object();
        set->name = name;
        set->SetMesh(mesh);
        set->SetMaterialSlot(0,palette_material);
        set->SetVisualOnly(true);
        set->SetPickability(false);
        set->SetCastsShadow(false);
        set->SetInstances(std::vector<fmat4>());
        set->SetVisibility(false);
        world_scene->AddObject(set);
    };
    flakes(snow_mesh,snow_set,snow_shader_index,"Snow");
    flakes(spindrift_mesh,spindrift_set,spindrift_shader_index,"Spindrift");
    spindrift_key = vec4(1e9f,1e9f,1e9f,1e9f);
}

//--- The physics thread ------------------------------------------------------------------------------------------

/*
    PHYSICS THREAD, from UpdateView after the camera: the storm at the view's centre, and the big gust - once, as the
    front's line comes within WEATHER_BAND of the view while it moves, and again only after the view has been out of
    the storm. A storm HELD (the panel, chasm_time) puts the front's line through the view at that strength.
*/
void ApplicationChasm::UpdateWeather(){
    vec2 at(camera_target.x,camera_target.z);
    WeatherDate date = WeatherDateOf(calendar_shown.load());
    float front = SnowFrontNow();
    float held = weather_held.load();
    if (held >= 0.0f){
        date.active = std::min(1.0f,held);
        front = at.y + CalendarSnowWobble(at.x);
    }
    float d = at.y - WeatherFrontLine(front,at.x);
    float storm = date.active * WeatherNear(d);
    weather_storm = storm;
    weather_d = d;
    if (storm < WEATHER_REARM){
        f_weather_armed = true;
    }
    //Not in a pause, where the trees stand still: a gust called then - the front panned into view, or asked for - waits,
    //and comes with its stinger as the game goes on.
    const bool f_paused = main_scene->IsPhysicsPaused();
    bool f_big = !f_paused && f_weather_gust_asked.exchange(false);
    if (!f_paused && f_weather_armed && date.active > 0.5f && std::fabs(d) < WEATHER_BAND){
        f_weather_armed = false;
        f_big = true;
    }
    if (f_big){
        weather_big_count++;
        CuePayload p;
        p.Set("x",at.x).Set("z",at.y).Set("storm",storm);
        cues.Signal("gust",p);
        //The birds in view go up with it rather than ride it out in the crowns.
        BirdRequest r;
        r.op = BIRD_REQUEST_FLUSH;
        r.at = at;
        r.reach = cam_distance * WEATHER_VIEW_REACH;
        {
            std::lock_guard<std::mutex> lock(birds_mutex);
            bird_requests.push_back(r);
        }
        debug->Info("Weather: the big gust, the view %.0f from the front's line\n",d);
    }
    /*
        The heat at the view (Climate.h), for the readout under the calendar: the air by the date, the storm here - a held
        one too, so holding a storm chills the readout - and the snow the ground is drawn with, which a held storm does
        not move; and the chasm's warmth by the rim and the steps down.
    */
    WeatherView heat;
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    if (w && w->grid && w->terrain && w->picker){
        const Grid& g = *w->grid;
        float span = g.bounds_max.y - g.bounds_min.y;
        float north = (span > 0.0f) ? (g.bounds_max.y - at.y) / span : 0.5f;
        int plot = w->picker->Pick(at).plot;
        heat.f_snow = CalendarSnowAt(at,SnowFrontNow());
        heat.heat_air = ClimateAir(calendar_shown.load(),north,storm,heat.f_snow);
        heat.heat_chasm = ClimateChasm(g,*w->terrain,plot,at);
        heat.heat_steps = ClimateStepsDown(*w->terrain,plot);
        heat.f_heat = true;
    }
    std::lock_guard<std::mutex> lock(weather_mutex);
    weather_view.at = at;
    weather_view.ground = camera_target.y;
    weather_view.distance = cam_distance;
    weather_view.big = weather_big_count;
    weather_view.yaw = cam_yaw;
    weather_view.heat_air = heat.heat_air;
    weather_view.heat_chasm = heat.heat_chasm;
    weather_view.heat_steps = heat.heat_steps;
    weather_view.f_snow = heat.f_snow;
    weather_view.f_heat = heat.f_heat;
}

//--- The render thread -------------------------------------------------------------------------------------------

//RENDER THREAD: no wind, no snow - the title, or no world. And no storm for the music and the sun, until the world shows.
void ApplicationChasm::ClearWeather(){
    weather_storm = 0.0f;
    if (f_weather_field){
        renderer->ClearWindField();
        f_weather_field = false;
    }
    if (snow_set && snow_density > 0.0f){
        snow_set->SetVisibility(false);
        snow_density = 0.0f;
    }
    if (spindrift_set && spindrift_set->IsVisible()){
        spindrift_set->SetVisibility(false);
        spindrift_key = vec4(1e9f,1e9f,1e9f,1e9f);
    }
    spindrift_density = 0.0f;
}

/*
    RENDER THREAD, after UploadWeather: how far this frame's wind bends point `at` of prop `tree` - default.vert's
    WindBendGround, STALK, worked through here with the same clock, the same grid and the tree's own material, so a bird
    perched in a crown rides it rather than hanging in the air beside it.
*/
vec3 ApplicationChasm::SwayOffset(int tree, const vec3& at){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    if (!f_weather_field || !w || !w->forest || tree < 0 || tree >= (int)w->forest->props.size()
        || weather_grid_w < 2 || weather_grid_h < 2){
        return vec3(0.0f,0.0f,0.0f);
    }
    const PropInstance& p = w->forest->props[(size_t)tree];
    Material* m = (sway_material[p.kind] >= 0) ? renderer->GetMaterial(sway_material[p.kind]) : NULL;
    float h = at.y - p.pos.y;
    if (!m || h <= 0.0f){
        return vec3(0.0f,0.0f,0.0f);
    }
    //SampleWind: bilinear, none off the grid.
    float gx = (at.x - weather_grid_x0) / weather_grid_step;
    float gz = (at.z - weather_grid_z0) / weather_grid_step;
    if (gx < 0.0f || gz < 0.0f || gx >= (float)(weather_grid_w - 1) || gz >= (float)(weather_grid_h - 1)){
        return vec3(0.0f,0.0f,0.0f);
    }
    int i = (int)gx;
    int j = (int)gz;
    float fx = gx - (float)i;
    float fz = gz - (float)j;
    auto node = [&](int a, int b){
        size_t k = (size_t)2 * ((size_t)b * weather_grid_w + a);
        return vec2(weather_grid[k],weather_grid[k + 1]);
    };
    vec2 wind = (node(i,j) * (1.0f - fx) + node(i + 1,j) * fx) * (1.0f - fz) + (node(i,j + 1) * (1.0f - fx) + node(i + 1,j + 1) * fx) * fz;
    float speed = wind.length();
    if (speed < 1e-4f){
        return vec3(0.0f,0.0f,0.0f);
    }
    const float t = (float)weather_seconds;
    const float phase = p.pos.x * 1.73f + p.pos.z * 2.41f;
    vec3 along(wind.x / speed,0.0f,wind.y / speed);
    vec3 across(-along.z,0.0f,along.x);
    float flutter = 0.25f * sinf(t * 2.3f + phase - 0.15f * h) + 0.1f * sinf(t * 3.9f + phase * 1.7f);
    float bend = m->glsl_material.wind_flex * h * h;
    float push = std::max(-0.9f * h,std::min(0.9f * h,bend * speed * (1.0f + flutter)));
    float sway = std::max(-0.5f * h,std::min(0.5f * h,bend * speed * 0.35f * sinf(t * 1.9f + phase * 2.3f)));
    //WindSwing: the push across the line from the foot only, the point kept at its distance.
    vec3 r = at - p.pos;
    float d = r.length();
    if (d < 1e-5f){
        return vec3(0.0f,0.0f,0.0f);
    }
    vec3 dir = r * (1.0f / d);
    vec3 shove = along * push + across * sway;
    shove = shove - dir * shove.dot(dir);
    vec3 bent = r + shove;
    float l = bent.length();
    return (l > 1e-5f) ? p.pos + bent * (d / l) - at : vec3(0.0f,0.0f,0.0f);
}

//RENDER THREAD, from PreRender: the weather clock, the wind over the view, the snow.
void ApplicationChasm::UploadWeather(){
    auto now = std::chrono::steady_clock::now();
    float dt = 0.0f;
    if (weather_last.time_since_epoch().count() != 0){
        dt = std::min(0.1f,std::chrono::duration<float>(now - weather_last).count());
    }
    weather_last = now;
    //The weather clock stands still in a pause, as the water and the mist do (the user's, 2026-10-18): the flakes hang
    //where they were and the trees hold their bend. Running, it keeps real time at any speed.
    const float run = world_scene->IsPhysicsPaused() ? 0.0f : dt;
    weather_seconds += (double)run;

    WeatherView v;
    {
        std::lock_guard<std::mutex> lock(weather_mutex);
        v = weather_view;
    }
    std::shared_ptr<const Grid> g = GetGrid();
    if (!g || v.distance <= 0.0f){
        ClearWeather();
        return;
    }
    WeatherDate date = WeatherDateOf(calendar_shown.load());
    float front = SnowFrontNow();
    float held = weather_held.load();
    if (held >= 0.0f){
        date.active = std::min(1.0f,held);
        front = v.at.y + CalendarSnowWobble(v.at.x);
    }
    const float strength = weather_strength.load();
    const float reach = v.distance * WEATHER_VIEW_REACH;
    const vec2 dir = WeatherWindDir();
    const float along = dir.x * v.at.x + dir.y * v.at.y;
    //A big gust the physics thread has called: rolled in from upwind of the view, over until it has passed it.
    if (v.big != weather_big_seen){
        weather_big_seen = v.big;
        weather_big.f_on = true;
        weather_big.start = weather_seconds;
        weather_big.from = along - WEATHER_BIG_LEAD * reach;
    }
    if (weather_big.f_on){
        float at = weather_big.from + (float)((weather_seconds - weather_big.start) * (double)WEATHER_BIG_TRAVEL);
        if (at > along + reach + WEATHER_BIG_WIDTH){
            weather_big.f_on = false;
        }
    }

    //The wind for the trees, on the ground round the view.
    auto t0 = std::chrono::steady_clock::now();
    float step = SnapStep(2.0f * reach / (float)(WEATHER_GRID_MAX - 1));
    float x0 = std::floor((v.at.x - reach) / step) * step;
    float z0 = std::floor((v.at.y - reach) / step) * step;
    int w = std::min(WEATHER_GRID_MAX,(int)std::ceil((v.at.x + reach - x0) / step) + 1);
    int h = std::min(WEATHER_GRID_MAX,(int)std::ceil((v.at.y + reach - z0) / step) + 1);
    weather_grid.resize((size_t)2 * w * h);
    float top = 0.0f;
    for (int j = 0; j < h; j++){
        for (int i = 0; i < w; i++){
            vec2 wind = WeatherWind(date,front,weather_big,vec2(x0 + step * i,z0 + step * j),weather_seconds,strength);
            weather_grid[(size_t)2 * (j * w + i)] = wind.x;
            weather_grid[(size_t)2 * (j * w + i) + 1] = wind.y;
            top = std::max(top,std::fabs(wind.x) + std::fabs(wind.y));
        }
    }
    weather_view_wind = WeatherWind(date,front,weather_big,v.at,weather_seconds,strength);
    if (top > 0.01f){
        renderer->SetWindField(weather_grid.data(),w,h,x0,z0,step,step,(float)weather_seconds,WIND_PLANE_XZ);
        f_weather_field = true;
        weather_grid_x0 = x0;
        weather_grid_z0 = z0;
        weather_grid_step = step;
        weather_grid_w = w;
        weather_grid_h = h;
    }else if (f_weather_field){
        renderer->ClearWindField();
        f_weather_field = false;
    }
    weather_bake_ms = std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now() - t0).count();
    LeanSmoke(date,front,strength);

    /*
        The spindrift: as thick as the wind on the line can lift - thinner as the front slows to a stop - and its flakes
        carried by the wind there. Its cells, one every SPINDRIFT_CELL along the line on the ground there, are laid out
        again only when the view or the front has moved enough to see; none past the view or over the chasm.
    */
    std::shared_ptr<const ChasmWorld> world = GetWorld();
    vec2 line_at(v.at.x,WeatherFrontLine(front,v.at.x));
    spindrift_wind = WeatherWind(date,front,WeatherBigGust(),line_at,weather_seconds,strength);
    float lift = (spindrift_wind.length() - SPINDRIFT_WIND_MIN) / (SPINDRIFT_WIND_FULL - SPINDRIFT_WIND_MIN);
    bool f_line = std::fabs(line_at.y - v.at.y) < reach;
    float drift_want = f_line ? date.active * std::max(0.0f,std::min(1.0f,lift)) : 0.0f;
    spindrift_density += (drift_want - spindrift_density) * std::min(1.0f,dt / SNOW_EASE_S);
    vec2 blown = spindrift_wind * (SPINDRIFT_CARRY * run);
    const float spindrift_wrap = SPINDRIFT_CELL * SNOW_DRIFT_STEPS;
    spindrift_drift = vec2(std::fmod(spindrift_drift.x + blown.x + spindrift_wrap,spindrift_wrap),
                           std::fmod(spindrift_drift.y + blown.y + spindrift_wrap,spindrift_wrap));
    if (spindrift_set && (spindrift_density < 0.002f || !world || !world->terrain || !world->picker)){
        if (spindrift_set->IsVisible()){
            spindrift_set->SetVisibility(false);
        }
        spindrift_key = vec4(1e9f,1e9f,1e9f,1e9f);
    }else if (spindrift_set){
        vec4 key(v.at.x,v.at.y,front,reach);
        if (std::fabs(key.x - spindrift_key.x) + std::fabs(key.y - spindrift_key.y) > 2.0f
            || std::fabs(key.z - spindrift_key.z) > 0.5f || std::fabs(key.w - spindrift_key.w) > 2.0f){
            spindrift_key = key;
            const Terrain& t = *world->terrain;
            std::vector<fmat4> cells;
            for (float x = std::floor((v.at.x - reach) / SPINDRIFT_CELL) * SPINDRIFT_CELL; x <= v.at.x + reach; x += SPINDRIFT_CELL){
                vec2 mid(x + 0.5f * SPINDRIFT_CELL,WeatherFrontLine(front,x + 0.5f * SPINDRIFT_CELL));
                GridPick k = world->picker->Pick(mid);
                if (!k.f_hit || k.plot < 0 || k.plot >= (int)t.level.size() || t.level[k.plot] == TERRAIN_FLOOR){
                    continue;
                }
                vec3 at(x,t.GroundHeight(mid,t.Height(k.plot)) - 0.05f,mid.y - SPINDRIFT_INTO);
                cells.push_back(Object::ComposeTransformScale(at,quat(vec3(0.0f,1.0f,0.0f),0.0f),
                                                              vec3(SPINDRIFT_CELL,SPINDRIFT_HIGH,SPINDRIFT_CELL)));
            }
            bool f_any = !cells.empty();
            spindrift_set->SetInstances(std::move(cells));
            if (spindrift_set->IsVisible() != f_any){
                spindrift_set->SetVisibility(f_any);
            }
        }
    }

    /*
        The snow: as thick as the storm at the view, eased so a jump of the date or the camera does not pop it - on real
        time, so a view panned onto the front in a pause still fills with snow, standing still in the air.
    */
    float storm = date.active * WeatherNear(v.at.y - WeatherFrontLine(front,v.at.x));
    snow_density += (storm - snow_density) * std::min(1.0f,dt / SNOW_EASE_S);
    vec2 drift = weather_view_wind * (SNOW_DRIFT * run);
    const float snow_wrap = SNOW_CELL * SNOW_DRIFT_STEPS;
    snow_drift = vec2(std::fmod(snow_drift.x + drift.x + snow_wrap,snow_wrap),std::fmod(snow_drift.y + drift.y + snow_wrap,snow_wrap));
    if (!snow_set){
        return;
    }
    if (snow_density < 0.002f){
        if (snow_set->IsVisible()){
            snow_set->SetVisibility(false);
        }
        return;
    }
    //The cells round a point ahead along the view, out to SNOW_REACH of it: past that the shader has faded them out.
    Camera* camera = world_scene->camera;
    vec3 eye = camera->GetPosition();
    vec3 look = vec3(v.at.x,v.ground,v.at.y) - eye;
    float l = look.length();
    snow_centre = (l > 1e-3f) ? eye + look * (std::min(0.6f * l,SNOW_AHEAD) / l) : eye;
    std::vector<fmat4> cells;
    int lo[3], hi[3];
    const float c[3] = {snow_centre.x,snow_centre.y,snow_centre.z};
    for (int a = 0; a < 3; a++){
        lo[a] = (int)std::floor((c[a] - SNOW_REACH) / SNOW_CELL);
        hi[a] = (int)std::floor((c[a] + SNOW_REACH) / SNOW_CELL);
    }
    const float corner_reach = SNOW_REACH + SNOW_CELL * 0.87f;
    for (int x = lo[0]; x <= hi[0]; x++){
        for (int y = lo[1]; y <= hi[1]; y++){
            for (int z = lo[2]; z <= hi[2]; z++){
                vec3 at((float)x * SNOW_CELL,(float)y * SNOW_CELL,(float)z * SNOW_CELL);
                vec3 mid = at + vec3(SNOW_CELL * 0.5f,SNOW_CELL * 0.5f,SNOW_CELL * 0.5f);
                if ((mid - snow_centre).length() > corner_reach){
                    continue;
                }
                cells.push_back(Object::ComposeTransformScale(at,quat(vec3(0.0f,1.0f,0.0f),0.0f),vec3(SNOW_CELL,SNOW_CELL,SNOW_CELL)));
            }
        }
    }
    snow_set->SetInstances(std::move(cells));
    if (!snow_set->IsVisible()){
        snow_set->SetVisibility(true);
    }
}

//The flakes' light: the sun as it shines, over a cool sky's share, so they warm and cool with the day and the season.
static vec3 FlakeLight(const DirectionalLight* sun){
    return sun ? sun->color * (0.55f * std::min(1.5f,sun->brightness)) + vec3(0.40f,0.43f,0.50f) : vec3(0.85f,0.88f,0.95f);
}

//RENDER THREAD, from the spindrift's sub-pass: the snow's program on the band along the front's line.
void ApplicationChasm::SetSpindriftUniforms(){
    spindrift_shader->Setint("snow_band",1);
    spindrift_shader->Setfloat("snow_seconds",(float)weather_seconds);
    spindrift_shader->Setfloat("snow_density",spindrift_density);
    spindrift_shader->Setvec3("snow_centre",snow_centre);
    spindrift_shader->Setfloat("snow_reach",SNOW_REACH);
    spindrift_shader->Setfloat("snow_fall",SPINDRIFT_FALL);
    spindrift_shader->Setfloat("snow_size",SPINDRIFT_SIZE);
    spindrift_shader->Setvec2("snow_drift",spindrift_drift);
    spindrift_shader->Setfloat("snow_drift_steps",(float)SNOW_DRIFT_STEPS);
    spindrift_shader->Setvec2("snow_wind",spindrift_wind);
    spindrift_shader->Setfloat("snow_carry",SPINDRIFT_CARRY);
    spindrift_shader->Setfloat("snow_gust_lag",SNOW_GUST_LAG);
    spindrift_shader->Setfloat("snow_streak",SPINDRIFT_STREAK);
    spindrift_shader->Setfloat("snow_near",SNOW_NEAR);
    spindrift_shader->Setvec3("snow_light",FlakeLight(sun));
    glDepthMask(GL_FALSE);
}

//RENDER THREAD, from the snow's sub-pass.
void ApplicationChasm::SetSnowUniforms(){
    snow_shader->Setint("snow_band",0);
    snow_shader->Setfloat("snow_seconds",(float)weather_seconds);
    snow_shader->Setfloat("snow_density",snow_density);
    snow_shader->Setvec3("snow_centre",snow_centre);
    snow_shader->Setfloat("snow_reach",SNOW_REACH);
    snow_shader->Setfloat("snow_fall",SNOW_FALL);
    snow_shader->Setfloat("snow_size",SNOW_SIZE);
    snow_shader->Setvec2("snow_drift",snow_drift);
    snow_shader->Setfloat("snow_drift_steps",(float)SNOW_DRIFT_STEPS);
    snow_shader->Setvec2("snow_wind",weather_view_wind);
    snow_shader->Setfloat("snow_carry",SNOW_DRIFT);
    snow_shader->Setfloat("snow_gust_lag",SNOW_GUST_LAG);
    snow_shader->Setfloat("snow_streak",SNOW_STREAK);
    snow_shader->Setfloat("snow_near",SNOW_NEAR);
    snow_shader->Setvec3("snow_light",FlakeLight(sun));
    //Over what stands, never into the depth buffer: a flake hides nothing.
    glDepthMask(GL_FALSE);
}

//--- The panel and the tool --------------------------------------------------------------------------------------

#ifdef USE_IMGUI
//RENDER THREAD with physics_mutex held, under the Time panel.
void ApplicationChasm::RenderWeatherPanel(){
    if (!ImGui::CollapsingHeader("Weather",ImGuiTreeNodeFlags_DefaultOpen)){
        return;
    }
    WeatherDate date = WeatherDateOf(calendar_shown.load());
    float d = weather_d.load();
    ImGui::Text("the date's: storm %.0f%%, breeze %.0f%%",date.active * 100.0f,date.breeze * 100.0f);
    ImGui::Text("at the view: storm %.2f, %.0f %s the front's line",weather_storm.load(),std::fabs(d),
                (d >= 0.0f) ? "south of" : "north of, under the snow,");
    ImGui::Text("wind %.1f a second, snow %.2f, baked in %.2f ms",weather_view_wind.length(),snow_density,weather_bake_ms);
    {
        std::lock_guard<std::mutex> lock(weather_mutex);
        const WeatherView& v = weather_view;
        ImGui::Text("heat at the view: air %.1f, chasm +%.1f (%i steps down)%s",v.heat_air,v.heat_chasm,v.heat_steps,
                    v.f_snow ? ", under snow" : "");
    }
    ImGui::TextDisabled("big gusts %u, %s",weather_big_count,f_weather_armed ? "the next may come" : "done for this storm");
    float held = weather_held.load();
    bool f_hold = held >= 0.0f;
    if (ImGui::Checkbox("hold a storm over the view##weather",&f_hold)){
        weather_held = f_hold ? std::max(0.0f,weather_storm.load()) : -1.0f;
    }
    if (f_hold){
        float s = weather_held.load();
        if (ImGui::SliderFloat("storm##weather",&s,0.0f,1.0f)){
            weather_held = s;
        }
    }
    float strength = weather_strength.load();
    if (ImGui::SliderFloat("wind strength##weather",&strength,0.0f,3.0f,"x%.2f")){
        weather_strength = strength;
    }
    if (ImGui::Button("big gust now##weather")){
        f_weather_gust_asked = true;
    }
}
#endif

#ifdef USE_MCP
json ApplicationChasm::WeatherStatusJson(){
    auto r = [](float value, float by){
        return std::round(value * by) / by;
    };
    WeatherDate date = WeatherDateOf(calendar_shown.load());
    WeatherView v;
    {
        std::lock_guard<std::mutex> lock(weather_mutex);
        v = weather_view;
    }
    json heat = nullptr;
    if (v.f_heat){
        heat = json{{"air",r(v.heat_air,10.0f)},{"chasm",r(v.heat_chasm,10.0f)},{"total",r(v.heat_air + v.heat_chasm,10.0f)},
                    {"steps_down",v.heat_steps},{"snow",v.f_snow},{"at",json::array({r(v.at.x,10.0f),r(v.at.y,10.0f)})}};
    }
    return json{{"heat",heat},{"storm",r(weather_storm.load(),100.0f)},{"front_distance",r(weather_d.load(),10.0f)},
                {"held",weather_held.load() >= 0.0f},{"strength",r(weather_strength.load(),100.0f)},
                {"dated",json{{"storm",r(date.active,100.0f)},{"breeze",r(date.breeze,100.0f)}}},
                {"wind",r(weather_view_wind.length(),100.0f)},{"snow",r(snow_density,100.0f)},
                {"big_gusts",weather_big_count},{"armed",f_weather_armed},{"bake_ms",r(weather_bake_ms,100.0f)}};
}

//chasm_time's `weather`: storm (0..1) holds a storm over the view, held false lets it go; strength scales the wind;
//gust true calls the big gust now.
void ApplicationChasm::SteerWeather(const json& s){
    if (!s.is_object()){
        return;
    }
    auto it = s.find("held");
    if (it != s.end() && it->is_boolean() && !it->get<bool>()){
        weather_held = -1.0f;
    }
    it = s.find("storm");
    if (it != s.end() && it->is_number()){
        weather_held = std::max(0.0f,std::min(1.0f,it->get<float>()));
    }
    it = s.find("strength");
    if (it != s.end() && it->is_number()){
        weather_strength = std::max(0.0f,std::min(5.0f,it->get<float>()));
    }
    it = s.find("gust");
    if (it != s.end() && it->is_boolean() && it->get<bool>()){
        f_weather_gust_asked = true;
    }
}
#endif
