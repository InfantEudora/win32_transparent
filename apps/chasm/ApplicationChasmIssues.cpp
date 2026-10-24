#include "ApplicationChasm.h"
#include "UIOverlay.h"
#include "ChasmHud.h"
#include "Window.h"
#include "Calendar.h"
#ifdef USE_MCP
#include "MCPServer.h"
#endif
#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdio.h>

/*
    NEEDS ATTENTION (docs/issues_plan.md, the user's 2026-10-09): what wants doing, over the tools' grid -
    a choice the player has left open, something placed that cannot work as it is, and what to build next
    while the game is being learned. Worked out from the published zones and economy, every frame and by
    the tool, and never saved: the one part of it that changes the run - a crop left unchosen defaulting to
    AUTO - is the economy's (Economy.h, ECONOMY_CROP_GRACE). The rows are the user's first eight.
*/
#define ISSUES_SHOWN        5
#define ISSUES_RED_DAYS     2.0f    //a hint whose deadline is closer than this goes red, to the top

//What the colony holds to live on, in days of it for everyone: water, and food at what each unit is worth.
//Stores, the camp's supplies and the houses' pantries - not what lies at a workplace or is being carried.
void ChasmDaysHeld(const ZoneState& z, const EconomyState& e, float& water, float& food, float* mouths_out,
                   uint32_t settlement){
    bool f_all = settlement == ECONOMY_EVERY_SETTLEMENT || !e.world;
    int64_t water_ticks = 0;
    int64_t food_ticks = 0;
    for (size_t id = 1; id < e.stock.size() && id < z.buildings.size(); id++){
        int kind = z.buildings[id].kind;
        if (z.buildings[id].size <= 0 || (kind != ZONE_KIND_STORE && kind != ZONE_KIND_CAMP && kind != ZONE_KIND_HOUSE)
            || (!f_all && EconomySettlementOf(e,(uint32_t)id) != settlement)){
            continue;
        }
        water_ticks += (int64_t)e.stock[id][GOOD_WATER] * NEEDS_WATER_WORTH;
        for (int g = 0; g < GOOD_COUNT; g++){
            food_ticks += (int64_t)e.stock[id][g] * EconomyFoodWorth(g);
        }
    }
    //Everyone at their SHARE (Economy.h): a baby drinks half what an adult does.
    int mouths = 0;
    for (const EconomyWorker& k : e.workers){
        mouths += (f_all || ChasmPersonSettlement(*e.world,e,k) == settlement) ? EconomyShare(k.born,e.tick) : 0;
    }
    float people_days = (float)std::max(1,mouths) / LIFE_SHARE_FULL * CALENDAR_DAY_TICKS;
    water = (float)water_ticks / people_days;
    food = (float)food_ticks / people_days;
    if (mouths_out){
        *mouths_out = (float)mouths / LIFE_SHARE_FULL;
    }
}

uint32_t ChasmPersonSettlement(const ChasmWorld& w, const EconomyState& e, const EconomyWorker& k){
    (void)w;
    return EconomyPersonLand(e,k);      //the rules' own, which the statistics' rows count people by too
}

int ChasmInCamp(const ZoneState& z, const EconomyState& e, uint32_t settlement){
    bool f_all = settlement == ECONOMY_EVERY_SETTLEMENT || !e.world;
    int in_camp = 0;
    for (const EconomyWorker& k : e.workers){
        if (f_all || ChasmPersonSettlement(*e.world,e,k) == settlement){
            in_camp += (k.house == 0 || (k.house < z.buildings.size() && z.buildings[k.house].kind == ZONE_KIND_CAMP)) ? 1 : 0;
        }
    }
    return in_camp;
}

namespace {

std::string DaysText(const char* what, float days){
    char buf[48];
    int d = (int)std::floor(days);
    if (d < 1){
        snprintf(buf,sizeof(buf),"%s UNDER A DAY",what);
    }else{
        snprintf(buf,sizeof(buf),"%s %i DAY%s",what,d,d == 1 ? "" : "S");
    }
    return buf;
}

const char* WorkplaceName(int kind){
    switch (kind){
        case ZONE_KIND_WOODCUTTER:  return "THE WOODCUTTER'S HUT";
        case ZONE_KIND_WATER:       return "THE WATER COLLECTOR";
        case ZONE_KIND_BUILDERS:    return "THE BUILDERS' HUT";
        case ZONE_KIND_RANGE:       return "THE RANGE";
        case ZONE_KIND_TOWER:       return "THE TOWER";
        case ZONE_KIND_ROPEWALK:    return "THE ROPEWALK";
        case ZONE_KIND_STONECUTTER: return "THE STONECUTTER'S HUT";
        case ZONE_KIND_MINE:        return "THE MINE";
        case ZONE_KIND_STEAM:       return "THE STEAM COLLECTOR";
        case ZONE_KIND_CONDENSER:   return "THE CONDENSER";
        case ZONE_KIND_SAIL_LOFT:   return "THE SAIL LOFT";
        case ZONE_KIND_STORE:       return "THE STORE";
        default:                    return "A WORKPLACE";
    }
}

#ifdef USE_MCP
const char* TierName(int tier){
    switch (tier){
        case ISSUE_DANGER:      return "danger";
        case ISSUE_COUNTDOWN:   return "countdown";
        case ISSUE_STUCK:       return "stuck";
        default:                return "next";
    }
}
#endif

}

