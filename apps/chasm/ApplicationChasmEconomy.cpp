#include "ApplicationChasm.h"
#ifdef USE_IMGUI
#include "imgui.h"
#endif
#ifdef USE_MCP
#include "MCPServer.h"
#endif
#include "Debug.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>

/*
    The economy in the app (docs/economy_plan.md): ticking it, publishing it, hashing it, and showing
    and setting it - the panel's building info and the chasm_economy tool. The rules are Economy.h's.
*/

std::shared_ptr<const EconomyState> ApplicationChasm::GetEconomy(){
    std::lock_guard<std::mutex> lock(grid_mutex);
    return economy_snapshot;
}

void ApplicationChasm::PublishEconomy(){
    std::shared_ptr<const EconomyState> copy = std::make_shared<EconomyState>(economy.State());
    std::lock_guard<std::mutex> lock(grid_mutex);
    economy_snapshot = copy;
}

//PHYSICS THREAD, after the walkers and before the calendar moves on.
void ApplicationChasm::TickEconomy(){
    uint32_t before = economy.State().version;
    economy.Tick(zones.State(),walkers,calendar_tick,main_scene->GetPhysicsTimestep());
    if (economy.State().version != before){
        PublishEconomy();
    }
}

/*
    Everything that decides what the economy does next, by its bits: stocks, the forest, each worker
    whole (his route's floats included), each field's growth. Not the derived parts - what a store
    takes, its room - which follow from the zones, hashed already.
*/
void ApplicationChasm::HashEconomy(StateHash& h){
    const EconomyState& e = economy.State();
    h.Begin("economy");
    uint32_t n = (uint32_t)e.stock.size();
    h.Bytes(&n,sizeof(n));
    for (const auto& s : e.stock){
        h.Bytes(s.data(),sizeof(int) * GOOD_COUNT);
    }
    h.Bytes(e.prop_state.data(),e.prop_state.size());
    n = (uint32_t)e.workers.size();
    h.Bytes(&n,sizeof(n));
    for (const EconomyWorker& k : e.workers){
        int32_t head[9] = {(int32_t)k.building,k.job,k.state,k.prop,(int32_t)k.store,k.carry_good,k.carry,k.timer,k.seg};
        h.Bytes(head,sizeof(head));
        h.Bytes(&k.along,sizeof(k.along));
        h.Bytes(&k.pos,sizeof(k.pos));
        uint32_t len = (uint32_t)k.route.size();
        h.Bytes(&len,sizeof(len));
        h.Bytes(k.route.data(),k.route.size() * sizeof(vec2));
        h.Bytes(k.speed.data(),k.speed.size() * sizeof(float));
    }
    for (const EconomyField& f : e.fields){
        int32_t pair[3] = {(int32_t)f.building,f.crop,f.grown_ticks};
        h.Bytes(pair,sizeof(pair));
    }
}

namespace {

json GoodsJson(uint8_t mask){
    json out = json::array();
    for (int g = 0; g < GOOD_COUNT; g++){
        if (mask & GOOD_BIT(g)){
            out.push_back(GoodName(g));
        }
    }
    return out;
}

json StockJson(const EconomyState& e, uint32_t id){
    json out = json::object();
    for (int g = 0; g < GOOD_COUNT; g++){
        int n = EconomyStock(e,id,g);
        if (n){
            out[GoodName(g)] = n;
        }
    }
    return out;
}

json WorkerJson(const EconomyWorker& k){
    json j{{"building",k.building},{"job",EconomyJobName(k.job)},{"state",EconomyWorkerStateName(k.state)},
           {"x",k.pos.x},{"z",k.pos.y},{"legs_to_go",std::max(0,(int)k.route.size() - 1 - k.seg)}};
    if (k.carry > 0){
        j["carrying"] = json{{GoodName(k.carry_good),k.carry}};
    }
    if (k.prop >= 0){
        j["tree"] = k.prop;
    }
    if (k.store){
        j["store"] = k.store;
    }
    return j;
}

}

