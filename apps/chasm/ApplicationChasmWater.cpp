/*
    The chasm's own look (grid_plan.md step 9) - the GL half of WaterMesh.h and Mist.h: the rivers'
    and falls' objects, and the mist and foam re-posed every frame. See the block in
    ApplicationChasm.h for how they are arranged.
*/
#include "ApplicationChasm.h"

#include "Debug.h"
#include "Palette.h"
#include "glad.h"
#ifdef USE_IMGUI
#include "imgui.h"
#endif

static Debugger* debug = new Debugger("ChasmWater",DEBUG_ALL);

#define WATER_DRAG_STEP     3.0f    //a river tool's drag takes a point each time the cursor has gone this far
#define WATER_POND_FLOW     0.12f   //standing water's streaks: hardly drifting, the swamp's pools' and the ponds'

//RENDER THREAD, at Init (from BuildScene): everything made once and reused by every world.
void ApplicationChasm::BuildWaterScene(){
    /*
        The water's material is only what lighting.glsl reads beside the colour, which the shader
        works out: a low roughness for the sun's glint, no metal (nothing here to reflect).
    */
    Material water = {};
    water.name = "chasm_water";
    water.glsl_material.color = vec4(1.0f,1.0f,1.0f,1.0f);
    water.glsl_material.metallic = 0.0f;
    water.glsl_material.roughness = 0.4f;
    renderer->AddMaterial(water);
    water_material = renderer->FindMaterialIndex(water.name);

    //default.vert for the varyings lighting.glsl reads; f_lit for the uniforms it reads.
    water_sheet_shader = new Shader("shaders/default.vert","shaders/chasm_water_sheet.frag");
    water_sheet_shader->f_lit = true;
    water_sheet_shader->uniform_callback = std::bind(&ApplicationChasm::SetWaterSheetUniforms,this);
    water_sheet_shader_index = renderer->AddCustomShader(water_sheet_shader);
    water_flat_shader = new Shader("shaders/default.vert","shaders/chasm_water_flat.frag");
    water_flat_shader->f_lit = true;
    water_flat_shader->uniform_callback = std::bind(&ApplicationChasm::SetWaterFlatUniforms,this);
    water_flat_shader_index = renderer->AddCustomShader(water_flat_shader);
    water_pool_shader = new Shader("shaders/default.vert","shaders/chasm_water_flat.frag");
    water_pool_shader->f_lit = true;
    water_pool_shader->uniform_callback = std::bind(&ApplicationChasm::SetWaterPoolUniforms,this);
    water_pool_shader_index = renderer->AddCustomShader(water_pool_shader);
    water_pond_shader = new Shader("shaders/default.vert","shaders/chasm_water_flat.frag");
    water_pond_shader->f_lit = true;
    water_pond_shader->uniform_callback = std::bind(&ApplicationChasm::SetWaterPondUniforms,this);
    water_pond_shader_index = renderer->AddCustomShader(water_pond_shader);

    auto Make = [&](const char* name){
        Object* o = new Object();
        o->name = name;
        o->SetVisualOnly(true);
        o->SetPickability(false);
        o->SetCastsShadow(false);
        o->SetMaterialSlot(0,water_material);
        o->SetVisibility(false);
        world_scene->AddObject(o);
        return o;
    };
    water_flat = Make("Rivers");
    water_pools = Make("Pools");
    water_ponds = Make("Ponds");
    water_sheets = Make("Falls");

    //The puffs: one shape per shade, lightest first, and a lighter one again for the foam.
    std::vector<vertex> verts;
    const int shades[MIST_VARIANTS] = {PAL_MIST_LIGHT,PAL_MIST,PAL_MIST_DARK};
    for (int i = 0; i < MIST_VARIANTS; i++){
        BuildPuffMesh((uint32_t)i + 1,shades[i],PAL_EFFECTS,verts);
        mist_meshes[i] = new Mesh();
        mist_meshes[i]->SetMeshData(verts.data(),(int)verts.size());
        mist_meshes[i]->Retain();
    }
    BuildPuffMesh(17,PAL_FOAM,PAL_EFFECTS,verts);
    foam_mesh = new Mesh();
    foam_mesh->SetMeshData(verts.data(),(int)verts.size());
    foam_mesh->Retain();

    foam_set = new Object();
    foam_set->name = "Foam set";
    foam_set->SetMesh(foam_mesh);
    foam_set->SetMaterialSlot(0,palette_material);
    foam_set->SetVisualOnly(true);
    foam_set->SetPickability(false);
    foam_set->SetCastsShadow(false);
    foam_set->SetInstances(std::vector<fmat4>());
    foam_set->SetVisibility(false);
    world_scene->AddObject(foam_set);
}

