#include "ApplicationChasm.h"
#include <math.h>
#include <algorithm>

/*
    THE WOODCUTTER'S PILE, DRAWN (docs/economy_plan.md): the wood a hut holds - what its woodcutter
    brought home because no store would take it - stacked as logs outside the hut, one log for every
    ECONOMY_WOOD_PER_TREE wood, on the first open plot beside it. Three logs on the ground, two on them,
    one on top: the six a full pile (ECONOMY_HUT_PILE) holds.

    View only, one instance set for every pile on the map, of the forest's own log; rebuilt when a
    hut's pile changes, which is a few times a minute at most.
*/

#define PILE_SCALE          0.7f    //the forest's draw scale for props (chasm_game_plan: trees at 0.7)
#define PILE_LOG_RADIUS     0.22f   //log_a's, in its own units (tools/blender_chasm_props.py)
#define PILE_TOWARD_HUT     0.12f   //of the way from the open plot back to the hut: clear of its wall, which is halfway

namespace {

//The first open plot beside a hut, by index: nothing built there, dry, on the hut's level, not a road.
//Its pile leans toward `hut`, the hut plot it is beside. -1 when the hut is walled in.
int PileSpot(const ChasmWorld& w, const ZoneState& z, const std::vector<int>& plots, int& hut){
    int best = -1;
    hut = -1;
    for (int v : plots){
        int nb[8];
        int n = ZonePlotNeighbours(w,v,nb,8);
        for (int j = 0; j < n; j++){
            int o = nb[j];
            if (z.storeys[o] > 0 || w.terrain->wet[o] || w.terrain->level[o] != w.terrain->level[v]
                || z.ground[o] == ZONE_GROUND_ROAD){
                continue;
            }
            if (best < 0 || o < best){
                best = o;
                hut = v;
            }
        }
    }
    return best;
}

}

//RENDER THREAD, from PreRender.
void ApplicationChasm::UploadPiles(){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    std::shared_ptr<const ZoneState> z = GetZones();
    std::shared_ptr<const EconomyState> e = GetEconomy();
    if (!w || !f_props_loaded[PROP_LOG]){
        return;
    }
    if (!pile_set){
        main_scene->AtTickBoundary([&](){
            pile_set = assetmanager->GetObjectFromAsset(prop_asset_names[PROP_LOG]);
            if (pile_set){
                pile_set->name = "log piles";
                pile_set->SetMaterialSlot(0,palette_material);
                pile_set->SetPickability(false);
                pile_set->SetVisualOnly(true);
                pile_set->SetInstances(std::vector<fmat4>());
                pile_set->SetVisibility(false);
                main_scene->AddObject(pile_set);
                UseGroundShader(pile_set->GetMesh());
            }
        });
        if (!pile_set){
            return;
        }
    }
    //What is to be drawn: every woodcutter's hut and its log count. Unchanged, nothing to do.
    std::vector<std::pair<uint32_t,int>> want;
    bool f_ours = z && e && z->world == w && e->world == w;
    if (f_ours){
        for (size_t id = 1; id < z->buildings.size(); id++){
            if (z->buildings[id].kind == ZONE_KIND_WOODCUTTER && z->buildings[id].size > 0){
                int wood = EconomyStock(*e,(uint32_t)id,GOOD_WOOD);
                if (wood > 0){
                    want.push_back(std::make_pair((uint32_t)id,(wood + ECONOMY_WOOD_PER_TREE - 1) / ECONOMY_WOOD_PER_TREE));
                }
            }
        }
    }
    uint32_t zones_version = f_ours ? z->version : 0;
    if (w == pile_built_world && want == pile_built && zones_version == pile_built_zones){
        return;
    }
    pile_built_world = w;
    pile_built = want;
    pile_built_zones = zones_version;
    std::vector<fmat4> logs;
    if (!want.empty()){
        std::vector<std::vector<int>> plots_of(z->buildings.size());
        for (size_t v = 0; v < z->building.size(); v++){
            if (z->building[v] && z->buildings[z->building[v]].kind == ZONE_KIND_WOODCUTTER){
                plots_of[z->building[v]].push_back((int)v);
            }
        }
        const float r = PILE_LOG_RADIUS * PILE_SCALE;
        //Across the pile, and up: three on the ground side by side, two in their hollows, one on top.
        const float across[6] = {-2.0f * r,0.0f,2.0f * r,-r,r,0.0f};
        const float up[6] = {0.0f,0.0f,0.0f,1.73f * r,1.73f * r,3.46f * r};
        for (const auto& pile : want){
            int hut = -1;
            int spot = PileSpot(*w,*z,plots_of[pile.first],hut);
            if (spot < 0){
                continue;
            }
            vec2 at = w->grid->fine.pos[spot];
            vec2 to_hut = w->grid->fine.pos[hut] - at;
            at = at + to_hut * PILE_TOWARD_HUT;
            //The logs lie along the hut's side: across the way to it.
            float l = std::max(1e-4f,to_hut.length());
            vec2 along(-to_hut.y / l,to_hut.x / l);
            vec2 side(to_hut.x / l,to_hut.y / l);
            float yaw = atan2f(-along.y,along.x);
            float ground = w->terrain->GroundHeight(at,w->terrain->Height(spot));
            for (int i = 0; i < std::min(6,pile.second); i++){
                vec2 p = at + side * across[i];
                logs.push_back(Object::ComposeTransformScale(vec3(p.x,ground + up[i],p.y),
                                                             quat(vec3(0.0f,1.0f,0.0f),yaw),
                                                             vec3(PILE_SCALE,PILE_SCALE,PILE_SCALE)));
            }
        }
    }
    bool f_any = !logs.empty();
    pile_set->SetInstances(std::move(logs));
    pile_set->SetVisibility(f_any && f_view_forest);
}
