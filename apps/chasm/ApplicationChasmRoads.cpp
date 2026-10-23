#include "ApplicationChasm.h"
#include "MeshBuild.h"
#include "BuildingMesh.h"
#include "RoadMesh.h"

#ifdef USE_IMGUI
#include "imgui.h"
#endif

#ifdef USE_MCP
#include "MCPServer.h"
#endif

#include "Debug.h"

#include <math.h>
#include <algorithm>

/*
    Roads, the app's side (step 10, docs/roads_plan.md). A road is ground, painted by the zone commands
    and drawn by the zone mesh; what is here is the road TOOL - a line drawn freehand and placed on
    release (docs/line_works_plan.md) - and since 2026-10-18 the same DRAG for every other tool that paints
    (docs/drag_plan.md), its tool for scripts, and the people's stand-in figure. The debug walker that
    proved the pathfinding lived here until 2026-10-07 (Walkers.h).
*/

static Debugger* debug = new Debugger("ChasmRoads",DEBUG_ALL);

//--- The road tool: a line, drawn and then placed (docs/line_works_plan.md) -------------------------

#define LINE_WALK_MOST      64      //plots a jump of the cursor is walked across before it is left a gap
#define LINE_BACK_MOST      8       //how far back along a line the cursor may come and shorten it

/*
    Plot `to` onto the end of a line, joined to its last plot along fine edges - a road is drawn along
    them, and a wall (to come) only seals where it is. A step to a neighbour is taken as it is; a longer
    one - a diagonal across a cell, or a mouse quicker than a plot a pass - is walked: each step, of the
    neighbours nearer `to`, the one nearest the straight line there (nearest `to` was tried first, and
    zig-zagged in a staircase either side of the line). Back onto one of the line's last few plots, the line
    is cut back to it, so drawing back over a line shortens it - the walked plots between included, which
    the cursor need not pass over again. Further back than that it is the line coming round to itself (a
    wall closing a ring), and goes on.
*/
void LineStepTo(const ChasmWorld& w, std::vector<int>& chain, int to){
    if (chain.empty()){
        chain.push_back(to);
        return;
    }
    if (to == chain.back()){
        return;
    }
    size_t back_to = (chain.size() > LINE_BACK_MOST) ? chain.size() - 1 - LINE_BACK_MOST : 0;
    for (size_t i = chain.size() - 1; i-- > back_to;){
        if (chain[i] == to){
            chain.resize(i + 1);
            return;
        }
    }
    const std::vector<vec2>& pos = w.grid->fine.pos;
    int at = chain.back();
    vec2 a = pos[at];
    vec2 ab = pos[to] - a;
    float ab2 = std::max(1e-6f,ab.x * ab.x + ab.y * ab.y);
    auto off_line = [&](const vec2& p){
        vec2 ap = p - a;
        float t = std::min(1.0f,std::max(0.0f,(ap.x * ab.x + ap.y * ab.y) / ab2));
        return (p - (a + ab * t)).length();
    };
    for (int step = 0; step < LINE_WALK_MOST && at != to; step++){
        int around[16];
        int n = ZonePlotNeighbours(w,at,around,16);
        int best = -1;
        float best_off = 0.0f;
        float here = (pos[at] - pos[to]).length();
        for (int i = 0; i < n; i++){
            const vec2& p = pos[around[i]];
            if ((p - pos[to]).length() >= here){
                continue;
            }
            float off = off_line(p);
            if (best < 0 || off < best_off){
                best_off = off;
                best = around[i];
            }
        }
        if (best < 0){
            break;
        }
        at = best;
        chain.push_back(at);
    }
    if (at != to){
        chain.push_back(to);    //walked as far as it would go: the rest is a gap, and is seen as one
    }
}

