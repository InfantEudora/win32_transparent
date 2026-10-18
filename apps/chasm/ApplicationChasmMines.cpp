#include "ApplicationChasm.h"
#include "MeshBuild.h"
#include "Palette.h"
#include <math.h>

/*
    THE MINES, DRAWN (docs/stone_plan.md, step 5). A standing mine is the worker's props, not a house body
    (BuildingMesh leaves it out): dug into a seam's wall or the mountain's slope, `mine_portal` with its back to the
    rock - the model faces +Z, so the engine's forward, -Z, points into it - and `mine_cart` out on its rails. Against the
    mountain a copy of the portal whose rock is the frozen row's grey, the mountain's own: modelled in the walls'
    temperate strata, it stood out brown on grey (the props' note, stone_plan.md "The props"). On an
    outcrop, `quarry_crane` beside the beds, its boom over them, the outcrop itself left in sight (PropHidden). One
    instance set of each, rebuilt when the zones change.

    And every floatstone seam's BAND (TerrainSeamBand): kept out of the terrain's chunks, an object of its crystal and
    one of the rock it would be, the crystal shown while the seam has floatstone left and the rock once it is dug out -
    so a worked-out seam goes back to the wall's own colour with nothing rebuilt.

    View only: placed from the zones and the world, shown by the economy's `dug`.
*/

#define MINE_PORTAL_TOWARD  0.55f   //from the mine's point toward its rock: the sill, the wall's face a little behind it
#define MINE_CART_OUT       0.45f   //the cart along its rails, out of the mouth
#define MINE_CART_LIFT      0.036f  //...standing on them (the props' note: its base is its wheels' bottom)
#define MINE_CRANE_OFF      1.5f    //an outcrop's crane, from the outcrop's middle: its boom reaches 1.43

static const char* mine_asset_names[MINE_PROPS] = {"mine_portal","mine_cart","quarry_crane"};

