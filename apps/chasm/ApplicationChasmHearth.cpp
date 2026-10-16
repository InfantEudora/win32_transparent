#include "ApplicationChasm.h"
#include "BuildingMesh.h"
#include "Debug.h"
#include "MeshBuild.h"
#include "Mist.h"
#include "Palette.h"
#include <algorithm>
#include <cmath>

static Debugger* debug = new Debugger("ChasmHearth",DEBUG_ALL);

/*
    THE HEARTHS, drawn (needs_plan.md step 3): a home WARM today - its firewood burned at dawn (Economy.h,
    HEAT) - smokes, from a house's chimney or a camp's fires, and a house's windows glow. The smoke is core's
    LIFE motion (Material.h): a column of puffs at each chimney, born small, swelling as they rise and lean
    away, thinning out - moved on the GPU, so a village of chimneys costs nothing a frame. The glow is a
    mesh of orange panes over the houses' own, UNLIT so it reads as firelight in daylight. Where they go
    is where the house's mesh put its chimney and windows (BuildHouseCell's fixtures). Rebuilt when a
    home's heat or the zones change, which is at dawn, mostly. View only: the rules never ask.
*/
#define HEARTH_PUFFS        5       //puffs a chimney, a fifth of a life apart: a steady column
#define HEARTH_LIFE         4.5f    //seconds a puff lives
#define HEARTH_RISE         2.6f    //how high it rises in that time
#define HEARTH_GROW         0.55f   //of its life, the share it swells in before it thins away
#define HEARTH_SIZE         0.28f   //a puff at its biggest
#define HEARTH_DRIFT_X      0.9f    //the way the smoke leans, world units over a life
#define HEARTH_DRIFT_Z      0.5f
#define HEARTH_WANDER       0.10f   //how far each puff starts from the last
#define HEARTH_FIRE_UP      0.25f   //a camp fire's smoke starts this far above its ground
#define HEARTH_PANE_OUT     0.004f  //a lit pane stands this far in front of the pane it lights

//RENDER THREAD, from BuildScene: the smoke's puff and the two materials - the palette moving as LIFE, and unlit.
void ApplicationChasm::BuildHearthScene(){
    std::vector<vertex> verts;
    BuildPuffMesh(29u,PAL_SMOKE,PAL_EFFECTS,verts);
    hearth_puff_mesh = new Mesh();
    hearth_puff_mesh->SetMeshData(verts.data(),(int)verts.size());
    hearth_puff_mesh->Retain();     //held for good - see object_setmesh_releases_old
    Material* palette = renderer->GetMaterial(palette_material);
    if (!palette){
        debug->Err("No palette material - no smoke or lit windows\n");
        return;
    }
    Material smoke = *palette;
    smoke.name = "chasm_palette_smoke";
    smoke.glsl_material.wind_mode = INSTANCE_MOTION_LIFE;
    renderer->AddMaterial(smoke);
    smoke_material = renderer->FindMaterialIndex(smoke.name);
    Material glow = *palette;
    glow.name = "chasm_palette_glow";
    glow.glsl_material.f_unlit = 1;
    renderer->AddMaterial(glow);
    glow_material = renderer->FindMaterialIndex(glow.name);
}

/*
    RENDER THREAD, from PreRender. Every warm home's smoke and glow, from its fixtures - asked of the house's
    own cells (BuildHouseCell, its vertices thrown away), so a chimney's smoke rises from the top of the
    chimney the mesh drew.
*/
void ApplicationChasm::UploadHearth(){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    std::shared_ptr<const ZoneState> zs = GetZones();
    std::shared_ptr<const EconomyState> es = GetEconomy();
    if (!hearth_puff_mesh || smoke_material < 0 || glow_material < 0){
        return;
    }
    if (!hearth_smoke){
        main_scene->AtTickBoundary([&](){
            hearth_smoke = new Object();
            hearth_smoke->name = "Hearth smoke";
            hearth_smoke->SetMesh(hearth_puff_mesh);
            hearth_smoke->SetMaterialSlot(0,smoke_material);
            hearth_smoke->SetVisualOnly(true);
            hearth_smoke->SetPickability(false);
            hearth_smoke->SetCastsShadow(false);
            hearth_smoke->SetInstances(std::vector<fmat4>());
            hearth_smoke->SetVisibility(false);
            main_scene->AddObject(hearth_smoke);
            hearth_pane_mesh = new Mesh();
            hearth_panes = new Object();
            hearth_panes->name = "Hearth windows";
            hearth_panes->SetMesh(hearth_pane_mesh);
            hearth_panes->SetMaterialSlot(0,glow_material);
            hearth_panes->SetVisualOnly(true);
            hearth_panes->SetPickability(false);
            hearth_panes->SetCastsShadow(false);
            hearth_panes->SetVisibility(false);
            main_scene->AddObject(hearth_panes);
        });
    }
    bool f_ready = w && zs && es && zs->world == w && es->world == w;
    if (!f_ready){
        if (hearth_built_world){
            hearth_smoke->SetVisibility(false);
            hearth_panes->SetVisibility(false);
            hearth_built_world = NULL;
        }
        return;
    }
    const ZoneState& z = *zs;
    const EconomyState& e = *es;
    if (w == hearth_built_world && z.version == hearth_built_zones && e.heat == hearth_built_heat){
        return;
    }
    hearth_built_world = w;
    hearth_built_zones = z.version;
    hearth_built_heat = e.heat;

    std::vector<vec3> tops;
    std::vector<vertex> panes;
    HearthFixtures(*w,z,&e.heat,&tops,&panes);
    bool f_smoke = !tops.empty();
    SetSmoke(hearth_smoke,tops);
    hearth_smoke->SetVisibility(f_smoke);
    if (!panes.empty()){
        hearth_pane_mesh->SetMeshData(panes.data(),(int)panes.size());
    }
    hearth_panes->SetVisibility(!panes.empty());
}