bool ApplicationChasm::LinePlotAllowed(const ChasmWorld& w, const ZoneState& z, const Exploration* ex, bool f_erase,
                                       int ground, int plot, const char** why){
    if (plot < 0 || plot >= (int)z.ground.size()){
        return false;
    }
    //Erasing takes up the line's own ground only - and a wall's, its gate's mark off a road (Zones.h, WALLS).
    if (f_erase){
        return z.ground[plot] == ground || (ground == ZONE_GROUND_WALL && z.gate[plot]);
    }
    //Play mode's own rule (play_mode_plan.md), which the command handler holds a play command to.
    if (PlayMode() && ex && !ex->Explored(w.grid->fine.pos[plot])){
        if (why){
            *why = "under the clouds";
        }
        return false;
    }
    //...nor outside every settlement's territory (settlements_plan.md), as the command handler holds it too.
    if (PlayMode()){
        std::shared_ptr<const EconomyState> es = GetEconomy();
        if (es && es->world.get() == &w && !EconomyCommandInTerritory(*es,w,ZONE_OP_GROUND_PAINT,plot)){
            if (why){
                *why = "outside the settlement";
            }
            return false;
        }
    }
    //...nor a wall under an arch with a floor built, which its gate would take down (arches_plan.md step 4).
    if (PlayMode() && ground == ZONE_GROUND_WALL && z.ground[plot] == ZONE_GROUND_ROAD && z.ArchBuiltOf(plot) > 0){
        if (why){
            *why = "an arch stands over the road";
        }
        return false;
    }
    //Already the line's ground - or a wall over a road, which leaves the road and gates it.
    if (z.ground[plot] == ground || (ground == ZONE_GROUND_WALL && z.ground[plot] == ZONE_GROUND_ROAD)){
        return true;
    }
    return ZoneCanGround(w,z,plot,why,ground);
}

/*
    PHYSICS THREAD, from UpdatePaint with the road or wall tool. A press on the map starts a line of the
    tool's ground (with shift, a line that takes that ground up); held, each plot the cursor reaches goes
    onto it (LineStepTo); let go, it is placed (PlaceLine). Dropped (DropLine, a right press), nothing more
    happens until the button is up.
*/
void ApplicationChasm::UpdateLineDrag(const GridPick& hover, bool f_over_scene, bool f_clicked, bool f_held){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    if (line_drag == LINE_DRAG_DROPPED){
        if (!f_held){
            line_drag = LINE_DRAG_NONE;
        }
        return;
    }
    if (line_drag == LINE_DRAG_NONE){
        if (!f_clicked || !f_over_scene || !hover.f_hit || !w){
            return;
        }
        line_drag = LINE_DRAG_DRAWING;
        std::lock_guard<std::mutex> lock(pick_mutex);
        line_chain.assign(1,hover.plot);
        f_line_erase = main_scene->inputcontroller->IsKeyDown(INPUT_SHIFT);
        line_ground = ChasmLineGround(paint_tool);
        line_version++;
        pick_version++;
        return;
    }
    if (!f_held){
        PlaceLine();
        return;
    }
    if (!f_over_scene || !hover.f_hit || !w){
        return;     //off the map, or over the overlay: the line waits where it was
    }
    std::lock_guard<std::mutex> lock(pick_mutex);
    size_t n = line_chain.size();
    int last = line_chain.back();
    LineStepTo(*w,line_chain,hover.plot);
    if (line_chain.size() != n || line_chain.back() != last){
        line_version++;
        pick_version++;
    }
}

