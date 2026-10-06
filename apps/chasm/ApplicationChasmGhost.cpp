#include "ApplicationChasm.h"
#include "BuildingMesh.h"
#include "MeshBuild.h"
#include "Palette.h"
#include "glad.h"

#include <algorithm>

/*
    THE GHOST (play mode; the user, 2026-10-06): instead of the debug line mesh round the plot under the
    cursor, a see-through copy of what the tool in hand would place there - a house a storey higher, a
    new hut, a garden's or lot's ground, a field's cell - tinted GREEN where the rules allow it and RED
    where they refuse (or, with the erase tool, red over what would go). The outline pass (Renderer::
    OutlinePass) draws its edge in the same colour, so it reads even over a busy village.

    View only: built on the render thread from the published zones, the hover and the tool, again only
    when one of those changes. The rules asked are the commands' own (ZoneCanBuild and the rest), and
    play mode's own one - nothing under the clouds - so green is what a click will do.

    A ROAD BEING DRAWN (docs/line_works_plan.md) is the whole line, a tile a plot, each green or red by
    itself: a plot the line may not take carries a negative u, which chasm_ghost.frag tints red whatever
    the ghost's tint - so one mesh, one object, and the outline green while any of it will be placed.
*/

#define GHOST_LIFT          0.06f       //a ground ghost over the ground's relief, clear of the ground's own fill
#define GHOST_ALPHA         0.45f
static const vec3 GHOST_OK(0.45f,0.95f,0.45f);
static const vec3 GHOST_REFUSED(1.0f,0.35f,0.30f);

namespace {

//The quarters of the cells round plot v - its own ground - laid on the ground just over it.
void GhostPlotTile(const ChasmWorld& w, int v, std::vector<vertex>& out){
    const GridPicker& p = *w.picker;
    const Grid& g = *w.grid;
    float level = w.terrain->Height(v);
    auto at = [&](const vec2& x){
        return vec3(x.x,w.terrain->GroundHeight(x,level) + GHOST_LIFT,x.y);
    };
    for (int i = 0; i < p.PlotQuadCount(v); i++){
        int qc = p.PlotQuadCorner(v,i);
        const GridQuad& q = g.fine.quads[qc / 4];
        int k = qc % 4;
        vec2 a = g.fine.pos[q.v[k]];
        vec2 centre = (g.fine.pos[q.v[0]] + g.fine.pos[q.v[1]] + g.fine.pos[q.v[2]] + g.fine.pos[q.v[3]]) * 0.25f;
        vec2 b = (a + g.fine.pos[q.v[(k + 1) % 4]]) * 0.5f;
        vec2 d = (a + g.fine.pos[q.v[(k + 3) % 4]]) * 0.5f;
        MeshQuad(out,at(a),at(b),at(centre),at(d),vec3(0.0f,1.0f,0.0f),PAL_PATH);
    }
}

//A coarse cell - a field's - as its four fine cells, on the ground.
void GhostCellTile(const ChasmWorld& w, int coarse, std::vector<vertex>& out){
    const Grid& g = *w.grid;
    for (int k = 0; k < 4; k++){
        const GridQuad& q = g.fine.quads[coarse * 4 + k];
        float level = w.terrain->Height(q.v[0]);
        vec3 c[4];
        for (int j = 0; j < 4; j++){
            vec2 x = g.fine.pos[q.v[j]];
            c[j] = vec3(x.x,w.terrain->GroundHeight(x,level) + GHOST_LIFT,x.y);
        }
        MeshQuad(out,c[0],c[1],c[2],c[3],vec3(0.0f,1.0f,0.0f),PAL_FIELD);
    }
}

/*
    One plot of a building of `kind`, `storeys` high and all of it standing, as if nothing else stood:
    the zones with every other building taken out and this plot put in (the selection's outline mesh
    does the same - ApplicationChasmSelect.cpp). `id` is the building it is part of, or 0 for a new one.
*/
void GhostBuildingPlot(const ChasmWorld& w, const ZoneState& z, int plot, int kind, int storeys, uint32_t id,
                       std::vector<vertex>& out){
    ZoneState only;
    only.world = z.world;
    only.buildings = z.buildings;
    if (!id){
        ZoneBuilding b;
        b.kind = (uint8_t)kind;
        b.size = 1;
        id = (uint32_t)only.buildings.size();
        only.buildings.push_back(b);
    }
    only.building.assign(z.building.size(),0);
    only.storeys.assign(z.storeys.size(),0);
    only.standing.assign(z.standing.size(),0);
    only.ground = z.ground;
    only.ground_built = z.ground_built;
    only.field.assign(z.field.size(),0);
    only.building[plot] = id;
    only.storeys[plot] = (uint8_t)storeys;
    only.standing[plot] = (uint8_t)storeys;
    const GridPicker& p = *w.picker;
    for (int i = 0; i < p.PlotQuadCount(plot); i++){
        BuildHouseCell(w,only,p.PlotQuadCorner(plot,i) / 4,out);
    }
}

}

