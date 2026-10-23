#include "ApplicationChasm.h"
#include "BuildingMesh.h"

/*
    THE ROPEWALKS, DRAWN (docs/goods_plan.md): what tells one from a house - its walk of strands on posts, its
    spinning wheel, and by them the rope it has made and the hemp it has to make more of: a COIL for every
    ROPEWALK_ROPE_A_COIL rope waiting there or part of it, a SHEAF for every rope's worth of hemp or part of it
    (BuildRopewalkYard). So a
    ropewalk with a full pile shows it, and one with no hemp shows that too. A SAIL LOFT is drawn here too, the same
    way (steam_plan.md step 4): a BOLT for every SAIL_CANVAS_A_BOLT canvas, a sheaf for every canvas's worth of hemp,
    and its frame bare with neither (BuildSailLoftYard).

    View only: one mesh for every ropewalk and sail loft on the map, rebuilt when the zones change or one's stock
    does - a few times a minute at most.
*/

#define ROPEWALK_ROPE_A_COIL    2                   //a full pile, six coils
#define ROPEWALK_HEMP_A_SHEAF   ECONOMY_ROPE_HEMP   //a sheaf, a rope's worth
#define SAIL_CANVAS_A_BOLT      2                   //a full loft, six bolts
#define SAIL_HEMP_A_SHEAF       ECONOMY_CANVAS_HEMP //a sheaf, a canvas's worth

static_assert((ECONOMY_ROPEWALK_PILE + ROPEWALK_ROPE_A_COIL - 1) / ROPEWALK_ROPE_A_COIL <= 6,
              "BuildRopewalkYard stacks six coils at most");
static_assert((ECONOMY_SAIL_LOFT_PILE + SAIL_CANVAS_A_BOLT - 1) / SAIL_CANVAS_A_BOLT <= 6,
              "BuildSailLoftYard stacks six bolts at most");

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
    //What is to be drawn: every ropewalk and sail loft, its coils or bolts and its sheaves. Unchanged, nothing to do.
    std::vector<std::array<uint32_t,3>> want;
    bool f_ours = z && z->world == w;
    bool f_stock = f_ours && e && e->world == w;
    if (f_ours){
        for (size_t id = 1; id < z->buildings.size(); id++){
            int kind = z->buildings[id].kind;
            if ((kind != ZONE_KIND_ROPEWALK && kind != ZONE_KIND_SAIL_LOFT) || z->buildings[id].size <= 0){
                continue;
            }
            bool f_rope = kind == ZONE_KIND_ROPEWALK;
            int made = f_stock ? EconomyStock(*e,(uint32_t)id,f_rope ? GOOD_ROPE : GOOD_CANVAS) : 0;
            int hemp = f_stock ? EconomyStock(*e,(uint32_t)id,GOOD_HEMP) : 0;
            int a_pile = f_rope ? ROPEWALK_ROPE_A_COIL : SAIL_CANVAS_A_BOLT;
            int a_sheaf = f_rope ? ROPEWALK_HEMP_A_SHEAF : SAIL_HEMP_A_SHEAF;
            want.push_back({(uint32_t)id,(uint32_t)((made + a_pile - 1) / a_pile),(uint32_t)((hemp + a_sheaf - 1) / a_sheaf)});
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
        if (z->buildings[r[0]].kind == ZONE_KIND_SAIL_LOFT){
            BuildSailLoftYard(*w,*z,r[0],(int)r[1],(int)r[2],verts);
        }else{
            BuildRopewalkYard(*w,*z,r[0],(int)r[1],(int)r[2],verts);
        }
    }
    if (verts.empty()){
        ropewalk_object->SetVisibility(false);
        return;
    }
    ropewalk_object->GetMesh()->SetMeshData(verts.data(),(int)verts.size());
    UseGroundShader(ropewalk_object->GetMesh());    //after: SetMeshData resets the mode
    ropewalk_object->SetVisibility(true);
}