/*
    Let go: the line goes out as ONE stroke of per-plot ground commands, in the order it was drawn, each
    plot once. What the preview showed red is left out - so a road meets a river and goes on past it, and
    the bridge goes in the gap - and the first of it is sent LAST, refused by the zones as it would have
    been, so the overlay says why ("CAN'T BUILD: ...") as for any refused placement: a refusal sent
    earlier would be cleared by the plots placed after it. An erasing line takes up the road on it and
    leaves everything else - or a wall's, the wall, and a gate's mark off its road. A wall across a road
    leaves the road and gates it (Zones.h, WALLS).
*/
void ApplicationChasm::PlaceLine(){
    std::vector<int> chain;
    bool f_erase = false;
    int ground = ZONE_GROUND_ROAD;
    {
        std::lock_guard<std::mutex> lock(pick_mutex);
        chain.swap(line_chain);
        f_erase = f_line_erase;
        ground = line_ground;
        line_version++;
        pick_version++;
    }
    line_drag = LINE_DRAG_NONE;
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    std::shared_ptr<const ZoneState> z = GetZones();
    if (!w || !z || z->world != w || chain.empty()){
        return;
    }
    std::shared_ptr<const Exploration> ex = GetExploration();
    paint_stroke = (paint_stroke + 1) & 0x3FFFFF;
    std::vector<int> sent;
    int refused = -1;
    for (int v : chain){
        if (std::find(sent.begin(),sent.end(),v) != sent.end()){
            continue;
        }
        sent.push_back(v);
        if (!LinePlotAllowed(*w,*z,ex.get(),f_erase,ground,v,NULL)){
            if (refused < 0 && !f_erase){
                refused = v;
            }
            continue;
        }
        if (!f_erase && z->ground[v] == ground && (z->ground_built[v] || PlayMode())){
            continue;   //that already - or planned, which in debug the paint builds at once
        }
        if (!f_erase && ground == ZONE_GROUND_WALL && z->gate[v]){
            continue;   //a road this wall gates already
        }
        SubmitZone(f_erase ? ZONE_OP_GROUND_ERASE : ZONE_OP_GROUND_PAINT,v,ground,paint_stroke);
    }
    if (refused >= 0){
        SubmitZone(ZONE_OP_GROUND_PAINT,refused,ground,paint_stroke);
    }
}

//A right press, or the tool put down: the line is gone, and nothing is placed.
void ApplicationChasm::DropLine(){
    line_drag = LINE_DRAG_DROPPED;
    std::lock_guard<std::mutex> lock(pick_mutex);
    line_chain.clear();
    line_version++;
    pick_version++;
}

//--- The drag: every other painting tool, drawn and then placed (docs/drag_plan.md) ---------------------

bool ApplicationChasm::DragTool(int tool){
    if (tool == CHASM_TOOL_GARDEN || tool == CHASM_TOOL_LOT || tool == CHASM_TOOL_ERASE || tool == CHASM_TOOL_TERRACE
        || ChasmCellTool(tool)){
        return true;
    }
    int kind = ToolKind(tool);
    return kind != ZONE_KIND_NONE && kind != ZONE_KIND_BRIDGE && !ZoneKindOnePlot(kind);
}

//A drag over coarse cells - a field's, a range's, and a terrace's squares (terraces_plan.md stage 2) - rather than plots.
static bool CellDrag(int tool){
    return ChasmCellTool(tool) || tool == CHASM_TOOL_TERRACE;
}

static vec2 CoarseCentre(const ChasmWorld& w, int c){
    return (w.grid->FineQuadCentre(c * 4) + w.grid->FineQuadCentre(c * 4 + 2)) * 0.5f;
}

/*
    Coarse cell `to`, the cursor at `at`, onto the end of a cell tool's chain: as LineStepTo, back onto one of its last
    cells cuts it back there; a jump of the cursor brings the cells along the way, as a slower drag would have crossed
    them. A diagonal step is the rules' to join (Zones::Apply brings the cell between).
*/
static void CellStepTo(const ChasmWorld& w, std::vector<int>& chain, int to, const vec2& at){
    if (to < 0){
        return;
    }
    if (chain.empty()){
        chain.push_back(to);
        return;
    }
    if (to == chain.back()){
        return;
    }
    size_t back_to = (chain.size() > LINE_BACK_MOST) ? chain.size() - 1 - LINE_BACK_MOST : 0;
    for (size_t i = chain.size() - 1; i-- > back_to;){
        if (chain[i] == to){
            chain.resize(i + 1);
            return;
        }
    }
    vec2 from = CoarseCentre(w,chain.back());
    int steps = std::min(LINE_WALK_MOST,(int)((at - from).length() / 0.5f));
    for (int i = 1; i < steps; i++){
        GridPick g = w.picker->Pick(from + (at - from) * ((float)i / steps));
        if (g.f_hit && g.coarse_quad >= 0 && g.coarse_quad != chain.back() && g.coarse_quad != to){
            chain.push_back(g.coarse_quad);
        }
    }
    chain.push_back(to);
}

