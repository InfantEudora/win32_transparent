#include "ApplicationChasm.h"
#include "MeshBuild.h"
#include <math.h>

/*
    THE CRATES, DRAWN (docs/crates_plan.md): every crate lying about, chasm_props.glb's `crate_a` where it
    lies, turned by a hash of its id so no two face the same way. One crate for everything (the user's): the
    same whatever it holds and however much, so it is always plain what it is. RUBBLE (stone_plan.md) is a crate
    too, but meant to read as stone on the ground: a set of its own, of its pile (LoadProps' `rubble_asset`) - and
    rubble of floatstone, a seam's crystal fallen, another, of its crystal heap (`crystal_asset`).

    View only: one instance set for every crate on the map, one for the rubble and one for the crystal, rebuilt
    when the economy's crates change - a load put down, a crate emptied or built over - which is rare.
*/

#define CRATE_SCALE     1.0f    //drawn as modelled - the rocks' and bushes' scale; trees and their logs are 0.7

float CrateYaw(uint32_t id){
    return (float)(MeshHash(id,0x63726174u) % 6283u) / 1000.0f;
}

int ApplicationChasm::CrateLook(const EconomyCrate& c) const{
    if (EconomyCrateCrystal(c) && crystal_set){
        return CRATE_LOOK_CRYSTAL;
    }
    return (EconomyCrateRubble(c.origin) && rubble_set) ? CRATE_LOOK_RUBBLE : CRATE_LOOK_CRATE;
}

//RENDER THREAD, from PreRender.
void ApplicationChasm::UploadCrates(){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    std::shared_ptr<const EconomyState> e = GetEconomy();
    if (!w){
        return;
    }
    //Each set made once, from its asset - none while the file lacks it.
    auto make_set = [&](Object*& set, const char* asset, const char* name){
        if (set || !asset){
            return;
        }
        world_scene->AtTickBoundary([&](){
            set = assetmanager->GetObjectFromAsset(asset);
            if (set){
                set->name = name;
                //The GLB names a material it carries no image for; the palette is ours.
                set->SetMaterialSlot(0,palette_material);
                set->SetPickability(false);
                set->SetVisualOnly(true);
                set->SetInstances(std::vector<fmat4>());
                set->SetVisibility(false);
                world_scene->AddObject(set);
            }
        });
    };
    make_set(crate_set,f_crate_loaded ? CHASM_CRATE_ASSET : NULL,"crates");
    make_set(rubble_set,rubble_asset,"rubble");
    make_set(crystal_set,crystal_asset,"floatstone lying");
    if (!crate_set && !rubble_set && !crystal_set){
        return;
    }
    bool f_ours = e && e->world == w;
    uint32_t version = f_ours ? e->crates_version : 0xFFFFFFFEu;
    if (w == crate_built_world && version == crate_built_version){
        return;
    }
    crate_built_world = w;
    crate_built_version = version;
    std::vector<fmat4> looks[3];
    const float scales[3] = {CRATE_SCALE,rubble_scale,CHASM_CRYSTAL_SCALE};
    if (f_ours){
        for (const EconomyCrate& c : e->crates){
            if (c.plot < 0 || c.plot >= (int)w->terrain->wet.size()){
                continue;
            }
            int look = CrateLook(c);
            float scale = scales[look];
            float ground = w->terrain->GroundHeight(c.pos,w->terrain->Height(c.plot));
            looks[look].push_back(Object::ComposeTransformScale(vec3(c.pos.x,ground,c.pos.y),
                                  quat(vec3(0.0f,1.0f,0.0f),CrateYaw(c.id)),vec3(scale,scale,scale)));
        }
    }
    Object* sets[3] = {crate_set,rubble_set,crystal_set};
    for (int i = 0; i < 3; i++){
        if (sets[i]){
            bool f_any = !looks[i].empty();
            sets[i]->SetInstances(std::move(looks[i]));
            sets[i]->SetVisibility(f_any);
        }
    }
}
