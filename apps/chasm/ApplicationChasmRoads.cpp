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

/*
    Roads and walkers, the app's side (step 10, docs/roads_plan.md). A road needs nothing here: it is
    ground, painted by the zone commands and drawn by the zone mesh. The walkers are simulation state
    the way the zones are - commands in, a tick, a published copy out - and this file is that wiring,
    the figures and path lines that draw them, and their panel and tool.
*/

static Debugger* debug = new Debugger("ChasmRoads",DEBUG_ALL);

#define WALKER_PATH_Y           0.30f       //the debug path line over the ground's relief
#define WALKER_PATH_COLOUR      0xFF40E0FFu
#define WALKER_PATH_STUCK       0xFFFF4040u

//--- Simulation: commands, the tick, the hash -------------------------------------------------------

std::shared_ptr<const WalkerSet> ApplicationChasm::GetWalkers(){
    std::lock_guard<std::mutex> lock(grid_mutex);
    return walker_snapshot;
}

//PHYSICS THREAD, after every change - see PublishZones.
void ApplicationChasm::PublishWalkers(){
    std::shared_ptr<const WalkerSet> copy = std::make_shared<WalkerSet>(walkers.State());
    std::lock_guard<std::mutex> lock(grid_mutex);
    walker_snapshot = copy;
}

void ApplicationChasm::SubmitWalker(int op, int home, int goal){
    SimCommand cmd;
    cmd.type = CHASM_CMD_WALKER;
    //Decided by the view from the camera, like a zone command - so the command is what is recorded.
    cmd.flags = SIM_CMD_FLAG_RECORD;
    cmd.subtype = (uint32_t)op;
    cmd.value[0] = (float)home;     //plot indices are well inside a float's exact integers
    cmd.value[1] = (float)goal;
    main_scene->SubmitCommand(cmd);
}

void ApplicationChasm::RegisterWalkerCommands(){
    main_scene->RegisterCommandHandler(CHASM_CMD_WALKER,
        [this](const SimCommand& cmd) -> objectid_t {
            EnsureZonesWorld();
            bool f_changed = false;
            if (cmd.subtype == WALKER_OP_SPAWN){
                f_changed = walkers.Spawn(zones.State(),(int)cmd.value[0],(int)cmd.value[1]);
            }else if (cmd.subtype == WALKER_OP_CLEAR){
                walkers.Clear();
                walkers.last_refusal.clear();
                f_changed = true;
            }else{
                walkers.last_refusal = "unknown walker op";
            }
            {
                std::lock_guard<std::mutex> lock(grid_mutex);
                walker_last_refusal = walkers.last_refusal;
            }
            if (f_changed){
                PublishWalkers();
            }
            walker_commands_done++;
            return OBJECTID_INVALID;
        });
}

/*
    PHYSICS THREAD. Commands have been drained by now, so a walker plans against this tick's zones.
    Published every tick there is a walker, not only when `version` moves: a walker part way down an
    edge has moved without anything the version counts (which is only for the path lines).
*/
void ApplicationChasm::TickWalkers(){
    uint32_t before = walkers.State().version;
    walkers.Tick(zones.State(),main_scene->GetPhysicsTimestep());
    if (!walkers.State().walkers.empty() || walkers.State().version != before){
        PublishWalkers();
    }
}

//Everything that decides where a walker goes next. `along` by its bits: a replay must match exactly.
void ApplicationChasm::HashWalkers(StateHash& h){
    const WalkerSet& s = walkers.State();
    h.Begin("walkers");
    uint32_t n = (uint32_t)s.walkers.size();
    h.Bytes(&n,sizeof(n));
    //Not zones_version: it counts the zone commands applied, which a restore numbers afresh. At a tick
    //boundary it always equals the zones' own version, since the tick re-plans on any difference.
    for (const Walker& k : s.walkers){
        int32_t head[5] = {k.home,k.goal,k.seg,(int32_t)k.f_outward | ((int32_t)k.f_stuck << 1),(int32_t)k.legs};
        h.Bytes(head,sizeof(head));
        h.Bytes(&k.along,sizeof(k.along));
        uint32_t len = (uint32_t)k.path.size();
        h.Bytes(&len,sizeof(len));
        h.Bytes(k.path.data(),k.path.size() * sizeof(int));
    }
}