/*
    PHYSICS THREAD, from UpdatePaint with a drag tool (DragTool): pressed, the drag starts on the plot - or a cell tool's
    cell - under the cursor, Shift making it one that takes away; held, what the cursor crosses goes onto it; let go,
    it is placed (PlaceDrag). Dropped (DropDrag, a right press), nothing more until the button is up.
*/
void ApplicationChasm::UpdateAreaDrag(const GridPick& hover, bool f_over_scene, bool f_clicked, bool f_held){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    if (area_drag == LINE_DRAG_DROPPED){
        if (!f_held){
            area_drag = LINE_DRAG_NONE;
        }
        return;
    }
    if (area_drag == LINE_DRAG_NONE){
        if (!f_clicked || !f_over_scene || !hover.f_hit || !w){
            return;
        }
        int tool = paint_tool;
        uint32_t owner = PaintOwner(tool);      //before the lock: it takes pick_mutex itself
        area_drag = LINE_DRAG_DRAWING;
        std::lock_guard<std::mutex> lock(pick_mutex);
        drag_chain.assign(1,CellDrag(tool) ? hover.coarse_quad : hover.plot);
        drag_cells.assign(1,hover.coarse_quad);
        drag_tool = tool;
        f_drag_erase = main_scene->inputcontroller->IsKeyDown(INPUT_SHIFT);
        f_drag_moved = false;
        drag_owner = owner;
        drag_version++;
        pick_version++;
        return;
    }
    if (!f_held){
        PlaceDrag();
        return;
    }
    if (!f_over_scene || !hover.f_hit || !w){
        return;     //off the map, or over the overlay: the drag waits where it was
    }
    std::lock_guard<std::mutex> lock(pick_mutex);
    std::vector<int> was = drag_chain;
    if (CellDrag(drag_tool)){
        CellStepTo(*w,drag_chain,hover.coarse_quad,hover.at);
    }else{
        LineStepTo(*w,drag_chain,hover.plot);
    }
    bool f_changed = drag_chain != was;
    if (drag_tool == CHASM_TOOL_ERASE && hover.coarse_quad >= 0
        && std::find(drag_cells.begin(),drag_cells.end(),hover.coarse_quad) == drag_cells.end()){
        drag_cells.push_back(hover.coarse_quad);
        f_changed = true;
    }
    if (f_changed){
        f_drag_moved = f_drag_moved || drag_chain.size() > 1 || drag_cells.size() > 1;
        drag_version++;
        pick_version++;
    }
}