//A building's part of the economy: what it holds, what it takes and its room (a store), its worker.
json ApplicationChasm::EconomyBuildingJson(uint32_t id){
    std::shared_ptr<const EconomyState> e = GetEconomy();
    std::shared_ptr<const ZoneState> z = GetZones();
    if (!e || !z || e->world != z->world || id == 0 || id >= z->buildings.size()){
        return nullptr;
    }
    json j{{"stock",StockJson(*e,id)}};
    if (z->buildings[id].kind == ZONE_KIND_STORE){
        uint8_t takes = EconomyAccepts(*e,id);
        j["takes"] = GoodsJson(takes);
        j["own_setting"] = GoodsJson(z->buildings[id].allow);
        j["attached"] = (id < e->attached.size() && e->attached[id] != 0);
        j["room"] = (id < e->room.size()) ? e->room[id] : 0;
    }
    if (z->buildings[id].kind == ZONE_KIND_FIELD){
        j["growth"] = EconomyFieldGrowth(*e,*z,id);
    }
    for (const EconomyWorker& k : e->workers){
        if (k.building == id){
            j["worker"] = WorkerJson(k);
        }
    }
    return j;
}

//--- The panel ------------------------------------------------------------------------------------

#ifdef USE_IMGUI
void ApplicationChasm::RenderEconomyInfo(uint32_t id){
    std::shared_ptr<const EconomyState> e = GetEconomy();
    std::shared_ptr<const ZoneState> z = GetZones();
    if (!e || !z || e->world != z->world || id == 0 || id >= z->buildings.size()){
        return;
    }
    std::string stock;
    for (int g = 0; g < GOOD_COUNT; g++){
        int n = EconomyStock(*e,id,g);
        if (n){
            char buf[32];
            snprintf(buf,sizeof(buf),"%s%s %i",stock.empty() ? "" : ", ",GoodName(g),n);
            stock += buf;
        }
    }
    if (z->buildings[id].kind == ZONE_KIND_STORE){
        int room = (id < e->room.size()) ? e->room[id] : 0;
        ImGui::Text("  holds %i of %i: %s",EconomyStockTotal(*e,id),room,stock.empty() ? "nothing" : stock.c_str());
    }else if (!stock.empty()){
        ImGui::Text("  waiting to be carried off: %s",stock.c_str());
    }
    if (z->buildings[id].kind == ZONE_KIND_FIELD){
        ImGui::Text("  grown %.0f%% toward the harvest",EconomyFieldGrowth(*e,*z,id) * 100.0f);
    }
    for (const EconomyWorker& k : e->workers){
        if (k.building == id){
            if (k.carry > 0){
                ImGui::Text("  its %s: %s, carrying %i %s",EconomyJobName(k.job),EconomyWorkerStateName(k.state),
                            k.carry,GoodName(k.carry_good));
            }else{
                ImGui::Text("  its %s: %s",EconomyJobName(k.job),EconomyWorkerStateName(k.state));
            }
        }
    }
}

/*
    The selected store: what it takes, with a box per good to set its own setting - a recorded zone
    command. Next to a workplace the boxes show the workplace's goods and cannot be changed (the user's
    rule: an attached store is that workplace's store).
*/
void ApplicationChasm::RenderSelectedStore(int plot){
    std::shared_ptr<const EconomyState> e = GetEconomy();
    std::shared_ptr<const ZoneState> z = GetZones();
    if (!e || !z || e->world != z->world || plot < 0 || plot >= (int)z->building.size()
        || z->KindOf(plot) != ZONE_KIND_STORE){
        ImGui::TextDisabled("select a store (Select tool, click) to choose what it takes");
        return;
    }
    uint32_t id = z->building[plot];
    uint8_t own = z->buildings[id].allow;
    uint8_t takes = EconomyAccepts(*e,id);
    bool f_attached = (id < e->attached.size() && e->attached[id] != 0);
    ImGui::Text("selected store %u takes%s:",id,f_attached ? " (next to a workplace - its goods only)" : "");
    if (f_attached){
        ImGui::BeginDisabled();
    }
    for (int g = 0; g < GOOD_COUNT; g++){
        if (g > 0){
            ImGui::SameLine();
        }
        bool f_on = (f_attached ? takes : own) & GOOD_BIT(g);
        std::string label = std::string(GoodName(g)) + "##takes";
        if (ImGui::Checkbox(label.c_str(),&f_on) && !f_attached){
            uint8_t mask = f_on ? (uint8_t)(own | GOOD_BIT(g)) : (uint8_t)(own & ~GOOD_BIT(g));
            SubmitZone(ZONE_OP_STORE_ALLOW,plot,mask);
        }
    }
    if (f_attached){
        ImGui::EndDisabled();
    }
}
#endif

//--- The tool -------------------------------------------------------------------------------------