void ChasmIssues(const ZoneState& z, const EconomyState& e, std::vector<ChasmIssue>& out, uint32_t locked, uint32_t settlement){
    out.clear();
    auto f_locked = [&](int tool){ return ((locked >> tool) & 1u) != 0; };
    if (e.world != z.world || e.workers.empty()){
        return;
    }
    /*
        ONE COLONY (settlements_plan.md, step 3): with `settlement`, only what is its own - a building or site by its door,
        a plot by its land, a person by ChasmPersonSettlement - and what it holds, so an outpost with no collector of its
        own is told to build one. ECONOMY_EVERY_SETTLEMENT passes everything, as it was.
    */
    bool f_all = settlement == ECONOMY_EVERY_SETTLEMENT;
    auto ours = [&](uint32_t id){ return f_all || EconomySettlementOf(e,id) == settlement; };
    auto ours_plot = [&](int v){ return f_all || (v >= 0 && v < (int)e.settlement.size() && e.settlement[v] == settlement); };
    auto ours_person = [&](const EconomyWorker& k){ return f_all || ChasmPersonSettlement(*e.world,e,k) == settlement; };
    //All of a good it holds: its buildings' stock, the crates on its land, what its people carry (EconomyGoodHeld's, narrowed).
    auto held_here = [&](int good){
        if (f_all){
            return EconomyGoodHeld(e,good);
        }
        int held = 0;
        for (size_t id = 1; id < e.stock.size(); id++){
            held += ours((uint32_t)id) ? e.stock[id][good] : 0;
        }
        for (const EconomyCrate& c : e.crates){
            held += ours_plot(c.plot) ? c.stock[good] : 0;
        }
        for (const EconomyWorker& k : e.workers){
            held += (k.carry_good == good && ours_person(k)) ? std::max(0,k.carry) : 0;
        }
        return held;
    };
    size_t n = z.buildings.size();
    //One pass over the plots and one over the cells: what of each building stands, and a field still a site.
    std::vector<uint8_t> stands(n,0);
    std::vector<int> first_plot(n,-1);
    std::vector<int> rope_cost(n,0);    //a site's rope still to build with (goods_plan.md): a winch's
    std::vector<int> stone_cost(n,0);   //...and its stone (stone_plan.md): a stonecutter's hut's, a mine's, a third storey's
    for (size_t v = 0; v < z.building.size(); v++){
        if (z.building[v] && z.standing[v] > 0){
            stands[z.building[v]] = 1;
        }
        if (z.building[v] && z.building[v] < n && z.storeys[v] > z.standing[v]){
            rope_cost[z.building[v]] += EconomyPlotLeftCost(*e.world,z,(int)v,GOOD_ROPE);
            stone_cost[z.building[v]] += EconomyPlotLeftCost(*e.world,z,(int)v,GOOD_STONE);
        }
        if (z.building[v] && z.building[v] < n && first_plot[z.building[v]] < 0){
            first_plot[z.building[v]] = (int)v;
        }
    }
    std::vector<uint8_t> field_site(n,0);
    for (size_t q = 0; q < z.field.size(); q++){
        if (z.field[q] && !z.field_built[q]){
            field_site[z.field[q]] = 1;
        }else if (z.field[q]){
            stands[z.field[q]] = 1;     //a building on cells - a range - stands with a built cell
        }
    }
    int placed[ZONE_KIND_COUNT] = {};
    bool f_producing = false;   //a woodcutter, collector or condenser standing: goods are being made
    for (size_t id = 1; id < n; id++){
        const ZoneBuilding& b = z.buildings[id];
        if (b.size <= 0 || b.kind >= ZONE_KIND_COUNT || !ours((uint32_t)id)){
            continue;
        }
        placed[b.kind]++;
        f_producing = f_producing || ((b.kind == ZONE_KIND_WOODCUTTER || b.kind == ZONE_KIND_WATER || b.kind == ZONE_KIND_CONDENSER)
                                      && stands[id] && !b.f_stopped);
    }

    //1. A field left without a crop, counting down to AUTO.
    for (const EconomyField& f : e.fields){
        uint32_t id = f.building;
        if (id >= n || z.buildings[id].kind != ZONE_KIND_FIELD || z.buildings[id].size <= 0
            || z.buildings[id].crop != ZONE_CROP_NONE || f.f_sown || f.last >= 0 || field_site[id] || !ours(id)){
            continue;
        }
        ChasmIssue i;
        i.kind = "choose_crop";
        i.tier = ISSUE_COUNTDOWN;
        i.left = 1.0f - std::min(1.0f,(float)f.wait / ECONOMY_CROP_GRACE);
        i.order = i.left;
        i.text = "FIELD: CHOOSE A CROP";
        i.value = "AUTO";
        i.building = id;
        out.push_back(i);
    }

    //2-6. The next steps, each with its deadline where there is one.
    float water_days = 0.0f;
    float food_days = 0.0f;
    ChasmDaysHeld(z,e,water_days,food_days,NULL,settlement);
    auto hint = [&](const char* kind, int order, const char* text, const std::string& value, int tool, float days){
        ChasmIssue i;
        i.kind = kind;
        i.tier = (days < ISSUES_RED_DAYS) ? ISSUE_DANGER : ISSUE_NEXT;
        i.order = (i.tier == ISSUE_DANGER) ? days : (float)order;
        i.text = text;
        i.value = value;
        i.tool = tool;
        out.push_back(i);
    };
    /*
        WATER MADE (the user's, 2026-10-24): a collector at a river, or a condenser from steam - a colony on the rim makes
        its water the second way, and was still told to build a collector. Either does, so the row names neither. A click
        takes up the condenser where a steam collector of the colony's already catches steam for one - on the rim, where
        a river may be nowhere near - else the collector while its tool is offered. Their tools come with a river found
        and with the chasm (demo_plan.md step 6): until either, the hint is to find a river.
    */
    if (!placed[ZONE_KIND_WATER] && !placed[ZONE_KIND_CONDENSER]){
        bool f_condenser = !f_locked(CHASM_TOOL_CONDENSER) && (placed[ZONE_KIND_STEAM] || f_locked(CHASM_TOOL_WATER));
        if (f_locked(CHASM_TOOL_WATER) && f_locked(CHASM_TOOL_CONDENSER)){
            hint("find_river",2,"FIND A RIVER",DaysText("WATER",water_days),-1,water_days);
        }else{
            hint("build_water",2,"BUILD A WATER SOURCE",DaysText("WATER",water_days),
                 f_condenser ? CHASM_TOOL_CONDENSER : CHASM_TOOL_WATER,water_days);
        }
    }
    if (!placed[ZONE_KIND_FIELD]){
        hint("build_field",3,"BUILD A FIELD",DaysText("FOOD",food_days),CHASM_TOOL_FIELD,food_days);
    }
    if (!placed[ZONE_KIND_WOODCUTTER]){
        int wood = EconomyStoredTotals(e,z,settlement)[GOOD_WOOD];
        char buf[32];
        snprintf(buf,sizeof(buf),"WOOD %i",wood);
        hint("build_woodcutter",4,"BUILD A WOODCUTTER",buf,CHASM_TOOL_WOODCUTTER,1e9f);
    }
    int in_camp = ChasmInCamp(z,e,settlement);
    if (in_camp > 0){
        char buf[32];
        snprintf(buf,sizeof(buf),"%i IN THE CAMP",in_camp);
        hint("build_houses",5,"BUILD HOUSES",buf,CHASM_TOOL_HOUSE,1e9f);
    }
    //A store only once something is made: until then there is nothing to keep, and the camp holds the supplies.
    if (!placed[ZONE_KIND_STORE] && f_producing){
        hint("build_store",6,"BUILD A STORE","",CHASM_TOOL_STORE,1e9f);
    }

    //7-8. What stands and cannot work as it is: a place nobody fills, a woodcutter with nowhere to pile.
    std::vector<int> working(n,0);
    std::vector<int> living(n,0);
    int builders = 0;
    for (const EconomyWorker& k : e.workers){
        if (k.building && k.building < n){
            working[k.building]++;
        }
        builders += (k.job == WORKER_JOB_BUILDER && ours(k.building)) ? 1 : 0;     //its own: none come from another
        if (k.house && k.house < n){
            living[k.house]++;
        }
    }
    for (size_t id = 1; id < n; id++){
        const ZoneBuilding& b = z.buildings[id];
        if (b.size <= 0 || !ours((uint32_t)id)){
            continue;
        }
        bool f_workplace =b.kind == ZONE_KIND_WOODCUTTER || b.kind == ZONE_KIND_WATER || b.kind == ZONE_KIND_BUILDERS
                           || b.kind == ZONE_KIND_RANGE || b.kind == ZONE_KIND_TOWER || b.kind == ZONE_KIND_ROPEWALK
                           || b.kind == ZONE_KIND_STONECUTTER || b.kind == ZONE_KIND_MINE || b.kind == ZONE_KIND_STEAM
                           || b.kind == ZONE_KIND_CONDENSER || b.kind == ZONE_KIND_SAIL_LOFT;
        if (f_workplace && stands[id] && id < e.places.size() && e.places[id] > 0 && working[id] == 0){
            ChasmIssue i;
            i.kind = "nobody_at";
            i.tier = ISSUE_STUCK;
            i.order = (float)id;
            i.text = std::string("NOBODY AT ") + WorkplaceName(b.kind);
            i.building = (uint32_t)id;
            out.push_back(i);
        }
        //A guard tower short of its stock with no builders to keep it full (defence_plan.md, step 7): only they do.
        if (b.kind == ZONE_KIND_TOWER && builders == 0 && EconomyTowerUp(z,first_plot[id])){
            std::array<int,GOOD_COUNT> lack = EconomyTowerShort(e,*e.world,(uint32_t)id,first_plot[id]);
            if (lack[GOOD_WATER] > 0 || lack[GOOD_PROVISIONS] > 0){
                ChasmIssue i;
                i.kind = "tower_unkept";
                i.tier = ISSUE_STUCK;
                i.order = (float)id;
                i.text = "TOWER: NO BUILDERS TO KEEP IT";
                i.value = "RUNNING DOWN";
                i.building = (uint32_t)id;
                out.push_back(i);
            }
        }
        //A hut standing with no room for a log - too small to keep one indoors, and no lot (woodcutting_plan.md).
        if (b.kind == ZONE_KIND_WOODCUTTER && stands[id] && id < e.pile_room.size() && id < e.log_wood.size()
            && e.pile_room[id] < e.log_wood[id]){
            ChasmIssue i;
            i.kind = "no_room";
            i.tier = ISSUE_STUCK;
            i.order = (float)id;
            i.text = "WOODCUTTER: NO ROOM FOR WOOD";
            i.building = (uint32_t)id;
            out.push_back(i);
        }
        //A zeppelin dock that stands but no zeppelin fits on (steam_plan.md step 5): painted too narrow, or crooked.
        if (b.kind == ZONE_KIND_DOCK && stands[id] && e.world){
            ZoneDockLie lie;
            if (ZoneDockFrame(*e.world,z,(uint32_t)id,lie) && !lie.f_fits){
                ChasmIssue i;
                i.kind = "dock_small";
                i.tier = ISSUE_STUCK;
                i.order = (float)id;
                i.text = "DOCK: TOO SMALL FOR A ZEPPELIN";
                i.building = (uint32_t)id;
                out.push_back(i);
            }
        }
        //A ropewalk painted too short to work (goods_plan.md) - placed, as a drag lays a plot at a time, but idle.
        if (b.kind == ZONE_KIND_ROPEWALK && stands[id] && b.size < ECONOMY_ROPEWALK_MIN_PLOTS){
            ChasmIssue i;
            i.kind = "ropewalk_short";
            i.tier = ISSUE_STUCK;
            i.order = (float)id;
            i.text = "ROPEWALK: TOO SHORT TO WORK";
            i.building = (uint32_t)id;
            out.push_back(i);
        }
        /*
            A store or a workplace SHUT IN, as a house can be (EconomyShutIn, EconomyDoorMatters; the user's, 2026-10-24):
            nothing is brought to it or fetched from it. A woodcutter's hut walled in by another kept a pile nobody could
            reach, and nothing said so.
        */
        if (EconomyDoorMatters(b.kind) && EconomyShutIn(e,(uint32_t)id)){
            ChasmIssue i;
            i.kind = "shut_in";
            i.tier = ISSUE_STUCK;
            i.order = (float)id;
            i.text = std::string(WorkplaceName(b.kind) + 4) + " SHUT IN";     //"THE WOODCUTTER'S HUT" less its "THE "
            i.value = "NO WAY IN";
            i.building = (uint32_t)id;
            out.push_back(i);
        }
        /*
            9-10, the houses (the user's, 2026-10-09; lifeline_plan.md): one shut in on every side, with no way in
            (EconomyShutIn) - red, at the very top, with anyone inside, who cannot get out; and one with more
            people than it holds, a child born into a full house, wanting a storey or a plot more.
        */
        if (b.kind == ZONE_KIND_HOUSE){
            if (EconomyShutIn(e,(uint32_t)id)){
                ChasmIssue i;
                i.kind = "house_shut_in";
                i.tier = living[id] ? ISSUE_DANGER : ISSUE_STUCK;
                i.order = living[id] ? 0.0f : (float)id;
                i.text = "HOUSE SHUT IN";
                if (living[id]){
                    i.value = std::to_string(living[id]) + " INSIDE";
                }
                i.building = (uint32_t)id;
                out.push_back(i);
            }else if (stands[id] && id < e.capacity.size() && living[id] > e.capacity[id]){
                ChasmIssue i;
                i.kind = "house_crowded";
                i.tier = ISSUE_STUCK;
                i.order = (float)id;
                i.text = "HOUSE CROWDED";
                i.value = std::to_string(living[id]) + " IN ROOM FOR " + std::to_string(e.capacity[id]);
                i.building = (uint32_t)id;
                out.push_back(i);
            }
        }
    }

    /*
        13. Land LYING FALLOW (land_plan.md; the user's, 2026-10-16): a field standing whose share has no farmer, so
        nothing is sown and nothing grows. One row a land, since the reason is its owner's: the camp farms
        ECONOMY_CAMP_FARMERS shares and hands its land to the first house lived in near it; a family has fewer free
        hands than its land has places; a field nobody can walk to has no owner at all. A click selects its first
        field, whose card says why and has "Give to:".
    */
    std::vector<int> fallow(n,0);
    std::vector<uint32_t> fallow_first(n,0);
    for (const EconomyField& f : e.fields){
        uint32_t id = f.building;
        if (id >= n || z.buildings[id].kind != ZONE_KIND_FIELD || z.buildings[id].size <= 0 || !stands[id]
            || (id < e.farmer_of.size() && e.farmer_of[id]) || !ours(id)){
            continue;
        }
        uint32_t o = (f.owner < n) ? f.owner : 0;
        fallow[o]++;
        fallow_first[o] = fallow_first[o] ? fallow_first[o] : id;
    }
    for (size_t o = 0; o < n; o++){
        if (!fallow[o]){
            continue;
        }
        ChasmIssue i;
        i.kind = "field_fallow";
        i.tier = ISSUE_STUCK;
        i.order = (float)fallow_first[o];
        i.text = (fallow[o] == 1) ? "FIELD LIES FALLOW" : std::to_string(fallow[o]) + " FIELDS LIE FALLOW";
        if (!o){
            i.value = (fallow[o] == 1) ? "NOBODY CAN REACH IT" : "NOBODY CAN REACH THEM";
        }else{
            i.value = (z.buildings[o].kind == ZONE_KIND_CAMP) ? "BUILD HOUSES" : "TOO FEW HANDS";
        }
        i.building = fallow_first[o];
        out.push_back(i);
    }

    /*
        11. A site that takes ROPE when the colony has too little and makes none (goods_plan.md; the user's: no
        first rope - "you have to set up production for it"): pointed at the first link missing, hemp - a field to
        choose it on, or the field tool with none - then a ropewalk. Once both are there the rope is coming, and
        the ropewalk's own rows say if it cannot work. One row for every such site: the advice is the same.
    */
    int rope_wanted = 0;
    uint32_t rope_site = 0;
    for (size_t id = 1; id < n; id++){
        int left = ours((uint32_t)id) ? rope_cost[id] - EconomySiteBrought(e,(uint32_t)id,GOOD_ROPE) : 0;
        if (left > 0){
            rope_wanted += left;
            rope_site = rope_site ? rope_site : (uint32_t)id;
        }
    }
    int chain = rope_wanted > 0 ? EconomyRopeChain(e,z) : ECONOMY_ROPE_MAKING;
    if (chain != ECONOMY_ROPE_MAKING && held_here(GOOD_ROPE) < rope_wanted){
        ChasmIssue i;
        i.kind = "site_needs_rope";
        i.tier = ISSUE_STUCK;
        i.order = (float)rope_site;
        std::string kind = ZoneKindName(z.buildings[rope_site].kind);
        std::transform(kind.begin(),kind.end(),kind.begin(),::toupper);
        i.text = kind + ": NEEDS ROPE";
        if (chain == ECONOMY_ROPE_NO_HEMP){
            i.value = "GROW HEMP";
            uint32_t field = 0;
            for (size_t id = 1; id < n; id++){
                const ZoneBuilding& b = z.buildings[id];
                if (b.kind == ZONE_KIND_FIELD && b.size > 0 && ours((uint32_t)id) && (!field || (stands[id] && !stands[field]))){
                    field = (uint32_t)id;
                }
            }
            if (field){
                i.building = field;     //its card has the crop to choose
            }else{
                i.tool = CHASM_TOOL_FIELD;
            }
        }else{
            i.value = "BUILD A ROPEWALK";
            i.tool = CHASM_TOOL_ROPEWALK;
        }
        out.push_back(i);
    }

    /*
        11b. A site that takes STONE the colony cannot come by (stone_plan.md step 5): more wanted than the stores hold and
        rubble lies on its own land. Since 2026-10-15 all rubble lies down on the balconies, so the advice is the first
        link missing: a winch down to it, then a stonecutter's hut to break the rocks - unless that is the very site
        waiting - and else rubble further out, to PICK UP. One row for every such site, as for rope.
    */
    int stone_wanted = 0;
    uint32_t stone_site = 0;
    for (size_t id = 1; id < n; id++){
        int left = ours((uint32_t)id) ? stone_cost[id] - EconomySiteBrought(e,(uint32_t)id,GOOD_STONE) : 0;
        if (left > 0){
            stone_wanted += left;
            stone_site = stone_site ? stone_site : (uint32_t)id;
        }
    }
    if (stone_wanted > 0){
        int stone_have = held_here(GOOD_STONE);
        for (const EconomyCrate& c : e.crates){
            bool f_ours = f_all && c.plot >= 0 && c.plot < (int)e.settlement.size() && e.settlement[c.plot] != 0;
            stone_have += f_ours ? c.stock[GOOD_STONE] : 0;
        }
        if (stone_have < stone_wanted){
            ChasmIssue i;
            i.kind = "site_needs_stone";
            i.tier = ISSUE_STUCK;
            i.order = (float)stone_site;
            std::string kind = ZoneKindName(z.buildings[stone_site].kind);
            std::transform(kind.begin(),kind.end(),kind.begin(),::toupper);
            i.text = kind + ": NEEDS STONE";
            bool f_hut = z.buildings[stone_site].kind == ZONE_KIND_STONECUTTER;
            //A tool still waiting for its discovery is not offered: the chasm to find first, and with no boulder found
            //the stonecutter's hut is passed over.
            if (!placed[ZONE_KIND_WINCH] && f_locked(CHASM_TOOL_WINCH)){
                i.value = "FIND THE CHASM";
            }else if (!placed[ZONE_KIND_WINCH]){
                i.value = "BUILD A WINCH TO THE RUBBLE";
                i.tool = CHASM_TOOL_WINCH;
            }else if (!f_hut && !placed[ZONE_KIND_STONECUTTER] && !f_locked(CHASM_TOOL_STONECUTTER)){
                i.value = "BUILD A STONECUTTER'S HUT";
                i.tool = CHASM_TOOL_STONECUTTER;
            }else{
                i.value = "PICK UP RUBBLE FURTHER OUT";
                i.building = stone_site;
            }
            out.push_back(i);
        }
    }

    //11c. A mine whose seam is dug out (stone_plan.md step 5): its miner is free, and it stands until erased.
    for (size_t id = 1; id < n; id++){
        const ZoneBuilding& b = z.buildings[id];
        if (b.kind != ZONE_KIND_MINE || b.size <= 0 || !stands[id] || first_plot[id] < 0 || !e.world->deposits
            || !ours((uint32_t)id)){
            continue;
        }
        int d = e.world->deposits->At(first_plot[id]);
        if (d >= 0 && e.world->deposits->deposits[d].kind == DEPOSIT_SEAM && EconomyDepositLeft(e,d) <= 0){
            ChasmIssue i;
            i.kind = "mine_worked_out";
            i.tier = ISSUE_STUCK;
            i.order = (float)id;
            i.text = "MINE: WORKED OUT";
            i.building = (uint32_t)id;
            out.push_back(i);
        }
    }

    /*
        12. Roads, walls and sites with a tree in their way no woodcutter reaches over dry land (woodcutting_plan.md):
        only a woodcutter fells. A ROW A BUILDING, named, and a click selects it and takes the view there (the user's,
        2026-10-16: "it's not obvious it's the bridge") - saying what it waits for: a hut put near, or the ICE, when a
        woodcutter reaches it over the river once it freezes (a bridge's far bank, e.trees_iced) - and "over the ice
        now" while it is frozen there, a woodcutter on his way. Roads, walls, gardens and lots in one row, a plot
        each, the view to the first.
    */
    if (!e.trees_unreached.empty() && e.world){
        const ChasmWorld& w = *e.world;
        float front = CalendarSnowFrontZ(CalendarSnowCover(e.tick),w.grid->bounds_min.y,w.grid->bounds_max.y);
        bool f_snow = CalendarSnowCover(e.tick) > 0.0f;
        auto waits_for = [&](uint32_t site, const vec2& at) -> std::string {
            if (!std::binary_search(e.trees_iced.begin(),e.trees_iced.end(),site)){
                return "NO WOODCUTTER NEAR";
            }
            return (f_snow && CalendarSnowAt(at,front)) ? "OVER THE ICE NOW" : "WAITS FOR THE ICE";
        };
        int ground = 0;
        int ground_plot = -1;
        uint32_t ground_site = 0;
        for (uint32_t site : e.trees_unreached){
            if (EconomyIsGroundSite(site) ? !ours_plot(EconomyGroundSitePlot(site)) : !ours(site)){
                continue;
            }
            if (EconomyIsGroundSite(site)){
                ground_plot = (ground == 0) ? EconomyGroundSitePlot(site) : ground_plot;
                ground_site = (ground == 0) ? site : ground_site;
                ground++;
                continue;
            }
            vec2 at;
            if (site >= z.buildings.size() || !ChasmBuildingPoint(w,z,site,at)){
                continue;
            }
            ChasmIssue i;
            i.kind = "trees_unreached";
            i.tier = ISSUE_STUCK;
            i.order = 1e6f + (float)site;
            std::string kind = ZoneKindName(z.buildings[site].kind);
            std::transform(kind.begin(),kind.end(),kind.begin(),::toupper);
            i.text = kind + ": TREES IN THE WAY";
            i.value = waits_for(site,at);
            i.building = site;
            out.push_back(i);
        }
        if (ground > 0 && ground_plot >= 0 && ground_plot < (int)w.grid->fine.pos.size()){
            ChasmIssue i;
            i.kind = "trees_unreached";
            i.tier = ISSUE_STUCK;
            i.order = 2e6f;
            i.text = "TREES IN THE WAY: " + waits_for(ground_site,w.grid->fine.pos[ground_plot]);
            i.value = std::to_string(ground) + (ground == 1 ? " PLACE" : " PLACES");
            i.plot = ground_plot;
            out.push_back(i);
        }
    }

    std::stable_sort(out.begin(),out.end(),[](const ChasmIssue& a, const ChasmIssue& b){
        if (a.tier != b.tier){
            return a.tier < b.tier;
        }
        return a.order < b.order;
    });
}