//RENDER THREAD, from PreRender.
void ApplicationChasm::UploadMines(){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    std::shared_ptr<const ZoneState> z = GetZones();
    std::shared_ptr<const EconomyState> e = GetEconomy();
    if (!w){
        return;
    }
    //Each set made once, from its asset - none while the file lacks it.
    for (int i = 0; i < MINE_PROPS; i++){
        if (mine_sets[i] || !assetmanager->GetMeshFromAsset(mine_asset_names[i])){
            continue;
        }
        world_scene->AtTickBoundary([&](){
            Object* set = assetmanager->GetObjectFromAsset(mine_asset_names[i]);
            if (set){
                set->name = std::string("mines: ") + mine_asset_names[i];
                set->SetMaterialSlot(0,palette_material);   //the GLB names a material it carries no image for
                set->SetPickability(false);
                set->SetVisualOnly(true);
                set->SetInstances(std::vector<fmat4>());
                set->SetVisibility(false);
                world_scene->AddObject(set);
            }
            mine_sets[i] = set;
        });
    }
    //The mountain's portal: the asset's triangles, the rock round its frame (the walls' strata columns 5, 6 and 8)
    //moved to the frozen row; timber, rails and the black mouth as they are.
    Mesh* portal = mine_sets[0] ? assetmanager->GetMeshFromAsset(mine_asset_names[0]) : NULL;
    if (!mine_mountain_set && portal && !portal->GetVertices().empty() && portal->GetVertices().size() % 3 == 0){
        std::vector<vertex> verts = portal->GetVertices();
        for (vertex& v : verts){
            int column = (int)floorf(v.uv.x * PALETTE_COLS);
            int row = (int)floorf(v.uv.y * PALETTE_ROWS);
            if (row == PAL_TEMPERATE && (column == PAL_ROCK_0 || column == PAL_ROCK_0 + 1 || column == PAL_ROCK_0 + 3)){
                v.uv = PaletteUV(column,PAL_FROZEN);
            }
        }
        world_scene->AtTickBoundary([&](){
            Object* set = new Object();
            set->name = "mines: mine_portal, against the mountain";
            set->SetMesh(new Mesh());
            set->GetMesh()->SetMeshData(verts.data(),(int)verts.size());
            set->SetMaterialSlot(0,palette_material);
            set->SetPickability(false);
            set->SetVisualOnly(true);
            set->SetInstances(std::vector<fmat4>());
            set->SetVisibility(false);
            world_scene->AddObject(set);
            mine_mountain_set = set;
        });
        mines_built_for.reset();    //placed again, now that there is a set for them
    }
    if (z && z->world == w && z != mines_built_for){
        mines_built_for = z;
        const Grid& g = *w->grid;
        const Terrain& t = *w->terrain;
        std::vector<fmat4> looks[MINE_PROPS];
        std::vector<fmat4> mountain;
        for (size_t v = 0; v < z->building.size(); v++){
            if (z->KindOf((int)v) != ZONE_KIND_MINE || z->standing[v] <= 0){
                continue;
            }
            vec2 at = g.fine.pos[v];
            float level = t.Height((int)v);
            vec2 way = w->deposits ? DepositDigWay(g,*w->picker,t,*w->deposits,(int)v) : vec2(0.0f,0.0f);
            if (way.dot(way) < 1e-6f){
                //An outcrop: the crane on a side of its own, by a hash of the plot, its boom (+X) over the beds.
                float a = (float)(MeshHash((uint32_t)v,0x6372616Eu) % 6283u) / 1000.0f;
                vec2 side(cosf(a),sinf(a));
                vec2 crane = at - side * MINE_CRANE_OFF;
                looks[2].push_back(Object::ComposeTransformScale(vec3(crane.x,t.GroundHeight(crane,level),crane.y),
                                                                 quat(vec3(0.0f,1.0f,0.0f),atan2f(-side.y,side.x)),
                                                                 vec3(1.0f,1.0f,1.0f)));
                continue;
            }
            //The model's -Z along `way`, into the rock.
            quat facing(vec3(0.0f,1.0f,0.0f),atan2f(-way.x,-way.y));
            vec2 sill = at + way * MINE_PORTAL_TOWARD;
            int d = w->deposits->At((int)v);
            bool f_mountain = d >= 0 && w->deposits->deposits[d].kind == DEPOSIT_MOUNTAIN && mine_mountain_set;
            (f_mountain ? mountain : looks[0]).push_back(Object::ComposeTransformScale(vec3(sill.x,t.GroundHeight(sill,level),sill.y),
                                                                                       facing,vec3(1.0f,1.0f,1.0f)));
            vec2 cart = sill - way * MINE_CART_OUT;
            looks[1].push_back(Object::ComposeTransformScale(vec3(cart.x,t.GroundHeight(cart,level) + MINE_CART_LIFT,cart.y),
                                                             facing,vec3(1.0f,1.0f,1.0f)));
        }
        for (int i = 0; i < MINE_PROPS; i++){
            if (mine_sets[i]){
                bool f_any = !looks[i].empty();
                mine_sets[i]->SetInstances(std::move(looks[i]));
                mine_sets[i]->SetVisibility(f_any);
            }
        }
        if (mine_mountain_set){
            bool f_any = !mountain.empty();
            mine_mountain_set->SetInstances(std::move(mountain));
            mine_mountain_set->SetVisibility(f_any);
        }
    }
    //THE SEAMS' BANDS: an object pair for each seam of a new mesh, then which of the two shows.
    if (!w->mesh){
        return;
    }
    const std::vector<TerrainSeamBand>& bands = w->mesh->seam_bands;
    if (w->mesh != seams_built_for){
        seams_built_for = w->mesh;
        world_scene->AtTickBoundary([&](){
            while (seam_objects.size() < bands.size() * 2){
                Object* o = new Object();
                o->name = "Seam band " + std::to_string(seam_objects.size() / 2) + ((seam_objects.size() % 2) ? " rock" : " crystal");
                o->SetMesh(new Mesh());
                o->SetMaterialSlot(0,palette_material);
                o->SetVisualOnly(true);
                o->SetVisibility(false);
                world_scene->AddObject(o);
                seam_objects.push_back(o);
            }
            for (size_t i = 0; i < seam_objects.size(); i++){
                const std::vector<vertex>* verts = NULL;
                if (i / 2 < bands.size()){
                    verts = (i % 2) ? &bands[i / 2].rock : &bands[i / 2].crystal;
                }
                if (verts && !verts->empty()){
                    std::vector<vertex>& v = const_cast<std::vector<vertex>&>(*verts);
                    seam_objects[i]->GetMesh()->SetMeshData(v.data(),(int)v.size());
                    UseGroundShader(seam_objects[i]->GetMesh());    //after: SetMeshData resets the mode
                }
                seam_objects[i]->SetVisibility(false);
            }
        });
    }
    bool f_ours = e && e->world == w;
    bool f_show = f_view_terrain;
    for (size_t d = 0; d < bands.size() && d * 2 + 1 < seam_objects.size(); d++){
        if (bands[d].crystal.empty()){
            continue;
        }
        bool f_dug_out = f_ours && EconomyDepositLeft(*e,(int)d) <= 0;
        seam_objects[d * 2]->SetVisibility(f_show && !f_dug_out);
        seam_objects[d * 2 + 1]->SetVisibility(f_show && f_dug_out);
    }
}