//The simulation's clock in seconds: a paused game holds the water, mist and foam still. The world's, whichever is active.
double ApplicationChasm::SimSeconds(){
    return (double)world_scene->GetPhysicsTick() * (double)world_scene->GetPhysicsTimestep();
}

void ApplicationChasm::SetWaterSheetUniforms(){
    //Both faces: a fall is seen from over the chasm and, past the lip, from above its back.
    glDisable(GL_CULL_FACE);
    water_sheet_shader->Setfloat("water_seconds",(float)SimSeconds());
    //The river's speed: a sheet's uv.y is the river's along carried on (WaterMesh.h).
    water_sheet_shader->Setfloat("flow_speed",WATER_RIVER_FLOW);
    water_sheet_shader->Setfloat("water_top",TERRAIN_WATER_Y);
    water_sheet_shader->Setfloat("mist_top",CHASM_MIST_TOP);
    SetIceUniforms(water_sheet_shader);
}

//Standing water: the streaks hardly drift, and it is murky - a swamp's green-brown, not a river's blue
//(biomes_plan.md step 5). The shader's own defaults are the rivers'.
void ApplicationChasm::SetWaterPoolUniforms(){
    water_pool_shader->Setfloat("water_seconds",(float)SimSeconds());
    water_pool_shader->Setfloat("flow_speed",WATER_POND_FLOW);
    water_pool_shader->Setvec3("water_light",vec3(0.42f,0.52f,0.40f));
    water_pool_shader->Setvec3("water_mid",vec3(0.29f,0.39f,0.31f));
    water_pool_shader->Setvec3("water_deep",vec3(0.20f,0.29f,0.24f));
    water_pool_shader->Setvec3("water_foam",vec3(0.74f,0.76f,0.64f));
    //The swamp's haze on the water, in the palette's swamp-mist cell (BuildScene reads it).
    water_pool_shader->Setfloat("haze_amount",0.6f);
    water_pool_shader->Setvec3("haze_colour",swamp_haze);
    SetIceUniforms(water_pool_shader);
}

//A pond placed by hand (water_tools_plan.md): standing water, as still as a pool, but clear - the shader's own colours,
//the rivers', and no swamp haze.
void ApplicationChasm::SetWaterPondUniforms(){
    water_pond_shader->Setfloat("water_seconds",(float)SimSeconds());
    water_pond_shader->Setfloat("flow_speed",WATER_POND_FLOW);
    SetIceUniforms(water_pond_shader);
}

void ApplicationChasm::SetWaterFlatUniforms(){
    water_flat_shader->Setfloat("water_seconds",(float)SimSeconds());
    water_flat_shader->Setfloat("flow_speed",WATER_RIVER_FLOW);
    SetIceUniforms(water_flat_shader);
}

