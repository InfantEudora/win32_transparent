/*
    The chasm's own look (grid_plan.md step 9) - the GL half of WaterMesh.h and Mist.h: the rivers'
    and falls' objects, and the mist and foam re-posed every frame. See the block in
    ApplicationChasm.h for how they are arranged.
*/
#include "ApplicationChasm.h"

#include "Debug.h"
#include "Palette.h"
#include "glad.h"

static Debugger* debug = new Debugger("ChasmWater",DEBUG_ALL);

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

    auto Make = [&](const char* name){
        Object* o = new Object();
        o->name = name;
        o->SetVisualOnly(true);
        o->SetPickability(false);
        o->SetCastsShadow(false);
        o->SetMaterialSlot(0,water_material);
        o->SetVisibility(false);
        main_scene->AddObject(o);
        return o;
    };
    water_flat = Make("Rivers");
    water_pools = Make("Pools");
    water_sheets = Make("Falls");

    //The puffs: one shape per shade, lightest first, and a lighter one again for the foam.
    std::vector<vertex> verts;
    //The swamp's wisps in a pale grey-green of their own: the foam's white made them snowballs.
    const int shades[MIST_VARIANTS] = {PAL_MIST_LIGHT,PAL_MIST,PAL_MIST_DARK,PAL_SWAMP_MIST};
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
    main_scene->AddObject(foam_set);
}

//The simulation's clock in seconds: a paused game holds the water, mist and foam still.
double ApplicationChasm::SimSeconds(){
    return (double)main_scene->GetPhysicsTick() * (double)main_scene->GetPhysicsTimestep();
}

void ApplicationChasm::SetWaterSheetUniforms(){
    //Both faces: a fall is seen from over the chasm and, past the lip, from above its back.
    glDisable(GL_CULL_FACE);
    water_sheet_shader->Setfloat("water_seconds",(float)SimSeconds());
    water_sheet_shader->Setfloat("flow_speed",0.32f);
}

//Standing water: the streaks hardly drift, and it is murky - a swamp's green-brown, not a river's blue
//(biomes_plan.md step 5). The shader's own defaults are the rivers'.
void ApplicationChasm::SetWaterPoolUniforms(){
    water_pool_shader->Setfloat("water_seconds",(float)SimSeconds());
    water_pool_shader->Setfloat("flow_speed",0.12f);
    water_pool_shader->Setvec3("water_light",vec3(0.42f,0.52f,0.40f));
    water_pool_shader->Setvec3("water_mid",vec3(0.29f,0.39f,0.31f));
    water_pool_shader->Setvec3("water_deep",vec3(0.20f,0.29f,0.24f));
    water_pool_shader->Setvec3("water_foam",vec3(0.74f,0.76f,0.64f));
}

void ApplicationChasm::SetWaterFlatUniforms(){
    water_flat_shader->Setfloat("water_seconds",(float)SimSeconds());
    water_flat_shader->Setfloat("flow_speed",2.2f);
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
        Surface(water_sheets,w->water->sheets,water_sheet_shader_index);
        debug->Info("Water: %zu river and %zu fall vertices, %zu falls (%.1f ms)\n",w->water->flat.size(),
                    w->water->sheets.size(),w->terrain->falls.size(),w->water->build_ms);
    }
    water_flat->SetVisibility(f_show && !w->water->flat.empty());
    water_pools->SetVisibility(f_show && !w->water->pools.empty());
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
    if (!w || !w->mist || !foam_set){
        return;
    }
    const MistData& m = *w->mist;
    size_t need = m.chunk_puffs.size() * MIST_VARIANTS;
    if (mist_sets.size() < need){
        main_scene->AtTickBoundary([&](){
            while (mist_sets.size() < need){
                Object* o = new Object();
                o->name = "Mist set " + std::to_string(mist_sets.size());
                int variant = (int)(mist_sets.size() % MIST_VARIANTS);
                o->SetMesh(mist_meshes[variant]);
                o->SetMaterialSlot(0,puff_material);
                o->SetVisualOnly(true);
                o->SetPickability(false);
                //The blanket shades itself: a puff's shadow on the next is what makes it read as
                //heaped rather than painted on. The swamp's thin wisps cast none - a shadow on the
                //water under a haze would only look like a stain.
                o->SetCastsShadow(variant != MIST_SWAMP_VARIANT);
                o->SetInstances(std::vector<fmat4>());
                o->SetVisibility(false);
                main_scene->AddObject(o);
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
        const std::vector<TerrainFall>& falls = w->terrain->falls;
        for (int f = 0; f < (int)falls.size(); f++){
            for (int s = 0; s < FOAM_PER_FALL; s++){
                vec3 pos;
                float r;
                FoamBall(falls[f],f,s,seconds,pos,r);
                if (r <= 0.02f){
                    continue;
                }
                foam.push_back(Object::ComposeTransformScale(pos,quat(vec3(0.0f,1.0f,0.0f),(float)s * 1.7f),
                                                             vec3(r,r,r)));
            }
        }
    }
    bool f_foam = !foam.empty();
    foam_set->SetInstances(std::move(foam));
    foam_set->SetVisibility(f_foam);
}