//Where a building is, for the view to go to: the middle of its plots, or of a field's cells. False if it is gone.
bool ChasmBuildingPoint(const ChasmWorld& w, const ZoneState& z, uint32_t id, vec2& out){
    vec2 sum(0.0f,0.0f);
    int count = 0;
    for (size_t v = 0; v < z.building.size(); v++){
        if (z.building[v] == id){
            sum += w.grid->fine.pos[v];
            count++;
        }
    }
    for (size_t q = 0; count == 0 && q < z.field.size(); q++){
        if (z.field[q] == id){
            for (int j = 0; j < 4; j++){
                sum += w.grid->FineQuadCentre((int)q * 4 + j) * 0.25f;
            }
            count++;
        }
    }
    if (count == 0){
        return false;
    }
    out = sum * (1.0f / (float)count);
    return true;
}

/*
    RENDER THREAD, from DrawOverlay in play mode: the first ISSUES_SHOWN of the list over the tools' grid, its
    right edge on the grid's and at least as wide - `bottom` is the top of the grid's stack. Each row a mark
    in its tier's colour (red a deadline close, amber a countdown or something stuck, dim a next step), what
    to do, and at the right its value; a countdown drains a line under it. A click on a row selects its place
    and takes the view there (OVERLAY_HIT_BUILDING), or takes up its tool (OVERLAY_HIT_TAKE_TOOL). Returns its
    top - `bottom` itself with nothing to say.
*/
uint32_t ApplicationChasm::ViewIssues(const ZoneState& z, const EconomyState& e, std::vector<ChasmIssue>& out){
    uint32_t view = ViewColony(e);
    ChasmIssues(z,e,out,ToolsLocked(),view);
    if (view == ECONOMY_EVERY_SETTLEMENT){
        return 0;
    }
    uint32_t number = EconomyColonyNumber(e,view);
    for (ChasmIssue& i : out){
        i.colony = number;
    }
    std::vector<ChasmIssue> theirs;
    for (const EconomySettlement& st : e.settlements){
        if (st.id == view){
            continue;
        }
        ChasmIssues(z,e,theirs,ToolsLocked(),st.id);
        for (ChasmIssue& i : theirs){
            if (i.tier == ISSUE_DANGER){
                i.text = "COLONY " + std::to_string(st.colony) + ": " + i.text;
                i.colony = st.colony;
                out.push_back(i);
            }
        }
    }
    std::stable_sort(out.begin(),out.end(),[](const ChasmIssue& a, const ChasmIssue& b){
        return (a.tier != b.tier) ? a.tier < b.tier : a.order < b.order;
    });
    return number;
}