/*
    RENDER THREAD. The current world's rivers and falls, once per world; the view toggle shows or
    hides them. SetMeshData puts a mesh back to MESH_MODE_NORMAL, so the shader tag goes after it.
*/
void ApplicationChasm::UploadWater(){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    bool f_show = f_view_water;
    if (!w || !w->water || !water_flat || (w == water_built_world && f_show == f_water_shown)){
        return;
    }
    if (w != water_built_world){
        water_built_world = w;
        auto Surface = [&](Object* o, const std::vector<vertex>& verts, int shader_index){
            if (verts.empty()){
                return;
            }
            Mesh* mesh = o->GetMesh();
            if (!mesh){
                mesh = new Mesh();
                mesh->num_materials = 1;
                o->SetMesh(mesh);
            }
            mesh->SetMeshData(const_cast<vertex*>(verts.data()),(int)verts.size());
            mesh->mesh_mode = MESH_MODE_SHADER;
            mesh->custom_shader_index = shader_index;
        };
        Surface(water_flat,w->water->flat,water_flat_shader_index);
        Surface(water_pools,w->water->pools,water_pool_shader_index);
        Surface(water_ponds,w->water->ponds,water_pond_shader_index);
        Surface(water_sheets,w->water->sheets,water_sheet_shader_index);
        debug->Info("Water: %zu river and %zu fall vertices, %zu falls, %d brinks, %d sheets pushed out by rock (%.1f ms)\n",
                    w->water->flat.size(),w->water->sheets.size(),w->terrain->falls.size(),w->water->brinks,
                    w->water->sheets_pushed,w->water->build_ms);
    }
    water_flat->SetVisibility(f_show && !w->water->flat.empty());
    water_pools->SetVisibility(f_show && !w->water->pools.empty());
    water_ponds->SetVisibility(f_show && !w->water->ponds.empty());
    water_sheets->SetVisibility(f_show && !w->water->sheets.empty());
    f_water_shown = f_show;
}

/*
    RENDER THREAD, every frame. Every puff is re-posed from the clock (Mist.h) - a few thousand
    transforms, well under a millisecond - so nothing about the mist is kept between frames but the
    objects. A new world makes more sets only if it has more chunks than any before it.
*/
void ApplicationChasm::UpdateMist(){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    if (!w || !w->mist || !w->water || !foam_set){
        return;
    }
    const MistData& m = *w->mist;
    size_t need = m.chunk_puffs.size() * MIST_VARIANTS;
    if (mist_sets.size() < need){
        world_scene->AtTickBoundary([&](){
            while (mist_sets.size() < need){
                Object* o = new Object();
                o->name = "Mist set " + std::to_string(mist_sets.size());
                int variant = (int)(mist_sets.size() % MIST_VARIANTS);
                o->SetMesh(mist_meshes[variant]);
                o->SetMaterialSlot(0,puff_material);
                o->SetVisualOnly(true);
                o->SetPickability(false);
                //The blanket shades itself: a puff's shadow on the next is what makes it read as
                //heaped rather than painted on.
                o->SetCastsShadow(true);
                o->SetInstances(std::vector<fmat4>());
                o->SetVisibility(false);
                world_scene->AddObject(o);
                mist_sets.push_back(o);
            }
        });
    }
    bool f_show = f_view_mist;
    double seconds = SimSeconds();
    /*
        The mist's sets are filled once per world (and when the view toggle flips): rest poses and
        motion parameters, and the GPU moves every puff from there. A frame costs nothing here.
    */
    if (w != mist_built_world || f_show != f_mist_shown){
        mist_built_world = w;
        f_mist_shown = f_show;
        std::vector<fmat4> sets[MIST_VARIANTS];
        std::vector<vec4> motion[MIST_VARIANTS];
        for (size_t c = 0; c < mist_sets.size() / MIST_VARIANTS; c++){
            if (c < m.chunk_puffs.size() && f_show){
                for (int i : m.chunk_puffs[c]){
                    const MistPuff& p = m.puffs[i];
                    sets[p.variant].push_back(MistRest(p));
                    vec4 mv[2];
                    MistMotion(p,mv);
                    motion[p.variant].push_back(mv[0]);
                    motion[p.variant].push_back(mv[1]);
                }
            }
            for (int k = 0; k < MIST_VARIANTS; k++){
                Object* o = mist_sets[c * MIST_VARIANTS + k];
                bool f_any = !sets[k].empty();
                o->SetInstances(std::move(sets[k]));
                o->SetInstanceMotion(std::move(motion[k]),MIST_MOTION_PAD);
                sets[k].clear();
                motion[k].clear();
                o->SetVisibility(f_any);
            }
        }
    }

    std::vector<fmat4> foam;
    if (f_show){
        //Every sheet as drawn, the springs' as well as the falls' (WaterMeshData::shapes): foam where it
        //meets the mist, spray drifting off it above that.
        const std::vector<WaterSheetShape>& shapes = w->water->shapes;
        //A fall frozen at its lip (chasm_water.glsl, ICE) churns nothing up at its foot.
        float front = SnowFrontNow();
        auto Add = [&](const vec3& pos, float r, int s){
            if (r > 0.02f){
                foam.push_back(Object::ComposeTransformScale(pos,quat(vec3(0.0f,1.0f,0.0f),(float)s * 1.7f),
                                                             vec3(r,r,r)));
            }
        };
        for (int f = 0; f < (int)shapes.size(); f++){
            if (CalendarSnowAt(shapes[f].origin,front)){
                continue;
            }
            for (int s = 0; s < FOAM_PER_FALL; s++){
                vec3 pos;
                float r;
                FoamBall(shapes[f].foot,f,s,seconds,pos,r);
                Add(pos,r,s);
            }
            for (int s = 0; s < SPRAY_PER_FALL; s++){
                vec3 pos;
                float r;
                SprayBall(shapes[f].foot,f,s,seconds,pos,r);
                Add(pos,r,s + FOAM_PER_FALL);
            }
        }
    }
    bool f_foam = !foam.empty();
    foam_set->SetInstances(std::move(foam));
    foam_set->SetVisibility(f_foam);
}

