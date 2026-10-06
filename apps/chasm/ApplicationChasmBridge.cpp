#include "ApplicationChasm.h"
#ifdef USE_MCP
#include "MCPServer.h"
#endif

/*
    BRIDGES in the app (docs/bridge_plan.md): the tool that places one over MCP. The rules are the zones'
    (ZoneBridgeChain), the placing a recorded zone command (ZONE_OP_BRIDGE), the building of it the
    economy's, the look BuildingMesh's - this is only the way in for scripts and tests.
*/

#ifdef USE_MCP
void ApplicationChasm::RegisterBridgeTools(){
    MCPServer::Get()->RegisterTool("chasm_bridge",
        "A bridge across a river (docs/bridge_plan.md), from the plot at x0,z0 (dry, one bank) to the plot "
        "at x1,z1 (dry, the other): the chain of plots it takes, whether the rules allow it (both ends dry, "
        "everything between wet, one level, the water crossed at most twice the river's usual width, clear "
        "of a fall) and why not. Unless check:true, placed - a RECORDED zone command, applied before this "
        "returns; play:true makes it a play command (a construction the idle build, refused under the "
        "clouds), otherwise it stands at once. include_screenshot as elsewhere.",
        json{
            {"type","object"},
            {"properties",{
                {"x0",{{"type","number"}}},
                {"z0",{{"type","number"}}},
                {"x1",{{"type","number"}}},
                {"z1",{{"type","number"}}},
                {"check",{{"type","boolean"}}},
                {"play",{{"type","boolean"}}},
                {"include_screenshot",{{"type","boolean"}}},
                {"include_ui",{{"type","boolean"}}}
            }},
            {"required",json::array({"x0","z0","x1","z1"})}
        },
        [this](const json& args) -> json {
            std::shared_ptr<const ChasmWorld> w = GetWorld();
            std::shared_ptr<const ZoneState> z = GetZones();
            if (!w || !z || z->world != w){
                return json{{"error","no world"}};
            }
            for (const char* k : {"x0","z0","x1","z1"}){
                if (!args.contains(k) || !args[k].is_number()){
                    return json{{"error",std::string("needs ") + k}};
                }
            }
            GridPick a = w->picker->Pick(vec2(args["x0"].get<float>(),args["z0"].get<float>()));
            GridPick b = w->picker->Pick(vec2(args["x1"].get<float>(),args["z1"].get<float>()));
            if (!a.f_hit || !b.f_hit){
                return json{{"error","an end is off the map"}};
            }
            std::vector<int> chain;
            const char* why = NULL;
            bool f_ok = ZoneBridgeChain(*w,*z,a.plot,b.plot,chain,&why);
            json plots = json::array();
            int wet = 0;
            for (int v : chain){
                plots.push_back(v);
                wet += w->terrain->wet[v] ? 1 : 0;
            }
            json result{{"from",a.plot},{"to",b.plot},{"plots",plots},{"wet_plots",wet},{"allowed",f_ok},
                        {"span",(w->grid->fine.pos[b.plot] - w->grid->fine.pos[a.plot]).length()}};
            if (!f_ok && why){
                result["refusal"] = why;
            }
            if (f_ok && !args.value("check",false)){
                SimCommand cmd;
                cmd.type = CHASM_CMD_ZONE;
                cmd.flags = SIM_CMD_FLAG_RECORD;
                cmd.subtype = (uint32_t)a.plot;
                cmd.value[0] = (float)ZONE_OP_BRIDGE;
                cmd.value[1] = (float)b.plot;      //exact: plots stay far below a float's 2^24
                cmd.value[2] = 0.0f;
                cmd.value[3] = args.value("play",false) ? 1.0f : 0.0f;
                uint32_t before = zone_commands_done.load();
                SubmitCommandAndWait(cmd);
                std::string refusal;
                {
                    std::lock_guard<std::mutex> lock(grid_mutex);
                    refusal = zone_last_refusal;
                }
                result["ran"] = zone_commands_done.load() != before;
                if (!refusal.empty()){
                    result["refusal"] = refusal;
                }
                std::shared_ptr<const ZoneState> after = GetZones();
                if (after && a.plot < (int)after->building.size()){
                    result["bridge"] = after->building[a.plot];
                }
            }
            return MaybeAttachScreenshot(result,args.value("include_screenshot",false),args.value("include_ui",true));
        });
}
#endif
