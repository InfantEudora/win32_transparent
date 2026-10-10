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
    //A field that waited its day for a crop: it starts with AUTO (issues_plan.md).
    for (int cell : economy.CropDefaults()){
        f_raised = zones.Apply(ZONE_OP_FIELD_CROP,(uint32_t)cell,ZONE_CROP_AUTO) || f_raised;
    }
    if (f_raised){
        PublishZones();
    }
    //Copied for the view once the pass's ticks are done (UpdateView), not after each of them: a whole
    //copy of the economy a tick, ten a pass at 10x, of which the view only ever looks at the last.
    if (economy.State().version != before){
        f_economy_unpublished = true;
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
    //Camps' pantries (Economy.h, STORES): by building id, like the stocks.
    n = (uint32_t)e.pantry.size();
    h.Bytes(&n,sizeof(n));
    for (const auto& s : e.pantry){
        h.Bytes(s.data(),sizeof(int) * GOOD_COUNT);
    }
    h.Bytes(e.prop_state.data(),e.prop_state.size());
    n = (uint32_t)e.workers.size();
    h.Bytes(&n,sizeof(n));
    h.Bytes(&e.next_person,sizeof(e.next_person));
    for (const EconomyWorker& k : e.workers){
        int32_t who[8] = {(int32_t)k.id,(int32_t)k.family,(int32_t)k.house,(int32_t)k.mother,(int32_t)k.father,
                          (int32_t)k.f_indoors,(int32_t)k.f_female,(int32_t)k.surname};
        h.Bytes(who,sizeof(who));
        h.Bytes(&k.born,sizeof(k.born));
        h.Bytes(&k.partner,sizeof(k.partner));
        h.Bytes(&k.due,sizeof(k.due));
        h.Bytes(k.skills,sizeof(k.skills));
        int32_t head[12] = {(int32_t)k.building,k.job,k.state,k.prop,(int32_t)k.store,k.carry_good,k.carry,k.timer,k.seg,
                            (int32_t)k.site,k.stint,k.post};
        h.Bytes(head,sizeof(head));
        //Posted by hand (defence_plan.md, step 8) - only one who is, so a colony without hashes as before.
        if (k.f_chosen){
            h.Add(((uint64_t)k.id << 32) | 0xC405u);
        }
        h.Bytes(&k.along,sizeof(k.along));
        h.Bytes(&k.pos,sizeof(k.pos));
        uint32_t len = (uint32_t)k.route.size();
        h.Bytes(&len,sizeof(len));
        h.Bytes(k.route.data(),k.route.size() * sizeof(vec2));
        h.Bytes(k.speed.data(),k.speed.size() * sizeof(float));
        //The layer of his post (ramparts_plan.md) - only when he was sent to one, likewise.
        if (k.post_layer >= 0){
            h.Add(((uint64_t)k.id << 32) | 0x9057000u | (uint32_t)k.post_layer);
        }
        //Gathering a scout's pack (defence_plan.md, "Scouting") - only while he is, likewise.
        if (k.gather > 0){
            h.Add(((uint64_t)k.id << 32) | 0x6A7000u | (uint32_t)k.gather);
        }
        //His route's layers (ramparts_plan.md) - only once he leaves the ground, so a colony that never does hashes as before.
        if (!k.route_layer.empty()){
            h.Add(((uint64_t)k.id << 32) | 0x1A7Eu);
            h.Bytes(k.route_layer.data(),k.route_layer.size());
        }
        int32_t needs[2] = {k.health,k.water};
        h.Bytes(needs,sizeof(needs));
        h.Bytes(k.pack.data(),sizeof(int) * GOOD_COUNT);     //his pack (defence_plan.md)
        //His archery (range_plan.md) - only once he has some, so a colony nobody trains in hashes as before.
        if (k.archery){
            h.Add(((uint64_t)k.id << 32) | (uint32_t)k.archery);
        }
    }
    //The dead (needs_plan.md): who, when, of what.
    n = (uint32_t)e.dead.size();
    h.Bytes(&n,sizeof(n));
    for (const EconomyDeath& d : e.dead){
        uint64_t death[7] = {d.id,d.tick,(uint64_t)d.cause,(uint64_t)d.born,d.mother,d.father,d.surname};
        h.Bytes(death,sizeof(death));
    }
    for (const EconomyField& f : e.fields){
        int32_t pair[9] = {(int32_t)f.building,f.crop,f.grown,(int32_t)f.f_sown,f.yield,f.last,(int32_t)f.owner,f.frost,f.wait};
        h.Bytes(pair,sizeof(pair));
    }
    n = (uint32_t)e.site.size();
    h.Bytes(&n,sizeof(n));
    h.Bytes(e.site.data(),e.site.size() * sizeof(e.site[0]));     //by good (defence_plan.md step 4)
    h.Bytes(e.ground_site.data(),e.ground_site.size() * sizeof(int));
    //Every home's fire since dawn (needs_plan.md, step 3).
    n = (uint32_t)e.heat.size();
    h.Bytes(&n,sizeof(n));
    h.Bytes(e.heat.data(),e.heat.size());
    //The crates (crates_plan.md): where each lies and what it holds, and who put it down when.
    n = (uint32_t)e.crates.size();
    h.Bytes(&n,sizeof(n));
    h.Bytes(&e.next_crate,sizeof(e.next_crate));
    for (const EconomyCrate& c : e.crates){
        int32_t head[4] = {(int32_t)c.id,c.plot,c.origin,(int32_t)c.by};
        h.Bytes(head,sizeof(head));
        h.Bytes(&c.tick,sizeof(c.tick));
        h.Bytes(&c.pos,sizeof(c.pos));
        h.Bytes(c.stock.data(),sizeof(int) * GOOD_COUNT);
    }
    //The towers' guard counts (defence_plan.md, step 8), the player's: only towers', by id.
    for (size_t id = 1; id < e.guards.size(); id++){
        if (e.guards[id] >= 0){
            h.Add(((uint64_t)id << 32) | (uint32_t)e.guards[id]);
        }
    }
    //What the colony keeps of its made goods, and the orders for more (goods_plan.md) - only once set, so a colony
    //that never set one hashes as before.
    for (int g = 0; g < GOOD_COUNT; g++){
        if (e.kept[g] || e.ordered[g]){
            int32_t kept[3] = {g,e.kept[g],e.ordered[g]};
            h.Bytes(kept,sizeof(kept));
        }
    }
}

