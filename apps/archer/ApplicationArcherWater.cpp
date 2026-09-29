/*
    The waterfall's objects - the GL half of Water.h. See the WATERFALL block in
    ApplicationArcher.h for how they are arranged, and water_plan.md for why.
*/
#include "ApplicationArcher.h"

#include "Debug.h"
#include "Primitives.h"
#include "glad.h"

#include <math.h>

static Debugger* debug = new Debugger("ArcherWater",DEBUG_ALL);

//The foam: a sphere this coarse reads as the reference's faceted balls rather than as marbles.
#define FOAM_SPHERE_SEGMENTS    8
#define FOAM_SPHERE_RINGS       5

/*
    The shaders, materials and objects, once; then every call re-lays the water from the level as
    it is now. RENDER THREAD, from RemeshBackdrop.
*/
void ApplicationArcher::RebuildWater(){
    if (water_scene && main_scene != water_scene){
        return;         //another level's remesh - the water is the world's
    }
    if (!water_scene){
        water_scene = main_scene;

        /*
            The water's own material is only what lighting.glsl reads beside the colour: a low
            roughness for the sun's glint, no metal (this app has no environment to reflect, so
            metal would only darken it) and no emission - the shader's water_fill does that, in
            the water's own colour rather than a fixed one.
        */
        Material water;
        water.name = "ar_water";
        water.glsl_material.color = vec4(0.30f,0.62f,0.86f,1.0f);
        water.glsl_material.metallic = 0.0f;
        water.glsl_material.roughness = 0.35f;
        water.glsl_material.emissive = vec4(0.0f,0.0f,0.0f,0.0f);
        renderer->AddMaterial(water);
        material_water = renderer->FindMaterialIndex(water.name);
        //Foam: near white with the water's blue in it, rough, and a little self-lit, so a ball in
        //the bank's shadow is still a white ball.
        Material foam;
        foam.name = "ar_foam";
        foam.glsl_material.color = vec4(0.88f,0.95f,1.0f,1.0f);
        foam.glsl_material.metallic = 0.0f;
        foam.glsl_material.roughness = 0.75f;
        foam.glsl_material.emissive = vec4(0.88f,0.95f,1.0f,0.30f);
        renderer->AddMaterial(foam);
        material_foam = renderer->FindMaterialIndex(foam.name);

        //Two programs from one source - see the note at the top of archer_water.glsl. default.vert
        //for the varyings lighting.glsl reads; f_lit for the uniforms it reads.
        water_sheet_shader = new Shader("shaders/default.vert","shaders/archer_water_sheet.frag");
        water_sheet_shader->f_lit = true;
        water_sheet_shader->uniform_callback = std::bind(&ApplicationArcher::SetWaterSheetUniforms,this);
        water_sheet_shader_index = renderer->AddCustomShader(water_sheet_shader);
        water_flat_shader = new Shader("shaders/default.vert","shaders/archer_water_flat.frag");
        water_flat_shader->f_lit = true;
        water_flat_shader->uniform_callback = std::bind(&ApplicationArcher::SetWaterFlatUniforms,this);
        water_flat_shader_index = renderer->AddCustomShader(water_flat_shader);

        //World-space vertices and identity transforms throughout, like the terrain.
        auto Make = [&](const char* name) -> Object* {
            Object* o = new Object();
            o->name = name;
            o->SetVisualOnly(true);
            o->SetPickability(false);
            o->SetPosition(vec3(0.0f,0.0f,0.0f));
            o->SetScale(vec3(1.0f,1.0f,1.0f));
            o->SetVisibility(false);
            main_scene->AddObject(o);
            return o;
        };
        water_rocks = Make("water_rocks");
        //The bank's three, since the rocks stand in the bank and must read as the same stone.
        water_rocks->SetMaterialSlot(0,material_grass_back);
        water_rocks->SetMaterialSlot(1,material_soil_back);
        water_rocks->SetMaterialSlot(2,material_rock_back);
        water_rocks->SetPickability(true);
        water_sheets = Make("water_sheets");
        water_sheets->SetMaterialSlot(0,material_water);
        water_sheets->SetCastsShadow(false);
        water_flats = Make("water_flats");
        water_flats->SetMaterialSlot(0,material_water);
        water_flats->SetCastsShadow(false);
        foam_root = Make("water_foam");
        foam_root->SetVisibility(true);
        foam_mesh = MakeSphere(1.0f,FOAM_SPHERE_SEGMENTS,FOAM_SPHERE_RINGS);
        if (foam_mesh){
            foam_mesh->Retain();
        }
    }

    //--- The water as the level has it now ------------------------------------------------------
    BackdropParams bank;
    WaterParams wp;
    std::vector<StageBlock> rocks;
    std::vector<vertex> sheets, flats;
    std::vector<FoamEmitter> emitters;
    for (const StageWater& w : stage.waters){
        int g = WaterGround(w,stage.blocks);
        if (g < 0){
            debug->Err("Water at x %.2f stands on no ground block - not built\n",w.x);
            continue;
        }
        WaterLayout l;
        LayoutWater(w,stage.blocks[g],bank,wp,l);
        BuildWaterRocks(w,l,wp,rocks);
        BuildWaterSheets(w,l,wp,sheets);
        BuildWaterFlats(w,l,wp,flats);
        WaterFoamEmitters(w,l,emitters);
    }

    //The rocks, meshed on their own - see WHY THE SHELF IS ITS OWN MESH in Water.h.
    TerrainRegion all;
    all.x_min = -1e30f; all.x_max = 1e30f;
    all.y_min = -1e30f; all.y_max = 1e30f;
    std::vector<vertex> rock_verts;
    if (!rocks.empty() && BuildTerrainVerts(rocks,all,WaterRockParams(),rock_verts) && !rock_verts.empty()){
        Mesh* mesh = water_rocks->GetMesh();
        if (!mesh){
            mesh = new Mesh();
            water_rocks->SetMesh(mesh);
        }
        mesh->SetMeshData(rock_verts.data(),(int)rock_verts.size());
        water_rocks->SetVisibility(true);
    }else{
        water_rocks->SetVisibility(false);
    }

    //The two surfaces. SetMeshData puts a mesh back to MESH_MODE_NORMAL, so the tag goes after it.
    auto Surface = [&](Object* o, std::vector<vertex>& verts, int shader_index){
        if (verts.empty()){
            o->SetVisibility(false);
            return;
        }
        Mesh* mesh = o->GetMesh();
        if (!mesh){
            mesh = new Mesh();
            mesh->num_materials = 1;
            o->SetMesh(mesh);
        }
        mesh->SetMeshData(verts.data(),(int)verts.size());
        mesh->mesh_mode = MESH_MODE_SHADER;
        mesh->custom_shader_index = shader_index;
        o->SetVisibility(true);
    };
    Surface(water_sheets,sheets,water_sheet_shader_index);
    Surface(water_flats,flats,water_flat_shader_index);

    //The foam: a ball object per slot, made once and hidden while its slot is free.
    foam_swarm.Init(emitters);
    foam_last_tick = -1;
    while (foam_objects.size() < foam_swarm.balls.size() && foam_mesh){
        Object* o = new Object();
        o->name = "foam." + std::to_string(foam_objects.size());
        o->SetMesh(foam_mesh);
        o->SetMaterialSlot(0,material_foam);
        o->SetPickability(false);
        o->SetCastsShadow(false);
        o->SetVisibility(false);
        foam_root->AttachChild(o);
        foam_objects.push_back(o);
    }
    for (Object* o : foam_objects){
        o->SetVisibility(false);
    }
    debug->Info("Water: %i falls, %i rock blocks, %zu sheet and %zu flat vertices, %zu foam slots\n",
                (int)stage.waters.size(),(int)rocks.size(),sheets.size(),flats.size(),
                foam_swarm.balls.size());
}

