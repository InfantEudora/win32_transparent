#include "ApplicationChasm.h"
#include "ChasmHud.h"
#include "BuildingMesh.h"
#include "ZoneMesh.h"
#include "MeshBuild.h"
#include "Palette.h"
#include "glad.h"

#include <algorithm>
#include <cctype>
#include <cmath>

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

    ANY OTHER DRAG (docs/drag_plan.md) is what the rules make of the commands its release will send, run on a
    copy of the zones (PlanDrag): the buildings it makes or grows, each apart from the next, a yard's plots, a
    field's cells - every other building a shade darker - and red where the rules refuse.

    A BRIDGE (docs/bridge_plan.md) is tiles too: before the press, the bank it would start from; dragged,
    the whole chain to the cursor, all one colour, since the rules are the whole bridge's (ZoneBridgeChain)
    and neither end may be under the clouds. Over the water a tile lies at the deck's height, where the
    bridge will run, rather than on the river bed out of sight.

    A WINCH is a thin gantry, and none at all where there is no balcony below to lower to - which is most
    of the map, so most of where the cursor goes showed nothing. Its plot is laid as a tile under the
    gantry, and the landing it lowers to as another.

    THE PRICE TAG (the user's, 2026-10-11): beside the cursor, what the ghost would cost - the goods its site
    will ask for, from the rules' own costs. A click's worth, as the ghost is: a new building's plot at the
    storeys it is painted with, and with the extension's corner plot that comes with it (ZoneJoinCorner); one
    storey more on what stands; a garden's, lot's or wall's plot; a field's or range's cell. A line being drawn
    or a bridge dragged, all of it. Nothing for what is that already - a lot painted on a lot is only claimed -
    nor for erasing. Dim where the ghost is red, and under the clouds only that it is hidden.
*/

#define GHOST_LIFT          0.06f       //a ground ghost over the ground's relief, clear of the ground's own fill
#define GHOST_ALPHA         0.45f
static const vec3 GHOST_OK(0.45f,0.95f,0.45f);
static const vec3 GHOST_REFUSED(1.0f,0.35f,0.30f);

namespace {

//The quarters of the cells round plot v - its own ground - laid on the ground just over it (ZoneMesh.h).
void GhostPlotTile(const ChasmWorld& w, const ZoneState& z, int v, std::vector<vertex>& out){
    BuildPlotTile(w,z,v,GHOST_LIFT,out);
}

//A coarse cell - a field's - as its four fine cells, on the ground.
void GhostCellTile(const ChasmWorld& w, const ZoneState& z, int coarse, std::vector<vertex>& out){
    const Grid& g = *w.grid;
    for (int k = 0; k < 4; k++){
        const GridQuad& q = g.fine.quads[coarse * 4 + k];
        vec3 c[4];
        for (int j = 0; j < 4; j++){
            vec2 x = g.fine.pos[q.v[j]];
            c[j] = vec3(x.x,ZoneGroundHeight(w,z,q.v[j],x) + GHOST_LIFT,x.y);
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
        //A guard tower's ladder where placing it would put it (defence_plan.md) - the door the player then has.
        if (kind == ZONE_KIND_TOWER){
            b.ladder = ZoneTowerDefaultLadder(w,z,plot);
        }
        id = (uint32_t)only.buildings.size();
        only.buildings.push_back(b);
    }
    only.building.assign(z.building.size(),0);
    only.storeys.assign(z.storeys.size(),0);
    only.standing.assign(z.standing.size(),0);
    only.ground = z.ground;
    only.ground_built = z.ground_built;
    only.terrace = z.terrace;               //what it stands on (terraces_plan.md)
    only.terrace_built = z.terrace_built;
    only.gate = z.gate;     //a guard tower's ladder goes on a side no wall or gate runs off (BuildTower)
    only.field.assign(z.field.size(),0);
    only.building[plot] = id;
    only.storeys[plot] = (uint8_t)storeys;
    only.standing[plot] = (uint8_t)storeys;
    const GridPicker& p = *w.picker;
    for (int i = 0; i < p.PlotQuadCount(plot); i++){
        BuildHouseCell(w,only,p.PlotQuadCorner(plot,i) / 4,out);
    }
}

//Several of a kind, as a drag's price tag counts them: "2 HOUSES".
const char* KindPlural(int kind){
    switch (kind){
        case ZONE_KIND_HOUSE:       return "HOUSES";
        case ZONE_KIND_STORE:       return "STORES";
        case ZONE_KIND_WATER:       return "COLLECTORS";
        case ZONE_KIND_STEAM:       return "STEAM COLLECTORS";
        case ZONE_KIND_CONDENSER:   return "CONDENSERS";
        case ZONE_KIND_FIELD:       return "FIELDS";
        case ZONE_KIND_RANGE:       return "RANGES";
        case ZONE_KIND_ROPEWALK:    return "ROPEWALKS";
        case ZONE_KIND_CAMP:        return "CAMPS";
        case ZONE_KIND_WOODCUTTER:
        case ZONE_KIND_BUILDERS:
        case ZONE_KIND_STONECUTTER: return "HUTS";
        default:                    return "BUILDINGS";
    }
}

//The goods in `cost`, in the HUD's order and upper case as the overlay writes them: "7 WOOD, 4 ROPE". Empty for none.
std::string CostWords(const std::array<int,GOOD_COUNT>& cost){
    std::string s;
    for (int g : HudGoodsOrder()){
        if (cost[g] > 0){
            s += (s.empty() ? "" : ", ") + std::to_string(cost[g]) + " " + GoodName(g);
        }
    }
    std::transform(s.begin(),s.end(),s.begin(),::toupper);
    return s;
}

//The tag: "COSTS 7 WOOD", or `lead` for COSTS ("A STOREY MORE: 7 WOOD"), the plots when more than one, and the trees a
//woodcutter must fell first (woodcutting_plan.md); a road's nothing to carry is said as such.
std::string CostTag(const std::array<int,GOOD_COUNT>& cost, const char* lead, int plots, int trees){
    std::string goods = CostWords(cost);
    std::string s = goods.empty() ? std::string("NO GOODS - ONLY WORK") : lead ? std::string(lead) + ": " + goods : "COSTS " + goods;
    if (plots > 1 && !goods.empty()){
        s += " - " + std::to_string(plots) + " PLOTS";
    }
    if (trees > 0){
        s += " - " + std::to_string(trees) + (trees == 1 ? " TREE" : " TREES") + " TO FELL";
    }
    return s;
}

}

//RENDER THREAD, from BuildScene: the see-through programs - default.vert, lit like any surface.
void ApplicationChasm::BuildGhostScene(){
    ghost_shader = new Shader("shaders/default.vert","shaders/chasm_ghost.frag");
    ghost_shader->f_lit = true;
    ghost_shader->uniform_callback = std::bind(&ApplicationChasm::SetGhostUniforms,this);
    ghost_shader_index = renderer->AddCustomShader(ghost_shader);
    slope_shader = new Shader("shaders/default.vert","shaders/chasm_slope.frag");
    slope_shader->f_lit = true;
    slope_shader->uniform_callback = std::bind(&ApplicationChasm::SetSlopeUniforms,this);
    slope_shader_index = renderer->AddCustomShader(slope_shader);
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
        world_scene->AtTickBoundary([&](){
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
            world_scene->AddObject(ghost);
        });
    }
    int tool = paint_tool;
    GridPick hover = GetHoverPick();
    //A road or wall being drawn: its line, shown wherever the cursor has gone meanwhile - off the map too.
    std::vector<int> chain;
    bool f_erase_line = false;
    int line_ground_drawn = ZONE_GROUND_ROAD;
    uint32_t line_at = 0;
    if (ChasmLineTool(tool)){
        std::lock_guard<std::mutex> lock(pick_mutex);
        chain = line_chain;
        f_erase_line = f_line_erase;
        line_ground_drawn = line_ground;
        line_at = line_version.load();
    }
    bool f_line = !chain.empty();
    int from = (tool == CHASM_TOOL_BRIDGE) ? bridge_from.load() : -1;
    bool f_tool = tool != CHASM_TOOL_SELECT;
    //Any other drag being drawn (drag_plan.md): what its release will make, as the rules made it on their copy.
    uint32_t plan_serial = 0;
    const DragPlan* plan = (PlayMode() && f_tool) ? CurrentDragPlan(&plan_serial) : NULL;
    bool f_area = plan != NULL;
    bool f_show = PlayMode() && f_tool && (hover.f_hit || f_line || f_area) && w && zs && zs->world == w;
    if (!f_show){
        if (ghost->IsVisible()){
            ghost->SetVisibility(false);
            ghost->SetOutline(vec4(0.0f,0.0f,0.0f,0.0f));
        }
        ghost_built = GhostKey();
        std::lock_guard<std::mutex> lock(pick_mutex);
        ghost_cost_text.clear();
        return;
    }
    const ChasmWorld& cw = *w;
    const ZoneState& z = *zs;
    bool f_field = ChasmCellTool(tool);     //a field, or a range painted like one (range_plan.md)
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
        //A bridge's far end is under the cursor; its near one, where the press was, is held to the clouds too.
        if (ex && from >= 0 && from < (int)cw.grid->fine.pos.size()){
            key.f_explored = key.f_explored && ex->Explored(cw.grid->fine.pos[from]);
        }
    }
    key.line_version = f_line ? line_at : 0;
    key.drag_serial = f_area ? plan_serial : 0;
    key.bridge_from = from;
    key.owner = PaintOwner(tool);       //a yard being painted: whose (yards_plan.md)
    //Nothing is put over a crate in play (crates_plan.md): red where the zone command would refuse it.
    std::shared_ptr<const EconomyState> es = GetEconomy();
    bool f_crates = es && es->world == w && !es->crates.empty();
    key.crates_version = (es && es->world == w) ? es->crates_version : 0;
    key.felled_version = (es && es->world == w) ? es->felled_version : 0;
    //The trees a site there would wait on (THE PRICE TAG, woodcutting_plan.md): standing ones on the plot or cell.
    auto trees_on = [&](int at_plot, int at_cell){
        return (es && es->world == w) ? EconomyTreesOn(*w,*es,at_plot,at_cell) : 0;
    };
    //...and what a woodcutter's hut or a water or steam collector `owner` (0 a new one) of `kind` would keep the more for
    //`more` floor, once all of it stands: a hut none for the floor he works in (EconomyHutRoom), a collector a load at
    //the least (EconomyCollectorRoom) - a new one's all of it.
    auto room_gain = [&](int room_kind, uint32_t owner, float more){
        int (*room)(float) = (room_kind == ZONE_KIND_WATER || room_kind == ZONE_KIND_STEAM || room_kind == ZONE_KIND_CONDENSER)
                             ? EconomyCollectorRoom : EconomyHutRoom;
        if (!owner){
            return room(more);
        }
        ZoneBuildingInfo info = ZoneBuildingFigures(*w,*zs,owner);
        float base = info.floor_area + info.site_area;
        return room(base + more) - room(base);
    };
    //...nor outside every settlement's territory (settlements_plan.md): a bridge's near end, where the press was.
    if (hover.f_hit && es && es->world == w){
        int op = f_field ? ZONE_OP_FIELD_PAINT : (from >= 0) ? ZONE_OP_BRIDGE : ZONE_OP_BUILD_PAINT;
        key.f_settled = EconomyCommandInTerritory(*es,cw,op,f_field ? hover.coarse_quad : (from >= 0) ? from : hover.plot);
    }
    auto crate_in_way = [&](int op, int index, int value){
        return f_crates && EconomyCrateInTheWay(*es,cw,op,index,value);
    };
    if (key == ghost_built){
        return;
    }
    ghost_built = key;

    std::vector<vertex> verts;
    bool f_ok = false;
    int plot = hover.plot;
    int kind = ToolKind(tool);
    //THE PRICE TAG: what it would cost, by good; whether there is anything to say; its lead-in; how many plots.
    std::array<int,GOOD_COUNT> cost{};
    bool f_cost = false;
    const char* lead = NULL;
    const char* note = NULL;    //said after it all: a bridge's trees no woodcutter gets to, a drag's size
    std::string area_note;
    int plots = 1;
    int trees = 0;
    int room_more = -1;         //a woodcutter's hut: the wood it would keep indoors the more, a collector its water; -1 neither
    bool f_room_new = false;    //...a new one's, all it keeps
    int room_kind = ZONE_KIND_NONE;     //...which of the two: the building under a click for a storey, not the tool's
    if (f_line){
        //Each plot once, a tile each. Erasing, only what has road (or wall) to take up, all of it red - what goes.
        std::vector<int> drawn;
        int laid = 0;
        for (int v : chain){
            if (std::find(drawn.begin(),drawn.end(),v) != drawn.end()){
                continue;
            }
            drawn.push_back(v);
            bool f_plot_ok = LinePlotAllowed(cw,z,ex.get(),f_erase_line,line_ground_drawn,v,NULL)
                             && (f_erase_line || !crate_in_way(ZONE_OP_GROUND_PAINT,v,line_ground_drawn));
            if (f_erase_line && !f_plot_ok){
                continue;
            }
            size_t first = verts.size();
            GhostPlotTile(cw,z,v,verts);
            if (f_erase_line){
                continue;
            }
            if (f_plot_ok){
                f_ok = true;
                //Its cost: each plot it lays - not one that is its ground already, nor a road a wall only gates.
                if (z.ground[v] != line_ground_drawn
                    && !(line_ground_drawn == ZONE_GROUND_WALL && z.ground[v] == ZONE_GROUND_ROAD)){
                    cost[GOOD_WOOD] += EconomyGroundKindCost(cw,line_ground_drawn,v);
                    laid++;
                }
            }else{
                for (size_t i = first; i < verts.size(); i++){
                    verts[i].uv = vec2(-1.0f,-1.0f);
                }
            }
        }
        f_cost = laid > 0;
        plots = laid;
    }else if (f_area){
        /*
            A DRAG (drag_plan.md): what the rules made of its commands on their copy, against the zones as they are. Each
            new plot of a building drawn as it will stand, with only what the drag makes or grows standing, so the
            buildings it makes are apart from each other; a yard's plots; a field's or range's cells, every other one
            it makes a shade darker so where one ends and the next begins shows. What the rules refuse, red - taken
            away, all of it. And its price, all of it.
        */
        const ZoneState& a = plan->after.State();
        bool f_cells = ChasmCellTool(plan->tool);
        if (plan->f_erase || plan->tool == CHASM_TOOL_ERASE){
            for (size_t i = 0; i < plan->commands.size(); i++){
                const DragCommand& c = plan->commands[i];
                if (plan->refused[i]){
                    continue;
                }
                if (c.op == ZONE_OP_BUILD_ERASE && z.building[c.index]){
                    uint32_t on = z.building[c.index];
                    GhostBuildingPlot(cw,z,c.index,z.buildings[on].kind,z.storeys[c.index],on,verts);
                }else if (c.op == ZONE_OP_FIELD_ERASE){
                    GhostCellTile(cw,z,c.index,verts);
                }else{
                    GhostPlotTile(cw,z,c.index,verts);
                }
            }
        }else{
            std::vector<uint32_t> made;     //the buildings it makes or grows, in the order met
            auto made_index = [&](uint32_t id){
                size_t k = std::find(made.begin(),made.end(),id) - made.begin();
                if (k == made.size()){
                    made.push_back(id);
                }
                return k;
            };
            int placed = 0;
            int founded = 0;            //...of its plots, on a foundation (terraces_plan.md)
            float floor_more = 0.0f;
            int made_kind = ZONE_KIND_NONE;
            if (f_cells){
                made_kind = (plan->tool == CHASM_TOOL_RANGE) ? ZONE_KIND_RANGE : ZONE_KIND_FIELD;
                std::vector<int> fresh;
                for (size_t c = 0; c < a.field.size(); c++){
                    if (a.field[c] && a.field[c] != z.field[c]){
                        fresh.push_back((int)c);
                        made_index(a.field[c]);
                    }
                }
                //In the order the drag made them - new ids are handed out so - for the shade to take turns along it.
                std::sort(made.begin(),made.end());
                for (int c : fresh){
                    size_t first = verts.size();
                    GhostCellTile(cw,z,c,verts);
                    if (made_index(a.field[c]) % 2 == 1){
                        for (size_t j = first; j < verts.size(); j++){
                            verts[j].uv = vec2(2.0f,2.0f);      //chasm_ghost.frag: the darker shade
                        }
                    }
                    for (int g = 0; g < GOOD_COUNT; g++){
                        cost[g] += EconomyCellCost(made_kind,g);
                    }
                    trees += trees_on(-1,c);
                    placed++;
                }
            }else if (plan->tool == CHASM_TOOL_TERRACE){
                //A terrace (terraces_plan.md stage 2): each new plot of it as a tile at its top - one height, all of it.
                for (size_t v = 0; v < a.terrace.size(); v++){
                    if (ZoneTerraced(a,(int)v) && !ZoneTerraced(z,(int)v)){
                        BuildPlotTile(cw,z,(int)v,GHOST_LIFT,verts,ZoneTerraceTop(a,(int)v) + GHOST_LIFT);
                        cost[GOOD_STONE] += EconomyTerraceStone(cw,(int)v,a.terrace[v]);
                        trees += trees_on((int)v,-1);
                        placed++;
                    }
                }
            }else if (plan->tool == CHASM_TOOL_GARDEN || plan->tool == CHASM_TOOL_LOT){
                int ground = (plan->tool == CHASM_TOOL_GARDEN) ? ZONE_GROUND_GARDEN : ZONE_GROUND_LOT;
                for (size_t v = 0; v < a.ground.size(); v++){
                    if (a.ground[v] == ground && z.ground[v] != ground){
                        GhostPlotTile(cw,z,(int)v,verts);
                        cost[GOOD_WOOD] += EconomyGroundKindCost(cw,ground,(int)v);
                        trees += ZoneEnclosesGround(ground) ? trees_on((int)v,-1) : 0;
                        placed++;
                    }
                }
            }else{
                made_kind = kind;
                //Only what the drag makes or grows stands in the copy drawn from - every plot of it, so a building it
                //extends is drawn joined to what it had - and only the cells round its new plots are drawn.
                ZoneState only;
                only.world = a.world;
                only.buildings = a.buildings;
                only.building.assign(a.building.size(),0);
                only.storeys.assign(a.storeys.size(),0);
                only.standing.assign(a.standing.size(),0);
                only.ground = a.ground;
                only.ground_built = a.ground_built;
                only.terrace = a.terrace;
                only.terrace_built = a.terrace_built;
                only.gate = a.gate;
                only.field.assign(a.field.size(),0);
                std::vector<int> quads;
                for (size_t v = 0; v < a.building.size(); v++){
                    if (!a.building[v] || a.building[v] == z.building[v]){
                        continue;
                    }
                    made_index(a.building[v]);
                    for (int g = 0; g < GOOD_COUNT; g++){
                        cost[g] += EconomyKindStoreysCost(cw,a,kind,(int)v,g,0,a.storeys[v]);
                    }
                    trees += trees_on((int)v,-1);
                    floor_more += a.storeys[v] * cw.picker->PlotArea((int)v);
                    placed++;
                    founded += ZoneNeedsFoundation(cw,a,kind,(int)v) ? 1 : 0;
                    for (int i = 0; i < cw.picker->PlotQuadCount((int)v); i++){
                        int q = cw.picker->PlotQuadCorner((int)v,i) / 4;
                        if (std::find(quads.begin(),quads.end(),q) == quads.end()){
                            quads.push_back(q);
                        }
                    }
                }
                std::sort(made.begin(),made.end());     //in the order the drag made them, as for cells
                for (size_t v = 0; v < a.building.size(); v++){
                    if (a.building[v] && std::find(made.begin(),made.end(),a.building[v]) != made.end()){
                        only.building[v] = a.building[v];
                        only.storeys[v] = a.storeys[v];
                        only.standing[v] = a.storeys[v];
                    }
                }
                for (int q : quads){
                    size_t first = verts.size();
                    BuildHouseCell(cw,only,q,verts);
                    //A cell all of one of them: the darker shade for every other one.
                    const GridQuad& fq = cw.grid->fine.quads[q];
                    uint32_t one = 0;
                    bool f_one = true;
                    for (int k = 0; k < 4; k++){
                        uint32_t b = only.building[fq.v[k]];
                        f_one = f_one && (!b || !one || b == one);
                        one = b ? b : one;
                    }
                    if (f_one && one && made_index(one) % 2 == 1){
                        for (size_t j = first; j < verts.size(); j++){
                            verts[j].uv = vec2(2.0f,2.0f);
                        }
                    }
                }
                //A hut's or a collector's room: one of them made or grown, as for a click.
                if (made.size() == 1 && (kind == ZONE_KIND_WOODCUTTER || kind == ZONE_KIND_WATER || kind == ZONE_KIND_STEAM
                                         || kind == ZONE_KIND_CONDENSER)){
                    uint32_t grown = (made[0] < z.buildings.size() && z.buildings[made[0]].size > 0) ? made[0] : 0;
                    room_more = room_gain(kind,grown,floor_more);
                    f_room_new = grown == 0;
                    room_kind = kind;
                }
            }
            f_ok = placed > 0;
            f_cost = placed > 0;
            //Its size: plots or cells, and how many buildings when more than one.
            const char* unit = f_cells ? "CELL" : "PLOT";
            area_note = " - " + ((made.size() > 1) ? std::to_string(made.size()) + " " + KindPlural(made_kind) + ", " : std::string())
                        + std::to_string(placed) + " " + unit + (placed == 1 ? "" : "S");
            if (founded > 0){
                area_note += ", " + std::to_string(founded) + " ON A FOUNDATION";
            }
            note = area_note.c_str();
        }
        //What the rules refuse: red, plot by plot or cell by cell.
        for (size_t i = 0; i < plan->commands.size() && !plan->f_erase && plan->tool != CHASM_TOOL_ERASE; i++){
            if (!plan->refused[i]){
                continue;
            }
            const DragCommand& c = plan->commands[i];
            size_t first = verts.size();
            if (f_cells){
                GhostCellTile(cw,z,c.index,verts);
            }else{
                GhostPlotTile(cw,z,c.index,verts);
            }
            for (size_t j = first; j < verts.size(); j++){
                verts[j].uv = vec2(-1.0f,-1.0f);
            }
        }
    }else if (tool == CHASM_TOOL_BRIDGE){
        //Not yet pressed, or back over where it was: the bank it would start from, dry and free.
        std::vector<int> span;
        if (from >= 0 && from != plot){
            f_ok = ZoneBridgeChain(cw,z,from,plot,span,NULL);
        }else{
            span.push_back(plot);
            f_ok = !cw.terrain->wet[plot] && ZoneCanBuild(cw,z,plot,ZONE_KIND_BRIDGE,0,NULL);
        }
        for (int v : span){
            BuildPlotTile(cw,z,v,GHOST_LIFT,verts,terrain_levels[cw.terrain->level[v]].height + ZONE_BRIDGE_DECK);
        }
        //Dragged across: the whole bridge's cost, its deck's storey a plot.
        if (from >= 0 && from != plot && span.size() > 1){
            /*
                ...and its trees, each a woodcutter's to fell before any of it rises - warned of when no woodcutter
                gets to one over dry land (the user's, 2026-10-16): on the far bank, it waits for the river to freeze
                (Economy::Derive's wood_reach), or for a hut near if not even the ice brings one there.
            */
            bool f_iced = false;
            bool f_unreached = false;
            for (int v : span){
                for (int g = 0; g < GOOD_COUNT; g++){
                    cost[g] += EconomyKindStoreysCost(cw,z,ZONE_KIND_BRIDGE,v,g,0,ZoneKindPaintStoreys(ZONE_KIND_BRIDGE));
                }
                int on = trees_on(v,-1);
                trees += on;
                int reach = (es && es->world == w && v < (int)es->wood_reach.size()) ? es->wood_reach[v] : ECONOMY_WOOD_REACHED;
                f_iced = f_iced || (on > 0 && reach == ECONOMY_WOOD_ICE);
                f_unreached = f_unreached || (on > 0 && reach == ECONOMY_WOOD_UNREACHED);
            }
            note = f_unreached ? " - NO WOODCUTTER NEAR" : f_iced ? " - ACROSS THE WATER: WAITS FOR THE ICE" : NULL;
            f_cost = true;
            plots = (int)span.size();
        }
    }else if (kind != ZONE_KIND_NONE){
        //A click on a building is one storey more of it, whatever the tool; on empty ground, a new one.
        uint32_t on = z.building[plot];
        int shown = on ? z.buildings[on].kind : kind;
        if (on){
            int top = ZoneKindMaxStoreys(shown);
            f_ok = z.storeys[plot] < top;
            GhostBuildingPlot(cw,z,plot,shown,std::min(top,z.storeys[plot] + 1),on,verts);
            if (f_ok){
                for (int g = 0; g < GOOD_COUNT; g++){
                    cost[g] = EconomyStoreyCost(cw,z,plot,g,z.storeys[plot] + 1);     //the storey over those planned
                }
                f_cost = true;
                lead = "A STOREY MORE";
                if (shown == ZONE_KIND_WOODCUTTER){
                    room_more = room_gain(shown,on,cw.picker->PlotArea(plot));
                    room_kind = shown;
                }
            }
        }else{
            //With one of its kind selected, extending it: red where the plot would not join it.
            f_ok = ZoneCanBuild(cw,z,plot,kind,key.owner,NULL) && ZoneCanExtend(cw,z,plot,kind,key.owner,NULL)
                   && !crate_in_way(ZONE_OP_BUILD_ADD,plot,0);
            GhostBuildingPlot(cw,z,plot,kind,ZoneKindPaintStoreys(kind),0,verts);
            for (int g = 0; g < GOOD_COUNT; g++){
                cost[g] = EconomyKindStoreysCost(cw,z,kind,plot,g,0,ZoneKindPaintStoreys(kind));
            }
            trees = trees_on(plot,-1);
            float floor_more = ZoneKindPaintStoreys(kind) * cw.picker->PlotArea(plot);
            //An extension across a cell brings the corner plot between with it, a storey of it (Zones::Apply).
            if (key.owner){
                int nb[8];
                int n = ZonePlotNeighbours(cw,plot,nb,8);
                bool f_beside = false;
                for (int i = 0; i < n; i++){
                    f_beside = f_beside || z.building[nb[i]] == key.owner;
                }
                int corner = f_beside ? -1 : ZoneJoinCorner(cw,z,plot,kind,key.owner);
                if (corner >= 0){
                    for (int g = 0; g < GOOD_COUNT; g++){
                        cost[g] += EconomyKindStoreyCost(cw,z,kind,corner,g,1);
                    }
                    plots = 2;
                    trees += trees_on(corner,-1);
                    floor_more += cw.picker->PlotArea(corner);
                }
            }
            f_cost = true;
            //Its stone is in the cost already (EconomyKindStoreyCost): the tag says what it is for.
            note = ZoneNeedsFoundation(cw,z,kind,plot) ? " - ON A FOUNDATION" : NULL;
            if (kind == ZONE_KIND_WOODCUTTER || kind == ZONE_KIND_WATER || kind == ZONE_KIND_STEAM || kind == ZONE_KIND_CONDENSER){
                room_more = room_gain(kind,key.owner,floor_more);
                f_room_new = key.owner == 0;
                room_kind = kind;
            }
        }
        if (shown == ZONE_KIND_WINCH){
            GhostPlotTile(cw,z,plot,verts);
            int landing = ZoneWinchLanding(cw,plot);
            if (landing >= 0){
                GhostPlotTile(cw,z,landing,verts);
            }
        }
    }else if (tool == CHASM_TOOL_TERRACE){
        /*
            A TERRACE (terraces_plan.md stage 2), by the square: its nine plots' tiles at the top a terrace from here alone
            would have - or the terrace it would extend, since a drag started on one does. Each plot the rules refuse red
            (a negative u, chasm_ghost.frag), the ghost green while any of it would be placed.
        */
        std::vector<int> square;
        ZoneCellPlots(cw,hover.coarse_quad,square);
        int top = ZoneTerraceTopOver(cw,z,square,(int)square.size());
        int placed = 0;
        for (int v : square){
            bool f_there = ZoneTerraced(z,v);
            bool f_can = top != ZONE_NO_TERRACE && (f_there ? z.terrace[v] == top : ZoneCanTerrace(cw,z,v,top,NULL));
            size_t first = verts.size();
            if (top != ZONE_NO_TERRACE){
                BuildPlotTile(cw,z,v,GHOST_LIFT,verts,(float)top * ZONE_STOREY_HEIGHT + GHOST_LIFT);
            }else{
                GhostPlotTile(cw,z,v,verts);
            }
            if (!f_can){
                for (size_t i = first; i < verts.size(); i++){
                    verts[i].uv = vec2(-1.0f,-1.0f);
                }
            }else if (!f_there){
                cost[GOOD_STONE] += EconomyTerraceStone(cw,v,top);
                trees += trees_on(v,-1);
                placed++;
            }
            f_ok = f_ok || f_can;
        }
        f_cost = placed > 0;
        plots = placed;
    }else if (tool == CHASM_TOOL_GARDEN || tool == CHASM_TOOL_LOT || ChasmLineTool(tool)){
        int ground = (tool == CHASM_TOOL_GARDEN) ? ZONE_GROUND_GARDEN : (tool == CHASM_TOOL_LOT) ? ZONE_GROUND_LOT
                                                                     : ChasmLineGround(tool);
        //A wall over a road is allowed: it gates it (Zones.h, WALLS). A yard painted for a selected building must
        //touch it, and not be another's (yards_plan.md) - over its own ground too, which it would take.
        f_ok = z.ground[plot] == ground || (ground == ZONE_GROUND_WALL && z.ground[plot] == ZONE_GROUND_ROAD)
               || ZoneCanGround(cw,z,plot,NULL,ground);
        //...but no wall where a tower's ladder stands (defence_plan.md), nor under a built arch (arches_plan.md step 4).
        f_ok = f_ok && !(ground == ZONE_GROUND_WALL && ZoneIsLadderPlot(cw,z,plot));
        f_ok = f_ok && !(PlayMode() && ground == ZONE_GROUND_WALL && z.ground[plot] == ZONE_GROUND_ROAD
                         && z.ArchBuiltOf(plot) > 0);
        bool f_own = z.ground[plot] == ground && ZoneGroundOwner(z,plot) == key.owner;
        f_ok = f_ok && (!key.owner || f_own || ZoneCanYard(cw,z,plot,ground,key.owner,NULL))
               && !crate_in_way(ZONE_OP_GROUND_PAINT,plot,ground);
        GhostPlotTile(cw,z,plot,verts);
        //A plot of it - not over its own ground, which is only claimed, nor a wall over a road, which only gates it.
        if (z.ground[plot] != ground && !(ground == ZONE_GROUND_WALL && z.ground[plot] == ZONE_GROUND_ROAD)){
            cost[GOOD_WOOD] = EconomyGroundKindCost(cw,ground,plot);
            f_cost = true;
            //A garden's or lot's own trees (a road's and wall's are along its line: RoadTrees, the economy's alone).
            trees = ZoneEnclosesGround(ground) ? trees_on(plot,-1) : 0;
        }
    }else if (f_field){
        f_ok = (z.field[hover.coarse_quad] || ZoneCanField(cw,z,hover.coarse_quad,NULL))
               && !crate_in_way(ZONE_OP_FIELD_PAINT,hover.coarse_quad,0);
        GhostCellTile(cw,z,hover.coarse_quad,verts);
        if (!z.field[hover.coarse_quad]){
            int cell_kind = (tool == CHASM_TOOL_RANGE) ? ZONE_KIND_RANGE : ZONE_KIND_FIELD;
            for (int g = 0; g < GOOD_COUNT; g++){
                cost[g] = EconomyCellCost(cell_kind,g);
            }
            f_cost = true;
            trees = trees_on(-1,hover.coarse_quad);
        }
    }else if (tool == CHASM_TOOL_ERASE){
        //What would go, in red: the plot's building as it stands, its ground, or the field under it.
        if (z.building[plot]){
            uint32_t on = z.building[plot];
            GhostBuildingPlot(cw,z,plot,z.buildings[on].kind,z.storeys[plot],on,verts);
        }else if (z.ground[plot]){
            GhostPlotTile(cw,z,plot,verts);
        }else if (z.field[hover.coarse_quad]){
            GhostCellTile(cw,z,hover.coarse_quad,verts);
        }
        f_ok = false;
    }
    //Play mode's own rule (play_mode_plan.md): nothing is placed under the clouds - a line asks it per plot, a drag's plan
    //per command.
    if (!f_line && !f_area && (!key.f_explored || !key.f_settled) && tool != CHASM_TOOL_ERASE){
        f_ok = false;
    }
    {
        std::lock_guard<std::mutex> lock(pick_mutex);
        ghost_cost_text = (f_cost && !verts.empty()) ? CostTag(cost,lead,plots,trees) : std::string();
        //What a woodcutter's hut keeps indoors for it (woodcutting_plan.md): none for the floor he works in; what a
        //collector holds, a load at the least.
        if (!ghost_cost_text.empty() && room_more >= 0
            && (room_kind == ZONE_KIND_WATER || room_kind == ZONE_KIND_STEAM || room_kind == ZONE_KIND_CONDENSER)){
            std::string what = (room_kind == ZONE_KIND_STEAM) ? "STEAM" : "WATER";
            ghost_cost_text += (room_more == 0) ? " - HOLDS NO MORE " + what
                             : " - HOLDS " + std::to_string(room_more) + (f_room_new ? " " : " MORE ") + what;
        }else if (!ghost_cost_text.empty() && room_more >= 0){
            ghost_cost_text += (room_more == 0) ? (f_room_new ? " - TOO SMALL TO KEEP WOOD" : " - KEEPS NO MORE WOOD")
                             : " - KEEPS " + std::to_string(room_more) + (f_room_new ? " WOOD INDOORS" : " MORE WOOD");
        }
        if (!ghost_cost_text.empty() && note){
            ghost_cost_text += note;
        }
        //Under the clouds the tag says only that (the user's, 2026-10-17): the trees to fell, the room, the ice a bridge
        //waits for are the hidden ground's, and gave it away. A line is held to them per plot, so it keeps its tag.
        if (!f_line && !f_area && !key.f_explored && tool != CHASM_TOOL_ERASE && !verts.empty()){
            ghost_cost_text = "HIDDEN UNDER THE CLOUDS";
        }
    }
    f_ghost_cost_ok = f_ok;
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

/*
    THE SLOPE GRID (the user's, 2026-10-21). The ghost says yes or no to the one plot under the cursor, and the
    ground round it looks open and gentle - on the starting map much of it is too steep to build on, which nothing
    showed until a click was refused. So while a tool is in hand, the plots round the cursor out to SLOPE_GRID_RADIUS
    - a field's cells for the field and the range - are outlined as the ghost's tile is: green where the land lets
    the tool stand, AMBER where its building stands only on a foundation, which costs stone (terraces_plan.md), red
    where it does not stand at all - amber and red filled in faintly too. It fades out toward its edge, a patch
    round the cursor rather than a second map.

    The LAND's say, not the whole rules': ZoneLandSays, or ZoneCanField on an empty ZoneState - one ground, dry,
    off the mountain, and not too steep for the tool (a road's slope for a road or a wall, a house's for the rest).
    What is built or laid there is the ghost's to say, and shows by itself. The tools whose ground is a place of its
    own - a water collector's bank, a steam collector's drop, a winch's rim, a mine's seam, a bridge's river - have no
    grid: the land's rule would be the wrong one. Ground that is no place to build at all - the water, the mountain, the chasm floor, a
    bare ledge, the map's edge - is left out rather than drawn red: that shows by itself too. Nothing under the
    clouds, which would give the hidden ground away.

    Each outline lies a little in from its tile's edge, so two neighbours' lines run side by side, each its own
    colour. The corners it runs between are the ground mesh's own (TerrainMesh's fans), so a straight piece
    between them lies on the ground; a cell's is laid on each of its corners' own ground, half an edge at a time.
*/
#define SLOPE_GRID_RADIUS   14.0f       //world units round the plot or cell under the cursor: four cells, seven plots or so
#define SLOPE_GRID_FULL     0.4f        //...at full strength out to this share of it, fading to nothing at its edge
#define SLOPE_GRID_INSET    0.10f       //an outline's corners, in toward its tile's middle - world units
#define SLOPE_GRID_WIDTH    0.09f       //...and its width
#define SLOPE_GRID_FILL     0.30f       //an amber or red tile's fill, a share of its outline's strength
#define SLOPE_GRID_ALPHA    0.85f
static const vec3 SLOPE_FOUNDATION(1.0f,0.72f,0.22f);

#define SLOPE_RULE_NONE     -1
#define SLOPE_RULE_PLOT     0           //ZoneLandSays: a building's, a garden's or a lot's
#define SLOPE_RULE_ROAD     1           //...a road's or a wall's
#define SLOPE_RULE_CELL     2           //ZoneCanField: a field's or a range's cell

namespace {

//A strip SLOPE_GRID_WIDTH wide along the ground from a to b, on plot `plot`'s ground; `uv` as chasm_slope.frag reads it.
void SlopeStrip(const ChasmWorld& w, const ZoneState& z, int plot, vec2 a, vec2 b, const vec2& uv, std::vector<vertex>& out){
    vec2 d = b - a;
    float len = d.length();
    if (len < 1e-4f){
        return;
    }
    d = d * (1.0f / len);
    //On past both ends by half its width, so the pieces of an outline meet at its corners.
    a = a - d * (SLOPE_GRID_WIDTH * 0.5f);
    b = b + d * (SLOPE_GRID_WIDTH * 0.5f);
    vec2 side = vec2(-d.y,d.x) * (SLOPE_GRID_WIDTH * 0.5f);
    auto at = [&](const vec2& x){
        return vec3(x.x,ZoneGroundHeight(w,z,plot,x) + GHOST_LIFT,x.y);
    };
    size_t first = out.size();
    MeshQuad(out,at(a - side),at(b - side),at(b + side),at(a + side),vec3(0.0f,1.0f,0.0f),PAL_PATH);
    for (size_t i = first; i < out.size(); i++){
        out[i].uv = uv;
    }
}

//`p` moved SLOPE_GRID_INSET toward `middle`.
vec2 SlopeInset(const vec2& p, const vec2& middle){
    vec2 d = middle - p;
    float len = d.length();
    return (len > SLOPE_GRID_INSET * 2.0f) ? p + d * (SLOPE_GRID_INSET / len) : p;
}

}

void ApplicationChasm::SetSlopeUniforms(){
    slope_shader->Setvec3("slope_ok",GHOST_OK);
    slope_shader->Setvec3("slope_refused",GHOST_REFUSED);
    slope_shader->Setvec3("slope_foundation",SLOPE_FOUNDATION);
    slope_shader->Setfloat("slope_alpha",SLOPE_GRID_ALPHA);
    //Over what stands, never into the depth buffer, as the ghost.
    glDepthMask(GL_FALSE);
}

//RENDER THREAD, every frame, after UpdateGhost: built again only when the plot or cell it is round, the rule, the map or
//what is explored changes.
void ApplicationChasm::UpdateSlopeGrid(){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    if (!slope_grid){
        if (!w || slope_shader_index < 0){
            return;
        }
        world_scene->AtTickBoundary([&](){
            slope_grid = new Object();
            slope_grid->name = "SlopeGrid";
            Mesh* mesh = new Mesh();
            mesh->num_materials = 1;
            slope_grid->SetMesh(mesh);
            slope_grid->SetMaterialSlot(0,palette_material);
            slope_grid->SetVisualOnly(true);
            slope_grid->SetPickability(false);
            slope_grid->SetCastsShadow(false);
            slope_grid->SetVisibility(false);
            world_scene->AddObject(slope_grid);
        });
    }
    int tool = paint_tool;
    int kind = ToolKind(tool);
    int rule = ChasmCellTool(tool) ? SLOPE_RULE_CELL : ChasmLineTool(tool) ? SLOPE_RULE_ROAD
             : (tool == CHASM_TOOL_GARDEN || tool == CHASM_TOOL_LOT) ? SLOPE_RULE_PLOT
             : (kind == ZONE_KIND_NONE || kind == ZONE_KIND_WATER || kind == ZONE_KIND_WINCH || kind == ZONE_KIND_MINE
                || kind == ZONE_KIND_BRIDGE || kind == ZONE_KIND_STEAM) ? SLOPE_RULE_NONE : SLOPE_RULE_PLOT;
    GridPick hover = GetHoverPick();
    std::shared_ptr<const Exploration> ex = GetExploration();
    std::shared_ptr<const ZoneState> zs = GetZones();      //its terraces, which are level ground (terraces_plan.md)
    SlopeKey key;
    if (PlayMode() && rule != SLOPE_RULE_NONE && hover.f_hit && w && zs && zs->world == w){
        key.world = w.get();
        key.zones_version = zs->version;
        key.explored = ex.get();
        key.explored_version = ex ? ex->version : 0;
        key.at = (rule == SLOPE_RULE_CELL) ? hover.coarse_quad : hover.plot;
        key.rule = rule;
        key.kind = (rule == SLOPE_RULE_PLOT) ? kind : ZONE_KIND_NONE;     //whether it takes a foundation
    }
    if (key == slope_built){
        return;
    }
    slope_built = key;
    int counts[3] = {0,0,0};    //ZONE_LAND_*'s order: refused, allowed, on a foundation
    std::vector<vertex> verts;
    if (key.at >= 0){
        const ChasmWorld& cw = *w;
        const ZoneState& z = *zs;
        const Grid& g = *cw.grid;
        const Terrain& t = *cw.terrain;
        //Ground to build on at all: the rest is left out (above).
        auto open = [&](int v){
            return !g.fine.f_boundary[v] && t.Buildable(v) && !t.wet[v] && !t.Mountain(v)
                   && (!ex || ex->Explored(g.fine.pos[v]));
        };
        auto strength = [](float d){
            float x = std::max(0.0f,std::min(1.0f,(d - SLOPE_GRID_RADIUS * SLOPE_GRID_FULL) / (SLOPE_GRID_RADIUS * (1.0f - SLOPE_GRID_FULL))));
            return 1.0f - x * x * (3.0f - 2.0f * x);
        };
        if (rule == SLOPE_RULE_CELL){
            //The land's say: nothing built or laid - but its terraces, level ground made of it.
            ZoneState none;
            none.terrace = z.terrace;
            none.terrace_built = z.terrace_built;
            auto cell_centre = [&](int c){
                return (g.FineQuadCentre(c * 4) + g.FineQuadCentre(c * 4 + 2)) * 0.5f;
            };
            vec2 from = cell_centre(key.at);
            for (int c = 0; c < (int)g.coarse.quads.size(); c++){
                float d = (cell_centre(c) - from).length();
                if (d > SLOPE_GRID_RADIUS){
                    continue;
                }
                ZoneRangeCell cell = ZoneRangeCellAt(cw,c);
                if (!open(cell.middle)){
                    continue;
                }
                bool f_ok = ZoneCanField(cw,none,c,NULL);
                counts[f_ok ? ZONE_LAND_ALLOWED : ZONE_LAND_REFUSED]++;
                float s = strength(d);
                vec2 uv(f_ok ? 0.0f : -1.0f,s);
                //Its outline: of each of its four fine cells, the two sides away from its middle, half a side at a time.
                for (int q = c * 4; q < c * 4 + 4; q++){
                    const GridQuad& fq = g.fine.quads[q];
                    for (int k = 0; k < 4; k++){
                        int a = fq.v[k];
                        int b = fq.v[(k + 1) % 4];
                        if (a == cell.middle || b == cell.middle){
                            continue;
                        }
                        vec2 pa = SlopeInset(g.fine.pos[a],cell.centre);
                        vec2 pb = SlopeInset(g.fine.pos[b],cell.centre);
                        vec2 half = SlopeInset((g.fine.pos[a] + g.fine.pos[b]) * 0.5f,cell.centre);
                        SlopeStrip(cw,z,a,pa,half,uv,verts);
                        SlopeStrip(cw,z,b,half,pb,uv,verts);
                    }
                }
                if (!f_ok){
                    size_t first = verts.size();
                    GhostCellTile(cw,z,c,verts);
                    for (size_t i = first; i < verts.size(); i++){
                        verts[i].uv = vec2(-1.0f,s * SLOPE_GRID_FILL);
                    }
                }
            }
        }else{
            const GridPicker& p = *cw.picker;
            vec2 from = g.fine.pos[key.at];
            for (int v = 0; v < (int)g.fine.pos.size(); v++){
                float d = (g.fine.pos[v] - from).length();
                if (d > SLOPE_GRID_RADIUS || !open(v)){
                    continue;
                }
                int says = ZoneLandSays(cw,z,v,key.kind,rule == SLOPE_RULE_ROAD,NULL);
                counts[says]++;
                float s = strength(d);
                //chasm_slope.frag: u 0 green, past 1 amber, under 0 red.
                float u = (says == ZONE_LAND_ALLOWED) ? 0.0f : (says == ZONE_LAND_FOUNDATION) ? 2.0f : -1.0f;
                vec2 uv(u,s);
                //Its outline: in each cell round it, from the middle of one side, by the cell's middle, to the next's
                //(BuildPlotTile's quarter, less the plot's own corner).
                const vec2& at = g.fine.pos[v];
                float level = t.Height(v);
                for (int i = 0; i < p.PlotQuadCount(v); i++){
                    int qc = p.PlotQuadCorner(v,i);
                    const GridQuad& q = g.fine.quads[qc / 4];
                    int k = qc % 4;
                    vec2 centre = SlopeInset(g.FineQuadCentre(qc / 4),at);
                    vec2 side_b = SlopeInset((at + g.fine.pos[q.v[(k + 1) % 4]]) * 0.5f,at);
                    vec2 side_d = SlopeInset((at + g.fine.pos[q.v[(k + 3) % 4]]) * 0.5f,at);
                    SlopeStrip(cw,z,v,side_b,centre,uv,verts);
                    SlopeStrip(cw,z,v,centre,side_d,uv,verts);
                }
                if (says != ZONE_LAND_ALLOWED){
                    size_t first = verts.size();
                    GhostPlotTile(cw,z,v,verts);
                    for (size_t i = first; i < verts.size(); i++){
                        verts[i].uv = vec2(u,s * SLOPE_GRID_FILL);
                    }
                }
            }
        }
    }
    {
        std::lock_guard<std::mutex> lock(pick_mutex);
        for (int i = 0; i < 3; i++){
            slope_counts[i] = counts[i];
        }
    }
    if (verts.empty()){
        slope_grid->SetVisibility(false);
        return;
    }
    Mesh* mesh = slope_grid->GetMesh();
    mesh->SetMeshData(verts.data(),(int)verts.size());
    //SetMeshData puts a mesh back to MESH_MODE_NORMAL, so the shader tag goes after it.
    mesh->mesh_mode = MESH_MODE_SHADER;
    mesh->custom_shader_index = slope_shader_index;
    slope_grid->SetVisibility(true);
}

/*
    The BUILD BAR'S TIP for a tool (THE PRICE TAG): what one plot or cell of it costs. A cell's and a wall's are
    fixed; a plot's goes by its area, so it is a plot of the map's median area - "about" - and the tag at the cursor
    has the plot's own. A guard tower's goes by its guard places, which come with where paths meet: a place's
    worth. Empty for a tool that places nothing.
*/
std::string ApplicationChasm::ToolCostTip(int tool){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    if (!w || tool == CHASM_TOOL_SELECT || tool == CHASM_TOOL_ERASE){
        return std::string();
    }
    if (w.get() != cost_typical_world){
        cost_typical_world = w.get();
        cost_typical_plot = -1;
        std::vector<std::pair<float,int>> areas;
        for (int v = 0; v < (int)w->grid->fine.pos.size(); v++){
            if (!w->terrain->wet[v]){
                areas.push_back(std::make_pair(w->picker->PlotArea(v),v));
            }
        }
        if (!areas.empty()){
            auto mid = areas.begin() + areas.size() / 2;
            std::nth_element(areas.begin(),mid,areas.end());
            cost_typical_plot = mid->second;
        }
    }
    int v = cost_typical_plot;
    std::array<int,GOOD_COUNT> cost{};
    if (ChasmCellTool(tool)){
        for (int g = 0; g < GOOD_COUNT; g++){
            cost[g] = EconomyCellCost((tool == CHASM_TOOL_RANGE) ? ZONE_KIND_RANGE : ZONE_KIND_FIELD,g);
        }
        return CostWords(cost) + " A CELL";
    }
    if (v < 0){
        return std::string();
    }
    if (tool == CHASM_TOOL_ROAD){
        return "NO GOODS - ONLY WORK: ITS TREES FELLED, THEN LAID";
    }
    if (tool == CHASM_TOOL_WALL){
        cost[GOOD_WOOD] = EconomyGroundKindCost(*w,ZONE_GROUND_WALL,v);
        return CostWords(cost) + " A PLOT, ITS TREES FELLED FIRST";
    }
    if (tool == CHASM_TOOL_GARDEN || tool == CHASM_TOOL_LOT){
        cost[GOOD_WOOD] = EconomyGroundKindCost(*w,(tool == CHASM_TOOL_GARDEN) ? ZONE_GROUND_GARDEN : ZONE_GROUND_LOT,v);
        return "ABOUT " + CostWords(cost) + " A PLOT";
    }
    int kind = ToolKind(tool);
    if (kind == ZONE_KIND_NONE){
        return std::string();
    }
    for (int g = 0; g < GOOD_COUNT; g++){
        cost[g] = EconomyKindStoreysCost(*w,ZoneState(),kind,v,g,0,ZoneKindPaintStoreys(kind));
    }
    if (kind == ZONE_KIND_TOWER){
        int places = std::max(1,ZoneTowerGuards(*w,v));
        for (int g = 0; g < GOOD_COUNT; g++){
            cost[g] = (int)std::lround((float)cost[g] / places);
        }
        return CostWords(cost) + " A GUARD PLACE";
    }
    std::string tip = "ABOUT " + CostWords(cost) + ((kind == ZONE_KIND_BRIDGE) ? " A PLOT OF DECK" : " A PLOT");
    return (ZoneKindMaxStoreys(kind) > ZoneKindPaintStoreys(kind)) ? tip + ", AS MUCH AGAIN A STOREY MORE" : tip;
}

//RENDER THREAD, from DrawOverlay in play mode: the price tag by the cursor, or the bar's tip by the tool hovered there.
void ApplicationChasm::DrawCostTips(float s){
    const float size = 14.0f * s;
    const float side = 9.0f * s;
    const float h = 24.0f * s;
    auto tag = [&](const std::string& text, vec2 t0, uint32_t colour){
        vec2 m = overlay->MeasureText(text.c_str(),size);
        vec2 t1(t0.x + m.x + side * 2.0f,t0.y + h);
        overlay->AddRect(t0,t1,7.0f * s,HudFaded(HUD_PANEL,225));
        overlay->AddText(text.c_str(),vec2(t0.x + side,t0.y + 17.0f * s),size,colour,UI_ALIGN_LEFT);
    };
    //On the bar: the tool's tip, at the bottom beside it, its price in it (DrawToolTip).
    if (bar_tip_tool >= 0){
        DrawToolTip(s);
        return;
    }
    std::string text;
    {
        std::lock_guard<std::mutex> lock(pick_mutex);
        text = ghost_cost_text;
    }
    int mx = overlay_mouse_x.load();
    int my = overlay_mouse_y.load();
    if (text.empty() || mx < 0 || my < 0 || !ghost || !ghost->IsVisible()){
        return;
    }
    //Below and right of the cursor, as the walk's tip is (DrawWalkTip) - left of it near the screen's right edge.
    float w = overlay->MeasureText(text.c_str(),size).x + side * 2.0f;
    float x = (float)mx + 18.0f * s;
    if (x + w > (float)main_window->width - 8.0f * s){
        x = (float)mx - 12.0f * s - w;
    }
    tag(text,vec2(x,(float)my + 14.0f * s),f_ghost_cost_ok ? HUD_TEXT : HUD_TEXT_DIM);
}