/*
    The commands a drag's release sends, in the order drawn, each plot or cell once: a building's plots painted as one
    stroke - which joins them into a building until it is full, then goes on with the next - a yard's, a field's or a
    range's likewise; taking away (Shift), what is there of the tool's sort; the eraser, whatever stands on each plot
    and the fields under it. Then each run on a copy of the zones as the rules run it, to see what it does.
*/
bool ApplicationChasm::PlanDrag(int tool, const std::vector<int>& chain, const std::vector<int>& cells, bool f_erase,
                                uint32_t owner, DragPlan& out){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    std::shared_ptr<const ZoneState> z = GetZones();
    out.commands.clear();
    out.refused.clear();
    out.why.clear();
    out.tool = tool;
    out.f_erase = f_erase;
    if (!w || !z || z->world != w || chain.empty()){
        return false;
    }
    out.before = z;
    std::vector<int> seen;
    auto once = [&](int i){
        if (i < 0 || std::find(seen.begin(),seen.end(),i) != seen.end()){
            return false;
        }
        seen.push_back(i);
        return true;
    };
    auto add = [&](int op, int index, int value1, uint32_t by){
        out.commands.push_back(DragCommand{op,index,value1,by});
    };
    int kind = ToolKind(tool);
    if (tool == CHASM_TOOL_ERASE){
        for (int v : chain){
            if (!once(v) || v >= (int)z->building.size()){
                continue;
            }
            if (ZoneCastleShape(*z,v) != ZONE_CASTLE_NONE){
                add(ZONE_OP_CASTLE,v,0,0);      //the castle's stone first: an arch stands over the road under it
            }else if (z->storeys[v]){
                add(ZONE_OP_BUILD_ERASE,v,0,0);
            }else if (z->ground[v]){
                add(ZONE_OP_GROUND_ERASE,v,0,0);
            }else if (ZoneTerraced(*z,v)){
                add(ZONE_OP_TERRACE_ERASE,v,0,0);       //what stood on it went first, by an earlier drag
            }
        }
        seen.clear();
        for (int c : cells){
            if (once(c) && c < (int)z->field.size() && z->field[c]){
                add(ZONE_OP_FIELD_ERASE,c,0,0);
            }
        }
    }else if (ChasmCellTool(tool)){
        int paint = ChasmCellToolOp(tool);
        for (int c : chain){
            if (once(c) && c < (int)z->field.size() && (!f_erase || z->field[c])){
                add(f_erase ? ZONE_OP_FIELD_ERASE : paint,c,0,0);
            }
        }
    }else if (tool == CHASM_TOOL_TERRACE){
        /*
            A TERRACE (terraces_plan.md stage 2), painted by the square as a field is - so its outline runs straight along
            the squares' edges and a field fits on it - each square's nine plots. One platform, so one height for all of
            it: the terrace the drag starts on, which it extends, or the highest ground under any of it, rounded up
            (ZoneTerraceTopOver). What lies too far below that is refused, plot by plot.
        */
        std::vector<int> plots, one;
        int lead = 0;
        for (size_t i = 0; i < chain.size(); i++){
            ZoneCellPlots(*w,chain[i],one);
            for (int v : one){
                if (once(v)){
                    plots.push_back(v);
                }
            }
            lead = (i == 0) ? (int)plots.size() : lead;
        }
        int top = f_erase ? ZONE_NO_TERRACE : ZoneTerraceTopOver(*w,*z,plots,lead);
        for (int v : plots){
            if (v < (int)z->terrace.size() && (!f_erase || ZoneTerraced(*z,v))){
                add(f_erase ? ZONE_OP_TERRACE_ERASE : ZONE_OP_TERRACE,v,top,0);
            }
        }
    }else if (tool == CHASM_TOOL_GARDEN || tool == CHASM_TOOL_LOT){
        int ground = (tool == CHASM_TOOL_GARDEN) ? ZONE_GROUND_GARDEN : ZONE_GROUND_LOT;
        for (int v : chain){
            if (once(v) && v < (int)z->ground.size() && (!f_erase || z->ground[v])){
                add(f_erase ? ZONE_OP_GROUND_ERASE : ZONE_OP_GROUND_PAINT,v,ground,f_erase ? 0 : owner);
            }
        }
    }else if (kind != ZONE_KIND_NONE){
        for (int v : chain){
            if (once(v) && v < (int)z->building.size() && (!f_erase || z->building[v])){
                add(f_erase ? ZONE_OP_BUILD_ERASE : ZONE_OP_BUILD_PAINT,v,kind,f_erase ? 0 : owner);
            }
        }
    }
    //On the copy, with a stroke of its own: any but the last applied, so the first command starts a new one.
    std::shared_ptr<const Exploration> exs = GetExploration();
    static const Exploration none;      //nothing to go by: all of it explored
    std::shared_ptr<const EconomyState> e = GetEconomy();
    out.after.Adopt(*z);
    uint32_t stroke = (z->stroke % 0x3FFFFFu) + 1;
    bool f_play = PlayMode();
    for (const DragCommand& c : out.commands){
        bool f_changed = ApplyZoneCommand(out.after,exs ? *exs : none,(e && e->world == w) ? e.get() : NULL,
                                          c.op,(uint32_t)c.index,c.value1,stroke,f_play,c.owner);
        bool f_refused = !f_changed && !out.after.last_refusal.empty();
        out.refused.push_back(f_refused ? 1 : 0);
        if (f_refused && out.why.empty()){
            out.why = out.after.last_refusal;
        }
    }
    return true;
}