/*
    The economy's commands, RECORDED like the date command, since they change simulation state and a replay
    must make the same change on the same tick:
      - a person's health and water set - the debug panel's villager list and the tool's set_needs, for
        putting a colony on the edge without waiting days for it;
      - an ORDER (defence_plan.md): a person sent to stand on a plot - his post, which is then his job - or
        released from it. A refused one says why on the play overlay, as a refused placement does;
      - a GUARD posted by hand on a tower, or released from it, and a tower's guard count (step 8) - the right
        click's and the tower card's; refused as an order is;
      - what the colony KEEPS of a made good, and an order for more (goods_plan.md) - a builders' hut's or a
        ropewalk's card;
      - someone ordered out told to GATHER a scout's pack first, or not (defence_plan.md, "Scouting") - his card's
        chip; refused as an order is;
      - a CRATE put down (crates_plan.md), a debug edit - the panel's and the tool's - a good at a time.
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
    main_scene->RegisterCommandHandler(CHASM_CMD_STOCK,
        [this](const SimCommand& cmd) -> objectid_t {
            if (economy.SetStock(cmd.subtype,(int)cmd.value[0],(int)cmd.value[1],cmd.value[2] != 0.0f)){
                PublishEconomy();
            }
            stock_commands_done++;
            return OBJECTID_INVALID;
        });
    main_scene->RegisterCommandHandler(CHASM_CMD_ORDER,
        [this](const SimCommand& cmd) -> objectid_t {
            std::string why;
            //value[1]: the layer he is sent to, one up so a recording from before layers - 0 there - asks for the top.
            if (economy.Order(cmd.subtype,(int)cmd.value[0],zones.State(),walkers,&why,(int)cmd.value[1] - 1)){
                PublishEconomy();
            }else{
                {
                    std::lock_guard<std::mutex> lock(grid_mutex);
                    order_last_refusal = why;
                }
                order_refusals++;
            }
            order_commands_done++;
            return OBJECTID_INVALID;
        });
    //Guards by hand (defence_plan.md, step 8): a right click on a tower, refused as an order is, and its count.
    main_scene->RegisterCommandHandler(CHASM_CMD_GUARD,
        [this](const SimCommand& cmd) -> objectid_t {
            std::string why;
            if (economy.PostGuard(cmd.subtype,(uint32_t)cmd.value[0],zones.State(),walkers,&why)){
                PublishEconomy();
            }else{
                {
                    std::lock_guard<std::mutex> lock(grid_mutex);
                    order_last_refusal = why;
                }
                order_refusals++;
            }
            order_commands_done++;
            return OBJECTID_INVALID;
        });
    main_scene->RegisterCommandHandler(CHASM_CMD_GUARDS,
        [this](const SimCommand& cmd) -> objectid_t {
            if (economy.SetGuards(cmd.subtype,(int)cmd.value[0],zones.State())){
                PublishEconomy();
            }
            guards_commands_done++;
            return OBJECTID_INVALID;
        });
    //A scout's pack gathered first (defence_plan.md, "Scouting"): his card's chip - refused as an order is.
    main_scene->RegisterCommandHandler(CHASM_CMD_GATHER,
        [this](const SimCommand& cmd) -> objectid_t {
            std::string why;
            if (economy.Gather(cmd.subtype,cmd.value[0] != 0.0f,zones.State(),walkers,&why)){
                PublishEconomy();
            }else{
                {
                    std::lock_guard<std::mutex> lock(grid_mutex);
                    order_last_refusal = why;
                }
                order_refusals++;
            }
            order_commands_done++;
            return OBJECTID_INVALID;
        });
    main_scene->RegisterCommandHandler(CHASM_CMD_KEEP,
        [this](const SimCommand& cmd) -> objectid_t {
            int good = (int)cmd.subtype;
            int count = (int)cmd.value[0];
            bool f_done = (cmd.value[1] != 0.0f) ? economy.OrderMore(good,count,zones.State()) : economy.SetKept(good,count);
            if (f_done){
                PublishEconomy();
            }
            keep_commands_done++;
            return OBJECTID_INVALID;
        });
    main_scene->RegisterCommandHandler(CHASM_CMD_CRATE,
        [this](const SimCommand& cmd) -> objectid_t {
            std::string why;
            if (economy.SpawnCrate((int)cmd.subtype,(int)cmd.value[0],(int)cmd.value[1],zones.State(),&why)){
                PublishEconomy();
            }else{
                {
                    std::lock_guard<std::mutex> lock(grid_mutex);
                    crate_last_refusal = why;
                }
                crate_refusals++;
            }
            crate_commands_done++;
            return OBJECTID_INVALID;
        });
}

void ApplicationChasm::SubmitCrate(int plot, int good, int amount){
    SimCommand cmd;
    cmd.type = CHASM_CMD_CRATE;
    cmd.flags = SIM_CMD_FLAG_RECORD;
    cmd.subtype = (uint32_t)std::max(0,plot);
    cmd.value[0] = (float)good;
    cmd.value[1] = (float)amount;   //well inside a float's exact integers
    main_scene->SubmitCommand(cmd);
}

void ApplicationChasm::SubmitOrder(uint32_t person, int plot, int layer){
    SimCommand cmd;
    cmd.type = CHASM_CMD_ORDER;
    cmd.flags = SIM_CMD_FLAG_RECORD;
    cmd.subtype = person;
    cmd.value[0] = (float)plot;     //a plot index, well inside a float's exact integers
    cmd.value[1] = (float)(layer + 1);
    main_scene->SubmitCommand(cmd);
}

void ApplicationChasm::SubmitGuard(uint32_t person, uint32_t tower){
    SimCommand cmd;
    cmd.type = CHASM_CMD_GUARD;
    cmd.flags = SIM_CMD_FLAG_RECORD;
    cmd.subtype = person;
    cmd.value[0] = (float)tower;    //a building id, well inside a float's exact integers
    main_scene->SubmitCommand(cmd);
}

void ApplicationChasm::SubmitGuards(uint32_t tower, int count){
    SimCommand cmd;
    cmd.type = CHASM_CMD_GUARDS;
    cmd.flags = SIM_CMD_FLAG_RECORD;
    cmd.subtype = tower;
    cmd.value[0] = (float)count;
    main_scene->SubmitCommand(cmd);
}

void ApplicationChasm::SubmitGather(uint32_t person, bool f_on){
    SimCommand cmd;
    cmd.type = CHASM_CMD_GATHER;
    cmd.flags = SIM_CMD_FLAG_RECORD;
    cmd.subtype = person;
    cmd.value[0] = f_on ? 1.0f : 0.0f;
    main_scene->SubmitCommand(cmd);
}

void ApplicationChasm::SubmitKeep(int good, int count, bool f_order){
    SimCommand cmd;
    cmd.type = CHASM_CMD_KEEP;
    cmd.flags = SIM_CMD_FLAG_RECORD;
    cmd.subtype = (uint32_t)std::max(0,good);
    cmd.value[0] = (float)std::max(0,count);
    cmd.value[1] = f_order ? 1.0f : 0.0f;
    main_scene->SubmitCommand(cmd);
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

json GoodsJson(GoodsMask mask){
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

//A stock of its own, a crate's: the goods it has, by name.
json StockJson(const std::array<int,GOOD_COUNT>& s){
    json out = json::object();
    for (int g = 0; g < GOOD_COUNT; g++){
        if (s[g]){
            out[GoodName(g)] = s[g];
        }
    }
    return out;
}

json WorkerJson(const EconomyWorker& k, const EconomyState* e = NULL){
    json skills = json::object();
    for (int sk = 0; sk < SKILL_COUNT; sk++){
        skills[SkillName(sk)] = (int)k.skills[sk];
    }
    //LIFELINES (lifeline_plan.md): his age and stage at the state's tick, his birthday, his parents (0: none here).
    uint64_t tick = e ? e->tick : 0;
    json j{{"id",k.id},{"name",EconomyPersonName(k)},{"family",k.family},{"house",k.house},
           {"age",EconomyAgeAt(k.born,tick)},{"stage",EconomyStageName(EconomyStageAt(k.born,tick))},
           {"birthday",EconomyBirthdayName(k.born)},{"born",k.born},{"mother",k.mother},{"father",k.father},
           {"partner",k.partner},{"surname",EconomySurnameName(k.surname)},
           {"female",k.f_female},
           {"skills",skills},{"indoors",EconomyIndoors(k)},
           {"building",k.building},{"job",EconomyJobName(k.job)},{"state",EconomyWorkerStateName(k.state)},
           {"x",k.pos.x},{"z",k.pos.y},{"legs_to_go",std::max(0,(int)k.route.size() - 1 - k.seg)},
           {"layer",EconomyWorkerLayer(k)},    //ramparts_plan.md: 0 the ground, else half storeys up
           //Needs (needs_plan.md) in days of life left - the unit the plan's numbers are in.
           {"health_days",std::round(k.health * 100.0 / CALENDAR_DAY_TICKS) / 100.0},
           {"water_days",std::round(k.water * 100.0 / CALENDAR_DAY_TICKS) / 100.0}};
    if (EconomyDry(k)){
        j["dry"] = true;
    }
    if (e && EconomyCold(*e,k)){
        j["cold"] = true;       //his home under the snow with no fire today (needs_plan.md, step 3)
    }
    //BIRTHS: expecting (and in how many days the child is due - 0 once it is, until she is home), her children,
    //and the couple's day of the year for its chance.
    if (k.due){
        j["due_in_days"] = std::max(0.0,std::round(((double)k.due - (double)tick) * 100.0 / CALENDAR_DAY_TICKS) / 100.0);
    }
    if (k.f_female && e){
        j["children"] = EconomyChildrenOf(*e,k.id);
    }
    if (k.partner){
        j["couple_day"] = EconomyCoupleDay(k.id,k.partner);
    }
    if (k.carry > 0){
        j["carrying"] = json{{GoodName(k.carry_good),k.carry}};
    }else if ((k.state == WORKER_TO_FETCH || k.state == WORKER_FETCHING) && k.carry_good >= 0){
        j["fetching"] = GoodName(k.carry_good);     //for a site: the good he is on his way for
    }
    if (k.prop >= 0){
        j["tree"] = k.prop;
    }
    if (k.store && EconomyIsCrateStore(k.store)){
        j["crate"] = EconomyCrateOfStore(k.store);     //a crate he is taking from or moving (crates_plan.md)
    }else if (k.store){
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
    //His post (defence_plan.md): the plot he was sent to - or a guard's tower - where on it he stands, and whether
    //he stands there now.
    int post = e ? EconomyPostPlot(*e,k) : -1;
    if (post >= 0 && e->world && post < (int)e->world->grid->fine.pos.size()){
        vec2 p = EconomyPostPoint(*e,k);
        j["post"] = json{{"plot",post},{"x",p.x},{"z",p.y},{"there",EconomyAtPost(*e,k)},{"ordered",k.post >= 0},{"layer",k.post_layer}};
    }
    if (k.gather > 0){
        j["gather"] = k.gather;     //places still to go to for a scout's pack (defence_plan.md, "Scouting")
    }
    if (k.f_chosen){
        j["chosen"] = true;         //a guard posted by hand (step 8)
    }
    //His pack: what he carries to live on, by good, and its food in days.
    json pack = json::object();
    for (int g = 0; g < GOOD_COUNT; g++){
        if (k.pack[g] > 0){
            pack[GoodName(g)] = k.pack[g];
        }
    }
    if (!pack.empty()){
        pack["food_days"] = std::round(EconomyPackFood(k) * 100.0 / CALENDAR_DAY_TICKS) / 100.0;
        j["pack"] = pack;
    }
    //His archery (range_plan.md), in percent, and the most his precision lets him reach.
    if (k.archery || k.job == WORKER_JOB_ARCHER){
        j["archery"] = json{{"percent",std::round(k.archery * 10000.0 / ARCHERY_FULL) / 100.0},
                            {"cap",std::round(EconomyArcheryCap(k) * 100.0 / ARCHERY_FULL)}};
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
    //No door: no plot of it can be walked onto from open ground (EconomyShutIn) - a house so is uninhabitable.
    if (EconomyShutIn(*e,id)){
        j["shut_in"] = true;
    }
    //A construction site: what its planned storeys take of each good, what has been brought, who carries for it.
    std::array<int,GOOD_COUNT> site_cost = EconomySiteCost(*z->world,*z,id);
    if (std::any_of(site_cost.begin(),site_cost.end(),[](int x){ return x > 0; })){
        ZoneBuildingInfo info = ZoneBuildingFigures(*z->world,*z,id);
        json carriers = json::array();
        for (const EconomyWorker& k : e->workers){
            if (k.site == id){
                carriers.push_back(EconomyPersonName(k));
            }
        }
        json needed = json::object();
        json brought = json::object();
        for (int g = 0; g < GOOD_COUNT; g++){
            if (site_cost[g] > 0 || EconomySiteBrought(*e,id,g) > 0){
                needed[GoodName(g)] = site_cost[g];
                brought[GoodName(g)] = EconomySiteBrought(*e,id,g);
            }
        }
        j["construction"] = json{{"storeys_to_build",info.site_storeys},{"storeys_standing",info.storeys},
                                 {"wood_needed",site_cost[GOOD_WOOD]},{"wood_brought",EconomySiteBrought(*e,id,GOOD_WOOD)},
                                 {"needed",needed},{"brought",brought},
                                 {"carriers",carriers},{"priority",info.f_priority}};
    }
    if (z->buildings[id].kind == ZONE_KIND_STORE){
        GoodsMask takes = EconomyAccepts(*e,id);
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
    //A house's own store of food, water and firewood, which its family eats and drinks from (needs_plan.md) - a
    //camp's is its PANTRY, beside its supplies (`stock`) (Economy.h, STORES).
    if (z->buildings[id].kind == ZONE_KIND_CAMP){
        json pantry = json::object();
        for (int g = 0; g < GOOD_COUNT; g++){
            int n = (id < e->pantry.size()) ? e->pantry[id][g] : 0;
            if (n){
                pantry[GoodName(g)] = n;
            }
        }
        j["pantry"] = pantry;
    }
    if (z->buildings[id].kind == ZONE_KIND_HOUSE || z->buildings[id].kind == ZONE_KIND_CAMP){
        j["keeps"] = GoodsJson((id < e->keeps.size()) ? e->keeps[id] : 0);
        j["room"] = (id < e->room.size()) ? e->room[id] : 0;
        j["woodshed"] = (id < e->woodshed.size()) ? e->woodshed[id] : 0;
        j["winter_snow"] = (id < e->snowed.size() && e->snowed[id] != 0);
    }
    //Its fire (step 3): what it burns a dawn under the snow, and whether it is warm or cold today.
    if (z->buildings[id].kind == ZONE_KIND_HOUSE || z->buildings[id].kind == ZONE_KIND_CAMP){
        static const char* heats[3] = {"none","warm","cold"};
        int heat = (id < e->heat.size()) ? e->heat[id] : HEAT_NONE;
        j["burns"] = (id < e->burn.size()) ? e->burn[id] : 0;
        j["heat"] = heats[std::min(2,std::max(0,heat))];
    }
    //A woodcutter's woodpile: the lot plot it is on, or -1 with no lot standing beside the hut.
    if (z->buildings[id].kind == ZONE_KIND_WOODCUTTER){
        j["pile_plot"] = (id < e->pile_plot.size()) ? e->pile_plot[id] : -1;
    }
    for (const EconomyWorker& k : e->workers){
        if (k.building == id){
            j["worker"] = WorkerJson(k,e.get());
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
    std::array<int,GOOD_COUNT> site_cost = EconomySiteCost(*z->world,*z,id);
    if (std::any_of(site_cost.begin(),site_cost.end(),[](int x){ return x > 0; })){
        ZoneBuildingInfo info = ZoneBuildingFigures(*z->world,*z,id);
        int carrying = 0;
        for (const EconomyWorker& k : e->workers){
            carrying += (k.site == id) ? 1 : 0;
        }
        ImGui::TextColored(ImVec4(0.95f,0.8f,0.4f,1.0f),"  under construction: %i storeys to build (%i standing)",
                           info.site_storeys,info.storeys);
        for (int g = 0; g < GOOD_COUNT; g++){
            if (site_cost[g] > 0){
                ImGui::Text("  %s %i of %i brought",GoodName(g),EconomySiteBrought(*e,id,g),site_cost[g]);
            }
        }
        ImGui::Text("  %i carrying for it",carrying);
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
                ImGui::Text("    %s, %i - %s%s, health %i%%, water %i%%",EconomyPersonName(k).c_str(),
                            EconomyAgeAt(k.born,e->tick),
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
    GoodsMask own = z->buildings[id].allow;
    GoodsMask takes = EconomyAccepts(*e,id);
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
            GoodsMask mask = f_on ? (GoodsMask)(own | GOOD_BIT(g)) : (GoodsMask)(own & ~GOOD_BIT(g));
            SubmitZone(ZONE_OP_STORE_ALLOW,plot,mask);
        }
    }
    if (f_attached){
        ImGui::EndDisabled();
    }
}

/*
    The selected plot's CRATE (crates_plan.md): what lies there, and a debug crate to put down - a good and how
    much, a recorded CHASM_CMD_CRATE, into the crate already there if there is one. Refused off open ground.
*/
void ApplicationChasm::RenderSelectedCrate(int plot){
    std::shared_ptr<const EconomyState> e = GetEconomy();
    std::shared_ptr<const ZoneState> z = GetZones();
    if (!e || !z || e->world != z->world || plot < 0 || plot >= (int)z->building.size()){
        return;
    }
    const EconomyCrate* c = EconomyCrateOn(*e,plot);
    if (c){
        std::string goods;
        for (int g = 0; g < GOOD_COUNT; g++){
            if (c->stock[g]){
                char buf[32];
                snprintf(buf,sizeof(buf),"%s%s %i",goods.empty() ? "" : ", ",GoodName(g),c->stock[g]);
                goods += buf;
            }
        }
        ImGui::Text("crate %u here (%s): %s",c->id,EconomyCrateOriginName(c->origin),goods.c_str());
    }
    const char* names[GOOD_COUNT];
    for (int g = 0; g < GOOD_COUNT; g++){
        names[g] = GoodName(g);
    }
    ImGui::SetNextItemWidth(90.0f);
    ImGui::Combo("##crate_good",&crate_edit_good,names,GOOD_COUNT);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::InputInt("##crate_amount",&crate_edit_amount);
    crate_edit_amount = std::max(1,crate_edit_amount);
    ImGui::SameLine();
    bool f_open = EconomyCrateGround(*z->world,*z,plot,z->world->grid->fine.pos[plot]);
    if (!f_open){
        ImGui::BeginDisabled();
    }
    if (ImGui::SmallButton(c ? "Add to the crate" : "Put a crate here")){
        SubmitCrate(plot,crate_edit_good,crate_edit_amount);
    }
    if (!f_open){
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("(not open ground)");
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
            GoodsMask pressing = EconomyPressing(k);
            ImVec4 colour = EconomyDry(k) ? ImVec4(1.0f,0.4f,0.35f,1.0f)
                          : pressing ? ImVec4(1.0f,0.75f,0.35f,1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_Text);
            ImGui::TextColored(colour,"%u %s",k.id,EconomyPersonName(k).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%i",EconomyAgeAt(k.born,e->tick));
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
            ImGui::Text("%s, %i - day %.2f, of %s",EconomyDeathName(d).c_str(),EconomyAgeAt(d.born,d.tick),
                        (double)d.tick / CALENDAR_DAY_TICKS,
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
        "the store at x,z (or plot) takes the goods named in `goods` (wood, water, wheat, greens, beans, provisions, hemp, rope) - "
        "a RECORDED zone command, applied before this returns; a store next to a workplace keeps taking "
        "that workplace's goods whatever it is set to. op site_priority: the construction site at x,z (or "
        "plot) goes first (`on`, default true) or back in the queue - recorded the same way; a site's "
        "`priority` shows in `sites`. op set_needs: person `person`'s health_days and/or water_days (held to their bars) "
        "set - a RECORDED debug edit (CHASM_CMD_NEEDS), the panel's villager list's. It lands before the "
        "tick's eating, so someone at home with food and water tops straight back up; health 0 kills only "
        "someone who cannot eat on that tick. op order: person `person` is sent to the plot at x,z (or "
        "`plot`) and stands there - his post, now his job (docs/defence_plan.md); op release: he leaves it "
        "and goes back to his work. Both RECORDED (CHASM_CMD_ORDER), the right click's and the card's; "
        "`refusal` says why one was refused (a child, nowhere open to stand, no way there). A worker's "
        "`post` shows where and whether he is there; a load he carried is put down as a crate. GUARDS BY HAND "
        "(step 8): op guard - person `person` made a guard of the tower `building` (or at x,z) by hand, over an "
        "automatic pick, as a right click on it does; op release_guard - he goes home and the tower takes one "
        "fewer; op guards - the tower takes `count` guards, 0 up to its places. All RECORDED (CHASM_CMD_GUARD, "
        "CHASM_CMD_GUARDS); `refusal` as for an order; a hand-picked worker shows `chosen`, a tower its `count`. op crate: a "
        "crate put down at x,z (or `plot`) holding `contents` (goods by name and amount, e.g. {\"wood\":8,"
        "\"beans\":4}) - RECORDED debug edits (CHASM_CMD_CRATE), a good at a time, into the crate already "
        "there if one is; refused off open ground (docs/crates_plan.md). `crates` lists every crate lying "
        "about: id, plot, x, z, goods, origin (dropped or found), who put it down and the day; a worker "
        "taking from one, or a builder moving it to a store (to_crate, lifting), names it as `crate`. "
        "op set_stock: the stock of building `building` (or the one at x,z; a camp's supplies) set to "
        "`contents` (goods by name and amount, e.g. {\"provisions\":6,\"wheat\":0}) - RECORDED debug edits "
        "(CHASM_CMD_STOCK), a good at a time; `pantry` true sets a camp's pantry instead. `provisions` reports PROVISIONS (docs/defence_plan.md): how many "
        "are still lacking beyond what is made, and what each builders' hut holds. STOCK KEPT (docs/goods_plan.md): op keep - "
        "the colony keeps `count` of made good `good` (provisions, rope) in its stores; op order_more - an order for `count` "
        "more of it, 0 cancelling the one running. Both RECORDED (CHASM_CMD_KEEP). `kept` reports each made good's kept "
        "number, its order's level (0 none), the level they make, and what the stores hold. SCOUTING (docs/defence_plan.md): "
        "op gather - person `person`, ordered out, first fills a scout's pack (`on`, default true), or goes straight on - "
        "RECORDED (CHASM_CMD_GATHER), `refusal` as for an order; a worker shows `gather`, the places still to go to. "
        "include_screenshot as elsewhere.",
        json{
            {"type","object"},
            {"properties",{
                {"op",{{"type","string"}}},
                {"x",{{"type","number"}}},
                {"z",{{"type","number"}}},
                {"plot",{{"type","integer"}}},
                {"goods",{{"type","array"},{"items",{{"type","string"}}}}},
                {"contents",{{"type","object"},{"description","crate, set_stock: goods by name and amount"}}},
                {"building",{{"type","integer"},{"description","set_stock, guard, guards: the building's id"}}},
                {"pantry",{{"type","boolean"},{"description","set_stock: a camp's pantry, not its supplies"}}},
                {"on",{{"type","boolean"}}},
                {"person",{{"type","integer"},{"description","set_needs, order, release, guard, release_guard, gather: the person's id"}}},
                {"layer",{{"type","integer"},{"description","order: the layer of the plot he stands on, in half storeys - 0 its ground, "
                                                           "a castle deck's (ramparts_plan.md); default its top"}}},
                {"count",{{"type","integer"},{"description","guards: how many guards the tower takes; keep, order_more: how many"}}},
                {"good",{{"type","string"},{"description","keep, order_more: the made good, provisions or rope"}}},
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
            }else if (op == "set_stock"){
                //A building's stock set (CHASM_CMD_STOCK), a good at a time: by `building` id, or the one at x,z.
                uint32_t id = args.value("building",0u);
                std::shared_ptr<const ZoneState> zs = GetZones();
                if (!id && args.contains("x") && args.contains("z") && zs && zs->world == w){
                    GridPick p = w->picker->Pick(vec2(args["x"].get<float>(),args["z"].get<float>()));
                    id = (p.f_hit && p.plot < (int)zs->building.size()) ? zs->building[p.plot] : 0;
                }
                if (!id){
                    return json{{"error","set_stock: no building (give `building` or x,z on one)"}};
                }
                json contents = args.value("contents",json::object());
                for (auto it = contents.begin(); it != contents.end(); ++it){
                    int good = GoodByName(it.key());
                    if (good < 0){
                        return json{{"error","no such good: " + it.key()}};
                    }
                    SimCommand cmd;
                    cmd.type = CHASM_CMD_STOCK;
                    cmd.flags = SIM_CMD_FLAG_RECORD;
                    cmd.subtype = id;
                    cmd.value[0] = (float)good;
                    cmd.value[1] = (float)it.value().get<int>();
                    cmd.value[2] = args.value("pantry",false) ? 1.0f : 0.0f;
                    SubmitCommandAndWait(cmd);
                }
                //A building placed on this very tick is not the economy's yet (its next tick takes it in).
                std::shared_ptr<const EconomyState> es = GetEconomy();
                result["ran"] = es && id < es->stock.size();
                if (!result["ran"].get<bool>()){
                    result["refusal"] = "the economy has no such building yet - step a tick after placing it";
                }
                result["building"] = id;
                result["stock"] = es ? StockJson(*es,id) : json::object();
            }else if (op == "order" || op == "release"){
                int plot = -1;
                if (op == "order"){
                    plot = args.value("plot",-1);
                    if (plot < 0 && args.contains("x") && args.contains("z")){
                        GridPick p = w->picker->Pick(vec2(args["x"].get<float>(),args["z"].get<float>()));
                        plot = p.f_hit ? p.plot : -1;
                    }
                    if (plot < 0){
                        return json{{"error","order: no plot at x,z (or give `plot`)"}};
                    }
                }
                SimCommand cmd;
                cmd.type = CHASM_CMD_ORDER;
                cmd.flags = SIM_CMD_FLAG_RECORD;
                cmd.subtype = (uint32_t)args.value("person",0);
                cmd.value[0] = (float)plot;
                cmd.value[1] = (float)(args.value("layer",-1) + 1);    //ramparts_plan.md: its top by default
                uint32_t before = order_commands_done.load();
                uint32_t refused = order_refusals.load();
                SubmitCommandAndWait(cmd);
                result["ran"] = order_commands_done.load() != before;
                std::string refusal;
                if (order_refusals.load() != refused){
                    std::lock_guard<std::mutex> lock(grid_mutex);
                    refusal = order_last_refusal;
                }
                result["refusal"] = refusal;
                result["plot"] = plot;
            }else if (op == "guard" || op == "release_guard" || op == "guards"){
                //Guards by hand (defence_plan.md, step 8): the tower by `building` id or at x,z.
                uint32_t tower = (uint32_t)args.value("building",0);
                if (op != "release_guard" && !tower && args.contains("x") && args.contains("z")){
                    GridPick p = w->picker->Pick(vec2(args["x"].get<float>(),args["z"].get<float>()));
                    std::shared_ptr<const ZoneState> zs = GetZones();
                    tower = (p.f_hit && zs && p.plot < (int)zs->building.size()) ? zs->building[p.plot] : 0;
                }
                if (op != "release_guard" && !tower){
                    return json{{"error",op + ": no tower - give `building`, or x,z on one"}};
                }
                SimCommand cmd;
                cmd.flags = SIM_CMD_FLAG_RECORD;
                if (op == "guards"){
                    cmd.type = CHASM_CMD_GUARDS;
                    cmd.subtype = tower;
                    cmd.value[0] = (float)args.value("count",ECONOMY_TOWER_GUARDS_PLACED);
                    uint32_t before = guards_commands_done.load();
                    SubmitCommandAndWait(cmd);
                    result["ran"] = guards_commands_done.load() != before;
                }else{
                    cmd.type = CHASM_CMD_GUARD;
                    cmd.subtype = (uint32_t)args.value("person",0);
                    cmd.value[0] = (float)((op == "guard") ? tower : 0);
                    uint32_t before = order_commands_done.load();
                    uint32_t refused = order_refusals.load();
                    SubmitCommandAndWait(cmd);
                    result["ran"] = order_commands_done.load() != before;
                    std::string refusal;
                    if (order_refusals.load() != refused){
                        std::lock_guard<std::mutex> lock(grid_mutex);
                        refusal = order_last_refusal;
                    }
                    result["refusal"] = refusal;
                }
                result["building"] = tower;
            }else if (op == "gather"){
                //A scout's pack gathered first (defence_plan.md, "Scouting").
                SimCommand cmd;
                cmd.type = CHASM_CMD_GATHER;
                cmd.flags = SIM_CMD_FLAG_RECORD;
                cmd.subtype = (uint32_t)args.value("person",0);
                cmd.value[0] = args.value("on",true) ? 1.0f : 0.0f;
                uint32_t before = order_commands_done.load();
                uint32_t refused = order_refusals.load();
                SubmitCommandAndWait(cmd);
                result["ran"] = order_commands_done.load() != before;
                std::string refusal;
                if (order_refusals.load() != refused){
                    std::lock_guard<std::mutex> lock(grid_mutex);
                    refusal = order_last_refusal;
                }
                result["refusal"] = refusal;
            }else if (op == "keep" || op == "order_more"){
                //STOCK KEPT (goods_plan.md): a made good's kept number, or an order for more of it.
                int good = GoodByName(args.value("good",std::string()));
                if (good < 0 || !(GOODS_MADE & GOOD_BIT(good))){
                    return json{{"error",op + ": give `good`, a made good - provisions or rope"}};
                }
                SimCommand cmd;
                cmd.type = CHASM_CMD_KEEP;
                cmd.flags = SIM_CMD_FLAG_RECORD;
                cmd.subtype = (uint32_t)good;
                cmd.value[0] = (float)std::max(0,args.value("count",0));
                cmd.value[1] = (op == "order_more") ? 1.0f : 0.0f;
                uint32_t before = keep_commands_done.load();
                SubmitCommandAndWait(cmd);
                result["ran"] = keep_commands_done.load() != before;
            }else if (op == "crate"){
                int plot = args.value("plot",-1);
                if (plot < 0 && args.contains("x") && args.contains("z")){
                    GridPick p = w->picker->Pick(vec2(args["x"].get<float>(),args["z"].get<float>()));
                    plot = p.f_hit ? p.plot : -1;
                }
                if (plot < 0){
                    return json{{"error","crate: no plot at x,z (or give `plot`)"}};
                }
                if (!args.contains("contents") || !args["contents"].is_object() || args["contents"].empty()){
                    return json{{"error","crate: give `contents`, goods by name and amount"}};
                }
                //A good a command, in the goods' own order, so a replay puts them in the same way; the first refusal stops it.
                std::array<int,GOOD_COUNT> amounts{};
                for (auto it = args["contents"].begin(); it != args["contents"].end(); ++it){
                    int good = GoodByName(it.key());
                    if (good < 0 || !it.value().is_number()){
                        return json{{"error","crate: no such good, or no amount: " + it.key()}};
                    }
                    amounts[good] = (int)std::lround(it.value().get<double>());
                }
                std::string refusal;
                for (int g = 0; g < GOOD_COUNT && refusal.empty(); g++){
                    if (amounts[g] <= 0){
                        continue;
                    }
                    SimCommand cmd;
                    cmd.type = CHASM_CMD_CRATE;
                    cmd.flags = SIM_CMD_FLAG_RECORD;
                    cmd.subtype = (uint32_t)plot;
                    cmd.value[0] = (float)g;
                    cmd.value[1] = (float)amounts[g];
                    uint32_t refused = crate_refusals.load();
                    SubmitCommandAndWait(cmd);
                    if (crate_refusals.load() != refused){
                        std::lock_guard<std::mutex> lock(grid_mutex);
                        refusal = crate_last_refusal;
                    }
                }
                result["refusal"] = refusal;
                result["plot"] = plot;
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
                workers.push_back(WorkerJson(k,e.get()));
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
            //PROVISIONS (defence_plan.md): still lacking beyond what is made - worked out by the rules, between
            //ticks - and what each builders' hut holds for making them.
            int lacking = 0;
            main_scene->AtTickBoundary([&](){
                lacking = economy.ProvisionsLacking(zones.State());
            });
            json huts = json::array();
            for (size_t id = 1; id < z->buildings.size(); id++){
                if (z->buildings[id].size > 0 && z->buildings[id].kind == ZONE_KIND_BUILDERS){
                    huts.push_back(json{{"id",id},{"holds",StockJson(*e,(uint32_t)id)}});
                }
            }
            result["provisions"] = json{{"lacking",lacking},{"huts",huts}};
            //STOCK KEPT (goods_plan.md): each made good's number, its order, the level of the two, and the stores' count.
            json kept = json::object();
            for (int g = 0; g < GOOD_COUNT; g++){
                if (GOODS_MADE & GOOD_BIT(g)){
                    kept[GoodName(g)] = json{{"kept",e->kept[g]},{"ordered",e->ordered[g]},{"level",EconomyKeptLevel(*e,g)},
                                             {"stored",totals[g]}};
                }
            }
            result["kept"] = kept;
            //GUARD TOWERS (defence_plan.md): each one's places - its guard count once it stands (step 8) - its
            //junction's, its guards and its stock - theirs to live on; once it stands whole, what it lacks of full and
            //what is on its way to it (the caretakers', step 7).
            json towers = json::array();
            for (size_t id = 1; id < z->buildings.size(); id++){
                if (z->buildings[id].size > 0 && z->buildings[id].kind == ZONE_KIND_TOWER){
                    json guards = json::array();
                    for (const EconomyWorker& k : e->workers){
                        if (k.job == WORKER_JOB_GUARD && k.building == id){
                            guards.push_back(k.id);
                        }
                    }
                    int plot = -1;
                    for (size_t v = 0; v < z->building.size() && plot < 0; v++){
                        plot = (z->building[v] == id) ? (int)v : -1;
                    }
                    json t{{"id",id},{"places",(id < e->places.size()) ? e->places[id] : 0},
                           {"count",(id < e->guards.size()) ? e->guards[id] : -1},
                           {"junction_places",(plot >= 0) ? ZoneTowerGuards(*e->world,plot) : 0},
                           {"guards",guards},{"stock",StockJson(*e,(uint32_t)id)}};
                    if (EconomyTowerUp(*z,plot)){
                        std::array<int,GOOD_COUNT> lack = EconomyTowerShort(*e,*e->world,(uint32_t)id,plot);
                        std::array<int,GOOD_COUNT> coming = EconomyComing(*e,(uint32_t)id);
                        t["short"] = json{{"water",lack[GOOD_WATER]},{"provisions",lack[GOOD_PROVISIONS]}};
                        t["coming"] = json{{"water",coming[GOOD_WATER]},{"provisions",coming[GOOD_PROVISIONS]}};
                    }
                    towers.push_back(t);
                }
            }
            result["towers"] = towers;
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
            int playing = 0;
            int posted = 0;
            int babies = 0;
            int expecting = 0;
            int caring = 0;
            for (const EconomyWorker& k : e->workers){
                housed += k.house ? 1 : 0;
                employed += (k.job >= 0) ? 1 : 0;
                playing += (k.job == WORKER_JOB_PLAYING) ? 1 : 0;
                posted += (k.post >= 0) ? 1 : 0;
                babies += (k.job == WORKER_JOB_BABY) ? 1 : 0;
                expecting += k.due ? 1 : 0;
                caring += (k.job == WORKER_JOB_CARING) ? 1 : 0;
            }
            //Posted (defence_plan.md): standing at a post; a job they had still counts in `working`, its place kept.
            result["people"] = json{{"total",e->workers.size()},{"housed",housed},{"working",employed},
                                    {"children",playing},{"babies",babies},{"expecting",expecting},{"caring",caring},
                                    {"posted",posted},{"camp_plot",e->camp_plot}};
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
                dead.push_back(json{{"id",d.id},{"name",EconomyDeathName(d)},{"age",EconomyAgeAt(d.born,d.tick)},
                                    {"mother",d.mother},{"father",d.father},
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
            //The crates lying about (crates_plan.md), in id order.
            json crates = json::array();
            for (const EconomyCrate& c : e->crates){
                std::string by;
                for (const EconomyWorker& k : e->workers){
                    if (c.by && k.id == c.by){
                        by = EconomyPersonName(k);
                    }
                }
                crates.push_back(json{{"id",c.id},{"plot",c.plot},{"x",c.pos.x},{"z",c.pos.y},{"goods",StockJson(c.stock)},
                                      {"origin",EconomyCrateOriginName(c.origin)},{"by",c.by},{"by_name",by},
                                      {"day",(double)c.tick / CALENDAR_DAY_TICKS}});
            }
            result["crates"] = crates;
            return MaybeAttachScreenshot(result,args.value("include_screenshot",false),args.value("include_ui",true));
        });
}
#endif
