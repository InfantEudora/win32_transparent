#include "ApplicationChasm.h"
#ifdef USE_IMGUI
#include "imgui.h"
#endif
#ifdef USE_MCP
#include "MCPServer.h"
#endif
#include "Debug.h"
#include <float.h>
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

/*
    PHYSICS THREAD, after the commands and before the calendar moves on. What the builders finished stands
    on this same tick: the economy names the plots, the zones raise them (construction_plan.md) - not a
    command, since it follows from the state, and a replay builds it again by itself.
*/
void ApplicationChasm::TickEconomy(){
    uint32_t before = economy.State().version;
    economy.Tick(zones.State(),walkers,calendar_tick,main_scene->GetPhysicsTimestep());
    bool f_raised = false;
    for (int plot : economy.Raises()){
        f_raised = zones.Apply(ZONE_OP_BUILD_RAISE,(uint32_t)plot) || f_raised;
    }
    //Gardens and lots the builders finished: their walls go up. Roads laid (line_works_plan.md).
    for (int plot : economy.GroundRaises()){
        f_raised = zones.Apply(ZONE_OP_GROUND_RAISE,(uint32_t)plot) || f_raised;
    }
    //A field site built: its cells, all at once (fields_plan.md).
    for (int cell : economy.FieldRaises()){
        f_raised = zones.Apply(ZONE_OP_FIELD_RAISE,(uint32_t)cell) || f_raised;
    }
    if (f_raised){
        PublishZones();
    }
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
    h.Bytes(&e.next_person,sizeof(e.next_person));
    for (const EconomyWorker& k : e.workers){
        int32_t who[6] = {(int32_t)k.id,(int32_t)k.family,(int32_t)k.house,k.age,(int32_t)k.f_indoors,(int32_t)k.f_female};
        h.Bytes(who,sizeof(who));
        h.Bytes(k.skills,sizeof(k.skills));
        int32_t head[11] = {(int32_t)k.building,k.job,k.state,k.prop,(int32_t)k.store,k.carry_good,k.carry,k.timer,k.seg,
                            (int32_t)k.site,k.stint};
        h.Bytes(head,sizeof(head));
        h.Bytes(&k.along,sizeof(k.along));
        h.Bytes(&k.pos,sizeof(k.pos));
        uint32_t len = (uint32_t)k.route.size();
        h.Bytes(&len,sizeof(len));
        h.Bytes(k.route.data(),k.route.size() * sizeof(vec2));
        h.Bytes(k.speed.data(),k.speed.size() * sizeof(float));
        int32_t needs[2] = {k.health,k.water};
        h.Bytes(needs,sizeof(needs));
    }
    //The dead (needs_plan.md): who, when, of what.
    n = (uint32_t)e.dead.size();
    h.Bytes(&n,sizeof(n));
    for (const EconomyDeath& d : e.dead){
        uint64_t death[3] = {d.id,d.tick,(uint64_t)d.cause};
        h.Bytes(death,sizeof(death));
    }
    for (const EconomyField& f : e.fields){
        int32_t pair[8] = {(int32_t)f.building,f.crop,f.grown,(int32_t)f.f_sown,f.yield,f.last,(int32_t)f.owner,f.frost};
        h.Bytes(pair,sizeof(pair));
    }
    n = (uint32_t)e.site.size();
    h.Bytes(&n,sizeof(n));
    h.Bytes(e.site.data(),e.site.size() * sizeof(int));
    h.Bytes(e.ground_site.data(),e.ground_site.size() * sizeof(int));
}

/*
    The economy's one command: a person's health and water set - the debug panel's villager list and the
    tool's set_needs, for putting a colony on the edge without waiting days for it. RECORDED, like the
    date command, since it changes simulation state and a replay must make the same edit on the same tick.
*/
void ApplicationChasm::RegisterEconomyCommands(){
    main_scene->RegisterCommandHandler(CHASM_CMD_NEEDS,
        [this](const SimCommand& cmd) -> objectid_t {
            if (economy.SetNeeds(cmd.subtype,(int)cmd.value[0],(int)cmd.value[1])){
                PublishEconomy();
            }
            needs_commands_done++;
            return OBJECTID_INVALID;
        });
}