/*
    Let go: a click - never past the plot it was pressed on - is what a click always was: one storey more on a building
    or a new one, one off it with Shift, a plot of yard or a field's cell. A drag goes out as ONE stroke of what PlanDrag
    found the rules take, in the order drawn; what they refuse is left out, so a row of houses goes on past a plot too
    steep, and the overlay says why (drag_last_refusal) - sent itself, a refusal would be cleared by what follows it.
*/
void ApplicationChasm::PlaceDrag(){
    std::vector<int> chain;
    std::vector<int> cells;
    int tool;
    bool f_erase, f_moved;
    uint32_t owner;
    {
        std::lock_guard<std::mutex> lock(pick_mutex);
        chain.swap(drag_chain);
        cells.swap(drag_cells);
        tool = drag_tool;
        f_erase = f_drag_erase;
        f_moved = f_drag_moved;
        owner = drag_owner;
        drag_version++;
        pick_version++;
    }
    area_drag = LINE_DRAG_NONE;
    if (chain.empty()){
        return;
    }
    paint_stroke = (paint_stroke + 1) & 0x3FFFFF;
    int kind = ToolKind(tool);
    if (!f_moved && tool != CHASM_TOOL_ERASE){
        int at = chain[0];
        if (ChasmCellTool(tool)){
            SubmitZone(f_erase ? ZONE_OP_FIELD_ERASE : ChasmCellToolOp(tool),at,0,
                       paint_stroke);
        }else if (tool == CHASM_TOOL_TERRACE){
            //A square of terrace: its nine plots, the commands PlanDrag makes for a drag of one square.
            DragPlan plan;
            if (PlanDrag(tool,chain,cells,f_erase,owner,plan)){
                for (size_t i = 0; i < plan.commands.size(); i++){
                    if (!plan.refused[i]){
                        SubmitZone(plan.commands[i].op,plan.commands[i].index,plan.commands[i].value1,paint_stroke);
                    }
                }
            }
        }else if (tool == CHASM_TOOL_GARDEN || tool == CHASM_TOOL_LOT){
            SubmitZone(f_erase ? ZONE_OP_GROUND_ERASE : ZONE_OP_GROUND_PAINT,at,
                       (tool == CHASM_TOOL_GARDEN) ? ZONE_GROUND_GARDEN : ZONE_GROUND_LOT,paint_stroke,f_erase ? 0 : owner);
        }else{
            SubmitZone(f_erase ? ZONE_OP_BUILD_REMOVE : ZONE_OP_BUILD_ADD,at,kind,paint_stroke,f_erase ? 0 : owner);
        }
        return;
    }
    DragPlan plan;
    if (!PlanDrag(tool,chain,cells,f_erase,owner,plan)){
        return;
    }
    for (size_t i = 0; i < plan.commands.size(); i++){
        if (!plan.refused[i]){
            const DragCommand& c = plan.commands[i];
            SubmitZone(c.op,c.index,c.value1,paint_stroke,c.owner);
        }
    }
    if (!plan.why.empty()){
        {
            std::lock_guard<std::mutex> lock(grid_mutex);
            drag_last_refusal = plan.why;
        }
        drag_refusals++;
    }
}

//A right press, or the tool put down: the drag is gone, and nothing is placed.
void ApplicationChasm::DropDrag(){
    area_drag = LINE_DRAG_DROPPED;
    std::lock_guard<std::mutex> lock(pick_mutex);
    drag_chain.clear();
    drag_cells.clear();
    drag_version++;
    pick_version++;
}

const ApplicationChasm::DragPlan* ApplicationChasm::CurrentDragPlan(uint32_t* serial){
    std::vector<int> chain, cells;
    int tool;
    bool f_erase, f_moved;
    uint32_t owner, version;
    {
        std::lock_guard<std::mutex> lock(pick_mutex);
        if (drag_chain.empty() || !f_drag_moved){
            f_drag_plan = false;
            return NULL;
        }
        chain = drag_chain;
        cells = drag_cells;
        tool = drag_tool;
        f_erase = f_drag_erase;
        f_moved = f_drag_moved;
        owner = drag_owner;
        version = drag_version.load();
    }
    (void)f_moved;
    //What the plan depends on beyond the drag: the zones, the explored ground, the crates and territories, the mode.
    std::shared_ptr<const ZoneState> z = GetZones();
    std::shared_ptr<const Exploration> ex = GetExploration();
    std::shared_ptr<const EconomyState> e = GetEconomy();
    uint64_t key[6] = {(uint64_t)version,(uint64_t)(uintptr_t)z.get(),(uint64_t)(z ? z->version : 0),(uint64_t)(uintptr_t)ex.get(),
                       e ? ((uint64_t)e->crates_version << 32 | e->zones_version) : 0,(uint64_t)PlayMode()};
    if (!f_drag_plan || !std::equal(key,key + 6,drag_plan_key)){
        std::copy(key,key + 6,drag_plan_key);
        f_drag_plan = PlanDrag(tool,chain,cells,f_erase,owner,drag_plan);
        drag_plan_serial++;
    }
    if (serial){
        *serial = drag_plan_serial;
    }
    return f_drag_plan ? &drag_plan : NULL;
}