float ApplicationChasm::DrawIssues(float s, float bottom){
    std::shared_ptr<const EconomyState> e = GetEconomy();
    std::shared_ptr<const ZoneState> z = GetZones();
    if (!e || !z){
        return bottom;
    }
    std::vector<ChasmIssue> issues;
    uint32_t colony = ViewIssues(*z,*e,issues);
    if (issues.empty()){
        return bottom;
    }
    int shown = std::min(ISSUES_SHOWN,(int)issues.size());
    PlayPick sel;
    {
        std::lock_guard<std::mutex> lock(pick_mutex);
        sel = play_selected;
    }
    int tool = paint_tool.load();
    const float size = 14.0f * s;
    const float title_size = 12.0f * s;
    const float title_h = 18.0f * s;
    const float line_h = 26.0f * s;
    const float pad = 12.0f * s;
    const float mark = 8.0f * s;
    const float mark_gap = 9.0f * s;
    const float col_gap = 14.0f * s;
    float w = build_bar_max.x - build_bar_min.x;
    for (int r = 0; r < shown; r++){
        float row = pad * 2.0f + mark + mark_gap + overlay->MeasureText(issues[r].text.c_str(),size).x;
        if (!issues[r].value.empty()){
            row += col_gap + overlay->MeasureText(issues[r].value.c_str(),size).x;
        }
        w = std::max(w,row);
    }
    float h = pad * 0.8f + title_h + line_h * (float)shown + pad * 0.4f;
    vec2 p1(build_bar_max.x,bottom);
    vec2 p0(p1.x - w,bottom - h);
    overlay->AddRect(p0,p1,9.0f * s,HUD_PANEL);
    overlay_hits_drawing.push_back(OverlayHit{p0,p1,OVERLAY_HIT_BLOCK,0});
    build_stack_left = std::min(build_stack_left,p0.x);     //wider than the grid, for the card to keep clear of
    float title_y = p0.y + pad * 0.8f + 11.0f * s;
    std::string title = colony ? "NEEDS ATTENTION, COLONY " + std::to_string(colony) : std::string("NEEDS ATTENTION");
    overlay->AddText(title.c_str(),vec2(p0.x + pad,title_y),title_size,HUD_TEXT_DIM,UI_ALIGN_LEFT);
    if ((int)issues.size() > shown){
        char more[24];
        snprintf(more,sizeof(more),"+%i MORE",(int)issues.size() - shown);
        overlay->AddText(more,vec2(p1.x - pad,title_y),title_size,HUD_TEXT_DIM,UI_ALIGN_RIGHT);
    }
    for (int r = 0; r < shown; r++){
        const ChasmIssue& i = issues[r];
        float top = p0.y + pad * 0.8f + title_h + line_h * (float)r;
        float mid = top + line_h * 0.5f;
        bool f_lit = (i.building && sel.kind == PLAY_PICK_BUILDING && sel.id == i.building) || (i.tool >= 0 && tool == i.tool);
        if (f_lit){
            overlay->AddRect(vec2(p0.x + 4.0f * s,top + 1.0f * s),vec2(p1.x - 4.0f * s,top + line_h - 1.0f * s),6.0f * s,
                             HudFaded(HUD_SELECT,60));
        }
        uint32_t colour = (i.tier == ISSUE_DANGER) ? HUD_DANGER : (i.tier == ISSUE_NEXT) ? HUD_TEXT_DIM : HUD_AMBER;
        overlay->AddRect(vec2(p0.x + pad,mid - mark * 0.5f),vec2(p0.x + pad + mark,mid + mark * 0.5f),mark * 0.5f,colour);
        float text_x = p0.x + pad + mark + mark_gap;
        overlay->AddText(i.text.c_str(),vec2(text_x,mid + 5.0f * s),size,(i.tier == ISSUE_DANGER) ? HUD_DANGER : HUD_TEXT,
                         UI_ALIGN_LEFT);
        if (!i.value.empty()){
            overlay->AddText(i.value.c_str(),vec2(p1.x - pad,mid + 5.0f * s),size,(i.tier == ISSUE_DANGER) ? HUD_DANGER : HUD_TEXT_DIM,
                             UI_ALIGN_RIGHT);
        }
        if (i.left >= 0.0f){
            //The countdown: what is left of it, draining from the right, under the row's words.
            vec2 line0(text_x,mid + 9.0f * s);
            vec2 line1(p1.x - pad,line0.y + 2.0f * s);
            overlay->AddRect(line0,line1,1.0f * s,HudFaded(HUD_AMBER,50));
            overlay->AddRect(line0,vec2(line0.x + (line1.x - line0.x) * i.left,line1.y),1.0f * s,HUD_AMBER);
        }
        if (i.building){
            overlay_hits_drawing.push_back(OverlayHit{vec2(p0.x,top),vec2(p1.x,top + line_h),OVERLAY_HIT_BUILDING,(int)i.building});
        }else if (i.plot >= 0){
            overlay_hits_drawing.push_back(OverlayHit{vec2(p0.x,top),vec2(p1.x,top + line_h),OVERLAY_HIT_PLACE,i.plot});
        }else if (i.tool >= 0){
            overlay_hits_drawing.push_back(OverlayHit{vec2(p0.x,top),vec2(p1.x,top + line_h),OVERLAY_HIT_TAKE_TOOL,i.tool});
        }
    }
    return p0.y;
}