#ifdef USE_MCP
void ApplicationChasm::RegisterEconomyTools(){
    MCPServer::Get()->RegisterTool("chasm_economy",
        "The colony's goods and workers (docs/economy_plan.md). op status (default): the goods in all "
        "stores together, every store (id, what it takes, its own setting, whether it is next to a "
        "workplace, room, stock), every worker (building, job, state, where, what he carries, his tree or "
        "store), every field's growth toward its harvest, and how many trees are felled. op store_allow: "
        "the store at x,z (or plot) takes the goods named in `goods` (wood, water, wheat, greens, beans) - "
        "a RECORDED zone command, applied before this returns; a store next to a workplace keeps taking "
        "that workplace's goods whatever it is set to. include_screenshot as elsewhere.",
        json{
            {"type","object"},
            {"properties",{
                {"op",{{"type","string"}}},
                {"x",{{"type","number"}}},
                {"z",{{"type","number"}}},
                {"plot",{{"type","integer"}}},
                {"goods",{{"type","array"},{"items",{{"type","string"}}}}},
                {"include_screenshot",{{"type","boolean"}}},
                {"include_ui",{{"type","boolean"}}}
            }}
        },
        [this](const json& args) -> json {
            std::shared_ptr<const ChasmWorld> w = GetWorld();
            if (!w){
                return json{{"error","no world"}};
            }
            std::string op = args.value("op",std::string("status"));
            json result = json::object();
            if (op == "store_allow"){
                int plot = args.value("plot",-1);
                if (plot < 0 && args.contains("x") && args.contains("z")){
                    GridPick p = w->picker->Pick(vec2(args["x"].get<float>(),args["z"].get<float>()));
                    plot = p.f_hit ? p.plot : -1;
                }
                uint8_t mask = 0;
                for (const json& g : args.value("goods",json::array())){
                    int good = GoodByName(g.get<std::string>());
                    if (good < 0){
                        return json{{"error","no such good: " + g.get<std::string>()}};
                    }
                    mask |= GOOD_BIT(good);
                }
                SimCommand cmd;
                cmd.type = CHASM_CMD_ZONE;
                cmd.flags = SIM_CMD_FLAG_RECORD;
                cmd.subtype = (uint32_t)std::max(0,plot);
                cmd.value[0] = (float)ZONE_OP_STORE_ALLOW;
                cmd.value[1] = (float)mask;
                cmd.value[2] = 0.0f;
                uint32_t before = zone_commands_done.load();
                SubmitCommandAndWait(cmd);
                std::string refusal;
                {
                    std::lock_guard<std::mutex> lock(grid_mutex);
                    refusal = zone_last_refusal;
                }
                result["ran"] = zone_commands_done.load() != before;
                result["refusal"] = refusal;
                result["plot"] = plot;
                //The economy works out what the store takes on its next tick.
                main_scene->AtTickBoundary([](){});
            }
            std::shared_ptr<const EconomyState> e = GetEconomy();
            std::shared_ptr<const ZoneState> z = GetZones();
            if (!e || !z || e->world != w || z->world != w){
                result["error"] = "no economy for this world yet";
                return result;
            }
            std::array<int,GOOD_COUNT> totals = EconomyStoredTotals(*e,*z);
            json stored = json::object();
            int food = 0;
            for (int g = 0; g < GOOD_COUNT; g++){
                stored[GoodName(g)] = totals[g];
                food += GoodIsFood(g) ? totals[g] : 0;
            }
            stored["food"] = food;
            result["stored"] = stored;
            json stores = json::array();
            for (size_t id = 1; id < z->buildings.size(); id++){
                if (z->buildings[id].kind == ZONE_KIND_STORE && z->buildings[id].size > 0){
                    json s = EconomyBuildingJson((uint32_t)id);
                    s["id"] = id;
                    stores.push_back(s);
                }
            }
            result["stores"] = stores;
            json workers = json::array();
            for (const EconomyWorker& k : e->workers){
                workers.push_back(WorkerJson(k));
            }
            result["workers"] = workers;
            json fields = json::array();
            for (const EconomyField& f : e->fields){
                fields.push_back(json{{"id",f.building},{"crop",ZoneCropName(z->buildings[f.building].crop)},
                                      {"growth",EconomyFieldGrowth(*e,*z,f.building)},
                                      {"waiting",StockJson(*e,f.building)}});
            }
            result["fields"] = fields;
            int felled = 0;
            for (uint8_t s : e->prop_state){
                felled += (s != PROP_STATE_STANDING) ? 1 : 0;
            }
            result["felled"] = felled;
            return MaybeAttachScreenshot(result,args.value("include_screenshot",false),args.value("include_ui",true));
        });
}
#endif
