#include "ApplicationChasm.h"
#include "MeshBuild.h"
#include <math.h>

/*
    THE CRATES, DRAWN (docs/crates_plan.md): every crate lying about, chasm_props.glb's `crate_a` where it
    lies, turned by a hash of its id so no two face the same way. One crate for everything (the user's): the
    same whatever it holds and however much, so it is always plain what it is.

    View only: one instance set for every crate on the map, rebuilt when the economy's crates change - a load
    put down, a crate emptied or built over - which is rare.
*/

#define CRATE_SCALE     1.0f    //drawn as modelled - the rocks' and bushes' scale; trees and their logs are 0.7

//RENDER THREAD, from PreRender.
void ApplicationChasm::UploadCrates(){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    std::shared_ptr<const EconomyState> e = GetEconomy();
    if (!w || !f_crate_loaded){
        return;
    }
    if (!crate_set){
        main_scene->AtTickBoundary([&](){
            crate_set = assetmanager->GetObjectFromAsset(CHASM_CRATE_ASSET);
            if (crate_set){
                crate_set->name = "crates";
                //The GLB names a material it carries no image for; the palette is ours.
                crate_set->SetMaterialSlot(0,palette_material);
                crate_set->SetPickability(false);
                crate_set->SetVisualOnly(true);
                crate_set->SetInstances(std::vector<fmat4>());
                crate_set->SetVisibility(false);
                main_scene->AddObject(crate_set);
            }
        });
        if (!crate_set){
            return;
        }
    }
    bool f_ours = e && e->world == w;
    uint32_t version = f_ours ? e->crates_version : 0xFFFFFFFEu;
    if (w == crate_built_world && version == crate_built_version){
        return;
    }
    crate_built_world = w;
    crate_built_version = version;
    std::vector<fmat4> crates;
    if (f_ours){
        for (const EconomyCrate& c : e->crates){
            if (c.plot < 0 || c.plot >= (int)w->terrain->wet.size()){
                continue;
            }
            float yaw = (float)(MeshHash(c.id,0x63726174u) % 6283u) / 1000.0f;
            float ground = w->terrain->GroundHeight(c.pos,w->terrain->Height(c.plot));
            crates.push_back(Object::ComposeTransformScale(vec3(c.pos.x,ground,c.pos.y),quat(vec3(0.0f,1.0f,0.0f),yaw),
                                                           vec3(CRATE_SCALE,CRATE_SCALE,CRATE_SCALE)));
        }
    }
    bool f_any = !crates.empty();
    crate_set->SetInstances(std::move(crates));
    crate_set->SetVisibility(f_any);
}