/*
    PHYSICS THREAD, from UpdatePaint, on a click with the walker tool: the first click is the home, the
    second the goal and makes the walker. Shift-click sends every walker away.
*/
void ApplicationChasm::UpdateWalkerTool(const GridPick& hover, bool f_shift){
    if (f_shift){
        SubmitWalker(WALKER_OP_CLEAR,-1,-1);
        walker_tool_home = -1;
        return;
    }
    if (walker_tool_home < 0){
        walker_tool_home = hover.plot;
        return;
    }
    SubmitWalker(WALKER_OP_SPAWN,walker_tool_home,hover.plot);
    walker_tool_home = -1;
}

//--- The view --------------------------------------------------------------------------------------

/*
    A walker's figure: a tunic tapering up from the feet, a head, a hat - about two thirds of a storey,
    a little taller than life so it reads at the game's zoom, like A Little Age's chunky villagers.
    Origin at the feet. One mesh, shared by every walker's object.
*/
static void BuildWalkerFigure(std::vector<vertex>& out){
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
    frustum(0.00f,0.17f,0.50f,0.11f,PAL_ROOF);          //tunic: red, so a walker is found at a glance
    frustum(0.50f,0.06f,0.54f,0.06f,PAL_WALL);          //neck
    frustum(0.54f,0.10f,0.72f,0.09f,PAL_WALL);          //head
    frustum(0.72f,0.15f,0.75f,0.15f,PAL_TIMBER);        //hat brim
    frustum(0.75f,0.08f,0.86f,0.04f,PAL_TIMBER);        //crown
}

//RENDER THREAD, from BuildScene (Init, before the physics thread exists).
void ApplicationChasm::BuildWalkerScene(){
    std::vector<vertex> verts;
    BuildWalkerFigure(verts);
    walker_mesh = new Mesh();
    walker_mesh->SetMeshData(verts.data(),(int)verts.size());
#ifdef DEBUG
    walker_path_view = new Object();
    walker_path_view->name = "Walker Paths";
    walker_path_view->SetVisualOnly(true);
    walker_path_mesh = new Mesh();
    walker_path_view->SetMesh(walker_path_mesh);
    walker_path_view->SetPickability(false);
    walker_path_view->SetCastsShadow(false);
    walker_path_view->SetVisibility(false);
    main_scene->AddObject(walker_path_view);
#endif
}

/*
    RENDER THREAD. One object per walker, made when there are more walkers than objects and hidden
    when there are fewer; placed every frame from the published set. The set moves on ticks, so a
    figure steps at the tick rate - fine for a debug walker; a smooth one would interpolate between
    the last two ticks' sets.
*/
void ApplicationChasm::UploadWalkers(){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    std::shared_ptr<const WalkerSet> ws = GetWalkers();
    size_t n = (w && ws && ws->world == w && f_view_walkers) ? ws->walkers.size() : 0;
    if (walker_objects.size() < n){
        main_scene->AtTickBoundary([&](){
            while (walker_objects.size() < n){
                Object* o = new Object();
                o->name = "Walker " + std::to_string(walker_objects.size());
                o->SetMesh(walker_mesh);
                o->SetMaterialSlot(0,palette_material);
                o->SetPickability(false);
                o->SetVisualOnly(true);
                o->SetVisibility(false);
                main_scene->AddObject(o);
                walker_objects.push_back(o);
            }
        });
    }
    for (size_t i = 0; i < walker_objects.size(); i++){
        Object* o = walker_objects[i];
        if (i >= n){
            o->SetVisibility(false);
            continue;
        }
        const Walker& k = ws->walkers[i];
        vec2 facing(0.0f,1.0f);
        vec2 p = Walkers::Position(*w,k,&facing);
        //On the ground of the plot it is at - or on a winch's rope, between the levels.
        o->SetPosition(vec3(p.x,Walkers::Height(*w,k,p),p.y),false);
        o->SetRotation(quat(vec3(0.0f,1.0f,0.0f),atan2f(facing.x,facing.y)),false);
        o->SetVisibility(true);
    }
#ifdef DEBUG
    //The paths, rebuilt when the set changes (a plot passed, a plan made), not every frame.
    uint32_t version = (ws && n) ? ws->version : 0;
    if (version == walker_path_built){
        return;
    }
    walker_path_built = version;
    std::vector<line_vertex> lines;
    for (size_t i = 0; i < n; i++){
        const Walker& k = ws->walkers[i];
        line_vertex lv;
        lv.color = k.f_stuck ? WALKER_PATH_STUCK : WALKER_PATH_COLOUR;
        for (size_t j = (size_t)k.seg; j + 1 < k.path.size(); j++){
            for (int e = 0; e < 2; e++){
                int v = k.path[j + e];
                vec2 p = w->grid->fine.pos[v];
                lv.pos = vec3(p.x,w->terrain->GroundHeight(p,w->terrain->Height(v)) + WALKER_PATH_Y,p.y);
                lines.push_back(lv);
            }
        }
    }
    if (lines.empty()){
        walker_path_view->SetVisibility(false);
    }else{
        walker_path_mesh->SetLineMeshData(lines.data(),(int)lines.size());
        walker_path_view->SetVisibility(true);
    }
#endif
}