void ApplicationChasm::SubmitNeeds(uint32_t person, int health, int water){
    SimCommand cmd;
    cmd.type = CHASM_CMD_NEEDS;
    cmd.flags = SIM_CMD_FLAG_RECORD;
    cmd.subtype = person;
    cmd.value[0] = (float)health;   //ticks, well inside a float's exact integers
    cmd.value[1] = (float)water;
    main_scene->SubmitCommand(cmd);
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
    json skills = json::object();
    for (int sk = 0; sk < SKILL_COUNT; sk++){
        skills[SkillName(sk)] = (int)k.skills[sk];
    }
    json j{{"id",k.id},{"name",EconomyPersonName(k)},{"family",k.family},{"house",k.house},{"age",k.age},
           {"female",k.f_female},
           {"skills",skills},{"indoors",EconomyIndoors(k)},
           {"building",k.building},{"job",EconomyJobName(k.job)},{"state",EconomyWorkerStateName(k.state)},
           {"x",k.pos.x},{"z",k.pos.y},{"legs_to_go",std::max(0,(int)k.route.size() - 1 - k.seg)},
           //Needs (needs_plan.md) in days of life left - the unit the plan's numbers are in.
           {"health_days",std::round(k.health * 100.0 / CALENDAR_DAY_TICKS) / 100.0},
           {"water_days",std::round(k.water * 100.0 / CALENDAR_DAY_TICKS) / 100.0}};
    if (EconomyDry(k)){
        j["dry"] = true;
    }
    if (k.carry > 0){
        j["carrying"] = json{{GoodName(k.carry_good),k.carry}};
    }
    if (k.prop >= 0){
        j["tree"] = k.prop;
    }
    if (k.store){
        j["store"] = k.store;
    }
    if (k.site && EconomyIsGroundSite(k.site)){
        j["ground_site"] = EconomyGroundSitePlot(k.site);   //a garden, lot or road plot
        if (k.stint){
            j["stint"] = k.stint;
        }
    }else if (k.site){
        j["site"] = k.site;
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
    //A construction site: the wood its planned storeys take, what has been brought, who carries for it.
    int site_wood = EconomySiteWood(*z->world,*z,id);
    if (site_wood > 0){
        ZoneBuildingInfo info = ZoneBuildingFigures(*z->world,*z,id);
        json carriers = json::array();
        for (const EconomyWorker& k : e->workers){
            if (k.site == id){
                carriers.push_back(EconomyPersonName(k));
            }
        }
        j["construction"] = json{{"storeys_to_build",info.site_storeys},{"storeys_standing",info.storeys},
                                 {"wood_needed",site_wood},{"wood_brought",EconomySiteBrought(*e,id)},
                                 {"carriers",carriers},{"priority",info.f_priority}};
    }
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
    if (z->buildings[id].kind == ZONE_KIND_HOUSE || z->buildings[id].kind == ZONE_KIND_CAMP){
        json family = json::array();
        for (const EconomyWorker& k : e->workers){
            if (k.house == id){
                family.push_back(EconomyPersonName(k));
            }
        }
        j["family"] = family;
        j["holds"] = (id < e->capacity.size()) ? e->capacity[id] : 0;
        //Its land (land_plan.md): its fields, their built cells, and the farmers it has places for.
        json land = json::array();
        for (const EconomyField& f : e->fields){
            if (f.owner == id){
                land.push_back(f.building);
            }
        }
        j["land"] = land;
        j["land_places"] = (id < e->places.size()) ? e->places[id] : 0;
    }
    //A house's own store of food, water and firewood, which its family eats and drinks from (needs_plan.md).
    if (z->buildings[id].kind == ZONE_KIND_HOUSE){
        j["keeps"] = GoodsJson((id < e->keeps.size()) ? e->keeps[id] : 0);
        j["room"] = (id < e->room.size()) ? e->room[id] : 0;
    }
    //A woodcutter's woodpile: the lot plot it is on, or -1 with no lot standing beside the hut.
    if (z->buildings[id].kind == ZONE_KIND_WOODCUTTER){
        j["pile_plot"] = (id < e->pile_plot.size()) ? e->pile_plot[id] : -1;
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
    int site_wood = EconomySiteWood(*z->world,*z,id);
    if (site_wood > 0){
        ZoneBuildingInfo info = ZoneBuildingFigures(*z->world,*z,id);
        int carrying = 0;
        for (const EconomyWorker& k : e->workers){
            carrying += (k.site == id) ? 1 : 0;
        }
        ImGui::TextColored(ImVec4(0.95f,0.8f,0.4f,1.0f),"  under construction: %i storeys to build (%i standing)",
                           info.site_storeys,info.storeys);
        ImGui::Text("  wood %i of %i brought, %i carrying for it",EconomySiteBrought(*e,id),site_wood,carrying);
        //The player's "build this first" - a recorded zone command on any plot of it, or a field's on any
        //cell (construction_plan.md).
        bool f_first = info.f_priority;
        if (ImGui::Checkbox("  build this first",&f_first)){
            auto plot = std::find(z->building.begin(),z->building.end(),id);
            auto cell = std::find(z->field.begin(),z->field.end(),id);
            if (plot != z->building.end()){
                SubmitZone(ZONE_OP_SITE_PRIORITY,(int)(plot - z->building.begin()),f_first ? 1 : 0);
            }else if (cell != z->field.end()){
                SubmitZone(ZONE_OP_FIELD_PRIORITY,(int)(cell - z->field.begin()),f_first ? 1 : 0);
            }
        }
    }
    if (z->buildings[id].kind == ZONE_KIND_STORE){
        int room = (id < e->room.size()) ? e->room[id] : 0;
        ImGui::Text("  holds %i of %i: %s",EconomyStockTotal(*e,id),room,stock.empty() ? "nothing" : stock.c_str());
    }else if (!stock.empty()){
        ImGui::Text("  waiting to be carried off: %s",stock.c_str());
    }
    if (z->buildings[id].kind == ZONE_KIND_FIELD){
        const EconomyField* f = EconomyFieldOf(*e,id);
        if (f && f->f_sown){
            ImGui::Text("  %s at %i%%, grown %.0f%% toward the harvest",ZoneCropName(f->crop),f->yield,
                        EconomyFieldGrowth(*e,*z,id) * 100.0f);
        }else if (f){
            int sow = EconomyFieldSowCrop(*f,z->buildings[id]);
            ImGui::Text("  empty - %s",(sow >= 0) ? (std::string("to be sown with ") + ZoneCropName(sow)).c_str()
                                                  : "choose a starting crop");
        }
        //Whose land, and who farms it (land_plan.md).
        if (f){
            uint32_t farmer = (id < e->farmer_of.size()) ? e->farmer_of[id] : 0;
            const char* who = "nobody";
            std::string name;
            for (const EconomyWorker& k : e->workers){
                if (k.id == farmer){
                    name = EconomyPersonName(k);
                    who = name.c_str();
                }
            }
            bool f_camp = f->owner && f->owner < z->buildings.size() && z->buildings[f->owner].kind == ZONE_KIND_CAMP;
            ImGui::Text("  land of %s %u%s, farmed by %s",f_camp ? "camp" : "house",
                        f->owner,z->buildings[id].land_of ? " (given)" : "",who);
        }
    }
    if (z->buildings[id].kind == ZONE_KIND_HOUSE || z->buildings[id].kind == ZONE_KIND_CAMP){
        int holds = (id < e->capacity.size()) ? e->capacity[id] : 0;
        int living = 0;
        for (const EconomyWorker& k : e->workers){
            living += (k.house == id) ? 1 : 0;
        }
        ImGui::Text("  holds %i; %i live here",holds,living);
        for (const EconomyWorker& k : e->workers){
            if (k.house == id){
                ImGui::Text("    %s, %i - %s%s, health %i%%, water %i%%",EconomyPersonName(k).c_str(),k.age,
                            EconomyJobName(k.job),EconomyIndoors(k) ? ", at home" : "",
                            k.health * 100 / NEEDS_HEALTH_FULL,k.water * 100 / NEEDS_WATER_FULL);
            }
        }
    }
    bool f_worked = false;
    for (const EconomyWorker& k : e->workers){
        if (k.building == id){
            f_worked = true;
            int skill = EconomyJobSkill(k.job);
            ImGui::Text("  worked by %s (%s %i): %s",EconomyPersonName(k).c_str(),SkillName(skill),(int)k.skills[skill],
                        EconomyWorkerStateName(k.state));
            if (k.carry > 0){
                ImGui::SameLine();
                ImGui::Text(", carrying %i %s",k.carry,GoodName(k.carry_good));
            }
        }
    }
    int kind = z->buildings[id].kind;
    int places = (id < e->places.size()) ? e->places[id] : 0;
    bool f_land = kind == ZONE_KIND_HOUSE || kind == ZONE_KIND_CAMP;
    if (!f_worked && EconomyJobOf(kind) != WORKER_JOB_NONE && (!f_land || places > 0)){
        ImGui::TextColored(ImVec4(1.0f,0.6f,0.35f,1.0f),f_land ? "  its land is not farmed - nobody of the family is free"
                                                              : "  nobody works here - it needs a free adult with a house");
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

/*
    THE PEOPLE (needs_plan.md): every villager - name, age, job, what he is doing - with his health and
    water as sliders in days, to put a colony on the edge without waiting days for it. A slider is the
    panel's own while it is dragged (needs_editing) and goes out as ONE recorded CHASM_CMD_NEEDS when it is
    let go, so a drag is one edit in a recording rather than one a frame; ctrl-click types a value. The
    buttons set everyone at once. A name goes amber while his need is pressing, red while he is dry.
*/
void ApplicationChasm::RenderPeoplePanel(){
    if (!ImGui::CollapsingHeader("People",ImGuiTreeNodeFlags_DefaultOpen)){
        return;
    }
    std::shared_ptr<const EconomyState> e = GetEconomy();
    std::shared_ptr<const ZoneState> z = GetZones();
    if (!e || !z || e->world != z->world){
        ImGui::TextDisabled("no colony yet");
        return;
    }
    const float day = (float)CALENDAR_DAY_TICKS;
    ImGui::Text("%i living, %i dead",(int)e->workers.size(),(int)e->dead.size());
    auto everyone = [&](int health, int water){
        for (const EconomyWorker& k : e->workers){
            SubmitNeeds(k.id,health,water);
        }
    };
    if (ImGui::SmallButton("All full##people")){
        everyone(NEEDS_HEALTH_FULL,NEEDS_WATER_FULL);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("All thirsty##people")){
        everyone(-1,NEEDS_PRESSING_WATER / 2);      //half a day left: past pressing, short of dry
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("All dry##people")){
        everyone(-1,0);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("All hungry##people")){
        everyone(NEEDS_PRESSING_HEALTH / 2,-1);
    }
    auto slider = [&](const char* id, uint32_t person, int bar, int now, int full){
        uint64_t key = ((uint64_t)person << 1) | (uint64_t)bar;
        auto it = needs_editing.find(key);
        float v = (it != needs_editing.end()) ? it->second : now / day;
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderFloat(id,&v,0.0f,full / day,"%.2f d");
        if (ImGui::IsItemActive()){
            needs_editing[key] = v;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()){
            int ticks = (int)std::lround(v * day);
            SubmitNeeds(person,(bar == 0) ? ticks : -1,(bar == 1) ? ticks : -1);
        }
        if (!ImGui::IsItemActive()){
            needs_editing.erase(key);
        }
    };
    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("people##table",6,flags)){
        ImGui::TableSetupColumn("name",ImGuiTableColumnFlags_WidthStretch,1.6f);
        ImGui::TableSetupColumn("age",ImGuiTableColumnFlags_WidthStretch,0.4f);
        ImGui::TableSetupColumn("job",ImGuiTableColumnFlags_WidthStretch,1.0f);
        ImGui::TableSetupColumn("doing",ImGuiTableColumnFlags_WidthStretch,1.0f);
        ImGui::TableSetupColumn("health",ImGuiTableColumnFlags_WidthStretch,1.3f);
        ImGui::TableSetupColumn("water",ImGuiTableColumnFlags_WidthStretch,1.3f);
        ImGui::TableHeadersRow();
        for (const EconomyWorker& k : e->workers){
            ImGui::PushID((int)k.id);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            uint8_t pressing = EconomyPressing(k);
            ImVec4 colour = EconomyDry(k) ? ImVec4(1.0f,0.4f,0.35f,1.0f)
                          : pressing ? ImVec4(1.0f,0.75f,0.35f,1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_Text);
            ImGui::TextColored(colour,"%u %s",k.id,EconomyPersonName(k).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%i",k.age);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(EconomyJobName(k.job));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(EconomyWorkerStateName(k.state));
            ImGui::TableNextColumn();
            slider("##health",k.id,0,k.health,NEEDS_HEALTH_FULL);
            ImGui::TableNextColumn();
            slider("##water",k.id,1,k.water,NEEDS_WATER_FULL);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (!e->dead.empty() && ImGui::TreeNode("people_dead","The dead (%i)",(int)e->dead.size())){
        for (const EconomyDeath& d : e->dead){
            ImGui::Text("%s, %i - day %.2f, of %s",EconomyDeathName(d).c_str(),d.age,(double)d.tick / CALENDAR_DAY_TICKS,
                        EconomyDeathCause(d.cause));
        }
        ImGui::TreePop();
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
        "store, his health and water in days of life left and whether he is dry - docs/needs_plan.md), "
        "every field's growth toward its harvest, every construction site (wood needed, brought, its "
        "carriers), how many trees are felled, and the dead (who, the colony's day they died on - a "
        "fraction, from 0 at its first dawn - and of what). op store_allow: "
        "the store at x,z (or plot) takes the goods named in `goods` (wood, water, wheat, greens, beans) - "
        "a RECORDED zone command, applied before this returns; a store next to a workplace keeps taking "
        "that workplace's goods whatever it is set to. op site_priority: the construction site at x,z (or "
        "plot) goes first (`on`, default true) or back in the queue - recorded the same way; a site's "
        "`priority` shows in `sites`. op set_needs: person `person`'s health_days and/or water_days (held to their bars) "
        "set - a RECORDED debug edit (CHASM_CMD_NEEDS), the panel's villager list's. It lands before the "
        "tick's eating, so someone at home with food and water tops straight back up; health 0 kills only "
        "someone who cannot eat on that tick. include_screenshot as elsewhere.",
        json{
            {"type","object"},
            {"properties",{
                {"op",{{"type","string"}}},
                {"x",{{"type","number"}}},
                {"z",{{"type","number"}}},
                {"plot",{{"type","integer"}}},
                {"goods",{{"type","array"},{"items",{{"type","string"}}}}},
                {"on",{{"type","boolean"}}},
                {"person",{{"type","integer"},{"description","set_needs: the person's id"}}},
                {"health_days",{{"type","number"}}},
                {"water_days",{{"type","number"}}},
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
            if (op == "store_allow" || op == "site_priority"){
                int plot = args.value("plot",-1);
                int zone_op = (op == "store_allow") ? ZONE_OP_STORE_ALLOW : ZONE_OP_SITE_PRIORITY;
                if (plot < 0 && args.contains("x") && args.contains("z")){
                    GridPick p = w->picker->Pick(vec2(args["x"].get<float>(),args["z"].get<float>()));
                    plot = p.f_hit ? p.plot : -1;
                    //A field there, and no building: a field site goes first by its cell.
                    std::shared_ptr<const ZoneState> zs = GetZones();
                    if (p.f_hit && zone_op == ZONE_OP_SITE_PRIORITY && zs && zs->world == w && !zs->building[p.plot]
                        && p.coarse_quad >= 0 && p.coarse_quad < (int)zs->field.size() && zs->field[p.coarse_quad]){
                        zone_op = ZONE_OP_FIELD_PRIORITY;
                        plot = p.coarse_quad;
                    }
                }
                //What the op sets: a store's goods, or whether a site goes first.
                int value = 0;
                if (op == "store_allow"){
                    for (const json& g : args.value("goods",json::array())){
                        int good = GoodByName(g.get<std::string>());
                        if (good < 0){
                            return json{{"error","no such good: " + g.get<std::string>()}};
                        }
                        value |= GOOD_BIT(good);
                    }
                }else{
                    value = args.value("on",true) ? 1 : 0;
                }
                SimCommand cmd;
                cmd.type = CHASM_CMD_ZONE;
                cmd.flags = SIM_CMD_FLAG_RECORD;
                cmd.subtype = (uint32_t)std::max(0,plot);
                cmd.value[0] = (float)zone_op;
                cmd.value[1] = (float)value;
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
                result[zone_op == ZONE_OP_FIELD_PRIORITY ? "cell" : "plot"] = plot;
                //The economy works out what the store takes, or who carries first, on its next tick.
                main_scene->AtTickBoundary([](){});
            }else if (op == "set_needs"){
                auto ticks = [&](const char* name){
                    return args.contains(name) ? (int)std::lround(args[name].get<double>() * CALENDAR_DAY_TICKS) : -1;
                };
                SimCommand cmd;
                cmd.type = CHASM_CMD_NEEDS;
                cmd.flags = SIM_CMD_FLAG_RECORD;
                cmd.subtype = (uint32_t)args.value("person",0);
                cmd.value[0] = (float)ticks("health_days");
                cmd.value[1] = (float)ticks("water_days");
                uint32_t before = needs_commands_done.load();
                SubmitCommandAndWait(cmd);
                result["ran"] = needs_commands_done.load() != before;
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
                //fields_plan.md: what grows and how well, what it was before, and what is sown next.
                const ZoneBuilding& zb = z->buildings[f.building];
                int sow = EconomyFieldSowCrop(f,zb);
                json yields = json::object();
                for (int c = 0; c < ZONE_CROP_COUNT; c++){
                    yields[ZoneCropName(c)] = EconomyCropYield(f.last,c);
                }
                //land_plan.md: whose land, the player's choice, who farms its share (0: it lies fallow).
                uint32_t farmer = (f.building < e->farmer_of.size()) ? e->farmer_of[f.building] : 0;
                //Where it is: the middle of its first cell, where its farmer stands - for a script's walks.
                vec2 at(0.0f,0.0f);
                auto first = std::find(z->field.begin(),z->field.end(),f.building);
                if (first != z->field.end()){
                    int c = (int)(first - z->field.begin());
                    for (int q = 0; q < 4; q++){
                        at += w->grid->FineQuadCentre(c * 4 + q) * 0.25f;
                    }
                }
                //THE FROST (Economy.h): the growing weather it has left at its middle, what is sown now in it, and
                //whether what grows ripens in time.
                float pace = EconomyFieldPace(*e,f.building);
                int left = EconomyGrowingTicksLeft(*w,at,calendar_shown.load());
                int now = EconomyFieldSowNow(f,zb,pace,left);
                json frost{{"growing_days_left",(float)left / CALENDAR_DAY_TICKS},{"pace",pace},
                           {"sows_now",(now >= 0) ? ZoneCropName(now) : "none"},
                           {"took",(f.frost >= 0) ? ZoneCropName(f.frost) : "none"}};
                if (f.f_sown && f.crop >= 0){
                    frost["ripens_in_time"] = EconomyRipenTicks(f.crop,f.grown,pace) <= left;
                }
                fields.push_back(json{{"id",f.building},{"x",at.x},{"z",at.y},{"sown",f.f_sown},{"owner",f.owner},{"land_of",zb.land_of},
                                      {"farmer",farmer},{"fallow",farmer == 0},
                                      {"crop",(f.f_sown && f.crop >= 0) ? ZoneCropName(f.crop) : "none"},
                                      {"yield",f.yield},{"last",(f.last >= 0) ? ZoneCropName(f.last) : "none"},
                                      {"starting_crop",ZoneCropName(zb.crop)},{"next_crop",ZoneCropName(zb.next_crop)},
                                      {"sows_next",(sow >= 0) ? ZoneCropName(sow) : "none"},{"next_yields",yields},
                                      {"frost",frost},
                                      {"site_cells",ZoneBuildingFigures(*w,*z,f.building).site_cells},
                                      {"growth",EconomyFieldGrowth(*e,*z,f.building)},
                                      {"waiting",StockJson(*e,f.building)}});
            }
            result["fields"] = fields;
            json sites = json::array();
            for (size_t id = 1; id < z->buildings.size(); id++){
                if (z->buildings[id].size > 0 && z->buildings[id].kind != ZONE_KIND_FIELD){
                    json b = EconomyBuildingJson((uint32_t)id);
                    if (b.is_object() && b.contains("construction")){
                        json s = b["construction"];
                        s["id"] = id;
                        s["kind"] = ZoneKindName(z->buildings[id].kind);
                        sites.push_back(s);
                    }
                }
            }
            result["sites"] = sites;
            //The road works (line_works_plan.md): plots still to lay, the people out on them, trees felled.
            int planned = 0;
            int laid = 0;
            for (size_t v = 0; v < z->ground.size(); v++){
                planned += ZoneRoadPlanned(*z,(int)v) ? 1 : 0;
                laid += ZoneRoadLaid(*z,(int)v) ? 1 : 0;
            }
            int on_roads = 0;
            for (const EconomyWorker& k : e->workers){
                on_roads += (k.state == WORKER_TO_ROAD || k.state == WORKER_CLEARING || k.state == WORKER_LAYING) ? 1 : 0;
            }
            result["roads"] = json{{"planned",planned},{"laid",laid},{"people_on_them",on_roads}};
            /*
                Each plot still to lay (the first 64): where it is, its line, and the trees in its way - each with
                where it stands and whether a building or a field covers it - so a road that is never laid can be
                looked at without sending anyone. The trees are the economy's (RoadTrees), read between ticks.
            */
            json planned_plots = json::array();
            main_scene->AtTickBoundary([&](){
                const ZoneState& zs = zones.State();
                std::shared_ptr<const ChasmWorld> wz = zones.GetWorld();
                if (!wz || !wz->forest){
                    return;
                }
                std::vector<int> trees;
                for (size_t v = 0; v < zs.ground.size() && planned_plots.size() < 64; v++){
                    if (!ZoneRoadPlanned(zs,(int)v)){
                        continue;
                    }
                    economy.RoadTrees(zs,(int)v,trees);
                    json tj = json::array();
                    for (int i : trees){
                        const PropInstance& pr = wz->forest->props[i];
                        //What paints over it - none, since RoadTrees leaves covered trees out (EconomyPropCovered).
                        const char* cover = !EconomyPropCovered(zs,pr.plot,pr.coarse) ? "none"
                                          : (pr.plot >= 0 && zs.storeys[pr.plot] > 0) ? "building"
                                          : (pr.coarse >= 0 && zs.field[pr.coarse]) ? "field" : "garden or lot";
                        tj.push_back(json{{"prop",i},{"x",pr.pos.x},{"z",pr.pos.z},{"plot",pr.plot},{"covered_by",cover}});
                    }
                    const vec2& p = wz->grid->fine.pos[v];
                    planned_plots.push_back(json{{"plot",v},{"x",p.x},{"z",p.y},{"line",zs.line[v]},{"trees",tj}});
                }
            });
            result["roads"]["planned_plots"] = planned_plots;
            //The walls (line_works_plan.md, step 3): plots planned and standing, the wood brought to the
            //planned, and who is out clearing their way or carrying for them.
            int wall_planned = 0, wall_standing = 0, wall_wood = 0, on_walls = 0;
            for (size_t v = 0; v < z->ground.size(); v++){
                if (ZoneWallPlanned(*z,(int)v)){
                    wall_planned++;
                    wall_wood += EconomyGroundBrought(*e,(int)v);
                }
                wall_standing += ZoneWallStands(*z,(int)v) ? 1 : 0;
            }
            for (const EconomyWorker& k : e->workers){
                int v = (k.site && EconomyIsGroundSite(k.site)) ? EconomyGroundSitePlot(k.site) : -1;
                on_walls += (v >= 0 && v < (int)z->ground.size() && z->ground[v] == ZONE_GROUND_WALL) ? 1 : 0;
            }
            result["walls"] = json{{"planned",wall_planned},{"standing",wall_standing},{"wood_brought",wall_wood},
                                   {"wood_per_plot",ECONOMY_WALL_WOOD},{"people_on_them",on_walls}};
            int housed = 0;
            int employed = 0;
            for (const EconomyWorker& k : e->workers){
                housed += k.house ? 1 : 0;
                employed += (k.job != WORKER_JOB_NONE) ? 1 : 0;
            }
            result["people"] = json{{"total",e->workers.size()},{"housed",housed},{"working",employed},
                                    {"camp_plot",e->camp_plot}};
            //Per job, who works it and its places - the overview's figures (builders_plan.md).
            EconomyJobCount counts[WORKER_JOB_COUNT];
            int jobless = 0;
            EconomyJobCounts(*e,*z,counts,&jobless);
            json jobs = json::object();
            for (int j = 0; j < WORKER_JOB_COUNT; j++){
                jobs[EconomyJobName(j)] = json{{"working",counts[j].working},{"places",counts[j].places}};
            }
            jobs["none"] = jobless;
            result["jobs"] = jobs;
            //The dead (needs_plan.md), in the order they died, with the day of the colony each died on.
            json dead = json::array();
            for (const EconomyDeath& d : e->dead){
                dead.push_back(json{{"id",d.id},{"name",EconomyDeathName(d)},{"age",d.age},
                                    {"day",(double)d.tick / CALENDAR_DAY_TICKS},{"cause",EconomyDeathCause(d.cause)}});
            }
            result["dead"] = dead;
            if (e->camp_plot >= 0){
                result["camp"] = json{{"x",w->grid->fine.pos[e->camp_plot].x},{"z",w->grid->fine.pos[e->camp_plot].y}};
            }
            int felled = 0;
            for (uint8_t s : e->prop_state){
                felled += (s != PROP_STATE_STANDING) ? 1 : 0;
            }
            result["felled"] = felled;
            return MaybeAttachScreenshot(result,args.value("include_screenshot",false),args.value("include_ui",true));
        });
}
#endif