#ifdef USE_MCP
void ApplicationChasm::RegisterIssueTools(){
    MCPServer::Get()->RegisterTool("chasm_issues",
        "NEEDS ATTENTION (docs/issues_plan.md): everything the panel over the tools' grid would list, in its order - "
        "the panel shows the first five, in play mode. Each: kind (choose_crop, build_water, build_field, "
        "build_woodcutter, build_houses, build_store, find_river (play mode, no river found yet), nobody_at, no_room, ropewalk_short, shut_in, house_shut_in, house_crowded, field_fallow, tower_unkept, site_needs_rope, site_needs_stone, mine_worked_out, trees_unreached), tier (danger: a deadline under two days; "
        "countdown: a choice the game makes when it runs out; stuck; next: a next step), text and value as drawn, "
        "left (a countdown's share still to run, 1 to 0) and wait/grace ticks for a field, building (what a click "
        "selects), plot (where a click takes the view: trees_unreached's roads and walls) or tool (what it takes up). "
        "trees_unreached is a row a building, its value NO WOODCUTTER NEAR, WAITS FOR THE ICE (reached over the river "
        "once it freezes) or OVER THE ICE NOW. BY COLONY (docs/settlements_plan.md, step 3): with more than one, the "
        "colony in view's - under the camera, else the cursor, else the last shown - as `colony` (0: one colony, all of "
        "it), each row's `colony` too, and every other colony's dangers, their text named COLONY N. View only. "
        "include_screenshot as elsewhere.",
        json{
            {"type","object"},
            {"properties",{
                {"include_screenshot",{{"type","boolean"}}},
                {"include_ui",{{"type","boolean"}}}
            }}
        },
        [this](const json& args) -> json {
            std::shared_ptr<const EconomyState> e = GetEconomy();
            std::shared_ptr<const ZoneState> z = GetZones();
            json list = json::array();
            uint32_t colony = 0;
            if (e && z){
                std::vector<ChasmIssue> issues;
                colony = ViewIssues(*z,*e,issues);
                for (const ChasmIssue& i : issues){
                    json one{{"kind",i.kind},{"tier",TierName(i.tier)},{"text",i.text},{"value",i.value},{"colony",i.colony}};
                    if (i.left >= 0.0f){
                        one["left"] = i.left;
                        const EconomyField* f = EconomyFieldOf(*e,i.building);
                        one["wait"] = f ? f->wait : 0;
                        one["grace"] = ECONOMY_CROP_GRACE;
                    }
                    if (i.building){
                        one["building"] = i.building;
                    }
                    if (i.plot >= 0){
                        one["plot"] = i.plot;
                    }
                    if (i.tool >= 0){
                        one["tool"] = i.tool;
                    }
                    list.push_back(one);
                }
            }
            json result{{"issues",list},{"shown",std::min(ISSUES_SHOWN,(int)list.size())},{"colony",colony}};
            return MaybeAttachScreenshot(result,args.value("include_screenshot",false),args.value("include_ui",true));
        });
}
#endif