/*
    Where a home's smoke rises and its windows glow, from its fixtures - asked of the house's own cells (BuildHouseCell,
    its vertices thrown away), so the smoke rises from the top of the chimney the mesh drew; a camp's from its fires.
    Given `heat` (EconomyState::heat), only the WARM homes'; without it, every home's - the title's diorama, which
    smokes in any season. Either list may be left out.
*/
void ApplicationChasm::HearthFixtures(const ChasmWorld& w, const ZoneState& z, const std::vector<uint8_t>* heat,
                                      std::vector<vec3>* tops, std::vector<vertex>* panes){
    const GridPicker& picker = *w.picker;
    //The homes' plots, by building.
    std::vector<std::vector<int>> plots(z.buildings.size());
    for (size_t v = 0; v < z.building.size(); v++){
        uint32_t id = z.building[v];
        bool f_home = id && id < z.buildings.size()
                      && (z.buildings[id].kind == ZONE_KIND_HOUSE || z.buildings[id].kind == ZONE_KIND_CAMP);
        if (f_home && (!heat || (id < heat->size() && (*heat)[id] == HEAT_WARM))){
            plots[id].push_back((int)v);
        }
    }
    std::vector<vertex> scratch;
    BuildingFixtures fx;
    std::vector<int> quads;
    for (size_t id = 1; id < plots.size(); id++){
        if (plots[id].empty()){
            continue;
        }
        if (z.buildings[id].kind == ZONE_KIND_CAMP){
            for (int v : plots[id]){
                for (int i = 0; tops && i < picker.PlotQuadCount(v); i++){
                    int q = picker.PlotQuadCorner(v,i) / 4;
                    if (ZoneCampFireTent(w,z,q) == v){
                        vec2 p = ZoneCampFireAt(w,q,v);
                        tops->push_back(vec3(p.x,w.terrain->GroundHeight(p,w.terrain->Height(v)) + HEARTH_FIRE_UP,p.y));
                    }
                }
            }
            continue;
        }
        quads.clear();
        for (int v : plots[id]){
            for (int i = 0; i < picker.PlotQuadCount(v); i++){
                quads.push_back(picker.PlotQuadCorner(v,i) / 4);
            }
        }
        std::sort(quads.begin(),quads.end());
        quads.erase(std::unique(quads.begin(),quads.end()),quads.end());
        fx.chimneys.clear();
        fx.panes.clear();
        for (int q : quads){
            scratch.clear();
            BuildHouseCell(w,z,q,scratch,&fx);
        }
        for (const BuildingChimney& c : fx.chimneys){
            if (c.building == id && tops){
                tops->push_back(c.top);
            }
        }
        for (const BuildingPane& pn : fx.panes){
            if (pn.building != id || !panes){
                continue;
            }
            vec3 out = pn.outward * HEARTH_PANE_OUT;
            MeshQuad(*panes,pn.corner[0] + out,pn.corner[1] + out,pn.corner[2] + out,pn.corner[3] + out,pn.outward,
                     PAL_WINDOW_LIT,PAL_EFFECTS);
        }
    }
}

//Object `o` - an instance set of the puff, in the smoke material - made the smoke rising from `tops`.
void ApplicationChasm::SetSmoke(Object* o, const std::vector<vec3>& tops){
    std::vector<fmat4> puffs;
    std::vector<vec4> motion;
    for (const vec3& top : tops){
        //A column: the puffs a fifth of a life apart, the whole column offset by the spot's hash so no two
        //chimneys breathe together.
        float offset = (float)(MeshHash((uint32_t)std::lround(top.x * 16.0f),(uint32_t)std::lround(top.z * 16.0f)) % 1000) / 1000.0f;
        for (int i = 0; i < HEARTH_PUFFS; i++){
            puffs.push_back(Object::ComposeTransformScale(top,quat(vec3(0.0f,1.0f,0.0f),0.0f),vec3(HEARTH_SIZE,HEARTH_SIZE,HEARTH_SIZE)));
            motion.push_back(vec4(std::fmod(offset + (float)i / HEARTH_PUFFS,1.0f),HEARTH_LIFE,HEARTH_RISE,HEARTH_GROW));
            motion.push_back(vec4(HEARTH_DRIFT_X,HEARTH_DRIFT_Z,HEARTH_WANDER,0.0f));
        }
    }
    o->SetInstances(std::move(puffs));
    o->SetInstanceMotion(std::move(motion),HEARTH_RISE + HEARTH_DRIFT_X + HEARTH_DRIFT_Z + HEARTH_WANDER + HEARTH_SIZE);
}