//--- The water tools (water_tools_plan.md step 4) ------------------------------------------------------------------

/*
    PHYSICS THREAD, from UpdatePaint with the river or pond tool in hand. A press starts the drag where it lands - a
    river's source, a pond's middle; held, a river takes a point every WATER_DRAG_STEP and a pond's edge follows the
    cursor; let go, the water is added (PlaceWater) - here, on the physics thread with the pass's lock held, as the N
    key makes a new map - or the drag is turned down, and the panel says why.
*/
void ApplicationChasm::UpdateWaterDrag(const GridPick& hover, bool f_over_scene, bool f_clicked, bool f_held){
    const bool f_river = paint_tool == CHASM_TOOL_RIVER;
    const bool f_on = f_over_scene && hover.f_hit;
    std::vector<vec2> drag;
    {
        std::lock_guard<std::mutex> lock(pick_mutex);
        drag = water_drag;
    }
    bool f_changed = false;
    if (f_clicked && f_on){
        drag.assign(f_river ? 1 : 2,hover.at);
        f_water_dragging = true;
        f_changed = true;
    }else if (f_held && f_water_dragging && f_on){
        if (f_river && (hover.at - drag.back()).length() >= WATER_DRAG_STEP){
            drag.push_back(hover.at);
            f_changed = true;
        }else if (!f_river && drag.size() == 2 && (hover.at - drag[1]).length() > 0.01f){
            drag[1] = hover.at;
            f_changed = true;
        }
    }else if (!f_held && f_water_dragging){
        std::shared_ptr<const Grid> g = GetGrid();
        GridWater water = g ? g->settings.water : GridWater();
        std::string why;
        if (f_river){
            if (f_on && (hover.at - drag.back()).length() > 0.5f){
                drag.push_back(hover.at);
            }
            if (drag.size() >= 2){
                GridPlacedRiver r;
                r.points = drag;
                r.width = water_river_width.load();
                water.rivers.push_back(r);
            }else{
                why = "a river is dragged from its source on";
            }
        }else{
            float radius = std::min(GRID_PLACED_POND_LARGEST,(drag[1] - drag[0]).length());
            if (radius < GRID_PLACED_POND_SMALLEST){
                why = "a pond is dragged out from its middle to its water's edge";
            }else if (g && !ChasmPondFits(g->layout,drag[0],radius,g->bounds_min,g->bounds_max)){
                why = "a pond lies on the plateau, clear of the map's edge, a rim, a crack and the mountain";
            }else{
                GridPlacedPond k;
                k.centre = drag[0];
                k.radius = radius;
                water.ponds.push_back(k);
            }
        }
        if (why.empty() && PlaceWater(water,why)){
            why = f_river ? "river placed: a new map" : "pond placed: a new map";
        }
        drag.clear();
        f_water_dragging = false;
        f_changed = true;
        std::lock_guard<std::mutex> lock(pick_mutex);
        water_status = why;
    }
    if (f_changed){
        std::lock_guard<std::mutex> lock(pick_mutex);
        water_drag = drag;
        pick_version++;
    }
}

