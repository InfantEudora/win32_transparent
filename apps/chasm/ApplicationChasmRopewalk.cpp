#include "ApplicationChasm.h"
#include "BuildingMesh.h"

/*
    THE ROPEWALKS, DRAWN (docs/goods_plan.md): what tells one from a house - its walk of strands on posts, its
    spinning wheel, and by them the rope it has made and the hemp it has to make more of: a COIL for every
    ROPEWALK_ROPE_A_COIL rope waiting there or part of it, a SHEAF for every rope's worth of hemp or part of it
    (BuildRopewalkYard). So a
    ropewalk with a full pile shows it, and one with no hemp shows that too.

    View only: one mesh for every ropewalk on the map, rebuilt when the zones change or a ropewalk's rope or
    hemp does - a few times a minute at most.
*/

#define ROPEWALK_ROPE_A_COIL    2                   //a full pile, six coils
#define ROPEWALK_HEMP_A_SHEAF   ECONOMY_ROPE_HEMP   //a sheaf, a rope's worth

static_assert((ECONOMY_ROPEWALK_PILE + ROPEWALK_ROPE_A_COIL - 1) / ROPEWALK_ROPE_A_COIL <= 6,
              "BuildRopewalkYard stacks six coils at most");

//RENDER THREAD, from PreRender.
void ApplicationChasm::UploadRopewalks(){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    std::shared_ptr<const ZoneState> z = GetZones();
    std::shared_ptr<const EconomyState> e = GetEconomy();
    if (!w){
        return;
    }
    if (!ropewalk_object){
        world_scene->AtTickBoundary([&](){
            Object* o = new Object();
            o->name = "ropewalks";
            o->SetMesh(new Mesh());
            o->SetMaterialSlot(0,palette_material);
            o->SetPickability(false);
            o->SetVisualOnly(true);
            o->SetVisibility(false);
            world_scene->AddObject(o);
            ropewalk_object = o;
        });
        if (!ropewalk_object){
            return;
        }
    }
    //What is to be drawn: every ropewalk, its coils and its sheaves. Unchanged, nothing to do.
    std::vector<std::array<uint32_t,3>> want;
    bool f_ours = z && z->world == w;
    bool f_stock = f_ours && e && e->world == w;
    if (f_ours){
        for (size_t id = 1; id < z->buildings.size(); id++){
            if (z->buildings[id].kind == ZONE_KIND_ROPEWALK && z->buildings[id].size > 0){
                int rope = f_stock ? EconomyStock(*e,(uint32_t)id,GOOD_ROPE) : 0;
                int hemp = f_stock ? EconomyStock(*e,(uint32_t)id,GOOD_HEMP) : 0;
                want.push_back({(uint32_t)id,(uint32_t)((rope + ROPEWALK_ROPE_A_COIL - 1) / ROPEWALK_ROPE_A_COIL),
                                (uint32_t)((hemp + ROPEWALK_HEMP_A_SHEAF - 1) / ROPEWALK_HEMP_A_SHEAF)});
            }
        }
    }
    uint32_t zones_version = f_ours ? z->version : 0xFFFFFFFEu;
    if (w == ropewalk_built_world && zones_version == ropewalk_built_zones && want == ropewalk_built){
        return;
    }
    ropewalk_built_world = w;
    ropewalk_built_zones = zones_version;
    ropewalk_built = want;
    std::vector<vertex> verts;
    for (const std::array<uint32_t,3>& r : want){
        BuildRopewalkYard(*w,*z,r[0],(int)r[1],(int)r[2],verts);
    }
    if (verts.empty()){
        ropewalk_object->SetVisibility(false);
        return;
    }
    ropewalk_object->GetMesh()->SetMeshData(verts.data(),(int)verts.size());
    UseGroundShader(ropewalk_object->GetMesh());    //after: SetMeshData resets the mode
    ropewalk_object->SetVisibility(true);
}