//--- The view --------------------------------------------------------------------------------------

/*
    The debug walker's figure, kept as the people's stand-in (ApplicationChasmWorkersView.cpp) when the
    props file has none: a tunic tapering up from the feet, a head, a hat - about two thirds of a storey,
    a little taller than life so it reads at the game's zoom, like A Little Age's chunky villagers.
    Origin at the feet, facing +z.
*/
void BuildWalkerFigure(std::vector<vertex>& out, int tunic_column){
    out.clear();
    //Square sections, bottom to top: tunic from 0 to 0.50, a neck, the head, a hat brim and crown.
    auto frustum = [&out](float y0, float h0, float y1, float h1, int column){
        vec3 b[4] = {vec3(-h0,y0,-h0),vec3(h0,y0,-h0),vec3(h0,y0,h0),vec3(-h0,y0,h0)};
        vec3 t[4] = {vec3(-h1,y1,-h1),vec3(h1,y1,-h1),vec3(h1,y1,h1),vec3(-h1,y1,h1)};
        for (int i = 0; i < 4; i++){
            int j = (i + 1) % 4;
            vec3 mid = (b[i] + b[j] + t[i] + t[j]) * 0.25f;
            MeshQuad(out,b[i],b[j],t[j],t[i],vec3(mid.x,0.0f,mid.z),column);
        }
        MeshQuad(out,t[0],t[1],t[2],t[3],vec3(0,1,0),column);
        MeshQuad(out,b[0],b[1],b[2],b[3],vec3(0,-1,0),column);
    };
    frustum(0.00f,0.17f,0.50f,0.11f,tunic_column);
    frustum(0.50f,0.06f,0.54f,0.06f,PAL_WALL);          //neck
    frustum(0.54f,0.10f,0.72f,0.09f,PAL_WALL);          //head
    frustum(0.72f,0.15f,0.75f,0.15f,PAL_TIMBER);        //hat brim
    frustum(0.75f,0.08f,0.86f,0.04f,PAL_TIMBER);        //crown
}

//--- MCP ---------------------------------------------------------------------------------------------

