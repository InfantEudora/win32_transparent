#include "ApplicationChasm.h"
#include <map>
#ifdef USE_IMGUI
#include "imgui.h"
#endif
#ifdef USE_MCP
#include "MCPServer.h"
#endif
#include "Debug.h"
#include <climits>
#include <ctype.h>
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
    /*
        EVENTS (Economy.h): a storey raised, and a building standing whole - every storey planned, a field's every cell -
        for the FIRST time. A house given another storey later is a storey raised again, not a second building, so the
        journal is asked whether it has this one already: ids are never reused.
    */
    auto finished = [&](uint32_t id, int plot){
        std::shared_ptr<const ChasmWorld> w = zones.GetWorld();
        const ZoneState& z = zones.State();
        if (!w || !id || id >= z.buildings.size()){
            return;
        }
        ZoneBuildingInfo info = ZoneBuildingFigures(*w,z,id);
        if (info.site_storeys > 0 || info.site_cells > 0){
            return;
        }
        for (const EconomyEvent& ev : economy.State().history.journal){
            if (ev.kind == EVENT_BUILDING_FINISHED && ev.building == id){
                return;
            }
        }
        EconomyEvent ev;
        ev.kind = EVENT_BUILDING_FINISHED;
        ev.building = id;
        ev.plot = plot;
        ev.amount = z.buildings[id].kind;
        vec2 at;
        if (plot < 0 && ChasmBuildingPoint(*w,z,id,at)){
            ev.pos = at;
        }
        economy.Event(ev,calendar_tick);
    };
    for (int plot : economy.Raises()){
        if (zones.Apply(ZONE_OP_BUILD_RAISE,(uint32_t)plot)){
            f_raised = true;
            const ZoneState& z = zones.State();
            EconomyEvent ev;
            ev.kind = EVENT_STOREY_RAISED;
            ev.building = z.building[plot];
            ev.plot = plot;
            ev.amount = (ev.building < z.buildings.size()) ? z.buildings[ev.building].kind : 0;
            economy.Event(ev,calendar_tick);
            finished(ev.building,plot);
        }
    }
    //Gardens and lots the builders finished: their walls go up. Roads laid (line_works_plan.md).
    for (int plot : economy.GroundRaises()){
        f_raised = zones.Apply(ZONE_OP_GROUND_RAISE,(uint32_t)plot) || f_raised;
    }
    //A field site built: its cells, all at once (fields_plan.md).
    for (int cell : economy.FieldRaises()){
        if (zones.Apply(ZONE_OP_FIELD_RAISE,(uint32_t)cell)){
            f_raised = true;
            const ZoneState& z = zones.State();
            finished((cell >= 0 && cell < (int)z.field.size()) ? z.field[cell] : 0,-1);
        }
    }
    //A field that waited its day for a crop: it starts with AUTO (issues_plan.md).
    for (int cell : economy.CropDefaults()){
        f_raised = zones.Apply(ZONE_OP_FIELD_CROP,(uint32_t)cell,ZONE_CROP_AUTO) || f_raised;
    }
    //A tent packed to take on the road (settlements_plan.md): its plot out of its camp.
    for (int plot : economy.TentsPacked()){
        f_raised = zones.Apply(ZONE_OP_BUILD_ERASE,(uint32_t)plot) || f_raised;
    }
    if (f_raised){
        PublishZones();
    }
    CheckStats();       //a day closed: the history against the state, in a debug build's log (ApplicationChasmStats.cpp)
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
    //What the mines have dug (stone_plan.md step 5) - only once any has, so a colony without a mine hashes as before.
    for (size_t d = 0; d < e.dug.size(); d++){
        if (e.dug[d] > 0){
            h.Add(((uint64_t)d << 32) | (uint32_t)e.dug[d] | 0x4D00000000000000ull);
        }
    }
    //The plants' fruit and their growing back (foraging_plan.md), by prop like the felled trees.
    h.Bytes(e.fruit.data(),e.fruit.size());
    h.Bytes(e.regrow.data(),e.regrow.size());
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
        //Taking a tent, or carrying one (settlements_plan.md) - only while he is, likewise.
        if (k.tent != ECONOMY_TENT_NONE){
            h.Add(((uint64_t)k.id << 32) | 0x7E4700u | (uint32_t)k.tent);
        }
        //Picking or drawing by hand at his post (foraging_plan.md) - only while he is, likewise.
        if (k.forage != FORAGE_NONE){
            h.Add(((uint64_t)k.id << 32) | 0xF0A600u | (uint32_t)k.forage);
        }
        //Going along with a grown-up, or carried by his mother (settlements_plan.md) - only while he is, likewise.
        if (k.with){
            h.Add(((uint64_t)k.id << 32) ^ 0x3171000000000000ull ^ (uint64_t)k.with);
        }
        //His route's layers (ramparts_plan.md) - only once he leaves the ground, so a colony that never does hashes as before.
        if (!k.route_layer.empty()){
            h.Add(((uint64_t)k.id << 32) | 0x1A7Eu);
            h.Bytes(k.route_layer.data(),k.route_layer.size());
        }
        //Where his body stands off his route (SPACING, zombies_plan.md) - only while it does, likewise.
        if (k.off.x != 0.0f || k.off.y != 0.0f){
            h.Add(((uint64_t)k.id << 32) | 0x0FF5u);
            h.Bytes(&k.off,sizeof(k.off));
        }
        //A meal under way (MEALS) and a fight's next blow (step 2) - only while there is one, likewise.
        if (EconomyMealOn(k)){
            int32_t meal[4] = {(int32_t)k.id,k.meal_good,k.meal_ticks,k.meal_worth};
            h.Bytes(meal,sizeof(meal));
        }
        if (k.swing > 0){
            h.Add(((uint64_t)k.id << 32) | 0x5000000u | (uint32_t)k.swing);
        }
        //His turn of the bow at a zombie (ARROWS, step 3) - only while he shoots, likewise.
        if (k.draw > 0){
            int32_t shot[3] = {(int32_t)k.id,k.draw,(int32_t)k.aim};
            h.Bytes(shot,sizeof(shot));
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
        uint8_t pick_up = c.f_pick_up ? 1 : 0;
        h.Bytes(&pick_up,sizeof(pick_up));
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
    //The zombies (zombies_plan.md), each whole on its route as a person is - only once there have been any, so a colony
    //that never had one hashes as before.
    if (!e.zombies.empty() || e.next_zombie > 1){
        n = (uint32_t)e.zombies.size();
        h.Add(((uint64_t)n << 32) | 0x2081Eu);
        h.Bytes(&e.next_zombie,sizeof(e.next_zombie));
        for (const EconomyZombie& m : e.zombies){
            int32_t head[6] = {(int32_t)m.id,(int32_t)m.target,(int32_t)m.f_lost,m.seg,m.health,m.bite};
            h.Bytes(head,sizeof(head));
            h.Bytes(&m.along,sizeof(m.along));
            h.Bytes(&m.pos,sizeof(m.pos));
            h.Bytes(&m.off,sizeof(m.off));
            uint32_t len = (uint32_t)m.route.size();
            h.Bytes(&len,sizeof(len));
            h.Bytes(m.route.data(),m.route.size() * sizeof(vec2));
            h.Bytes(m.speed.data(),m.speed.size() * sizeof(float));
        }
        //Their arrows (step 3): who at what, a hit or not, the flight, where from and to - only once there are any.
        if (!e.arrows.empty()){
            n = (uint32_t)e.arrows.size();
            h.Add(((uint64_t)n << 32) | 0xA770u);
            for (const EconomyArrow& a : e.arrows){
                int32_t head[6] = {(int32_t)a.by,(int32_t)a.zombie,(int32_t)a.f_hit,a.ticks,a.flown,a.lie};
                h.Bytes(head,sizeof(head));
                float where[8] = {a.from.x,a.from.y,a.from.z,a.to.x,a.to.y,a.to.z,a.at.x,a.at.y};
                h.Bytes(where,sizeof(where));
            }
        }
    }
    /*
        THE HISTORY (statistics_plan.md), a part of its own so a replay that parts there says so. Its TAIL: the journal
        and the ledger are only ever added to, so with the state traced every tick an entry that differs differs on the
        tick it is added, as one of the last. Hashing all of it every tick would grow with the game. Where it began,
        its lengths, the last rows and lines, and today's row so far.
    */
    const EconomyHistory& hist = e.history;
    h.Begin("history");
    auto day = [&](const EconomyDay& d){
        int32_t counts[4] = {d.day,d.felled,d.broken,d.storeys};
        h.Bytes(counts,sizeof(counts));
        h.Bytes(d.people.data(),sizeof(int) * ECONOMY_STAGES);
        h.Bytes(d.held.data(),sizeof(int) * GOOD_COUNT);
    };
    uint64_t head[5] = {hist.from,hist.journal.size(),hist.days.size(),(uint64_t)hist.felled_from,(uint64_t)hist.broken_from};
    h.Bytes(head,sizeof(head));
    day(hist.start);
    day(hist.today);
    const size_t tail = 16;
    for (size_t i = (hist.days.size() > tail) ? hist.days.size() - tail : 0; i < hist.days.size(); i++){
        day(hist.days[i]);
    }
    for (size_t i = (hist.journal.size() > tail) ? hist.journal.size() - tail : 0; i < hist.journal.size(); i++){
        const EconomyEvent& ev = hist.journal[i];
        int32_t fields[6] = {ev.kind,(int32_t)ev.person,(int32_t)ev.building,ev.plot,(int32_t)ev.what,ev.amount};
        h.Bytes(&ev.tick,sizeof(ev.tick));
        h.Bytes(fields,sizeof(fields));
        h.Bytes(&ev.pos,sizeof(ev.pos));
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
      - someone ordered out told to TAKE A TENT first, or not, and one carried PITCHED where he stands
        (settlements_plan.md) - his card's chips; refused as an order is;
      - someone sent to PICK a ripe plant or DRAW water by hand (foraging_plan.md) - the right click's menu; refused
        as an order is;
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
            //value[2]: one of a group's (selection_plan.md, step 3), spread round the plot - 0 in recordings from before.
            //value[3]: a PLAY command, held to the clouds as a zone command is - a tool's, and recordings from before, not.
            const Exploration* clouds = (cmd.value[3] != 0.0f) ? &exploration : NULL;
            if (economy.Order(cmd.subtype,(int)cmd.value[0],zones.State(),walkers,&why,(int)cmd.value[1] - 1,cmd.value[2] != 0.0f,
                              clouds)){
                PublishEconomy();
                //Taken, so he answers (ApplicationChasmSound.cpp, Say) - a refused order is the refusal's sound alone.
                Say(cmd.subtype,((int)cmd.value[0] >= 0) ? "sent" : "released",CHASM_SAY_ORDER);
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
                Say(cmd.subtype,((uint32_t)cmd.value[0] != 0) ? "sent" : "released",CHASM_SAY_ORDER);
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
                Say(cmd.subtype,(cmd.value[0] != 0.0f) ? "gather" : "straight_on",CHASM_SAY_ORDER);
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
    //A tent taken first (settlements_plan.md, "Founding: taking a tent"): his card's chip - refused as an order is.
    main_scene->RegisterCommandHandler(CHASM_CMD_TENT,
        [this](const SimCommand& cmd) -> objectid_t {
            std::string why;
            if (economy.TakeTent(cmd.subtype,cmd.value[0] != 0.0f,zones.State(),walkers,&why)){
                PublishEconomy();
                Say(cmd.subtype,(cmd.value[0] != 0.0f) ? "take_tent" : "straight_on",CHASM_SAY_ORDER);
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
    /*
        ...and pitched where he stands: the zones and the economy both, here between ticks - the tent stands and he
        lives in it before the next tick moves anyone. Refused as an order is, with the rules' reason.
    */
    main_scene->RegisterCommandHandler(CHASM_CMD_PITCH,
        [this](const SimCommand& cmd) -> objectid_t {
            EnsureZonesWorld();
            std::string why;
            int plot = -1;
            uint32_t camp = 0;
            if (economy.PitchSpot(cmd.subtype,zones.State(),&plot,&camp,&why)){
                if (zones.PitchTent(plot,camp)){
                    economy.Pitched(cmd.subtype,plot,zones.State(),walkers);
                    //A camp of its own - outside every settlement - is a settlement FOUNDED (an event, Economy.h), on
                    //the tick this command comes before.
                    if (!camp){
                        EconomyEvent ev;
                        ev.kind = EVENT_SETTLED;
                        ev.person = cmd.subtype;
                        ev.building = zones.State().building[plot];
                        ev.plot = plot;
                        economy.Event(ev,calendar_tick);
                    }
                    PublishZones();
                    PublishEconomy();
                    Say(cmd.subtype,"pitch",CHASM_SAY_ORDER);
                }else{
                    why = zones.last_refusal.empty() ? std::string("no room for a tent here") : zones.last_refusal;
                }
            }
            if (!why.empty()){
                {
                    std::lock_guard<std::mutex> lock(grid_mutex);
                    order_last_refusal = why;
                }
                order_refusals++;
            }
            order_commands_done++;
            return OBJECTID_INVALID;
        });
    //Settled without a tent (settlements_plan.md, the user's): sent into another settlement and there - refused as an order is.
    main_scene->RegisterCommandHandler(CHASM_CMD_SETTLE,
        [this](const SimCommand& cmd) -> objectid_t {
            std::string why;
            if (economy.SettleHere(cmd.subtype,zones.State(),walkers,&why)){
                PublishEconomy();
                Say(cmd.subtype,"settle",CHASM_SAY_ORDER);
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
    //Picking a plant or drawing water by hand (foraging_plan.md): the right click's menu - refused as an order is, and a
    //PLAY one (value[3]) under the clouds.
    main_scene->RegisterCommandHandler(CHASM_CMD_FORAGE,
        [this](const SimCommand& cmd) -> objectid_t {
            std::string why;
            const Exploration* clouds = (cmd.value[3] != 0.0f) ? &exploration : NULL;
            if (economy.Forage(cmd.subtype,(int)cmd.value[0],(int)cmd.value[1],zones.State(),walkers,&why,cmd.value[2] != 0.0f,
                               clouds)){
                PublishEconomy();
                Say(cmd.subtype,"forage",CHASM_SAY_ORDER,(int)cmd.value[1]);
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
    //PICK UP (stone_plan.md): a crate's card's chip, the player's - refused for a crate no longer there, and a PLAY one
    //(value[1]) for a crate under the clouds, which play does not even let be selected (PlayPickUnder).
    main_scene->RegisterCommandHandler(CHASM_CMD_PICK_UP,
        [this](const SimCommand& cmd) -> objectid_t {
            std::string why;
            bool f_hidden = false;
            for (const EconomyCrate& c : economy.State().crates){
                f_hidden = f_hidden || (cmd.value[1] != 0.0f && c.id == cmd.subtype && !exploration.Explored(c.pos));
            }
            if (f_hidden){
                why = "under the clouds";
            }
            if (!f_hidden && economy.SetPickUp(cmd.subtype,cmd.value[0] != 0.0f,&why)){
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
    //ZOMBIES dropped, or all taken off (zombies_plan.md): a debug edit, like the crate - Z, the panel, chasm_zombie.
    main_scene->RegisterCommandHandler(CHASM_CMD_ZOMBIE,
        [this](const SimCommand& cmd) -> objectid_t {
            std::string why;
            if (economy.SpawnZombies((int)cmd.subtype,(int)cmd.value[0],zones.State(),&why)){
                PublishEconomy();
            }else{
                {
                    std::lock_guard<std::mutex> lock(grid_mutex);
                    zombie_last_refusal = why;
                }
                zombie_refusals++;
            }
            zombie_commands_done++;
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

void ApplicationChasm::SubmitOrder(uint32_t person, int plot, int layer, bool f_spread){
    SimCommand cmd;
    cmd.type = CHASM_CMD_ORDER;
    cmd.flags = SIM_CMD_FLAG_RECORD;
    cmd.subtype = person;
    cmd.value[0] = (float)plot;     //a plot index, well inside a float's exact integers
    cmd.value[1] = (float)(layer + 1);
    cmd.value[2] = f_spread ? 1.0f : 0.0f;
    cmd.value[3] = PlayMode() ? 1.0f : 0.0f;   //read by the view and carried in the command, as a zone command's
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

void ApplicationChasm::SubmitPickUp(uint32_t crate, bool f_on){
    SimCommand cmd;
    cmd.type = CHASM_CMD_PICK_UP;
    cmd.flags = SIM_CMD_FLAG_RECORD;
    cmd.subtype = crate;
    cmd.value[0] = f_on ? 1.0f : 0.0f;
    cmd.value[1] = PlayMode() ? 1.0f : 0.0f;
    main_scene->SubmitCommand(cmd);
}

void ApplicationChasm::SubmitTent(uint32_t person, bool f_on){
    SimCommand cmd;
    cmd.type = CHASM_CMD_TENT;
    cmd.flags = SIM_CMD_FLAG_RECORD;
    cmd.subtype = person;
    cmd.value[0] = f_on ? 1.0f : 0.0f;
    main_scene->SubmitCommand(cmd);
}

void ApplicationChasm::SubmitPitch(uint32_t person){
    SimCommand cmd;
    cmd.type = CHASM_CMD_PITCH;
    cmd.flags = SIM_CMD_FLAG_RECORD;
    cmd.subtype = person;
    main_scene->SubmitCommand(cmd);
}

void ApplicationChasm::SubmitSettle(uint32_t person){
    SimCommand cmd;
    cmd.type = CHASM_CMD_SETTLE;
    cmd.flags = SIM_CMD_FLAG_RECORD;
    cmd.subtype = person;
    main_scene->SubmitCommand(cmd);
}

void ApplicationChasm::SubmitForage(uint32_t person, int plot, int what, bool f_spread){
    SimCommand cmd;
    cmd.type = CHASM_CMD_FORAGE;
    cmd.flags = SIM_CMD_FLAG_RECORD;
    cmd.subtype = person;
    cmd.value[0] = (float)plot;     //a plot index, well inside a float's exact integers
    cmd.value[1] = (float)what;
    cmd.value[2] = f_spread ? 1.0f : 0.0f;
    cmd.value[3] = PlayMode() ? 1.0f : 0.0f;
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
    //Where his body stands off his route (SPACING, zombies_plan.md): x, z above are the route's.
    if (k.off.x != 0.0f || k.off.y != 0.0f){
        j["off"] = json::array({std::round(k.off.x * 1000.0f) / 1000.0f,std::round(k.off.y * 1000.0f) / 1000.0f});
    }
    //A meal under way (MEALS) - what, the ticks to go, what it has still to give - and a fight (zombies_plan.md, step 2).
    if (EconomyMealOn(k)){
        j["meal"] = json{{"good",GoodName(k.meal_good)},{"ticks_left",k.meal_ticks},{"worth_left",k.meal_worth}};
    }
    if (k.draw > 0){
        j["shooting"] = json{{"zombie",k.aim},{"turn_ticks",k.draw}};     //ARROWS (step 3): at which, how far into his turn
    }
    if (k.swing > 0){
        j["fighting"] = true;
        j["next_blow_ticks"] = k.swing;
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
    //How far through a timed task (EconomyTaskProgress) - his card's bar - with the ticks it runs for.
    float progress = EconomyTaskProgress(k);
    if (progress >= 0.0f){
        j["task_progress"] = std::round(progress * 1000.0f) / 1000.0f;
        j["task_ticks"] = k.timer_full;
    }
    if (k.carry > 0){
        j["carrying"] = json{{GoodName(k.carry_good),k.carry}};
    }else if ((k.state == WORKER_TO_FETCH || k.state == WORKER_FETCHING) && k.carry_good >= 0){
        j["fetching"] = GoodName(k.carry_good);     //for a site: the good he is on his way for
    }
    if (k.prop >= 0){
        j["tree"] = k.prop;
        //Where it stands, to set beside how far he walks to it (walk_left).
        if (e && e->world && e->world->forest && k.prop < (int)e->world->forest->props.size()){
            const vec3& t = e->world->forest->props[k.prop].pos;
            j["tree_at"] = json{{"x",t.x},{"z",t.z}};
        }
    }
    //What is left of his route to walk, in world units.
    if (k.seg + 1 < (int)k.route.size()){
        float left = (k.route[k.seg + 1] - k.pos).length();
        for (size_t i = (size_t)k.seg + 1; i + 1 < k.route.size(); i++){
            left += (k.route[i + 1] - k.route[i]).length();
        }
        j["walk_left"] = left;
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
    //ICE (seasons_plan.md): his only way home is over the ice, thawing in so many days (Economy::IceHome).
    for (size_t i = 0; e && i < e->ice_home.size(); i++){
        if (e->ice_home[i].first == k.id){
            j["ice_home_days"] = e->ice_home[i].second;
        }
    }
    if (k.gather > 0){
        j["gather"] = k.gather;     //places still to go to for a scout's pack (defence_plan.md, "Scouting")
    }
    if (k.tent != ECONOMY_TENT_NONE){
        j["tent"] = (k.tent == ECONOMY_TENT_CARRIED) ? "carried" : "to_take";     //settlements_plan.md
    }
    if (k.forage != FORAGE_NONE){
        j["forage"] = (k.forage == FORAGE_FRUIT) ? "fruit" : "water";      //by hand at his post (foraging_plan.md)
    }
    if (k.with){
        j["with"] = k.with;     //the grown-up he goes along with, or the mother carrying him (settlements_plan.md)
    }
    if (e){
        j["settlement"] = EconomyPersonSettlement(*e,k);    //his home's (settlements_plan.md), 0 none
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
    json j{{"stock",StockJson(*e,id)},{"settlement",EconomySettlementOf(*e,id)}};
    //A camp's own water carrier draws here, beside the water (settlements_plan.md).
    if (id < e->draw_plot.size() && e->draw_plot[id] >= 0 && e->world){
        const vec2& p = e->world->grid->fine.pos[e->draw_plot[id]];
        j["draws_at"] = json{{"plot",e->draw_plot[id]},{"x",p.x},{"z",p.y}};
    }
    //...and has a picker, with plants bearing in its land (foraging_plan.md).
    if (id < e->picker_places.size() && e->picker_places[id] > 0){
        j["picker_places"] = e->picker_places[id];
    }
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
        j["burns"] = (id < e->burn_halves.size()) ? e->burn_halves[id] * 0.5f : 0.0f;     //in halves (HEAT RISES)
        j["heat"] = heats[std::min(2,std::max(0,heat))];
    }
    //APPEAL AND CROWDING (building_size_plan.md): a couple's chance of a child a year here, its appeal, and how crowded.
    if (z->buildings[id].kind == ZONE_KIND_HOUSE){
        int from_storeys = 0;
        int from_garden = 0;
        EconomyHouseAppeal(*z,id,&from_storeys,&from_garden);
        j["birth_chance"] = EconomyBirthChance(*e,*z,id);
        j["appeal"] = json{{"storeys",from_storeys},{"garden",from_garden}};
        j["crowded_by"] = EconomyCrowdedBy(*e,*z,id);
    }
    //A woodcutter's woodpile: the lot plot it is on, or -1 with no lot standing beside the hut; its room, indoors and in
    //all, and what a log makes there (woodcutting_plan.md) - the camp's own woodcutter's too.
    if (z->buildings[id].kind == ZONE_KIND_WOODCUTTER || z->buildings[id].kind == ZONE_KIND_CAMP){
        if (z->buildings[id].kind == ZONE_KIND_WOODCUTTER){
            j["pile_plot"] = (id < e->pile_plot.size()) ? e->pile_plot[id] : -1;
            j["wood_indoors"] = (id < e->hut_room.size()) ? e->hut_room[id] : 0;
        }
        j["wood_room"] = (id < e->pile_room.size()) ? e->pile_room[id] : 0;
        j["log_wood"] = (id < e->log_wood.size()) ? e->log_wood[id] : 0;
    }
    //A collector's hold, by its standing floor.
    if (z->buildings[id].kind == ZONE_KIND_WATER){
        j["water_room"] = (id < e->water_room.size()) ? e->water_room[id] : 0;
    }
    //A workplace the player stopped (stopping_work_plan.md), and its places - 0 while stopped.
    if (ZoneKindStoppable(z->buildings[id].kind)){
        j["stopped"] = z->buildings[id].f_stopped != 0;
        j["places"] = (id < e->places.size()) ? e->places[id] : 0;
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
        "fraction, from 0 at its first dawn - and of what), the DISCOVERIES (docs/demo_plan.md step 6: river, chasm, "
        "boulder, floatstone, builders_hut, winch, balcony - each made or not, its tick and person, the build tools it "
        "brings) and tools_locked (what play mode's build bar still hides; none in debug mode). op store_allow: "
        "the store at x,z (or plot) takes the goods named in `goods` (wood, water, wheat, greens, beans, provisions, hemp, rope, fruit, stone, floatstone) - "
        "a RECORDED zone command, applied before this returns; a store next to a workplace keeps taking "
        "that workplace's goods whatever it is set to. op site_priority: the construction site at x,z (or "
        "plot) goes first (`on`, default true) or back in the queue - recorded the same way; a site's "
        "`priority` shows in `sites`. op set_needs: person `person`'s health_days and/or water_days (held to their bars) "
        "set - a RECORDED debug edit (CHASM_CMD_NEEDS), the panel's villager list's. It lands before the "
        "tick's eating, so someone at home with food and water tops straight back up; health 0 kills only "
        "someone who cannot eat on that tick. op order: person `person` is sent to the plot at x,z (or "
        "`plot`) and stands there - his post, now his job (docs/defence_plan.md); `spread` true: as one of a group, "
        "the plot while it has room, else the nearest round it (give a group's nearest first, as the right click does); "
        "op release: he leaves it "
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
        "about: id, plot, x, z, goods, origin (dropped, found, camp, rubble, broken - docs/stone_plan.md - or site: what a "
        "site taken off was brought), who put it down, "
        "the day, pick_up and too_far (a builder turned it down: past ECONOMY_PICK_UP_WATER of a water bar's walk); a worker taking from one, or a builder moving it to a store (to_crate, lifting), names it "
        "as `crate`. op pick_up: crate `crate` (an id) fetched first by the builders, from anywhere (`on`, default true), "
        "or back in the queue - RECORDED (CHASM_CMD_PICK_UP), `refusal` for no such crate. op rocks - the rocks and "
        "boulders within `reach` (default 20) of x,z: prop, kind, plot, x, z, distance, stone, standing (stone_plan.md). "
        "op deposits - every DEPOSIT (stone_plan.md step 4): id, kind (seam, mountain, outcrop), good, holds (a number, or "
        "endless), x, z, plot_count, explored_plots, props (an outcrop's rock in the forest), plots (the mountain's "
        "foot only with `all` true); a seam's piles (its crystal lying as crates: crate, plot, floatstone, stone, "
        "pick_up), wall, band, "
        "balcony and foot_y; dug and left (THE MINE, step 5); the mountain's rise behind its foot (least, median, most, plots_over_1_5); and the map's "
        "seams, balconies and outcrops. "
        "op set_stock: the stock of building `building` (or the one at x,z; a camp's supplies) set to "
        "`contents` (goods by name and amount, e.g. {\"provisions\":6,\"wheat\":0}) - RECORDED debug edits "
        "(CHASM_CMD_STOCK), a good at a time; `pantry` true sets a camp's pantry instead. `provisions` reports PROVISIONS (docs/defence_plan.md): how many "
        "are still lacking beyond what is made, and what each builders' hut holds. STOCK KEPT (docs/goods_plan.md): op keep - "
        "the colony keeps `count` of made good `good` (provisions, rope) in its stores; op order_more - an order for `count` "
        "more of it, 0 cancelling the one running. Both RECORDED (CHASM_CMD_KEEP). `kept` reports each made good's kept "
        "number, its order's level (0 none), the level they make, and what the stores hold. SCOUTING (docs/defence_plan.md): "
        "op gather - person `person`, ordered out, first fills a scout's pack (`on`, default true), or goes straight on - "
        "RECORDED (CHASM_CMD_GATHER), `refusal` as for an order; a worker shows `gather`, the places still to go to. "
        "`notices` is what the rules told the player: `made` since the load, and the `last` few - nothing_to_take (told "
        "to gather, nothing anywhere), ran_out (an ordered man's pack out of `what`), no_tent (told to take a tent, none "
        "to spare anywhere he can get to) and seam_seen (the first to see a floatstone seam). `events` is every EVENT the rules "
        "made (Economy.h; docs/sound_plan.md) - `made` since the load and the `last` 64: tick, kind (those four, born, died, "
        "tree_felled, rock_broken, storey_raised, building_finished, harvest, settled), person, building, plot, x, z, what, "
        "amount; chasm_stats has the saved history they feed. SETTLEMENTS (docs/settlements_plan.md): "
        "`settlements` lists each - its oldest home's id - with its beds, its reach (world units of walking), its "
        "territory's plots and its people; a worker and a building show "
        "their `settlement`, 0 none. op take_tent - person `person`, ordered out, first takes a tent from the nearest camp "
        "with one to spare (`on`, default true), or goes straight on; op pitch - the tent he carries put up where he "
        "stands, or as near as it may go, his home from then on (his order over); op settle - one ordered onto another "
        "settlement's ground and inside its borders moves in without a tent, to a camp's bed to spare, an empty "
        "house there, or squeezed in where there is most room. All RECORDED (CHASM_CMD_TENT, CHASM_CMD_PITCH, CHASM_CMD_SETTLE), `refusal` as for an order; a "
        "worker shows `tent`: to_take or carried. A camp's own water carrier draws at `draws_at` (a building's). "
        "FORAGING (docs/foraging_plan.md): op forage - person `person` sent to pick the ripe plant on the plot at x,z (or "
        "`plot`), `what` fruit (default), or to draw water at the edge of the water there, `what` water - an order to stand "
        "there and a one-off into his pack; `spread` true as one of a group's. RECORDED (CHASM_CMD_FORAGE), `refusal` as for "
        "an order; a worker shows `forage` while at it. op plants - the plants that bear within `reach` (default 20) of x,z: "
        "prop, kind (bush, fruit_tree), plot, settlement, fruit (-1 gone), ripe_now, regrow_days. Each settlement lists "
        "its `plants`: bushes, fruit_trees, gone, ripe and the fruit on them; a camp with a picker shows `picker_places`. "
        "THE HUD'S FIGURES (view only, worked out by the functions the panels draw with): `held` - water_days and food_days "
        "as NEEDS ATTENTION counts them (stores, camp supplies and the houses' pantries, everyone at their share; unrounded - "
        "the rows round down), and mouths, everyone in adults; `bar` - the top bar as drawn: people, housed, food (the "
        "foods, provisions apart), then wood, water and any good it names; `people.in_camp` - those sleeping in the camp "
        "or with no home, BUILD HOUSES's count; `worth_days` - what a unit of water and of each food is worth to one adult, "
        "in days; `firewood` - the lived-in homes the full winter's snow reaches (homes), what they burn a dawn under it "
        "(burn_per_dawn), the wood they hold (at_home: a camp's supplies too), the stores' and other camps' (in_stores), "
        "dawns_at_home and dawns_with_stores, and the homes warm_today and cold_today since the last dawn; `crops` - each "
        "crop's days to ripen at a pace of 1 and ripens_at_camp, sown today, before the frost there; "
        "`camp_growing_days_left`; `neediest` - MOST IN NEED's rows in its order: id, name, need (dry, thirsty, hungry, "
        "none), health_percent, health_days, water_days. op buildings: every building, alone - id, kind, plot (its "
        "first, -1 on cells) or cell, x, z, stands (any of it), site (any still to build), storeys, site_storeys, "
        "site_cells, stopped (a stoppable workplace), places (a home's: its land's farmers), workers "
        "(id, name, job - a farmer's workplace is the home whose land he farms), settlement; a home's living and holds, "
        "a camp's camp_places (its own woodcutter, water carrier and picker), a field's farmer; and `homeless`. "
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
                {"person",{{"type","integer"},{"description","set_needs, order, release, guard, release_guard, gather, take_tent, "
                                                            "pitch, settle, forage: the person's id"}}},
                {"layer",{{"type","integer"},{"description","order: the layer of the plot he stands on, in half storeys - 0 its ground, "
                                                           "a castle deck's (ramparts_plan.md); default its top"}}},
                {"count",{{"type","integer"},{"description","guards: how many guards the tower takes; keep, order_more: how many"}}},
                {"good",{{"type","string"},{"description","keep, order_more: the made good, provisions or rope"}}},
                {"health_days",{{"type","number"}}},
                {"water_days",{{"type","number"}}},
                {"what",{{"type","string"},{"description","forage: fruit or water"}}},
                {"spread",{{"type","boolean"},{"description","forage: one of a group's"}}},
                {"reach",{{"type","number"},{"description","plants: how far from x,z, world units (default 20)"}}},
                {"crate",{{"type","integer"},{"description","pick_up: the crate's id"}}},
                {"play",{{"type","boolean"},{"description","order, forage, pick_up: as a PLAY command, held to the clouds - forage and "
                                                          "pick_up refused under them, an order's refusal there only 'no way there' "
                                                          "(default false: a debug command, as every tool's)"}}},
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
                cmd.value[2] = args.value("spread",false) ? 1.0f : 0.0f;  //one of a group: the plot, else the nearest round it
                cmd.value[3] = args.value("play",false) ? 1.0f : 0.0f;
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
            }else if (op == "take_tent" || op == "pitch" || op == "settle"){
                //A tent taken first, or pitched where he stands - or, sent into another settlement and there, moving
                //in without one (settlements_plan.md, "Founding: taking a tent").
                SimCommand cmd;
                cmd.type = (op == "pitch") ? CHASM_CMD_PITCH : (op == "settle") ? CHASM_CMD_SETTLE : CHASM_CMD_TENT;
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
            }else if (op == "forage"){
                //Picking a ripe plant or drawing water by hand (foraging_plan.md): the plot at x,z (or `plot`).
                int plot = args.value("plot",-1);
                if (plot < 0 && args.contains("x") && args.contains("z")){
                    GridPick p = w->picker->Pick(vec2(args["x"].get<float>(),args["z"].get<float>()));
                    plot = p.f_hit ? p.plot : -1;
                }
                std::string what = args.value("what",std::string("fruit"));
                if (plot < 0 || (what != "fruit" && what != "water")){
                    return json{{"error","forage: give a plot at x,z (or `plot`), and `what`: fruit or water"}};
                }
                SimCommand cmd;
                cmd.type = CHASM_CMD_FORAGE;
                cmd.flags = SIM_CMD_FLAG_RECORD;
                cmd.subtype = (uint32_t)args.value("person",0);
                cmd.value[0] = (float)plot;
                cmd.value[1] = (float)((what == "water") ? FORAGE_WATER : FORAGE_FRUIT);
                cmd.value[2] = args.value("spread",false) ? 1.0f : 0.0f;
                cmd.value[3] = args.value("play",false) ? 1.0f : 0.0f;
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
            }else if (op == "plants"){
                //FORAGING (foraging_plan.md): the plants that bear within `reach` (default 20) of x,z, nearest first -
                //for finding one to send someone to, and for watching one pick bare and grow back.
                std::shared_ptr<const EconomyState> es = GetEconomy();
                if (!es || es->world != w || !w->forest || !args.contains("x") || !args.contains("z")){
                    return json{{"error","plants: give x,z - and an economy for this world"}};
                }
                vec2 at(args["x"].get<float>(),args["z"].get<float>());
                float reach = args.value("reach",20.0f);
                std::vector<std::pair<float,int>> nearby;
                for (size_t i = 0; i < w->forest->props.size(); i++){
                    const PropInstance& p = w->forest->props[i];
                    float d = (vec2(p.pos.x,p.pos.z) - at).length();
                    if (ForestBears(p) && d <= reach){
                        nearby.push_back(std::make_pair(d,(int)i));
                    }
                }
                std::sort(nearby.begin(),nearby.end());
                json list = json::array();
                for (size_t t = 0; t < nearby.size() && t < 40; t++){
                    int i = nearby[t].second;
                    const PropInstance& p = w->forest->props[i];
                    int f = (i < (int)es->fruit.size()) ? es->fruit[i] : 0;
                    list.push_back(json{{"prop",i},{"kind",p.f_fruit ? "fruit_tree" : "bush"},{"plot",p.plot},
                                        {"x",p.pos.x},{"z",p.pos.z},{"distance",nearby[t].first},
                                        {"settlement",(p.plot >= 0 && p.plot < (int)es->settlement.size()) ? es->settlement[p.plot] : 0},
                                        {"fruit",(f == FORAGE_GONE) ? -1 : f},{"ripe_now",EconomyFruitOn(*es,i)},
                                        {"regrow_days",(i < (int)es->regrow.size()) ? es->regrow[i] : 0},
                                        {"standing",i < (int)es->prop_state.size() && es->prop_state[i] == PROP_STATE_STANDING}});
                }
                result["plants"] = list;
                result["found"] = (int)nearby.size();
                return result;
            }else if (op == "rocks"){
                //THE STONECUTTER (stone_plan.md): the rocks and boulders within `reach` (default 20) of x,z, nearest first.
                std::shared_ptr<const EconomyState> es = GetEconomy();
                if (!es || es->world != w || !w->forest || !args.contains("x") || !args.contains("z")){
                    return json{{"error","rocks: give x,z - and an economy for this world"}};
                }
                vec2 at(args["x"].get<float>(),args["z"].get<float>());
                float reach = args.value("reach",20.0f);
                std::vector<std::pair<float,int>> nearby;
                for (size_t i = 0; i < w->forest->props.size(); i++){
                    const PropInstance& p = w->forest->props[i];
                    float d = (vec2(p.pos.x,p.pos.z) - at).length();
                    if (ForestIsRock(p.kind) && d <= reach){
                        nearby.push_back(std::make_pair(d,(int)i));
                    }
                }
                std::sort(nearby.begin(),nearby.end());
                json list = json::array();
                for (size_t t = 0; t < nearby.size() && t < 40; t++){
                    int i = nearby[t].second;
                    const PropInstance& p = w->forest->props[i];
                    list.push_back(json{{"prop",i},{"kind",prop_asset_names[p.kind]},{"plot",p.plot},{"x",p.pos.x},{"z",p.pos.z},
                                        {"distance",nearby[t].first},{"stone",ForestRockStone(p.kind)},
                                        {"standing",i < (int)es->prop_state.size() && es->prop_state[i] == PROP_STATE_STANDING}});
                }
                result["rocks"] = list;
                result["found"] = (int)nearby.size();
                return result;
            }else if (op == "deposits"){
                /*
                    DEPOSITS (Deposits.h, stone_plan.md step 4): every one - seams, the mountain, outcrops - what it holds,
                    where, and how much of it has been seen. The mountain's foot is long: its plots only with `all`, and
                    how far the mountain rises behind them, which the mine's ground rule (step 5) will be held against.
                */
                if (!w->deposits){
                    return json{{"error","deposits: none for this world"}};
                }
                std::shared_ptr<const Exploration> ex = GetExploration();
                bool f_all = args.value("all",false);
                const Grid& g = *w->grid;
                const Terrain& t = *w->terrain;
                //What the forest stands for each - an outcrop's rock - and the crystal lying on a seam's foot, as crates.
                std::vector<json> looks(w->deposits->deposits.size(),json::array());
                for (size_t i = 0; w->forest && i < w->forest->props.size(); i++){
                    const PropInstance& p = w->forest->props[i];
                    int d = (p.kind == PROP_OUTCROP) ? w->deposits->At(p.plot) : -1;
                    if (d >= 0){
                        looks[d].push_back(json{{"prop",(int)i},{"kind",prop_asset_names[p.kind]},{"plot",p.plot},
                                                {"x",p.pos.x},{"y",p.pos.y},{"z",p.pos.z},{"scale",p.scale}});
                    }
                }
                std::shared_ptr<const EconomyState> es = GetEconomy();
                std::vector<json> piles(w->deposits->deposits.size(),json::array());
                for (size_t i = 0; es && es->world == w && i < es->crates.size(); i++){
                    const EconomyCrate& c = es->crates[i];
                    int d = w->deposits->SeamAt(c.plot);
                    if (d >= 0){
                        piles[d].push_back(json{{"crate",c.id},{"plot",c.plot},{"floatstone",c.stock[GOOD_FLOATSTONE]},
                                                {"stone",c.stock[GOOD_STONE]},{"pick_up",c.f_pick_up},{"too_far",c.f_too_far}});
                    }
                }
                json list = json::array();
                int counts[DEPOSIT_KINDS] = {};
                for (size_t i = 0; i < w->deposits->deposits.size(); i++){
                    const Deposit& d = w->deposits->deposits[i];
                    counts[d.kind]++;
                    int seen = 0;
                    for (int p : d.plots){
                        seen += (!ex || ex->Empty() || ex->Explored(g.fine.pos[p])) ? 1 : 0;
                    }
                    json one = {{"id",(int)i},{"kind",DepositKindName(d.kind)},{"good",GoodName(d.good)},
                                {"holds",(d.holds == DEPOSIT_ENDLESS) ? json("endless") : json(d.holds)},
                                {"x",d.centre.x},{"z",d.centre.y},{"plot_count",(int)d.plots.size()},{"explored_plots",seen},
                                {"props",looks[i]}};
                    if (d.kind != DEPOSIT_MOUNTAIN || f_all){
                        one["plots"] = d.plots;
                    }
                    if (d.kind == DEPOSIT_MOUNTAIN && f_all){
                        json at = json::array();
                        for (int p : d.plots){
                            at.push_back(json::array({g.fine.pos[p].x,g.fine.pos[p].y}));
                        }
                        one["plots_at"] = at;
                    }
                    //THE MINE (step 5): what has been dug out of it, and what is left.
                    if (es && es->world == w){
                        int left = EconomyDepositLeft(*es,(int)i);
                        one["dug"] = (i < es->dug.size()) ? es->dug[i] : 0;
                        one["left"] = (left == INT_MAX) ? json("endless") : json(left);
                    }
                    if (d.kind == DEPOSIT_SEAM){
                        json at = json::array();
                        for (int p : d.plots){
                            at.push_back(json::array({g.fine.pos[p].x,g.fine.pos[p].y,t.Height(p)}));
                        }
                        json wall_at = json::array();
                        for (int p : d.wall){
                            wall_at.push_back(json::array({g.fine.pos[p].x,g.fine.pos[p].y,t.Height(p)}));
                        }
                        one["plots_at"] = at;
                        one["wall_at"] = wall_at;
                        one["piles"] = piles[i];
                        one["wall"] = d.wall;
                        one["band"] = d.band;
                        one["balcony"] = d.balcony;
                        one["foot_y"] = t.Height(d.plots[0]);
                    }
                    if (d.kind == DEPOSIT_MOUNTAIN){
                        std::vector<float> rise;
                        for (int p : d.plots){
                            float most = 0.0f;
                            for (int k = 0; k < w->picker->PlotQuadCount(p); k++){
                                const GridQuad& q = g.fine.quads[w->picker->PlotQuadCorner(p,k) / 4];
                                for (int c = 0; c < 4; c++){
                                    if (t.Mountain(q.v[c])){
                                        most = std::max(most,t.ground[q.v[c]] - t.ground[p]);
                                    }
                                }
                            }
                            rise.push_back(most);
                        }
                        std::sort(rise.begin(),rise.end());
                        int over_storey = 0;
                        for (float r : rise){
                            over_storey += (r >= 1.5f) ? 1 : 0;
                        }
                        if (!rise.empty()){
                            one["rise"] = json{{"least",rise.front()},{"median",rise[rise.size() / 2]},{"most",rise.back()},
                                               {"plots_over_1_5",over_storey}};
                        }
                    }
                    list.push_back(one);
                }
                result["deposits"] = list;
                result["seams"] = counts[DEPOSIT_SEAM];
                result["balconies"] = w->deposits->balconies;
                result["outcrops"] = counts[DEPOSIT_OUTCROP];
                result["build_ms"] = w->deposits->build_ms;
                return result;
            }else if (op == "buildings"){
                /*
                    Every building in one call (apps/llm/docs/void_plan.md): where it is, whether it stands or is still a
                    site, and who works it - what otherwise takes a chasm_pick a place. One pass over the plots and one
                    over the cells for where each starts and whether any of it stands, as ChasmIssues does. View only.
                */
                std::shared_ptr<const EconomyState> es = GetEconomy();
                std::shared_ptr<const ZoneState> zs = GetZones();
                if (!es || !zs || es->world != w || zs->world != w){
                    return json{{"error","no economy for this world yet"}};
                }
                size_t n = zs->buildings.size();
                std::vector<int> first_plot(n,-1);
                std::vector<int> first_cell(n,-1);
                std::vector<uint8_t> stands(n,0);
                for (size_t v = 0; v < zs->building.size(); v++){
                    uint32_t id = zs->building[v];
                    if (id && id < n){
                        first_plot[id] = (first_plot[id] < 0) ? (int)v : first_plot[id];
                        stands[id] |= (zs->standing[v] > 0) ? 1 : 0;
                    }
                }
                for (size_t q = 0; q < zs->field.size(); q++){
                    uint32_t id = zs->field[q];
                    if (id && id < n){
                        first_cell[id] = (first_cell[id] < 0) ? (int)q : first_cell[id];
                        stands[id] |= zs->field_built[q] ? 1 : 0;   //a building on cells stands with a built cell
                    }
                }
                std::vector<int> living(n,0);
                for (const EconomyWorker& k : es->workers){
                    living[(k.house < n) ? k.house : 0]++;
                }
                json list = json::array();
                for (size_t id = 1; id < n; id++){
                    const ZoneBuilding& b = zs->buildings[id];
                    if (b.size <= 0){
                        continue;
                    }
                    ZoneBuildingInfo info = ZoneBuildingFigures(*w,*zs,(uint32_t)id);
                    vec2 at(0.0f,0.0f);
                    ChasmBuildingPoint(*w,*zs,(uint32_t)id,at);
                    json workers = json::array();
                    for (const EconomyWorker& k : es->workers){
                        if (k.building == id){
                            workers.push_back(json{{"id",k.id},{"name",EconomyPersonName(k)},{"job",EconomyJobName(k.job)}});
                        }
                    }
                    json one{{"id",id},{"kind",ZoneKindName(b.kind)},{"plot",first_plot[id]},{"x",at.x},{"z",at.y},
                             {"stands",stands[id] != 0},{"site",info.site_storeys > 0 || info.site_cells > 0},
                             {"storeys",info.storeys},{"site_storeys",info.site_storeys},
                             {"places",(id < es->places.size()) ? es->places[id] : 0},{"workers",workers},
                             {"settlement",EconomySettlementOf(*es,(uint32_t)id)}};
                    if (first_cell[id] >= 0){
                        one["cell"] = first_cell[id];
                        one["site_cells"] = info.site_cells;
                    }
                    if (ZoneKindStoppable(b.kind)){
                        one["stopped"] = b.f_stopped != 0;
                    }
                    if (b.kind == ZONE_KIND_HOUSE || b.kind == ZONE_KIND_CAMP){
                        one["living"] = living[id];
                        one["holds"] = (id < es->capacity.size()) ? es->capacity[id] : 0;
                    }
                    if (b.kind == ZONE_KIND_CAMP){
                        //THE CAMP'S OWN WORK (Economy.h): kept apart from `places`, which are its land's farmers.
                        auto at = [&](const std::vector<int>& v){ return (id < v.size()) ? v[id] : 0; };
                        one["camp_places"] = json{{EconomyJobName(WORKER_JOB_WOODCUTTER),at(es->cutter_places)},
                                                  {EconomyJobName(WORKER_JOB_WATER),at(es->drawer_places)},
                                                  {EconomyJobName(WORKER_JOB_PICKER),at(es->picker_places)}};
                    }
                    if (b.kind == ZONE_KIND_FIELD){
                        //A field employs nobody: its farmer's workplace is the home whose land it is (land_plan.md).
                        uint32_t farmer = (id < es->farmer_of.size()) ? es->farmer_of[id] : 0;
                        one["farmer"] = farmer;
                        for (const EconomyWorker& k : es->workers){
                            if (farmer && k.id == farmer){
                                one["farmer_name"] = EconomyPersonName(k);
                            }
                        }
                    }
                    list.push_back(one);
                }
                result["buildings"] = list;
                result["homeless"] = living[0];
                return result;
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
            }else if (op == "pick_up"){
                //PICK UP (stone_plan.md): as the chip on the crate's card.
                SimCommand cmd;
                cmd.type = CHASM_CMD_PICK_UP;
                cmd.flags = SIM_CMD_FLAG_RECORD;
                cmd.subtype = (uint32_t)std::max(0,args.value("crate",0));
                cmd.value[0] = args.value("on",true) ? 1.0f : 0.0f;
                cmd.value[1] = args.value("play",false) ? 1.0f : 0.0f;
                uint32_t refused = crate_refusals.load();
                SubmitCommandAndWait(cmd);
                std::string refusal;
                if (crate_refusals.load() != refused){
                    std::lock_guard<std::mutex> lock(grid_mutex);
                    refusal = crate_last_refusal;
                }
                result["refusal"] = refusal;
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
                float pace = EconomyFieldPace(*e,*z,f.building);
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
            //EVENTS (Economy.h; sound_plan.md): the last few the rules made, oldest first, and how many in all.
            json events = json::array();
            for (const EconomyEvent& ev : e->events){
                json what = json::array();
                for (int g = 0; g < GOOD_COUNT; g++){
                    if (ev.what & GOOD_BIT(g)){
                        what.push_back(GoodName(g));
                    }
                }
                json j{{"tick",ev.tick},{"kind",EconomyEventName(ev.kind)},{"person",ev.person},
                       {"building",ev.building},{"plot",ev.plot},{"x",ev.pos.x},{"z",ev.pos.y},
                       {"what",what},{"amount",ev.amount}};
                //An area found (areas_plan.md): its `what` is no good but whether it is the first of its kind.
                if (ev.kind == EVENT_DISCOVERED){
                    const AreaData* areas = e->world ? e->world->areas.get() : nullptr;
                    bool f_known = areas && ev.amount >= 0 && ev.amount < (int)areas->areas.size();
                    j.erase("what");
                    j["first"] = (ev.what & 1) != 0;
                    j["area"] = f_known ? AreaKindName(areas->areas[(size_t)ev.amount].kind) : "unknown";
                }
                events.push_back(j);
            }
            result["events"] = json{{"made",e->events_made},{"last",events}};
            /*
                NOTICES, as this tool gave them before the events (and apps/llm's VoidBrief reads them): the four kinds
                told about a person - `made` of them since the load, and those of them still in the events' ring.
            */
            json notices = json::array();
            uint32_t notices_made = 0;
            const int notice_kinds[] = {EVENT_NOTHING_TO_TAKE,EVENT_RAN_OUT,EVENT_NO_TENT,EVENT_SEAM_SEEN};
            for (int kind : notice_kinds){
                notices_made += e->events_by_kind[kind];
            }
            for (const EconomyEvent& n : e->events){
                if (std::find(std::begin(notice_kinds),std::end(notice_kinds),n.kind) == std::end(notice_kinds)){
                    continue;
                }
                json what = json::array();
                if (n.what & GOOD_BIT(GOOD_WATER)){
                    what.push_back("water");
                }
                if (n.what & GOODS_EATEN){
                    what.push_back("food");
                }
                if (n.what & GOOD_BIT(GOOD_FLOATSTONE)){
                    what.push_back("floatstone");
                }
                notices.push_back(json{{"tick",n.tick},{"person",n.person},{"what",what},{"kind",EconomyEventName(n.kind)}});
            }
            result["notices"] = json{{"made",notices_made},{"last",notices}};
            //SETTLEMENTS (settlements_plan.md): each one - its oldest home's id - with its beds and reach, its
            //territory's plots and its people.
            std::map<uint32_t,std::pair<int,int>> settled;
            for (uint32_t s : e->settlement){
                if (s){
                    settled[s].first++;
                }
            }
            for (const EconomyWorker& k : e->workers){
                uint32_t s = EconomyPersonSettlement(*e,k);
                if (s){
                    settled[s].second++;
                }
            }
            //FORAGING (foraging_plan.md): what bears fruit in each one's land - its bushes and fruit trees, those gone
            //(built over), those ripe now and the fruit on them - the figures the tuning is set from.
            struct Plants{ int bushes = 0, trees = 0, gone = 0, ripe = 0, fruit = 0; };
            std::map<uint32_t,Plants> plants;
            if (w->forest){
                for (size_t i = 0; i < w->forest->props.size() && i < e->fruit.size(); i++){
                    const PropInstance& p = w->forest->props[i];
                    if (!ForestBears(p) || p.plot < 0 || p.plot >= (int)e->settlement.size() || !e->settlement[p.plot]){
                        continue;
                    }
                    Plants& pl = plants[e->settlement[p.plot]];
                    (p.f_fruit ? pl.trees : pl.bushes)++;
                    pl.gone += (e->fruit[i] == FORAGE_GONE) ? 1 : 0;
                    int on = EconomyFruitOn(*e,(int)i);
                    pl.ripe += (on > 0) ? 1 : 0;
                    pl.fruit += on;
                }
            }
            json settlements = json::array();
            for (const EconomySettlement& st : e->settlements){
                std::pair<int,int> got = settled.count(st.id) ? settled[st.id] : std::pair<int,int>(0,0);
                Plants pl = plants.count(st.id) ? plants[st.id] : Plants();
                settlements.push_back(json{{"id",st.id},{"beds",st.beds},{"reach",st.reach},{"plots",got.first},
                                           {"people",got.second},
                                           {"plants",json{{"bushes",pl.bushes},{"fruit_trees",pl.trees},{"gone",pl.gone},
                                                          {"ripe",pl.ripe},{"fruit",pl.fruit}}}});
            }
            result["settlements"] = settlements;
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
                                    {"posted",posted},{"camp_plot",e->camp_plot},{"in_camp",ChasmInCamp(*z,*e)}};
            /*
                The HUD's figures (apps/llm/docs/void_plan.md), from the same functions the panels draw with: the days of
                water and food held as NEEDS ATTENTION counts them - the houses' pantries too, which `stored` leaves out
                - and the top bar as drawn. Unrounded, as the rows round days DOWN.
            */
            float water_days = 0.0f;
            float food_days = 0.0f;
            float mouths = 0.0f;
            ChasmDaysHeld(*z,*e,water_days,food_days,&mouths);
            result["held"] = json{{"water_days",water_days},{"food_days",food_days},{"mouths",mouths}};
            json bar{{"people",e->workers.size()},{"housed",housed}};
            int bar_food = 0;
            for (int g = 0; g < GOOD_COUNT; g++){
                bar_food += (GOODS_FOOD & GOOD_BIT(g)) ? totals[g] : 0;
            }
            bar["food"] = bar_food;
            GoodsMask bar_goods = ChasmBarGoods(*z,*e,totals);
            for (int g = 0; g < GOOD_COUNT; g++){
                if (bar_goods & GOOD_BIT(g)){
                    bar[GoodName(g)] = totals[g];
                }
            }
            result["bar"] = bar;
            //What a unit of each is worth to one adult, in days: water's drinking, a food's eating (EconomyFoodWorth).
            json worth{{"water",(double)NEEDS_WATER_WORTH / CALENDAR_DAY_TICKS}};
            for (int g = 0; g < GOOD_COUNT; g++){
                if (EconomyFoodWorth(g) > 0){
                    worth[GoodName(g)] = (double)EconomyFoodWorth(g) / CALENDAR_DAY_TICKS;
                }
            }
            result["worth_days"] = worth;
            /*
                FIREWOOD (Economy.h, HEAT) for the winter ahead, whatever the season: the lived-in homes the full winter's
                snow reaches, what they burn a dawn under it, the wood they hold - a camp's supplies with its pantry, as it
                burns both - and the dawns that lasts; then with the rest of the stores' and camps' wood too. And how many
                burned or went cold at the last dawn.
            */
            std::vector<int> home_living(z->buildings.size(),0);
            for (const EconomyWorker& k : e->workers){
                if (k.house && k.house < home_living.size()){
                    home_living[k.house]++;
                }
            }
            int fire_homes = 0, fire_at_home = 0, fire_supplies = 0, warm = 0, cold = 0;
            float fire_burn = 0.0f;
            for (size_t id = 1; id < z->buildings.size() && id < e->stock.size(); id++){
                int kind = z->buildings[id].kind;
                if (z->buildings[id].size <= 0 || (kind != ZONE_KIND_HOUSE && kind != ZONE_KIND_CAMP)){
                    continue;
                }
                int heat = (id < e->heat.size()) ? e->heat[id] : HEAT_NONE;
                warm += (heat == HEAT_WARM) ? 1 : 0;
                cold += (heat == HEAT_COLD) ? 1 : 0;
                int burn = (id < e->burn.size()) ? e->burn[id] : 0;
                if (burn <= 0 || home_living[id] <= 0 || id >= e->snowed.size() || !e->snowed[id]){
                    continue;
                }
                fire_homes++;
                fire_burn += (id < e->burn_halves.size()) ? e->burn_halves[id] * 0.5f : (float)burn;
                fire_at_home += e->stock[id][GOOD_WOOD];
                if (kind == ZONE_KIND_CAMP){
                    fire_at_home += (id < e->pantry.size()) ? e->pantry[id][GOOD_WOOD] : 0;
                    fire_supplies += e->stock[id][GOOD_WOOD];
                }
            }
            int fire_elsewhere = totals[GOOD_WOOD] - fire_supplies;    //the stores' and the other camps', not counted at home
            json firewood{{"homes",fire_homes},{"burn_per_dawn",fire_burn},{"at_home",fire_at_home},{"in_stores",fire_elsewhere},
                          {"warm_today",warm},{"cold_today",cold}};
            if (fire_burn > 0.0f){
                firewood["dawns_at_home"] = (float)fire_at_home / fire_burn;
                firewood["dawns_with_stores"] = (float)(fire_at_home + fire_elsewhere) / fire_burn;
            }
            result["firewood"] = firewood;
            /*
                CROPS (Economy.h, THE FROST): each one's growing days at a pace of 1 - a field's own pace, its farmer's
                skill, is its `frost.pace` - and whether it ripens before the frost at the camp, sown today at that pace;
                the growing weather the camp has left. With no field yet, this is what "build a field" can be told.
            */
            int camp_left = -1;
            if (e->camp_plot >= 0 && e->camp_plot < (int)w->grid->fine.pos.size()){
                camp_left = EconomyGrowingTicksLeft(*w,w->grid->fine.pos[e->camp_plot],calendar_shown.load());
                result["camp_growing_days_left"] = (float)camp_left / CALENDAR_DAY_TICKS;
            }
            json crops = json::object();
            for (int c = 0; c < ZONE_CROP_COUNT; c++){
                int ticks = EconomyRipenTicks(c,0,1.0f);
                json one{{"days",(float)ticks / CALENDAR_DAY_TICKS}};
                if (camp_left >= 0){
                    one["ripens_at_camp"] = ticks <= camp_left;
                }
                crops[ZoneCropName(c)] = one;
            }
            result["crops"] = crops;
            //MOST IN NEED (ChasmNeediest): the panel's rows in its order - what each is short of as his row says it, and
            //his health as the row's percent and in days.
            json neediest = json::array();
            for (const EconomyWorker* k : ChasmNeediest(*e,NEEDIEST_SHOWN)){
                std::string need = ChasmNeedName(*k);
                std::transform(need.begin(),need.end(),need.begin(),::tolower);
                neediest.push_back(json{{"id",k->id},{"name",EconomyPersonName(*k)},{"need",need.empty() ? "none" : need},
                                        {"health_percent",(int)std::lround(100.0f * std::max(0,k->health) / NEEDS_HEALTH_FULL)},
                                        {"health_days",std::round(k->health * 100.0 / CALENDAR_DAY_TICKS) / 100.0},
                                        {"water_days",std::round(k->water * 100.0 / CALENDAR_DAY_TICKS) / 100.0}});
            }
            result["neediest"] = neediest;
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
            /*
                The DISCOVERIES (demo_plan.md step 6): each made with its tick - the history's start for what the landing
                saw - and who, and what tools it brings; and the tools play mode still hides.
            */
            std::shared_ptr<const ChasmDiscoveries> found = GetDiscoveries();
            json discoveries = json::array();
            for (int d = 0; found && d < DISCOVERY_COUNT; d++){
                json one{{"discovery",ChasmDiscoveryName(d)},{"made",found->tick[d] >= 0},{"tools",ChasmDiscoveryTools(d)}};
                if (found->tick[d] >= 0){
                    one["tick"] = found->tick[d];
                    one["person"] = found->person[d];
                }
                discoveries.push_back(one);
            }
            result["discoveries"] = discoveries;
            //In play mode only: debug mode builds anything.
            uint32_t locks = ToolsLocked();
            json locked = json::array();
            for (int d = 0; d < DISCOVERY_COUNT; d++){
                bool f_waits = false;
                for (int t = 0; t < CHASM_TOOL_COUNT; t++){
                    f_waits = f_waits || (ChasmToolNeeds(t) == d && ((locks >> t) & 1u));
                }
                if (f_waits){
                    locked.push_back(ChasmDiscoveryTools(d));
                }
            }
            result["tools_locked"] = locked;
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
                                      {"day",(double)c.tick / CALENDAR_DAY_TICKS},{"pick_up",c.f_pick_up},{"too_far",c.f_too_far}});
            }
            result["crates"] = crates;
            return MaybeAttachScreenshot(result,args.value("include_screenshot",false),args.value("include_ui",true));
        });
}
#endif