//Both programs' clocks are the simulation's, so a paused game holds the water still.
void ApplicationArcher::SetWaterSheetUniforms(){
    //Both faces: a sheet seen from just above its lip shows its back.
    glDisable(GL_CULL_FACE);
    float seconds = (float)((double)main_scene->GetPhysicsTick() * ARCHER_DT);
    water_sheet_shader->Setfloat("water_seconds",seconds);
    water_sheet_shader->Setfloat("flow_speed",0.9f);
}

void ApplicationArcher::SetWaterFlatUniforms(){
    glDisable(GL_CULL_FACE);
    float seconds = (float)((double)main_scene->GetPhysicsTick() * ARCHER_DT);
    water_flat_shader->Setfloat("water_seconds",seconds);
    water_flat_shader->Setfloat("flow_speed",0.35f);
}

/*
    The foam, stepped to the simulation's tick and copied onto its balls. RENDER THREAD, from
    PreRender. Catches up the ticks since the last frame - none while paused, a few after a stall,
    capped so a long one does not run them all in one frame - as the streaks do.
*/
void ApplicationArcher::UpdateWater(){
    if (!foam_root || main_scene != water_scene){
        return;
    }
    int64_t tick = main_scene->GetPhysicsTick();
    int64_t steps = (foam_last_tick < 0) ? 1 : tick - foam_last_tick;
    if ((steps < 0) || (steps > 8)){
        steps = 1;
    }
    for (int64_t k = steps - 1; k >= 0; k--){
        foam_swarm.Step(tick - k);
    }
    foam_last_tick = tick;

    for (size_t i = 0; i < foam_objects.size(); i++){
        Object* o = foam_objects[i];
        const FoamBall* b = (i < foam_swarm.balls.size()) ? &foam_swarm.balls[i] : NULL;
        float s = b ? b->Scale() : 0.0f;
        if (s <= 0.002f){
            if (o->IsVisible()){
                o->SetVisibility(false);
            }
            continue;
        }
        o->SetPosition(vec3(b->x,b->y,b->z));
        o->SetScale(vec3(s,s,s));
        if (!o->IsVisible()){
            o->SetVisibility(true);
        }
    }
}
