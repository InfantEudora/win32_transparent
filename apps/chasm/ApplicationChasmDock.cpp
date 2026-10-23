#include "ApplicationChasm.h"
#include "Debug.h"
#include <math.h>
#include <algorithm>

static Debugger* debug = new Debugger("ChasmDock",DEBUG_ALL);

/*
    THE ZEPPELIN DOCKS, DRAWN (docs/steam_plan.md, step 5): on every dock that stands, its mooring MAST at its nose's
    end; once its zeppelin is begun, the SCAFFOLD it is built on, until it is finished (the user's, 2026-10-23); the
    ZEPPELIN as far as it stands, each stage a model of its own; and finished, its MOORING lines to the mast. All are
    chasm_props.glb's, modelled in one frame (blender_chasm_props.py, "The zeppelin"): the dock's ground under its
    middle, the nose along -Z - so each is an instance set on its dock and turned to its nose (ZoneDockFrame), the
    rules' own reckoning of where the zeppelin lies. A dock too small for one has its mast and nothing more. A model
    the file lacks is not drawn, and says so in the log.

    View only: the instances rebuilt when the zones or the economy change.
*/

const char* dock_asset_names[DOCK_PROPS] = {"zeppelin_1","zeppelin_2","zeppelin_3","zeppelin_4","zeppelin_5",
                                            "zeppelin_scaffold","dock_mast","dock_mooring"};
#define DOCK_SCAFFOLD   ZEPPELIN_STAGES     //after the stages, in dock_asset_names
#define DOCK_MAST       (ZEPPELIN_STAGES + 1)
#define DOCK_MOORING    (ZEPPELIN_STAGES + 2)
static_assert(DOCK_PROPS == ZEPPELIN_STAGES + 3,"a model for every stage, the scaffold, the mast and the mooring");

void ApplicationChasm::BuildDockScene(){
    for (int k = 0; k < DOCK_PROPS; k++){
        Mesh* mesh = assetmanager->GetMeshFromAsset(dock_asset_names[k]);
        if (!mesh){
            debug->Info("'%s' not in chasm_props.glb - not drawn (steam_plan.md step 5)\n",dock_asset_names[k]);
            continue;
        }
        mesh->Retain();     //held for good: each set is given it, and SetMesh releases what it replaces
        UseGroundShader(mesh);
        dock_mesh[k] = mesh;
    }
}

//RENDER THREAD, from PreRender.
void ApplicationChasm::UploadDocks(){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    std::shared_ptr<const ZoneState> z = GetZones();
    std::shared_ptr<const EconomyState> e = GetEconomy();
    if (!w){
        return;
    }
    if (!dock_set[0]){
        world_scene->AtTickBoundary([&](){
            for (int k = 0; k < DOCK_PROPS; k++){
                Object* o = new Object();
                o->name = std::string("Dock ") + dock_asset_names[k];
                o->SetMesh(dock_mesh[k] ? dock_mesh[k] : new Mesh());
                o->SetMaterialSlot(0,palette_material);
                o->SetVisualOnly(true);
                o->SetPickability(false);
                o->SetInstances(std::vector<fmat4>());
                o->SetVisibility(false);
                world_scene->AddObject(o);
                dock_set[k] = o;
            }
        });
        if (!dock_set[0]){
            return;
        }
    }
    bool f_ours = z && z->world == w;
    bool f_stock = f_ours && e && e->world == w;
    uint32_t zones_version = f_ours ? z->version : 0xFFFFFFFEu;
    uint32_t economy_version = f_stock ? e->version : 0xFFFFFFFEu;
    if (w == dock_built_world && zones_version == dock_built_zones && economy_version == dock_built_economy){
        return;
    }
    dock_built_world = w;
    dock_built_zones = zones_version;
    dock_built_economy = economy_version;
    std::vector<fmat4> sets[DOCK_PROPS];
    if (f_ours){
        //Docks whose every cell stands - a dock site is the field's stakes and cord (CropMesh).
        std::vector<uint8_t> whole(z->buildings.size(),0);
        for (size_t id = 1; id < z->buildings.size(); id++){
            whole[id] = (z->buildings[id].kind == ZONE_KIND_DOCK && z->buildings[id].size > 0) ? 1 : 0;
        }
        for (size_t c = 0; c < z->field.size(); c++){
            uint32_t id = z->field[c];
            if (id && id < whole.size() && !z->field_built[c]){
                whole[id] = 0;
            }
        }
        const vec3 up(0.0f,1.0f,0.0f);
        for (size_t id = 1; id < whole.size(); id++){
            if (!whole[id]){
                continue;
            }
            ZoneDockLie lie;
            if (!ZoneDockFrame(*w,*z,(uint32_t)id,lie)){
                continue;
            }
            //The models' nose is -Z: turned so it points along the dock's.
            fmat4 m = Object::ComposeTransformScale(vec3(lie.centre.x,lie.ground,lie.centre.y),
                                                    quat(up,atan2f(-lie.nose.x,-lie.nose.y)),vec3(1.0f,1.0f,1.0f));
            sets[DOCK_MAST].push_back(m);
            if (!lie.f_fits){
                continue;       //too small for a zeppelin (the card says so): the mast alone
            }
            int stage = f_stock ? EconomyZeppelinStage(*e,(uint32_t)id) : 0;
            if (stage < ZEPPELIN_STAGES){
                sets[DOCK_SCAFFOLD].push_back(m);
            }else{
                sets[DOCK_MOORING].push_back(m);
            }
            if (stage >= 1){
                sets[std::min(stage,ZEPPELIN_STAGES) - 1].push_back(m);
            }
        }
    }
    for (int k = 0; k < DOCK_PROPS; k++){
        bool f_any = !sets[k].empty();
        dock_set[k]->SetInstances(std::move(sets[k]));
        dock_set[k]->SetVisibility(dock_mesh[k] && f_any);
    }
}