//--- The panel ---------------------------------------------------------------------------------------

#ifdef USE_IMGUI
void ApplicationChasm::RenderWalkerPanel(){
    if (!ImGui::CollapsingHeader("Walkers",ImGuiTreeNodeFlags_DefaultOpen)){
        return;
    }
    bool b = f_view_walkers;
    if (ImGui::Checkbox("show##walkers",&b)){
        f_view_walkers = b;
    }
    ImGui::SameLine();
    //The panel is not the physics thread, so it goes through the command queue like a tool would.
    if (ImGui::Button("Clear all##walkers")){
        SubmitWalker(WALKER_OP_CLEAR,-1,-1);
    }
    ImGui::TextDisabled("walker tool (7): click a home, then a goal; shift-click clears");
    if (paint_tool == CHASM_TOOL_WALKER && walker_tool_home >= 0){
        ImGui::Text("home at plot %i - now click the goal",walker_tool_home);
    }
    std::shared_ptr<const WalkerSet> ws = GetWalkers();
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    if (ws && w && ws->world == w){
        for (size_t i = 0; i < ws->walkers.size(); i++){
            const Walker& k = ws->walkers[i];
            int left = (int)k.path.size() - 1 - k.seg;
            ImGui::Text("%i: %i -> %i, %s, %i plots to go, %u legs%s",(int)i,k.home,k.goal,
                        k.f_outward ? "out" : "back",left,k.legs,k.f_stuck ? "  STUCK" : "");
        }
    }
    std::string refusal;
    {
        std::lock_guard<std::mutex> lock(grid_mutex);
        refusal = walker_last_refusal;
    }
    if (!refusal.empty()){
        ImGui::TextColored(ImVec4(1.0f,0.45f,0.35f,1.0f),"last refused: %s",refusal.c_str());
    }
}
#endif

//--- MCP ---------------------------------------------------------------------------------------------