#ifdef USE_MCP
void ApplicationChasm::RegisterRoadTools(){
    MCPServer::Get()->RegisterTool("chasm_road",
        "Paint (or with erase:true, erase) a road along a straight line from x0,z0 to x1,z1, as a drag of "
        "the road tool would: every plot the line crosses, and the corner between two that only touch "
        "diagonally, so the road is joined. Each plot is one recorded zone command. Returns how many "
        "were painted and the refusals by reason (a road needs flat ground, no river, no field, no house). "
        "play:true paints it as a PLAY command: a planned road the idle clear of trees and lay "
        "(docs/line_works_plan.md), refused under the clouds. ground:wall draws a palisade instead (Zones.h, "
        "WALLS) - over a road it leaves the road and gates it, and erase takes walls (and gates) off only. "
        "Each call is one stroke, so one LINE: one place in the construction queue.",
        json{
            {"type","object"},
            {"properties",{
                {"x0",{{"type","number"}}},
                {"z0",{{"type","number"}}},
                {"x1",{{"type","number"}}},
                {"z1",{{"type","number"}}},
                {"erase",{{"type","boolean"}}},
                {"play",{{"type","boolean"},{"description","a planned road, built by the villagers (default false: laid at once)"}}},
                {"ground",{{"type","string"},{"enum",{"road","wall"}},{"description","what the line is (default road)"}}},
                {"include_screenshot",{{"type","boolean"}}},
                {"include_ui",{{"type","boolean"}}}
            }},
            {"required",{"x0","z0","x1","z1"}}
        },
        [this](const json& args) -> json {
            std::shared_ptr<const ChasmWorld> w = GetWorld();
            if (!w){
                return json{{"error","no world"}};
            }
            vec2 a(args["x0"].get<float>(),args["z0"].get<float>());
            vec2 b(args["x1"].get<float>(),args["z1"].get<float>());
            bool f_erase = args.value("erase",false);
            bool f_play = args.value("play",false);
            int ground = (args.value("ground",std::string("road")) == "wall") ? ZONE_GROUND_WALL : ZONE_GROUND_ROAD;
            /*
                One stroke for the call, so the line is one (Zones.h, LINES): in a range of the tool's own,
                above chasm_paint's (2^22 up) and the mouse's (below), and below 2^24 - the stroke travels
                in a float.
            */
            static std::atomic<uint32_t> tool_lines{0};
            uint32_t stroke = (1u << 23) + (++tool_lines & 0x7FFFFF);
            //The plots in the order a drag would reach them, bridged where the drag stepped diagonally.
            std::vector<int> plots;
            int steps = std::max(1,(int)((b - a).length() / 0.25f));
            for (int i = 0; i <= steps; i++){
                vec2 p = a + (b - a) * ((float)i / steps);
                GridPick pick = w->picker->Pick(p);
                if (!pick.f_hit || (!plots.empty() && plots.back() == pick.plot)){
                    continue;
                }
                if (!plots.empty() && !f_erase){
                    int bridge = RoadBridgePlot(*w,plots.back(),pick.plot,p);
                    if (bridge >= 0){
                        plots.push_back(bridge);
                    }
                }
                plots.push_back(pick.plot);
            }
            int done = 0;
            json refusals = json::object();
            for (int plot : plots){
                SimCommand cmd;
                cmd.type = CHASM_CMD_ZONE;
                cmd.flags = SIM_CMD_FLAG_RECORD;
                cmd.subtype = (uint32_t)plot;
                cmd.value[0] = (float)(f_erase ? ZONE_OP_GROUND_ERASE : ZONE_OP_GROUND_PAINT);
                cmd.value[1] = (float)ground;
                cmd.value[2] = (float)stroke;
                cmd.value[3] = f_play ? 1.0f : 0.0f;
                SubmitCommandAndWait(cmd);
                std::string refusal;
                {
                    std::lock_guard<std::mutex> lock(grid_mutex);
                    refusal = zone_last_refusal;
                }
                if (refusal.empty()){
                    done++;
                }else{
                    refusals[refusal] = refusals.value(refusal,0) + 1;
                }
            }
            json result{{"plots",(int)plots.size()},{"changed_or_already",done},{"refused",refusals}};
            //What the line's plots became, for checking the derived things the rules draw from them:
            //an arch's storeys (0: none) and the plot behind a gate (-1: none).
            std::shared_ptr<const ZoneState> z = GetZones();
            if (z && z->world == w){
                json arches = json::array();
                json gates = json::array();
                json wall_gates = json::array();
                uint32_t line = 0;
                for (int plot : plots){
                    if (z->gate[plot]){
                        wall_gates.push_back(plot);     //a road this line's wall runs through, or crosses
                    }
                    if (!line && z->ground[plot] == ground){
                        line = z->line[plot];
                    }
                    //As the zones worked it out (Zones::RefreshArches), and the building it is part of.
                    if (z->ArchOf(plot) > 0){
                        arches.push_back(json{{"plot",plot},{"storeys",z->ArchOf(plot)},{"owner",z->ArchOwnerOf(plot)},
                                              {"built",z->ArchBuiltOf(plot)}});
                    }
                    int gate = ZoneGateOf(*w,*z,plot);
                    if (gate >= 0){
                        gates.push_back(json{{"road",plot},{"into",gate}});
                    }
                }
                result["arches"] = arches;
                result["gates"] = gates;
                result["wall_gates"] = wall_gates;
                result["line"] = line;
            }
            return MaybeAttachScreenshot(result,args.value("include_screenshot",false),args.value("include_ui",true));
        });
}
#endif