//RENDER THREAD, from BuildScene: the see-through program - default.vert, lit like any surface.
void ApplicationChasm::BuildGhostScene(){
    ghost_shader = new Shader("shaders/default.vert","shaders/chasm_ghost.frag");
    ghost_shader->f_lit = true;
    ghost_shader->uniform_callback = std::bind(&ApplicationChasm::SetGhostUniforms,this);
    ghost_shader_index = renderer->AddCustomShader(ghost_shader);
}

void ApplicationChasm::SetGhostUniforms(){
    ghost_shader->Setvec3("ghost_tint",f_ghost_ok ? GHOST_OK : GHOST_REFUSED);
    ghost_shader->Setvec3("ghost_refused",GHOST_REFUSED);
    ghost_shader->Setfloat("ghost_alpha",GHOST_ALPHA);
    //Over what stands, never into the depth buffer: a ghost hides nothing, itself included.
    glDepthMask(GL_FALSE);
}

/*
    RENDER THREAD, every frame. What the tool would do at the hover, and whether it may - built again only
    when the hover, the tool, the zones or what is explored there changes.
*/
void ApplicationChasm::UpdateGhost(){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    std::shared_ptr<const ZoneState> zs = GetZones();
    if (!ghost){
        if (!w || ghost_shader_index < 0){
            return;
        }
        main_scene->AtTickBoundary([&](){
            ghost = new Object();
            ghost->name = "Ghost";
            Mesh* mesh = new Mesh();
            mesh->num_materials = 1;
            ghost->SetMesh(mesh);
            ghost->SetMaterialSlot(0,palette_material);
            ghost->SetVisualOnly(true);
            ghost->SetPickability(false);
            ghost->SetCastsShadow(false);
            ghost->SetVisibility(false);
            main_scene->AddObject(ghost);
        });
    }
    int tool = paint_tool;
    GridPick hover = GetHoverPick();
    //A road being drawn: its line, shown wherever the cursor has gone meanwhile - off the map too.
    std::vector<int> chain;
    bool f_erase_line = false;
    uint32_t line_at = 0;
    if (tool == CHASM_TOOL_ROAD){
        std::lock_guard<std::mutex> lock(pick_mutex);
        chain = line_chain;
        f_erase_line = f_line_erase;
        line_at = line_version.load();
    }
    bool f_line = !chain.empty();
    bool f_tool = tool != CHASM_TOOL_SELECT && tool != CHASM_TOOL_BRIDGE && tool != CHASM_TOOL_WALKER;
    bool f_show = PlayMode() && f_tool && (hover.f_hit || f_line) && w && zs && zs->world == w;
    if (!f_show){
        if (ghost->IsVisible()){
            ghost->SetVisibility(false);
            ghost->SetOutline(vec4(0.0f,0.0f,0.0f,0.0f));
        }
        ghost_built = GhostKey();
        return;
    }
    const ChasmWorld& cw = *w;
    const ZoneState& z = *zs;
    bool f_field = (tool == CHASM_TOOL_FIELD);
    std::shared_ptr<const Exploration> ex = GetExploration();
    GhostKey key;
    key.world = w.get();
    key.zones_version = z.version;
    key.plot = hover.plot;
    key.coarse = hover.coarse_quad;
    key.tool = tool;
    if (hover.f_hit){
        vec2 where = f_field ? (cw.grid->FineQuadCentre(hover.coarse_quad * 4) + cw.grid->FineQuadCentre(hover.coarse_quad * 4 + 2)) * 0.5f
                             : cw.grid->fine.pos[hover.plot];
        key.f_explored = !ex || ex->Explored(where);
    }
    key.line_version = f_line ? line_at : 0;
    if (key == ghost_built){
        return;
    }
    ghost_built = key;

    std::vector<vertex> verts;
    bool f_ok = false;
    int plot = hover.plot;
    int kind = ToolKind(tool);
    if (f_line){
        //Each plot once, a tile each. Erasing, only what has road to take up, all of it red - what goes.
        std::vector<int> drawn;
        for (int v : chain){
            if (std::find(drawn.begin(),drawn.end(),v) != drawn.end()){
                continue;
            }
            drawn.push_back(v);
            bool f_plot_ok = LinePlotAllowed(cw,z,ex.get(),f_erase_line,v,NULL);
            if (f_erase_line && !f_plot_ok){
                continue;
            }
            size_t first = verts.size();
            GhostPlotTile(cw,v,verts);
            if (f_erase_line){
                continue;
            }
            if (f_plot_ok){
                f_ok = true;
            }else{
                for (size_t i = first; i < verts.size(); i++){
                    verts[i].uv = vec2(-1.0f,-1.0f);
                }
            }
        }
    }else if (kind != ZONE_KIND_NONE){
        //A click on a building is one storey more of it, whatever the tool; on empty ground, a new one.
        uint32_t on = z.building[plot];
        if (on){
            int on_kind = z.buildings[on].kind;
            int top = ZoneKindMaxStoreys(on_kind);
            f_ok = z.storeys[plot] < top;
            GhostBuildingPlot(cw,z,plot,on_kind,std::min(top,z.storeys[plot] + 1),on,verts);
        }else{
            f_ok = ZoneCanBuild(cw,z,plot,kind,0,NULL);
            GhostBuildingPlot(cw,z,plot,kind,1,0,verts);
        }
    }else if (tool == CHASM_TOOL_GARDEN || tool == CHASM_TOOL_LOT || tool == CHASM_TOOL_ROAD){
        int ground = (tool == CHASM_TOOL_GARDEN) ? ZONE_GROUND_GARDEN : (tool == CHASM_TOOL_LOT) ? ZONE_GROUND_LOT : ZONE_GROUND_ROAD;
        f_ok = z.ground[plot] == ground || ZoneCanGround(cw,z,plot,NULL,ground);
        GhostPlotTile(cw,plot,verts);
    }else if (f_field){
        f_ok = z.field[hover.coarse_quad] || ZoneCanField(cw,z,hover.coarse_quad,NULL);
        GhostCellTile(cw,hover.coarse_quad,verts);
    }else if (tool == CHASM_TOOL_ERASE){
        //What would go, in red: the plot's building as it stands, its ground, or the field under it.
        if (z.building[plot]){
            uint32_t on = z.building[plot];
            GhostBuildingPlot(cw,z,plot,z.buildings[on].kind,z.storeys[plot],on,verts);
        }else if (z.ground[plot]){
            GhostPlotTile(cw,plot,verts);
        }else if (z.field[hover.coarse_quad]){
            GhostCellTile(cw,hover.coarse_quad,verts);
        }
        f_ok = false;
    }
    //Play mode's own rule (play_mode_plan.md): nothing is placed under the clouds - a line asks it per plot.
    if (!f_line && !key.f_explored && tool != CHASM_TOOL_ERASE){
        f_ok = false;
    }
    if (verts.empty()){
        ghost->SetVisibility(false);
        ghost->SetOutline(vec4(0.0f,0.0f,0.0f,0.0f));
        return;
    }
    f_ghost_ok = f_ok;
    Mesh* mesh = ghost->GetMesh();
    mesh->SetMeshData(verts.data(),(int)verts.size());
    //SetMeshData puts a mesh back to MESH_MODE_NORMAL, so the shader tag goes after it.
    mesh->mesh_mode = MESH_MODE_SHADER;
    mesh->custom_shader_index = ghost_shader_index;
    vec3 c = f_ok ? GHOST_OK : GHOST_REFUSED;
    ghost->SetOutline(vec4(c.x,c.y,c.z,1.0f));
    ghost->SetVisibility(true);
}