#ifdef USE_MCP
void ApplicationChasm::RegisterWalkerTools(){
    MCPServer::Get()->RegisterTool("chasm_road",
        "Paint (or with erase:true, erase) a road along a straight line from x0,z0 to x1,z1, as a drag of "
        "the road tool would: every plot the line crosses, and the corner between two that only touch "
        "diagonally, so the road is joined. Each plot is one recorded zone command. Returns how many "
        "were painted and the refusals by reason (a road needs flat ground, no river, no field, no house).",
        json{
            {"type","object"},
            {"properties",{
                {"x0",{{"type","number"}}},
                {"z0",{{"type","number"}}},
                {"x1",{{"type","number"}}},
                {"z1",{{"type","number"}}},
                {"erase",{{"type","boolean"}}},
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
                cmd.value[1] = (float)ZONE_GROUND_ROAD;
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
                for (int plot : plots){
                    int arch = ZoneArchStoreys(*w,*z,plot);
                    if (arch > 0){
                        arches.push_back(json{{"plot",plot},{"storeys",arch}});
                    }
                    int gate = ZoneGateOf(*w,*z,plot);
                    if (gate >= 0){
                        gates.push_back(json{{"road",plot},{"into",gate}});
                    }
                }
                result["arches"] = arches;
                result["gates"] = gates;
            }
            return MaybeAttachScreenshot(result,args.value("include_screenshot",false),args.value("include_ui",true));
        });

    MCPServer::Get()->RegisterTool("chasm_walker",
        "Debug walkers (step 10, docs/roads_plan.md): figures walking between a home and a goal plot "
        "and back for ever, by A* over the plots - fastest on roads, closed at cliffs, rivers, houses "
        "(except home and goal) and walls. op spawn takes home and goal as plots or as x/z points; "
        "clear removes every walker; list (the default) reports each one's place, path and state. "
        "Spawn and clear are commands, recorded and replayed; they need the simulation to run a tick.",
        json{
            {"type","object"},
            {"properties",{
                {"op",{{"type","string"},{"enum",{"spawn","clear","list"}}}},
                {"home",{{"type","integer"},{"description","plot (fine vertex)"}}},
                {"goal",{{"type","integer"}}},
                {"home_x",{{"type","number"}}},
                {"home_z",{{"type","number"}}},
                {"goal_x",{{"type","number"}}},
                {"goal_z",{{"type","number"}}},
                {"include_path",{{"type","boolean"},{"description","list each walker's remaining plots"}}},
                {"include_screenshot",{{"type","boolean"}}},
                {"include_ui",{{"type","boolean"}}}
            }}
        },
        [this](const json& args) -> json {
            std::shared_ptr<const ChasmWorld> w = GetWorld();
            if (!w){
                return json{{"error","no world"}};
            }
            std::string op = args.value("op",std::string("list"));
            auto plot_of = [&](const char* name, const char* xname, const char* zname){
                if (args.contains(xname) && args.contains(zname)){
                    GridPick pick = w->picker->Pick(vec2(args[xname].get<float>(),args[zname].get<float>()));
                    return pick.f_hit ? pick.plot : -1;
                }
                return args.value(name,-1);
            };
            json result{{"op",op}};
            if (op == "spawn" || op == "clear"){
                int home = -1, goal = -1;
                if (op == "spawn"){
                    home = plot_of("home","home_x","home_z");
                    goal = plot_of("goal","goal_x","goal_z");
                    if (home < 0 || goal < 0){
                        return json{{"error","no plot at home or goal"}};
                    }
                    result["home"] = home;
                    result["goal"] = goal;
                }
                SimCommand cmd;
                cmd.type = CHASM_CMD_WALKER;
                cmd.flags = SIM_CMD_FLAG_RECORD;
                cmd.subtype = (op == "spawn") ? WALKER_OP_SPAWN : WALKER_OP_CLEAR;
                cmd.value[0] = (float)home;
                cmd.value[1] = (float)goal;
                uint32_t before = walker_commands_done.load();
                SubmitCommandAndWait(cmd);
                result["ran"] = walker_commands_done.load() != before;
                std::lock_guard<std::mutex> lock(grid_mutex);
                result["refusal"] = walker_last_refusal;
            }else if (op != "list"){
                return json{{"error","unknown op " + op}};
            }
            std::shared_ptr<const WalkerSet> ws = GetWalkers();
            json list = json::array();
            if (ws && ws->world == w){
                for (const Walker& k : ws->walkers){
                    vec2 p = Walkers::Position(*w,k);
                    json wj{{"home",k.home},{"goal",k.goal},{"outward",k.f_outward},{"x",p.x},{"z",p.y},
                            {"plots_to_go",(int)k.path.size() - 1 - k.seg},{"stuck",k.f_stuck},{"legs",k.legs}};
                    if (args.value("include_path",false)){
                        wj["path"] = std::vector<int>(k.path.begin() + k.seg,k.path.end());
                    }
                    list.push_back(wj);
                }
            }
            result["walkers"] = list;
            result["tick"] = main_scene->GetPhysicsTick();
            return MaybeAttachScreenshot(result,args.value("include_screenshot",false),args.value("include_ui",true));
        });
}
#endif