bool ApplicationChasm::PlaceWater(const GridWater& water, std::string& why){
    //A new map in the middle of a recording would leave it nothing to replay against.
    if (main_scene->inputcontroller->IsRecording()){
        why = "not while recording: water makes a new map";
        return false;
    }
    std::shared_ptr<const Grid> g = GetGrid();
    if (!g){
        why = "no map";
        return false;
    }
    GridSettings s = g->settings;
    s.water = water;
    RegenerateGrid(s);
    return true;
}

/*
    DEBUG PANEL, under the river and pond tools: the next river's width, and the water this map has - each river by
    how it came to end, each pond by where it lies, each with a button to take it away (a new map again).
*/
void ApplicationChasm::RenderWaterTools(){
#ifdef USE_IMGUI
    std::shared_ptr<const Grid> g = GetGrid();
    if (!g){
        return;
    }
    float width = water_river_width.load();
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::SliderFloat("river width##water",&width,GRID_PLACED_RIVER_NARROWEST,GRID_PLACED_RIVER_WIDEST,"%.1f")){
        water_river_width = width;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("drag a river from its source, a pond out from its middle: each makes the map again");
    const GridWater& water = g->settings.water;
    const ChasmLayout& l = g->layout;
    size_t placed_from = l.rivers.size() - std::min(l.rivers.size(),water.rivers.size());
    static const char* const ends[4] = {"falls over the rim","joins a river","sinks into a hole","runs off the edge"};
    GridWater after = water;
    bool f_change = false;
    for (size_t i = 0; i < water.rivers.size(); i++){
        ImGui::PushID((int)i);
        if (ImGui::SmallButton("x##river") && !f_change){
            after.rivers.erase(after.rivers.begin() + i);
            f_change = true;
        }
        ImGui::SameLine();
        const GridPlacedRiver& r = water.rivers[i];
        int end = (placed_from + i < l.rivers.size()) ? std::max(0,std::min(3,l.rivers[placed_from + i].end)) : 2;
        ImGui::Text("river %zu: %zu points from %.0f, %.0f, width %.1f - %s",i + 1,r.points.size(),r.points.front().x,
                    r.points.front().y,r.width,ends[end]);
        ImGui::PopID();
    }
    for (size_t i = 0; i < water.ponds.size(); i++){
        ImGui::PushID(1000 + (int)i);
        if (ImGui::SmallButton("x##pond") && !f_change){
            after.ponds.erase(after.ponds.begin() + i);
            f_change = true;
        }
        ImGui::SameLine();
        const GridPlacedPond& k = water.ponds[i];
        bool f_there = false;
        for (const ChasmLayout::Pond& p : l.ponds){
            f_there = f_there || (p.centre.x == k.centre.x && p.centre.y == k.centre.y);
        }
        ImGui::Text("pond %zu: at %.0f, %.0f, radius %.1f%s",i + 1,k.centre.x,k.centre.y,k.radius,
                    f_there ? "" : " - left out: not on the plateau");
        ImGui::PopID();
    }
    if (!water.Empty() && ImGui::Button("Take all the water away##water") && !f_change){
        after = GridWater();
        f_change = true;
    }
    std::string status;
    if (f_change){
        if (!PlaceWater(after,status)){
            std::lock_guard<std::mutex> lock(pick_mutex);
            water_status = status;
        }else{
            std::lock_guard<std::mutex> lock(pick_mutex);
            water_status = "taken away: a new map";
        }
    }
    {
        std::lock_guard<std::mutex> lock(pick_mutex);
        status = water_status;
    }
    if (!status.empty()){
        ImGui::TextDisabled("%s",status.c_str());
    }
#endif
}
