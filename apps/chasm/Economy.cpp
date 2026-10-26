#include "Economy.h"
#include "Calendar.h"
#include "ChasmWorld.h"
#include "Climate.h"
#include "Walkers.h"
#include "RoadMesh.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>

const char* GoodName(int good){
    switch (good){
        case GOOD_WOOD:     return "wood";
        case GOOD_WATER:    return "water";
        case GOOD_WHEAT:    return "wheat";
        case GOOD_GREENS:   return "greens";
        case GOOD_BEANS:    return "beans";
        case GOOD_PROVISIONS: return "provisions";
        case GOOD_HEMP:     return "hemp";
        case GOOD_ROPE:     return "rope";
        case GOOD_FRUIT:    return "fruit";
        case GOOD_STONE:    return "stone";
        case GOOD_FLOATSTONE: return "floatstone";
        case GOOD_STEAM:    return "steam";
        case GOOD_CANVAS:   return "canvas";
        default:            return "?";
    }
}

int GoodByName(const std::string& name){
    for (int g = 0; g < GOOD_COUNT; g++){
        if (name == GoodName(g)){
            return g;
        }
    }
    return -1;
}

const char* EconomyWorkerStateName(int state){
    switch (state){
        case WORKER_AT_HOME:    return "at_home";
        case WORKER_TO_WORK:    return "to_work";
        case WORKER_WORKING:    return "working";
        case WORKER_TO_STORE:   return "to_store";
        case WORKER_UNLOADING:  return "unloading";
        case WORKER_TO_HOME:    return "to_home";
        case WORKER_HOLDING:    return "holding";
        case WORKER_TO_FETCH:   return "to_fetch";
        case WORKER_FETCHING:   return "fetching";
        case WORKER_TO_SITE:    return "to_site";
        case WORKER_BUILDING:   return "building";
        case WORKER_TO_ROAD:    return "to_road";
        case WORKER_CLEARING:   return "clearing";
        case WORKER_LAYING:     return "laying";
        case WORKER_TO_SUPPLY:  return "to_supply";
        case WORKER_SUPPLYING:  return "supplying";
        case WORKER_TO_SOW:     return "to_sow";
        case WORKER_SOWING:     return "sowing";
        case WORKER_TO_PLAY:    return "to_play";
        case WORKER_PLAYING:    return "playing";
        case WORKER_TO_POST:    return "to_post";
        case WORKER_ON_POST:    return "on_post";
        case WORKER_TO_RANGE:   return "to_range";
        case WORKER_TRAINING:   return "training";
        case WORKER_TO_CRATE:   return "to_crate";
        case WORKER_LIFTING:    return "lifting";
        case WORKER_TO_INPUT:   return "to_input";
        case WORKER_TAKING_INPUT: return "taking_input";
        case WORKER_TO_WORKSHOP: return "to_workshop";
        case WORKER_MAKING:     return "making";
        case WORKER_TO_GET:     return "to_get";
        case WORKER_GETTING:    return "getting";
        case WORKER_TO_PANTRY:  return "to_pantry";
        default:                return "?";
    }
}

const char* EconomyJobName(int job){
    switch (job){
        case WORKER_JOB_WOODCUTTER: return "woodcutter";
        case WORKER_JOB_FARMER:     return "farmer";
        case WORKER_JOB_WATER:      return "water_carrier";
        case WORKER_JOB_BUILDER:    return "builder";
        case WORKER_JOB_ARCHER:     return "archer";
        case WORKER_JOB_GUARD:      return "guard";
        case WORKER_JOB_ROPEMAKER:  return "ropemaker";
        case WORKER_JOB_PICKER:     return "picker";
        case WORKER_JOB_STONECUTTER: return "stonecutter";
        case WORKER_JOB_MINER:      return "miner";
        case WORKER_JOB_STEAM:      return "steam_carrier";
        case WORKER_JOB_CONDENSER:  return "condenser_hand";
        case WORKER_JOB_SAILMAKER:  return "sailmaker";
        case WORKER_JOB_NONE:       return "none";
        case WORKER_JOB_PLAYING:    return "playing";
        case WORKER_JOB_BABY:       return "baby";
        case WORKER_JOB_CARING:     return "caring";
        case WORKER_JOB_OLD:        return "old";
        default:                    return "?";
    }
}

const char* SkillName(int skill){
    switch (skill){
        case SKILL_STRENGTH:    return "strength";
        case SKILL_PRECISION:   return "precision";
        case SKILL_HUSBANDRY:   return "husbandry";
        case SKILL_INGENUITY:   return "ingenuity";
        default:                return "?";
    }
}

int EconomyJobOf(int kind){
    switch (kind){
        case ZONE_KIND_WOODCUTTER:  return WORKER_JOB_WOODCUTTER;
        case ZONE_KIND_HOUSE:       return WORKER_JOB_FARMER;      //its land's (land_plan.md): a field employs nobody
        case ZONE_KIND_CAMP:        return WORKER_JOB_FARMER;
        case ZONE_KIND_WATER:       return WORKER_JOB_WATER;
        case ZONE_KIND_BUILDERS:    return WORKER_JOB_BUILDER;
        case ZONE_KIND_RANGE:       return WORKER_JOB_ARCHER;      //range_plan.md: its trainees
        case ZONE_KIND_TOWER:       return WORKER_JOB_GUARD;       //defence_plan.md: its guards
        case ZONE_KIND_ROPEWALK:    return WORKER_JOB_ROPEMAKER;   //goods_plan.md
        case ZONE_KIND_STONECUTTER: return WORKER_JOB_STONECUTTER; //stone_plan.md
        case ZONE_KIND_MINE:        return WORKER_JOB_MINER;       //stone_plan.md step 5
        case ZONE_KIND_STEAM:       return WORKER_JOB_STEAM;       //steam_plan.md
        case ZONE_KIND_CONDENSER:   return WORKER_JOB_CONDENSER;   //steam_plan.md step 3
        case ZONE_KIND_SAIL_LOFT:   return WORKER_JOB_SAILMAKER;   //steam_plan.md step 4
        default:                    return WORKER_JOB_NONE;
    }
}

int EconomyRopewalkPlots(const ZoneState& z, uint32_t id){
    if (id >= z.buildings.size() || z.buildings[id].kind != ZONE_KIND_ROPEWALK || z.buildings[id].size <= 0){
        return 0;
    }
    int n = 0;
    for (size_t v = 0; v < z.building.size(); v++){
        n += (z.building[v] == id && z.standing[v] > 0) ? 1 : 0;
    }
    return n;
}

float EconomyRopewalkPace(const ZoneState& z, uint32_t id){
    int plots = EconomyRopewalkPlots(z,id);
    return (plots >= ECONOMY_ROPEWALK_MIN_PLOTS) ? (float)plots / (float)ECONOMY_ROPEWALK_MIN_PLOTS : 0.0f;
}

static const EconomyHempWork hemp_works[] = {
    {ZONE_KIND_ROPEWALK,WORKER_JOB_ROPEMAKER,GOOD_ROPE,ECONOMY_ROPE_HEMP,ECONOMY_ROPE_SECONDS,ECONOMY_ROPEWALK_PILE},
    {ZONE_KIND_SAIL_LOFT,WORKER_JOB_SAILMAKER,GOOD_CANVAS,ECONOMY_CANVAS_HEMP,ECONOMY_CANVAS_SECONDS,ECONOMY_SAIL_LOFT_PILE},
};

const EconomyHempWork* EconomyHempWorkOf(int job){
    for (const EconomyHempWork& h : hemp_works){
        if (h.job == job){
            return &h;
        }
    }
    return NULL;
}

const EconomyHempWork* EconomyHempWorkAt(int kind){
    for (const EconomyHempWork& h : hemp_works){
        if (h.kind == kind){
            return &h;
        }
    }
    return NULL;
}

float EconomyHempPace(const ZoneState& z, uint32_t id){
    if (id >= z.buildings.size() || z.buildings[id].size <= 0){
        return 0.0f;
    }
    if (z.buildings[id].kind != ZONE_KIND_SAIL_LOFT){
        return EconomyRopewalkPace(z,id);
    }
    for (size_t v = 0; v < z.building.size(); v++){
        if (z.building[v] == id && z.standing[v] > 0){
            return 1.0f;
        }
    }
    return 0.0f;
}

int EconomyGoodHeld(const EconomyState& e, int good){
    if (good < 0 || good >= GOOD_COUNT){
        return 0;
    }
    int held = 0;
    for (const std::array<int,GOOD_COUNT>& s : e.stock){
        held += s[good];
    }
    for (const EconomyCrate& c : e.crates){
        held += c.stock[good];
    }
    for (const EconomyWorker& k : e.workers){
        held += (k.carry_good == good) ? std::max(0,k.carry) : 0;
    }
    return held;
}

//--- Events and the history (Economy.h; sound_plan.md, statistics_plan.md) ---------------------------

const char* EconomyEventName(int kind){
    switch (kind){
        case EVENT_NOTHING_TO_TAKE:     return "nothing_to_take";
        case EVENT_RAN_OUT:             return "ran_out";
        case EVENT_NO_TENT:             return "no_tent";
        case EVENT_SEAM_SEEN:           return "seam_seen";
        case EVENT_BORN:                return "born";
        case EVENT_DIED:                return "died";
        case EVENT_TREE_FELLED:         return "tree_felled";
        case EVENT_ROCK_BROKEN:         return "rock_broken";
        case EVENT_STOREY_RAISED:       return "storey_raised";
        case EVENT_BUILDING_FINISHED:   return "building_finished";
        case EVENT_HARVEST:             return "harvest";
        case EVENT_SETTLED:             return "settled";
        case EVENT_DISCOVERED:          return "discovered";
        case EVENT_FROST_TOOK:          return "frost_took";
        case EVENT_GREW_UP:             return "grew_up";
        case EVENT_EXPECTING:           return "expecting";
        case EVENT_BOULDER_FOUND:       return "boulder_found";
        case EVENT_BALCONY_REACHED:     return "balcony_reached";
        case EVENT_GOAL_MET:            return "goal_met";
        case EVENT_GOAL_FAILED:         return "goal_failed";
        case EVENT_DEMO_COMPLETE:       return "demo_complete";
        case EVENT_COLONY_LOST:         return "colony_lost";
        case EVENT_WALL_BROKEN:         return "wall_broken";
        case EVENT_GATE_BROKEN:         return "gate_broken";
        case EVENT_BREACHED:            return "breached";
        case EVENT_ZOMBIE_FELLED:       return "zombie_felled";
        case EVENT_RANKED:              return "ranked";
        case EVENT_GREW_OLD:            return "grew_old";
        case EVENT_ZEPPELIN_STAGE:      return "zeppelin_stage";
        default:                        return "unknown";
    }
}

int EconomyEventByName(const std::string& name){
    for (int k = 0; k < EVENT_COUNT; k++){
        if (name == EconomyEventName(k)){
            return k;
        }
    }
    return -1;
}

//What is worth remembering one by one: who came and went, grew up or old and was expected, what was reaped, raised and lost
//to the frost, where the colony spread, what it found, and who rose a rank - not what happens tens of times a day,
//which the ledger counts, nor the scouts' troubles. The DISCOVERIES (demo_plan.md) are read from here, so what they
//need is kept - and the demo's goals and its end, each made once, by whether the journal has it yet.
bool EconomyJournaled(int kind){
    return kind == EVENT_BORN || kind == EVENT_DIED || kind == EVENT_HARVEST || kind == EVENT_BUILDING_FINISHED
           || kind == EVENT_SETTLED || kind == EVENT_SEAM_SEEN || kind == EVENT_DISCOVERED || kind == EVENT_FROST_TOOK
           || kind == EVENT_GREW_UP || kind == EVENT_EXPECTING || kind == EVENT_BOULDER_FOUND
           || kind == EVENT_BALCONY_REACHED || kind == EVENT_GOAL_MET || kind == EVENT_GOAL_FAILED
           || kind == EVENT_DEMO_COMPLETE || kind == EVENT_COLONY_LOST
           || kind == EVENT_WALL_BROKEN || kind == EVENT_GATE_BROKEN || kind == EVENT_BREACHED || kind == EVENT_RANKED
           || kind == EVENT_GREW_OLD || kind == EVENT_ZEPPELIN_STAGE;
}

//--- Kills and ranks (Economy.h, KILLS and RANKS; ranks_plan.md) ----------------------------------------------------

/*
    By rank, none first: the kills each takes - a kill a rank for now (the user's, 2026-10-19), to test with, each its
    own so any one can be set apart later - how much quicker it fights, and the percent of its arrows' hits that kill
    outright. Current tuning, in ranks_plan.md's table.
*/
const int rank_kills[RANK_COUNT] = {0,1,2,3};
const float rank_pace[RANK_COUNT] = {1.0f,1.05f,1.1f,1.15f};
const int rank_one_shot[RANK_COUNT] = {0,10,20,30};

int EconomyRank(const std::array<int,KILL_HOWS>& kills){
    int total = EconomyKills(kills);
    int rank = RANK_NONE;
    for (int r = RANK_NONE + 1; r < RANK_COUNT; r++){
        rank = (total >= rank_kills[r]) ? r : rank;
    }
    return rank;
}

const char* EconomyRankName(int rank){
    switch (rank){
        case RANK_NOVICE:      return "Novice";
        case RANK_EXPERT:      return "Expert";
        case RANK_MASTER:     return "Master";
        default:                return "";
    }
}

int EconomyRankKills(int rank){
    return (rank > RANK_NONE && rank < RANK_COUNT) ? rank_kills[rank] : 0;
}

float EconomyRankPace(int rank){
    return (rank > RANK_NONE && rank < RANK_COUNT) ? rank_pace[rank] : 1.0f;
}

int EconomyRankOneShot(int rank){
    return (rank > RANK_NONE && rank < RANK_COUNT) ? rank_one_shot[rank] : 0;
}

std::string EconomyKillsTold(const std::array<int,KILL_HOWS>& kills){
    int total = EconomyKills(kills);
    if (total <= 0){
        return std::string();
    }
    int rank = EconomyRank(kills);
    return (rank > RANK_NONE ? std::string(", ") + EconomyRankName(rank) : std::string())
           + ", " + std::to_string(total) + (total == 1 ? " kill" : " kills");
}

bool EconomyCounted(int kind){
    return kind == EVENT_TREE_FELLED || kind == EVENT_ROCK_BROKEN || kind == EVENT_STOREY_RAISED || kind == EVENT_ZOMBIE_FELLED;
}

/*
    All of each good the colony HAS: every building's stock and a camp's families' pantry, what everyone carries and
    packs, and the crates its people put down or a camp left behind - so a good does not seem to vanish while it is
    only moved. Not what lies out there to be found: rubble, a broken rock's stone, a find - the colony has those once
    someone brings them in.
*/
void EconomyDayLevels(const EconomyState& e, uint64_t tick, EconomyDay& row){
    row.people.fill(0);
    row.held.fill(0);
    /*
        BY COLONY (THE HISTORY): every colony standing has its row, and each figure goes to its colony's as well as the
        whole's - so the two cannot disagree. A row's counts are the events', kept as they came (Economy::Event).
    */
    for (EconomyDay& r : row.colonies){
        r.people.fill(0);
        r.held.fill(0);
    }
    for (const EconomySettlement& st : e.settlements){
        EconomyColonyRow(row,st.colony);
    }
    auto of = [&](uint32_t settlement) -> EconomyDay& {
        return EconomyColonyRow(row,EconomyColonyNumber(e,settlement));
    };
    auto of_plot = [&](int v) -> EconomyDay& {
        return of((v >= 0 && v < (int)e.settlement.size()) ? e.settlement[v] : 0);
    };
    for (const EconomyWorker& k : e.workers){
        EconomyDay& mine = of(EconomyPersonLand(e,k));
        int stage = std::max(0,std::min(ECONOMY_STAGES - 1,EconomyStageAt(k.born,tick)));
        row.people[stage]++;
        mine.people[stage]++;
        if (k.carry_good >= 0 && k.carry_good < GOOD_COUNT){
            row.held[k.carry_good] += std::max(0,k.carry);
            mine.held[k.carry_good] += std::max(0,k.carry);
        }
        for (int g = 0; g < GOOD_COUNT; g++){
            row.held[g] += k.pack[g];
            mine.held[g] += k.pack[g];
        }
    }
    for (const std::vector<std::array<int,GOOD_COUNT>>* stocks : {&e.stock,&e.pantry}){
        for (size_t id = 0; id < stocks->size(); id++){
            const std::array<int,GOOD_COUNT>& s = (*stocks)[id];
            if (std::none_of(s.begin(),s.end(),[](int n){ return n != 0; })){
                continue;
            }
            EconomyDay& theirs = of(EconomySettlementOf(e,(uint32_t)id));
            for (int g = 0; g < GOOD_COUNT; g++){
                row.held[g] += s[g];
                theirs.held[g] += s[g];
            }
        }
    }
    for (const EconomyCrate& c : e.crates){
        if (c.origin == CRATE_DROPPED || c.origin == CRATE_CAMP || c.origin == CRATE_SITE){
            EconomyDay& theirs = of_plot(c.plot);
            for (int g = 0; g < GOOD_COUNT; g++){
                row.held[g] += c.stock[g];
                theirs.held[g] += c.stock[g];
            }
        }
    }
    //Nobody's, and a colony gone, only while they hold anything: people, goods, or a count from today's events.
    row.colonies.erase(std::remove_if(row.colonies.begin(),row.colonies.end(),[&](const EconomyDay& r){
        bool f_standing = r.colony && EconomyColonySettlement(e,r.colony) != 0;
        bool f_any = r.felled || r.broken || r.storeys || r.zombies
                     || std::any_of(r.people.begin(),r.people.end(),[](int n){ return n != 0; })
                     || std::any_of(r.held.begin(),r.held.end(),[](int n){ return n != 0; });
        return !f_standing && !f_any;
    }),row.colonies.end());
}

EconomyDay& EconomyColonyRow(EconomyDay& d, uint32_t colony){
    auto it = std::lower_bound(d.colonies.begin(),d.colonies.end(),colony,
                               [](const EconomyDay& r, uint32_t c){ return r.colony < c; });
    if (it == d.colonies.end() || it->colony != colony){
        EconomyDay r;
        r.day = d.day;
        r.colony = colony;
        it = d.colonies.insert(it,r);
    }
    return *it;
}

uint32_t EconomyPersonLand(const EconomyState& e, const EconomyWorker& k){
    if (uint32_t mine = EconomyPersonSettlement(e,k)){
        return mine;
    }
    if (!e.world || !e.world->picker){
        return 0;
    }
    GridPick at = e.world->picker->Pick(k.pos);
    return (at.f_hit && at.plot >= 0 && at.plot < (int)e.settlement.size()) ? e.settlement[at.plot] : 0;
}

void EconomyForestCleared(const EconomyState& e, int& felled, int& broken){
    felled = 0;
    broken = 0;
    if (!e.world || !e.world->forest){
        return;
    }
    const std::vector<PropInstance>& props = e.world->forest->props;
    for (size_t i = 0; i < props.size() && i < e.prop_state.size(); i++){
        if (e.prop_state[i] != PROP_STATE_STANDING){
            broken += ForestIsRock(props[i].kind) ? 1 : 0;
            felled += ForestIsRock(props[i].kind) ? 0 : 1;
        }
    }
}

void Economy::Event(int kind, uint32_t person, uint32_t building, int plot, const vec2& pos, int amount, GoodsMask what){
    EconomyEvent ev;
    ev.kind = kind;
    ev.person = person;
    ev.building = building;
    ev.plot = plot;
    ev.pos = pos;
    ev.amount = amount;
    ev.what = what;
    Event(ev,now);
}

void Economy::Event(EconomyEvent ev, uint64_t tick){
    ev.tick = tick;
    if (ev.pos.x == 0.0f && ev.pos.y == 0.0f && state.world && ev.plot >= 0 && ev.plot < (int)state.world->grid->fine.pos.size()){
        ev.pos = state.world->grid->fine.pos[ev.plot];
    }
    /*
        ITS COLONY (THE HISTORY, BY COLONY): where it happened - its building's, else its plot's land, else its person's
        (EconomyPersonLand) - by the colony's lasting number; 0 on land nobody holds.
    */
    if (!ev.colony){
        uint32_t s = EconomySettlementOf(state,ev.building);
        s = (!s && ev.plot >= 0 && ev.plot < (int)state.settlement.size()) ? state.settlement[ev.plot] : s;
        for (size_t i = 0; !s && ev.person && i < state.workers.size(); i++){
            s = (state.workers[i].id == ev.person) ? EconomyPersonLand(state,state.workers[i]) : 0;
        }
        ev.colony = EconomyColonyNumber(state,s);
    }
    state.events.push_back(ev);
    if (state.events.size() > ECONOMY_EVENTS_KEPT){
        state.events.erase(state.events.begin());
    }
    state.events_made++;
    if (ev.kind >= 0 && ev.kind < EVENT_COUNT){
        state.events_by_kind[ev.kind]++;
    }
    //THE HISTORY: kept as it is made, so the journal and the telling are one.
    EconomyDay& today = state.history.today;
    if (EconomyJournaled(ev.kind)){
        state.history.journal.push_back(ev);
    }
    //A count to the whole's row and its colony's alike (BY COLONY).
    if (EconomyCounted(ev.kind)){
        EconomyDay& theirs = EconomyColonyRow(today,ev.colony);
        for (EconomyDay* d : {&today,&theirs}){
            d->felled += (ev.kind == EVENT_TREE_FELLED) ? 1 : 0;
            d->broken += (ev.kind == EVENT_ROCK_BROKEN) ? 1 : 0;
            d->storeys += (ev.kind == EVENT_STOREY_RAISED) ? 1 : 0;
            d->zombies += (ev.kind == EVENT_ZOMBIE_FELLED) ? 1 : 0;
        }
    }
    state.version++;
}

void Economy::Notice(int kind, uint32_t person, GoodsMask what){
    vec2 at;
    for (const EconomyWorker& k : state.workers){
        at = (k.id == person) ? k.pos : at;
    }
    Event(kind,person,0,-1,at,0,what);
}

void Economy::CloseDay(uint64_t tick){
    EconomyHistory& h = state.history;
    EconomyDayLevels(state,tick,h.today);
    h.days.push_back(h.today);
    h.today = EconomyDay();
    h.today.day = (int)(tick / CALENDAR_DAY_TICKS);
    state.version++;
}

void Economy::BeginHistory(uint64_t tick){
    EconomyHistory& h = state.history;
    h = EconomyHistory();
    h.from = tick;
    h.start.day = (int)(tick / CALENDAR_DAY_TICKS);
    EconomyDayLevels(state,tick,h.start);
    EconomyForestCleared(state,h.felled_from,h.broken_from);
    h.today.day = h.start.day;
}

int EconomyRopeChain(const EconomyState& e, const ZoneState& z){
    //Hemp: held anywhere, growing, or chosen - a field's first crop before it is ever sown, or its next.
    bool f_hemp = EconomyGoodHeld(e,GOOD_HEMP) > 0;
    for (const EconomyField& f : e.fields){
        f_hemp = f_hemp || (f.f_sown && f.crop == ZONE_CROP_HEMP);
    }
    bool f_ropewalk = false;
    for (size_t id = 1; id < z.buildings.size(); id++){
        const ZoneBuilding& b = z.buildings[id];
        if (b.size <= 0){
            continue;
        }
        if (b.kind == ZONE_KIND_FIELD){
            const EconomyField* f = EconomyFieldOf(e,(uint32_t)id);
            bool f_never = !f || (!f->f_sown && f->last < 0);
            f_hemp = f_hemp || (f_never && b.crop == ZONE_CROP_HEMP) || b.next_crop == ZONE_CROP_HEMP;
        }
        f_ropewalk = f_ropewalk || b.kind == ZONE_KIND_ROPEWALK;
    }
    return !f_hemp ? ECONOMY_ROPE_NO_HEMP : !f_ropewalk ? ECONOMY_ROPE_NO_ROPEWALK : ECONOMY_ROPE_MAKING;
}

void EconomyJobCounts(const EconomyState& e, const ZoneState& z, EconomyJobCount out[WORKER_JOB_COUNT], int* jobless,
                      uint32_t settlement){
    for (int j = 0; j < WORKER_JOB_COUNT; j++){
        out[j] = EconomyJobCount();
    }
    bool f_all = settlement == ECONOMY_EVERY_SETTLEMENT;
    size_t n = std::min(z.buildings.size(),e.places.size());
    for (size_t id = 1; id < n; id++){
        if (!f_all && EconomySettlementOf(e,(uint32_t)id) != settlement){
            continue;
        }
        int job = EconomyJobOf(z.buildings[id].kind);
        if (job >= 0 && job < WORKER_JOB_COUNT){
            out[job].places += e.places[id];
        }
        //The camp's own woodcutter (woodcutting_plan.md), its water carrier (settlements_plan.md) and its picker
        //(foraging_plan.md).
        out[WORKER_JOB_WOODCUTTER].places += (id < e.cutter_places.size()) ? e.cutter_places[id] : 0;
        out[WORKER_JOB_WATER].places += (id < e.drawer_places.size()) ? e.drawer_places[id] : 0;
        out[WORKER_JOB_PICKER].places += (id < e.picker_places.size()) ? e.picker_places[id] : 0;
    }
    int none = 0;
    for (const EconomyWorker& k : e.workers){
        if (!f_all && EconomyPersonLand(e,k) != settlement){
            continue;
        }
        if (k.job >= 0 && k.job < WORKER_JOB_COUNT){
            out[k.job].working++;
        }else if (k.job == WORKER_JOB_NONE){
            none++;         //a child playing is not jobless
        }
    }
    if (jobless){
        *jobless = none;
    }
}

int EconomyJobSkill(int job){
    switch (job){
        case WORKER_JOB_WOODCUTTER: return SKILL_STRENGTH;
        case WORKER_JOB_FARMER:     return SKILL_HUSBANDRY;
        case WORKER_JOB_WATER:      return SKILL_INGENUITY;
        case WORKER_JOB_ARCHER:     return SKILL_PRECISION;
        case WORKER_JOB_GUARD:      return SKILL_PRECISION;     //after archery (AssignJobs)
        case WORKER_JOB_ROPEMAKER:  return SKILL_PRECISION;     //a craft (goods_plan.md)
        case WORKER_JOB_PICKER:     return SKILL_HUSBANDRY;     //knowing the plants (foraging_plan.md)
        case WORKER_JOB_STONECUTTER: return SKILL_STRENGTH;     //gameplay_plan.md's table: quarrying
        case WORKER_JOB_MINER:      return SKILL_STRENGTH;      //...and mining
        case WORKER_JOB_STEAM:      return SKILL_INGENUITY;     //"machines and steam" (gameplay_plan.md's table)
        case WORKER_JOB_CONDENSER:  return SKILL_INGENUITY;     //...which names the condenser too
        case WORKER_JOB_SAILMAKER:  return SKILL_PRECISION;     //a craft, as the ropemaker's (steam_plan.md step 4)
        default:                    return SKILL_STRENGTH;      //a woodcutter, a builder, and anyone carrying
    }
}

float EconomySkillFactor(int skill){
    return 0.6f + 0.08f * (float)std::max(1,std::min(10,skill));
}

float EconomyTaskProgress(const EconomyWorker& k){
    switch (k.state){
        case WORKER_WORKING:
        case WORKER_UNLOADING:
        case WORKER_FETCHING:
        case WORKER_BUILDING:
        case WORKER_LAYING:
        case WORKER_GETTING:
        case WORKER_SOWING:
        case WORKER_PLAYING:
        case WORKER_TRAINING:
        case WORKER_LIFTING:
        case WORKER_TAKING_INPUT:
        case WORKER_MAKING:
            break;
        case WORKER_ON_POST:
            if (k.forage == FORAGE_NONE){
                return -1.0f;   //only picking or drawing by hand there runs on the timer: a unit at a time
            }
            break;
        default:
            return -1.0f;
    }
    //A save from before timer_full, mid-task: no bar until his next one starts.
    if (k.timer_full <= 0 || k.timer <= 0 || k.timer > k.timer_full){
        return -1.0f;
    }
    return 1.0f - (float)k.timer / (float)k.timer_full;
}

int EconomyLoadRoom(const EconomyState& e, const ZoneState& z, const EconomyWorker& k){
    //A load home to the pantry is a fetch, ECONOMY_LOAD for anyone (TakeHome): the rest are his work's.
    bool f_work = k.state != WORKER_TO_PANTRY;
    bool f_camp = k.building && k.building < z.buildings.size() && z.buildings[k.building].kind == ZONE_KIND_CAMP;
    int room = ECONOMY_LOAD;
    if (f_work && k.job == WORKER_JOB_WOODCUTTER && k.carry_good == GOOD_WOOD){
        int log_wood = (k.building < e.log_wood.size() && e.log_wood[k.building] > 0) ? e.log_wood[k.building]
                                                                                       : ECONOMY_WOOD_PER_TREE;
        room = ECONOMY_WOODCUTTER_LOGS * log_wood;
    }else if (f_work && f_camp && k.job == WORKER_JOB_WATER && k.carry_good == GOOD_WATER){
        room = ECONOMY_CAMP_WATER_LOAD;
    }
    return std::max(room,k.carry);
}

namespace {
uint32_t Mix(uint32_t a, uint32_t b);      //below, with the settlers it was written for
}

//--- Lifelines (docs/lifeline_plan.md) -------------------------------------------------------------

int EconomyAgeAt(int64_t born, uint64_t tick){
    int64_t lived = (int64_t)tick - born;
    return (lived > 0) ? (int)(lived / CALENDAR_YEAR_TICKS) : 0;
}

int EconomyStageAt(int64_t born, uint64_t tick){
    int64_t lived = (int64_t)tick - born;
    return (lived < LIFE_BABY_TICKS) ? LIFE_BABY : (lived < LIFE_CHILD_TICKS) ? LIFE_CHILD
         : (lived < LIFE_YOUNG_TICKS) ? LIFE_YOUNG : (lived < LIFE_OLD_TICKS) ? LIFE_ADULT : LIFE_OLD;
}

int64_t EconomyLifespan(uint32_t seed, uint32_t id){
    return (int64_t)LIFE_DIES_FROM + (int64_t)(Mix(seed,id * 8 + 7) % (uint32_t)LIFE_DIES_SPAN);
}

int EconomyShare(int64_t born, uint64_t tick){
    int stage = EconomyStageAt(born,tick);
    return (stage == LIFE_BABY) ? LIFE_SHARE_BABY : (stage == LIFE_CHILD) ? LIFE_SHARE_CHILD : LIFE_SHARE_FULL;
}

const char* EconomyStageName(int stage){
    switch (stage){
        case LIFE_BABY:     return "baby";
        case LIFE_CHILD:    return "child";
        case LIFE_YOUNG:    return "young adult";
        case LIFE_ADULT:    return "adult";
        case LIFE_OLD:      return "old";
        default:            return "?";
    }
}

int EconomyBirthday(int64_t born){
    int64_t in_year = ((born % CALENDAR_YEAR_TICKS) + CALENDAR_YEAR_TICKS) % CALENDAR_YEAR_TICKS;
    return (int)(in_year / CALENDAR_DAY_TICKS);
}

std::string EconomyBirthdayName(int64_t born){
    int day = EconomyBirthday(born);
    return std::string(CalendarSeasonName(day / CALENDAR_DAYS_PER_SEASON)) + " "
           + std::to_string(day % CALENDAR_DAYS_PER_SEASON + 1);
}

int EconomyCoupleDay(uint32_t a, uint32_t b){
    uint32_t lo = std::min(a,b);
    uint32_t hi = std::max(a,b);
    return (int)(((lo * 2654435761u) ^ (hi * 2246822519u) ^ 0x5EEDu) % (uint32_t)(CALENDAR_SEASONS * CALENDAR_DAYS_PER_SEASON));
}

int EconomyChildrenOf(const EconomyState& e, uint32_t mother){
    if (!mother){
        return 0;
    }
    int n = 0;
    for (const EconomyWorker& k : e.workers){
        n += (k.mother == mother) ? 1 : 0;
    }
    for (const EconomyDeath& d : e.dead){
        n += (d.mother == mother) ? 1 : 0;
    }
    return n;
}

int EconomyHouseAppeal(const ZoneState& z, uint32_t h, int* from_storeys, int* from_garden){
    int storeys = 0;
    for (size_t v = 0; v < z.building.size(); v++){
        storeys = (z.building[v] == h) ? std::max(storeys,(int)z.standing[v]) : storeys;
    }
    //Its garden, standing: the first plot LIFE_BIRTH_GARDEN, each after it half the one before.
    int garden = 0;
    int next = LIFE_BIRTH_GARDEN;
    for (size_t v = 0; v < z.ground_owner.size() && next > 0; v++){
        if (z.ground[v] == ZONE_GROUND_GARDEN && z.ground_built[v] && ZoneGroundOwner(z,(int)v) == h){
            garden += next;
            next /= 2;
        }
    }
    int high = std::max(0,storeys - 1) * LIFE_BIRTH_PER_STOREY;
    if (from_storeys){
        *from_storeys = high;
    }
    if (from_garden){
        *from_garden = garden;
    }
    return high + garden;
}

int EconomyLivingIn(const EconomyState& e, uint32_t h){
    int n = 0;
    for (const EconomyWorker& k : e.workers){
        n += (k.house == h) ? 1 : 0;
    }
    return n;
}

int EconomyCrowdedBy(const EconomyState& e, const ZoneState& z, uint32_t h){
    if (!h || h >= z.buildings.size() || z.buildings[h].kind != ZONE_KIND_HOUSE || h >= e.capacity.size()){
        return 0;       //a camp crams its homeless in by design (settlements_plan.md): only a house is crowded
    }
    return std::max(0,EconomyLivingIn(e,h) - e.capacity[h]);
}

int EconomyBirthChance(const EconomyState& e, const ZoneState& z, uint32_t h){
    int living = EconomyLivingIn(e,h);
    int room = (h < e.capacity.size()) ? e.capacity[h] : 0;
    int base = (living > room) ? LIFE_BIRTH_CROWDED : (living < room) ? LIFE_BIRTH_ROOM : LIFE_BIRTH_FULL;
    return base + EconomyHouseAppeal(z,h,NULL,NULL);
}

float EconomyCrowdedPace(int crowded_by){
    return std::max(LIFE_CROWDED_LEAST,1.0f - LIFE_CROWDED_PACE * (float)std::max(0,crowded_by));
}

float EconomyPersonCrowdedPace(const EconomyState& e, const ZoneState& z, const EconomyWorker& k){
    return EconomyCrowdedPace(EconomyCrowdedBy(e,z,k.house));
}

namespace {

//A person's mother and father by id, among the living or the dead: the tree outlives the people in it.
void ParentsOf(const EconomyState& e, uint32_t id, uint32_t out[2]){
    out[0] = out[1] = 0;
    for (const EconomyWorker& k : e.workers){
        if (k.id == id){
            out[0] = k.mother;
            out[1] = k.father;
            return;
        }
    }
    for (const EconomyDeath& d : e.dead){
        if (d.id == id){
            out[0] = d.mother;
            out[1] = d.father;
            return;
        }
    }
}

}

bool EconomyRelated(const EconomyState& e, const EconomyWorker& a, const EconomyWorker& b){
    //Each with their parents and grandparents: any id the two lists share, 0 (nobody) aside. Each list starts
    //with the person, so one being the other's parent or grandparent is a shared id too.
    uint32_t up[2][7];
    const EconomyWorker* both[2] = {&a,&b};
    for (int s = 0; s < 2; s++){
        up[s][0] = both[s]->id;
        up[s][1] = both[s]->mother;
        up[s][2] = both[s]->father;
        ParentsOf(e,up[s][1],&up[s][3]);
        ParentsOf(e,up[s][2],&up[s][5]);
    }
    for (int i = 0; i < 7; i++){
        for (int j = 0; j < 7; j++){
            if (up[0][i] && up[0][i] == up[1][j]){
                return true;
            }
        }
    }
    return false;
}

int EconomyCoupleSurname(const EconomyState& e, uint32_t mother, uint32_t father){
    uint32_t lo = std::min(mother,father);
    uint32_t hi = std::max(mother,father);
    bool f_hi = ((((lo * 2246822519u) ^ (hi * 3266489917u) ^ 0x5A2Eu) >> 7) & 1u) != 0;
    //The one the hash picks, or - with one of them nobody (0) - the other.
    uint32_t pick = (f_hi && hi) || !lo ? hi : lo;
    for (const EconomyWorker& k : e.workers){
        if (k.id == pick){
            return k.surname;
        }
    }
    for (const EconomyDeath& d : e.dead){
        if (d.id == pick){
            return d.surname;
        }
    }
    return 0;
}

//--- Posts (docs/defence_plan.md) --------------------------------------------------------------------

int EconomyPostPlot(const EconomyState& e, const EconomyWorker& k){
    if (k.post >= 0){
        return k.post;
    }
    if (k.job == WORKER_JOB_GUARD && k.building && k.building < e.door.size()){
        return e.door[k.building];
    }
    return -1;
}

vec2 EconomyPostPoint(const EconomyState& e, const EconomyWorker& k){
    int plot = EconomyPostPlot(e,k);
    if (plot < 0 || !e.world || plot >= (int)e.world->grid->fine.pos.size()){
        return k.pos;
    }
    vec2 p = e.world->grid->fine.pos[plot];
    if (k.post >= 0){
        /*
            Several ORDERED to one plot and layer (selection_plan.md, "Several people selected", step 3): each a place of
            his own, by his rank among them in id order - evenly round a ring through its corners, ECONOMY_POST_STAND of
            the way out, so they stand apart and still on it. One alone stands on its point, as ever.
        */
        int rank = 0;
        int n = 0;
        for (const EconomyWorker& o : e.workers){
            if (o.post == k.post && o.post_layer == k.post_layer){
                n++;
                rank += (o.id < k.id) ? 1 : 0;
            }
        }
        std::vector<vec2> corners;
        if (n > 1){
            ZoneTowerCorners(*e.world,plot,corners);
        }
        if (corners.size() < 2){
            return p;
        }
        float total = 0.0f;
        for (size_t i = 0; i < corners.size(); i++){
            corners[i] = p + (corners[i] - p) * ECONOMY_POST_STAND;
        }
        for (size_t i = 0; i < corners.size(); i++){
            total += (corners[(i + 1) % corners.size()] - corners[i]).length();
        }
        //rank/n of the way round from its first corner.
        float want = total * (float)rank / (float)n;
        for (size_t i = 0; i < corners.size(); i++){
            vec2 a = corners[i];
            vec2 b = corners[(i + 1) % corners.size()];
            float len = (b - a).length();
            if (want <= len || i + 1 == corners.size()){
                return a + (b - a) * ((len > 1e-5f) ? std::min(1.0f,want / len) : 0.0f);
            }
            want -= len;
        }
        return p;
    }
    //A guard: his place by his rank among its guards, in id order (ZoneTowerPlace) - spread round the platform.
    int rank = 0;
    for (const EconomyWorker& o : e.workers){
        rank += (o.job == WORKER_JOB_GUARD && o.post < 0 && o.building == k.building && o.id < k.id) ? 1 : 0;
    }
    return ZoneTowerPlace(*e.world,plot,rank);
}

int EconomyPostRoom(const ChasmWorld& w, int plot){
    if (plot < 0 || plot >= (int)w.grid->fine.pos.size()){
        return 1;
    }
    return std::max(1,(int)(w.picker->PlotArea(plot) / ECONOMY_POST_ROOM));
}

int EconomyTowerStock(const ChasmWorld& w, int plot, int good){
    int per = (good == GOOD_WATER) ? ECONOMY_TOWER_WATER : (good == GOOD_PROVISIONS) ? ECONOMY_TOWER_PROVISIONS : 0;
    return ZoneTowerGuards(w,plot) * per;
}

int EconomyWellWater(const ChasmWorld& w, int plot){
    uint32_t h = Mix(Mix(w.grid->settings.seed,(uint32_t)plot),0x3E11u);
    return ECONOMY_WELL_WATER_MIN + (int)(h % (uint32_t)(ECONOMY_WELL_WATER_MAX - ECONOMY_WELL_WATER_MIN + 1));
}

std::array<int,GOOD_COUNT> EconomyTowerShort(const EconomyState& e, const ChasmWorld& w, uint32_t id, int plot){
    std::array<int,GOOD_COUNT> short_of{};
    if (plot < 0 || id >= e.stock.size()){
        return short_of;
    }
    for (int g : {GOOD_WATER,GOOD_PROVISIONS}){
        short_of[g] = std::max(0,EconomyTowerStock(w,plot,g) - e.stock[id][g]);
    }
    return short_of;
}

std::array<int,GOOD_COUNT> EconomyComing(const EconomyState& e, uint32_t id, const EconomyWorker* except){
    std::array<int,GOOD_COUNT> coming{};
    for (const EconomyWorker& o : e.workers){
        if (&o == except || o.site != id || o.carry_good < 0 || o.carry_good >= GOOD_COUNT){
            continue;
        }
        if (o.state == WORKER_TO_FETCH || o.state == WORKER_FETCHING){
            coming[o.carry_good] += ECONOMY_LOAD;      //he takes less if less is wanted
        }else if (o.state == WORKER_TO_SITE){
            coming[o.carry_good] += o.carry;
        }
    }
    return coming;
}

bool EconomyAtPost(const EconomyState& e, const EconomyWorker& k){
    return EconomyPostPlot(e,k) >= 0 && (k.pos - EconomyPostPoint(e,k)).length() < ECONOMY_POST_REACH;
}

uint32_t EconomyOnTower(const EconomyState& e, const ZoneState& z, const EconomyWorker& k){
    int plot = EconomyPostPlot(e,k);
    uint32_t b = (plot >= 0 && plot < (int)z.building.size()) ? z.building[plot] : 0;
    return (b && b < z.buildings.size() && z.buildings[b].kind == ZONE_KIND_TOWER && z.standing[plot] > 0
            && EconomyAtPost(e,k)) ? b : 0;
}

bool EconomyCanOrder(const EconomyWorker& k, uint64_t tick){
    int stage = EconomyStageAt(k.born,tick);
    return stage == LIFE_YOUNG || stage == LIFE_ADULT;
}

const char* EconomyWhyNotOrdered(const EconomyWorker& k, uint64_t tick){
    return (EconomyStageAt(k.born,tick) == LIFE_OLD) ? "too old" : "too young";
}

int EconomyPackFood(const EconomyWorker& k){
    int food = 0;
    for (int g = 0; g < GOOD_COUNT; g++){
        food += k.pack[g] * EconomyFoodWorth(g);
    }
    return food;
}

//What his pack holds any of: GOOD_BIT(GOOD_WATER), GOODS_EATEN for any food, both or neither (EVENT_RAN_OUT).
static GoodsMask PackHolds(const EconomyWorker& k){
    return (GoodsMask)((k.pack[GOOD_WATER] > 0 ? GOOD_BIT(GOOD_WATER) : 0) | (EconomyPackFood(k) > 0 ? GOODS_EATEN : 0));
}

/*
    Names by id, from short lists: a first name by the person - a woman's or a man's - and a SURNAME of his
    own (`surname`): a settler's by his family, so a family shares its name; anyone born since, one of his
    parents' (COUPLES). Placeholders with the feel of a frontier village; nothing in the rules reads them.
    The lists are stepped through by a stride that shares no factor with their length, so the first
    NAME_COUNT people - and families - never share a name (two Catos on seed 12 had, when it was a hash).
*/
#define NAME_COUNT      30      //each first-name list's length
#define SURNAME_COUNT   28
static const char* surnames[SURNAME_COUNT] = {"Aldering","Brink","Dekker","Hoeve","Kamp","Molen","Rietveld","Smit",
                                              "Ter Horst","Veen","Visser","Wolters","Bakker","Boer","Bosch","De Wit",
                                              "Haverkamp","Hulst","Kuiper","Mulder","Postma","Schaap","Timmer",
                                              "Van Dijk","Verhoef","Vos","Wever","Zijlstra"};

int EconomySettlerSurname(uint32_t family){
    return (int)((family * 11u + 4u) % SURNAME_COUNT);
}

std::string EconomySurnameName(int surname){
    return surnames[(surname >= 0 ? surname : 0) % SURNAME_COUNT];
}

std::string EconomyPersonName(const EconomyWorker& k){
    static const char* women[NAME_COUNT] = {"Ada","Cato","Elin","Fenna","Hanne","Kaat","Lieve","Nora","Roos","Tess",
                                            "Anouk","Femke","Lotte","Mila","Sanne","Bente","Evi","Floor","Griet","Ilse",
                                            "Isa","Jet","Liv","Marit","Maud","Merel","Pien","Saar","Veerle","Wies"};
    static const char* men[NAME_COUNT] = {"Bram","Dirk","Gijs","Ivo","Joris","Maas","Otto","Pim","Sem","Wout",
                                          "Bas","Daan","Jesse","Noud","Teun","Aart","Bart","Cas","Dolf","Floris",
                                          "Gerrit","Hidde","Jelle","Kees","Luuk","Mees","Rik","Siem","Thijs","Wessel"};
    uint32_t a = (k.id * 13u + 9u) % NAME_COUNT;
    return std::string(k.f_female ? women[a] : men[a]) + " " + EconomySurnameName(k.surname);
}

namespace {

/*
    A crop's growing time, in days of growing weather, and its yield per unit of field area. Greens
    are fast and light, beans slow and rich, wheat between (gameplay_plan.md, "Goods and food"). Since
    the frost (fields_plan.md) a crop must ripen inside the season - 28 days by the camp, the snow gone
    on spring 3 - so wheat and beans were shortened (the user's, 2026-10-08: 18 and 25 before, beans
    never came through): at an average farmer's pace greens come in three times a season, wheat once
    (twice for a good farmer), beans once if sown in the first week or so after the thaw. What a unit
    is worth eaten is EconomyFoodWorth's (needs_plan.md). The yields were doubled the same day (the
    user's, from 0.6 / 0.35 / 0.45): ten people took about 37 cells, and now about 16 (needs_plan.md,
    "What the rates mean"). HEMP (goods_plan.md, current tuning) ripens as wheat does, once a season, and a
    one-cell field's harvest - about 8 - makes the 4 rope a winch's storey takes.
*/
const int crop_days[ZONE_CROP_COUNT] = {15,8,20,15};
const float crop_yield[ZONE_CROP_COUNT] = {1.2f,0.7f,0.9f,0.6f};
static_assert(ZONE_CROP_COUNT == 4 && ZONE_CROP_HEMP == 3,"a crop added needs its good in GoodOfCrop (Goods.h)");

/*
    THE ROTATION (fields_plan.md, the user's follows-what table): a crop's yield, in percent, by the crop
    harvested before it. Repeating a crop wears it down; beans feed the soil for whatever follows them.
    Hemp (goods_plan.md, agreed 2026-10-10) smothers the weeds, so what follows it does well.
*/
const int crop_after[ZONE_CROP_COUNT][ZONE_CROP_COUNT] = {
    //sown:  wheat greens beans hemp
    {        60,   100,   100,  100},   //after wheat
    {        100,   70,   100,  100},   //after greens
    {        120,  120,    60,  120},   //after beans
    {        110,  110,   100,   60},   //after hemp
};

//How often a waiting worker looks for something to do, in ticks - staggered by his building's id,
//so they do not all look on the same tick. Half a second: soon enough to look prompt.
#define ECONOMY_LOOK_TICKS  25
//How often the posted are looked at for a way home only over the ice (IceHome): a second.
#define ECONOMY_ICE_LOOK_TICKS  50

//A small integer hash, for the settlers' ages and skills: the same world gives the same colony.
uint32_t Mix(uint32_t a, uint32_t b){
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u + (a << 6) + (a >> 2));
    h ^= h >> 15;
    h *= 0x85EBCA77u;
    h ^= h >> 13;
    h *= 0xC2B2AE3Du;
    h ^= h >> 16;
    return h;
}

/*
    The settlers (lifeline_plan.md): three families, each a couple with their grown children, two of them with
    a child too - twelve people. Sizes 5, 4 and 3, so the first houses are a real choice: a house just big
    enough for the family of three is too small for the other two.
*/
struct SettlerFamily{
    int grown;          //grown children, besides the couple
    int children;       //children, playing
};
const SettlerFamily settler_families[] = {{2,1},{1,1},{1,0}};
//Their ages, from the seed: the couples', the grown children's, and the children's days short of growing up -
//so every child does in the colony's first year.
#define SETTLER_COUPLE_AGE      24      //to 32
#define SETTLER_COUPLE_SPAN     9
#define SETTLER_GROWN_AGE       5       //to 12
#define SETTLER_GROWN_SPAN      8
#define SETTLER_CHILD_DAYS      36
#define SETTLER_DUE_DAY         5       //the expecting settler's child: due from this day of the first year...
#define SETTLER_DUE_SPAN        16      //...over this many - before the first winter, so a baby is seen early

bool IsTree(int kind){
    return kind <= PROP_SNOW_PINE;
}

int SecondsToTicks(float seconds){
    return std::max(1,(int)std::lround(seconds * CALENDAR_TICKS_PER_SECOND));
}

int JobOf(int kind){
    return EconomyJobOf(kind);
}

//What a worker carries off from his workplace (FetchFromWorkplace): whatever it holds - but a ropemaker the rope and a
//sailmaker the canvas, never the hemp waiting there to be made into it (EconomyHempWork), a condenser's hand the water,
//never its steam (steam_plan.md), and a woodcutter wood: the camp's (woodcutting_plan.md) holds its supplies too.
GoodsMask WorkplaceMakes(int job){
    const EconomyHempWork* work = EconomyHempWorkOf(job);
    return work ? GOOD_BIT(work->made) : (job == WORKER_JOB_WOODCUTTER) ? GOOD_BIT(GOOD_WOOD)
         : (job == WORKER_JOB_CONDENSER) ? GOOD_BIT(GOOD_WATER)
         : (job == WORKER_JOB_STONECUTTER) ? GOOD_BIT(GOOD_STONE)
         : (job == WORKER_JOB_MINER) ? (GOOD_BIT(GOOD_STONE) | GOOD_BIT(GOOD_FLOATSTONE)) : GOODS_ALL;
}

//The plot a fine quad's corner k is, and a coarse cell's centre: the one vertex its four children share.
int CoarseCentre(const Grid& g, int c){
    for (int k = 0; k < 4; k++){
        int v = g.fine.quads[c * 4].v[k];
        bool f_all = true;
        for (int q = c * 4 + 1; q < c * 4 + 4 && f_all; q++){
            const GridQuad& fq = g.fine.quads[q];
            f_all = (fq.v[0] == v || fq.v[1] == v || fq.v[2] == v || fq.v[3] == v);
        }
        if (f_all){
            return v;
        }
    }
    return g.fine.quads[c * 4].v[0];
}

//Is p under the snow now? Off the map's grid there is nothing to snow on.
bool UnderSnow(const ChasmWorld& w, const vec2& p, uint64_t tick){
    float cover = CalendarSnowCover(tick);
    if (cover <= 0.0f){
        return false;
    }
    float front = CalendarSnowFrontZ(cover,w.grid->bounds_min.y,w.grid->bounds_max.y);
    return CalendarSnowAt(p,front);
}

bool AllInField(const ChasmWorld& w, const ZoneState& z, int v){
    const GridPicker& p = *w.picker;
    for (int i = 0; i < p.PlotQuadCount(v); i++){
        if (!z.field[w.grid->fine.quads[p.PlotQuadCorner(v,i) / 4].parent]){
            return false;
        }
    }
    return p.PlotQuadCount(v) > 0;
}

int CarryTotal(const std::array<int,GOOD_COUNT>& s){
    int n = 0;
    for (int g = 0; g < GOOD_COUNT; g++){
        n += s[g];
    }
    return n;
}

//A builders' hut that stands: where provisions are made (POSTS, Economy.h).
static bool Workshop(const ZoneState& z, const std::vector<uint8_t>& stands, uint32_t h){
    return h && h < z.buildings.size() && z.buildings[h].kind == ZONE_KIND_BUILDERS && h < stands.size() && stands[h];
}

//What a stock holds of water or food (`goods`) in ticks of a person's bar: a unit at what it is worth.
int64_t HeldTicks(const std::array<int,GOOD_COUNT>& s, GoodsMask goods){
    int64_t held = 0;
    for (int g = 0; g < GOOD_COUNT; g++){
        if (goods & GOOD_BIT(g)){
            held += (int64_t)s[g] * ((g == GOOD_WATER) ? NEEDS_WATER_WORTH : EconomyFoodWorth(g));
        }
    }
    return held;
}

//How many live in a home.
int64_t Living(const std::vector<EconomyWorker>& workers, uint32_t home){
    int64_t n = 0;
    for (const EconomyWorker& o : workers){
        n += (o.house == home) ? 1 : 0;
    }
    return n;
}

//What a home's people eat and drink, in quarters of an adult's (A SHARE, Economy.h): Living for a home's food and
//water, where Living is its heads, for its room.
int64_t Mouths(const std::vector<EconomyWorker>& workers, uint32_t home, uint64_t tick){
    int64_t n = 0;
    for (const EconomyWorker& o : workers){
        n += (o.house == home) ? EconomyShare(o.born,tick) : 0;
    }
    return n;
}

/*
    RESERVED (EconomyReserved): what o, on his way to take from `id`, counts on of `good` - for a site, a carrier's
    load or a builder's for provisions (GoodFree leaves these out), and for his home, a load (FetchHome's spare
    leaves it out). The rules and the cards' striped goods count by the same two.
*/
static int SiteReservation(const EconomyWorker& o, uint32_t id, int good){
    if (o.store != id || o.carry_good != good){
        return 0;
    }
    if (o.site && (o.state == WORKER_TO_FETCH || o.state == WORKER_FETCHING)){
        return ECONOMY_LOAD;
    }
    //A builder on his way for wheat or beans for provisions: the load he set out for (MakeProvisions).
    return (o.state == WORKER_TO_INPUT || o.state == WORKER_TAKING_INPUT) ? o.stint : 0;
}

static int HomeReservation(const EconomyWorker& o, uint32_t id, int good){
    return (o.store == id && o.carry_good == good && (o.state == WORKER_TO_GET || o.state == WORKER_GETTING)) ? ECONOMY_LOAD : 0;
}

//Of `goods`, the one a stock holds most of, the lower on a tie; -1 for none.
int MostOf(const std::array<int,GOOD_COUNT>& s, GoodsMask goods){
    int best = -1;
    for (int g = 0; g < GOOD_COUNT; g++){
        if ((goods & GOOD_BIT(g)) && s[g] > 0 && (best < 0 || s[g] > s[best])){
            best = g;
        }
    }
    return best;
}

}

bool EconomyFrozen(const ChasmWorld& w, const ZoneState& z, uint32_t id, uint64_t tick){
    //The first plot, as home_plot takes it.
    for (size_t v = 0; v < z.building.size(); v++){
        if (z.building[v] == id){
            return UnderSnow(w,w.grid->fine.pos[v],tick);
        }
    }
    return false;
}

int EconomyIceDays(const ChasmWorld& w, int plot, uint64_t tick){
    if (plot < 0 || plot >= (int)w.grid->fine.pos.size()){
        return 0;
    }
    //A day at a time: the snow's front goes back north through the spring, so the first day it is past.
    const vec2& p = w.grid->fine.pos[plot];
    int most = (int)(CALENDAR_YEAR_TICKS / CALENDAR_DAY_TICKS);
    for (int d = 0; d < most; d++){
        if (!UnderSnow(w,p,tick + (uint64_t)d * CALENDAR_DAY_TICKS)){
            return d;
        }
    }
    return most;
}

//--- Walking straight ------------------------------------------------------------------------------

bool EconomyStraightClear(const ChasmWorld& w, const ZoneState& z, const vec2& a, const vec2& b,
                          int end_plot_a, int end_plot_b){
    const Terrain& t = *w.terrain;
    const GridPicker& p = *w.picker;
    float len = (b - a).length();
    int steps = std::max(1,(int)std::ceil(len / ECONOMY_STRAIGHT_STEP));
    int ground = end_plot_a;        //a plot the whole way must be the same ground as
    int prev = -1;
    vec2 prev_at = a;
    for (int i = 0; i <= steps; i++){
        vec2 at = a + (b - a) * ((float)i / steps);
        GridPick k = p.Pick(at);
        if (!k.f_hit || k.plot < 0){
            return false;
        }
        int v = k.plot;
        if (v == prev){
            continue;
        }
        if (ground < 0){
            ground = v;
        }
        bool f_end = (v == end_plot_a || v == end_plot_b);
        if (!t.SameGround(v,ground)){
            return false;
        }
        if (!f_end){
            //A camp is open ground between tents, and a standing bridge a deck over the water. A standing
            //palisade is not: the line steps plot to plot along edges, so it cannot slip past one.
            bool f_bridge = ZoneBridgeWalkable(z,v);
            if ((t.wet[v] && !f_bridge) || t.Mountain(v) || (z.storeys[v] > 0 && z.KindOf(v) != ZONE_KIND_CAMP && !f_bridge)
                || AllInField(w,z,v) || ZoneShutToWalkers(z,v)){     //a gate's shut door too (defence_plan.md)
                return false;
            }
        }else if (t.wet[v] && !ZoneBridgeWalkable(z,v)){
            return false;       //an end on the wet: only a bridge's
        }
        if (prev >= 0){
            //From one plot to the next: they must be neighbours, with nothing between them, and not
            //too steep - the rules of a walker's edge, met along the line.
            int nb[8];
            int n = ZonePlotNeighbours(w,prev,nb,8);
            bool f_neighbour = false;
            for (int j = 0; j < n; j++){
                f_neighbour = f_neighbour || nb[j] == v;
            }
            if (!f_neighbour){
                return false;
            }
            //Onto a guard tower and off it only by its ladder, as the walkers' edges allow (Walkers::EdgeOpen).
            int ladder_v = ZoneTowerLadder(z,v);
            int ladder_prev = ZoneTowerLadder(z,prev);
            if ((ladder_v >= 0 && prev != ladder_v) || (ladder_prev >= 0 && v != ladder_prev)){
                return false;
            }
            if (ZoneBoundaryBetween(z,prev,v) != ZONE_BOUNDARY_NONE && !ZoneGateBetween(w,z,prev,v)){
                return false;
            }
            //Never over a terrace's edge (terraces_plan.md stage 2): its wall, its stair or its step - the walkers' way.
            if ((ZoneTerraceStands(z,v) || ZoneTerraceStands(z,prev)) && ZoneGroundAt(w,z,v) != ZoneGroundAt(w,z,prev)){
                return false;
            }
            float run = std::max(1e-3f,(w.grid->fine.pos[v] - w.grid->fine.pos[prev]).length());
            if (std::fabs(ZoneGroundAt(w,z,v) - ZoneGroundAt(w,z,prev)) > WALKER_STEEPEST * run){
                return false;
            }
        }
        prev = v;
        prev_at = at;
    }
    (void)prev_at;
    return true;
}

vec2 EconomyWorkerFacing(const EconomyWorker& k){
    int s = std::min(k.seg,(int)k.route.size() - 2);
    if (s >= 0){
        vec2 d = k.route[s + 1] - k.route[s];
        float l = d.length();
        if (l > 1e-4f){
            return d * (1.0f / l);
        }
    }
    return vec2(0.0f,1.0f);
}

int EconomyRouteLayer(const EconomyWorker& k, int i){
    return (i >= 0 && i < (int)k.route_layer.size()) ? k.route_layer[i] : WALK_GROUND;
}

int EconomyWorkerLayer(const EconomyWorker& k){
    int s = k.seg;
    if (s + 1 < (int)k.route.size()){
        float len = (k.route[s + 1] - k.route[s]).length();
        return EconomyRouteLayer(k,(k.along * 2.0f < len) ? s : s + 1);
    }
    return EconomyRouteLayer(k,std::min(s,(int)k.route.size() - 1));
}

//He stops where he is: a route of the one point he stands on, on the layer he is on.
static void StopWhereHeIs(EconomyWorker& k){
    int layer = EconomyWorkerLayer(k);
    k.route.assign(1,k.pos);
    k.speed.clear();
    k.route_layer.assign((layer != WALK_GROUND) ? 1 : 0,(uint8_t)layer);
    k.seg = 0;
    k.along = 0.0f;
}

//--- The state -------------------------------------------------------------------------------------

void Economy::Reset(std::shared_ptr<const ChasmWorld> world){
    state = EconomyState();
    state.world = world;
    pick_up_looks.clear();
    now = 0;
    home_plot.clear();
    plots_of.clear();
    if (world && world->forest){
        state.prop_state.assign(world->forest->props.size(),PROP_STATE_STANDING);
    }
    //THE MINE: nothing dug yet from any deposit.
    if (world && world->deposits){
        state.dug.assign(world->deposits->deposits.size(),0);
    }
    BuildTrees();
    //FORAGING: a new map's plants all ripe (foraging_plan.md) - a save puts back those that are not (Restore).
    if (world && world->forest){
        state.fruit.assign(world->forest->props.size(),0);
        state.regrow.assign(world->forest->props.size(),0);
        for (int i : plants){
            state.fruit[i] = (uint8_t)EconomyFruitFull(world->forest->props[i]);
        }
    }
    FindCamp();
    MakeSettlers();
    raises.clear();
    ground_raises.clear();
    field_raises.clear();
    crop_defaults.clear();
    tents_packed.clear();
    ground_breaks.clear();
    gate_breaks.clear();
    f_zombies_replan = false;
    tents_of.clear();       //a camp gone leaves its goods by what was one at the last Derive: none before the first
    share_of.clear();
    field_pace.clear();
    crowded_by.clear();     //counted afresh on the first tick, so a load or replay starts from its own people
    f_resettle = false;
    f_new_colony = true;
}

/*
    THE CAMP's spot (Zones.h, ZoneStartCampPlot): where the settlers stand at the start, and where anyone
    with nowhere to live goes back to. The camp itself is a building the zones place there.
*/
#define ECONOMY_CAMP_REACH      2.0f    //within this of his home plot, he is there - the camp is a ring of people

void Economy::FindCamp(){
    //A test map has no camp, and so no settlers (test_maps_plan.md): it starts with nobody on it.
    bool f_chasm = state.world && GridKindHasChasm(state.world->grid->settings.kind);
    state.camp_plot = f_chasm ? ZoneStartCampPlot(*state.world,EconomySettlerCount()) : -1;
}

int EconomySettlerCount(){
    int n = 0;
    for (size_t f = 0; f < sizeof(settler_families) / sizeof(settler_families[0]); f++){
        n += 2 + settler_families[f].grown + settler_families[f].children;
    }
    return n;
}

/*
    The colony's first people, standing round the camp: each family its father, its mother, its grown children
    and its child. Their ages, birthdays, the children's sexes and everyone's skills come from the world's seed.
*/
void Economy::MakeSettlers(){
    state.workers.clear();
    state.next_person = 1;
    if (state.camp_plot < 0){
        return;
    }
    const ChasmWorld& w = *state.world;
    uint32_t seed = w.grid->settings.seed;
    vec2 camp = w.grid->fine.pos[state.camp_plot];
    //Born `years` and a day of the year (the seed's) before the colony's first dawn.
    auto born_ago = [&](uint32_t id, int years){
        int day = (int)(Mix(seed,id * 8 + 6) % (CALENDAR_SEASONS * CALENDAR_DAYS_PER_SEASON));
        return -((int64_t)years * CALENDAR_YEAR_TICKS + (int64_t)day * CALENDAR_DAY_TICKS);
    };
    std::vector<size_t> grown;      //the grown children, by index
    std::vector<size_t> mothers;    //each family's
    int n = 0;
    for (size_t f = 0; f < sizeof(settler_families) / sizeof(settler_families[0]); f++){
        const SettlerFamily& sf = settler_families[f];
        uint32_t parent[2] = {0,0};     //father, mother
        for (int m = 0; m < 2 + sf.grown + sf.children; m++){
            EconomyWorker k;
            k.id = state.next_person++;
            k.family = (uint32_t)f + 1;
            k.surname = (uint8_t)EconomySettlerSurname(k.family);
            if (m < 2){
                //The couple, partners (BIRTHS): he first, she the next id.
                k.f_female = (m == 1);
                k.born = born_ago(k.id,SETTLER_COUPLE_AGE + (int)(Mix(seed,k.id * 8 + 0) % SETTLER_COUPLE_SPAN));
                k.partner = (m == 0) ? k.id + 1 : parent[0];
                parent[m] = k.id;
                if (m == 1){
                    mothers.push_back(state.workers.size());
                }
            }else{
                k.father = parent[0];
                k.mother = parent[1];
                k.f_female = (Mix(seed,k.id * 8 + 5) & 1) != 0;
                if (m < 2 + sf.grown){
                    k.born = born_ago(k.id,SETTLER_GROWN_AGE + (int)(Mix(seed,k.id * 8 + 0) % SETTLER_GROWN_SPAN));
                    grown.push_back(state.workers.size());
                }else{
                    int days = 1 + (int)(Mix(seed,k.id * 8 + 0) % SETTLER_CHILD_DAYS);
                    k.born = -((int64_t)LIFE_CHILD_TICKS - (int64_t)days * CALENDAR_DAY_TICKS);
                    k.job = WORKER_JOB_PLAYING;
                }
            }
            for (int sk = 0; sk < SKILL_COUNT; sk++){
                k.skills[sk] = (uint8_t)(1 + Mix(seed,k.id * 8 + 1 + sk) % 10);
            }
            //In a ring round the camp, a family together.
            float a = (float)n * 0.83f;
            float r = 0.6f + 0.25f * (float)(n % 3);
            k.pos = camp + vec2(std::cos(a),std::sin(a)) * r;
            k.route.assign(1,k.pos);
            state.workers.push_back(k);
            n++;
        }
    }
    //A son and a daughter of different families among the grown children, so that a couple can form
    //(lifeline_plan.md): where the seed gave none, the last grown child is made the first one's other sex.
    bool f_pair = false;
    for (size_t a : grown){
        for (size_t b : grown){
            const EconomyWorker& ka = state.workers[a];
            const EconomyWorker& kb = state.workers[b];
            f_pair = f_pair || (ka.family != kb.family && ka.f_female && !kb.f_female);
        }
    }
    if (!f_pair && grown.size() >= 2){
        state.workers[grown.back()].f_female = !state.workers[grown.front()].f_female;
    }
    //One mother expecting (the user's), the seed's, her child due in the first year's first weeks - every
    //settler mother is under LIFE_CHILDREN_MOST.
    if (!mothers.empty()){
        EconomyWorker& m = state.workers[mothers[Mix(seed,0xB1A7u) % mothers.size()]];
        m.due = (uint64_t)(SETTLER_DUE_DAY + Mix(seed,0xB1A8u) % SETTLER_DUE_SPAN) * CALENDAR_DAY_TICKS;
    }
}

/*
    A PERSON PUT DOWN (Economy.h; test_maps_plan.md step 5): what the seed does not say is a settler's - his age, sex
    and skills, from his id - so one put down with nothing named is any settler. Then housed and staffed at once, as
    one settling here is (SettleHere), so the rules take him up on the next tick rather than at the zones' next change.
*/
bool Economy::AddPerson(int plot, uint32_t house, int age_days, const int skills[SKILL_COUNT], int sex, int archery, int kills,
                        const ZoneState& z, Walkers& walkers, std::string* why, uint32_t* made){
    auto refuse = [&](const char* text){
        if (why){
            *why = text;
        }
        return false;
    };
    if (!state.world || z.world != state.world){
        return refuse("no world");
    }
    const ChasmWorld& w = *state.world;
    if (plot < 0 || plot >= (int)z.ground.size() || plot >= (int)w.grid->fine.pos.size()){
        return refuse("no such plot");
    }
    if (const char* bad = EconomyDropRefusal(w,z,plot)){
        return refuse(bad);
    }
    //A home that stands, as HouseFamilies lives in.
    bool f_camp = house && house < z.buildings.size() && z.buildings[house].kind == ZONE_KIND_CAMP;
    if (house && (house >= z.buildings.size() || house >= stands.size() || !stands[house] || z.buildings[house].size <= 0
                  || (z.buildings[house].kind != ZONE_KIND_HOUSE && !f_camp))){
        return refuse("not a home that stands");
    }
    const uint32_t seed = w.grid->settings.seed;
    const uint32_t id = state.next_person;
    //Grown up: a child would want a mother to care for him (LIFELINES).
    int64_t age = (age_days > 0) ? (int64_t)age_days * CALENDAR_DAY_TICKS
                                 : (int64_t)(SETTLER_COUPLE_AGE + (int)(Mix(seed,id * 8 + 0) % SETTLER_COUPLE_SPAN)) * CALENDAR_YEAR_TICKS;
    if (age < (int64_t)LIFE_CHILD_TICKS){
        return refuse("a child: only someone grown up is put down");
    }
    //Into the family living in his house, or one of his own - a new number, never one of the dead's (as a couple's).
    uint32_t family = 0;
    uint8_t surname = 0;
    uint32_t next_family = 1;
    for (const EconomyWorker& o : state.workers){
        next_family = std::max(next_family,o.family + 1);
        if (house && !f_camp && o.house == house && !family){
            family = o.family;
            surname = o.surname;
        }
    }
    for (const EconomyDeath& d : state.dead){
        next_family = std::max(next_family,d.family + 1);
    }
    if (!family){
        family = next_family;
        surname = (uint8_t)EconomySettlerSurname(family);
    }
    EconomyWorker k;
    k.id = id;
    k.family = family;
    k.surname = surname;
    k.house = house;
    k.born = (int64_t)state.tick - age;
    k.f_female = (sex == 0) ? (Mix(seed,id * 8 + 5) & 1) != 0 : sex == 2;
    for (int sk = 0; sk < SKILL_COUNT; sk++){
        int s = skills ? skills[sk] : 0;
        k.skills[sk] = (uint8_t)((s > 0) ? std::min(10,s) : (int)(1 + Mix(seed,id * 8 + 1 + sk) % 10));
    }
    k.archery = std::max(0,std::min(archery,EconomyArcheryCap(k)));
    //His kills, for his rank: brought with him, so the history counts them apart from the zombies it saw killed.
    k.kills[KILL_BOW] = std::max(0,kills);
    state.history.kills_from += k.kills[KILL_BOW];
    k.pos = w.grid->fine.pos[plot];
    k.route.assign(1,k.pos);
    state.workers.push_back(k);     //the newest id: still in id order
    state.next_person++;
    HouseFamilies(z,walkers);
    for (EconomyWorker& o : state.workers){
        if (o.id == id && o.state == WORKER_AT_HOME && !AtHome(o)){
            GoHome(o,z,walkers);
        }
    }
    f_restaff = true;
    state.version++;
    if (made){
        *made = id;
    }
    return true;
}

/*
    LIFELINES, a tick of them. A mother caring with no baby of hers left a baby - grown into a child, or dead -
    is free again; a baby a year old plays; a child of 2 1/2 is a young adult (out playing, he finishes and comes
    home first); a young adult turning ADULT may pair (COUPLES), so a house standing empty is offered again on
    that tick rather than at the next change; and anyone OLD with a job still - grown old this tick, a mother's
    caring year ended past 36, a save's - has it taken by AssignJobs, on this tick too (OLD AGE). And BIRTHS: an
    expecting mother's child is born the first time she is home after it is due - never pulled from her work - at
    her side, indoors (in a camp, in her tent), and she is caring from then. The child's surname is one of its
    parents' (EconomyCoupleSurname).
*/
bool Economy::Lifelines(uint64_t tick, bool& f_house){
    bool f_jobs = false;
    f_house = false;
    std::vector<const EconomyWorker*> grown;    //GREW UP on this tick: a young adult, told once the loop is done
    std::vector<const EconomyWorker*> aged;     //...and GREW OLD
    for (EconomyWorker& k : state.workers){
        int stage = EconomyStageAt(k.born,tick);
        if (k.job == WORKER_JOB_BABY && stage >= LIFE_CHILD){
            k.job = WORKER_JOB_PLAYING;
            state.version++;
        }
        if (k.job == WORKER_JOB_PLAYING && stage >= LIFE_YOUNG){
            k.job = WORKER_JOB_NONE;
            f_jobs = true;
        }
        if (stage == LIFE_YOUNG && tick > 0 && EconomyStageAt(k.born,tick - 1) < LIFE_YOUNG){
            grown.push_back(&k);
        }
        if (stage == LIFE_ADULT && tick > 0 && EconomyStageAt(k.born,tick - 1) < LIFE_ADULT && !k.partner){
            f_house = true;
        }
        if (stage == LIFE_OLD && tick > 0 && EconomyStageAt(k.born,tick - 1) < LIFE_OLD){
            aged.push_back(&k);
        }
        f_jobs = f_jobs || (stage == LIFE_OLD && !EconomyStageJob(k.job));
    }
    for (const EconomyWorker* k : grown){
        Event(EVENT_GREW_UP,k->id,k->house,-1,k->pos);
    }
    for (const EconomyWorker* k : aged){
        Event(EVENT_GREW_OLD,k->id,k->house,-1,k->pos);
    }
    for (EconomyWorker& m : state.workers){
        if (m.job == WORKER_JOB_CARING && std::none_of(state.workers.begin(),state.workers.end(),[&](const EconomyWorker& o){
                return o.mother == m.id && o.job == WORKER_JOB_BABY; })){
            m.job = WORKER_JOB_NONE;
            f_jobs = true;
        }
    }
    std::vector<EconomyWorker> born;
    uint32_t seed = state.world->grid->settings.seed;
    for (EconomyWorker& m : state.workers){
        if (!m.due || tick < m.due || m.state != WORKER_AT_HOME || !AtHome(m)){
            continue;
        }
        EconomyWorker b;
        b.id = state.next_person++;
        b.family = m.family;
        b.house = m.house;
        b.born = (int64_t)tick;
        b.mother = m.id;
        b.father = m.partner;
        b.surname = (uint8_t)EconomyCoupleSurname(state,m.id,m.partner);
        b.f_female = (Mix(seed,b.id * 8 + 5) & 1) != 0;
        for (int sk = 0; sk < SKILL_COUNT; sk++){
            b.skills[sk] = (uint8_t)(1 + Mix(seed,b.id * 8 + 1 + sk) % 10);     //random, as a settler's - until step 5
        }
        b.job = WORKER_JOB_BABY;
        b.pos = m.pos;
        b.route.assign(1,b.pos);
        b.f_indoors = true;
        born.push_back(b);
        m.due = 0;
        m.job = WORKER_JOB_CARING;
        m.building = 0;
        f_jobs = true;
    }
    if (!born.empty()){
        state.workers.insert(state.workers.end(),born.begin(),born.end());     //the newest ids: still in id order
        state.version++;
    }
    for (const EconomyWorker& b : born){
        Event(EVENT_BORN,b.id,b.house,-1,b.pos);
    }
    return f_jobs;
}

/*
    BIRTHS (Economy.h), at dawn after the fires: each couple whose day of the year it is, with what a child asks
    of them, has its chance - by a hash of the seed, the mother and the year, so a replay draws it again.
*/
void Economy::ChancesOfChildren(const ZoneState& z, uint64_t tick){
    const int year_days = CALENDAR_SEASONS * CALENDAR_DAYS_PER_SEASON;
    int day = (int)((tick / CALENDAR_DAY_TICKS) % (uint64_t)year_days);
    uint32_t seed = state.world->grid->settings.seed;
    auto person = [&](uint32_t id) -> const EconomyWorker* {
        for (const EconomyWorker& k : state.workers){
            if (k.id == id){
                return &k;
            }
        }
        return NULL;
    };
    auto of_age = [&](const EconomyWorker& k){
        return EconomyStageAt(k.born,tick) == LIFE_ADULT;      //grown, and not yet old
    };
    for (EconomyWorker& m : state.workers){
        if (!m.f_female || !m.partner || m.due || m.job == WORKER_JOB_CARING){
            continue;
        }
        const EconomyWorker* p = person(m.partner);
        if (!p || EconomyCoupleDay(m.id,p->id) != day){
            continue;
        }
        //Their own house, standing: not the camp's tents.
        uint32_t h = m.house;
        if (!h || h != p->house || h >= z.buildings.size() || z.buildings[h].kind != ZONE_KIND_HOUSE || !stands[h]){
            continue;
        }
        bool f_warm = h >= state.heat.size() || state.heat[h] != HEAT_COLD;
        bool f_fed = !EconomyPressing(m) && !EconomyPressing(*p);
        if (!f_warm || !f_fed || !of_age(m) || !of_age(*p) || EconomyChildrenOf(state,m.id) >= LIFE_CHILDREN_MOST){
            continue;
        }
        //By its room - crowded, full or a place to spare - and its APPEAL (building_size_plan.md).
        int chance = EconomyBirthChance(state,z,h);
        if ((int)(Mix(Mix(seed,m.id * 4 + 3),(uint32_t)(tick / CALENDAR_YEAR_TICKS)) % 100) < chance){
            m.due = tick + LIFE_EXPECTING_TICKS;
            state.version++;
            Event(EVENT_EXPECTING,m.id,h,-1,m.pos);
        }
    }
}

/*
    PLAYING (lifeline_plan.md): a child at home goes out now and then - on one look in LIFE_PLAY_LOOKS, from a
    hash of him and the look - to a plot a step or two from home that nothing stands on, or among a camp's
    tents (open ground, as the walkers have it), plays there a while (WORKER_PLAYING) and comes home. Only
    where he can walk to; otherwise he stays in. The old take the same walk (OLD AGE).
*/
bool Economy::GoPlay(EconomyWorker& k, const ZoneState& z, Walkers& walkers, uint64_t tick){
    uint32_t look = (uint32_t)(tick / ECONOMY_LOOK_TICKS);
    if (Mix(k.id * 2 + 1,look) % LIFE_PLAY_LOOKS != 0){
        return false;
    }
    const ChasmWorld& w = *state.world;
    int v = HomePlot(k);
    for (int step = 0; step < 2 && v >= 0; step++){
        int nb[8];
        int count = ZonePlotNeighbours(w,v,nb,8);
        int open[8];
        int n = 0;
        for (int j = 0; j < count; j++){
            int kind = z.KindOf(nb[j]);
            if ((z.storeys[nb[j]] == 0 && kind == ZONE_KIND_NONE) || kind == ZONE_KIND_CAMP){
                open[n++] = nb[j];
            }
        }
        if (n == 0){
            break;
        }
        v = open[Mix(look,k.id * 4 + (uint32_t)step) % (uint32_t)n];
    }
    if (v < 0 || v == HomePlot(k) || !RouteTo(k,z,walkers,v,w.grid->fine.pos[v])){
        return false;
    }
    k.state = WORKER_TO_PLAY;
    return true;
}

int EconomyDepositLeft(const EconomyState& e, int d){
    if (!e.world || !e.world->deposits || d < 0 || d >= (int)e.world->deposits->deposits.size()){
        return 0;
    }
    int holds = e.world->deposits->deposits[d].holds;
    int dug = (d < (int)e.dug.size()) ? e.dug[d] : 0;
    return (holds == DEPOSIT_ENDLESS) ? INT_MAX : std::max(0,holds - dug);
}

int Economy::MineDepositAt(uint32_t id) const{
    if (!id || id >= plots_of.size() || plots_of[id].empty() || !state.world->deposits){
        return -1;
    }
    return state.world->deposits->At(plots_of[id].front());
}

/*
    THE MINE (Economy.h): from home, to the rock of his mine - before its portal, or on its outcrop - while its deposit
    has some left and a store of his takes the load he would dig whole. False with nothing to do.
*/
bool Economy::GoDig(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    int d = MineDepositAt(k.building);
    int left = EconomyDepositLeft(state,d);
    int plot = (k.building < home_plot.size()) ? home_plot[k.building] : -1;
    if (left <= 0 && d >= 0){
        DugOut(z,d);    //home from its last load: now the mine lets him go
        return false;
    }
    if (plot < 0){
        return false;
    }
    int good = state.world->deposits->deposits[d].good;
    int load = std::min(ECONOMY_LOAD,left);
    bool f_room = false;
    for (size_t s = 1; s < state.accepts.size() && !f_room; s++){
        f_room = (state.accepts[s] & GOOD_BIT(good)) && Ours(k,(uint32_t)s) && StoreRoomLeft((uint32_t)s,&k) >= load;
    }
    if (!f_room){
        return false;
    }
    const ChasmWorld& w = *state.world;
    vec2 at = w.grid->fine.pos[plot] + DepositDigWay(*w.grid,*w.picker,*w.terrain,*w.deposits,plot) * ECONOMY_MINE_FACE;
    if (!RouteTo(k,z,walkers,plot,at)){
        return false;
    }
    k.prop = -1;
    k.store = 0;
    k.state = WORKER_TO_WORK;
    return true;
}

/*
    Deposit `d` dug out: every mine on it loses its place - its miner goes free on the next tick (AssignJobs) - but not
    while he carries a load of it: a job ended takes the load with it, so the last of a seam would be lost on the way.
*/
void Economy::DugOut(const ZoneState& z, int d){
    for (size_t id = 1; id < z.buildings.size() && id < state.places.size(); id++){
        if (z.buildings[id].kind == ZONE_KIND_MINE && MineDepositAt((uint32_t)id) == d && !MineCarrying((uint32_t)id)){
            state.places[id] = 0;
        }
    }
    f_restaff = true;
    state.version++;
}

bool Economy::MineCarrying(uint32_t id) const{
    return std::any_of(state.workers.begin(),state.workers.end(),[id](const EconomyWorker& k){
        return k.building == id && k.carry > 0;
    });
}

float Economy::WalkSeconds(EconomyWorker probe, const ZoneState& z, Walkers& walkers, int to_plot, const vec2& to_point){
    if (!RouteTo(probe,z,walkers,to_plot,to_point)){
        return -1.0f;
    }
    float seconds = 0.0f;
    for (size_t i = 0; i + 1 < probe.route.size() && i < probe.speed.size(); i++){
        seconds += (probe.route[i + 1] - probe.route[i]).length() / std::max(0.01f,probe.speed[i]);
    }
    return seconds;
}

//Can someone standing at `from` walk to plot `to`? The route a worker would take, thrown away.
bool Economy::CanWalk(const ZoneState& z, Walkers& walkers, const vec2& from, int to){
    if (to < 0){
        return false;
    }
    EconomyWorker probe;
    probe.pos = from;
    return RouteTo(probe,z,walkers,to,state.world->grid->fine.pos[to]);
}

bool Economy::CanWalkPosted(const ZoneState& z, Walkers& walkers, const EconomyWorker& k, int to, int layer){
    if (to < 0){
        return false;
    }
    EconomyWorker probe = k;
    probe.post = to;
    return RouteTo(probe,z,walkers,to,state.world->grid->fine.pos[to],layer);
}

//Standing at his home plot (or the camp): where he is "at home", indoors when it is a house.
bool Economy::AtHome(const EconomyWorker& k) const{
    int home = HomePlot(k);
    return home < 0 || (k.pos - state.world->grid->fine.pos[home]).length() < ECONOMY_CAMP_REACH;
}

//Where his round starts and ends: his house's first plot, or the camp while he has none.
int Economy::HomePlot(const EconomyWorker& k) const{
    if (k.house && k.house < home_plot.size() && home_plot[k.house] >= 0){
        //In a camp, his own tent: the camp's plots shared out by his id.
        if (k.house < tents_of.size() && !tents_of[k.house].empty()){
            return tents_of[k.house][k.id % tents_of[k.house].size()];
        }
        return home_plot[k.house];
    }
    return state.camp_plot;
}

/*
    FAMILIES INTO HOUSES. A house that is gone sends its family back to the camp; then each family with
    no house, in family order, takes the empty house of the lowest id that holds all of them, and they
    walk there - now, if they are at the camp, or when their round brings them home. What is still empty
    after them goes to new COUPLES (Economy.h). A family left with nowhere at all is CRAMMED into a home of
    the settlement it stands in, and moves on from there as one with no house does.
*/
void Economy::HouseFamilies(const ZoneState& z, Walkers& walkers){
    const ChasmWorld& w = *state.world;
    size_t n = z.buildings.size();
    //Only what stands is lived in: a house still a site waits for its builders.
    auto is_house = [&](size_t id){
        return id < n && z.buildings[id].kind == ZONE_KIND_HOUSE && z.buildings[id].size > 0 && stands[id];
    };
    auto is_camp = [&](size_t id){
        return id < n && z.buildings[id].kind == ZONE_KIND_CAMP && z.buildings[id].size > 0 && stands[id];
    };
    //...and only one with a door is moved into: a house SHUT IN is uninhabitable (EconomyShutIn). A family
    //already shut in keeps it - nobody can get them out but the player, by clearing a plot beside it.
    auto can_take = [&](size_t id){
        return is_house(id) && !EconomyShutIn(state,(uint32_t)id);
    };
    std::vector<int> living(n,0);
    std::vector<int> family_size;
    std::vector<uint32_t> family_house;
    for (EconomyWorker& k : state.workers){
        //His home is gone: back to the camp's spot, with nowhere to live - until a home is found him, below.
        if (k.house && !is_house(k.house) && !is_camp(k.house)){
            k.house = 0;
            if (k.state == WORKER_AT_HOME){
                GoHome(k,z,walkers);
            }
        }
        if (k.family >= family_size.size()){
            family_size.resize(k.family + 1,0);
            family_house.resize(k.family + 1,0);
        }
        family_size[k.family]++;
        if (k.house){
            living[k.house]++;
            family_house[k.family] = k.house;
        }
    }
    auto move = [&](uint32_t f, uint32_t to){
        if (family_house[f]){
            living[family_house[f]] -= family_size[f];
        }
        living[to] += family_size[f];
        family_house[f] = to;
        for (EconomyWorker& k : state.workers){
            if (k.family == f){
                k.house = to;
                if (k.state == WORKER_AT_HOME){
                    GoHome(k,z,walkers);    //moving in
                }
            }
        }
        state.version++;
    };
    auto where = [&](uint32_t f) -> vec2 {
        for (const EconomyWorker& k : state.workers){
            if (k.family == f){
                return k.pos;       //they keep together
            }
        }
        return vec2(0.0f,0.0f);
    };
    //SETTLEMENTS (Economy.h): a family moves only within its settlement - its home's; one with none, the one it
    //stands in (its home just went: CRAMMED, below), or anywhere while it stands in none.
    std::vector<uint32_t> family_settled(family_size.size(),0);
    for (uint32_t f = 1; f < family_size.size(); f++){
        if (family_house[f]){
            family_settled[f] = EconomySettlementOf(state,family_house[f]);
        }else if (family_size[f] > 0){
            GridPick at = w.picker->Pick(where(f));
            family_settled[f] = (at.f_hit && at.plot >= 0 && at.plot < (int)state.settlement.size()) ? state.settlement[at.plot] : 0;
        }
    }
    auto ours = [&](uint32_t f, size_t id){
        uint32_t s = (f < family_settled.size()) ? family_settled[f] : EconomySettlementOf(state,family_house[f]);
        return (!family_house[f] && !s) || s == EconomySettlementOf(state,(uint32_t)id);
    };
    //ON THE ROAD (Economy.h, TAKING A TENT): a family of nobody but tent carriers, with no home - it pitches its own
    //where it likes, so none is found for it.
    std::vector<uint8_t> family_road(family_size.size(),1);
    for (const EconomyWorker& k : state.workers){
        family_road[k.family] = family_road[k.family] && k.tent == ECONOMY_TENT_CARRIED;
    }
    auto on_road = [&](uint32_t f){
        return f < family_road.size() && family_road[f] && !family_house[f];
    };
    /*
        ORPHANS (settlements_plan.md, the user's 2026-10-12: "they can now squeeze in" - lifeline_plan.md had none): a
        household with nobody grown left in it, living where no grown-up lives either - a home of its own, or none -
        squeezes into a home of its settlement where a grown-up does: a house or a camp, the one with the most room to
        spare that they can walk to (the lower id on a tie), as the CRAMMED do (below), and lives from its pantry and
        its family's fetching. It takes no house of its own until one of it is grown. Children going along with their
        grown-ups (WITH THE GROWN) are no orphans: those grown-ups are of their household still.
    */
    std::vector<uint8_t> family_grown(family_size.size(),0);
    std::vector<int> grown_in(n,0);
    for (const EconomyWorker& k : state.workers){
        if (EconomyStageAt(k.born,state.tick) >= LIFE_YOUNG){
            family_grown[k.family] = 1;
            if (k.house && k.house < n){
                grown_in[k.house]++;
            }
        }
    }
    auto orphans = [&](uint32_t f){
        return f < family_grown.size() && family_size[f] > 0 && !family_grown[f];
    };
    for (uint32_t f = 1; f < family_size.size(); f++){
        uint32_t h = family_house[f];
        if (!orphans(f) || (h && h < n && grown_in[h] > 0)){
            continue;
        }
        vec2 at = where(f);
        size_t best = 0;
        for (size_t id = 1; id < n; id++){
            if ((can_take(id) || is_camp(id)) && home_plot[id] >= 0 && grown_in[id] > 0 && ours(f,id)
                && (best == 0 || state.capacity[id] - living[id] > state.capacity[best] - living[best])
                && CanWalk(z,walkers,at,home_plot[id])){
                best = id;
            }
        }
        if (best){
            move(f,(uint32_t)best);
        }
    }
    //...and one CRAMMED into a house with another family (below) waits for one of its own, whether or not the house
    //holds them all (the user's 2026-10-19: four lone archers squeezed into the first of four houses stayed there).
    //Orphans it took in (above) are no other family: they live from it, and would only follow it out.
    auto crammed = [&](uint32_t f){
        uint32_t h = family_house[f];
        if (!is_house(h) || living[h] <= family_size[f]){
            return false;
        }
        int others = living[h] - family_size[f];
        for (uint32_t o = 1; o < family_size.size(); o++){
            if (o != f && family_house[o] == h && orphans(o)){
                others -= family_size[o];
            }
        }
        return others > 0;
    };
    /*
        Into a house: every family without one - with no home, in a camp, or crammed - the largest first. Each
        takes the smallest empty house that holds all of them and that they can walk to (the lower id on a tie).
        None: a smaller family living in one that would, and that could move to an empty house that holds
        it, moves there and lets them in - a house grows a storey at a time, so the first family in often
        took it before it was big.
    */
    std::vector<uint32_t> waiting;
    for (uint32_t f = 1; f < family_size.size(); f++){
        if (family_size[f] > 0 && (!is_house(family_house[f]) || crammed(f)) && !on_road(f) && !orphans(f)){
            waiting.push_back(f);
        }
    }
    std::stable_sort(waiting.begin(),waiting.end(),[&](uint32_t a, uint32_t b){
        return family_size[a] > family_size[b];
    });
    for (uint32_t f : waiting){
        if (is_house(family_house[f]) && !crammed(f)){
            continue;       //crammed no longer: the other family there found a house of its own
        }
        vec2 at = where(f);
        size_t best = 0;
        for (size_t id = 1; id < n; id++){
            if (can_take(id) && living[id] == 0 && state.capacity[id] >= family_size[f] && ours(f,id)
                && (best == 0 || state.capacity[id] < state.capacity[best]) && CanWalk(z,walkers,at,home_plot[id])){
                best = id;
            }
        }
        if (best == 0){
            for (size_t id = 1; id < n && best == 0; id++){
                if (!can_take(id) || living[id] == 0 || living[id] >= family_size[f] || state.capacity[id] < family_size[f]
                    || !ours(f,id) || !CanWalk(z,walkers,at,home_plot[id])){
                    continue;
                }
                uint32_t lodger = 0;
                for (const EconomyWorker& k : state.workers){
                    if (k.house == id){
                        lodger = k.family;
                        break;
                    }
                }
                size_t elsewhere = 0;
                for (size_t e = 1; e < n; e++){
                    if (can_take(e) && living[e] == 0 && state.capacity[e] >= living[id]
                        && EconomySettlementOf(state,(uint32_t)e) == EconomySettlementOf(state,(uint32_t)id)
                        && CanWalk(z,walkers,w.grid->fine.pos[home_plot[id]],home_plot[e])
                        && (elsewhere == 0 || state.capacity[e] < state.capacity[elsewhere])){
                        elsewhere = e;
                    }
                }
                if (lodger && elsewhere && !orphans(lodger)){
                    move(lodger,(uint32_t)elsewhere);
                    best = id;
                }
            }
        }
        if (best){
            move(f,(uint32_t)best);
        }
    }
    /*
        COUPLES (Economy.h): a house still empty, two grown people move into together and found a new family
        there - the eldest unpaired adult first (the lower id on a tie: the workers are in id order), with the
        eldest who can pair with them, into the smallest empty house that holds two and that both can walk to
        (the lower id on a tie). Each keeps their surname; the family each leaves keeps its house.
        Or one of them LIVES ALONE in a house that holds two: the other moves in there (settlements_plan.md, the
        user's 2026-10-11 - three settlers of three families, and the first house went to one of them alone).
        Nobody setting out with a tent pairs off: they are leaving.
    */
    std::vector<size_t> empty;
    for (size_t id = 1; id < n; id++){
        if (can_take(id) && living[id] == 0 && state.capacity[id] >= LIFE_COUPLE_ROOM){
            empty.push_back(id);
        }
    }
    std::stable_sort(empty.begin(),empty.end(),[&](size_t a, size_t b){
        return state.capacity[a] < state.capacity[b];
    });
    auto unpaired = [&](const EconomyWorker& k){
        return !k.partner && k.tent == ECONOMY_TENT_NONE && EconomyStageAt(k.born,state.tick) == LIFE_ADULT;
    };
    std::vector<uint8_t> alone(n,0);
    bool f_alone = false;
    for (const EconomyWorker& k : state.workers){
        if (k.house && k.house < n && living[k.house] == 1 && can_take(k.house) && state.capacity[k.house] >= LIFE_COUPLE_ROOM
            && unpaired(k)){
            alone[k.house] = 1;
            f_alone = true;
        }
    }
    std::vector<size_t> single;
    for (size_t i = 0; i < state.workers.size() && (!empty.empty() || f_alone); i++){
        if (unpaired(state.workers[i])){
            single.push_back(i);
        }
    }
    std::stable_sort(single.begin(),single.end(),[&](size_t a, size_t b){
        return state.workers[a].born < state.workers[b].born;
    });
    //A new family's number: never one of the dead's, so the tree's families stay apart.
    uint32_t next_family = (uint32_t)family_size.size();
    for (const EconomyDeath& d : state.dead){
        next_family = std::max(next_family,d.family + 1);
    }
    for (size_t s = 0; s < single.size() && (!empty.empty() || f_alone); s++){
        EconomyWorker& a = state.workers[single[s]];
        if (a.partner){
            continue;       //paired this pass already, as someone's eldest match
        }
        for (size_t t = 0; t < single.size(); t++){
            EconomyWorker& b = state.workers[single[t]];
            if (t == s || b.partner || b.f_female == a.f_female || b.family == a.family || EconomyRelated(state,a,b)){
                continue;
            }
            //Both of the house's settlement (SETTLEMENTS): nobody pairs off into another one.
            auto fits = [&](size_t id){
                uint32_t s = EconomySettlementOf(state,(uint32_t)id);
                return EconomyPersonSettlement(state,a) == s && EconomyPersonSettlement(state,b) == s
                       && CanWalk(z,walkers,a.pos,home_plot[id]) && CanWalk(z,walkers,b.pos,home_plot[id]);
            };
            //The house one of them lives in alone - the elder's, if both do - else the smallest empty one.
            uint32_t h = 0;
            for (const EconomyWorker* k : {&a,&b}){
                if (!h && k->house && k->house < n && alone[k->house] && fits(k->house)){
                    h = k->house;
                }
            }
            if (!h){
                auto home = std::find_if(empty.begin(),empty.end(),fits);
                if (home == empty.end()){
                    continue;
                }
                h = (uint32_t)*home;
                empty.erase(home);
            }
            uint32_t f = next_family++;
            family_size.resize(f + 1,0);
            family_house.resize(f + 1,0);
            for (EconomyWorker* k : {&a,&b}){
                bool f_moves = k->house != h;
                if (k->house){
                    living[k->house]--;
                    alone[k->house] = 0;    //alone there no longer, or gone from it
                }
                family_size[k->family]--;
                k->family = f;
                k->house = h;
                if (f_moves && k->state == WORKER_AT_HOME){
                    GoHome(*k,z,walkers);   //moving in
                }
            }
            a.partner = b.id;
            b.partner = a.id;
            family_size[f] = 2;
            family_house[f] = h;
            living[h] = 2;
            state.version++;
            break;
        }
    }
    //Still with nowhere: the camp with room for all of them nearest, that they can walk to.
    for (uint32_t f = 1; f < family_size.size(); f++){
        if (family_size[f] == 0 || family_house[f] || on_road(f)){
            continue;
        }
        vec2 at = where(f);
        size_t best = 0;
        float best_d = 1e30f;
        for (size_t id = 1; id < n; id++){
            if (is_camp(id) && state.capacity[id] - living[id] >= family_size[f] && ours(f,id)){
                float d = (w.grid->fine.pos[home_plot[id]] - at).length();
                if (d < best_d && CanWalk(z,walkers,at,home_plot[id])){
                    best_d = d;
                    best = id;
                }
            }
        }
        if (best){
            move(f,(uint32_t)best);
        }
    }
    /*
        CRAMMED (settlements_plan.md, the user's): a family with nowhere even then - its home gone, and no house or
        camp with room for it - squeezes into a home of the settlement it stands in, over its room if need be: a
        house or a camp, the one with the most room to spare that they can walk to (the lower id on a tie). From
        there it moves on to a house of its own as soon as one holds it (above). Standing in no settlement - the
        last home of theirs gone with it - it waits at the camp's spot, as ever.
    */
    for (uint32_t f = 1; f < family_size.size() && f < family_settled.size(); f++){
        if (family_size[f] == 0 || family_house[f] || !family_settled[f] || on_road(f)){
            continue;
        }
        vec2 at = where(f);
        size_t best = 0;
        for (size_t id = 1; id < n; id++){
            if ((can_take(id) || is_camp(id)) && home_plot[id] >= 0 && ours(f,id)
                && (best == 0 || state.capacity[id] - living[id] > state.capacity[best] - living[best])
                && CanWalk(z,walkers,at,home_plot[id])){
                best = id;
            }
        }
        if (best){
            move(f,(uint32_t)best);
        }
    }
}

/*
    WORKERS INTO WORKPLACES (people_plan.md, builders_plan.md). Every workplace has its PLACES (`places`,
    Derive), filled IN ORDER - the builders' huts first, then every other workplace as it was placed (its id)
    - as far as the people go: with fewer housed adults than places, the newest workplaces stand empty (the
    user's). So a new hut takes its builders from the newest workplaces, and a death costs the newest one its
    worker, who moves to the older place left empty.
    A worker keeps his job while his workplace is there and among those the people reach - the lower person
    ids first where a workplace has fewer places than workers. Anyone else is free again. Then each empty
    place, in the order, takes the free housed adult with the best score: his skill for the job, worth
    ECONOMY_SKILL_DISTANCE world units a point, less the straight line from his house to the workplace - the
    lower id on a tie, and only someone who can walk there.
    A house's or the camp's LAND (land_plan.md) has its place in the order by the house's id, and only its own
    family fills it - for the camp, whoever lives there - so it counts against the people only as far as the
    family reaches. It takes its farmers as the order comes to it: those farming it already, then the free of
    the family, then those working somewhere later in the order - best husbandry first, the lower id on a tie.
    The camp's OWN WORK (woodcutter, water carrier, picker) follows its farmers the same way, from those living
    there; what they leave open goes to the rest of the colony after the whole order, to those no place needs.
    So the order is walked once, every workplace keeping its workers (the land choosing its own) before the
    empty places are filled.
    POSTED people (POSTS, defence_plan.md) are out of it altogether: a job one has is his, its place counted
    taken before the order is walked - never filled again, never moved by it - while its workplace is there to
    have it; a jobless one is given none until he is released. A TOWER'S GUARDS are kept the same way (zombies_plan.md
    step 3b), so a tower is never what a shortage of people empties: the newest of the other workplaces is.
    SETTLEMENTS (Economy.h): a workplace is filled from its own settlement's people alone, and counted against
    them - each settlement's newest workplaces stand empty when its people run short, not another's.
*/
void Economy::AssignJobs(const ZoneState& z, Walkers& walkers){
    SettleLand(z,walkers);
    const ChasmWorld& w = *state.world;
    size_t n = std::min(z.buildings.size(),state.places.size());
    //The camp's own woodcutter's places (THE CAMP'S OWN WORK) - a camp with no land has those alone.
    auto cutters = [&](uint32_t id){
        return (id < state.cutter_places.size()) ? state.cutter_places[id] : 0;
    };
    //...and its water carrier's (settlements_plan.md), where it has water near.
    auto drawers = [&](uint32_t id){
        return (id < state.drawer_places.size()) ? state.drawer_places[id] : 0;
    };
    //...and its picker's (foraging_plan.md), where a plant bears in its land.
    auto pickers = [&](uint32_t id){
        return (id < state.picker_places.size()) ? state.picker_places[id] : 0;
    };
    //A camp's own places of a job (THE CAMP'S OWN WORK): its woodcutter's, water carrier's or picker's.
    auto camp_places = [&](uint32_t id, int job){
        return (job == WORKER_JOB_WOODCUTTER) ? cutters(id) : (job == WORKER_JOB_WATER) ? drawers(id)
             : (job == WORKER_JOB_PICKER) ? pickers(id) : 0;
    };
    std::vector<uint32_t> order;
    for (int pass = 0; pass < 2; pass++){
        for (size_t id = 1; id < n; id++){
            if ((state.places[id] > 0 || cutters((uint32_t)id) > 0 || drawers((uint32_t)id) > 0 || pickers((uint32_t)id) > 0)
                && (z.buildings[id].kind == ZONE_KIND_BUILDERS) == (pass == 0)){
                order.push_back((uint32_t)id);
            }
        }
    }
    //Housed, and of an age to work: a child playing is no place's (LIFELINES); nor is anyone posted.
    auto employable = [&](const EconomyWorker& k){
        return k.house && HomePlot(k) >= 0 && !EconomyStageJob(k.job) && k.post < 0;
    };
    auto is_land = [&](uint32_t id){
        return z.buildings[id].kind == ZONE_KIND_HOUSE || z.buildings[id].kind == ZONE_KIND_CAMP;
    };
    //Off an errand for his old job: home - or, with a load on him (a woodcutter's first log, out for a second),
    //to a store with it, put down where he is if none takes it (crates_plan.md, step 5). Home with it, it was
    //neither put down nor carried on.
    auto leave_errand = [&](EconomyWorker& k){
        k.prop = -1;
        if (k.carry > 0 && FindStore(k,z,walkers)){
            return;
        }
        if (k.carry > 0){
            DropLoad(k,z);
        }
        GoHome(k,z,walkers);
    };
    /*
        OLD AGE (Economy.h): whoever is old and has a job still gives it up before anything is kept, so his place is free
        for the order below as a caring mother's is, and he is no place's again. On his way to his work or at it, he goes
        home - a load on him to its store first. A guard comes down from his tower, and a trainee off the line, by
        themselves once the job is gone. An order he is under ends here: the old do not guard.
    */
    for (EconomyWorker& k : state.workers){
        if (EconomyStageAt(k.born,state.tick) != LIFE_OLD || EconomyStageJob(k.job)){
            continue;
        }
        if (k.post >= 0){
            Order(k.id,-1,z,walkers,NULL);
        }
        if (k.job != WORKER_JOB_NONE && (k.state == WORKER_TO_WORK || k.state == WORKER_WORKING
                                         || k.state == WORKER_TO_SOW || k.state == WORKER_SOWING)){
            leave_errand(k);
        }
        k.job = WORKER_JOB_OLD;
        k.building = 0;
        k.f_chosen = false;
        state.version++;
    }
    std::vector<int> people(z.buildings.size() + 1,0);     //by settlement: the employable not yet counted to a place
    auto home_of = [&](const EconomyWorker& k){
        return std::min<size_t>(EconomyPersonSettlement(state,k),z.buildings.size());
    };
    auto place_of = [&](uint32_t id){
        return std::min<size_t>(EconomySettlementOf(state,id),z.buildings.size());
    };
    for (const EconomyWorker& k : state.workers){
        people[home_of(k)] += employable(k) ? 1 : 0;
    }
    std::vector<int> reached(n,0);      //of each workplace's places, how many the people reach
    std::vector<int> held(n,0);
    std::vector<uint8_t> kept(state.workers.size(),0);
    /*
        The posted keep theirs first, as far as its places go; one whose workplace went comes back to none. So does a
        guard posted BY HAND (step 8): the player's pick, never swapped out by the auto-fill below nor taken by a
        shortage of people - he loses it only with his tower, or its count lowered past the hand-picked (the lower
        person ids kept).
        Then EVERY OTHER GUARD the same way (zombies_plan.md step 3b, the user's): a tower's guard keeps it until the
        player releases him, as someone ordered out keeps his post - never moved to a place left open nearer the front
        of the order, nor taken by a builders' hut painted with nobody free. One automatic guard was moved off his tower
        mid-wave so, to the range a death had left open, and died climbing down. Last, so a count lowered sends the
        automatic guards home before the hand-picked.
    */
    std::vector<int> reserved(n,0);
    //...a camp's own places, by the job's number: its woodcutter's, its water carrier's, its picker's.
    std::vector<std::array<int,WORKER_JOB_COUNT>> reserved_camp(n,std::array<int,WORKER_JOB_COUNT>{});
    auto keep = [&](size_t i){
        EconomyWorker& k = state.workers[i];
        uint32_t b = k.building;
        bool f_camp_own = b && b < n && z.buildings[b].kind == ZONE_KIND_CAMP
                          && (k.job == WORKER_JOB_WOODCUTTER || k.job == WORKER_JOB_WATER || k.job == WORKER_JOB_PICKER);
        if (f_camp_own && reserved_camp[b][k.job] < camp_places(b,k.job)){
            reserved_camp[b][k.job]++;
            kept[i] = 1;
            people[home_of(k)] -= (employable(k) ? 1 : 0);
        }else if (!f_camp_own && b && b < n && z.buildings[b].size > 0 && JobOf(z.buildings[b].kind) == k.job
                  && reserved[b] < state.places[b]){
            reserved[b]++;
            kept[i] = 1;
            people[home_of(k)] -= (employable(k) ? 1 : 0);      //a guard was counted free above
        }else{
            k.job = WORKER_JOB_NONE;
            k.building = 0;
            k.f_chosen = false;
            state.version++;
        }
    };
    for (size_t i = 0; i < state.workers.size(); i++){
        EconomyWorker& k = state.workers[i];
        if (k.f_chosen && k.job != WORKER_JOB_GUARD){
            k.f_chosen = false;
        }
        if (k.job >= 0 && (k.post >= 0 || k.f_chosen)){
            keep(i);
        }
    }
    for (size_t i = 0; i < state.workers.size(); i++){
        const EconomyWorker& k = state.workers[i];
        if (!kept[i] && k.job == WORKER_JOB_GUARD && k.post < 0 && !k.f_chosen){
            keep(i);
        }
    }
    for (uint32_t id : order){
        int& free = people[place_of(id)];
        if (!is_land(id)){
            reached[id] = std::min(state.places[id] - reserved[id],free);
            free -= reached[id];
            //Its own, as far as its share goes - the lower person ids first; one living in another settlement no more.
            for (size_t i = 0; i < state.workers.size() && held[id] < reached[id]; i++){
                const EconomyWorker& k = state.workers[i];
                if (!kept[i] && k.building == id && k.job == JobOf(z.buildings[id].kind) && employable(k)
                    && home_of(k) == place_of(id)){
                    kept[i] = 1;
                    held[id]++;
                }
            }
            continue;
        }
        //Land: its family's, those already on it first, then the free, then those it takes from a later place.
        std::vector<std::pair<std::pair<int,int>,size_t>> family;     //((rank, -husbandry), index): index order on a tie
        for (size_t i = 0; i < state.workers.size(); i++){
            const EconomyWorker& k = state.workers[i];
            if (kept[i] || k.house != id || !employable(k)){
                continue;
            }
            int rank = (k.job == WORKER_JOB_FARMER && k.building == id) ? 0 : (k.job == WORKER_JOB_NONE) ? 1 : 2;
            family.push_back(std::make_pair(std::make_pair(rank,-(int)k.skills[SKILL_HUSBANDRY]),i));
        }
        std::sort(family.begin(),family.end());
        reached[id] = std::min(state.places[id] - reserved[id],std::min(free,(int)family.size()));
        free -= reached[id];
        for (int t = 0; t < reached[id]; t++){
            size_t i = family[t].second;
            EconomyWorker& k = state.workers[i];
            kept[i] = 1;
            held[id]++;
            if (k.job == WORKER_JOB_FARMER && k.building == id){
                continue;
            }
            if (k.job != WORKER_JOB_NONE && (k.state == WORKER_TO_WORK || k.state == WORKER_WORKING
                                             || k.state == WORKER_TO_SOW || k.state == WORKER_SOWING)){
                //Taken from his old workplace on his way to it or at it: home first. A load he already
                //carries he takes on to its store, as anyone does.
                leave_errand(k);
            }
            k.job = WORKER_JOB_FARMER;
            k.building = id;
            state.version++;
        }
        /*
            The camp's own woodcutter (THE CAMP'S OWN WORK), after its farmer and the same way: of those living there,
            the one it has, then the free, then one taken from a later place - the strongest first. Then its water
            carrier (settlements_plan.md) the same way, the most ingenious first, as a collector's carrier is chosen;
            then its picker (foraging_plan.md), by husbandry - so an outpost of three is a woodcutter, a water carrier
            and a picker. What they leave open goes to the rest of the colony after the order (below).
        */
        for (int camp_job : {WORKER_JOB_WOODCUTTER,WORKER_JOB_WATER,WORKER_JOB_PICKER}){
            int open = std::min(camp_places(id,camp_job) - reserved_camp[id][camp_job],free);
            if (open <= 0){
                continue;
            }
            int skill = EconomyJobSkill(camp_job);
            std::vector<std::pair<std::pair<int,int>,size_t>> best;      //((rank, -skill), index)
            for (size_t i = 0; i < state.workers.size(); i++){
                const EconomyWorker& k = state.workers[i];
                if (kept[i] || k.house != id || !employable(k)){
                    continue;
                }
                int rank = (k.job == camp_job && k.building == id) ? 0 : (k.job == WORKER_JOB_NONE) ? 1 : 2;
                best.push_back(std::make_pair(std::make_pair(rank,-(int)k.skills[skill]),i));
            }
            std::sort(best.begin(),best.end());
            int take = std::min(open,(int)best.size());
            free -= take;
            reserved_camp[id][camp_job] += take;    //...counted as taken, for the colony's turn
            for (int t = 0; t < take; t++){
                EconomyWorker& k = state.workers[best[t].second];
                kept[best[t].second] = 1;
                if (k.job == camp_job && k.building == id){
                    continue;
                }
                if (k.job != WORKER_JOB_NONE && (k.state == WORKER_TO_WORK || k.state == WORKER_WORKING
                                                 || k.state == WORKER_TO_SOW || k.state == WORKER_SOWING)){
                    leave_errand(k);
                }
                k.job = camp_job;
                k.building = id;
                state.version++;
            }
        }
    }
    /*
        THE CAMP'S OWN WORK from the rest of the colony (one camp to a colony, settlements_plan.md; the user's,
        2026-10-14): what its own sleepers left open of a camp's places - all of them once the last has moved into a
        house - goes to the colony's people no other place needs, after every workplace in the order and never taken
        from one. So the picker goes on picking from a house, and a house's field is farmed before anyone draws water
        by hand. Whoever has the job already keeps it here, so his round goes on; the rest are picked once the other
        places are filled (below).
    */
    std::vector<std::array<int,WORKER_JOB_COUNT>> camp_open(n,std::array<int,WORKER_JOB_COUNT>{});
    for (uint32_t id : order){
        if (z.buildings[id].kind != ZONE_KIND_CAMP){
            continue;
        }
        int& free = people[place_of(id)];
        for (int camp_job : {WORKER_JOB_WOODCUTTER,WORKER_JOB_WATER,WORKER_JOB_PICKER}){
            int open = std::min(camp_places(id,camp_job) - reserved_camp[id][camp_job],free);
            for (size_t i = 0; i < state.workers.size() && open > 0; i++){
                const EconomyWorker& k = state.workers[i];
                if (!kept[i] && k.job == camp_job && k.building == id && employable(k) && home_of(k) == place_of(id)){
                    kept[i] = 1;
                    open--;
                    free--;
                }
            }
            camp_open[id][camp_job] = std::max(0,open);
            free -= camp_open[id][camp_job];
        }
    }
    for (size_t i = 0; i < state.workers.size(); i++){
        EconomyWorker& k = state.workers[i];
        if (k.job == WORKER_JOB_NONE || EconomyStageJob(k.job) || kept[i] || k.post >= 0){
            continue;
        }
        bool f_there = k.building && k.building < n && employable(k) && z.buildings[k.building].size > 0
                       && ((JobOf(z.buildings[k.building].kind) == k.job && state.places[k.building] > 0)
                           || (k.job == WORKER_JOB_WOODCUTTER && cutters(k.building) > 0)
                           || (k.job == WORKER_JOB_WATER && drawers(k.building) > 0)
                           || (k.job == WORKER_JOB_PICKER && pickers(k.building) > 0));
        k.job = WORKER_JOB_NONE;
        k.building = 0;
        if (!f_there){
            //His workplace gone: what he was carrying goes with the job, and he goes home.
            k.prop = -1;
            k.carry = 0;
            k.carry_good = -1;
            if (k.state != WORKER_AT_HOME && k.state != WORKER_TO_HOME){
                GoHome(k,z,walkers);
            }
        }else if (k.state == WORKER_TO_WORK || k.state == WORKER_WORKING || k.state == WORKER_TO_SOW || k.state == WORKER_SOWING){
            //Moved by the order, on his way to his old workplace or at it: home. A load he already carries
            //he takes on to its store, as anyone does.
            leave_errand(k);
        }
        state.version++;
    }
    for (uint32_t id : order){
        if (is_land(id)){
            continue;       //its family's alone, chosen above: a place they cannot fill stays empty
        }
        int job = JobOf(z.buildings[id].kind);
        vec2 at = w.grid->fine.pos[home_plot[id]];
        int skill = EconomyJobSkill(job);
        while (held[id] < reached[id]){
            EconomyWorker* best = NULL;
            int best_first = -1;
            float best_score = -1e30f;
            for (EconomyWorker& k : state.workers){
                if (k.job != WORKER_JOB_NONE || !employable(k) || home_of(k) != place_of(id)){
                    continue;
                }
                //A tower's guards by ARCHERY first - what the range trained - and then as any place, by precision
                //(range_plan.md, defence_plan.md).
                int first = (job == WORKER_JOB_GUARD) ? k.archery : 0;
                float score = k.skills[skill] * ECONOMY_SKILL_DISTANCE - (w.grid->fine.pos[HomePlot(k)] - at).length();
                //Only someone who can walk there from his house - a river with no bridge parts a village.
                if ((first > best_first || (first == best_first && score > best_score))
                    && CanWalk(z,walkers,w.grid->fine.pos[HomePlot(k)],home_plot[id])){
                    best_first = first;
                    best_score = score;
                    best = &k;
                }
            }
            if (!best){
                break;      //nobody free can get there: the place stays empty, and he stays free to build
            }
            best->job = job;
            best->building = id;
            held[id]++;
            state.version++;
        }
    }
    //...and the camp's places left to the colony (above), from those still free, as any place's: his skill less the
    //walk from home, and only someone who can walk there.
    for (uint32_t id : order){
        for (int camp_job : {WORKER_JOB_WOODCUTTER,WORKER_JOB_WATER,WORKER_JOB_PICKER}){
            if (camp_open[id][camp_job] <= 0){
                continue;
            }
            vec2 at = w.grid->fine.pos[home_plot[id]];      //a camp has these places only with a door (Derive)
            int skill = EconomyJobSkill(camp_job);
            while (camp_open[id][camp_job] > 0){
                EconomyWorker* best = NULL;
                float best_score = -1e30f;
                for (EconomyWorker& k : state.workers){
                    if (k.job != WORKER_JOB_NONE || !employable(k) || home_of(k) != place_of(id)){
                        continue;
                    }
                    float score = k.skills[skill] * ECONOMY_SKILL_DISTANCE - (w.grid->fine.pos[HomePlot(k)] - at).length();
                    if (score > best_score && (k.house == id || CanWalk(z,walkers,w.grid->fine.pos[HomePlot(k)],home_plot[id]))){
                        best_score = score;
                        best = &k;
                    }
                }
                if (!best){
                    break;
                }
                best->job = camp_job;
                best->building = id;
                camp_open[id][camp_job]--;
                state.version++;
            }
        }
    }
    Tending();
}

/*
    WHOSE LAND (land_plan.md). Every field that stands gets an owner: the player's choice if that house is lived
    in; else the owner it has, while that is a house lived in - so land does not move to a nearer house built
    later, and nobody's farm changes under them; else the nearest lived-in house it can be walked to, by the
    straight line between their walking points (the lower id on a tie, the first of the nearest few); with
    none, the nearest camp that can. So the camp's land, never kept from a house, passes to one as soon as
    one is lived in. A field still a site, or one nobody can walk to, has no owner. Then the land's places
    (LandPlaces).
*/
void Economy::SettleLand(const ZoneState& z, Walkers& walkers){
    const ChasmWorld& w = *state.world;
    size_t n = std::min(z.buildings.size(),std::min(stands.size(),home_plot.size()));
    std::vector<uint8_t> lived(n,0);
    for (const EconomyWorker& k : state.workers){
        if (k.house && k.house < n){
            lived[k.house] = 1;
        }
    }
    auto lived_house = [&](uint32_t id){
        return id && id < n && z.buildings[id].kind == ZONE_KIND_HOUSE && z.buildings[id].size > 0 && stands[id]
               && lived[id] && home_plot[id] >= 0;
    };
    for (EconomyField& f : state.fields){
        uint32_t id = f.building;
        uint32_t owner = 0;
        //SETTLEMENTS (Economy.h): a field's land only ever a house's or camp's of its own settlement.
        auto ours = [&](uint32_t h){
            return EconomySettlementOf(state,h) == EconomySettlementOf(state,id);
        };
        if (id < n && stands[id] && home_plot[id] >= 0){
            if (lived_house(z.buildings[id].land_of) && ours(z.buildings[id].land_of)){
                owner = z.buildings[id].land_of;
            }else if (lived_house(f.owner) && ours(f.owner)){
                owner = f.owner;
            }else{
                vec2 at = w.grid->fine.pos[home_plot[id]];
                std::vector<std::pair<float,uint32_t>> houses;
                std::vector<std::pair<float,uint32_t>> camps;
                for (size_t h = 1; h < n; h++){
                    if (home_plot[h] < 0 || !ours((uint32_t)h)){
                        continue;
                    }
                    float d = (w.grid->fine.pos[home_plot[h]] - at).length();
                    if (lived_house((uint32_t)h)){
                        houses.push_back(std::make_pair(d,(uint32_t)h));
                    }else if (z.buildings[h].kind == ZONE_KIND_CAMP && z.buildings[h].size > 0 && stands[h]){
                        camps.push_back(std::make_pair(d,(uint32_t)h));
                    }
                }
                std::sort(houses.begin(),houses.end());
                std::sort(camps.begin(),camps.end());
                //The way its farmer walks - from the house out to the field's middle. A camp too: land across
                //the chasm from everyone took camp farmers who could never reach it (seen on seed 12). A camp
                //that has it already keeps it unasked, so its land costs no search on every change.
                auto reaches = [&](uint32_t h){
                    return CanWalk(z,walkers,w.grid->fine.pos[home_plot[h]],home_plot[id]);
                };
                for (size_t t = 0; t < houses.size() && t < 4 && !owner; t++){
                    owner = reaches(houses[t].second) ? houses[t].second : 0;
                }
                for (size_t t = 0; t < camps.size() && t < 4 && !owner; t++){
                    owner = (camps[t].second == f.owner || reaches(camps[t].second)) ? camps[t].second : 0;
                }
            }
        }
        if (owner != f.owner){
            f.owner = owner;
            state.version++;
        }
    }
    LandPlaces(z);
}

/*
    The land's PLACES (land_plan.md): each owner's fields shared out in field order, a share taking whole fields
    until it has ECONOMY_FARMER_CELLS cells or more - so five one-cell fields are one farmer's, seven two
    farmers', and one field of ten still one's: a field is never split between farmers. A farmer to a share.
*/
void Economy::LandPlaces(const ZoneState& z){
    size_t n = std::min(z.buildings.size(),state.places.size());
    share_of.assign(z.buildings.size(),-1);
    auto is_land = [&](uint32_t id){
        return id && id < n && z.buildings[id].size > 0
               && (z.buildings[id].kind == ZONE_KIND_HOUSE || z.buildings[id].kind == ZONE_KIND_CAMP);
    };
    for (size_t id = 1; id < n; id++){
        if (is_land((uint32_t)id)){
            state.places[id] = 0;
        }
    }
    std::vector<int> cells(n,0);        //in the owner's last share so far
    for (const EconomyField& f : state.fields){
        uint32_t o = f.owner;
        if (!is_land(o) || f.building >= share_of.size() || f.building >= stands.size() || !stands[f.building]){
            continue;
        }
        if (state.places[o] == 0 || cells[o] >= ECONOMY_FARMER_CELLS){
            //The camp farms ECONOMY_CAMP_FARMERS shares at most (THE CAMP'S OWN WORK): the rest lies fallow, no share.
            if (z.buildings[o].kind == ZONE_KIND_CAMP && state.places[o] >= ECONOMY_CAMP_FARMERS){
                continue;
            }
            state.places[o]++;
            cells[o] = 0;
        }
        cells[o] += (int)cells_of[f.building].size() - site_cells[f.building];
        share_of[f.building] = state.places[o] - 1;
    }
}

/*
    Who farms each share (land_plan.md): the owner's farmers in person-id order, the first the first share's.
    A field grows at its farmer's pace; a share with nobody to farm it LIES FALLOW.
*/
void Economy::Tending(){
    size_t n = share_of.size();
    state.farmer_of.assign(n,0);
    field_pace.assign(n,0.0f);
    std::vector<std::vector<const EconomyWorker*>> farmers(n);
    for (const EconomyWorker& k : state.workers){
        if (k.job == WORKER_JOB_FARMER && k.building < n){
            farmers[k.building].push_back(&k);
        }
    }
    for (const EconomyField& f : state.fields){
        if (f.building >= n || share_of[f.building] < 0 || f.owner >= n){
            continue;
        }
        const std::vector<const EconomyWorker*>& list = farmers[f.owner];
        int s = share_of[f.building];
        if (s < (int)list.size()){
            state.farmer_of[f.building] = list[s]->id;
            //Posted (defence_plan.md), his share waits for him: still his, and growing nothing meanwhile. Crowded at
            //home, slower (building_size_plan.md).
            field_pace[f.building] = (list[s]->post >= 0) ? 0.0f
                                   : EconomySkillFactor(list[s]->skills[SKILL_HUSBANDRY]) * CrowdPace(*list[s]);
        }
    }
}

void Economy::Crowding(const ZoneState& z){
    std::vector<int> living(z.buildings.size(),0);
    for (const EconomyWorker& k : state.workers){
        if (k.house < living.size()){
            living[k.house]++;
        }
    }
    std::vector<int> over(z.buildings.size(),0);
    for (size_t h = 1; h < over.size() && h < state.capacity.size(); h++){
        if (z.buildings[h].kind == ZONE_KIND_HOUSE){
            over[h] = std::max(0,living[h] - state.capacity[h]);
        }
    }
    if (over != crowded_by){
        crowded_by.swap(over);
        Tending();
    }
}

float Economy::CrowdPace(const EconomyWorker& k) const{
    return EconomyCrowdedPace((k.house < crowded_by.size()) ? crowded_by[k.house] : 0);
}

/*
    From his house to his workplace to fetch what it has made - the hut's pile, the collector's water - when a
    store will take a load of it. True if he set off. A farmer's harvest is FetchHarvest's: his land is fields.
    A MADE good (STOCK KEPT) - a ropemaker's rope, a sailmaker's canvas - only while the stores lack some of its level, and
    no more than that.
*/
bool Economy::FetchFromWorkplace(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    if (!k.building || home_plot[k.building] < 0){
        return false;
    }
    //The camp's woodcutter leaves its supplies where they are: anyone's already, and taken from there (woodcutting_plan.md).
    if (z.buildings[k.building].kind == ZONE_KIND_CAMP){
        return false;
    }
    const std::array<int,GOOD_COUNT>& own = state.stock[k.building];
    GoodsMask made = WorkplaceMakes(k.job);
    int good = -1;
    for (int gd = 0; gd < GOOD_COUNT; gd++){
        if ((made & GOOD_BIT(gd)) && own[gd] > 0 && (good < 0 || own[gd] > own[good])){
            good = gd;
        }
    }
    if (good < 0){
        return false;
    }
    //A woodcutter's a log's worth at a time, as his workplace makes one (log_wood).
    int log_wood = (k.building < state.log_wood.size() && state.log_wood[k.building] > 0) ? state.log_wood[k.building]
                                                                                           : ECONOMY_WOOD_PER_TREE;
    int per_load = (k.job == WORKER_JOB_WOODCUTTER) ? log_wood : ECONOMY_LOAD;
    //A load each for those of his workplace already on their way to take one here (WORKERS BY PLOTS,
    //building_size_plan.md): not his to come for too.
    int there = own[good];
    for (const EconomyWorker& o : state.workers){
        if (&o != &k && o.building == k.building && o.job == k.job && o.prop < 0 && o.carry <= 0
            && (o.state == WORKER_TO_WORK || o.state == WORKER_WORKING)){
            there -= per_load;
        }
    }
    int load = std::min(per_load,there);
    //Water, steam, rope and canvas only by the load: they come a unit at a time, and a trip for one is a waste - but a
    //made good's last few for its level all the same.
    int least = (k.job == WORKER_JOB_WATER || k.job == WORKER_JOB_STEAM || k.job == WORKER_JOB_CONDENSER
                 || EconomyHempWorkOf(k.job)) ? ECONOMY_LOAD : 1;
    if (GOODS_MADE & GOOD_BIT(good)){
        //His workplace's colony's level, less the loads those others are coming for: what they take there is not on
        //its way to a store yet, so KeptShort cannot count it - and two sailmakers walked for one load (steam_plan.md
        //step 4).
        int short_by = KeptShort(z,good,EconomySettlementOf(state,k.building)) - (own[good] - there);
        load = std::min(load,short_by);
        least = std::min(least,short_by);
    }
    if (load <= 0 || there < least){
        return false;
    }
    bool f_taken = false;
    for (size_t id = 1; id < state.accepts.size() && !f_taken; id++){
        f_taken = (state.accepts[id] & GOOD_BIT(good)) && Ours(k,(uint32_t)id) && StoreRoomLeft((uint32_t)id,&k) >= load;
    }
    if (!f_taken){
        return false;
    }
    int plot = SourcePlot(k.building,state.world->grid->fine.pos[home_plot[k.building]]);
    if (plot >= 0 && RouteTo(k,z,walkers,plot,state.world->grid->fine.pos[plot])){
        k.prop = -1;
        k.state = WORKER_TO_WORK;
        return true;
    }
    return false;
}

void Economy::BuildTrees(){
    tree_start.clear();
    tree_items.clear();
    plants.clear();
    rocks.clear();
    tree_nx = tree_nz = 0;
    if (!state.world || !state.world->forest){
        return;
    }
    for (size_t i = 0; i < state.world->forest->props.size(); i++){
        if (ForestBears(state.world->forest->props[i])){
            plants.push_back((int)i);
        }
        if (ForestIsRock(state.world->forest->props[i].kind)){
            rocks.push_back((int)i);     //THE STONECUTTER's: few enough to look through whole
        }
    }
    const Grid& g = *state.world->grid;
    tree_origin = g.bounds_min;
    tree_nx = std::max(1,(int)std::ceil((g.bounds_max.x - g.bounds_min.x) / tree_cell));
    tree_nz = std::max(1,(int)std::ceil((g.bounds_max.y - g.bounds_min.y) / tree_cell));
    const std::vector<PropInstance>& props = state.world->forest->props;
    auto bucket = [&](const PropInstance& pr){
        int bx = std::max(0,std::min(tree_nx - 1,(int)((pr.pos.x - tree_origin.x) / tree_cell)));
        int bz = std::max(0,std::min(tree_nz - 1,(int)((pr.pos.z - tree_origin.y) / tree_cell)));
        return bz * tree_nx + bx;
    };
    tree_start.assign(tree_nx * tree_nz + 1,0);
    for (const PropInstance& pr : props){
        if (IsTree(pr.kind)){
            tree_start[bucket(pr) + 1]++;
        }
    }
    for (size_t i = 1; i < tree_start.size(); i++){
        tree_start[i] += tree_start[i - 1];
    }
    tree_items.resize(tree_start.back());
    std::vector<int> fill(tree_start.begin(),tree_start.end() - 1);
    for (size_t i = 0; i < props.size(); i++){
        if (IsTree(props[i].kind)){
            tree_items[fill[bucket(props[i])]++] = (int)i;
        }
    }
}

/*
    What follows from the zones. Called when they change, and on load (with f_share false: the save's
    stocks are already shared out).
*/
void Economy::Derive(const ZoneState& z, Walkers& walkers, bool f_share){
    const ChasmWorld& w = *state.world;
    const Grid& g = *w.grid;
    const GridPicker& p = *w.picker;
    size_t n = z.buildings.size();
    size_t known = state.stock.size();
    state.stock.resize(n,std::array<int,GOOD_COUNT>{});
    state.pantry.resize(n,std::array<int,GOOD_COUNT>{});    //a camp's; split, it stays with the piece keeping the id
    state.heat.resize(std::max<size_t>(n,1),HEAT_NONE);
    state.guards.resize(n,-1);
    state.zeppelin.resize(n,0);
    state.endless.resize(n,0);
    /*
        Where each site with something brought stood at the last Derive: one taken off - cancelled, or its last plot
        erased - leaves it there as a crate (the user's, 2026-10-16), below. A dry plot of it in a settlement's land
        first, so a bridge's goods are left on its near bank, not out on the water or across it.
    */
    std::vector<int> old_spot(plots_of.size(),-1);
    for (size_t id = 1; id < plots_of.size() && id < state.site.size(); id++){
        if (std::none_of(state.site[id].begin(),state.site[id].end(),[](int x){ return x > 0; })){
            continue;
        }
        for (int pass = 0; pass < 2 && old_spot[id] < 0; pass++){
            for (int v : plots_of[id]){
                bool f_land = v < (int)state.settlement.size() && state.settlement[v] != 0;
                if (!w.terrain->wet[v] && (f_land || pass == 1)){
                    old_spot[id] = v;
                    break;
                }
            }
        }
        if (old_spot[id] < 0 && id < cells_of.size() && !cells_of[id].empty()){
            old_spot[id] = CoarseCentre(g,cells_of[id][0]);     //a field's or range's: its middle
        }
    }
    //Every plot building's plots, and a field's first cell.
    plots_of.assign(n,std::vector<int>());
    arches_of.assign(n,std::vector<int>());
    for (size_t v = 0; v < z.building.size(); v++){
        if (z.building[v]){
            plots_of[z.building[v]].push_back((int)v);
        }else if (z.ArchOwnerOf((int)v) && z.ArchOwnerOf((int)v) < n){
            arches_of[z.ArchOwnerOf((int)v)].push_back((int)v);     //apart: plots_of's first plot is its door
        }
    }
    std::vector<int> old_door = home_plot;      //where each stood at the last Derive: a camp gone leaves its goods there
    //...and which were camps then: one gone is of no kind now (Zones: "gone; its id is never handed out again").
    std::vector<uint8_t> was_camp(tents_of.size(),0);
    for (size_t id = 0; id < tents_of.size(); id++){
        was_camp[id] = !tents_of[id].empty();
    }
    home_plot.assign(n,-1);
    state.shut.assign(n,0);
    std::vector<int> first_cell(n,-1);
    cells_of.assign(n,std::vector<int>());
    site_cells.assign(n,0);
    for (size_t c = 0; c < z.field.size(); c++){
        uint32_t id = z.field[c];
        if (!id){
            continue;
        }
        if (first_cell[id] < 0){
            first_cell[id] = (int)c;
        }
        cells_of[id].push_back((int)c);
        site_cells[id] += z.field_built[c] ? 0 : 1;
    }
    /*
        THE ZEPPELIN DOCKS (steam_plan.md step 5): where each one's zeppelin lies and whether it fits - and none built on
        an id that is no dock now, so a dock placed on it later starts afresh.
    */
    dock_lie.assign(n,ZoneDockLie());
    for (size_t id = 1; id < n; id++){
        if (z.buildings[id].kind == ZONE_KIND_DOCK && z.buildings[id].size > 0){
            ZoneDockFrame(w,z,(uint32_t)id,dock_lie[id]);
        }else{
            state.zeppelin[id] = 0;
        }
    }
    for (size_t id = 1; id < n; id++){
        if (z.buildings[id].size <= 0){
            continue;
        }
        if (ZoneKindOnCells(z.buildings[id].kind)){
            //A field's - or a range's - middle vertex, where its people and carriers walk to.
            if (first_cell[id] >= 0){
                home_plot[id] = CoarseCentre(g,first_cell[id]);
            }
        }else if (!plots_of[id].empty()){
            /*
                Its DOOR: its lowest plot that a walk from open ground can end on (Walkers::OpenFromOutside), so
                its family can get home and its workers to it. Its lowest plot alone, as it was, was often in its
                middle - the coarse grid's vertices keep the lowest numbers when it is subdivided, and one with
                every plot round it built over is shut in - and a house like that was never lived in, a workplace
                never staffed. With no way in anywhere, still its lowest, and the building SHUT IN (EconomyShutIn).
                A CAMP's tents need not touch (THE CAMP, Zones.h): its door is in its largest group of touching tents
                (the lowest plot's group on a tie), so a tent pitched far off in its colony does not take its supplies,
                its water carrier and its woodcutter's reach away with it. One in a piece: the same as ever.
            */
            const std::vector<int>* plots = &plots_of[id];
            std::vector<int> group;
            if (z.buildings[id].kind == ZONE_KIND_CAMP && plots_of[id].size() > 1){
                std::vector<int> best;
                std::vector<int> stack;
                std::vector<int> seen;
                for (int start : plots_of[id]){
                    if (std::find(seen.begin(),seen.end(),start) != seen.end()){
                        continue;
                    }
                    group.clear();
                    stack.assign(1,start);
                    seen.push_back(start);
                    while (!stack.empty()){
                        int at = stack.back();
                        stack.pop_back();
                        group.push_back(at);
                        int nb[8];
                        int m = ZonePlotNeighbours(w,at,nb,8);
                        for (int i = 0; i < m; i++){
                            if (z.building[nb[i]] == id && std::find(seen.begin(),seen.end(),nb[i]) == seen.end()){
                                seen.push_back(nb[i]);
                                stack.push_back(nb[i]);
                            }
                        }
                    }
                    if (group.size() > best.size()){
                        best.swap(group);
                    }
                }
                std::sort(best.begin(),best.end());
                group.swap(best);
                plots = &group;
            }
            home_plot[id] = plots->front();
            state.shut[id] = 1;
            for (int v : *plots){
                if (walkers.OpenFromOutside(z,v)){
                    home_plot[id] = v;
                    state.shut[id] = 0;
                    break;
                }
            }
        }
    }
    /*
        A store split by an erase: each new piece takes its share of what the store held, by floor
        area, rounded down; the old id keeps the rest. New pieces come in id order, so it is the same
        split every time.
    */
    if (f_share){
        std::vector<float> area(n,0.0f);
        for (size_t id = known; id < n; id++){
            uint32_t from = z.buildings[id].split_from;
            //A store's goods, and a house's own (ECONOMY_HOUSE_KEEPS) - what either holds goes with its floor.
            int kind = z.buildings[id].kind;
            if (from && from < known && (kind == ZONE_KIND_STORE || kind == ZONE_KIND_HOUSE)){
                area[id] = ZoneBuildingFigures(w,z,(uint32_t)id).floor_area;
                if (area[from] == 0.0f){
                    area[from] = ZoneBuildingFigures(w,z,from).floor_area;
                }
            }
        }
        std::vector<float> total(n,0.0f);
        for (size_t id = known; id < n; id++){
            uint32_t from = z.buildings[id].split_from;
            if (area[id] > 0.0f){
                total[from] += area[id];
            }
        }
        std::vector<std::array<int,GOOD_COUNT>> before(n,std::array<int,GOOD_COUNT>{});
        for (size_t id = 1; id < known; id++){
            if (total[id] > 0.0f){
                before[id] = state.stock[id];
                total[id] += area[id];
            }
        }
        for (size_t id = known; id < n; id++){
            uint32_t from = z.buildings[id].split_from;
            if (area[id] > 0.0f && total[from] > 0.0f){
                for (int gd = 0; gd < GOOD_COUNT; gd++){
                    int share = (int)std::floor(before[from][gd] * area[id] / total[from]);
                    state.stock[id][gd] = share;
                    state.stock[from][gd] -= share;
                }
            }
        }
    }
    //Gone buildings hold nothing: a store pulled down loses what it had, a site what it was brought.
    state.site.resize(n,std::array<int,GOOD_COUNT>{});
    for (size_t id = 1; id < n; id++){
        if (z.buildings[id].size <= 0){
            /*
                ...but a CAMP's supplies and pantry are left where it stood, as a crate (the user's, 2026-10-11): an
                outpost packed up, its last tent taken on, loses nothing it had laid by - and a camp erased the same.
            */
            int door = (id < was_camp.size() && was_camp[id] && id < old_door.size()) ? old_door[id] : -1;
            if (door >= 0 && door < (int)g.fine.pos.size()){
                int at = CratePlotNear(z,door,g.fine.pos[door]);
                for (int gd = 0; gd < GOOD_COUNT && at >= 0; gd++){
                    int amount = state.stock[id][gd] + state.pantry[id][gd];
                    if (amount > 0){
                        AddToCrate(at,g.fine.pos[at],gd,amount,CRATE_CAMP,0);
                    }
                }
            }
            //...and a SITE's goods brought where it stood (old_spot, above): nothing carried to it is lost.
            int spot = (id < old_spot.size()) ? old_spot[id] : -1;
            if (spot >= 0 && spot < (int)g.fine.pos.size()){
                int at = CratePlotNear(z,spot,g.fine.pos[spot]);
                for (int gd = 0; gd < GOOD_COUNT && at >= 0; gd++){
                    if (state.site[id][gd] > 0){
                        AddToCrate(at,g.fine.pos[at],gd,state.site[id][gd],CRATE_SITE,0);
                    }
                }
            }
            state.stock[id] = std::array<int,GOOD_COUNT>{};
            state.pantry[id] = std::array<int,GOOD_COUNT>{};
            state.site[id] = std::array<int,GOOD_COUNT>{};
        }
    }
    /*
        The gardens, lots and walls still to build, and the wood brought to each: a plot that is no longer a
        site - erased, built over, or finished - keeps none (what was left over is spent). A wall's plot is
        one too (line_works_plan.md, step 3), its wood carried once its way is cleared.
    */
    state.ground_site.resize(z.ground.size(),0);
    ground_sites.clear();
    road_sites.clear();
    wall_sites.clear();
    ground_site_stone.assign(z.ground.size(),0);
    for (size_t v = 0; v < z.ground.size(); v++){
        bool f_wall = ZoneWallPlanned(z,(int)v);
        //A terrace still a site (terraces_plan.md stage 2) is one too, its one good stone; nothing else is on it yet.
        if (ZoneTerraceSite(z,(int)v)){
            ground_sites.push_back((int)v);
            ground_site_stone[v] = 1;
        }else if ((ZoneEnclosesGround(z.ground[v]) || f_wall) && !z.ground_built[v] && z.storeys[v] == 0){
            ground_sites.push_back((int)v);
            if (f_wall){
                wall_sites.push_back((int)v);
            }
        }else{
            state.ground_site[v] = 0;
            //The roads still to lay (line_works_plan.md): work, not wood, so not among the sites above.
            if (ZoneRoadPlanned(z,(int)v)){
                road_sites.push_back((int)v);
            }
        }
    }
    //What stands of each building, and what is still to build (Zones.h, CONSTRUCTION).
    stands.assign(n,0);
    site_storeys.assign(n,0);
    for (size_t id = 1; id < n; id++){
        if (ZoneKindOnCells(z.buildings[id].kind)){
            //Something of it built: its farmer is taken on (a site has none, fields_plan.md) - a range's trainees.
            stands[id] = (z.buildings[id].size > 0 && site_cells[id] < (int)cells_of[id].size()) ? 1 : 0;
            continue;
        }
        for (int v : plots_of[id]){
            stands[id] = stands[id] || z.standing[v] > 0;
            site_storeys[id] += z.storeys[v] - z.standing[v];
        }
        //Its arches' floors still to build are its site's too (arches_plan.md step 3).
        for (int v : arches_of[id]){
            site_storeys[id] += ZoneArchFloors(z.ArchOf(v)) - z.ArchBuiltOf(v);
        }
    }
    /*
        How many each workplace employs (builders_plan.md): one, once it stands. A builders' hut a builder
        per plot-storey standing - and one while nothing of it stands yet, who builds it: with everyone at
        work, nobody else would be free to build the hut that frees them. A house's or camp's are its land's
        farmers, worked out with the fields at the end (LandPlaces).
    */
    //Each building's ARCHES' floors (arches_plan.md step 2): plot-storeys of its own, where they are counted below.
    std::vector<int> arch_floors(n,0);
    for (size_t id = 1; id < n; id++){
        for (int v : arches_of[id]){
            arch_floors[id] += z.ArchBuiltOf(v);      //those built (step 3)
        }
    }
    /*
        GUARD COUNTS (step 8): a tower's, the player's, never more than its junction's guards. New, it takes
        ECONOMY_TOWER_GUARDS_PLACED; from a save before the count (Restore's -2), every place, as it had. Anything
        else has none - an id a tower had and lost included, so a tower placed on it later starts afresh.
    */
    for (size_t id = 1; id < n; id++){
        int& count = state.guards[id];
        if (z.buildings[id].kind != ZONE_KIND_TOWER || z.buildings[id].size <= 0 || plots_of[id].empty()){
            count = -1;
            continue;
        }
        int cap = ZoneTowerGuards(w,plots_of[id].front());
        count = std::min(cap,(count == -2) ? cap : (count < 0) ? ECONOMY_TOWER_GUARDS_PLACED : count);
    }
    state.places.assign(n,0);
    for (size_t id = 1; id < n; id++){
        const ZoneBuilding& b = z.buildings[id];
        if (b.size <= 0 || JobOf(b.kind) == WORKER_JOB_NONE || home_plot[id] < 0
            || b.kind == ZONE_KIND_HOUSE || b.kind == ZONE_KIND_CAMP){
            continue;
        }
        if (b.kind == ZONE_KIND_BUILDERS){
            int standing = arch_floors[id];
            for (int v : plots_of[id]){
                standing += z.standing[v];
            }
            state.places[id] = std::max(1,standing);
        }else if (b.kind == ZONE_KIND_RANGE){
            //An archery range (range_plan.md): a trainee a built cell.
            state.places[id] = (int)cells_of[id].size() - site_cells[id];
        }else if (b.kind == ZONE_KIND_TOWER){
            //A guard tower (defence_plan.md): its GUARD COUNT (step 8, above) once it stands whole - its platform up,
            //its stock in.
            state.places[id] = (stands[id] && site_storeys[id] == 0) ? std::max(0,state.guards[id]) : 0;
        }else if (b.kind == ZONE_KIND_ROPEWALK){
            //A ropewalk (goods_plan.md): its ropemaker once ECONOMY_ROPEWALK_MIN_PLOTS of it stand - one plot is too short.
            int standing = 0;
            for (int v : plots_of[id]){
                standing += (z.standing[v] > 0) ? 1 : 0;
            }
            state.places[id] = (standing >= ECONOMY_ROPEWALK_MIN_PLOTS) ? 1 : 0;
        }else if (b.kind == ZONE_KIND_WOODCUTTER || b.kind == ZONE_KIND_WATER || b.kind == ZONE_KIND_STONECUTTER
                  || b.kind == ZONE_KIND_STEAM || b.kind == ZONE_KIND_CONDENSER || b.kind == ZONE_KIND_SAIL_LOFT){
            //WORKERS BY PLOTS (building_size_plan.md): a worker for every ECONOMY_PLOTS_PER_WORKER plots standing, rounded up.
            //A sail loft too (steam_plan.md step 4): unlike the ropewalk, its floor is hands, not speed.
            int standing = 0;
            for (int v : plots_of[id]){
                standing += (z.standing[v] > 0) ? 1 : 0;
            }
            state.places[id] = (standing + ECONOMY_PLOTS_PER_WORKER - 1) / ECONOMY_PLOTS_PER_WORKER;
        }else{
            state.places[id] = stands[id] ? 1 : 0;
        }
        //STOPPED by the player (stopping_work_plan.md): no places, so AssignJobs frees whoever worked it.
        if (b.f_stopped && ZoneKindStoppable(b.kind)){
            state.places[id] = 0;
        }
        //A MINE (stone_plan.md step 5) whose deposit is dug out: no place, its miner free - as when stopped - once he
        //has set his last load down (DugOut).
        if (b.kind == ZONE_KIND_MINE && EconomyDepositLeft(state,MineDepositAt((uint32_t)id)) <= 0 && !MineCarrying((uint32_t)id)){
            state.places[id] = 0;
        }
    }
    //The camp's own woodcutter (THE CAMP'S OWN WORK): a place while it stands - and no hut stands in its settlement
    //(A HUT TAKES OVER, after Settle). Its farmers are its land's (LandPlaces).
    state.cutter_places.assign(n,0);
    for (size_t id = 1; id < n; id++){
        if (z.buildings[id].kind == ZONE_KIND_CAMP && z.buildings[id].size > 0 && stands[id] && home_plot[id] >= 0){
            state.cutter_places[id] = ECONOMY_CAMP_WOODCUTTERS;
        }
    }
    /*
        ...and its own WATER CARRIER (Economy.h, THE CAMP'S OWN WORK; settlements_plan.md): where a river's or a pool's
        edge lies within ECONOMY_CAMP_WATER_REACH of walking from its door - the dry plot beside the wet bank nearest
        by the walk (the lower index on a tie), off the mountain, with nothing built on it but a road or the camp's
        own tents. Nobody walks the wet bank: he draws standing beside it (the user's).
    */
    state.drawer_places.assign(n,0);
    state.draw_plot.assign(n,-1);
    std::vector<float> bank_walk;
    for (size_t id = 1; id < n; id++){
        if (z.buildings[id].kind != ZONE_KIND_CAMP || z.buildings[id].size <= 0 || !stands[id] || home_plot[id] < 0){
            continue;       //a camp that stands, with a door
        }
        walkers.MapFlood(z,std::vector<int>(1,home_plot[id]),ECONOMY_CAMP_WATER_REACH,bank_walk);
        int best = -1;
        for (size_t v = 0; v < bank_walk.size(); v++){
            if (!(bank_walk[v] <= ECONOMY_CAMP_WATER_REACH) || w.terrain->wet[v] || w.terrain->Mountain((int)v)
                || (z.building[v] && z.KindOf((int)v) != ZONE_KIND_CAMP) || (best >= 0 && bank_walk[v] >= bank_walk[best])){
                continue;
            }
            int nb[8];
            int k = ZonePlotNeighbours(w,(int)v,nb,8);
            for (int i = 0; i < k; i++){
                if (w.terrain->wet[nb[i]] && !ZoneBridgeWalkable(z,nb[i])){
                    best = (int)v;
                    break;
                }
            }
        }
        if (best >= 0){
            state.draw_plot[id] = best;
            state.drawer_places[id] = ECONOMY_CAMP_WATER_CARRIERS;
        }
    }
    /*
        What each store takes, and its room. Next to a workplace: that workplace's goods - a woodcutter
        or a collector sharing a fine edge with a store plot, or a field one plot away (buildings keep a
        plot off fields, so beside one is as close as a store gets).
    */
    state.accepts.assign(n,0);
    state.attached.assign(n,0);
    state.keeps.assign(n,0);
    state.room.assign(n,0);
    for (size_t id = 1; id < n; id++){
        if (z.buildings[id].kind != ZONE_KIND_STORE || z.buildings[id].size <= 0 || !stands[id]){
            continue;
        }
        GoodsMask attached = 0;
        for (int v : plots_of[id]){
            int nb[8];
            int k = ZonePlotNeighbours(w,v,nb,8);
            for (int j = 0; j < k; j++){
                int kind = z.KindOf(nb[j]);
                if (kind == ZONE_KIND_WOODCUTTER){
                    attached |= GOOD_BIT(GOOD_WOOD);
                }else if (kind == ZONE_KIND_WATER){
                    attached |= GOOD_BIT(GOOD_WATER);
                }else if (kind == ZONE_KIND_STEAM){
                    attached |= GOOD_BIT(GOOD_STEAM);       //steam_plan.md
                }else if (kind == ZONE_KIND_CONDENSER){
                    //What it makes and what it is made from, as a ropewalk's (steam_plan.md step 3).
                    attached |= GOOD_BIT(GOOD_STEAM) | GOOD_BIT(GOOD_WATER);
                }else if (EconomyHempWorkAt(kind)){
                    //What it makes and what it is made from (goods_plan.md): the hemp brought there, the rope taken - or
                    //at a sail loft the canvas (steam_plan.md step 4).
                    attached |= GOOD_BIT(GOOD_HEMP) | GOOD_BIT(EconomyHempWorkAt(kind)->made);
                }else if (kind == ZONE_KIND_STONECUTTER){
                    attached |= GOOD_BIT(GOOD_STONE);       //stone_plan.md
                }else if (kind == ZONE_KIND_MINE){
                    attached |= GOOD_BIT(GOOD_STONE) | GOOD_BIT(GOOD_FLOATSTONE);     //stone_plan.md step 5
                }
                //A field's crop changes as it is rotated (fields_plan.md): its store takes every crop's good,
                //the foods and hemp. A range on cells makes nothing (range_plan.md), so a store beside one is a
                //store like any other.
                for (int i = 0; i < p.PlotQuadCount(nb[j]); i++){
                    uint32_t cell_building = z.field[g.fine.quads[p.PlotQuadCorner(nb[j],i) / 4].parent];
                    if (cell_building && z.buildings[cell_building].kind == ZONE_KIND_FIELD){
                        attached |= GOODS_CROPS;
                    }
                }
            }
        }
        state.attached[id] = attached;
        state.accepts[id] = attached ? attached : z.buildings[id].allow;
        state.keeps[id] = state.accepts[id];
        state.room[id] = (int)std::floor(ZoneBuildingFigures(w,z,(uint32_t)id).floor_area * ECONOMY_STORE_ROOM);
    }
    //A guard tower keeps its water and provisions (defence_plan.md): its guards' pantry, not a store - nobody
    //delivers to it, so it takes nothing (`accepts`); its room is what it is built with.
    for (size_t id = 1; id < n; id++){
        if (z.buildings[id].kind == ZONE_KIND_TOWER && z.buildings[id].size > 0){
            state.keeps[id] = GOOD_BIT(GOOD_WATER) | GOOD_BIT(GOOD_PROVISIONS);
            int plot = plots_of[id].empty() ? -1 : plots_of[id].front();
            state.room[id] = EconomyTowerStock(w,plot,GOOD_WATER) + EconomyTowerStock(w,plot,GOOD_PROVISIONS);
        }
    }
    /*
        A woodcutter's woodpile: on a lot plot of HIS OWN YARD that stands beside his hut (yards_plan.md) - the
        lowest such plot beside any of his plots. A plot has one owner, so two huts never pile on the same
        spot. The view draws the logs on the same plot. A woodcutter without one fells nothing, and his card
        and NEEDS ATTENTION say so.
    */
    state.pile_plot.assign(n,-1);
    pile_door.assign(n,-1);
    for (size_t id = 1; id < n; id++){
        if (z.buildings[id].kind != ZONE_KIND_WOODCUTTER || z.buildings[id].size <= 0){
            continue;
        }
        for (int v : plots_of[id]){
            int nb[8];
            int k = ZonePlotNeighbours(w,v,nb,8);
            for (int j = 0; j < k; j++){
                int o = nb[j];
                if (z.ground[o] != ZONE_GROUND_LOT || !z.ground_built[o] || z.storeys[o] > 0
                    || ZoneGroundOwner(z,o) != (uint32_t)id){
                    continue;
                }
                if (state.pile_plot[id] < 0 || o < state.pile_plot[id]){
                    state.pile_plot[id] = o;
                }
            }
        }
        //The yard's door: of his hut's plots beside the pile's lot, the lowest (plots_of is in index order).
        for (int v : plots_of[id]){
            int nb[8];
            int k = (state.pile_plot[id] >= 0) ? ZonePlotNeighbours(w,v,nb,8) : 0;
            if (std::find(nb,nb + k,state.pile_plot[id]) != nb + k){
                pile_door[id] = v;
                break;
            }
        }
    }
    /*
        What a log is worth to a woodcutter working here (woodcutting_plan.md, the user's): a bigger shop makes the
        work easier. A hut two storeys high on EVERY plot - so a raised corner of a big hut does not buy it - makes
        more of a log than one storey; the camp, with no hut at all, least.
    */
    state.log_wood.assign(n,0);
    state.pile_room.assign(n,0);
    state.hut_room.assign(n,0);
    //A hut's lots (THE WOODPILE'S ROOM): every standing lot plot of its yard, by area.
    std::vector<float> lot_area(n,0.0f);
    for (size_t v = 0; v < z.ground.size(); v++){
        uint32_t owner = (z.ground[v] == ZONE_GROUND_LOT && z.ground_built[v] && z.storeys[v] == 0) ? ZoneGroundOwner(z,(int)v) : 0;
        if (owner && owner < n && z.buildings[owner].kind == ZONE_KIND_WOODCUTTER){
            lot_area[owner] += p.PlotArea((int)v);
        }
    }
    for (size_t id = 1; id < n; id++){
        if (z.buildings[id].kind == ZONE_KIND_WOODCUTTER && !plots_of[id].empty()){
            bool f_loft = std::all_of(plots_of[id].begin(),plots_of[id].end(),[&](int v){ return z.standing[v] >= 2; });
            state.log_wood[id] = f_loft ? ECONOMY_LOFT_LOG_WOOD : ECONOMY_WOOD_PER_TREE;
            //Indoors by its standing floor - a cheap storehouse - and outside on its lots (the user's, woodcutting_plan.md).
            state.hut_room[id] = EconomyHutRoom(ZoneBuildingFigures(w,z,(uint32_t)id).floor_area);
            state.pile_room[id] = state.hut_room[id] + (int)std::floor(lot_area[id] * ECONOMY_LOT_ROOM);
        }else if (z.buildings[id].kind == ZONE_KIND_CAMP && z.buildings[id].size > 0){
            //The camp's own woodcutter's (THE CAMP'S OWN WORK): his wood goes in with its supplies, up to so much.
            state.log_wood[id] = ECONOMY_CAMP_LOG_WOOD;
            state.pile_room[id] = ECONOMY_CAMP_WOODPILE;
        }
    }
    //A collector holds by its standing floor, as a hut keeps wood (the user's, 2026-10-11): a bigger one buys a buffer.
    //A steam collector the same (steam_plan.md), and a condenser its water.
    state.water_room.assign(n,0);
    for (size_t id = 1; id < n; id++){
        int kind = z.buildings[id].kind;
        if ((kind == ZONE_KIND_WATER || kind == ZONE_KIND_STEAM || kind == ZONE_KIND_CONDENSER) && z.buildings[id].size > 0){
            state.water_room[id] = EconomyCollectorRoom(ZoneBuildingFigures(w,z,(uint32_t)id).floor_area);
        }
    }
    //How many a house holds: its floor area at ECONOMY_PERSON_AREA each, one family at most this big.
    state.capacity.assign(n,0);
    state.burn.assign(n,0);
    state.burn_halves.assign(n,0);
    state.woodshed.assign(n,0);
    state.snowed.assign(n,0);
    float full_winter = CalendarSnowFrontZ(1.0f,w.grid->bounds_min.y,w.grid->bounds_max.y);
    tents_of.assign(n,std::vector<int>());
    for (size_t id = 1; id < n; id++){
        if (!stands[id]){
            continue;       //nobody lives on a site
        }
        if (z.buildings[id].kind == ZONE_KIND_HOUSE && z.buildings[id].size > 0){
            float area = ZoneBuildingFigures(w,z,(uint32_t)id).floor_area;
            state.capacity[id] = std::max(1,std::min(ECONOMY_HOUSE_MOST,(int)std::floor(area / ECONOMY_PERSON_AREA)));
            //Its family's own food and firewood (P5): smaller than a store's, by the same floor area.
            state.keeps[id] = ECONOMY_HOUSE_KEEPS;
            state.room[id] = (int)std::floor(area * ECONOMY_HOUSE_ROOM);
            /*
                HEAT: its fire; its woodshed; whether winter's snow reaches it. HEAT RISES (building_size_plan.md): a
                plot's ground storey burns a whole firewood, each storey over it - and each arch floor - half, so tall
                and packed houses are cheaper to keep warm. In halves, the odd one burnt every other dawn (Heat); the
                day's burn rounded up for the woodshed and fetching.
            */
            int halves = arch_floors[id];
            for (int v : plots_of[id]){
                halves += (z.standing[v] > 0) ? 2 + (z.standing[v] - 1) : 0;
            }
            state.burn_halves[id] = halves * ECONOMY_FIREWOOD_BURN;
            state.burn[id] = (state.burn_halves[id] + 1) / 2;
            state.woodshed[id] = state.burn[id] * ECONOMY_WOODSHED_DAYS;
            state.snowed[id] = (!plots_of[id].empty() && CalendarSnowAt(w.grid->fine.pos[plots_of[id][0]],full_winter)) ? 1 : 0;
        }else if (z.buildings[id].kind == ZONE_KIND_CAMP && z.buildings[id].size > 0){
            //A camp holds a person a tent, any number of families (Zones.h, THE CAMP).
            for (int v : plots_of[id]){
                if (z.standing[v] > 0){     //a tent still to be pitched holds nobody
                    tents_of[id].push_back(v);
                }
            }
            state.capacity[id] = (int)tents_of[id].size();
            //Its fires burn by its floor area too: a plot each, a tent with a fire beside it or none (needs_plan.md).
            state.burn[id] = (int)plots_of[id].size() * ECONOMY_FIREWOOD_BURN;
            state.burn_halves[id] = state.burn[id] * 2;
            /*
                Its families FETCH for it like a house's (the user's, 2026-10-09 - they did not, so a baby born
                there went dry once the settlers' supplies did): a pantry's room and a woodshed by its floor area,
                and whether winter's snow reaches it. A load brought home is all taken in, as the supplies are.
            */
            state.room[id] = (int)std::floor(ZoneBuildingFigures(w,z,(uint32_t)id).floor_area * ECONOMY_HOUSE_ROOM);
            state.woodshed[id] = state.burn[id] * ECONOMY_WOODSHED_DAYS;
            state.snowed[id] = (!plots_of[id].empty() && CalendarSnowAt(w.grid->fine.pos[plots_of[id][0]],full_winter)) ? 1 : 0;
        }
    }
    state.door = home_plot;
    //A growing record for every field, kept in id order.
    size_t at = 0;
    std::vector<EconomyField> fields;
    for (size_t id = 1; id < n; id++){
        while (at < state.fields.size() && state.fields[at].building < id){
            at++;
        }
        if (z.buildings[id].kind != ZONE_KIND_FIELD || z.buildings[id].size <= 0){
            continue;
        }
        if (at < state.fields.size() && state.fields[at].building == id){
            fields.push_back(state.fields[at]);
        }else{
            EconomyField f;     //empty, waiting to be sown (fields_plan.md)
            f.building = (uint32_t)id;
            //A piece split off by an erase stays its field's owner's land (land_plan.md).
            for (const EconomyField& from : state.fields){
                if (z.buildings[id].split_from && from.building == z.buildings[id].split_from){
                    f.owner = from.owner;
                }
            }
            fields.push_back(f);
        }
    }
    state.fields.swap(fields);
    //The land (land_plan.md): its places by the owners the fields have, and who farms each share.
    LandPlaces(z);
    Tending();
    Settle(z,walkers);
    /*
        A HUT TAKES OVER (woodcutting_plan.md, the user's 2026-10-11): a camp has a woodcutter of its own only while no
        woodcutter's hut stands in its settlement. The hut is where its woodcutter works from then on - not a second
        one beside him - so an outpost of three that builds a hut still has somebody free to build.
        A WATER COLLECTOR the same for the camp's water carrier (stopping_work_plan.md, the user's 2026-10-12). Either
        takes over while it STANDS, worked or stopped: a collector stopped with water plenty in store frees its carrier,
        and does not send her to draw water by hand at the camp instead.
    */
    for (size_t id = 1; id < n; id++){
        uint32_t s = EconomySettlementOf(state,(uint32_t)id);
        int kind = z.buildings[id].kind;
        if ((kind != ZONE_KIND_WOODCUTTER && kind != ZONE_KIND_WATER) || z.buildings[id].size <= 0 || !stands[id] || !s){
            continue;
        }
        std::vector<int>& camp_places = (kind == ZONE_KIND_WOODCUTTER) ? state.cutter_places : state.drawer_places;
        for (size_t c = 1; c < n; c++){
            if (camp_places[c] > 0 && EconomySettlementOf(state,(uint32_t)c) == s){
                camp_places[c] = 0;
                if (kind == ZONE_KIND_WATER){
                    state.draw_plot[c] = -1;    //nobody draws there now: chasm_economy no longer names a spot
                }
            }
        }
    }
    /*
        FORAGING (Economy.h): a plant built over is gone for good - and a camp has its own PICKER while a plant still
        stands to bear in its settlement's land, ripe or growing back, so nobody is kept on for land that has none.
        After the settlements, which say whose land it is.
    */
    PlantsCovered(z);
    state.picker_places.assign(n,0);
    {
        std::vector<uint8_t> bearing(n + 1,0);     //by settlement: a plant stands in its land
        for (int i : plants){
            int v = w.forest->props[i].plot;
            if (state.fruit[i] != FORAGE_GONE && state.prop_state[i] == PROP_STATE_STANDING
                && v >= 0 && v < (int)state.settlement.size()){
                bearing[std::min<size_t>(state.settlement[v],n)] = 1;
            }
        }
        for (size_t id = 1; id < n; id++){
            uint32_t s = EconomySettlementOf(state,(uint32_t)id);
            if (z.buildings[id].kind == ZONE_KIND_CAMP && z.buildings[id].size > 0 && stands[id] && home_plot[id] >= 0
                && s && bearing[std::min<size_t>(s,n)]){
                state.picker_places[id] = ECONOMY_CAMP_PICKERS;
            }
        }
    }
    /*
        The roads, walls and sites with a tree in their way that no woodcutter reaches - and nobody else fells one
        now (woodcutting_plan.md): each, for NEEDS ATTENTION. A woodcutter reaches ECONOMY_WOODCUTTER_REACH of
        WALKING from his workplace's door (WoodNear's): a hut that stands, or a camp with its own woodcutter's place -
        after the settlements, since a hut takes a camp's place over.
        ...and of those, the ones he will reach over the ICE midwinter brings (the user's, 2026-10-16: a bridge's far
        bank is felled in winter) - `trees_iced`, so NEEDS ATTENTION says it waits for the ice rather than for a hut.
        By the season's furthest snow, not today's, so it holds all year; it is not looked at again as the snow comes.
    */
    state.trees_unreached.clear();
    state.trees_iced.clear();
    state.wood_reach.assign(g.fine.pos.size(),ECONOMY_WOOD_UNREACHED);
    std::vector<float> walk;
    for (size_t id = 1; id < n; id++){
        bool f_cuts = (z.buildings[id].kind == ZONE_KIND_WOODCUTTER && stands[id] && !z.buildings[id].f_stopped)
                      || state.cutter_places[id] > 0;
        if (f_cuts && home_plot[id] >= 0){
            walkers.MapFlood(z,std::vector<int>(1,home_plot[id]),ECONOMY_WOODCUTTER_REACH,walk);
            for (size_t v = 0; v < walk.size() && v < state.wood_reach.size(); v++){
                if (walk[v] <= ECONOMY_WOODCUTTER_REACH){
                    state.wood_reach[v] = ECONOMY_WOOD_REACHED;
                }
            }
            walkers.MapFlood(z,std::vector<int>(1,home_plot[id]),ECONOMY_WOODCUTTER_REACH,walk,WALK_ICE_WINTER);
            for (size_t v = 0; v < walk.size() && v < state.wood_reach.size(); v++){
                if (walk[v] <= ECONOMY_WOODCUTTER_REACH && state.wood_reach[v] == ECONOMY_WOOD_UNREACHED){
                    state.wood_reach[v] = ECONOMY_WOOD_ICE;
                }
            }
        }
    }
    auto reach_of = [&](const PropInstance& pr){
        return (pr.plot >= 0 && pr.plot < (int)state.wood_reach.size()) ? (int)state.wood_reach[pr.plot] : ECONOMY_WOOD_UNREACHED;
    };
    //A site's trees no woodcutter reaches over dry land: listed - and as iced, while every one of them is over the ice.
    std::vector<std::pair<uint32_t,int>> found;     //site, the furthest reach of one of its trees: reached never
    auto note = [&](uint32_t site, int r){
        if (r != ECONOMY_WOOD_REACHED){
            found.push_back(std::make_pair(site,r));
        }
    };
    std::vector<int> in_way;
    for (const std::vector<int>* sites : {&road_sites,&wall_sites}){
        for (int v : *sites){
            RoadTrees(z,v,in_way);
            for (int i : in_way){
                note(ECONOMY_SITE_GROUND | (uint32_t)v,reach_of(w.forest->props[i]));
            }
        }
    }
    if (w.forest){
        for (size_t i = 0; i < w.forest->props.size() && i < state.prop_state.size(); i++){
            const PropInstance& pr = w.forest->props[i];
            uint32_t site = (IsTree(pr.kind) && state.prop_state[i] == PROP_STATE_STANDING) ? EconomyTreeSite(z,pr.plot,pr.coarse) : 0;
            if (site){
                note(site,reach_of(pr));
            }
        }
    }
    std::sort(found.begin(),found.end());
    for (size_t a = 0; a < found.size();){
        size_t b = a;
        bool f_iced = true;
        for (; b < found.size() && found[b].first == found[a].first; b++){
            f_iced = f_iced && found[b].second == ECONOMY_WOOD_ICE;
        }
        state.trees_unreached.push_back(found[a].first);
        if (f_iced){
            state.trees_iced.push_back(found[a].first);
        }
        a = b;
    }
    state.zones_version = z.version;
}

//--- Settlements (docs/settlements_plan.md) --------------------------------------------------------

/*
    The SETTLEMENTS and their territories (Economy.h). Each has a HEART, its founding home - the oldest home standing
    outside every territory, a camp or a house - reaching as far as the settlement's beds (EconomySettlementReach).
    Its land then GROWS FROM EVERY BUILDING of it that stands (the user's, 2026-10-12: "it should grow with each
    building, even a guard post or a wall"): ECONOMY_SETTLEMENT_BUILDING of walking round each, a guard tower's
    ECONOMY_SETTLEMENT_TOWER, each built wall or gate plot's ECONOMY_SETTLEMENT_WALL - and one standing on land another
    added grows it in turn, so a settlement spreads by building outward. Not a site, nor a field, range, road, garden
    or lot: cheap to lay, they would spread land for next to nothing. Each plot goes to the settlement it is the
    shortest walk from, by any of its buildings - the older on a tie. Then each building's, by its door.
*/
#define ECONOMY_SETTLEMENT_ROUNDS   8       //the most rounds of narrowing the hearts' reaches to their beds
void Economy::Settle(const ZoneState& z, Walkers& walkers){
    size_t plots = state.world->grid->fine.pos.size();
    size_t n = std::min(std::min(z.buildings.size(),stands.size()),std::min(home_plot.size(),state.capacity.size()));
    n = std::min(n,plots_of.size());
    auto is_home = [&](size_t id){
        int kind = z.buildings[id].kind;
        return (kind == ZONE_KIND_CAMP || kind == ZONE_KIND_HOUSE) && z.buildings[id].size > 0 && stands[id]
               && home_plot[id] >= 0 && home_plot[id] < (int)plots;
    };
    //How far a building grows its settlement's land: one standing on plots, a guard tower furthest; 0 for none.
    auto grows = [&](size_t id){
        const ZoneBuilding& b = z.buildings[id];
        //A swamp hut is nobody's (ZoneKindWild): a settlement reaching it does not take it in.
        if (b.size <= 0 || !stands[id] || ZoneKindOnCells(b.kind) || ZoneKindWild(b.kind) || plots_of[id].empty()){
            return 0.0f;
        }
        return (b.kind == ZONE_KIND_TOWER) ? ECONOMY_SETTLEMENT_TOWER : ECONOMY_SETTLEMENT_BUILDING;
    };
    //The walls that stand, a gate's plot with them: each grows the land round itself.
    std::vector<int> walls;
    for (size_t v = 0; v < plots && v < z.ground.size(); v++){
        if (ZoneWallStands(z,(int)v) || (v < z.gate.size() && z.gate[v] != ZONE_GATE_NONE && z.ground_built[v])){
            walls.push_back((int)v);
        }
    }
    const float class_reach[3] = {ECONOMY_SETTLEMENT_TOWER,ECONOMY_SETTLEMENT_BUILDING,ECONOMY_SETTLEMENT_WALL};
    /*
        A heart's reach by its BEDS - those of every home its land took in. From the most for each, narrowed while
        its beds hold up less, and everything worked out again; narrowing only ever leaves homes out, so it settles
        in a round or two: the widest reach each settlement's own beds hold up.
    */
    std::vector<float> heart_reach(n,ECONOMY_SETTLEMENT_REACH);
    std::vector<uint32_t> joined;               //by building id: the settlement whose land it grew, 0 none
    std::vector<uint8_t> wall_joined;
    std::vector<int> order;                     //by founding home's id: its settlement's place, 1 the oldest
    std::vector<uint8_t> has_camp;              //by founding home's id: its settlement has its camp
    std::vector<float> walk;
    auto is_camp = [&](size_t id){
        return z.buildings[id].kind == ZONE_KIND_CAMP;
    };
    for (int round = 0; round < ECONOMY_SETTLEMENT_ROUNDS; round++){
        state.settlement.assign(plots,0);
        state.settlements.clear();
        settlement_walk.assign(plots,INFINITY);
        joined.assign(n,0);
        wall_joined.assign(walls.size(),0);
        order.assign(n,0);
        has_camp.assign(n,0);
        //The plots of the flood in `walk` to settlement s, out to `reach`: each the land of the shortest walk.
        auto claim = [&](uint32_t s, float reach){
            for (size_t v = 0; v < plots && v < walk.size(); v++){
                uint32_t o = state.settlement[v];
                if (walk[v] <= reach && (walk[v] < settlement_walk[v] || (walk[v] == settlement_walk[v] && o && order[s] < order[o]))){
                    settlement_walk[v] = walk[v];
                    state.settlement[v] = s;
                }
            }
        };
        //The land a building would join: its door's, else any plot of it held - a bridge's far end.
        auto land_at = [&](size_t id){
            int door = home_plot[id];
            if (door >= 0 && door < (int)plots && state.settlement[door]){
                return state.settlement[door];
            }
            for (int v : plots_of[id]){
                if (v >= 0 && v < (int)plots && state.settlement[v]){
                    return state.settlement[v];
                }
            }
            return 0u;
        };
        for (;;){
            /*
                GROW, in waves: whatever stands on a settlement's land joins it and adds its own, flooded together by
                settlement and reach - and what that adds may take in more, so again until nothing new joins.
            */
            for (;;){
                std::vector<std::vector<int>> from(state.settlements.size() * 3);
                bool f_new = false;
                for (size_t id = 1; id < n; id++){
                    float r = grows(id);
                    uint32_t s = (r > 0.0f && !joined[id]) ? land_at(id) : 0;
                    /*
                        ONE CAMP TO A COLONY (settlements_plan.md, the user's 2026-10-12): a camp never joins land that
                        has its camp already - it is the heart of its own (FOUND, below), or an outpost pitched beyond the
                        border would be taken in once its own beds had widened the older one's reach to it. On land
                        with none - a colony of houses - it is that colony's camp.
                    */
                    if (s && is_camp(id) && has_camp[s]){
                        continue;
                    }
                    if (s && is_camp(id)){
                        has_camp[s] = 1;
                    }
                    if (s){
                        joined[id] = s;
                        f_new = true;
                        std::vector<int>& f = from[(size_t)(order[s] - 1) * 3 + ((r == ECONOMY_SETTLEMENT_TOWER) ? 0 : 1)];
                        f.insert(f.end(),plots_of[id].begin(),plots_of[id].end());
                    }
                }
                for (size_t i = 0; i < walls.size(); i++){
                    uint32_t s = wall_joined[i] ? 0 : state.settlement[walls[i]];
                    if (s){
                        wall_joined[i] = 1;
                        f_new = true;
                        from[(size_t)(order[s] - 1) * 3 + 2].push_back(walls[i]);
                    }
                }
                if (!f_new){
                    break;
                }
                for (size_t b = 0; b < from.size(); b++){
                    if (!from[b].empty()){
                        walkers.MapFlood(z,from[b],class_reach[b % 3],walk);
                        claim(state.settlements[b / 3].id,class_reach[b % 3]);
                    }
                }
            }
            //FOUND: the oldest home standing outside all land founds the next, from its heart - and a camp that joined
            //no land, wherever it stands: its land then the plots nearer it than the others' (ONE CAMP TO A COLONY).
            size_t found = 0;
            for (size_t id = 1; id < n && !found; id++){
                found = (is_home(id) && !joined[id] && (!state.settlement[home_plot[id]] || is_camp(id))) ? id : 0;
            }
            if (!found){
                break;
            }
            walkers.MapFlood(z,plots_of[found],heart_reach[found],walk);
            if (walk.size() < plots){
                break;      //no map to walk
            }
            EconomySettlement s;
            s.id = (uint32_t)found;
            s.reach = heart_reach[found];
            state.settlements.push_back(s);
            order[found] = (int)state.settlements.size();
            joined[found] = (uint32_t)found;
            has_camp[found] = is_camp(found) ? 1 : 0;
            claim((uint32_t)found,heart_reach[found]);
        }
        //Each one's beds, and whether they hold up its heart's reach.
        bool f_narrowed = false;
        for (EconomySettlement& s : state.settlements){
            s.beds = 0;
            for (size_t h = 1; h < n; h++){
                s.beds += (is_home(h) && joined[h] == s.id) ? state.capacity[h] : 0;
            }
            float held = EconomySettlementReach(s.beds);
            if (held < heart_reach[s.id]){
                heart_reach[s.id] = held;
                f_narrowed = true;
            }
        }
        if (!f_narrowed){
            break;
        }
    }
    state.settlement_of.assign(z.buildings.size(),0);
    for (size_t id = 1; id < z.buildings.size() && id < home_plot.size(); id++){
        int v = home_plot[id];
        state.settlement_of[id] = (z.buildings[id].size > 0 && !ZoneKindWild(z.buildings[id].kind) && v >= 0 && v < (int)plots)
                                  ? state.settlement[v] : 0;
        /*
            A BRIDGE's door is the lower of its two ends, often the far one - on land no settlement holds, and then
            nobody built it, though its near end was placed inside one (the user's camp_3, 2026-10-12). It is the
            settlement's of either end: its door's, else the other's.
        */
        if (!state.settlement_of[id] && z.buildings[id].kind == ZONE_KIND_BRIDGE && id < plots_of.size()){
            for (int e : plots_of[id]){
                if (e >= 0 && e < (int)plots && !state.world->terrain->wet[e] && state.settlement[e]){
                    state.settlement_of[id] = state.settlement[e];
                    break;
                }
            }
        }
    }
    /*
        ITS COLONY NUMBER (Economy.h): each settlement its heart's, in the order founded - so of two hearts carrying one
        number, the older keeps it and the one cut off from it is given the next - then every home its settlement's.
    */
    std::vector<uint32_t> given;
    for (EconomySettlement& s : state.settlements){
        uint32_t c = (s.id < state.colony.size()) ? state.colony[s.id] : 0;
        if (!c || std::find(given.begin(),given.end(),c) != given.end()){
            c = state.next_colony++;
        }
        s.colony = c;
        given.push_back(c);
    }
    state.colony.assign(z.buildings.size(),0);
    for (size_t h = 1; h < n && h < joined.size(); h++){
        if (is_home(h) && joined[h]){
            state.colony[h] = EconomyColonyNumber(state,joined[h]);
        }
    }
}

bool Economy::Ours(const EconomyWorker& k, uint32_t id) const{
    uint32_t mine = EconomyPersonSettlement(state,k);
    if (EconomyIsCrateStore(id)){
        const EconomyCrate* c = CrateById(id & ~ECONOMY_STORE_CRATE);
        return c && c->plot >= 0 && c->plot < (int)state.settlement.size() && state.settlement[c->plot] == mine;
    }
    if (EconomyIsGroundSite(id)){
        int v = EconomyGroundSitePlot(id);
        return v >= 0 && v < (int)state.settlement.size() && state.settlement[v] == mine;
    }
    return EconomySettlementOf(state,id) == mine;
}

bool EconomyCommandInTerritory(const EconomyState& e, const ChasmWorld& w, int op, int index){
    bool f_places = op == ZONE_OP_BUILD_PAINT || op == ZONE_OP_BUILD_ADD || op == ZONE_OP_FIELD_PAINT
                    || op == ZONE_OP_GROUND_PAINT || op == ZONE_OP_BRIDGE || op == ZONE_OP_RANGE_PAINT || op == ZONE_OP_TERRACE;
    if (!f_places || e.world.get() != &w || e.settlement.empty()){
        return true;
    }
    int plot = index;
    if (ZoneOpOnCell(op)){
        const Grid& g = *w.grid;
        if (index < 0 || index >= (int)g.coarse.quads.size()){
            return true;    //nothing there to refuse: the zones refuse it themselves
        }
        vec2 p(0.0f,0.0f);
        for (int k = 0; k < 4; k++){
            p += g.FineQuadCentre(index * 4 + k) * 0.25f;
        }
        GridPick pick = w.picker->Pick(p);
        plot = pick.f_hit ? pick.plot : -1;
    }
    return plot >= 0 && plot < (int)e.settlement.size() && e.settlement[plot] != 0;
}

//--- Routes ----------------------------------------------------------------------------------------

/*
    A route from where the worker stands to `to_point`, on or beside plot `to_plot`: straight where the
    line is clear, otherwise the grid's A* from the plot he is on, with a straight step at either end
    between the plot and where he actually is. False, and the route left as it was, if there is no way.
    He sets out from the layer he is on, and ends on the one the walkers give `to_plot` for him - a tower's
    deck, a castle's for the posted (ramparts_plan.md); only from the ground to the ground does he go straight.
*/
bool Economy::RouteTo(EconomyWorker& k, const ZoneState& z, Walkers& walkers, int to_plot, const vec2& to_point,
                      int to_layer){
    const ChasmWorld& w = *state.world;
    GridPick here = w.picker->Pick(k.pos);
    WalkPlace from;
    from.plot = here.f_hit ? here.plot : -1;
    from.layer = (uint8_t)EconomyWorkerLayer(k);
    if (from.plot < 0 || to_plot < 0){
        return false;
    }
    int from_plot = from.plot;
    int who = EconomyPosted(k) ? WALK_WHO_POSTED : WALK_WHO_VILLAGER;
    /*
        A WOODCUTTER OVER THE ICE (the user's, 2026-10-16): in winter, his walk to or from ground he gets to only over the
        ice (wood_reach, Derive) - a bridge's far bank, a tree in its way there (WoodNear) - takes the ice; any other walk
        of his goes round as anyone's. A dry way round can be there and be far too long: one went 60 units up the river
        and back. Caught across by the thaw, he waits as anyone cut off does - for the bridge he cleared the way for.
    */
    auto iced = [&](int v){
        return v >= 0 && v < (int)state.wood_reach.size() && state.wood_reach[v] == ECONOMY_WOOD_ICE;
    };
    if (who == WALK_WHO_VILLAGER && k.job == WORKER_JOB_WOODCUTTER && walkers.AnyIce() && (iced(from_plot) || iced(to_plot))){
        who = WALK_WHO_ICE;
    }
    std::vector<vec2> route;
    std::vector<float> speed;
    std::vector<uint8_t> layer;
    if (from.layer == WALK_GROUND && walkers.GoalPlace(z,to_plot,who,to_layer).layer == WALK_GROUND
        && EconomyStraightClear(w,z,k.pos,to_point,from_plot,to_plot)){
        route.push_back(k.pos);
        route.push_back(to_point);
        speed.push_back(ECONOMY_WORKER_SPEED);
    }else{
        std::vector<WalkPlace> places;
        std::vector<float> edge_speed;
        if (!walkers.PlanRoute(z,from,to_plot,who,places,edge_speed,to_layer)){
            return false;
        }
        std::vector<int> plots;
        for (const WalkPlace& p : places){
            plots.push_back(p.plot);
        }
        /*
            Part way along an edge, the plot under him can be the one he has just left: a re-plan when the
            zones change - anything placed, anywhere - walked him back to it and turned him round, and every
            placement made the walkers twitch (24 of 29 on grid routes stepped back, 2026-10-07). When the
            path's first plot is behind him - the turn there sharper than square - and the second can be
            walked to straight, he goes on to the second.
        */
        size_t first = 0;
        if (plots.size() >= 2 && places[0].layer == WALK_GROUND && places[1].layer == WALK_GROUND){
            vec2 p0 = w.grid->fine.pos[plots[0]];
            vec2 p1 = w.grid->fine.pos[plots[1]];
            if ((p0 - k.pos).dot(p1 - p0) < 0.0f && EconomyStraightClear(w,z,k.pos,p1,from_plot,plots[1])){
                first = 1;
            }
        }
        route.push_back(k.pos);
        layer.push_back(places[0].layer);
        for (size_t i = first; i < plots.size(); i++){
            vec2 pt = w.grid->fine.pos[plots[i]];
            if ((pt - route.back()).length() > 1e-4f){
                //The step from where he stands onto the first plot is open ground; after that, the edges' -
                //and on to the second, the first edge's, which he is on.
                speed.push_back((i == 0) ? ECONOMY_WORKER_SPEED : edge_speed[i - 1]);
                route.push_back(pt);
                layer.push_back(places[i].layer);
            }else if (i > 0){
                layer.back() = places[i].layer;     //where he stands is the plot's point: on its layer
            }
        }
        if ((to_point - route.back()).length() > 1e-4f){
            speed.push_back(ECONOMY_WORKER_SPEED);
            route.push_back(to_point);
            layer.push_back(places.back().layer);
        }
        //All of it on the ground: nothing to keep.
        if (std::all_of(layer.begin(),layer.end(),[](uint8_t l){ return l == WALK_GROUND; })){
            layer.clear();
        }
    }
    k.route.swap(route);
    k.speed.swap(speed);
    k.route_layer.swap(layer);
    k.seg = 0;
    k.along = 0.0f;
    k.f_indoors = false;     //setting out
    return true;
}

void Economy::Walk(EconomyWorker& k, float dt){
    float left = dt;
    //Under a load anyone walks by his strength, whatever his job (the user's, 2026-10-11), average at 5: a round
    //is mostly walking, so a woodcutter's felling faster alone made his strength count for little.
    float carry_pace = EconomyCarryPace(k);
    while (k.seg < (int)k.route.size() - 1 && left > 0.0f){
        vec2 a = k.route[k.seg];
        vec2 b = k.route[k.seg + 1];
        float len = (b - a).length();
        //...and slowed behind someone walking his way just ahead (SPACING, following).
        float v = std::max(0.05f,k.speed[k.seg] * carry_pace * k.crowd);
        float need = (len - k.along) / v;
        if (need <= left){
            left -= need;
            k.seg++;
            k.along = 0.0f;
            k.pos = b;
        }else{
            k.along += v * left;
            left = 0.0f;
            k.pos = a + (b - a) * (k.along / std::max(1e-4f,len));
        }
    }
}

static bool Arrived(const EconomyWorker& k){
    return k.seg >= (int)k.route.size() - 1;
}

/*
    Indoors, he is at his door wherever it is: a door the zones moved (Derive) moves him with it, through his
    own house, which nobody sees. Left where the old door was, he could be shut in there by the building that
    moved it - "at home", so never going out, and never fetching (a family died of thirst that way, 2026-10-09).
*/
void Economy::KeepAtDoors(){
    for (EconomyWorker& k : state.workers){
        if (!EconomyIndoors(k) || k.house >= home_plot.size() || home_plot[k.house] < 0){
            continue;
        }
        //A camp's are at their tents, not its door: a baby born there is "indoors", in its mother's tent.
        if (k.house < tents_of.size() && !tents_of[k.house].empty()){
            continue;
        }
        vec2 door = state.world->grid->fine.pos[home_plot[k.house]];
        if ((k.pos - door).length() > 1e-4f){
            k.pos = door;
            k.route.assign(1,door);
            k.speed.clear();
            k.route_layer.clear();      //a door is on the ground
            k.seg = 0;
            k.along = 0.0f;
            state.version++;
        }
    }
}

//Every worker on his way plans again from where he stands - the zones under his route have changed.
void Economy::Replan(const ZoneState& z, Walkers& walkers){
    for (EconomyWorker& k : state.workers){
        //Stranded short of home: perhaps the change opened a way.
        if (k.state == WORKER_AT_HOME && !AtHome(k)){
            GoHome(k,z,walkers);
            continue;
        }
        //A child on his way out to play: home instead, the play given up. A trainee on his way to the range the
        //same - out again at his next look, to wherever his place is now (range_plan.md).
        if (k.state == WORKER_TO_PLAY || k.state == WORKER_TO_RANGE){
            GoHome(k,z,walkers);
            continue;
        }
        //Posted and cut off from his post, or on his way to it: the way there again (defence_plan.md).
        if ((k.state == WORKER_ON_POST && !EconomyAtPost(state,k)) || (k.state == WORKER_TO_POST && !Arrived(k))){
            GoToPost(k,z,walkers);
            continue;
        }
        if (k.state != WORKER_TO_WORK && k.state != WORKER_TO_STORE && k.state != WORKER_TO_HOME
            && k.state != WORKER_TO_FETCH && k.state != WORKER_TO_SITE && k.state != WORKER_TO_ROAD
            && k.state != WORKER_TO_SUPPLY && k.state != WORKER_TO_GET && k.state != WORKER_TO_PANTRY
            && k.state != WORKER_TO_SOW && k.state != WORKER_TO_CRATE && k.state != WORKER_TO_INPUT
            && k.state != WORKER_TO_WORKSHOP){
            continue;
        }
        if (Arrived(k)){
            continue;
        }
        vec2 goal = k.route.back();
        GridPick at = state.world->picker->Pick(goal);
        int goal_plot = at.f_hit ? at.plot : -1;
        if ((k.state == WORKER_TO_WORK || k.state == WORKER_TO_ROAD) && k.prop >= 0){
            goal_plot = state.world->forest->props[k.prop].plot;
        }else if (k.state == WORKER_TO_ROAD){
            goal_plot = EconomyGroundSitePlot(k.site);
        }else if (k.state == WORKER_TO_STORE && k.store < plots_of.size() && !plots_of[k.store].empty()){
            //Re-chosen at arrival if it no longer takes his load; until then, the plot he was making for.
            goal_plot = at.f_hit ? at.plot : plots_of[k.store].front();
        }
        //Home is wherever its door is now: the change may have moved it (Derive), and the old one may be shut in.
        bool f_home = (k.state == WORKER_TO_HOME || k.state == WORKER_TO_PANTRY);
        if (!(f_home ? RouteHome(k,z,walkers) : RouteTo(k,z,walkers,goal_plot,goal))){
            //No way now: stop where he is and think again at the next look - posted, as one cut off from his post.
            StopWhereHeIs(k);
            k.state = (EconomyPosted(k) && k.carry <= 0) ? WORKER_ON_POST : (k.carry > 0) ? WORKER_HOLDING : WORKER_AT_HOME;
            k.prop = -1;
            k.site = 0;         //a carrier cut off from his site gives it up; what he holds to a store, or put down
            if (k.carry <= 0){
                k.carry_good = -1;  //one fetching home names the kind he was after, holding nothing yet
            }
        }
    }
}

//--- The jobs --------------------------------------------------------------------------------------

/*
    The wood within `reach` of `at` that a woodcutter could go for: standing trees and felled logs (only logs,
    with f_logs) within ECONOMY_WOODCUTTER_REACH of his hut, that nobody else is after and that are not under a
    building or a field - a tree IN THE WAY of a planned road or wall (TreesInTheWay), or on a site (EconomyTreeSite),
    first: only a woodcutter fells one (woodcutting_plan.md); then LOGS (the user's, 2026-10-08: logs left lying were
    passed over for the trees nearer the hut), then by the WALK from `at`, the lower index on a tie.
    By the walk, not the straight line (the user's, 2026-10-11: a camp's woodcutter went for a forest across a stream,
    a long way round its end): Walkers::MapFlood from the plot at `at`, the map's own ways - a river crossed on a
    bridge, a cliff by a winch - out to `reach`. From his hut that is his reach; from elsewhere - a second log near the
    first - the hut's is still the straight line, which the first log was well inside.
    OVER THE ICE (the user's, 2026-10-16): in winter, a tree in the way that only the ice gets him to - a bridge's far
    bank - as well, by that walk; never any other tree there (RouteTo takes him over).
*/
void Economy::WoodNear(const EconomyWorker& k, const ZoneState& z, Walkers& walkers, const vec2& at, float reach, bool f_logs,
                       std::vector<std::pair<std::pair<int,float>,int>>& out) const{
    out.clear();
    const ChasmWorld& w = *state.world;
    if (!w.forest || tree_nx == 0 || k.building >= home_plot.size() || home_plot[k.building] < 0){
        return;
    }
    const std::vector<PropInstance>& props = w.forest->props;
    vec2 hut = w.grid->fine.pos[home_plot[k.building]];
    GridPick from = w.picker->Pick(at);
    if (!from.f_hit || from.plot < 0){
        return;
    }
    std::vector<float> walk;
    walkers.MapFlood(z,std::vector<int>(1,from.plot),reach,walk);
    std::vector<float> ice_walk;    //over the ice too: flooded the first time a tree needs it
    bool f_ice = !f_logs && walkers.AnyIce();
    bool f_from_hut = (at - hut).length() < 1e-3f;
    std::vector<int> in_way;
    if (!f_logs){
        TreesInTheWay(k,z,hut,ECONOMY_WOODCUTTER_REACH,in_way);
    }
    std::vector<int> claimed;
    for (const EconomyWorker& o : state.workers){
        if (&o != &k && o.prop >= 0){
            claimed.push_back(o.prop);
        }
    }
    int r = (int)std::ceil(reach / tree_cell);
    int bx = (int)((at.x - tree_origin.x) / tree_cell);
    int bz = (int)((at.y - tree_origin.y) / tree_cell);
    for (int z_ = std::max(0,bz - r); z_ <= std::min(tree_nz - 1,bz + r); z_++){
        for (int x_ = std::max(0,bx - r); x_ <= std::min(tree_nx - 1,bx + r); x_++){
            int b = z_ * tree_nx + x_;
            for (int j = tree_start[b]; j < tree_start[b + 1]; j++){
                int i = tree_items[j];
                int ps = state.prop_state[i];
                if (ps == PROP_STATE_STUMP || (f_logs && ps != PROP_STATE_LOG)){
                    continue;
                }
                const PropInstance& pr = props[i];
                vec2 p(pr.pos.x,pr.pos.z);
                float d = (pr.plot >= 0 && pr.plot < (int)walk.size()) ? walk[pr.plot] : INFINITY;
                bool f_over_ice = false;
                if (!(d <= reach) && f_ice && ps == PROP_STATE_STANDING && pr.plot >= 0
                    && (std::binary_search(in_way.begin(),in_way.end(),i)
                        || (EconomyTreeSite(z,pr.plot,pr.coarse) && Ours(k,EconomyTreeSite(z,pr.plot,pr.coarse))))){
                    if (ice_walk.empty()){
                        walkers.MapFlood(z,std::vector<int>(1,from.plot),reach,ice_walk,WALK_ICE_NOW);
                    }
                    d = (pr.plot < (int)ice_walk.size()) ? ice_walk[pr.plot] : INFINITY;
                    f_over_ice = true;
                }
                if (!(d <= reach) || (!f_from_hut && (p - hut).length() > ECONOMY_WOODCUTTER_REACH)){
                    continue;       //too far to walk, or none to be walked at all
                }
                //Under a building, a garden or lot, or a field: as good as not there (EconomyPropCovered).
                if (EconomyPropCovered(z,pr.plot,pr.coarse)){
                    continue;
                }
                if (std::find(claimed.begin(),claimed.end(),i) != claimed.end()){
                    continue;
                }
                //In the way of a line, or of a site of his own settlement's (EconomyTreeSite): first.
                uint32_t site = (ps == PROP_STATE_STANDING) ? EconomyTreeSite(z,pr.plot,pr.coarse) : 0;
                bool f_way = std::binary_search(in_way.begin(),in_way.end(),i) || (site && Ours(k,site));
                //A FRUIT TREE (foraging_plan.md) he passes by - felled only when it stands in the way of something.
                //...and over the ice, only what is in the way.
                if ((pr.f_fruit && ps == PROP_STATE_STANDING && !f_way) || (f_over_ice && !f_way)){
                    continue;
                }
                int first = f_way ? 0 : (ps == PROP_STATE_LOG) ? 1 : 2;
                out.push_back(std::make_pair(std::make_pair(first,d),i));
            }
        }
    }
    std::sort(out.begin(),out.end());
}

/*
    Out to a piece of wood (`found`, WoodNear's): the first of the nearest few he can get to, standing at its
    foot on the side he comes from. At most a few are tried, so a hut walled off from its wood does not
    search the forest every look.
*/
bool Economy::GoForWood(EconomyWorker& k, const ZoneState& z, Walkers& walkers,
                        const std::vector<std::pair<std::pair<int,float>,int>>& found, size_t tries){
    const std::vector<PropInstance>& props = state.world->forest->props;
    for (size_t t = 0; t < found.size() && t < tries; t++){
        int i = found[t].second;
        const PropInstance& pr = props[i];
        vec2 tree(pr.pos.x,pr.pos.z);
        vec2 to = tree - k.pos;
        float l = to.length();
        vec2 stand = (l > 0.8f) ? tree - to * (0.6f / l) : tree;
        if (RouteTo(k,z,walkers,pr.plot,stand)){
            k.prop = i;
            k.state = WORKER_TO_WORK;
            return true;
        }
    }
    return false;
}

//The woodcutter's next wood, from home: a log lying within reach of his hut, else a tree to fell (WoodNear).
bool Economy::FindTree(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    if (k.building >= home_plot.size() || home_plot[k.building] < 0){
        return false;
    }
    std::vector<std::pair<std::pair<int,float>,int>> found;
    WoodNear(k,z,walkers,state.world->grid->fine.pos[home_plot[k.building]],ECONOMY_WOODCUTTER_REACH,false,found);
    return GoForWood(k,z,walkers,found,4);
}

/*
    THE STONECUTTER's next stone (stone_plan.md), from home, while a store of his settlement's has room for some: of
    what lies within ECONOMY_STONECUTTER_REACH's WALK of his hut (Walkers::MapFlood, as WoodNear), a BOULDER on a site
    of his settlement's first, then RUBBLE - a crate holding stone, less what others are already lifting - then the
    nearest ROCK standing and not built over; by the walk, the lower index on a tie. The first of the nearest few he can
    get to: a crate as a builder goes to one (WORKER_TO_CRATE), a rock as a woodcutter to a tree (WORKER_TO_WORK).
*/
bool Economy::FindRock(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    const ChasmWorld& w = *state.world;
    if (!w.forest || k.building >= home_plot.size() || home_plot[k.building] < 0){
        return false;
    }
    bool f_room = false;
    for (size_t s = 1; s < state.accepts.size() && !f_room; s++){
        f_room = (state.accepts[s] & GOOD_BIT(GOOD_STONE)) && Ours(k,(uint32_t)s) && StoreRoomLeft((uint32_t)s,&k) > 0;
    }
    if (!f_room){
        return false;
    }
    std::vector<float> walk;
    walkers.MapFlood(z,std::vector<int>(1,home_plot[k.building]),ECONOMY_STONECUTTER_REACH,walk);
    auto walked = [&](int plot){
        return (plot >= 0 && plot < (int)walk.size()) ? walk[plot] : INFINITY;
    };
    //(rank, walk), then the prop's index - or a crate's, as -1 - its id.
    std::vector<std::pair<std::pair<int,float>,int>> found;
    for (const EconomyCrate& c : state.crates){
        uint32_t id = ECONOMY_STORE_CRATE | c.id;
        int left = c.stock[GOOD_STONE];
        for (const EconomyWorker& o : state.workers){
            if (&o != &k && o.store == id && (o.state == WORKER_TO_CRATE || o.state == WORKER_LIFTING)){
                left -= ECONOMY_LOAD;
            }
        }
        float d = walked(c.plot);
        if (left > 0 && d <= ECONOMY_STONECUTTER_REACH){
            found.push_back(std::make_pair(std::make_pair(1,d),-1 - (int)c.id));
        }
    }
    for (int i : rocks){
        const PropInstance& pr = w.forest->props[i];
        float d = walked(pr.plot);
        if (state.prop_state[i] != PROP_STATE_STANDING || !(d <= ECONOMY_STONECUTTER_REACH)
            || EconomyPropCovered(z,pr.plot,pr.coarse)){
            continue;
        }
        bool f_claimed = false;
        for (const EconomyWorker& o : state.workers){
            f_claimed = f_claimed || (&o != &k && o.prop == i);
        }
        if (f_claimed){
            continue;
        }
        uint32_t site = (pr.kind == PROP_BOULDER) ? EconomyTreeSite(z,pr.plot,pr.coarse) : 0;
        found.push_back(std::make_pair(std::make_pair((site && Ours(k,site)) ? 0 : 2,d),i));
    }
    std::sort(found.begin(),found.end());
    for (size_t t = 0; t < found.size() && t < 4; t++){
        int i = found[t].second;
        if (i < 0){
            const EconomyCrate* c = CrateById((uint32_t)(-1 - i));
            if (c && RouteTo(k,z,walkers,c->plot,CrateStand(*c,k.pos))){
                k.store = ECONOMY_STORE_CRATE | c->id;
                k.carry_good = GOOD_STONE;      //what he goes for; nothing carried yet
                k.carry = 0;
                k.prop = -1;
                k.state = WORKER_TO_CRATE;
                return true;
            }
            continue;
        }
        const PropInstance& pr = w.forest->props[i];
        vec2 rock(pr.pos.x,pr.pos.z);
        vec2 to = rock - k.pos;
        float l = to.length();
        //Beside it, on the side he comes from: a boulder is broad.
        float off = (pr.kind == PROP_BOULDER) ? 1.0f : 0.6f;
        vec2 stand = (l > off + 0.2f) ? rock - to * (off / l) : rock;
        if (RouteTo(k,z,walkers,pr.plot,stand)){
            k.prop = i;
            k.state = WORKER_TO_WORK;
            return true;
        }
    }
    return false;
}

/*
    A rock BROKEN (stone_plan.md): gone for good, and its stone (ForestRockStone) left where it stood as RUBBLE - on its
    own plot, or where that is no open ground (a boulder under a site) the nearest that is (CratePlotNear). What waited on
    it goes up, as for a felled tree (Felled). He lifts a load of the rubble there and then, off to a store as a builder
    moving a crate does (WORKER_LIFTING) - the rest lies for him, the sites and the builders.
*/
void Economy::BreakRock(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    const ChasmWorld& w = *state.world;
    int prop = k.prop;
    const PropInstance& pr = w.forest->props[prop];
    state.prop_state[prop] = PROP_STATE_STUMP;      //a rock's CLEARED
    state.felled_version++;
    state.version++;
    Event(EVENT_ROCK_BROKEN,k.id,0,pr.plot,vec2(pr.pos.x,pr.pos.z),prop);
    Felled(z,prop);
    k.prop = -1;
    vec2 at(pr.pos.x,pr.pos.z);
    int plot = pr.plot;
    if (!EconomyCrateGround(w,z,plot,at)){
        plot = CratePlotNear(z,pr.plot,at);
        if (plot >= 0){
            at = w.grid->fine.pos[plot];
        }
    }
    const EconomyCrate* c = NULL;
    if (plot >= 0){
        AddToCrate(plot,at,GOOD_STONE,ForestRockStone(pr.kind),CRATE_BROKEN,0);
        c = EconomyCrateOn(state,plot);
    }
    if (!c){
        GoHome(k,z,walkers);    //nowhere open for it near: the stone is lost with the rock
        return;
    }
    k.store = ECONOMY_STORE_CRATE | c->id;
    k.carry_good = GOOD_STONE;
    k.carry = 0;
    k.state = WORKER_LIFTING;
    EconomyStartTimer(k,SecondsToTicks(ECONOMY_PICK_SECONDS));
}

//--- Foraging (docs/foraging_plan.md) --------------------------------------------------------------

//The ripe plant on `plot` with the most fruit, the lower index on a tie, of `plants` - every prop if NULL: -1 none.
static int BestPlantOn(const EconomyState& e, const std::vector<int>* plants, int plot){
    if (!e.world || !e.world->forest || plot < 0){
        return -1;
    }
    const std::vector<PropInstance>& props = e.world->forest->props;
    int best = -1;
    int most = 0;
    auto look = [&](int i){
        int have = (props[i].plot == plot) ? EconomyFruitOn(e,i) : 0;
        if (have > most){
            most = have;
            best = i;
        }
    };
    if (plants){
        for (int i : *plants){
            look(i);
        }
    }else{
        for (size_t i = 0; i < props.size(); i++){
            if (ForestBears(props[i])){
                look((int)i);
            }
        }
    }
    return best;
}

int EconomyFruitFull(const PropInstance& p){
    return !ForestBears(p) ? 0 : p.f_fruit ? FORAGE_TREE_FRUIT : FORAGE_BUSH_FRUIT;
}

int EconomyFruitOn(const EconomyState& e, int prop){
    if (prop < 0 || prop >= (int)e.fruit.size() || prop >= (int)e.prop_state.size()){
        return 0;
    }
    uint8_t f = e.fruit[prop];
    return (f == FORAGE_GONE || e.prop_state[prop] != PROP_STATE_STANDING) ? 0 : (int)f;
}

int EconomyRipePlantOn(const EconomyState& e, int plot){
    return BestPlantOn(e,NULL,plot);
}

int Economy::FruitOn(int prop) const{
    return EconomyFruitOn(state,prop);
}

int Economy::PickFrom(int prop, int want){
    int have = FruitOn(prop);
    int got = std::max(0,std::min(have,want));
    if (got > 0){
        state.fruit[prop] = (uint8_t)(have - got);
        if (state.fruit[prop] == 0){
            state.regrow[prop] = 0;         //emptied: its days to grow back start now
        }
        state.fruit_version++;
        state.version++;
    }
    return got;
}

/*
    AT DAWN, every plant (FORAGING, Economy.h). In winter or under the snow, THE FROST, as a field's: the fruit on it
    is lost, and what was growing back with it - after winter every plant grows its fruit afresh. In growing weather an
    empty plant counts the dawn, and has its fruit again once FORAGE_REGROW_DAYS are counted. One picked but not
    bare waits until it is.
*/
void Economy::FruitAtDawn(uint64_t tick){
    if (!state.world || !state.world->forest){
        return;
    }
    const ChasmWorld& w = *state.world;
    bool f_winter = CalendarDateOf(tick).season == SEASON_WINTER;
    for (int i : plants){
        uint8_t& f = state.fruit[i];
        if (f == FORAGE_GONE || state.prop_state[i] != PROP_STATE_STANDING){
            continue;
        }
        const PropInstance& p = w.forest->props[i];
        if (f_winter || UnderSnow(w,vec2(p.pos.x,p.pos.z),tick)){
            if (f > 0 || state.regrow[i] > 0){
                f = 0;
                state.regrow[i] = 0;
                state.fruit_version++;
            }
            continue;
        }
        if (f == 0 && ++state.regrow[i] >= FORAGE_REGROW_DAYS){
            f = (uint8_t)EconomyFruitFull(p);
            state.regrow[i] = 0;
            state.fruit_version++;
        }
    }
    state.version++;
}

//Built over - a building's standing storey, a garden or lot, a built field (EconomyPropCovered): gone for good, so a
//plant the player cleared does not come back when what covers it is pulled down (the user's).
void Economy::PlantsCovered(const ZoneState& z){
    if (!state.world || !state.world->forest){
        return;
    }
    for (int i : plants){
        const PropInstance& p = state.world->forest->props[i];
        if (state.fruit[i] != FORAGE_GONE && EconomyPropCovered(z,p.plot,p.coarse)){
            state.fruit[i] = FORAGE_GONE;
            state.regrow[i] = 0;
            state.fruit_version++;
            state.version++;
        }
    }
}

bool Economy::PlotOurs(const EconomyWorker& k, int plot) const{
    if (plot < 0 || plot >= (int)state.settlement.size()){
        return false;
    }
    uint32_t s = state.settlement[plot];
    return s == 0 || s == EconomyPersonSettlement(state,k);
}

/*
    THE CAMP'S PICKER: the ripe plants of his own settlement's land - only his, as every errand (Ours) - nobody else is
    after, within `reach` of `at` (all of the land at 0), nearest first by the straight line; out to the first of the
    nearest few he can get to, standing at its foot on the side he comes from, as a woodcutter does at a tree.
*/
bool Economy::GoPick(EconomyWorker& k, const ZoneState& z, Walkers& walkers, const vec2& at, float reach){
    if (!state.world || !state.world->forest){
        return false;
    }
    const std::vector<PropInstance>& props = state.world->forest->props;
    uint32_t mine = EconomyPersonSettlement(state,k);
    if (!mine){
        return false;
    }
    std::vector<int> claimed;
    for (const EconomyWorker& o : state.workers){
        if (&o != &k && o.prop >= 0){
            claimed.push_back(o.prop);
        }
    }
    std::vector<std::pair<float,int>> found;
    for (int i : plants){
        const PropInstance& p = props[i];
        if (p.plot < 0 || p.plot >= (int)state.settlement.size() || state.settlement[p.plot] != mine || FruitOn(i) <= 0){
            continue;
        }
        float d = (vec2(p.pos.x,p.pos.z) - at).length();
        if ((reach > 0.0f && d > reach) || std::find(claimed.begin(),claimed.end(),i) != claimed.end()){
            continue;
        }
        found.push_back(std::make_pair(d,i));
    }
    std::sort(found.begin(),found.end());
    for (size_t t = 0; t < found.size() && t < 4; t++){
        int i = found[t].second;
        const PropInstance& p = props[i];
        vec2 plant(p.pos.x,p.pos.z);
        vec2 to = plant - k.pos;
        float l = to.length();
        vec2 stand = (l > 0.8f) ? plant - to * (0.6f / l) : plant;
        if (RouteTo(k,z,walkers,p.plot,stand)){
            k.prop = i;
            k.store = 0;
            k.state = WORKER_TO_WORK;
            return true;
        }
    }
    return false;
}

int Economy::ForagePlant(const EconomyWorker& k) const{
    return BestPlantOn(state,&plants,k.post);
}

int Economy::ForageWant(const EconomyWorker& k) const{
    if (k.forage == FORAGE_FRUIT){
        int plant = ForagePlant(k);
        int room = std::max(0,NEEDS_SCOUT_FOOD - EconomyPackFood(k)) / EconomyFoodWorth(GOOD_FRUIT);
        return (plant >= 0) ? std::min(FruitOn(plant),room) : 0;
    }
    if (k.forage == FORAGE_WATER){
        //Frozen over while he draws: as an order to draw under the snow is refused (Forage).
        if (UnderSnow(*state.world,k.pos,now)){
            return 0;
        }
        return std::max(0,NEEDS_SCOUT_WATER - k.pack[GOOD_WATER]);
    }
    return 0;
}

int Economy::ForageUnitTicks(const EconomyWorker& k) const{
    if (k.forage == FORAGE_FRUIT){
        return SecondsToTicks(FORAGE_PICK_SECONDS / (EconomySkillFactor(k.skills[EconomyJobSkill(WORKER_JOB_PICKER)]) * CrowdPace(k)));
    }
    return SecondsToTicks(ECONOMY_CAMP_WATER_SECONDS / CrowdPace(k));
}

void Economy::ForageUnit(EconomyWorker& k){
    if (k.forage == FORAGE_FRUIT){
        int plant = ForagePlant(k);
        if (plant >= 0 && PickFrom(plant,1) > 0){
            k.pack[GOOD_FRUIT]++;
        }
    }else if (k.forage == FORAGE_WATER){
        k.pack[GOOD_WATER]++;
    }
    state.version++;
}

/*
    Where someone sent to draw water at wet plot `plot` stands (FORAGING BY HAND): out through the water from it, a few
    plots, for the dry ones along its edge - open as an order finds a plot - nearest the place clicked first, and the
    first of those he can get to. Nobody walks the wet bank: he draws standing beside it, as the camp's carrier does.
*/
#define FORAGE_BANK_WATER       48      //wet plots looked through for the edge, at most

int Economy::BankPlot(const EconomyWorker& k, const ZoneState& z, Walkers& walkers, int plot){
    const ChasmWorld& w = *state.world;
    const Terrain& t = *w.terrain;
    int n_plots = (int)w.grid->fine.pos.size();
    auto wet = [&](int v){
        return t.wet[v] && !ZoneBridgeWalkable(z,v);
    };
    auto open = [&](int v){
        if (v >= (int)z.ground.size() || w.grid->fine.f_boundary[v] || wet(v) || t.Mountain(v)){
            return false;
        }
        int kind = z.KindOf(v);
        return z.ground[v] != ZONE_GROUND_WALL && !ZoneGateShut(z,v) && (kind == ZONE_KIND_NONE || kind == ZONE_KIND_CAMP);
    };
    std::vector<int> seen{plot};
    std::vector<int> water{plot};
    std::vector<std::pair<float,int>> banks;
    const vec2 origin = w.grid->fine.pos[plot];
    for (size_t at = 0; at < water.size() && water.size() < FORAGE_BANK_WATER; at++){
        int nb[8];
        int c = ZonePlotNeighbours(w,water[at],nb,8);
        for (int i = 0; i < c; i++){
            int v = nb[i];
            if (v < 0 || v >= n_plots || std::find(seen.begin(),seen.end(),v) != seen.end()){
                continue;
            }
            seen.push_back(v);
            if (wet(v)){
                water.push_back(v);
            }else if (open(v)){
                banks.push_back(std::make_pair((w.grid->fine.pos[v] - origin).length(),v));
            }
        }
    }
    std::sort(banks.begin(),banks.end());
    for (size_t b = 0; b < banks.size() && b < 6; b++){
        if (CanWalkPosted(z,walkers,k,banks[b].second)){
            return banks[b].second;
        }
    }
    return -1;
}

/*
    The nearest store that takes his load and has room for all of it - by the straight line, the lower
    id on a tie - that he can get to; at it, the plot of it nearest him. A store next to his workplace is
    nearer than any other, which is what makes it his.
*/
bool Economy::FindStore(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    const ChasmWorld& w = *state.world;
    std::vector<std::pair<float,uint32_t>> found;
    for (size_t id = 1; id < state.accepts.size(); id++){
        if (!(state.accepts[id] & GOOD_BIT(k.carry_good)) || !Ours(k,(uint32_t)id)){
            continue;       //SETTLEMENTS: a store of his own
        }
        if (StoreRoomLeft((uint32_t)id,&k) < k.carry){
            continue;       //...with the loads on their way counted, or he goes with the others for one load of room
        }
        float best = 1e30f;
        for (int v : plots_of[id]){
            best = std::min(best,(w.grid->fine.pos[v] - k.pos).length());
        }
        found.push_back(std::make_pair(best,(uint32_t)id));
    }
    std::sort(found.begin(),found.end());
    for (size_t t = 0; t < found.size() && t < 4; t++){
        uint32_t id = found[t].second;
        int plot = -1;
        float best = 1e30f;
        for (int v : plots_of[id]){
            float d = (w.grid->fine.pos[v] - k.pos).length();
            if (d < best){
                best = d;
                plot = v;
            }
        }
        if (RouteTo(k,z,walkers,plot,w.grid->fine.pos[plot])){
            k.store = id;
            k.state = WORKER_TO_STORE;
            return true;
        }
    }
    return false;
}

//The way home. In a camp, home is in front of his tent, where he can be seen; in a house, its door.
bool Economy::RouteHome(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    int home = HomePlot(k);
    bool f_camp = k.house && k.house < z.buildings.size() && z.buildings[k.house].kind == ZONE_KIND_CAMP;
    vec2 at = (home >= 0) ? (f_camp ? ZoneCampDoor(*state.world,home) : state.world->grid->fine.pos[home]) : vec2();
    return home >= 0 && RouteTo(k,z,walkers,home,at);
}

void Economy::GoHome(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    //Done with what he was doing: a pressing need his home cannot meet comes before going back to it.
    if (SeekSupply(k,z,walkers)){
        return;
    }
    k.prop = -1;
    k.store = 0;
    //Posted (defence_plan.md): back to his post, not home - a guard to his tower, by a supply for his pack's water
    //if it is short (PackUp).
    if (EconomyPosted(k)){
        if (!PackUp(k,z,walkers)){
            GoToPost(k,z,walkers);
        }
        return;
    }
    if (RouteHome(k,z,walkers)){
        k.state = WORKER_TO_HOME;
    }else{
        //No way back: he waits where he is, and is home in the sense that he is free.
        StopWhereHeIs(k);
        k.state = WORKER_AT_HOME;
    }
}

/*
    POSTED (defence_plan.md), in the middle of something else - an order lands between ticks, whatever he was
    at: he drops it for his post. A pressing need he is seeing to he finishes first (GoHome sends him on after).
    A storey he was putting up stands with what it has - its wood is in the site already, and nobody else would
    come to finish it. The rest of the errand - a tree, a site, a store - is let go for others (a load on him
    went with the order: Order). Setting out from home, he packs what he will live on from its pantry.
*/
void Economy::TakePost(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    if (k.state == WORKER_TO_SUPPLY || k.state == WORKER_SUPPLYING){
        return;
    }
    if (k.state == WORKER_BUILDING && k.site){
        Build(z,k.site);
    }
    if (k.state == WORKER_AT_HOME && AtHome(k) && k.house && k.house < state.stock.size() && k.house < z.buildings.size()
        && (z.buildings[k.house].kind == ZONE_KIND_HOUSE || z.buildings[k.house].kind == ZONE_KIND_CAMP)){
        std::array<int,GOOD_COUNT> before = k.pack;
        FillPack(k,HomePantry(z,k.house));
        if (z.buildings[k.house].kind == ZONE_KIND_CAMP){
            FillPack(k,state.stock[k.house]);       //then the supplies, as he would eat
        }
        //GATHERING: his home the first place he took from, if it had anything.
        if (k.gather > 0 && k.pack != before){
            k.gather--;
        }
    }
    k.prop = -1;
    k.site = 0;
    k.store = 0;
    k.stint = 0;
    EconomyStartTimer(k,0);
    if (k.carry <= 0){
        k.carry = 0;
        k.carry_good = -1;      //one fetching home names the kind he was after, holding nothing yet
    }
    if (!PackUp(k,z,walkers)){
        GoToPost(k,z,walkers);
    }
}

//The way to his post - a guard's, up onto his tower to his place there; with none, he waits where he is - on his
//post in name, cut off from it (TickWorker, Replan).
void Economy::GoToPost(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    const ChasmWorld& w = *state.world;
    int plot = EconomyPostPlot(state,k);
    //An ordered man on the layer he was sent to (ramparts_plan.md); a guard up on his tower.
    int layer = (k.post >= 0) ? k.post_layer : -1;
    if (plot >= 0 && plot < (int)w.grid->fine.pos.size() && RouteTo(k,z,walkers,plot,EconomyPostPoint(state,k),layer)){
        k.state = WORKER_TO_POST;
        return;
    }
    StopWhereHeIs(k);
    k.state = WORKER_ON_POST;
}

void Economy::RePlace(int plot, int layer, const EconomyWorker* except, const ZoneState& z, Walkers& walkers){
    if (plot < 0){
        return;
    }
    for (EconomyWorker& o : state.workers){
        if (&o != except && o.post == plot && o.post_layer == layer
            && (o.state == WORKER_TO_POST || o.state == WORKER_ON_POST)){
            GoToPost(o,z,walkers);
        }
    }
}

/*
    WHERE ONE OF A GROUP GOES (selection_plan.md, "Several people selected", step 3). The group's orders come nearest
    first, each applied before the next, so the plot clicked fills to its room (EconomyPostRoom) - those posted on it
    already counted - and the rest go to the open plots nearest it, each filled the same way: searched outward through
    plots open as an order finds one and walkable as a walker's ground is (Walkers::GroundEnterable) - or, sent up onto
    a deck, plots with that deck - and taken only where he has a way there. A pocket nobody can walk into is passed
    over, not refused.
*/
#define ECONOMY_SPREAD_PLOTS        48      //plots round the clicked one looked at for a group's places, at most

bool Economy::SpreadPost(const EconomyWorker& k, int plot, int layer, const ZoneState& z, Walkers& walkers, int* to,
                         int* to_layer){
    const ChasmWorld& w = *state.world;
    const Terrain& t = *w.terrain;
    int n_plots = (int)w.grid->fine.pos.size();
    auto room = [&](int v, int lay){
        int others = 0;
        for (const EconomyWorker& o : state.workers){
            others += (&o != &k && o.post == v && o.post_layer == lay) ? 1 : 0;
        }
        return EconomyPostRoom(w,v) - others;
    };
    if (room(plot,layer) > 0){
        return false;
    }
    //The layer he stands on there: the one sent to, or for "its top" its highest deck, else its ground.
    int decks[WALK_MOST_DECKS];
    int n_decks = WalkDeckLayers(z,plot,decks);
    int on = (layer >= 0) ? layer : (n_decks > 0) ? *std::max_element(decks,decks + n_decks) : WALK_GROUND;
    auto open = [&](int v){
        if (v < 0 || v >= n_plots || v >= (int)z.ground.size() || w.grid->fine.f_boundary[v]){
            return false;
        }
        int kind = z.KindOf(v);
        if (z.ground[v] == ZONE_GROUND_WALL || ZoneGateShut(z,v)
            || (kind != ZONE_KIND_NONE && kind != ZONE_KIND_CAMP && !ZoneBridgeWalkable(z,v))){
            return false;
        }
        if (on == WALK_GROUND){
            return t.SameGround(plot,v) && !(t.wet[v] && !ZoneBridgeWalkable(z,v)) && !t.Mountain(v)
                   && !ZoneShutToWalkers(z,v) && !ZoneCastleShuts(z,v);
        }
        int ls[WALK_MOST_DECKS];
        int nl = WalkDeckLayers(z,v,ls);
        return std::find(ls,ls + nl,on) != ls + nl;
    };
    const vec2 origin = w.grid->fine.pos[plot];
    std::vector<int> seen{plot};
    std::vector<int> frontier;
    int next = plot;
    for (int looked = 0; looked < ECONOMY_SPREAD_PLOTS && next >= 0; looked++){
        if (next != plot && room(next,on) > 0 && CanWalkPosted(z,walkers,k,next,on)){
            *to = next;
            *to_layer = on;
            return true;
        }
        int nb[8];
        int n_nb = ZonePlotNeighbours(w,next,nb,8);
        for (int i = 0; i < n_nb; i++){
            if (std::find(seen.begin(),seen.end(),nb[i]) == seen.end()){
                seen.push_back(nb[i]);
                if (open(nb[i])){
                    frontier.push_back(nb[i]);
                }
            }
        }
        auto nearest = std::min_element(frontier.begin(),frontier.end(),[&](int a, int b){
            return (w.grid->fine.pos[a] - origin).length() < (w.grid->fine.pos[b] - origin).length();
        });
        next = (nearest != frontier.end()) ? *nearest : -1;
        if (nearest != frontier.end()){
            frontier.erase(nearest);
        }
    }
    return false;
}

/*
    His PACK filled from a stock (POSTS): water up to NEEDS_PACK_WATER units, food up to NEEDS_PACK_FOOD of the
    health it gives - the kind that keeps best first, since it is what he lives on away from home, and a bean is
    two days in one. A whole unit more while still short, so beans can take it a day past the mark. GATHERING, a
    scout's pack: as much as he carries (NEEDS_SCOUT_*).
*/
void Economy::FillPack(EconomyWorker& k, std::array<int,GOOD_COUNT>& from){
    int water_to = (k.gather > 0) ? NEEDS_SCOUT_WATER : NEEDS_PACK_WATER;
    int food_to = (k.gather > 0) ? NEEDS_SCOUT_FOOD : NEEDS_PACK_FOOD;
    while (from[GOOD_WATER] > 0 && k.pack[GOOD_WATER] < water_to){
        from[GOOD_WATER]--;
        k.pack[GOOD_WATER]++;
        state.version++;
    }
    //Provisions first where there are any - a soldier's food, one is the whole pack's (defence_plan.md).
    static const int keeps_best_first[] = {GOOD_PROVISIONS,GOOD_BEANS,GOOD_WHEAT,GOOD_GREENS,GOOD_FRUIT};
    for (int good : keeps_best_first){
        while (from[good] > 0 && EconomyPackFood(k) < food_to){
            from[good]--;
            k.pack[good]++;
            state.version++;
        }
    }
}

/*
    A GUARD'S PACK (zombies_plan.md step 3b, the user's): every guard carries water for a few days, as the posted do -
    his tower's stock is his pantry, his pack what keeps him there once it runs dry. Packed at home when he sets out
    from there (TakePost); still short of its water - home had none, or he was made a guard out at work and never
    passed it - he goes by the nearest place with some, where he fills it as anyone posted does on a supply trip, and
    then up. Never a tower: a pack filled from the stock it would stand guard over adds nothing. Only on his way up -
    standing on his tower he is never sent down for it. Each stop takes water from where it is, so it ends: with his
    pack full, or nowhere left that has any.
*/
bool Economy::PackUp(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    if (k.job != WORKER_JOB_GUARD || k.post >= 0 || k.pack[GOOD_WATER] >= NEEDS_PACK_WATER || EconomyAtPost(state,k)){
        return false;
    }
    return SeekSupplyOf(k,z,walkers,false,GOOD_BIT(GOOD_WATER),false);
}

/*
    GATHERING (defence_plan.md, "Scouting, provisioned first"): what his pack lacks of a scout's - water, food or both -
    from the nearest place a pressing need would send him to, posted, where some is to be had (SeekSupplyOf). There he
    eats and drinks, fills his pack (FillPack) and, with something still lacking, tries the next place
    (TickWorker, WORKER_SUPPLYING) - ECONOMY_GATHER_STOPS in all.
*/
bool Economy::GatherSupplies(EconomyWorker& k, const ZoneState& z, Walkers& walkers, bool* f_none){
    GoodsMask want = 0;
    if (k.pack[GOOD_WATER] < NEEDS_SCOUT_WATER){
        want |= GOOD_BIT(GOOD_WATER);
    }
    if (EconomyPackFood(k) < NEEDS_SCOUT_FOOD){
        want |= GOODS_EATEN;
    }
    bool f_off = want && SeekSupplyOf(k,z,walkers,false,want);
    if (f_none){
        *f_none = want && !f_off;
    }
    return f_off;
}

//Home again, released: his pack back into the pantry, as far as it keeps each good and has room - a camp's
//pantry takes it all, but PROVISIONS, which no home holds (defence_plan.md). What does not fit stays packed,
//for his next post.
void Economy::Unpack(EconomyWorker& k, const ZoneState& z){
    uint32_t h = k.house;
    if (!h || h >= z.buildings.size() || h >= state.stock.size()){
        return;
    }
    bool f_camp = z.buildings[h].kind == ZONE_KIND_CAMP;
    std::array<int,GOOD_COUNT>& pantry = HomePantry(z,h);
    for (int g = 0; g < GOOD_COUNT; g++){
        if (k.pack[g] <= 0 || g == GOOD_PROVISIONS){
            continue;
        }
        int put = f_camp ? k.pack[g]
                : (h < state.keeps.size() && (state.keeps[h] & GOOD_BIT(g))) ? std::min(k.pack[g],HouseRoom(z,h,g)) : 0;
        if (put > 0){
            pantry[g] += put;
            k.pack[g] -= put;
            state.version++;
        }
    }
}

void Economy::TickWorker(EconomyWorker& k, const ZoneState& z, Walkers& walkers, uint64_t tick, float dt){
    bool f_look = ((tick + k.id) % ECONOMY_LOOK_TICKS) == 0;
    std::array<int,GOOD_COUNT>& own = state.stock[k.building];     //his workplace's: stock[0] for none
    //The camp's own water carrier (THE CAMP'S OWN WORK): draws at the water's edge, not at a collector.
    bool f_draws = k.job == WORKER_JOB_WATER && k.building && k.building < z.buildings.size()
                   && z.buildings[k.building].kind == ZONE_KIND_CAMP;
    //...and its picker (FORAGING): fruit from the plants of its land, not anything at a workplace.
    bool f_picks = k.job == WORKER_JOB_PICKER && k.building && k.building < z.buildings.size()
                   && z.buildings[k.building].kind == ZONE_KIND_CAMP;
    //His pace at his work: by its skill - and slower from a crowded house (APPEAL AND CROWDING, building_size_plan.md).
    float pace = EconomySkillFactor(k.skills[EconomyJobSkill(k.job)]) * CrowdPace(k);
    //What a log he takes up is worth, and the wood his pile holds: his workplace's (woodcutting_plan.md).
    int log_wood = (k.building < state.log_wood.size() && state.log_wood[k.building] > 0) ? state.log_wood[k.building]
                                                                                           : ECONOMY_WOOD_PER_TREE;
    int pile_room = (k.building < state.pile_room.size()) ? state.pile_room[k.building] : 0;
    /*
        Where a woodcutter puts his wood down (THE WOODPILE'S ROOM): on the lot beside his hut, from its door - the
        hut plot beside it - or, with no lot, in the hut at its own door; the camp's own woodcutter in its supplies,
        at its door. -1 with no room anywhere.
    */
    int pile = -1;
    if (k.job == WORKER_JOB_WOODCUTTER && pile_room > 0 && k.building && k.building < home_plot.size()){
        pile = (k.building < pile_door.size() && pile_door[k.building] >= 0) ? pile_door[k.building] : home_plot[k.building];
    }
    /*
        He has a load: to a store that takes it, or - a woodcutter's wood - to his woodpile. With neither he puts it down
        where he stands, a crate (DropLoad), and is free (crates_plan.md, step 5). He used to hold it until something
        took it - with no store at all, for good: a site's carriers stood at a cancelled bridge with its wood.
    */
    auto deliver = [&](){
        if (FindStore(k,z,walkers)){
            return;
        }
        if (pile >= 0 && k.carry_good == GOOD_WOOD && own[GOOD_WOOD] + k.carry <= pile_room){
            if (RouteTo(k,z,walkers,pile,state.world->grid->fine.pos[pile])){
                k.store = 0;
                k.state = WORKER_TO_STORE;
                return;
            }
        }
        DropLoad(k,z);
        GoHome(k,z,walkers);
    };
    //Somewhere that takes `amount` wood whole: a store with the room, or his pile.
    auto wood_room = [&](int amount){
        for (size_t s = 1; s < state.accepts.size(); s++){
            if ((state.accepts[s] & GOOD_BIT(GOOD_WOOD)) && Ours(k,(uint32_t)s) && StoreRoomLeft((uint32_t)s,&k) >= amount){
                return true;
            }
        }
        return pile >= 0 && own[GOOD_WOOD] + amount <= pile_room;
    };
    //...and whether he could take it there from where he stands: deliver's own ways - a store, then his pile - tried on a copy
    //of him, so nothing he is doing changes. The room alone is not enough: a hut walled in by another has a pile with room
    //that nobody can walk to.
    auto wood_goes = [&](int amount){
        EconomyWorker probe = k;
        probe.carry = amount;
        probe.carry_good = GOOD_WOOD;
        return FindStore(probe,z,walkers) || (pile >= 0 && own[GOOD_WOOD] + amount <= pile_room
                                              && RouteTo(probe,z,walkers,pile,state.world->grid->fine.pos[pile]));
    };
    /*
        The camp's picker done at a plant: on to the next ripe one within FORAGE_ROUND_REACH of him while his load has
        room; else back to the camp with what he picked, into its supplies at its door as the water carrier's water
        goes - cut off from it, a store that takes fruit; with nothing picked, home.
    */
    auto pick_on = [&](){
        k.prop = -1;
        if (k.carry < ECONOMY_LOAD && GoPick(k,z,walkers,k.pos,FORAGE_ROUND_REACH)){
            return;
        }
        if (k.carry <= 0){
            k.carry = 0;
            k.carry_good = -1;
            GoHome(k,z,walkers);
            return;
        }
        int door = (k.building < home_plot.size()) ? home_plot[k.building] : -1;
        if (door >= 0 && RouteTo(k,z,walkers,door,state.world->grid->fine.pos[door])){
            k.store = 0;
            k.state = WORKER_TO_STORE;
        }else{
            deliver();
        }
    };
    //Posted, and at something else: dropped for the post (TakePost) - a pressing need seen to first. A guard posted by
    //hand the same (step 8): he goes as an ordered man does, where one the auto-fill picked finishes his errand first.
    if ((k.post >= 0 || (k.f_chosen && k.job == WORKER_JOB_GUARD)) && k.state != WORKER_TO_POST && k.state != WORKER_ON_POST
        && k.state != WORKER_TO_SUPPLY && k.state != WORKER_SUPPLYING){
        TakePost(k,z,walkers);
    }
    //TAKING A TENT: told to take one - on his way to his post, or standing on it - off to the nearest camp with one to
    //spare, before any gathering; none he can get to has one, told so (a NOTICE), and on as he was.
    if (k.tent == ECONOMY_TENT_TO_TAKE && k.post >= 0 && (k.state == WORKER_TO_POST || k.state == WORKER_ON_POST)
        && !GoForTent(k,z,walkers)){
        Notice(EVENT_NO_TENT,k.id);
        k.tent = ECONOMY_TENT_NONE;
        state.version++;
    }
    //GATHERING: told to fill a scout's pack first - on his way to his post, or standing on it - off for it; with
    //nothing to be had, or nowhere he can get to, on as he was - told, if he has taken nothing at all (a NOTICE).
    bool f_none = false;
    if (k.gather > 0 && k.post >= 0 && (k.state == WORKER_TO_POST || k.state == WORKER_ON_POST) && !GatherSupplies(k,z,walkers,&f_none)){
        if (f_none && k.gather == ECONOMY_GATHER_STOPS){
            Notice(EVENT_NOTHING_TO_TAKE,k.id);
        }
        k.gather = 0;
        state.version++;
    }
    switch (k.state){
        case WORKER_AT_HOME:
            if (!f_look){
                break;
            }
            /*
                Not home - his way there was cut (a river, a wall), or a re-plan stopped him short of it: he sees
                to a pressing need wherever he can reach one, his own pantry being no use to him out here, and
                every few looks tries the way home again (a failed search can cover the whole map). He used to
                wait for the zones to change, and when they did not he stood there until he died of thirst.
            */
            if (!AtHome(k)){
                if (!SeekSupply(k,z,walkers,false) && ((tick + k.id) / ECONOMY_LOOK_TICKS) % 4 == 0){
                    GoHome(k,z,walkers);
                }
                break;
            }
            //A baby: indoors in its mother's care, and nothing else (BIRTHS) - fed from the house's stock.
            if (k.job == WORKER_JOB_BABY){
                break;
            }
            //Home from a post: his pack back into the pantry (POSTS).
            if (std::any_of(k.pack.begin(),k.pack.end(),[](int n){ return n > 0; })){
                Unpack(k,z);
            }
            /*
                MEALS: his fill of what his home holds before he goes out again, as at a store - home for one look, he took
                a unit a visit and ate it on the way, and a woodcutter came home hungry for days with his pantry full
                (the user's, 2026-10-14). Needs begins each unit (EatAndDrink); he waits while one is under way or
                another would begin.
            */
            if (EatingAtHome(k,z)){
                break;
            }
            //Before any work: what he needs and his home has run out of (needs_plan.md).
            if (SeekSupply(k,z,walkers)){
                break;
            }
            //A guard (defence_plan.md): out to his tower with his pack, as anyone posted sets out - no errands.
            if (k.job == WORKER_JOB_GUARD){
                TakePost(k,z,walkers);
                break;
            }
            //A child: no work, no errands, no building - out to play now and then (LIFELINES).
            if (k.job == WORKER_JOB_PLAYING){
                GoPlay(k,z,walkers,tick);
                break;
            }
            //Then his house's pantry, running low, if he is the one to go (step 2) - before wood for a site, and
            //before his own round.
            if (FetchHome(k,z,walkers)){
                break;
            }
            //A mother caring for her baby: the house's errands, above, and home again (BIRTHS).
            if (k.job == WORKER_JOB_CARING){
                break;
            }
            //The old: the house's errands, above, and otherwise a walk round the house now and then, as a child
            //plays (OLD AGE).
            if (k.job == WORKER_JOB_OLD){
                GoPlay(k,z,walkers,tick);
                break;
            }
            //A builder, as a caretaker (defence_plan.md): the guard towers kept full first, then provisions wanted,
            //then building - the user's order (KeepTowers, MakeProvisions).
            if (k.job == WORKER_JOB_BUILDER && (KeepTowers(k,z,walkers) || MakeProvisions(k,z,walkers))){
                break;
            }
            //...then a crate the player has put PICK UP (stone_plan.md): ahead of building, as FETCH FIRST was to be - by
            //anyone free, the jobless as well as a builder (the user's, 2026-10-18), since the order is the player's.
            if ((k.job == WORKER_JOB_NONE || k.job == WORKER_JOB_BUILDER) && MoveCrate(k,z,walkers,true)){
                break;
            }
            if (k.job == WORKER_JOB_NONE || k.job == WORKER_JOB_BUILDER){
                //Nothing of his own to do, or a builder: he carries wood to a construction that wants it, or
                //goes out to clear a planned road's or wall's way and lay a road - whichever comes first in
                //the one ranking (FindSiteWork; line_works_plan.md); the line work alone if none of that could go.
                //And a builder, as caretaker, with none of that to do: a crate to a storehouse, LAST of his
                //errands (crates_plan.md, the user's) - never the jobless.
                if (!FindSiteWork(k,z,walkers) && !FindRoadWork(k,z,walkers,false) && k.job == WORKER_JOB_BUILDER){
                    MoveCrate(k,z,walkers);
                }
                break;
            }
            if (!k.house){
                break;
            }
            //A farmer's land (land_plan.md): a field to sow before all else - a harvest can wait, a field sown
            //late grows late - then a harvest to fetch.
            if (k.job == WORKER_JOB_FARMER){
                if (!GoSow(k,z,walkers,tick)){
                    FetchHarvest(k,z,walkers);
                }
                break;
            }
            //A trainee (range_plan.md): out to his place on the line, all year.
            if (k.job == WORKER_JOB_ARCHER){
                GoTrain(k,z,walkers);
                break;
            }
            //A ropemaker or a sailmaker (EconomyHempWork): a load of what his workshop made to a store first, then more
            //made of the hemp there, or hemp fetched for it (MakeOfHemp).
            if (EconomyHempWorkOf(k.job)){
                if (!FetchFromWorkplace(k,z,walkers)){
                    MakeOfHemp(k,z,walkers);
                }
                break;
            }
            //A condenser's hand (steam_plan.md step 3): a load of its water to a store first, then steam fetched for it.
            if (k.job == WORKER_JOB_CONDENSER){
                if (!FetchFromWorkplace(k,z,walkers)){
                    FeedCondenser(k,z,walkers);
                }
                break;
            }
            /*
                The camp's water carrier (settlements_plan.md): out to the water's edge to draw a load, while its
                supplies hold less than ECONOMY_CAMP_WATER_HOLD - not while snow lies there, as a collector stops.
            */
            if (f_draws){
                int bank = (k.building < state.draw_plot.size()) ? state.draw_plot[k.building] : -1;
                if (bank >= 0 && own[GOOD_WATER] < ECONOMY_CAMP_WATER_HOLD
                    && !UnderSnow(*state.world,state.world->grid->fine.pos[bank],tick)
                    && RouteTo(k,z,walkers,bank,state.world->grid->fine.pos[bank])){
                    k.prop = -1;
                    k.store = 0;
                    k.state = WORKER_TO_WORK;
                }
                break;
            }
            //The camp's picker (foraging_plan.md): out to the nearest ripe plant of his settlement, while its supplies
            //hold less fruit than ECONOMY_CAMP_FRUIT_HOLD.
            if (f_picks){
                if (own[GOOD_FRUIT] < ECONOMY_CAMP_FRUIT_HOLD){
                    GoPick(k,z,walkers,k.pos,0.0f);
                }
                break;
            }
            //A stonecutter (stone_plan.md): a boulder, rubble or a rock within reach of his hut - nothing waits at it.
            if (k.job == WORKER_JOB_STONECUTTER){
                FindRock(k,z,walkers);
                break;
            }
            //A miner (stone_plan.md step 5): to his mine to dig a load, while its deposit has some and a store has room.
            if (k.job == WORKER_JOB_MINER){
                GoDig(k,z,walkers);
                break;
            }
            //What waits at his workplace first - the pile, the harvest, the water - when a store takes it;
            //then, for a woodcutter, a tree, while a store or his pile has room for it - and only with a pile.
            if (FetchFromWorkplace(k,z,walkers)){
                break;
            }
            if (k.job == WORKER_JOB_WOODCUTTER && pile >= 0 && wood_room(log_wood)){
                FindTree(k,z,walkers);
            }
            break;
        case WORKER_TO_WORK:
            Walk(k,dt);
            if (Arrived(k)){
                if (k.prop < 0){
                    k.state = WORKER_WORKING;       //at his workplace, to pick up a load - or at the water's edge, to draw one
                    EconomyStartTimer(k,f_draws ? SecondsToTicks(ECONOMY_CAMP_WATER_SECONDS * ECONOMY_CAMP_WATER_LOAD / CrowdPace(k))
                                                : SecondsToTicks(ECONOMY_PICK_SECONDS));
                    //...or at the rock of his mine, to dig a load out of it at his pace.
                    if (k.job == WORKER_JOB_MINER){
                        int left = EconomyDepositLeft(state,MineDepositAt(k.building));
                        EconomyStartTimer(k,SecondsToTicks(ECONOMY_MINE_SECONDS * (float)std::max(1,std::min(ECONOMY_LOAD,left)) / pace));
                    }
                    break;
                }
                //The camp's picker at a plant: what it still has, as much as his load has room for, at his pace - picked
                //bare while he walked, on to the next.
                if (f_picks){
                    int n = std::min(FruitOn(k.prop),ECONOMY_LOAD - std::max(0,k.carry));
                    if (n > 0){
                        k.state = WORKER_WORKING;
                        EconomyStartTimer(k,SecondsToTicks(FORAGE_PICK_SECONDS * (float)n / pace));
                    }else{
                        pick_on();
                    }
                    break;
                }
                int ps = state.prop_state[k.prop];
                if (ps == PROP_STATE_STUMP){
                    //Someone was quicker: home - or, with a first log on his shoulder, off with that.
                    k.prop = -1;
                    if (k.carry > 0){
                        deliver();
                    }else{
                        GoHome(k,z,walkers);
                    }
                }else{
                    k.state = WORKER_WORKING;
                    //Felling at his pace: a strong woodcutter is quicker at it - and breaking a rock, by its stone.
                    int kind = state.world->forest->props[k.prop].kind;
                    float work = (k.job == WORKER_JOB_STONECUTTER) ? ECONOMY_BREAK_SECONDS * (float)ForestRockStone(kind)
                                                                   : ECONOMY_FELL_SECONDS;
                    EconomyStartTimer(k,(ps == PROP_STATE_STANDING) ? SecondsToTicks(work / pace) : SecondsToTicks(ECONOMY_PICK_SECONDS));
                }
            }
            break;
        case WORKER_WORKING:
            if (--k.timer > 0){
                break;
            }
            if (k.prop >= 0 && f_picks){
                //Picked: what is still on it, as much as his load has room for (PickFrom) - and on (pick_on).
                int got = PickFrom(k.prop,ECONOMY_LOAD - std::max(0,k.carry));
                if (got > 0){
                    k.carry = std::max(0,k.carry) + got;
                    k.carry_good = GOOD_FRUIT;
                }
                pick_on();
                break;
            }
            if (k.prop < 0 && f_draws){
                //Drawn: a load, home to the camp's supplies at its door, which take it whole (UNLOADING).
                int door = (k.building < home_plot.size()) ? home_plot[k.building] : -1;
                k.carry_good = GOOD_WATER;
                k.carry = ECONOMY_CAMP_WATER_LOAD;
                if (door >= 0 && RouteTo(k,z,walkers,door,state.world->grid->fine.pos[door])){
                    k.store = 0;
                    k.state = WORKER_TO_STORE;
                }else{
                    deliver();      //cut off from the camp: a store that takes it, or he puts it down
                }
                break;
            }
            if (k.prop < 0 && k.job == WORKER_JOB_MINER){
                //Dug (THE MINE): a load out of the deposit - what is left of a seam at most - and off to a store with it.
                int d = MineDepositAt(k.building);
                int n = std::min(ECONOMY_LOAD,EconomyDepositLeft(state,d));
                if (n <= 0){
                    GoHome(k,z,walkers);
                    break;
                }
                state.dug[d] += n;
                state.version++;
                k.carry = n;
                k.carry_good = state.world->deposits->deposits[d].good;
                deliver();      //the last load too: the mine keeps its place until he is home from it (GoDig, DugOut)
                break;
            }
            if (k.prop < 0 && k.job == WORKER_JOB_FARMER){
                //At a field of his land: its harvest, home first (land_plan.md).
                if (!PickHarvest(k,z,walkers)){
                    GoHome(k,z,walkers);
                }
                break;
            }
            if (k.prop < 0){
                //Picking up at his workplace: a load of what lies there, if it still does - of what it makes.
                GoodsMask made = WorkplaceMakes(k.job);
                int good = -1;
                for (int gd = 0; gd < GOOD_COUNT; gd++){
                    if ((made & GOOD_BIT(gd)) && own[gd] > 0 && (good < 0 || own[gd] > own[good])){
                        good = gd;
                    }
                }
                //A made good (STOCK KEPT) no more than the stores still lack of its level.
                int short_by = (good >= 0 && (GOODS_MADE & GOOD_BIT(good)))
                               ? KeptShort(z,good,EconomySettlementOf(state,k.building)) : ECONOMY_LOAD;
                if (good < 0 || short_by <= 0){
                    GoHome(k,z,walkers);
                    break;
                }
                k.carry_good = good;
                k.carry = std::min(std::min(k.job == WORKER_JOB_WOODCUTTER ? log_wood : ECONOMY_LOAD,own[good]),short_by);
                own[good] -= k.carry;
                if (!FindStore(k,z,walkers)){
                    own[good] += k.carry;   //the store went while he walked: it stays where it was
                    k.carry = 0;
                    k.carry_good = -1;
                    GoHome(k,z,walkers);
                }
            }else if (k.job == WORKER_JOB_STONECUTTER && state.prop_state[k.prop] == PROP_STATE_STANDING){
                BreakRock(k,z,walkers);     //its stone left as rubble, and a load of it lifted (stone_plan.md)
            }else if (state.prop_state[k.prop] == PROP_STATE_STANDING){
                //Felled: it lies as a log, which he now picks up - and a wall or site it stood in the way of, all it
                //takes there, goes up (only a woodcutter fells, woodcutting_plan.md).
                state.prop_state[k.prop] = PROP_STATE_LOG;
                state.felled_version++;
                {
                    const PropInstance& tree = state.world->forest->props[k.prop];
                    Event(EVENT_TREE_FELLED,k.id,0,tree.plot,vec2(tree.pos.x,tree.pos.z),k.prop);
                }
                Felled(z,k.prop);
                EconomyStartTimer(k,SecondsToTicks(ECONOMY_PICK_SECONDS));
            }else if (state.prop_state[k.prop] == PROP_STATE_LOG){
                /*
                    NOWHERE FOR IT NOW: the room he set out for was taken while he felled, and his pile is full or he
                    cannot get to it. The log is left lying, and LOGS FIRST brings him back for it once there is room -
                    as a collector's carrier leaves a load that no store takes where it was. Lifted, it went down as a
                    crate where he stood: a woodcutter whose hut another had walled in had strewn a hundred through the
                    forest (a bug report, 2026-10-24). What he already carries goes on as ever.
                */
                int lifted = ((k.carry_good == GOOD_WOOD) ? k.carry : 0) + log_wood;
                if (!wood_room(lifted) || !wood_goes(lifted)){
                    k.prop = -1;
                    if (k.carry > 0){
                        deliver();
                    }else{
                        GoHome(k,z,walkers);
                    }
                    break;
                }
                state.prop_state[k.prop] = PROP_STATE_STUMP;
                state.felled_version++;
                k.prop = -1;
                k.carry = lifted;
                k.carry_good = GOOD_WOOD;
                /*
                    TWO LOGS A ROUND (the user's, 2026-10-08): with another lying within ECONOMY_LOG_PAIR_REACH of
                    him, and somewhere to take both, he picks that one up too before he carries them off - a
                    log is half the walk of a tree's wood, and nothing to fell.
                */
                if (k.carry < ECONOMY_WOODCUTTER_LOGS * log_wood && wood_room(k.carry + log_wood)){
                    std::vector<std::pair<std::pair<int,float>,int>> logs;
                    WoodNear(k,z,walkers,k.pos,ECONOMY_LOG_PAIR_REACH,true,logs);
                    if (!logs.empty() && wood_goes(k.carry + log_wood) && GoForWood(k,z,walkers,logs,2)){
                        break;
                    }
                }
                deliver();      //never left standing in the wood with it
            }else if (k.carry > 0){
                k.prop = -1;
                deliver();
            }else{
                GoHome(k,z,walkers);
            }
            break;
        case WORKER_TO_STORE:
            Walk(k,dt);
            if (Arrived(k)){
                k.state = WORKER_UNLOADING;
                EconomyStartTimer(k,SecondsToTicks(ECONOMY_PICK_SECONDS));
            }
            break;
        case WORKER_UNLOADING:{
            if (--k.timer > 0){
                break;
            }
            uint32_t s = k.store;
            if (s == 0){
                //At his woodpile: onto it, as far as it has room - the camp's water carrier's and picker's into its
                //supplies, whole.
                int put = (f_draws || f_picks) ? k.carry : std::min(k.carry,std::max(0,pile_room - own[k.carry_good]));
                own[k.carry_good] += put;
                k.carry -= put;
            }else if (s < state.accepts.size() && (state.accepts[s] & GOOD_BIT(k.carry_good))){
                //The store may have changed while he walked: what it still takes and has room for.
                int put = std::min(k.carry,std::max(0,state.room[s] - CarryTotal(state.stock[s])));
                state.stock[s][k.carry_good] += put;
                k.carry -= put;
            }
            if (k.carry > 0){
                deliver();
            }else{
                k.carry_good = -1;
                GoHome(k,z,walkers);
            }
            break;
        }
        case WORKER_TO_HOME:
            Walk(k,dt);
            if (Arrived(k)){
                /*
                    His family was given a house while he walked back to the old home (HouseFamilies moves
                    only those standing at home): on to the new one. Left here he was "at home, indoors"
                    in a house he was nowhere near - out of sight, and never looking for work or water.
                */
                if (!AtHome(k)){
                    GoHome(k,z,walkers);
                    break;
                }
                k.state = WORKER_AT_HOME;
                //In at his door - but never into a tent: a camp's people stay where they can be seen.
                k.f_indoors = k.house != 0 && k.house < z.buildings.size() && z.buildings[k.house].kind == ZONE_KIND_HOUSE;
            }
            break;
        //--- Playing (LIFELINES) ---
        case WORKER_TO_PLAY:
            Walk(k,dt);
            if (Arrived(k)){
                k.state = WORKER_PLAYING;
                EconomyStartTimer(k,SecondsToTicks(LIFE_PLAY_SECONDS));
            }
            break;
        case WORKER_PLAYING:
            if (--k.timer > 0){
                break;
            }
            GoHome(k,z,walkers);
            break;
        //--- Posted (defence_plan.md) ---
        case WORKER_TO_POST:
            //A guard whose tower went, or whose place there did: home (AssignJobs took the job).
            if (EconomyPostPlot(state,k) < 0){
                GoHome(k,z,walkers);
                break;
            }
            Walk(k,dt);
            if (Arrived(k)){
                k.state = WORKER_ON_POST;
                EconomyStartTimer(k,0); //FORAGING by hand: begins on the post (below)
            }
            break;
        case WORKER_ON_POST:
            if (EconomyPostPlot(state,k) < 0){
                GoHome(k,z,walkers);
                break;
            }
            /*
                FORAGING BY HAND (Economy.h): on his post, a unit at a time into his pack - a fruit off the plant there
                at his pace, or a unit of water at the camp's water carrier's - each when its timer runs out, until the
                plant is bare, his pack full or the water frozen. Then he stands, as ever. A pressing need still takes
                him off (below); back on his post he goes on with it.
            */
            if (k.forage != FORAGE_NONE && EconomyAtPost(state,k)){
                if (k.timer > 0 && --k.timer == 0){
                    ForageUnit(k);
                }
                if (k.timer <= 0){
                    if (ForageWant(k) > 0){
                        EconomyStartTimer(k,ForageUnitTicks(k));
                    }else{
                        k.forage = FORAGE_NONE;
                    }
                    state.version++;
                }
            }
            if (!f_look){
                break;
            }
            /*
                On his post he stands, and looks only to his needs - his own house one supply among the rest
                (SeekSupply); a guard on his tower lives on its stock first (Needs). Cut off from it - a wall, the
                way gone - he sees to a pressing need wherever he can reach one and tries the way again every few
                looks, as someone cut off from home does.
            */
            if (EconomyAtPost(state,k)){
                //HELD BY ZOMBIES (zombies_plan.md step 3b): with one in range he stays through a pressing need, on his
                //pack and his tower's stock, and goes for it once none is left.
                if (!ZombiesInRange(k,z)){
                    SeekSupply(k,z,walkers);
                }
            }else if (!SeekSupply(k,z,walkers) && ((tick + k.id) / ECONOMY_LOOK_TICKS) % 4 == 0){
                GoToPost(k,z,walkers);
            }
            break;
        //--- Training (range_plan.md) ---
        case WORKER_TO_RANGE:
            Walk(k,dt);
            if (Arrived(k)){
                k.state = WORKER_TRAINING;
                EconomyStartTimer(k,SecondsToTicks(ECONOMY_TRAIN_SECONDS));
            }
            break;
        case WORKER_TRAINING:{
            //A stint on the line, in any season and under the snow: archery grows at his precision's pace, up to
            //what his precision allows. Given another job meanwhile, he goes home.
            if (k.job != WORKER_JOB_ARCHER){
                GoHome(k,z,walkers);
                break;
            }
            //Shooting at a zombie (ARROWS): his stint waits, and the straw with it.
            if (k.draw > 0){
                break;
            }
            int gain = (int)std::lround(ARCHERY_PER_TICK * pace);
            k.archery = std::min(EconomyArcheryCap(k),k.archery + gain);
            if (--k.timer <= 0){
                GoHome(k,z,walkers);
            }
            break;
        }
        case WORKER_HOLDING:
            /*
                No longer a state anyone stays in (crates_plan.md, step 5): one cut off with a load by a re-plan
                (Replan), or holding one in a save from before. At his next look a store or his pile, else it is put
                down (deliver) - a farmer's harvest the pantry had no room for too, which the house fetches from the
                crate when it runs short (FetchHome) rather than him standing there until it did.
            */
            if (f_look){
                deliver();
            }
            break;
        //--- Moving a crate (crates_plan.md) ---
        case WORKER_TO_CRATE:
            Walk(k,dt);
            if (Arrived(k)){
                k.state = WORKER_LIFTING;
                EconomyStartTimer(k,SecondsToTicks(ECONOMY_PICK_SECONDS));
            }
            break;
        case WORKER_LIFTING:{
            if (--k.timer > 0){
                break;
            }
            /*
                A load of the good he came for - or, that gone, of the first that is there - that a store still takes
                whole, with the room, and off to it (FindStore). The crate may have been emptied while he walked, or a
                store filled: then he takes nothing and goes home.
            */
            uint32_t from = k.store;
            std::array<int,GOOD_COUNT>* src = SourceStock(from);
            int good = -1;
            for (int pass = 0; pass < 2 && src && good < 0; pass++){
                for (int g = 0; g < GOOD_COUNT && good < 0; g++){
                    if ((pass == 0) != (g == k.carry_good) || (*src)[g] <= 0){
                        continue;
                    }
                    int amount = std::min(ECONOMY_LOAD,(*src)[g]);
                    for (size_t s = 1; s < state.accepts.size(); s++){
                        if ((state.accepts[s] & GOOD_BIT(g)) && Ours(k,(uint32_t)s) && StoreRoomLeft((uint32_t)s,&k) >= amount){
                            good = g;
                            break;
                        }
                    }
                }
            }
            if (good >= 0){
                k.carry_good = good;
                k.carry = std::min(ECONOMY_LOAD,(*src)[good]);
                if (FindStore(k,z,walkers)){
                    (*src)[good] -= k.carry;
                    CrateTouched(from);
                    break;
                }
            }
            k.carry = 0;
            k.carry_good = -1;
            GoHome(k,z,walkers);
            break;
        }
        //--- Provisions (defence_plan.md, step 5): a builder's wheat and beans to his hut, and making them there ---
        case WORKER_TO_INPUT:
            Walk(k,dt);
            if (Arrived(k)){
                k.state = WORKER_TAKING_INPUT;
                EconomyStartTimer(k,SecondsToTicks(ECONOMY_PICK_SECONDS));
            }
            break;
        case WORKER_TAKING_INPUT:{
            if (--k.timer > 0){
                break;
            }
            //The load he set out for, or what is left of it there - taken by others, or eaten, while he walked.
            uint32_t from = k.store;
            std::array<int,GOOD_COUNT>* src = SourceStock(from);
            int good = k.carry_good;
            int take = (src && good >= 0 && good < GOOD_COUNT) ? std::min(k.stint,(*src)[good]) : 0;
            uint32_t h = k.building;
            int plot = (take > 0 && h < z.buildings.size()) ? NearestPlot(h,k.pos) : -1;
            if (plot < 0 || !RouteTo(k,z,walkers,plot,state.world->grid->fine.pos[plot])){
                k.store = 0;
                k.stint = 0;
                k.carry_good = -1;
                GoHome(k,z,walkers);
                break;
            }
            (*src)[good] -= take;
            CrateTouched(from);
            k.carry = take;
            k.store = 0;
            k.stint = 0;
            k.state = WORKER_TO_WORKSHOP;
            state.version++;
            break;
        }
        case WORKER_TO_WORKSHOP:
            Walk(k,dt);
            if (Arrived(k)){
                if (EconomyHempWorkOf(k.job)){
                    AtHempWorkshop(k,z,walkers,pace);   //goods_plan.md: the same errand, at a ropewalk or a sail loft
                }else if (k.job == WORKER_JOB_CONDENSER){
                    AtCondenser(k,z,walkers);           //steam_plan.md step 3: steam brought to a condenser
                }else{
                    AtWorkshop(k,z,walkers,pace);
                }
            }
            break;
        case WORKER_MAKING:{
            if (--k.timer > 0){
                break;
            }
            //A rope or a canvas (EconomyHempWork: `carry_good` names it, nothing carried) into the workshop's stock - the
            //workshop gone, or too short, meanwhile, its hemp went with it - and on (AtHempWorkshop).
            const EconomyHempWork* work = EconomyHempWorkOf(k.job);
            if (work && k.carry_good == work->made){
                if (EconomyHempPace(z,k.building) > 0.0f){
                    state.stock[k.building][work->made]++;
                    state.version++;
                }
                k.carry_good = -1;
                k.stint++;
                k.state = WORKER_TO_WORKSHOP;
                AtHempWorkshop(k,z,walkers,pace);
                break;
            }
            //One made, into the hut's stock - the hut gone meanwhile, it went with it - and on (AtWorkshop).
            if (Workshop(z,stands,k.building)){
                state.stock[k.building][GOOD_PROVISIONS]++;
                state.version++;
            }
            k.stint++;
            k.state = WORKER_TO_WORKSHOP;   //not MAKING while AtWorkshop counts what is being made
            AtWorkshop(k,z,walkers,pace);
            break;
        }
        case WORKER_TO_FETCH:
            //His site gone while he walks - cancelled, erased: he sees it at his next look rather than at the wood.
            if (f_look && !SiteAlive(z,k.site)){
                k.site = 0;
                k.carry_good = -1;
                GoHome(k,z,walkers);
                break;
            }
            Walk(k,dt);
            if (Arrived(k)){
                k.state = WORKER_FETCHING;
                EconomyStartTimer(k,SecondsToTicks(ECONOMY_PICK_SECONDS));
            }
            break;
        case WORKER_FETCHING:{
            if (--k.timer > 0){
                break;
            }
            //A load of what is there and what the site still wants of the good he came for - either may have
            //changed on the way. A guard tower that stands whole wants what it lacks of full, and from a
            //caretaker only (step 7): a jobless carrier whose tower site was finished meanwhile takes nothing.
            uint32_t from = k.store;
            int good = k.carry_good;
            bool f_site = SiteAlive(z,k.site) && good >= 0 && good < GOOD_COUNT;
            std::array<int,GOOD_COUNT>* src = SourceStock(from);     //a building's, or a crate's - gone, if emptied meanwhile
            int have = (f_site && src) ? (*src)[good] : 0;
            int wanted = !f_site ? 0 : !TowerWhole(z,k.site) ? SiteLeft(z,k.site,&k)[good]
                       : (k.job == WORKER_JOB_BUILDER) ? TowerLeft(z,k.site,&k)[good] : 0;
            int take = std::min(ECONOMY_LOAD,std::min(have,wanted));
            int to = !f_site ? -1 : EconomyIsGroundSite(k.site) ? GroundSiteApproach(z,EconomyGroundSitePlot(k.site))
                                                                : NearestPlot(k.site,k.pos,true);
            if (take <= 0 || to < 0 || !RouteTo(k,z,walkers,to,state.world->grid->fine.pos[to])){
                k.site = 0;
                k.carry_good = -1;
                GoHome(k,z,walkers);
                break;
            }
            (*src)[good] -= take;
            CrateTouched(from);
            k.carry = take;
            k.store = 0;
            k.state = WORKER_TO_SITE;
            break;
        }
        case WORKER_TO_SITE:
            /*
                His site gone while he carries to it: he sees it at his next look and turns, his load to a store or put
                down where he is (deliver). He used to walk on to where it had been - and, with no store taking wood,
                stand there holding it (the user's, 2026-10-18: five people at a cancelled bridge).
            */
            if (f_look && !SiteAlive(z,k.site)){
                k.site = 0;
                deliver();
                break;
            }
            Walk(k,dt);
            if (Arrived(k)){
                uint32_t id = k.site;
                /*
                    A guard tower that stands whole (step 7): the load into its stock - its guards' pantry - up to
                    full, and home. What it no longer lacks (eaten from less, filled by other hands) goes to a
                    store. Whoever carries it: a load for a tower site finished while he walked goes in too.
                */
                if (TowerWhole(z,id) && k.carry_good >= 0 && k.carry_good < GOOD_COUNT){
                    int put = std::min(k.carry,EconomyTowerShort(state,*state.world,id,plots_of[id].front())[k.carry_good]);
                    if (put > 0){
                        state.stock[id][k.carry_good] += put;
                        k.carry -= put;
                        state.version++;
                    }
                    k.site = 0;
                    if (k.carry > 0){
                        deliver();
                    }else{
                        k.carry_good = -1;
                        GoHome(k,z,walkers);
                    }
                    break;
                }
                if (SiteAlive(z,id) && k.carry_good >= 0 && SiteNeed(z,id)[k.carry_good] > 0){
                    if (EconomyIsGroundSite(id)){
                        state.ground_site[EconomyGroundSitePlot(id)] += k.carry;     //its one good: wood, or a terrace's stone
                    }else{
                        state.site[id][k.carry_good] += k.carry;
                    }
                    k.carry = 0;
                    k.carry_good = -1;
                    k.state = WORKER_BUILDING;
                    EconomyStartTimer(k,SecondsToTicks(ECONOMY_BUILD_SECONDS / pace));
                }else{
                    //Pulled down, or finished by other hands, while he walked: the load goes to a store.
                    k.site = 0;
                    deliver();
                }
            }
            break;
        case WORKER_BUILDING:
            if (--k.timer > 0){
                break;
            }
            Build(z,k.site);
            k.site = 0;
            GoHome(k,z,walkers);
            break;
        //--- Road works (line_works_plan.md) ---
        case WORKER_TO_ROAD:{
            Walk(k,dt);
            if (!Arrived(k)){
                break;
            }
            int v = EconomyGroundSitePlot(k.site);
            if (!SiteAlive(z,k.site)){
                NextRoadWork(k,z,walkers);      //erased, or laid by other hands, while he walked
                break;
            }
            //To lay it: only with nothing of its way still standing - a neighbour painted since may have
            //brought a tree into it, which is a woodcutter's to fell. A wall has nothing to lay: its wood is
            //carried (FindSiteWork).
            std::vector<int> trees;
            RoadTrees(z,v,trees);
            if (!trees.empty() || z.ground[v] != ZONE_GROUND_ROAD){
                NextRoadWork(k,z,walkers);
                break;
            }
            k.state = WORKER_LAYING;
            EconomyStartTimer(k,SecondsToTicks(ECONOMY_LAY_SECONDS / pace));
            break;
        }
        case WORKER_CLEARING:
            //No longer entered: only a woodcutter fells (woodcutting_plan.md). One felling for a road in a save from
            //before stops here, and goes on with the next piece.
            k.prop = -1;
            NextRoadWork(k,z,walkers);
            break;
        case WORKER_LAYING:{
            if (--k.timer > 0){
                break;
            }
            int v = EconomyGroundSitePlot(k.site);
            std::vector<int> trees;
            if (SiteAlive(z,k.site)){
                RoadTrees(z,v,trees);
            }
            if (SiteAlive(z,k.site) && trees.empty() && z.ground[v] == ZONE_GROUND_ROAD
                && !std::count(ground_raises.begin(),ground_raises.end(),v)){
                ground_raises.push_back(v);     //laid: the app raises it this tick (ZONE_OP_GROUND_RAISE)
                state.version++;
            }
            k.stint++;
            NextRoadWork(k,z,walkers);
            break;
        }
        //--- A pressing need (needs_plan.md) ---
        case WORKER_TO_SUPPLY:
            Walk(k,dt);
            if (Arrived(k)){
                k.state = WORKER_SUPPLYING;
                EconomyStartTimer(k,1); //his first meal on the next tick: the meals are the time it takes (MEALS)
            }
            break;
        case WORKER_SUPPLYING:
            //A wait at a filling collector (below), or a meal under way: he stays.
            if (--k.timer > 0 || EconomyMealOn(k)){
                break;
            }
            //No meal with a zombie near (MEALS): he waits it out here, rather than go hungry and come straight back.
            if (ZombieNear(k,NEEDS_MEAL_SAFE)){
                EconomyStartTimer(k,ZOMBIE_LOOK_TICKS);
                break;
            }
            //The next unit, while one more fits - from what is there now (others may have been quicker), as he would
            //at home: at his own (posted), its pantry first. A crate is gone if it was emptied meanwhile, and goes if
            //he empties it.
            if (std::array<int,GOOD_COUNT>* src = SourceStock(k.store)){
                bool f_more = k.store == k.house && Consume(k,HomePantry(z,k.house));
                f_more = f_more || Consume(k,*src);
                CrateTouched(k.store);
                if (f_more){
                    break;
                }
            }
            /*
                At a collector that is filling, with room for more and no hunger to see to: he waits for its
                next unit, a few seconds off, and drinks his fill - rather than walk home with nothing and back
                again, which kept two of a camp dry for a day, or with one day's water for a walk of more than
                one. While he is still thirsty he counts on a unit there, so nobody else sets out for it.
            */
            if (k.water + NEEDS_WATER_WORTH <= NEEDS_WATER_FULL && !(EconomyPressing(k) & GOODS_FOOD) && k.store < z.buildings.size()
                && k.store < home_plot.size() && home_plot[k.store] >= 0
                && ((z.buildings[k.store].kind == ZONE_KIND_WATER
                     && !UnderSnow(*state.world,state.world->grid->fine.pos[home_plot[k.store]],tick))
                    || z.buildings[k.store].kind == ZONE_KIND_CONDENSER)
                && std::any_of(state.workers.begin(),state.workers.end(),[&](const EconomyWorker& o){ return o.building == k.store; })){
                EconomyStartTimer(k,SecondsToTicks(ECONOMY_PICK_SECONDS));
                break;
            }
            {
                uint32_t from = k.store;
                k.store = 0;
                //Posted (defence_plan.md): his pack topped up from what is here, and back to the post.
                if (EconomyPosted(k)){
                    if (std::array<int,GOOD_COUNT>* src = SourceStock(from)){
                        if (from == k.house){
                            FillPack(k,HomePantry(z,k.house));
                        }
                        FillPack(k,*src);
                        CrateTouched(from);
                    }
                    /*
                        TAKING A TENT: at a camp, one to spare packed - its plot out of the camp, on this same tick, as
                        a builder's storey goes up - and carried. None to spare now, someone quicker: the next camp,
                        from his post's way (above).
                    */
                    if (k.tent == ECONOMY_TENT_TO_TAKE){
                        int v = TentToPack(k,z,from);
                        if (v >= 0){
                            tents_packed.push_back(v);
                            k.tent = ECONOMY_TENT_CARRIED;
                            state.version++;
                        }
                    }
                    //GATHERING: a place fewer to go to - and with something still lacking, the next one.
                    if (k.gather > 0){
                        k.gather--;
                        if (k.gather > 0 && GatherSupplies(k,z,walkers)){
                            state.version++;
                            break;
                        }
                        k.gather = 0;
                        state.version++;
                    }
                    GoHome(k,z,walkers);
                    break;
                }
                if (k.carry > 0){
                    deliver();          //a load still on him: to a store, or put down
                    break;
                }
                /*
                    A load home with him from wherever he ate or drank, if a home may fetch from it (FetchFrom) - a
                    store, a camp's supplies, a collector, his own land's field (the user's, 2026-10-09: it was only
                    a store or the camp, and only for a house already short) - whenever his house has room under its
                    top-up, the scarcest kind first, as far as TakeHome allows: a field's fair share, firewood's.
                    Not a child: errands are for the grown (LIFELINES), the old among them.
                */
                if ((from < z.buildings.size() || EconomyIsCrateStore(from))
                    && (!EconomyStageJob(k.job) || k.job == WORKER_JOB_CARING || k.job == WORKER_JOB_OLD)
                    && FetchFrom(k,z,from)){
                    GoodsMask first = 0;
                    HomeShort(k,z,&first);
                    bool f_took = false;
                    for (GoodsMask goods : {first,(GoodsMask)GOOD_BIT(GOOD_WATER),(GoodsMask)GOODS_FOOD,(GoodsMask)GOOD_BIT(GOOD_WOOD)}){
                        f_took = f_took || (goods && TakeHome(k,z,walkers,from,goods));
                    }
                    if (f_took){
                        break;
                    }
                }
                GoHome(k,z,walkers);
            }
            break;
        //--- Fetching home (needs_plan.md, step 2) ---
        case WORKER_TO_GET:
            Walk(k,dt);
            if (Arrived(k)){
                k.state = WORKER_GETTING;
                EconomyStartTimer(k,SecondsToTicks(ECONOMY_PICK_SECONDS));
            }
            break;
        case WORKER_GETTING:{
            if (--k.timer > 0){
                break;
            }
            //Of the kind he came for, what is there now and what home still has room for - either may have
            //changed on the way. Of food, whichever there is most of: what he set out for may be gone.
            GoodsMask goods = (k.carry_good == GOOD_WATER || k.carry_good == GOOD_WOOD) ? (GoodsMask)GOOD_BIT(k.carry_good)
                                                                                       : (GoodsMask)GOODS_FOOD;
            if (!TakeHome(k,z,walkers,k.store,goods)){
                k.carry_good = -1;
                GoHome(k,z,walkers);
            }
            break;
        }
        case WORKER_TO_PANTRY:{
            Walk(k,dt);
            if (!Arrived(k)){
                break;
            }
            //His family was moved while he walked: on to the new home with it.
            if (!AtHome(k)){
                if (!RouteHome(k,z,walkers)){
                    deliver();
                }
                break;
            }
            /*
                Into the house's own stock as far as it keeps it and has room; a camp's pantry takes it all - but a
                harvest from the camp's own land (PickHarvest: `store` the camp) goes into its supplies, the colony's.
            */
            uint32_t h = k.house;
            int put = 0;
            if (h && h < z.buildings.size() && h < state.stock.size() && k.carry_good >= 0){
                if (z.buildings[h].kind == ZONE_KIND_CAMP){
                    put = k.carry;
                    (k.store == h ? state.stock[h] : HomePantry(z,h))[k.carry_good] += put;
                }else if (h < state.keeps.size() && (state.keeps[h] & GOOD_BIT(k.carry_good))){
                    put = std::min(k.carry,HouseRoom(z,h,k.carry_good));
                    state.stock[h][k.carry_good] += put;
                }
            }
            k.carry -= put;
            k.store = 0;
            state.version++;
            if (k.carry > 0){
                deliver();          //what home has no room for after all goes to a store
                break;
            }
            k.carry_good = -1;
            k.state = WORKER_AT_HOME;
            k.f_indoors = h != 0 && h < z.buildings.size() && z.buildings[h].kind == ZONE_KIND_HOUSE;
            break;
        }
        //--- Sowing (fields_plan.md) ---
        case WORKER_TO_SOW:
            Walk(k,dt);
            if (Arrived(k)){
                //The field of his land he came to sow is `store` (land_plan.md).
                int cells = (k.store < cells_of.size()) ? std::max(1,(int)cells_of[k.store].size()) : 1;
                k.state = WORKER_SOWING;
                EconomyStartTimer(k,SecondsToTicks(ECONOMY_SOW_SECONDS * (float)cells / pace));
            }
            break;
        case WORKER_SOWING:{
            if (--k.timer > 0){
                break;
            }
            //Sown with what is wanted NOW - the player may have chosen again while he walked - if it still
            //wants sowing: built, empty, and its crop decided and still in time for the frost.
            uint32_t id = k.store;
            EconomyField* f = FieldOf(id);
            int crop = -1;
            if (f && id < z.buildings.size() && id < home_plot.size() && home_plot[id] >= 0){
                int left = EconomyGrowingTicksLeft(*state.world,state.world->grid->fine.pos[home_plot[id]],tick);
                crop = EconomyFieldSowNow(*f,z.buildings[id],(id < field_pace.size()) ? field_pace[id] : 0.0f,left);
            }
            if (f && !f->f_sown && crop >= 0 && id < site_cells.size() && site_cells[id] == 0){
                f->crop = crop;
                f->f_sown = true;
                f->grown = 0;
                f->yield = EconomyCropYield(f->last,crop);
                f->frost = -1;
                state.version++;
            }
            GoHome(k,z,walkers);
            break;
        }
        default:
            k.state = WORKER_AT_HOME;
            break;
    }
}

EconomyField* Economy::FieldOf(uint32_t id){
    for (EconomyField& f : state.fields){
        if (f.building == id){
            return &f;
        }
    }
    return NULL;
}

/*
    A field of his land wants SOWING (fields_plan.md, land_plan.md): all of it built, nothing growing, its crop
    decided - the player's starting crop the first time, its next crop after - and in time to ripen before the
    frost (EconomyFieldSowNow: never in winter or under snow); its share farmed, not lying fallow; and nobody
    else already out to sow it. The nearest to him by the straight line, the lower id on a tie. Out to it
    (`store`), where its harvest is picked up too.
*/
bool Economy::GoSow(EconomyWorker& k, const ZoneState& z, Walkers& walkers, uint64_t calendar_tick){
    const ChasmWorld& w = *state.world;
    uint32_t best = 0;
    float best_d = 1e30f;
    for (const EconomyField& f : state.fields){
        uint32_t id = f.building;
        if (f.owner != k.building || id >= z.buildings.size() || id >= site_cells.size() || site_cells[id] > 0
            || home_plot[id] < 0 || id >= state.farmer_of.size() || !state.farmer_of[id] || f.f_sown
            || EconomyFieldSowCrop(f,z.buildings[id]) < 0){
            continue;
        }
        int left = EconomyGrowingTicksLeft(w,w.grid->fine.pos[home_plot[id]],calendar_tick);
        if (EconomyFieldSowNow(f,z.buildings[id],(id < field_pace.size()) ? field_pace[id] : 0.0f,left) < 0){
            continue;       //winter, the snow, or too late in the year for its crop: spring
        }
        bool f_taken = std::any_of(state.workers.begin(),state.workers.end(),[&](const EconomyWorker& o){
            return &o != &k && o.store == id && (o.state == WORKER_TO_SOW || o.state == WORKER_SOWING);
        });
        float d = (w.grid->fine.pos[home_plot[id]] - k.pos).length();
        if (!f_taken && d < best_d){
            best_d = d;
            best = id;
        }
    }
    if (!best){
        return false;
    }
    int plot = SourcePlot(best,k.pos);
    if (plot >= 0 && RouteTo(k,z,walkers,plot,w.grid->fine.pos[plot])){
        k.prop = -1;
        k.store = best;
        k.state = WORKER_TO_SOW;
        return true;
    }
    return false;
}

/*
    A trainee from home (range_plan.md): to HIS place on his range's line. The range's built cells, in index
    order, are shared out to its trainees by id, a cell each (a place a built cell); he walks to his cell's
    middle and on to its line (ZoneRangeLine), and a last half-step toward its target, so he stands facing it -
    his facing is his last leg's (EconomyWorkerFacing). Nothing names the cell: he stands in it, which is how
    the view finds his target. True if he set off.
*/
bool Economy::GoTrain(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    uint32_t id = k.building;
    if (id >= cells_of.size() || id >= z.buildings.size() || z.buildings[id].kind != ZONE_KIND_RANGE){
        return false;
    }
    std::vector<int> built;
    for (int c : cells_of[id]){
        if (z.field_built[c]){
            built.push_back(c);
        }
    }
    if (built.empty()){
        return false;
    }
    int rank = 0;
    for (const EconomyWorker& o : state.workers){
        rank += (o.job == WORKER_JOB_ARCHER && o.building == id && o.id < k.id) ? 1 : 0;
    }
    ZoneRangeCell r = ZoneRangeCellAt(*state.world,built[rank % built.size()]);
    if (r.middle < 0 || !RouteTo(k,z,walkers,r.middle,ZoneRangeLine(r))){
        return false;
    }
    k.route.push_back(k.route.back() + r.dir * 0.02f);
    k.speed.push_back(k.speed.empty() ? ECONOMY_WORKER_SPEED : k.speed.back());
    if (!k.route_layer.empty()){
        k.route_layer.push_back(k.route_layer.back());
    }
    k.prop = -1;
    k.store = 0;
    k.state = WORKER_TO_RANGE;
    return true;
}

/*
    From home, a harvest waiting on his land (land_plan.md): of its fields with food lying - fallow ones too,
    for what grew before - the one with the most, the lower id on a tie, that no other farmer is already out to
    fetch from; and only with somewhere for a load of it: the camp's supplies always, his house while it is
    short of food and not ahead of the colony (FairShare), or else a store with room. Out to it (`store`). True
    if he set off. HEMP (goods_plan.md) is fetched too, once no food lies there - food first, as ever - and
    never home: only the camp's supplies or a store take it.
*/
bool Economy::FetchHarvest(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    const ChasmWorld& w = *state.world;
    uint32_t land = k.building;
    bool f_camp = land < z.buildings.size() && z.buildings[land].kind == ZONE_KIND_CAMP;
    bool f_home = f_camp || ((HomeShort(k,z,NULL) & GOODS_FOOD) && !HomeAhead(k,z));
    uint32_t best = 0;
    int most = 0;
    for (const EconomyField& f : state.fields){
        uint32_t id = f.building;
        if (f.owner != land || id >= state.stock.size() || home_plot[id] < 0){
            continue;
        }
        int good = MostOf(state.stock[id],GOODS_FOOD);
        good = (good >= 0) ? good : MostOf(state.stock[id],GOODS_CROPS);
        int have = (good >= 0) ? state.stock[id][good] : 0;
        if (have <= most){
            continue;
        }
        bool f_taken = std::any_of(state.workers.begin(),state.workers.end(),[&](const EconomyWorker& o){
            return &o != &k && o.store == id && (o.state == WORKER_TO_WORK || o.state == WORKER_WORKING);
        });
        bool f_stored = f_camp || (f_home && GoodIsFood(good));
        int load = std::min(ECONOMY_LOAD,have);
        for (size_t s = 1; s < state.accepts.size() && !f_stored; s++){
            f_stored = (state.accepts[s] & GOOD_BIT(good)) && Ours(k,(uint32_t)s) && StoreRoomLeft((uint32_t)s,&k) >= load;
        }
        if (!f_taken && f_stored){
            best = id;
            most = have;
        }
    }
    if (!best){
        return false;
    }
    int plot = SourcePlot(best,k.pos);
    if (plot >= 0 && RouteTo(k,z,walkers,plot,w.grid->fine.pos[plot])){
        k.prop = -1;
        k.store = best;
        k.state = WORKER_TO_WORK;
        return true;
    }
    return false;
}

/*
    At the field he came to (`store`), its harvest picked up - HOME FIRST (land_plan.md): the camp's land a whole
    load into the camp's supplies, which take it all; a house's while it is short of food and not ahead of the
    colony, what brings it up to its fair share (TakeHome); otherwise a load to the nearest store that takes it.
    Hemp once no food lies there, as FetchHarvest picks it, and never home. False, taking nothing, with nothing
    there or nowhere for it.
*/
bool Economy::PickHarvest(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    uint32_t id = k.store;
    if (!id || id >= state.stock.size()){
        return false;
    }
    std::array<int,GOOD_COUNT>& own = state.stock[id];
    int good = MostOf(own,GOODS_FOOD);
    good = (good >= 0) ? good : MostOf(own,GOODS_CROPS);
    if (good < 0){
        return false;
    }
    if (k.house < z.buildings.size() && z.buildings[k.house].kind == ZONE_KIND_CAMP){
        int take = std::min(ECONOMY_LOAD,own[good]);
        if (!RouteHome(k,z,walkers)){
            return false;
        }
        own[good] -= take;
        k.carry_good = good;
        k.carry = take;
        k.store = k.house;      //for the camp's supplies, not its pantry (WORKER_TO_PANTRY)
        k.prop = -1;
        k.state = WORKER_TO_PANTRY;
        state.version++;
        return true;
    }
    if (GoodIsFood(good) && (HomeShort(k,z,NULL) & GOODS_FOOD) && !HomeAhead(k,z) && TakeHome(k,z,walkers,id,GOODS_FOOD)){
        return true;
    }
    k.carry_good = good;
    k.carry = std::min(ECONOMY_LOAD,own[good]);
    own[good] -= k.carry;
    if (FindStore(k,z,walkers)){
        state.version++;
        return true;
    }
    own[good] += k.carry;       //no store takes it after all: it stays where it was
    k.carry = 0;
    k.carry_good = -1;
    return false;
}

/*
    FAIR SHARES (the user's, 2026-10-08): a family kept its fields' food at home while the next house had none -
    its own land was a pantry nobody else could reach. So what a house may keep from its own land is the
    colony's average - all the food in homes, the camps' supplies with them, over everyone living in them - and
    ECONOMY_FAIR_DAYS a head more; a house with that much sends its harvest to storage, where the others fetch it.
    Fetching from a store or the camp is not held to it: that food is everyone's already. A head here is an adult's
    SHARE (Economy.h): the average is per adult's share, and a house's fair share is so many of them as its mouths
    make - a baby half of one.
*/
int64_t Economy::FairShare(const ZoneState& z) const{
    size_t n = std::min(z.buildings.size(),state.stock.size());
    std::vector<uint8_t> home(n,0);
    int64_t mouths = 0;
    for (const EconomyWorker& k : state.workers){
        if (k.house && k.house < n){
            home[k.house] = 1;
            mouths += EconomyShare(k.born,state.tick);
        }
    }
    int64_t food = 0;
    for (size_t id = 1; id < n; id++){
        food += home[id] ? HeldTicks(HomeHeld(z,(uint32_t)id),GOODS_FOOD) : 0;    //a camp's pantry with its supplies
    }
    return (mouths ? food * LIFE_SHARE_FULL / mouths : 0) + (int64_t)ECONOMY_FAIR_DAYS * CALENDAR_DAY_TICKS;
}

bool Economy::HomeAhead(const EconomyWorker& k, const ZoneState& z) const{
    uint32_t h = k.house;
    if (!h || h >= z.buildings.size() || h >= state.stock.size() || z.buildings[h].kind != ZONE_KIND_HOUSE){
        return false;
    }
    int64_t mouths = Mouths(state.workers,h,state.tick);
    return mouths > 0 && HeldTicks(state.stock[h],GOODS_FOOD) >= mouths * FairShare(z) / LIFE_SHARE_FULL;
}

void Economy::FollowZones(const ZoneState& z, Walkers& walkers){
    if (!state.world || z.world != state.world || z.version == state.zones_version){
        return;
    }
    const ChasmWorld& w = *state.world;
    size_t known = state.stock.size();
    Derive(z,walkers,true);
    //A guard tower placed in debug stands whole at once, never a site: it stands FULL, as one built would
    //(defence_plan.md). New ids only - a load never comes here, and a tower once placed keeps what it has.
    for (size_t id = known; id < z.buildings.size(); id++){
        if (z.buildings[id].kind == ZONE_KIND_TOWER && stands[id] && site_storeys[id] == 0 && !plots_of[id].empty()){
            state.stock[id][GOOD_WATER] = EconomyTowerStock(w,plots_of[id].front(),GOOD_WATER);
            state.stock[id][GOOD_PROVISIONS] = EconomyTowerStock(w,plots_of[id].front(),GOOD_PROVISIONS);
        }
        //A WELL (water_tools_plan.md) - only ever placed in debug - holds what the seed gives its plot.
        if (z.buildings[id].kind == ZONE_KIND_WELL && stands[id] && !plots_of[id].empty()){
            state.stock[id][GOOD_WATER] = EconomyWellWater(w,plots_of[id].front());
        }
    }
    KeepAtDoors();
    LoseCoveredCrates(z);
    //A new colony's first tick: the settlers' supplies into their camp, and the rubble along the chasm.
    if (f_new_colony){
        f_new_colony = false;
        uint32_t camp = 0;
        for (size_t id = 1; id < z.buildings.size(); id++){
            if (z.buildings[id].kind == ZONE_KIND_CAMP && stands[id]){
                std::array<int,GOOD_COUNT>& s = state.stock[id];
                s[GOOD_WOOD] += ECONOMY_START_WOOD;
                s[GOOD_WHEAT] += ECONOMY_START_WHEAT;
                s[GOOD_BEANS] += ECONOMY_START_BEANS;
                s[GOOD_WATER] += ECONOMY_START_WATER;
                camp = (uint32_t)id;
                break;
            }
        }
        PlaceRubble(z);
        //THE HISTORY begins with the landing: its levels with the supplies in, and the settlers' camp its first line -
        //on a map with settlers; a test map starts with nobody (test_maps_plan.md).
        BeginHistory(now);
        if (!state.workers.empty()){
            Event(EVENT_SETTLED,0,camp,state.camp_plot,vec2(),(int)state.workers.size());
        }
    }
    HouseFamilies(z,walkers);
    AssignJobs(z,walkers);
    Replan(z,walkers);
    state.version++;
}

void Economy::Tick(const ZoneState& z, Walkers& walkers, uint64_t tick, float dt){
    if (!state.world || z.world != state.world){
        return;
    }
    const ChasmWorld& w = *state.world;
    now = tick;
    state.tick = tick;
    /*
        THE LEDGER (Economy.h, THE HISTORY): the day before this one closed, before anything of this tick happens - so
        a row's people are its births and deaths on from the row before, exactly. On the day changing rather than on
        dawn's tick, so the date set by hand (CHASM_CMD_CALENDAR) closes the day it left. A new colony's history
        begins on its first tick instead (FollowZones).
    */
    if (!f_new_colony && tick / CALENDAR_DAY_TICKS != (uint64_t)state.history.today.day){
        CloseDay(tick);
    }
    walkers.SetSeason(tick);    //the ice the posted may cross (Walkers, ICE), for every walk planned this tick
    raises.clear();
    ground_raises.clear();
    field_raises.clear();
    crop_defaults.clear();
    tents_packed.clear();
    ground_breaks.clear();
    gate_breaks.clear();
    if (f_resettle && z.version == state.zones_version){
        //A save from before land (Restore): its land settled and its farmers taken on, on its first tick. No
        //re-plan - nothing on the ground changed, so everyone walks on as saved.
        HouseFamilies(z,walkers);
        AssignJobs(z,walkers);
        state.version++;
    }
    f_resettle = false;
    bool f_zones_moved = z.version != state.zones_version;     //...as FollowZones asks: the zombies' ways too
    FollowZones(z,walkers);
    Crowding(z);        //APPEAL AND CROWDING: who works slower this tick for where he lives
    //An endless WELL (water_tools_plan.md) full again, before anyone comes to it this tick.
    for (size_t id = 1; id < state.endless.size() && id < z.buildings.size(); id++){
        if (state.endless[id] && z.buildings[id].kind == ZONE_KIND_WELL && state.stock[id][GOOD_WATER] < ECONOMY_WELL_ENDLESS){
            state.stock[id][GOOD_WATER] = ECONOMY_WELL_ENDLESS;
            state.version++;
        }
    }
    //An ORDER for more of a made good (STOCK KEPT) is done once its colony's stores hold its level.
    for (const EconomySettlement& st : state.settlements){
        if (st.colony >= state.ordered.size()){
            continue;
        }
        std::array<int,GOOD_COUNT>& ordered = state.ordered[st.colony];
        if (std::any_of(ordered.begin(),ordered.end(),[](int x){ return x > 0; })){
            std::array<int,GOOD_COUNT> stored = EconomyStoredTotals(state,z,st.id);
            for (int g = 0; g < GOOD_COUNT; g++){
                if (ordered[g] > 0 && stored[g] >= ordered[g]){
                    ordered[g] = 0;
                    state.version++;
                }
            }
        }
    }
    //LIFELINES: the stages moving on and the births, the places shared out again at once when they free
    //someone or take a mother out of hers - not at the zones' next change.
    bool f_house = false;
    bool f_grew = Lifelines(tick,f_house);
    //Someone of age to pair: the free houses offered again (COUPLES) - and the places after, a couple moving.
    if (f_house){
        HouseFamilies(z,walkers);
        f_grew = true;
    }
    //...and an order given or ended since the last tick (Order): the posted out of the staffing, the released
    //back in - on this tick, after the zones' own change if there was one.
    if (f_grew || f_restaff){
        AssignJobs(z,walkers);
        state.version++;
    }
    f_restaff = false;
    //Who works where, by workplace id: a collector fills only while somebody works it (a field: field_pace) -
    //not while he is away at a post (defence_plan.md) - and for each who does (WORKERS BY PLOTS, building_size_plan.md).
    std::vector<float> working(z.buildings.size(),0.0f);
    for (const EconomyWorker& k : state.workers){
        if (k.building && k.building < working.size() && k.post < 0){
            working[k.building] += CrowdPace(k);
        }
    }
    CalendarDate date = CalendarDateOf(tick);
    /*
        A CROP LEFT UNCHOSEN (issues_plan.md, the user's 2026-10-09): a field with every cell built, never sown,
        with no starting crop, waits ECONOMY_CROP_GRACE for the player and then starts with AUTO - a cell of it
        named in crop_defaults, which the app sets on this same tick, as it raises what the builders finished.
        Counted in ticks, so it stops with the pause and a replay does it again on the same tick.
    */
    for (EconomyField& f : state.fields){
        uint32_t id = f.building;
        bool f_waiting = id < z.buildings.size() && z.buildings[id].crop == ZONE_CROP_NONE && !f.f_sown && f.last < 0
                         && id < cells_of.size() && !cells_of[id].empty() && site_cells[id] == 0;
        if (!f_waiting){
            f.wait = 0;
            continue;
        }
        //Held at the grace, so a default the zones refused is named again on the next tick.
        f.wait = std::min(f.wait + 1,(int)ECONOMY_CROP_GRACE);
        if (f.wait >= ECONOMY_CROP_GRACE){
            crop_defaults.push_back(cells_of[id][0]);
        }
    }
    /*
        The fields grow once SOWN (fields_plan.md), in any weather but winter's and the snow's, while
        their share of the land is farmed, at that farmer's pace (land_plan.md) - a fallow field waits.
        At harvest the field yields its built area x its crop's own yield x the yield it was sown at, and
        is empty again, that crop its last. THE FROST (Economy.h): winter takes whatever still grows -
        the field empty again with its soil as it was, the crop before still its last, the loss kept for
        its card until it is sown again.
    */
    for (EconomyField& f : state.fields){
        int crop = f.crop;
        if (!f.f_sown || crop < 0 || crop >= ZONE_CROP_COUNT){
            continue;
        }
        if (date.season == SEASON_WINTER){
            f.frost = crop;
            f.f_sown = false;
            f.crop = -1;
            f.grown = 0;
            state.version++;
            Event(EVENT_FROST_TOOK,0,f.building,(f.building < home_plot.size()) ? home_plot[f.building] : -1,vec2(),crop);
            continue;
        }
        int home = home_plot[f.building];
        float farmed = (f.building < field_pace.size()) ? field_pace[f.building] : 0.0f;
        if (home < 0 || farmed <= 0.0f || date.season == SEASON_WINTER || UnderSnow(w,w.grid->fine.pos[home],tick)){
            continue;
        }
        f.grown += (int)std::lround(farmed * 100.0f);
        if (f.grown >= crop_days[crop] * CALENDAR_DAY_TICKS * 100){
            float area = ZoneBuildingFigures(w,z,f.building).floor_area;
            int reaped = (int)std::floor(area * crop_yield[crop] * (float)f.yield / 100.0f);
            state.stock[f.building][GoodOfCrop(crop)] += reaped;
            uint32_t farmer = (f.building < state.farmer_of.size()) ? state.farmer_of[f.building] : 0;
            vec2 at;
            if (f.building < cells_of.size() && !cells_of[f.building].empty()){
                for (int q = 0; q < 4; q++){
                    at += w.grid->FineQuadCentre(cells_of[f.building][0] * 4 + q) * 0.25f;
                }
            }
            Event(EVENT_HARVEST,farmer,f.building,-1,at,reaped,GOOD_BIT(GoodOfCrop(crop)));
            f.grown = 0;
            f.f_sown = false;
            f.last = crop;
            f.crop = -1;
            state.version++;
        }
    }
    /*
        A collector fills, a unit at a time, up to what it holds - not frozen under the snow: a unit every
        ECONOMY_WATER_SECONDS for each worker at it, so a second carrier brings water rather than waits for it. From the
        tick alone, so nothing is kept between ticks.
    */
    int water_ticks = SecondsToTicks(ECONOMY_WATER_SECONDS);
    for (size_t id = 1; id < z.buildings.size(); id++){
        if (z.buildings[id].kind != ZONE_KIND_WATER || z.buildings[id].size <= 0 || home_plot[id] < 0 || working[id] <= 0.0f){
            continue;
        }
        int every = std::max(1,(int)std::lround((float)water_ticks / working[id]));
        if (tick % (uint64_t)every != 0){
            continue;
        }
        std::array<int,GOOD_COUNT>& s = state.stock[id];
        int hold = (id < state.water_room.size()) ? state.water_room[id] : ECONOMY_LOAD;
        if (s[GOOD_WATER] < hold && !UnderSnow(w,w.grid->fine.pos[home_plot[id]],tick)){
            s[GOOD_WATER]++;
            state.version++;
        }
    }
    /*
        A STEAM COLLECTOR (steam_plan.md) fills the same way with steam, a unit every ECONOMY_STEAM_SECONDS for each
        worker at it, faster by EconomySteamYield the lower it stands - and snow does not stop it: the chasm keeps its
        edge warm.
    */
    int steam_ticks = SecondsToTicks(ECONOMY_STEAM_SECONDS);
    for (size_t id = 1; id < z.buildings.size(); id++){
        if (z.buildings[id].kind != ZONE_KIND_STEAM || z.buildings[id].size <= 0 || home_plot[id] < 0 || working[id] <= 0.0f){
            continue;
        }
        float pace = working[id] * EconomySteamYield(w,home_plot[id]);
        int every = std::max(1,(int)std::lround((float)steam_ticks / pace));
        if (tick % (uint64_t)every != 0){
            continue;
        }
        std::array<int,GOOD_COUNT>& s = state.stock[id];
        int hold = (id < state.water_room.size()) ? state.water_room[id] : ECONOMY_LOAD;
        if (s[GOOD_STEAM] < hold){
            s[GOOD_STEAM]++;
            state.version++;
        }
    }
    /*
        A CONDENSER (steam_plan.md step 3) turns a unit of steam into a unit of water every ECONOMY_CONDENSER_SECONDS for
        each hand at it, up to what it holds, snow or not: the steam from a steam collector beside it first - the lowest id
        with any, so several condensers share one a unit at a time - and only then its own, what its hands brought.
    */
    int condense_ticks = SecondsToTicks(ECONOMY_CONDENSER_SECONDS);
    std::vector<uint32_t> feeds;
    for (size_t id = 1; id < z.buildings.size() && id < plots_of.size(); id++){
        if (z.buildings[id].kind != ZONE_KIND_CONDENSER || z.buildings[id].size <= 0 || home_plot[id] < 0 || working[id] <= 0.0f){
            continue;
        }
        int every = std::max(1,(int)std::lround((float)condense_ticks / working[id]));
        if (tick % (uint64_t)every != 0){
            continue;
        }
        std::array<int,GOOD_COUNT>& s = state.stock[id];
        int hold = (id < state.water_room.size()) ? state.water_room[id] : ECONOMY_LOAD;
        if (s[GOOD_WATER] >= hold){
            continue;
        }
        EconomyCondenserFeeds(w,z,plots_of[id],feeds);
        int* steam = NULL;
        for (uint32_t c : feeds){
            if (c < state.stock.size() && state.stock[c][GOOD_STEAM] > 0){
                steam = &state.stock[c][GOOD_STEAM];
                break;
            }
        }
        if (!steam && s[GOOD_STEAM] > 0){
            steam = &s[GOOD_STEAM];
        }
        if (steam){
            (*steam)--;
            s[GOOD_WATER]++;
            state.version++;
        }
    }
    //HEAT: every home's fire, lit or not, at dawn - and then the couples whose day it is (BIRTHS), which
    //wants that fire.
    if (tick % (uint64_t)CALENDAR_DAY_TICKS == 0){
        Heat(z,tick);
        ChancesOfChildren(z,tick);
        FruitAtDawn(tick);      //FORAGING: the frost's, and what grows back
    }
    //WITH THE GROWN: the children going along posted beside their grown-up, the babies carried - before anyone moves.
    WithTheGrown(z,walkers);
    for (EconomyWorker& k : state.workers){
        if (k.with && EconomyStageAt(k.born,tick) == LIFE_BABY){
            continue;       //carried: it goes nowhere by itself (below)
        }
        TickWorker(k,z,walkers,tick,dt);
    }
    //...and a baby carried is where its mother is now - out of sight, in her arms.
    for (EconomyWorker& k : state.workers){
        if (!k.with || EconomyStageAt(k.born,tick) != LIFE_BABY){
            continue;
        }
        for (const EconomyWorker& m : state.workers){
            if (m.id == k.with){
                k.pos = m.pos;
                StopWhereHeIs(k);
                k.state = WORKER_AT_HOME;
                k.f_indoors = true;
                break;
            }
        }
    }
    //ZOMBIES (zombies_plan.md): after everyone has moved, so each goes for where its target is now.
    TickZombies(z,walkers,tick,dt,f_zones_moved);
    //WALLS BREAK: those at what stops them hit it, once they have walked - before the spacing, which moves no route.
    ZombiesHit(z);
    //SPACING: every body stood apart from its neighbours, once all of them have moved.
    SpaceOut(z,dt);
    //ARROWS: those in the air come down, and the archers loose more - before the fight, so one shot down bites nobody.
    Shoot(z);
    //A FIGHT: the bites and blows from where they all stand now - its dead struck off by Needs, a cause of their own.
    bitten_dead.clear();
    bitten_struck.clear();
    Fights();
    Needs(z,walkers,tick);
    //THE FALLEN: whoever fell long enough ago is gone.
    TickFallen();
    //ICE: who would be cut off by the thaw, for their cards - a second's look is soon enough for a risk of days.
    if (tick % (uint64_t)ECONOMY_ICE_LOOK_TICKS == 0){
        IceHome(z,walkers);
    }
    //After everyone has moved, died or moved out: a camp left empty opens its pantry to all.
    OpenPantries(z);
    if (!state.workers.empty()){
        state.version++;    //they move every tick - the view redraws them
    }
}

//--- Needs (docs/needs_plan.md) ---------------------------------------------------------------------

int EconomyFoodWorth(int good){
    switch (good){
        case GOOD_WHEAT:    return CALENDAR_DAY_TICKS;
        case GOOD_GREENS:   return CALENDAR_DAY_TICKS / 2;
        case GOOD_BEANS:    return CALENDAR_DAY_TICKS * 2;
        case GOOD_FRUIT:    return CALENDAR_DAY_TICKS;      //as wheat (foraging_plan.md): tuning, like the rest
        //Made from a wheat and a beans, and worth the two together (defence_plan.md, PROVISIONS).
        case GOOD_PROVISIONS: return EconomyFoodWorth(GOOD_WHEAT) + EconomyFoodWorth(GOOD_BEANS);
        default:            return 0;
    }
}

const char* EconomyDeathCause(int cause){
    switch (cause){
        case NEEDS_DIED_HUNGER: return "hunger";
        case NEEDS_DIED_THIRST: return "thirst";
        case NEEDS_DIED_COLD:   return "cold";
        case NEEDS_DIED_ZOMBIE: return "a zombie";
        case NEEDS_DIED_OLD_AGE: return "old age";
        default:                return "?";
    }
}

std::string EconomyDeathName(const EconomyDeath& d){
    EconomyWorker k;
    k.id = d.id;
    k.family = d.family;
    k.f_female = d.f_female;
    k.surname = d.surname;
    return EconomyPersonName(k);
}

//A meal and a drink, standing at home - from his house's own stock, or his camp's supplies.
void Economy::EatAndDrink(EconomyWorker& k, const ZoneState& z){
    if (!k.house || k.house >= state.stock.size() || k.house >= z.buildings.size() || k.state != WORKER_AT_HOME
        || !AtHome(k)){
        return;
    }
    int kind = z.buildings[k.house].kind;
    if (kind != ZONE_KIND_HOUSE && kind != ZONE_KIND_CAMP){
        return;
    }
    Consume(k,HomePantry(z,k.house));
    if (kind == ZONE_KIND_CAMP){
        Consume(k,state.stock[k.house]);    //then the supplies (STORES, Economy.h)
    }
}

bool Economy::EatingAtHome(const EconomyWorker& k, const ZoneState& z) const{
    if (EconomyMealOn(k)){
        return true;
    }
    if (!k.house || k.house >= state.stock.size() || k.house >= z.buildings.size() || k.state != WORKER_AT_HOME
        || !AtHome(k)){
        return false;
    }
    int kind = z.buildings[k.house].kind;
    if (kind != ZONE_KIND_HOUSE && kind != ZONE_KIND_CAMP){
        return false;
    }
    //What EatAndDrink reaches - a house's stock, or a camp's pantry and supplies - and none begun with a zombie near.
    return NextMeal(k,HomeHeld(z,k.house)) >= 0 && !ZombieNear(k,NEEDS_MEAL_SAFE);
}

/*
    Eating and drinking from a stock, at home or away: only a whole unit that fits under full, so nothing is
    wasted. Water first: food does nothing for someone dry. Then food, the kind that keeps worst first - fruit and
    greens rot, wheat keeps, beans keep longest (gameplay_plan.md, "Goods and food") - so a pantry eats down its fruit
    and greens and saves its beans; FRUIT first of all (foraging_plan.md, the user's). PROVISIONS last of all
    (defence_plan.md): kept for soldiers, eaten only when nothing else there fits.
    A MEAL (Economy.h) at a time: the unit taken now and its worth given over its while (MealTick) - none while one is
    under way, and none begun with a zombie near.
*/
int Economy::NextMeal(const EconomyWorker& k, const std::array<int,GOOD_COUNT>& from) const{
    if (from[GOOD_WATER] > 0 && k.water + NEEDS_WATER_WORTH <= NEEDS_WATER_FULL){
        return GOOD_WATER;
    }
    if (EconomyDry(k)){
        return -1;
    }
    static const int keeps_worst_first[] = {GOOD_FRUIT,GOOD_GREENS,GOOD_WHEAT,GOOD_BEANS,GOOD_PROVISIONS};
    for (int good : keeps_worst_first){
        if (from[good] > 0 && k.health + EconomyFoodWorth(good) <= NEEDS_HEALTH_FULL){
            return good;
        }
    }
    return -1;
}

bool Economy::Consume(EconomyWorker& k, std::array<int,GOOD_COUNT>& from){
    if (EconomyMealOn(k)){
        return false;
    }
    int take = NextMeal(k,from);
    //Asked last, as it is the dearest: everyone at home asks every tick. None begun while he shoots (ARROWS).
    if (take < 0 || k.draw > 0 || ZombieNear(k,NEEDS_MEAL_SAFE)){
        return false;
    }
    from[take]--;
    k.meal_good = take;
    k.meal_worth = (take == GOOD_WATER) ? NEEDS_WATER_WORTH : EconomyFoodWorth(take);
    k.meal_ticks = std::max(1,SecondsToTicks((take == GOOD_WATER) ? ECONOMY_DRINK_SECONDS : ECONOMY_MEAL_SECONDS));
    state.version++;
    return true;
}

//A tick of his meal: its worth given out evenly over the ticks left, the rest on the last - and none while a zombie is near.
void Economy::MealTick(EconomyWorker& k){
    //It waits while a zombie is near him, and while he shoots at one (ARROWS).
    if (!EconomyMealOn(k) || k.draw > 0 || ZombieNear(k,NEEDS_MEAL_SAFE)){
        return;
    }
    int add = k.meal_worth / k.meal_ticks;
    k.meal_worth -= add;
    k.meal_ticks--;
    if (k.meal_good == GOOD_WATER){
        k.water = std::min(NEEDS_WATER_FULL,k.water + add);
    }else{
        k.health = std::min(NEEDS_HEALTH_FULL,k.health + add);
    }
    if (k.meal_ticks == 0){
        k.meal_good = -1;
        k.meal_worth = 0;
    }
}

bool Economy::SetStock(const ZoneState& z, uint32_t id, int good, int amount, bool f_pantry, bool f_endless, bool f_site){
    if (!id || id >= state.stock.size() || id >= state.pantry.size() || good < 0 || good >= GOOD_COUNT || amount < 0){
        return false;
    }
    if (f_site){
        if (id >= state.site.size() || !SiteAlive(z,id)){
            return false;
        }
        state.site[id][good] = amount;
        state.version++;
        Build(z,id);
        return true;
    }
    //A WELL holds water alone. It is endless only when told so, and any amount set without that makes it finite again.
    bool f_well = id < z.buildings.size() && z.buildings[id].kind == ZONE_KIND_WELL && id < state.endless.size();
    if ((f_well && (good != GOOD_WATER || f_pantry)) || (!f_well && f_endless)){
        return false;
    }
    if (f_well){
        state.endless[id] = f_endless ? 1 : 0;
    }
    (f_pantry ? state.pantry[id] : state.stock[id])[good] = amount;
    state.version++;
    return true;
}

bool Economy::SetNeeds(uint32_t person, int health, int water, int pack_water, int pack_provisions){
    for (EconomyWorker& k : state.workers){
        if (k.id != person){
            continue;
        }
        if (health >= 0){
            k.health = std::min(health,NEEDS_HEALTH_FULL);
        }
        if (water >= 0){
            k.water = std::min(water,NEEDS_WATER_FULL);
        }
        if (pack_water >= 0){
            k.pack[GOOD_WATER] = std::min(pack_water,NEEDS_SCOUT_WATER);
        }
        if (pack_provisions >= 0){
            k.pack[GOOD_PROVISIONS] = std::min(pack_provisions,NEEDS_SCOUT_FOOD / EconomyFoodWorth(GOOD_PROVISIONS));
        }
        state.version++;
        return true;
    }
    return false;
}

/*
    AN ORDER (POSTS, defence_plan.md). Sent: only someone old enough, to a plot nothing stands on - open ground,
    a road, a field, a camp's or a standing bridge's - that he can walk to from where he is. The new post is
    his on the spot; what he was doing he drops on the next tick (TakePost, from TickWorker), after the zones
    that tick brings, and the staffing follows then too (f_restaff). A LOAD on him he puts down at once, as a
    CRATE (DropLoad, crates_plan.md) - here and not on the next tick, so it is down before the meal a pressing
    need sends him to, and lies on the ground as the zones stand now. Already posted, he turns for the new one
    at once. Released: he goes home, and is staffed like anyone on the next tick - back
    to the work kept for him. A pressing need he is seeing to goes on either way.
*/
/*
    ICE (seasons_plan.md, the user's 2026-10-12): in winter the posted cross frozen water (Walkers::SetSeason), which
    is a risk - one sent over it and still there at the thaw has no way back until a bridge, or the next winter. The
    user chose it as a risk with a warning: here, those posted whose way home crosses the ice with no way round on
    dry ground or a bridge, and the days until the first ice on that way is gone - their card says so.
*/
void Economy::IceHome(const ZoneState& z, Walkers& walkers){
    state.ice_home.clear();
    const ChasmWorld& w = *state.world;
    if (CalendarSnowCover(now) <= 0.0f){
        return;     //no snow, no ice
    }
    std::vector<WalkPlace> places;
    std::vector<float> speeds;
    for (const EconomyWorker& k : state.workers){
        int home = EconomyPosted(k) ? HomePlot(k) : -1;
        GridPick here = (home >= 0) ? w.picker->Pick(k.pos) : GridPick();
        if (!here.f_hit || here.plot < 0 || here.plot == home){
            continue;
        }
        WalkPlace from;
        from.plot = here.plot;
        from.layer = (uint8_t)EconomyWorkerLayer(k);
        if (!walkers.PlanRoute(z,from,home,WALK_WHO_POSTED,places,speeds)){
            continue;
        }
        int days = -1;
        for (const WalkPlace& p : places){
            if (p.layer == WALK_GROUND && w.terrain->wet[p.plot] && !ZoneBridgeWalkable(z,p.plot)){
                int d = EconomyIceDays(w,p.plot,now);
                days = (days < 0) ? d : std::min(days,d);
            }
        }
        //No ice on his way, or a way round that anyone walks: nothing to lose at the thaw.
        if (days < 0 || walkers.PlanRoute(z,from,home,WALK_WHO_VILLAGER,places,speeds)){
            continue;
        }
        state.ice_home.push_back(std::make_pair(k.id,days));
    }
}

bool Economy::Order(uint32_t person, int plot, const ZoneState& z, Walkers& walkers, std::string* why, int layer,
                    bool f_spread, const Exploration* clouds){
    auto refuse = [&](const char* reason){
        if (why){
            *why = reason;
        }
        return false;
    };
    if (!state.world || z.world != state.world){
        return refuse("no colony");
    }
    EconomyWorker* k = NULL;
    for (EconomyWorker& o : state.workers){
        if (o.id == person){
            k = &o;
        }
    }
    if (!k){
        return refuse("nobody by that id");
    }
    const ChasmWorld& w = *state.world;
    //Where he was posted: those left there spread round it again (RePlace), as do those he joins.
    int was_plot = k->post;
    int was_layer = k->post_layer;
    if (plot < 0){
        if (k->post < 0){
            return refuse("not posted");
        }
        k->post = -1;
        k->post_layer = -1;
        RePlace(was_plot,was_layer,k,z,walkers);
        k->gather = 0;
        k->forage = FORAGE_NONE;
        //A tent not taken yet is not gone for; one carried he keeps, to pitch when he likes (settlements_plan.md).
        if (k->tent == ECONOMY_TENT_TO_TAKE){
            k->tent = ECONOMY_TENT_NONE;
        }
        if (k->state == WORKER_TO_POST || k->state == WORKER_ON_POST){
            GoHome(*k,z,walkers);
        }
        f_restaff = true;
        state.version++;
        return true;
    }
    if (!EconomyCanOrder(*k,now)){
        return refuse(EconomyWhyNotOrdered(*k,now));
    }
    if (plot >= (int)z.ground.size() || plot >= (int)w.grid->fine.pos.size()){
        return refuse("no such plot");
    }
    //Under the clouds he may be sent - it is how they are pushed back - but why not is never more than no way there.
    bool f_hidden = clouds && !clouds->Explored(w.grid->fine.pos[plot]);
    int kind = z.KindOf(plot);
    if (z.ground[plot] == ZONE_GROUND_WALL || ZoneGateShut(z,plot)
        || (kind != ZONE_KIND_NONE && kind != ZONE_KIND_CAMP && !ZoneBridgeWalkable(z,plot))){
        return refuse(f_hidden ? "no way there" : "something stands there");
    }
    //Over the ice he may go in winter (Walkers, ICE), but not stand on it: the thaw would leave him in the river.
    if (w.terrain->wet[plot] && !ZoneBridgeWalkable(z,plot)){
        return refuse(f_hidden ? "no way there" : "on the water");
    }
    walkers.SetSeason(now);     //an order between ticks: the ice as it is now
    //On the debug castle (ramparts_plan.md) he stands on top - its deck, which only the posted walk.
    if (!CanWalkPosted(z,walkers,*k,plot,layer)){
        return refuse("no way there");
    }
    //One of a GROUP (selection_plan.md, step 3): the plot clicked while it has room for him, else the nearest round it.
    int to = plot;
    int to_layer = layer;
    if (f_spread && SpreadPost(*k,plot,layer,z,walkers,&to,&to_layer)){
        plot = to;
        layer = to_layer;
    }
    bool f_new = k->post < 0;
    k->post = plot;
    k->post_layer = layer;
    k->forage = FORAGE_NONE;    //a new post is only that: Forage gives him something to do there after
    if (k->carry > 0){
        DropLoad(*k,z);
    }
    if (k->state == WORKER_TO_POST || k->state == WORKER_ON_POST){
        GoToPost(*k,z,walkers);
    }
    RePlace(plot,layer,k,z,walkers);
    if (was_plot != plot || was_layer != layer){
        RePlace(was_plot,was_layer,k,z,walkers);
    }
    f_restaff = f_restaff || f_new;
    state.version++;
    return true;
}

/*
    GATHERING (defence_plan.md, "Scouting, provisioned first"): someone ordered out told to fill a scout's pack first,
    ECONOMY_GATHER_STOPS places at most - or, turned off, to go straight on: on his way to a place for it, he turns
    for his post at once, unless a pressing need takes him there anyway. Set here; he goes on the tick that follows
    (TickWorker), wherever he is on his way or on his post; one setting out from home fills it there first (TakePost).
*/
bool Economy::Gather(uint32_t person, bool f_on, const ZoneState& z, Walkers& walkers, std::string* why){
    auto set = [&](EconomyWorker& k){
        //...but not on his way for a tent: that comes first, and gathering after (TickWorker).
        if (!f_on && k.gather > 0 && k.state == WORKER_TO_SUPPLY && k.tent != ECONOMY_TENT_TO_TAKE && !EconomyPressing(k)
            && state.world && z.world == state.world){
            k.store = 0;
            GoToPost(k,z,walkers);
        }
        k.gather = f_on ? ECONOMY_GATHER_STOPS : 0;
    };
    for (EconomyWorker& k : state.workers){
        if (k.id != person){
            continue;
        }
        if (!EconomyCanOrder(k,now)){
            if (why){
                *why = EconomyWhyNotOrdered(k,now);     //a child gathers as the grown-up he goes with does (below)
            }
            return false;
        }
        if (f_on && k.post < 0){
            if (why){
                *why = "not sent out";
            }
            return false;
        }
        set(k);
        //WITH THE GROWN: the children going along with him gather as he does.
        for (EconomyWorker& c : state.workers){
            if (c.with == person && c.post >= 0 && EconomyStageAt(c.born,now) == LIFE_CHILD){
                set(c);
            }
        }
        state.version++;
        return true;
    }
    if (why){
        *why = "nobody by that id";
    }
    return false;
}

/*
    FORAGING BY HAND (Economy.h, foraging_plan.md): the player's one-off for someone sent out - a right click on a ripe
    plant, or on water, and its menu. Refused before he goes: nothing ripe there, no room in his pack, the water frozen
    or out of his reach, another settlement's land. Then an ORDER to the plant's plot, or to the edge beside the water
    (BankPlot) - the order's own refusals hold - and what to do there, which begins once he stands on it (TickWorker).
    One of a GROUP picks the nearest ripe plant to the one clicked that none of them is sent to, within
    FORAGE_SPREAD_REACH; drawing, they share the edge out as a group's posts do.
*/
bool Economy::Forage(uint32_t person, int plot, int what, const ZoneState& z, Walkers& walkers, std::string* why,
                     bool f_spread, const Exploration* clouds){
    auto refuse = [&](const char* reason){
        if (why){
            *why = reason;
        }
        return false;
    };
    if (!state.world || z.world != state.world || !state.world->forest){
        return refuse("no colony");
    }
    EconomyWorker* k = NULL;
    for (EconomyWorker& o : state.workers){
        if (o.id == person){
            k = &o;
        }
    }
    if (!k){
        return refuse("nobody by that id");
    }
    if (!EconomyCanOrder(*k,now)){
        return refuse(EconomyWhyNotOrdered(*k,now));
    }
    const ChasmWorld& w = *state.world;
    if (plot < 0 || plot >= (int)z.ground.size() || plot >= (int)w.grid->fine.pos.size()){
        return refuse("no such plot");
    }
    //A PLAY command (`clouds`) forages nothing hidden - its menu is not even offered there, the view's own rule.
    auto hidden = [&](const vec2& p){
        return clouds && !clouds->Explored(p);
    };
    if (hidden(w.grid->fine.pos[plot])){
        return refuse("under the clouds");
    }
    int to = -1;
    bool f_spread_post = false;
    if (what == FORAGE_FRUIT){
        if (std::max(0,NEEDS_SCOUT_FOOD - EconomyPackFood(*k)) / EconomyFoodWorth(GOOD_FRUIT) <= 0){
            return refuse("no room in his pack for more food");
        }
        int plant = -1;
        if (!f_spread){
            plant = BestPlantOn(state,&plants,plot);
        }else{
            const std::vector<PropInstance>& props = w.forest->props;
            const vec2 origin = w.grid->fine.pos[plot];
            float best = FORAGE_SPREAD_REACH;
            for (int i : plants){
                const PropInstance& p = props[i];
                float d = (vec2(p.pos.x,p.pos.z) - origin).length();
                if (d > best || (plant >= 0 && d >= best) || FruitOn(i) <= 0 || hidden(vec2(p.pos.x,p.pos.z))){
                    continue;       //the lower index on a tie: plants are in index order
                }
                bool f_taken = false;
                for (const EconomyWorker& o : state.workers){
                    f_taken = f_taken || (&o != k && o.forage == FORAGE_FRUIT && o.post == p.plot);
                }
                if (!f_taken){
                    best = d;
                    plant = i;
                }
            }
        }
        if (plant < 0){
            return refuse(f_spread ? "no more ripe fruit near" : "no ripe fruit there");
        }
        to = w.forest->props[plant].plot;
    }else if (what == FORAGE_WATER){
        if (!w.terrain->wet[plot] || ZoneBridgeWalkable(z,plot)){
            return refuse("no water there");
        }
        if (UnderSnow(w,w.grid->fine.pos[plot],now)){
            return refuse("the water is frozen");
        }
        if (k->pack[GOOD_WATER] >= NEEDS_SCOUT_WATER){
            return refuse("no room in his pack for more water");
        }
        walkers.SetSeason(now);     //between ticks: the ice as it is now, as an order's
        to = BankPlot(*k,z,walkers,plot);
        if (to < 0){
            return refuse("no way to the water's edge");
        }
        f_spread_post = f_spread;
    }else{
        return refuse("nothing to forage");
    }
    //Only on his own settlement's land or land none holds (the user's), as a pressing need goes (SeekSupplyOf).
    if (!PlotOurs(*k,to)){
        return refuse("another settlement's land");
    }
    if (!Order(person,to,z,walkers,why,-1,f_spread_post,clouds)){
        return false;
    }
    k->forage = what;
    if (k->state == WORKER_TO_POST || k->state == WORKER_ON_POST){
        EconomyStartTimer(*k,0); //begun afresh once he stands there - never a pressing need's meal, which has its own timer
    }
    state.version++;
    return true;
}

/*
    TAKING A TENT (settlements_plan.md, "Founding: taking a tent"): someone ordered out told to take a tent first, from
    the nearest camp with one to spare - or, turned off, to go straight on: on his way to a camp for it, he turns for
    his post at once, unless a pressing need takes him there anyway. Set here; he goes on the tick that follows
    (TickWorker), as for gathering. One he carries already is his until he pitches it.
*/
bool Economy::TakeTent(uint32_t person, bool f_on, const ZoneState& z, Walkers& walkers, std::string* why){
    auto refuse = [&](const char* reason){
        if (why){
            *why = reason;
        }
        return false;
    };
    for (EconomyWorker& k : state.workers){
        if (k.id != person){
            continue;
        }
        if (k.tent == ECONOMY_TENT_CARRIED){
            return refuse("already carrying a tent");
        }
        if (f_on && !EconomyCanOrder(k,now)){
            return refuse(EconomyWhyNotOrdered(k,now));     //a child shares his grown-ups' tents (WITH THE GROWN)
        }
        if (f_on && k.post < 0){
            return refuse("not sent out");
        }
        if (!f_on && k.tent == ECONOMY_TENT_TO_TAKE && k.state == WORKER_TO_SUPPLY && !EconomyPressing(k)
            && state.world && z.world == state.world){
            k.store = 0;
            GoToPost(k,z,walkers);
        }
        k.tent = f_on ? ECONOMY_TENT_TO_TAKE : ECONOMY_TENT_NONE;
        state.version++;
        return true;
    }
    return refuse("nobody by that id");
}

/*
    A camp's beds to spare for `k`: its tents less those packed already this tick (out of the camp only once the tick
    is done), those someone else is on his way to pack, and those slept in - by its people not away carrying tents of
    their own, nor on their way for one here (their bed is the tent they take). So his own is one to spare, for him.
    For ONE OF ITS OWN PEOPLE, none slept in counts (the user's no_tent_to_take, 2026-10-12): a camp holding more than
    its tents - children squeezed in, a family crammed - has every tent slept in, so nobody living there could pack
    up and move on. Its people may take its tents while any are left and not spoken for; those staying are crammed.
*/
int Economy::CampSpare(const ZoneState& z, uint32_t id, const EconomyWorker* k) const{
    if (id == 0 || id >= z.buildings.size() || id >= tents_of.size() || id >= stands.size()
        || z.buildings[id].kind != ZONE_KIND_CAMP || z.buildings[id].size <= 0 || !stands[id]){
        return 0;
    }
    int spare = 0;
    for (int v : tents_of[id]){
        spare += (std::find(tents_packed.begin(),tents_packed.end(),v) != tents_packed.end()) ? 0 : 1;
    }
    bool f_ours = k && k->house == id;
    for (const EconomyWorker& o : state.workers){
        if (&o == k){
            continue;
        }
        bool f_taking = o.tent == ECONOMY_TENT_TO_TAKE && o.store == id
                        && (o.state == WORKER_TO_SUPPLY || o.state == WORKER_SUPPLYING);
        bool f_sleeps = !f_ours && o.house == id && o.tent != ECONOMY_TENT_CARRIED;
        spare -= (f_taking || f_sleeps) ? 1 : 0;
    }
    return spare;
}

int Economy::TentToPack(const EconomyWorker& k, const ZoneState& z, uint32_t id) const{
    if (CampSpare(z,id,&k) <= 0){
        return -1;
    }
    auto packed = [&](int v){
        return std::find(tents_packed.begin(),tents_packed.end(),v) != tents_packed.end();
    };
    /*
        The one nearest him, the lower plot on a tie (the user's, 2026-10-14) - he goes to it (GoForTent) and packs the
        one he stands at. It was the one at the camp's edge, the fewest of its tents beside it, to keep the camp in one
        piece; since a camp's tents need not touch (one camp to a colony), a tent pitched alone across the town had
        none beside it, and went from under its pitcher when someone packed one at the camp.
    */
    const std::vector<vec2>& pos = state.world->grid->fine.pos;
    int best = -1;
    float best_d = 0.0f;
    for (int v : tents_of[id]){
        if (packed(v)){
            continue;
        }
        float d = (pos[v] - k.pos).length();
        if (best < 0 || d < best_d || (d == best_d && v < best)){
            best = v;
            best_d = d;
        }
    }
    return best;
}

bool Economy::GoForTent(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    const ChasmWorld& w = *state.world;
    std::vector<std::pair<float,uint32_t>> found;
    size_t n = std::min(z.buildings.size(),tents_of.size());
    //Each camp by the tent he would pack there - the one nearest him (TentToPack) - and to that tent.
    for (size_t id = 1; id < n; id++){
        int plot = TentToPack(k,z,(uint32_t)id);
        if (plot >= 0){
            found.push_back(std::make_pair((w.grid->fine.pos[plot] - k.pos).length(),(uint32_t)id));
        }
    }
    std::sort(found.begin(),found.end());
    for (size_t t = 0; t < found.size() && t < 4; t++){
        uint32_t id = found[t].second;
        int plot = TentToPack(k,z,id);
        if (RouteTo(k,z,walkers,plot,SourcePoint(id,plot,k.pos))){
            k.store = id;
            k.prop = -1;
            k.state = WORKER_TO_SUPPLY;
            state.version++;
            return true;
        }
    }
    return false;
}

/*
    ONE CAMP TO A COLONY (settlements_plan.md, the user's 2026-10-12): the camp of the colony plot `plot` lies in - the
    oldest, should a save from before hold two - or 0, with none or outside every colony. A tent pitched or painted
    there is that camp's wherever it stands (THE CAMP, Zones.h: its tents need not touch), so a colony has one camp and
    one set of the camp's places, however its tents fall; the first tent outside every colony founds one.
*/
uint32_t EconomyColonyCamp(const EconomyState& e, const ZoneState& z, int plot){
    uint32_t s = (plot >= 0 && plot < (int)e.settlement.size()) ? e.settlement[plot] : 0;
    if (!s){
        return 0;
    }
    for (uint32_t id = 1; id < z.buildings.size(); id++){
        if (z.buildings[id].kind == ZONE_KIND_CAMP && z.buildings[id].size > 0 && EconomySettlementOf(e,id) == s){
            return id;
        }
    }
    return 0;
}

/*
    PITCHING (settlements_plan.md): where the tent he carries goes up - the plot he stands on, else the nearest a camp
    may stand on, ring by ring out to ECONOMY_PITCH_RINGS, on his own terrace step: not over a crate or a standing tree.
    In a colony with a camp it is that camp's (EconomyColonyCamp), wherever it stands; otherwise a camp of its own - a
    colony's first, or outside every colony a new one, which is what it is for. He stands there, so not under clouds.
*/
bool Economy::PitchSpot(uint32_t person, const ZoneState& z, int* plot, uint32_t* camp, std::string* why) const{
    auto refuse = [&](const std::string& reason){
        if (why){
            *why = reason;
        }
        return false;
    };
    if (!state.world || z.world != state.world){
        return refuse("no colony");
    }
    const EconomyWorker* k = NULL;
    for (const EconomyWorker& o : state.workers){
        k = (o.id == person) ? &o : k;
    }
    if (!k){
        return refuse("nobody by that id");
    }
    if (k->tent != ECONOMY_TENT_CARRIED){
        return refuse("no tent to pitch");
    }
    const ChasmWorld& w = *state.world;
    const Terrain& t = *w.terrain;
    GridPick here = w.picker->Pick(k->pos);
    if (!here.f_hit || here.plot < 0 || EconomyWorkerLayer(*k) != WALK_GROUND){
        return refuse("nowhere to pitch it");
    }
    const char* first_why = NULL;
    std::vector<int> ring(1,here.plot);
    std::vector<int> seen(1,here.plot);
    for (int r = 0; r <= ECONOMY_PITCH_RINGS && !ring.empty(); r++){
        std::sort(ring.begin(),ring.end(),[&](int a, int b){
            float da = (w.grid->fine.pos[a] - k->pos).length();
            float db = (w.grid->fine.pos[b] - k->pos).length();
            return (da != db) ? da < db : a < b;
        });
        std::vector<int> next;
        for (int v : ring){
            int nb[8];
            int n = ZonePlotNeighbours(w,v,nb,8);
            for (int i = 0; i < n; i++){
                if (std::find(seen.begin(),seen.end(),nb[i]) == seen.end() && t.SameGround(here.plot,nb[i]) && !t.wet[nb[i]]){
                    seen.push_back(nb[i]);
                    next.push_back(nb[i]);
                }
            }
            //Only on a plot with nothing built on it - the rules would take a click on a built one for a storey.
            const char* no = NULL;
            if (z.building[v]){
                first_why = first_why ? first_why : "something stands there";
                continue;
            }
            uint32_t joins = EconomyColonyCamp(state,z,v);
            bool f_ok = (!joins || ZoneCanExtend(w,z,v,ZONE_KIND_CAMP,joins,&no)) && ZoneCanBuild(w,z,v,ZONE_KIND_CAMP,joins,&no);
            if (f_ok && std::any_of(state.crates.begin(),state.crates.end(),[&](const EconomyCrate& c){ return c.plot == v; })){
                f_ok = false;
                no = "a crate lies there";
            }
            if (f_ok && EconomyTreesOn(w,state,v,-1) > 0){
                f_ok = false;
                no = "a tree stands there";
            }
            if (f_ok){
                *plot = v;
                *camp = joins;
                return true;
            }
            first_why = first_why ? first_why : no;
        }
        ring.swap(next);
    }
    return refuse(first_why ? std::string("no room for a tent here: ") + first_why : std::string("no room for a tent here"));
}

//His house place given up (settlements_plan.md, the user's): a household of his own from now on - as a new couple
//founds one, its number never one of the living's or the dead's (HouseFamilies) - unless he is one already.
void Economy::OwnHousehold(EconomyWorker& k){
    bool f_others = std::any_of(state.workers.begin(),state.workers.end(),[&](const EconomyWorker& o){
        return o.id != k.id && o.family == k.family;
    });
    if (!f_others){
        return;
    }
    uint32_t f = 0;
    for (const EconomyWorker& o : state.workers){
        f = std::max(f,o.family + 1);
    }
    for (const EconomyDeath& d : state.dead){
        f = std::max(f,d.family + 1);
    }
    k.family = f;
}

/*
    REJOINING (settlements_plan.md, the user's 2026-10-11): pitching or settling in the settlement where his partner
    lives, he joins the partner's household and home instead of founding one of his own - whatever its room, as a
    family grown by a birth is: they are one family again, housed as one, and may have children again.
*/
const EconomyWorker* Economy::PartnerLivingIn(const EconomyWorker& k, uint32_t settlement) const{
    if (!k.partner || !settlement){
        return NULL;
    }
    for (const EconomyWorker& p : state.workers){
        if (p.id == k.partner){
            bool f_home = p.house && p.tent == ECONOMY_TENT_NONE && EconomySettlementOf(state,p.house) == settlement;
            return f_home ? &p : NULL;
        }
    }
    return NULL;
}

/*
    WITH THE GROWN (Economy.h; settlements_plan.md, the user's 2026-10-12: "they'd have to come with. They'll need
    protection from an archer further along in the game"). A CHILD goes along when every grown-up of its household - a
    young adult counts, the old do not, since they are never sent out (OLD AGE) - is LEAVING it: carrying or taking a
    tent, or posted onto another settlement's ground, where Settle here is offered. With one of them staying the
    children stay too, as with a partner left behind. Beside its
    mother if she is one of them, else its father, else the eldest (the lower id on a tie). A BABY always goes with its
    mother (the user's): carried whenever she is posted, and from then until she is home again - `with` remembers it,
    since a baby in a camp often stands off its own tent and could not be told from one out with her.
*/
uint32_t Economy::GoesWith(const EconomyWorker& k) const{
    int stage = EconomyStageAt(k.born,now);
    if (stage == LIFE_BABY){
        for (const EconomyWorker& m : state.workers){
            if (m.id == k.mother){
                bool f_home = m.state == WORKER_AT_HOME && AtHome(m);
                return (EconomyPosted(m) || (k.with == m.id && !f_home)) ? m.id : 0;
            }
        }
        return 0;
    }
    if (stage != LIFE_CHILD){
        return 0;
    }
    const EconomyWorker* lead = NULL;
    int lead_rank = 0;
    for (const EconomyWorker& o : state.workers){
        if (o.family != k.family || o.id == k.id || !EconomyCanOrder(o,now)){
            continue;
        }
        uint32_t there =(o.post >= 0 && o.post < (int)state.settlement.size()) ? state.settlement[o.post] : 0;
        bool f_leaving = o.post >= 0 && (o.tent != ECONOMY_TENT_NONE || (there && there != EconomyPersonSettlement(state,o)));
        if (!f_leaving){
            return 0;       //one of them stays: so do the children
        }
        int rank = (o.id == k.mother) ? 0 : (o.id == k.father) ? 1 : 2;
        if (!lead || rank < lead_rank || (rank == lead_rank && o.born < lead->born)){
            lead = &o;
            lead_rank = rank;
        }
    }
    return lead ? lead->id : 0;
}

/*
    ...worked out at each tick's start, before anyone moves. A child going along is POSTED beside that grown-up, as one
    of a group is (EconomyPostPoint), on whatever post he has - packing at home as anyone setting out does, gathering
    where he gathers (and once at least, out playing with nothing packed) - and is let go with him: home again, or
    alone if his grown-ups are gone (ORPHANS, HouseFamilies). A child grown up on the way keeps his post as his own.
    A baby carried goes nowhere by itself: Tick keeps it at its mother's side.
*/
void Economy::WithTheGrown(const ZoneState& z, Walkers& walkers){
    for (EconomyWorker& k : state.workers){
        uint32_t with = GoesWith(k);
        if (with != k.with){
            k.with = with;
            state.version++;
        }
        if (EconomyStageAt(k.born,now) != LIFE_CHILD){
            continue;
        }
        const EconomyWorker* g = NULL;
        for (const EconomyWorker& o : state.workers){
            g = (with && o.id == with) ? &o : g;
        }
        int was_plot = k.post;
        int was_layer = k.post_layer;
        if (g && (k.post != g->post || k.post_layer != g->post_layer)){
            if (k.post < 0){
                k.gather = (g->gather > 0) ? g->gather : (k.state == WORKER_AT_HOME && AtHome(k)) ? 0 : 1;
            }
            k.post = g->post;
            k.post_layer = g->post_layer;
            if (k.state == WORKER_TO_POST || k.state == WORKER_ON_POST){
                GoToPost(k,z,walkers);
            }
            RePlace(k.post,k.post_layer,&k,z,walkers);
            RePlace(was_plot,was_layer,&k,z,walkers);
            state.version++;
        }else if (!g && k.post >= 0){
            k.post = -1;
            k.post_layer = -1;
            k.gather = 0;
            RePlace(was_plot,was_layer,&k,z,walkers);
            if (k.state == WORKER_TO_POST || k.state == WORKER_ON_POST){
                GoHome(k,z,walkers);
            }
            state.version++;
        }
    }
}

//Those going with `id` now (GoesWith): his household's children posted beside him, the baby he carries.
std::vector<uint32_t> Economy::Along(uint32_t id) const{
    std::vector<uint32_t> out;
    for (const EconomyWorker& o : state.workers){
        if (GoesWith(o) == id){
            out.push_back(o.id);
        }
    }
    return out;
}

//...into his new home with him, pitching or settling: of his household and home from now on, and off their post.
void Economy::BringAlong(const EconomyWorker& k, const std::vector<uint32_t>& along, const ZoneState& z, Walkers& walkers){
    for (EconomyWorker& o : state.workers){
        if (std::find(along.begin(),along.end(),o.id) == along.end()){
            continue;
        }
        o.family = k.family;
        o.house = k.house;
        if (o.post >= 0){
            int was_plot = o.post;
            int was_layer = o.post_layer;
            o.post = -1;
            o.post_layer = -1;
            o.gather = 0;
            RePlace(was_plot,was_layer,&o,z,walkers);
            if (o.state == WORKER_TO_POST || o.state == WORKER_ON_POST){
                GoHome(o,z,walkers);
            }
        }
        state.version++;
    }
}

/*
    SETTLING (settlements_plan.md, the user's 2026-10-11): someone ordered onto the ground of another settlement moves
    there without a tent - once he stands inside its borders, on his way or at his post. Into his partner's home if
    they live there (REJOINING); else a camp of it with a bed to spare, the nearest; else an empty house of it; else
    squeezed in (below). As for pitching, his house place goes - a household of his own - his order is over, and he
    goes home to the new one.
*/
bool Economy::SettleHere(uint32_t person, const ZoneState& z, Walkers& walkers, std::string* why){
    auto refuse = [&](const char* reason){
        if (why){
            *why = reason;
        }
        return false;
    };
    if (!state.world || z.world != state.world){
        return refuse("no colony");
    }
    EconomyWorker* k = NULL;
    for (EconomyWorker& o : state.workers){
        k = (o.id == person) ? &o : k;
    }
    if (!k){
        return refuse("nobody by that id");
    }
    if (!EconomyCanOrder(*k,now)){
        return refuse(EconomyWhyNotOrdered(*k,now));     //a child goes with his grown-ups (WITH THE GROWN), and settles with them
    }
    uint32_t there = (k->post >= 0 && k->post < (int)state.settlement.size()) ? state.settlement[k->post] : 0;
    if (!there || there == EconomyPersonSettlement(state,*k)){
        return refuse("not sent to another settlement");
    }
    const ChasmWorld& w = *state.world;
    GridPick here = w.picker->Pick(k->pos);
    if (!here.f_hit || here.plot < 0 || here.plot >= (int)state.settlement.size() || state.settlement[here.plot] != there){
        return refuse("not there yet");
    }
    //His partner's home there - else a bed: a camp of it with one to spare, the nearest - else an empty house of it, the nearest.
    const EconomyWorker* partner = PartnerLivingIn(*k,there);
    uint32_t partner_family = partner ? partner->family : 0;
    uint32_t bed = partner ? partner->house : 0;
    for (int pass = 0; pass < 2 && !bed; pass++){
        float best = 1e30f;
        for (size_t id = 1; id < z.buildings.size() && id < home_plot.size(); id++){
            if (home_plot[id] < 0 || EconomySettlementOf(state,(uint32_t)id) != there || !stands[id]){
                continue;
            }
            bool f_free = false;
            if (pass == 0){
                f_free = CampSpare(z,(uint32_t)id,k) > 0;
            }else if (z.buildings[id].kind == ZONE_KIND_HOUSE && z.buildings[id].size > 0 && state.capacity[id] > 0
                      && !EconomyShutIn(state,(uint32_t)id)){
                f_free = std::none_of(state.workers.begin(),state.workers.end(),[&](const EconomyWorker& o){
                    return o.house == id;
                });
            }
            float d = (w.grid->fine.pos[home_plot[id]] - k->pos).length();
            if (f_free && d < best){
                best = d;
                bed = (uint32_t)id;
            }
        }
    }
    /*
        No bed free: he SQUEEZES IN all the same (the user's, 2026-10-11: "a new person settling in a camp with 2 tents
        should be homeless but belong to that settlement") - crammed into the home of it with the most room to spare,
        the nearest on a tie, as one whose home went is (HouseFamilies, CRAMMED), and on to a house when one holds him.
    */
    if (!bed){
        int best_room = 0;
        float best_d = 0.0f;
        for (size_t id = 1; id < z.buildings.size() && id < home_plot.size() && id < state.capacity.size(); id++){
            int kind = z.buildings[id].kind;
            if (home_plot[id] < 0 || EconomySettlementOf(state,(uint32_t)id) != there || !stands[id] || z.buildings[id].size <= 0
                || (kind != ZONE_KIND_CAMP && kind != ZONE_KIND_HOUSE) || (kind == ZONE_KIND_HOUSE && EconomyShutIn(state,(uint32_t)id))){
                continue;
            }
            int room = state.capacity[id] - (int)std::count_if(state.workers.begin(),state.workers.end(),[&](const EconomyWorker& o){
                return o.house == id && o.tent != ECONOMY_TENT_CARRIED;
            });
            float d = (w.grid->fine.pos[home_plot[id]] - k->pos).length();
            if (!bed || room > best_room || (room == best_room && d < best_d)){
                bed = (uint32_t)id;
                best_room = room;
                best_d = d;
            }
        }
    }
    if (!bed){
        return refuse("nowhere to live there");     //a settlement is its homes: never met
    }
    std::vector<uint32_t> along = Along(k->id);    //WITH THE GROWN: while he is still on his way
    if (partner){
        k->family = partner_family;
    }else{
        OwnHousehold(*k);
    }
    k->house = bed;
    BringAlong(*k,along,z,walkers);
    if (k->tent == ECONOMY_TENT_TO_TAKE){
        k->tent = ECONOMY_TENT_NONE;
    }
    int was_plot = k->post;
    int was_layer = k->post_layer;
    k->post = -1;
    k->post_layer = -1;
    RePlace(was_plot,was_layer,k,z,walkers);     //those still posted there spread round it again (selection_plan.md)
    k->gather = 0;
    k->forage = FORAGE_NONE;
    //Home - unless a pressing need has him on his way to a supply, which he sees to first (GoHome sends him on).
    if (k->state == WORKER_TO_POST || k->state == WORKER_ON_POST){
        GoHome(*k,z,walkers);
    }
    HouseFamilies(z,walkers);
    f_restaff = true;
    state.version++;
    return true;
}

/*
    PITCHED (settlements_plan.md): the tent he carried stands on `plot` (Zones::PitchTent). The zones' change is
    followed at once, so the new camp is known - its bed, its door, its settlement - and he moves in: his house place
    given up (the user's), a household of his own from now on, as a new couple founds one - unless he is one already,
    or his partner lives in that settlement: then he joins them, at their home (REJOINING), and the tent stands spare.
    His order is over: he is home. A house with room for him there, if one stands empty, as anyone in a camp is offered.
*/
void Economy::Pitched(uint32_t person, int plot, const ZoneState& z, Walkers& walkers){
    if (!state.world || z.world != state.world || plot < 0 || plot >= (int)z.building.size() || !z.building[plot]){
        return;
    }
    uint32_t camp = z.building[plot];
    FollowZones(z,walkers);
    EconomyWorker* k = NULL;
    for (EconomyWorker& o : state.workers){
        k = (o.id == person) ? &o : k;
    }
    if (!k){
        return;
    }
    const EconomyWorker* partner = PartnerLivingIn(*k,EconomySettlementOf(state,camp));
    uint32_t home = partner ? partner->house : camp;
    std::vector<uint32_t> along = Along(k->id);    //WITH THE GROWN: while he still carries the tent
    if (partner){
        k->family = partner->family;
    }else{
        OwnHousehold(*k);
    }
    if (k->carry > 0){
        DropLoad(*k,z);
    }
    k->house = home;
    BringAlong(*k,along,z,walkers);
    k->tent = ECONOMY_TENT_NONE;
    int was_plot = k->post;
    int was_layer = k->post_layer;
    k->post = -1;
    k->post_layer = -1;
    RePlace(was_plot,was_layer,k,z,walkers);     //those still posted there spread round it again (selection_plan.md)
    k->gather = 0;
    k->forage = FORAGE_NONE;
    k->store = 0;
    k->prop = -1;
    k->f_indoors = false;
    StopWhereHeIs(*k);
    k->state = WORKER_AT_HOME;
    HouseFamilies(z,walkers);
    if (k->state == WORKER_AT_HOME && !AtHome(*k)){
        GoHome(*k,z,walkers);
    }
    f_restaff = true;
    state.version++;
}

/*
    GUARDS BY HAND (defence_plan.md, step 8). Posted: someone old enough, with a house to go out from, to a tower
    that stands whole and that he can walk to. He leaves his job, or his order, for it - his load put down as a
    crate, as for an order - and goes up on the next tick (TickWorker). Into a free place under its count; with
    none, its count goes up by one while it has places to spare; with every place taken, the weakest archer of its
    AUTOMATIC guards goes home for him (the user's: the player's pick overrides the game's) - refused only when
    every one is hand-picked. Already its guard: only made the player's pick. Released (tower 0): home jobless,
    and its count drops to the guards it has less him, so nobody takes his place.
*/
bool Economy::PostGuard(uint32_t person, uint32_t tower, const ZoneState& z, Walkers& walkers, std::string* why){
    auto refuse = [&](const char* reason){
        if (why){
            *why = reason;
        }
        return false;
    };
    if (!state.world || z.world != state.world){
        return refuse("no colony");
    }
    EconomyWorker* k = NULL;
    for (EconomyWorker& o : state.workers){
        if (o.id == person){
            k = &o;
        }
    }
    if (!k){
        return refuse("nobody by that id");
    }
    auto at_tower = [&](uint32_t b, bool f_chosen_only){
        int n = 0;
        for (const EconomyWorker& o : state.workers){
            n += (o.job == WORKER_JOB_GUARD && o.building == b && (o.f_chosen || !f_chosen_only)) ? 1 : 0;
        }
        return n;
    };
    //Its places follow its count at once, not at the next change of the zones (Derive).
    auto places_now = [&](uint32_t b){
        if (b < state.places.size()){
            state.places[b] = TowerWhole(z,b) ? std::max(0,state.guards[b]) : 0;
        }
    };
    if (tower == 0){
        uint32_t b = k->building;
        if (k->job != WORKER_JOB_GUARD || !b || b >= state.guards.size()){
            return refuse("not a guard");
        }
        state.guards[b] = std::max(0,std::min(state.guards[b] - 1,at_tower(b,false) - 1));
        places_now(b);
        k->job = WORKER_JOB_NONE;
        k->building = 0;
        k->f_chosen = false;
        if (k->post < 0 && (k->state == WORKER_TO_POST || k->state == WORKER_ON_POST)){
            GoHome(*k,z,walkers);
        }
        f_restaff = true;
        state.version++;
        return true;
    }
    if (!EconomyCanOrder(*k,now) || k->job == WORKER_JOB_PLAYING || k->job == WORKER_JOB_BABY){
        return refuse(EconomyWhyNotOrdered(*k,now));
    }
    if (k->job == WORKER_JOB_CARING){
        return refuse("caring for her baby");
    }
    if (tower >= z.buildings.size() || z.buildings[tower].kind != ZONE_KIND_TOWER || z.buildings[tower].size <= 0){
        return refuse("no tower there");
    }
    //A site - or one placed this very tick, not yet the economy's (Derive) - is no tower to stand on yet.
    if (tower >= state.guards.size() || !TowerWhole(z,tower)){
        return refuse("the tower is not built yet");
    }
    if (!k->house || HomePlot(*k) < 0){
        return refuse("no house to go out from");
    }
    int plot = plots_of[tower].front();
    if (!CanWalkPosted(z,walkers,*k,plot)){
        return refuse("no way there");
    }
    if (k->job == WORKER_JOB_GUARD && k->building == tower){
        k->f_chosen = true;
        state.version++;
        return true;
    }
    int cap = ZoneTowerGuards(*state.world,plot);
    if (at_tower(tower,true) >= cap){
        return refuse("every place is taken");
    }
    int& count = state.guards[tower];
    if (at_tower(tower,false) >= count){
        if (count < cap){
            count++;
        }else{
            EconomyWorker* out = NULL;
            for (EconomyWorker& o : state.workers){
                if (o.job == WORKER_JOB_GUARD && o.building == tower && !o.f_chosen
                    && (!out || o.archery < out->archery
                        || (o.archery == out->archery && o.skills[SKILL_PRECISION] < out->skills[SKILL_PRECISION]))){
                    out = &o;
                }
            }
            if (out){
                out->job = WORKER_JOB_NONE;     //off the tower and home on the next tick, as when it is pulled down
                out->building = 0;
            }
        }
        places_now(tower);
    }
    int was_plot = k->post;
    int was_layer = k->post_layer;
    k->post = -1;
    k->post_layer = -1;
    k->forage = FORAGE_NONE;
    RePlace(was_plot,was_layer,k,z,walkers);     //those he was ordered out with spread round it again
    if (k->carry > 0){
        DropLoad(*k,z);
    }
    k->job = WORKER_JOB_GUARD;
    k->building = tower;
    k->f_chosen = true;
    if (k->state == WORKER_TO_POST || k->state == WORKER_ON_POST){
        GoToPost(*k,z,walkers);
    }
    f_restaff = true;
    state.version++;
    return true;
}

bool Economy::SetGuards(uint32_t tower, int count, const ZoneState& z){
    if (!state.world || z.world != state.world || tower >= z.buildings.size() || tower >= state.guards.size()
        || state.guards[tower] < 0 || tower >= plots_of.size() || plots_of[tower].empty()){
        return false;
    }
    state.guards[tower] = std::max(0,std::min(count,ZoneTowerGuards(*state.world,plots_of[tower].front())));
    if (tower < state.places.size()){
        state.places[tower] = TowerWhole(z,tower) ? state.guards[tower] : 0;
    }
    f_restaff = true;
    state.version++;
    return true;
}

/*
    A PRESSING NEED (EconomyPressing) his home has none of - water, or food - and he takes on no more work
    (the user's, 2026-10-07): off to the nearest place that has some of what home lacks - a store, the
    camp's supplies, a collector's water, a field's harvest; never another family's house. Nearest by the
    straight line, the lower id on a tie; the first of the nearest few he can walk to. False with no need,
    a home that can meet it, or nowhere he can get to - then he carries on as before, since stopping work
    would save nobody, and the work may be the collector that would. `f_home` false: he cannot get home, so
    what it holds does not count. POSTED (defence_plan.md), he is not going home either: his own house is
    then a supply like any other, by its distance - another family's still never is.
    PROVISIONS (defence_plan.md): a posted person's food is any food, provisions too, the nearest place
    first. A villager's is the crops; only if no place he can get to has any does he look again, with
    provisions - his LAST RESORT.
*/
bool Economy::SeekSupply(EconomyWorker& k, const ZoneState& z, Walkers& walkers, bool f_home){
    GoodsMask pressing = EconomyPressing(k);
    //A baby goes nowhere for it (BIRTHS): its family's fetching is what feeds it.
    if (!pressing || k.job == WORKER_JOB_BABY){
        return false;
    }
    bool f_posted = EconomyPosted(k);
    if (f_posted){
        return SeekSupplyOf(k,z,walkers,false,pressing);
    }
    if (SeekSupplyOf(k,z,walkers,f_home,(GoodsMask)(pressing & ~GOOD_BIT(GOOD_PROVISIONS)))){
        return true;
    }
    return (pressing & GOODS_FOOD) && SeekSupplyOf(k,z,walkers,f_home,pressing);
}

//SeekSupply's search for a place with some of `want`: water, and food - provisions only where `want` has them.
bool Economy::SeekSupplyOf(EconomyWorker& k, const ZoneState& z, Walkers& walkers, bool f_home, GoodsMask want,
                           bool f_towers){
    bool f_posted = EconomyPosted(k);
    /*
        SETTLEMENTS (the user's, 2026-10-11): in need, only from his own settlement, or from land no settlement holds -
        never another village's supplies, posted or not, nor a scout's gathering (GatherSupplies comes here too).
    */
    uint32_t mine = EconomyPersonSettlement(state,k);
    auto in_reach = [&](uint32_t settlement){
        return settlement == 0 || settlement == mine;
    };
    //What building `id` - or a crate (ECONOMY_STORE_CRATE) - holds for him: his own home all of it, a camp's
    //pantry with its supplies.
    auto held = [&](uint32_t id){
        const std::array<int,GOOD_COUNT>* s = SourceStock(id);
        return (id == k.house) ? HomeHeld(z,id) : s ? *s : std::array<int,GOOD_COUNT>{};
    };
    //What `id` has of `goods`.
    auto has = [&](uint32_t id, GoodsMask goods){
        std::array<int,GOOD_COUNT> s = held(id);
        for (int g = 0; g < GOOD_COUNT; g++){
            if ((goods & GOOD_BIT(g)) && s[g] > 0){
                return true;
            }
        }
        return false;
    };
    //What his home has he gets there, on his way back.
    if (f_home && k.house && k.house < state.stock.size()){
        if (has(k.house,GOOD_BIT(GOOD_WATER))){
            want &= (GoodsMask)~GOOD_BIT(GOOD_WATER);
        }
        if (has(k.house,GOODS_FOOD)){
            want &= (GoodsMask)~GOODS_EATEN;
        }
    }
    if (!want){
        return false;
    }
    /*
        What is there of a kind of good that nobody already on his way to it counts on - a unit each, what
        takes him past pressing, though he drinks or eats his fill when he gets there. Without it a supply
        as small as a collector's sent everyone thirsty at once, and half back with nothing (two of ten dry
        for a day); counting each one's fill instead kept the thirsty at home dry while the collector
        refilled under them (three of ten died).
    */
    auto spare = [&](uint32_t id, GoodsMask goods){
        std::array<int,GOOD_COUNT> s = held(id);
        int left = 0;
        for (int g = 0; g < GOOD_COUNT; g++){
            left += (goods & GOOD_BIT(g)) ? s[g] : 0;
        }
        for (const EconomyWorker& o : state.workers){
            if (&o != &k && o.store == id && (o.state == WORKER_TO_SUPPLY || o.state == WORKER_SUPPLYING)
                && (EconomyPressing(o) & goods)){
                left--;
            }
        }
        return left;
    };
    GoodsMask food = want & GOODS_EATEN;
    auto offers = [&](uint32_t id){
        return ((want & GOOD_BIT(GOOD_WATER)) && spare(id,GOOD_BIT(GOOD_WATER)) > 0)
            || (food && spare(id,food) > 0);
    };
    const ChasmWorld& w = *state.world;
    //Where he would eat there: his own house at its door, anywhere else at its plot nearest him.
    auto source = [&](uint32_t id){
        return (id == k.house) ? HomePlot(k) : SourcePlot(id,k.pos);
    };
    std::vector<std::pair<float,uint32_t>> found;
    size_t n = std::min(z.buildings.size(),state.stock.size());
    for (size_t id = 1; id < n; id++){
        bool f_own = f_posted && id == k.house;
        //Never a builders' hut: its wheat, beans and provisions are for the sites (defence_plan.md). A guard tower
        //only for the posted: its stock is its guards', and the village would empty it.
        if ((id == k.house && !f_own) || z.buildings[id].size <= 0 || (PrivateStock(z,(uint32_t)id) && !f_own)
            || z.buildings[id].kind == ZONE_KIND_BUILDERS || (z.buildings[id].kind == ZONE_KIND_TOWER && (!f_posted || !f_towers))
            || !in_reach(EconomySettlementOf(state,(uint32_t)id)) || !has((uint32_t)id,want) || !offers((uint32_t)id)){
            continue;
        }
        int plot = source((uint32_t)id);
        if (plot >= 0){
            found.push_back(std::make_pair((w.grid->fine.pos[plot] - k.pos).length(),(uint32_t)id));
        }
    }
    //A crate is a store anyone may eat and drink at (crates_plan.md) - in his settlement, or on land none holds.
    for (const EconomyCrate& c : state.crates){
        uint32_t id = ECONOMY_STORE_CRATE | c.id;
        uint32_t at = (c.plot >= 0 && c.plot < (int)state.settlement.size()) ? state.settlement[c.plot] : 0;
        if (in_reach(at) && has(id,want) && offers(id)){
            found.push_back(std::make_pair((c.pos - k.pos).length(),id));
        }
    }
    std::sort(found.begin(),found.end());
    for (size_t t = 0; t < found.size() && t < 4; t++){
        uint32_t id = found[t].second;
        int plot = source(id);
        if (RouteTo(k,z,walkers,plot,SourcePoint(id,plot,k.pos))){
            k.store = id;
            k.prop = -1;
            k.state = WORKER_TO_SUPPLY;
            return true;
        }
    }
    return false;
}

/*
    FETCHING HOME (needs_plan.md, step 2). What his house is short of: water, food, firewood or any of them -
    water or food where it holds less than ECONOMY_HOME_DAYS of what its family drinks or eats, each counted in
    days of the family's bars, which fall a day's worth a day; firewood (step 3, HEAT) in its season, where it
    holds less than ECONOMY_HOME_DAYS of its burning or its fair share, whichever is less. `first` is the
    scarcest, in days - water on a tie, then food: thirst kills sooner. A camp counts its pantry and its
    supplies together (HomeHeld), since its families live on both, but has room only in its pantry. Nothing
    of a kind without room for a trip's worth - half a load, or half the room if that is less:
    a house too small for two days of both is always short, and was fetched for a unit at a time, every look.
    Food and water share the house's room; firewood has its woodshed.
*/
GoodsMask Economy::HomeShort(const EconomyWorker& k, const ZoneState& z, GoodsMask* first) const{
    uint32_t h = k.house;
    if (!h || h >= z.buildings.size() || h >= state.stock.size() || h >= stands.size() || !stands[h]
        || (z.buildings[h].kind != ZONE_KIND_HOUSE && z.buildings[h].kind != ZONE_KIND_CAMP)){
        return 0;
    }
    auto trip_fits = [&](int good){
        int room = (good == GOOD_WOOD) ? state.woodshed[h] : state.room[h];
        return room > 0 && HouseRoom(z,h,good) >= std::max(1,std::min((ECONOMY_LOAD + 1) / 2,(room + 1) / 2));
    };
    std::array<int,GOOD_COUNT> held = HomeHeld(z,h);
    //Its mouths at their SHARES (Economy.h): a day's eating and drinking is `mouths` quarters of an adult's.
    int64_t mouths = std::max<int64_t>(1,Mouths(state.workers,h,state.tick));
    int64_t enough = mouths * ECONOMY_HOME_DAYS * CALENDAR_DAY_TICKS / LIFE_SHARE_FULL;
    int64_t water = HeldTicks(held,GOOD_BIT(GOOD_WATER));
    int64_t food = HeldTicks(held,GOODS_FOOD);
    GoodsMask short_of = (GoodsMask)((water < enough && trip_fits(GOOD_WATER) ? GOOD_BIT(GOOD_WATER) : 0)
                                 | (food < enough && trip_fits(GOOD_WHEAT) ? GOODS_FOOD : 0));
    //Firewood's days are its burning's: ticks of it held, against water's and food's ticks a head.
    int64_t wood_ticks = INT64_MAX;
    if (FirewoodNow(h)){
        int wood = held[GOOD_WOOD];
        if (wood < std::min(state.burn[h] * ECONOMY_HOME_DAYS,FirewoodFair(z,h)) && trip_fits(GOOD_WOOD)){
            short_of |= GOOD_BIT(GOOD_WOOD);
            wood_ticks = (int64_t)wood * CALENDAR_DAY_TICKS / state.burn[h];
        }
    }
    if (first){
        *first = 0;
        int64_t least = INT64_MAX;
        const std::pair<GoodsMask,int64_t> kinds[3] = {{(GoodsMask)GOOD_BIT(GOOD_WATER),water * LIFE_SHARE_FULL / mouths},
                                                     {(GoodsMask)GOODS_FOOD,food * LIFE_SHARE_FULL / mouths},
                                                     {(GoodsMask)GOOD_BIT(GOOD_WOOD),wood_ticks}};
        for (const auto& kd : kinds){
            if ((short_of & kd.first) && (kd.second < least || !*first)){
                least = kd.second;
                *first = kd.first;
            }
        }
    }
    return short_of;
}

/*
    From home, before other work: off for a load for his house, if it is short (HomeShort) and he is the one to
    go - an idle member first; one with a job, between his rounds, only with no jobless member home to go
    instead - and nobody of the house is on the errand already. From the nearest place with some to spare that
    a home may fetch from (FetchFrom) - the camp's supplies are the colony's (the user's, 2026-10-07), and so,
    since 2026-10-09, are a collector's water and a woodcutter's pile; for food, a harvest lying on the house's
    own land too (land_plan.md: seen in its first run, a family fetching from the camp past its own full fields)
    - by the straight line, the lower id on a tie: the first of the nearest few he can walk to, and home from.
    The scarcer first; the other if nowhere has the scarcer. True if he set off.
*/
bool Economy::FetchFrom(const EconomyWorker& k, const ZoneState& z, uint32_t id) const{
    //SETTLEMENTS: only from a place in his own (Ours).
    if (!Ours(k,id)){
        return false;
    }
    //A crate is a store anyone may take from (crates_plan.md), while it lies there.
    if (EconomyIsCrateStore(id)){
        return SourceStock(id) != NULL;
    }
    if (!id || id == k.house || id >= z.buildings.size() || id >= state.stock.size() || id >= stands.size()
        || z.buildings[id].size <= 0 || !stands[id]){
        return false;
    }
    switch (z.buildings[id].kind){
        case ZONE_KIND_STORE:
        case ZONE_KIND_CAMP:
        case ZONE_KIND_WATER:
        case ZONE_KIND_CONDENSER:               //its water (steam_plan.md step 3)
        case ZONE_KIND_WOODCUTTER:
        case ZONE_KIND_WELL:                    //water_tools_plan.md
            return true;
        case ZONE_KIND_HOUSE:
            return !PrivateStock(z,id);         //nobody lives there: its stock is anyone's now
        case ZONE_KIND_FIELD:{
            const EconomyField* f = EconomyFieldOf(state,id);
            return f && f->owner == k.house && !HomeAhead(k,z);
        }
        default:
            return false;
    }
}

bool Economy::FetchHome(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    GoodsMask first = 0;
    GoodsMask short_of = HomeShort(k,z,&first);
    if (!short_of){
        return false;
    }
    //Idle for this: no job - or a mother caring for her baby (BIRTHS), or the old (OLD AGE), home anyway.
    auto idle = [](const EconomyWorker& x){
        return x.job == WORKER_JOB_NONE || x.job == WORKER_JOB_CARING || x.job == WORKER_JOB_OLD;
    };
    for (const EconomyWorker& o : state.workers){
        if (&o == &k || o.house != k.house){
            continue;
        }
        if (o.state == WORKER_TO_GET || o.state == WORKER_GETTING || o.state == WORKER_TO_PANTRY){
            return false;       //one errand a house at a time
        }
        if (!idle(k) && idle(o) && o.state == WORKER_AT_HOME && AtHome(o)){
            return false;       //an idle one is home: he goes, at his next look
        }
    }
    //What a place - a building or a crate - has of `goods` that nobody else fetching home already counts on: a load each.
    auto spare = [&](uint32_t id, GoodsMask goods){
        const std::array<int,GOOD_COUNT>* s = SourceStock(id);
        int left = 0;
        for (int g = 0; g < GOOD_COUNT && s; g++){
            left += (goods & GOOD_BIT(g)) ? (*s)[g] : 0;
        }
        for (const EconomyWorker& o : state.workers){
            if (&o != &k && o.carry_good >= 0 && (goods & GOOD_BIT(o.carry_good))){
                left -= HomeReservation(o,id,o.carry_good);
            }
        }
        return left;
    };
    const ChasmWorld& w = *state.world;
    int home = HomePlot(k);
    size_t n = std::min(z.buildings.size(),state.stock.size());
    //The scarcest first, then the others a kind at a time: water, food, firewood.
    GoodsMask order[3] = {first,0,0};
    int kinds = 1;
    for (GoodsMask kd : {(GoodsMask)GOOD_BIT(GOOD_WATER),(GoodsMask)GOODS_FOOD,(GoodsMask)GOOD_BIT(GOOD_WOOD)}){
        if ((short_of & kd) && kd != first){
            order[kinds++] = kd;
        }
    }
    for (GoodsMask goods : order){
        if (!goods){
            continue;
        }
        std::vector<std::pair<float,uint32_t>> found;
        for (size_t id = 1; id < n; id++){
            int kind = z.buildings[id].kind;
            //A field's harvest for food only - its own land's, within its fair share (FetchFrom).
            if (!FetchFrom(k,z,(uint32_t)id) || (kind == ZONE_KIND_FIELD && goods != GOODS_FOOD)
                || spare((uint32_t)id,goods) <= 0){
                continue;
            }
            //A camp with people in it keeps its wood for its own fires (HEAT): they cannot fetch any more.
            if (kind == ZONE_KIND_CAMP && goods == GOOD_BIT(GOOD_WOOD) && Living(state.workers,(uint32_t)id) > 0){
                continue;
            }
            int plot = SourcePlot((uint32_t)id,k.pos);
            if (plot >= 0){
                found.push_back(std::make_pair((w.grid->fine.pos[plot] - k.pos).length(),(uint32_t)id));
            }
        }
        //...and the crates lying about (crates_plan.md): a store anyone may take from - of his settlement's.
        for (const EconomyCrate& c : state.crates){
            uint32_t id = ECONOMY_STORE_CRATE | c.id;
            if (spare(id,goods) > 0 && Ours(k,id)){
                found.push_back(std::make_pair((c.pos - k.pos).length(),id));
            }
        }
        std::sort(found.begin(),found.end());
        for (size_t t = 0; t < found.size() && t < 4; t++){
            uint32_t id = found[t].second;
            int plot = SourcePlot(id,k.pos);
            vec2 at = SourcePoint(id,plot,k.pos);
            if (CanWalk(z,walkers,at,home) && RouteTo(k,z,walkers,plot,at)){
                k.store = id;
                k.carry_good = MostOf(*SourceStock(id),goods);   //the kind he is after; nothing carried yet
                k.carry = 0;
                k.prop = -1;
                k.state = WORKER_TO_GET;
                return true;
            }
        }
    }
    return false;
}

/*
    A load for his house picked up at `from`: of `goods`, the kind there is most of - as much as a load and the
    house's room for it allow, and no more than brings the house to a day past ECONOMY_HOME_DAYS of it, so the
    next errand is a day off and water does not fill the room food needs (it did: nine water, no food, for a
    day) - nor, from its own land's field, past its fair share (FairShare) - and off home with it
    (WORKER_TO_PANTRY). Firewood the same, in days of its burning, into its woodshed, in its season, and never
    past its fair share (FirewoodFair). False, taking nothing, with none there, no room at home or no way back.
*/
bool Economy::TakeHome(EconomyWorker& k, const ZoneState& z, Walkers& walkers, uint32_t from, GoodsMask goods){
    uint32_t h = k.house;
    //A building's stock, or a crate's (crates_plan.md) - gone if it was emptied while he walked.
    std::array<int,GOOD_COUNT>* src = SourceStock(from);
    //Never from a home somebody lives in (WHOSE IT IS, Economy.h) - whoever asks.
    if (!src || !h || h >= z.buildings.size() || h >= state.room.size() || from == h
        || (z.buildings[h].kind != ZONE_KIND_HOUSE && z.buildings[h].kind != ZONE_KIND_CAMP) || PrivateStock(z,from)){
        return false;
    }
    int good = MostOf(*src,goods);
    if (good < 0){
        return false;
    }
    std::array<int,GOOD_COUNT> held = HomeHeld(z,h);
    int upto = 0;
    if (good == GOOD_WOOD){
        if (FirewoodNow(h)){
            upto = std::min(state.burn[h] * (ECONOMY_HOME_DAYS + 1),FirewoodFair(z,h)) - held[GOOD_WOOD];
        }
    }else{
        GoodsMask kind = (good == GOOD_WATER) ? (GoodsMask)GOOD_BIT(GOOD_WATER) : (GoodsMask)GOODS_FOOD;
        int worth = (good == GOOD_WATER) ? NEEDS_WATER_WORTH : EconomyFoodWorth(good);
        //By its mouths at their SHARES (Economy.h), in quarters of an adult's.
        int64_t mouths = Mouths(state.workers,h,state.tick);
        int64_t want = mouths * (ECONOMY_HOME_DAYS + 1) * CALENDAR_DAY_TICKS / LIFE_SHARE_FULL - HeldTicks(held,kind);
        if (from < z.buildings.size() && z.buildings[from].kind == ZONE_KIND_FIELD){
            want = std::min(want,mouths * FairShare(z) / LIFE_SHARE_FULL - HeldTicks(held,kind));
        }
        upto = (want > 0 && worth > 0) ? (int)((want + worth - 1) / worth) : 0;
    }
    int take = std::min(std::min(ECONOMY_LOAD,upto),std::min((*src)[good],HouseRoom(z,h,good)));
    if (take <= 0 || !RouteHome(k,z,walkers)){
        return false;
    }
    (*src)[good] -= take;
    CrateTouched(from);
    k.carry_good = good;
    k.carry = take;
    k.store = 0;
    k.prop = -1;
    k.state = WORKER_TO_PANTRY;
    state.version++;
    return true;
}

/*
    HEAT (Economy.h), at dawn: every inhabited home under the snow burns its firewood - a whole day's, or none
    - and is warm or cold until the next dawn; any other home wants no fire. The homeless (heat[0]) are cold
    under the snow at the camp's spot, where they wait.
*/
void Economy::Heat(const ZoneState& z, uint64_t tick){
    const ChasmWorld& w = *state.world;
    std::vector<int> living(state.heat.size(),0);
    for (const EconomyWorker& k : state.workers){
        if (k.house < living.size()){
            living[k.house]++;
        }
    }
    size_t n = std::min(std::min(state.heat.size(),z.buildings.size()),std::min(state.stock.size(),state.burn.size()));
    n = std::min(n,std::min(state.pantry.size(),state.burn_halves.size()));
    uint64_t day = tick / CALENDAR_DAY_TICKS;
    for (size_t id = 1; id < n; id++){
        uint8_t heat = HEAT_NONE;
        if (state.burn[id] > 0 && living[id] > 0 && id < plots_of.size() && !plots_of[id].empty()
            && UnderSnow(w,w.grid->fine.pos[plots_of[id][0]],tick)){
            //Today's: an odd half (HEAT RISES) burnt on every other dawn, so two days burn it whole.
            int burn = (state.burn_halves[id] + (int)(day & 1)) / 2;
            //A camp's pantry first, then its supplies (STORES); a house's pantry is its stock, so this is only that.
            int& own = state.pantry[id][GOOD_WOOD];
            int& wood = state.stock[id][GOOD_WOOD];
            heat = (own + wood >= burn) ? HEAT_WARM : HEAT_COLD;
            if (heat == HEAT_WARM){
                int first = std::min(own,burn);
                own -= first;
                wood -= burn - first;
            }
        }
        state.heat[id] = heat;
    }
    if (!state.heat.empty()){
        bool f_out = living[0] > 0 && state.camp_plot >= 0 && UnderSnow(w,w.grid->fine.pos[state.camp_plot],tick);
        state.heat[0] = f_out ? HEAT_COLD : HEAT_NONE;
    }
    state.version++;
}

int Economy::HouseRoom(const ZoneState& z, uint32_t h, int good) const{
    if (h >= state.stock.size() || h >= state.room.size() || h >= state.pantry.size() || good < 0 || good >= GOOD_COUNT){
        return 0;
    }
    //A camp's room is its pantry's: the supplies beside it are not its families' to fill.
    bool f_camp = h < z.buildings.size() && z.buildings[h].kind == ZONE_KIND_CAMP;
    const std::array<int,GOOD_COUNT>& s = f_camp ? state.pantry[h] : state.stock[h];
    if (good == GOOD_WOOD){
        return std::max(0,((h < state.woodshed.size()) ? state.woodshed[h] : 0) - s[GOOD_WOOD]);
    }
    return std::max(0,state.room[h] - (CarryTotal(s) - s[GOOD_WOOD]));
}

bool Economy::PrivateStock(const ZoneState& z, uint32_t id) const{
    return EconomyPrivateStock(state,z,id);
}

std::array<int,GOOD_COUNT>& Economy::HomePantry(const ZoneState& z, uint32_t h){
    bool f_camp = h < z.buildings.size() && z.buildings[h].kind == ZONE_KIND_CAMP && h < state.pantry.size();
    return f_camp ? state.pantry[h] : state.stock[h];
}

std::array<int,GOOD_COUNT> Economy::HomeHeld(const ZoneState& z, uint32_t h) const{
    std::array<int,GOOD_COUNT> held = (h < state.stock.size()) ? state.stock[h] : std::array<int,GOOD_COUNT>{};
    if (h < z.buildings.size() && z.buildings[h].kind == ZONE_KIND_CAMP && h < state.pantry.size()){
        for (int g = 0; g < GOOD_COUNT; g++){
            held[g] += state.pantry[h][g];
        }
    }
    return held;
}

/*
    A camp nobody lives in any more: what its families had fetched is nobody's, so it joins the supplies, where
    anyone may take it (the user's, 2026-10-09) - on the tick the last of them left, not carried anywhere.
*/
void Economy::OpenPantries(const ZoneState& z){
    size_t n = std::min(std::min(state.pantry.size(),state.stock.size()),z.buildings.size());
    for (size_t id = 1; id < n; id++){
        if (CarryTotal(state.pantry[id]) == 0 || Living(state.workers,(uint32_t)id) > 0){
            continue;
        }
        for (int g = 0; g < GOOD_COUNT; g++){
            state.stock[id][g] += state.pantry[id][g];
        }
        state.pantry[id] = std::array<int,GOOD_COUNT>{};
        state.version++;
    }
}

//Its family fetches firewood: a house the full winter's snow reaches, lived in, from autumn's first day to the
//snow leaving it in spring.
bool Economy::FirewoodNow(uint32_t h) const{
    if (h >= state.snowed.size() || !state.snowed[h] || state.burn[h] <= 0 || h >= plots_of.size() || plots_of[h].empty()
        || Living(state.workers,h) <= 0){
        return false;
    }
    CalendarDate d = CalendarDateOf(now);
    if (d.season == SEASON_AUTUMN || d.season == SEASON_WINTER){
        return true;
    }
    return d.season == SEASON_SPRING && UnderSnow(*state.world,state.world->grid->fine.pos[plots_of[h][0]],now);
}

/*
    FAIR SHARES of firewood (the user's, 2026-10-08): as food's, the colony's AT HOME - every house fetching
    firewood, its wood over their burning - so house h's share is its burning's part of it, and
    ECONOMY_FAIR_DAYS of its burning more. While wood is short the houses rise together, a day at a time.
*/
int Economy::FirewoodFair(const ZoneState& z, uint32_t h) const{
    int64_t wood = 0;
    int64_t burn = 0;
    size_t n = std::min(z.buildings.size(),std::min(state.stock.size(),state.burn.size()));
    for (size_t id = 1; id < n; id++){
        if (FirewoodNow((uint32_t)id)){
            wood += HomeHeld(z,(uint32_t)id)[GOOD_WOOD];
            burn += state.burn[id];
        }
    }
    if (h >= state.burn.size() || burn <= 0){
        return 0;
    }
    return (int)((wood * state.burn[h] + burn - 1) / burn) + state.burn[h] * ECONOMY_FAIR_DAYS;
}

/*
    Every person, every tick, after his round has moved on: a tick of water gone, and of health - three
    while dry - then a meal and a drink if he stands at home, and from his pack while he has one. Health at
    nothing: he dies. He is struck from the colony, what he carried left as a crate, his pack with him; his family stays in
    its house, and on this same tick his job is given out again, and a house left empty taken by a family
    waiting for one. OLD AGE: one whose day has come dies the same way the first time he stands at home.
*/
void Economy::Needs(const ZoneState& z, Walkers& walkers, uint64_t tick){
    bool f_died = false;
    const uint32_t seed = state.world->grid->settings.seed;
    std::vector<uint32_t> aged_dead;    //OLD AGE: his day come, and home - by id, as the bitten are
    for (EconomyWorker& k : state.workers){
        //His SHARE (Economy.h): his bars fall on that many ticks in four - a baby's on two, by his id so that
        //a house's babies do not all fall on the same ticks. Eating and drinking then follow, a unit as one fits.
        //Dry is asked after the water goes, so the tick that empties it already drains as dry.
        if (EconomyShareTick(k,tick)){
            if (k.water > 0){
                k.water--;
            }
            k.health -= EconomyHealthDrain(state,k);
        }
        //Bitten to death this tick (A FIGHT): struck off below, whatever there is to eat.
        if (std::find(bitten_dead.begin(),bitten_dead.end(),k.id) != bitten_dead.end()){
            f_died = true;
            continue;
        }
        //OLD AGE: never out on an errand or a walk - home, as a birth waits for its mother.
        if (EconomyStageAt(k.born,tick) == LIFE_OLD && (int64_t)tick - k.born >= EconomyLifespan(seed,k.id)
            && k.state == WORKER_AT_HOME && AtHome(k)){
            aged_dead.push_back(k.id);
            f_died = true;
            continue;
        }
        //MEALS: the one under way goes on, before another is begun from wherever he is.
        MealTick(k);
        EatAndDrink(k,z);
        if (k.with && EconomyStageAt(k.born,tick) == LIFE_BABY){
            //WITH THE GROWN: a baby carried is fed from its mother's pack, as she would feed it.
            for (EconomyWorker& m : state.workers){
                if (m.id == k.with){
                    Consume(k,m.pack);
                }
            }
        }else if (EconomyPosted(k)){
            /*
                POSTS: a whole unit whenever it fits, from his pack - but on a tower from its stock as at home, his
                pantry, and from the pack only a kind the tower has none of: water while it has no water, food while
                it has no food. So the pack waits while the tower has any (a bean that fits where a provision does
                not was eaten from it).
            */
            if (uint32_t tower = EconomyOnTower(state,z,k)){
                std::array<int,GOOD_COUNT>& s = state.stock[tower];
                Consume(k,s);
                std::array<int,GOOD_COUNT> open = k.pack;
                if (s[GOOD_WATER] > 0){
                    open[GOOD_WATER] = 0;
                }
                if (MostOf(s,GOODS_EATEN) >= 0){
                    for (int g = 0; g < GOOD_COUNT; g++){
                        open[g] = (GOODS_EATEN & GOOD_BIT(g)) ? 0 : open[g];
                    }
                }
                std::array<int,GOOD_COUNT> before = open;
                Consume(k,open);
                for (int g = 0; g < GOOD_COUNT; g++){
                    k.pack[g] -= before[g] - open[g];
                }
            }else{
                //Ordered out (not a tower's guard, whose tower is his pantry): told when his pack can no longer keep him
                //- the tick it goes from holding water and food to lacking one of them, or both (a NOTICE). Nothing
                //more: he goes for it when a need presses, as ever (defence_plan.md, "Scouting").
                GoodsMask had = PackHolds(k);
                Consume(k,k.pack);
                GoodsMask has = PackHolds(k);
                if (k.post >= 0 && had == (GOOD_BIT(GOOD_WATER) | GOODS_EATEN) && has != had){
                    Notice(EVENT_RAN_OUT,k.id,(GoodsMask)(had & ~has));
                }
            }
        }else{
            //Not posted, and something packed: RELEASED, and on his way home - from deep in the clouds, maybe - he lives
            //on it still, after his pantry if he is home, until it goes back into the pantry there (Unpack). He walked
            //home thirsty with water on his back, and a pressing need sent him past his home for it.
            Consume(k,k.pack);
        }
        f_died = f_died || k.health <= 0;
    }
    if (!f_died){
        return;
    }
    std::vector<EconomyWorker> living;
    for (EconomyWorker& k : state.workers){
        bool f_bitten = std::find(bitten_dead.begin(),bitten_dead.end(),k.id) != bitten_dead.end();
        bool f_aged = std::find(aged_dead.begin(),aged_dead.end(),k.id) != aged_dead.end();
        if (k.health > 0 && !f_bitten && !f_aged){
            living.push_back(std::move(k));
            continue;
        }
        EconomyDeath d;
        d.id = k.id;
        d.family = k.family;
        d.f_female = k.f_female;
        d.surname = k.surname;
        d.born = k.born;
        d.mother = k.mother;
        d.father = k.father;
        d.tick = tick;
        d.cause = f_bitten ? NEEDS_DIED_ZOMBIE : f_aged ? NEEDS_DIED_OLD_AGE : EconomyDry(k) ? NEEDS_DIED_THIRST
                : EconomyCold(state,k) ? NEEDS_DIED_COLD : NEEDS_DIED_HUNGER;
        d.kills = k.kills;
        //What he carried is left where he fell, a crate (crates_plan.md, step 5) - it went with him before.
        if (k.carry > 0){
            DropLoad(k,z);
        }
        //...and he lies there a while (THE FALLEN, zombies_plan.md step 6): by the bite that killed him, else no blow.
        const vec2* struck = NULL;
        for (const std::pair<uint32_t,vec2>& b : bitten_struck){
            if (f_bitten && b.first == k.id){
                struck = &b.second;
            }
        }
        PersonFalls(k,struck);
        state.dead.push_back(d);
        Event(EVENT_DIED,d.id,k.house,-1,EconomyBody(k),d.cause);
    }
    state.workers.swap(living);
    HouseFamilies(z,walkers);
    AssignJobs(z,walkers);
    state.version++;
}

//--- Construction ----------------------------------------------------------------------------------

namespace {

//What a kind's storey takes against a house's: a tent is canvas and a few poles, a winch has its gear too.
float BuildFactor(int kind){
    switch (kind){
        case ZONE_KIND_CAMP:    return 0.5f;
        case ZONE_KIND_WINCH:   return 1.5f;
        case ZONE_KIND_BRIDGE:  return 0.75f;   //a deck and posts, no walls or roof
        default:                return 1.0f;
    }
}

/*
    What a kind's storey takes of a good other than wood, a plot-storey (SITE COSTS PER GOOD, defence_plan.md
    step 4) - by kind alone, not by area: a winch its rope (goods_plan.md), a tent its guy lines (settlements_plan.md).
    A guard tower goes by its guard places instead (TowerStoreyCost).
*/
int StoreyGoods(int kind, int good){
    if (good == GOOD_STONE){
        //A stonecutter's hut and a mine, their stone (stone_plan.md): the buildings that make stone are the first to cost it.
        return (kind == ZONE_KIND_STONECUTTER) ? ECONOMY_STONECUTTER_STONE : (kind == ZONE_KIND_MINE) ? ECONOMY_MINE_STONE : 0;
    }
    if (good != GOOD_ROPE){
        return 0;
    }
    return (kind == ZONE_KIND_WINCH) ? ECONOMY_WINCH_ROPE : (kind == ZONE_KIND_CAMP) ? ECONOMY_TENT_ROPE : 0;
}

//A guard tower's storey (defence_plan.md): its share of so much a guard place, of every good - wood too, not by its
//floor - so no junction's tower is cheaper for what it holds. Its water and provisions it KEEPS (StoreyKept).
int TowerStoreyCost(const ChasmWorld& w, int plot, int good){
    int per = (good == GOOD_WOOD) ? ECONOMY_TOWER_WOOD : (good == GOOD_WATER) ? ECONOMY_TOWER_WATER
            : (good == GOOD_PROVISIONS) ? ECONOMY_TOWER_PROVISIONS : 0;
    return ZoneTowerGuards(w,plot) * per / std::max(1,ZoneKindMaxStoreys(ZONE_KIND_TOWER));
}

//Of what a storey took, what goes into the building's stock rather than being used up: a tower's water and
//provisions, which its guards live on (defence_plan.md).
bool StoreyKept(int kind, int good){
    return kind == ZONE_KIND_TOWER && good != GOOD_WOOD;
}

}

int EconomyStoreyCost(const ChasmWorld& w, const ZoneState& z, int plot, int good, int level){
    //An arch's floor costs what its owner's storey would on that plot (arches_plan.md step 3).
    uint32_t owner = z.building[plot] ? 0 : z.ArchOwnerOf(plot);
    int kind = (owner && owner < z.buildings.size()) ? z.buildings[owner].kind : z.KindOf(plot);
    return EconomyKindStoreyCost(w,z,kind,plot,good,level);
}

int EconomyStoreysCost(const ChasmWorld& w, const ZoneState& z, int plot, int good, int from, int to){
    int cost = 0;
    for (int level = from + 1; level <= to; level++){
        cost += EconomyStoreyCost(w,z,plot,good,level);
    }
    return cost;
}

int EconomyPlotLeftCost(const ChasmWorld& w, const ZoneState& z, int plot, int good){
    if (z.building[plot]){
        return EconomyStoreysCost(w,z,plot,good,z.standing[plot],z.storeys[plot]);
    }
    //An arch's floors stand over its passage, its first floor at the second storey.
    if (z.ArchOwnerOf(plot)){
        return EconomyStoreysCost(w,z,plot,good,1 + z.ArchBuiltOf(plot),1 + ZoneArchFloors(z.ArchOf(plot)));
    }
    return 0;
}

int EconomyKindStoreyCost(const ChasmWorld& w, const ZoneState& z, int kind, int plot, int good, int level){
    if (kind == ZONE_KIND_TOWER){
        return TowerStoreyCost(w,plot,good);
    }
    float area = w.picker->PlotArea(plot);
    if (good == GOOD_WOOD){
        float wood = area * ECONOMY_BUILD_WOOD_PER_AREA * BuildFactor(kind);
        //A FOOTING (building_size_plan.md): a plot's ground storey, where the player chose plots over plot size.
        int footing = (level == 1 && ZoneKindHasBody(kind)) ? ECONOMY_FOOTING_WOOD : 0;
        return std::max(1,(int)std::ceil(wood)) + footing;
    }
    //STONE from the third storey up (building_size_plan.md), by area as wood is: height dearer than width.
    if (good == GOOD_STONE && level >= ECONOMY_HIGH_STOREY){
        return StoreyGoods(kind,good) + std::max(1,(int)std::ceil(area * ECONOMY_HIGH_STONE_PER_AREA));
    }
    //...and on a ground storey that stands on a FOUNDATION (terraces_plan.md), by its area and how far the ground
    //rises across it - so the builders bring it before anything rises, as they bring the rest.
    if (good == GOOD_STONE && level == 1 && ZoneNeedsFoundation(w,z,kind,plot)){
        return StoreyGoods(kind,good) + std::max(1,(int)std::ceil(area * ZonePlotRise(w,plot) * ECONOMY_FOUNDATION_STONE));
    }
    return StoreyGoods(kind,good);
}

int EconomyKindStoreysCost(const ChasmWorld& w, const ZoneState& z, int kind, int plot, int good, int from, int to){
    int cost = 0;
    for (int level = from + 1; level <= to; level++){
        cost += EconomyKindStoreyCost(w,z,kind,plot,good,level);
    }
    return cost;
}

/*
    A site painted on coarse cells - a field - takes so much a cell still to build, by its kind and good: a
    field its wood. A kind on cells that takes more says so here.
*/
int EconomyCellCost(int kind, int good){
    switch (kind){
        case ZONE_KIND_FIELD:   return (good == GOOD_WOOD) ? ECONOMY_FIELD_WOOD_PER_CELL : 0;
        //An archery range (range_plan.md): wood for the stands, wheat for the bales.
        case ZONE_KIND_RANGE:   return (good == GOOD_WOOD) ? ECONOMY_RANGE_WOOD_PER_CELL
                                     : (good == GOOD_WHEAT) ? ECONOMY_RANGE_WHEAT_PER_CELL : 0;
        //A zeppelin dock (steam_plan.md step 5): its cradle's footings and its mast. Its zeppelin is a site of its own.
        case ZONE_KIND_DOCK:    return (good == GOOD_WOOD) ? ECONOMY_DOCK_WOOD_PER_CELL : 0;
        default:                return 0;
    }
}

//For the panel and the tools: the whole map's plots, so not for the tick (Economy::SiteNeed is).
std::array<int,GOOD_COUNT> EconomySiteCost(const ChasmWorld& w, const ZoneState& z, uint32_t id){
    std::array<int,GOOD_COUNT> cost{};
    //On cells (a field): so much a cell still a site.
    int cells = 0;
    bool f_cells = false;
    for (size_t c = 0; c < z.field.size(); c++){
        f_cells = f_cells || z.field[c] == id;
        cells += (z.field[c] == id && !z.field_built[c]) ? 1 : 0;
    }
    if (f_cells && id < z.buildings.size()){
        for (int g = 0; g < GOOD_COUNT; g++){
            cost[g] = cells * EconomyCellCost(z.buildings[id].kind,g);
        }
        return cost;
    }
    for (size_t v = 0; v < z.building.size(); v++){
        //Its arches' floors still to build too (arches_plan.md step 3).
        if (z.building[v] == id || (!z.building[v] && z.ArchOwnerOf((int)v) == id)){
            for (int g = 0; g < GOOD_COUNT; g++){
                cost[g] += EconomyPlotLeftCost(w,z,(int)v,g);
            }
        }
    }
    return cost;
}

int EconomySiteBrought(const EconomyState& e, uint32_t id, int good){
    return (id < e.site.size() && good >= 0 && good < GOOD_COUNT) ? e.site[id][good] : 0;
}

int EconomyGroundWood(const ChasmWorld& w, int plot){
    float wood = w.picker->PlotArea(plot) * ECONOMY_GROUND_WOOD_PER_AREA;
    return std::max(1,(int)std::ceil(wood));
}

int EconomyGroundCost(const ChasmWorld& w, const ZoneState& z, int plot){
    if (plot < 0 || plot >= (int)z.ground.size()){
        return 0;
    }
    return EconomyGroundKindCost(w,z.ground[plot],plot);
}

int EconomyHutRoom(float floor_area){
    //Past the floor he works in only (the user's, 2026-10-11): a small hut is a workshop, a big one a store too.
    return (int)std::floor(std::max(0.0f,floor_area - ECONOMY_HUT_WORKSHOP) * ECONOMY_HUT_ROOM);
}

int EconomyCollectorRoom(float floor_area){
    //The hut's rule, but never under a load: its carrier comes only for a whole one (FetchFromWorkplace), and it
    //has no lot to spill onto.
    return ECONOMY_LOAD + (int)std::floor(std::max(0.0f,floor_area - ECONOMY_WATER_PUMP) * ECONOMY_WATER_ROOM);
}

float EconomySteamYield(const ChasmWorld& w, int plot){
    return 1.0f + ECONOMY_STEAM_PER_STEP * (float)ClimateStepsDown(*w.terrain,plot);
}

void EconomyCondenserFeeds(const ChasmWorld& w, const ZoneState& z, const std::vector<int>& plots, std::vector<uint32_t>& out){
    out.clear();
    for (int v : plots){
        int nb[8];
        int n = ZonePlotNeighbours(w,v,nb,8);
        for (int j = 0; j < n; j++){
            uint32_t c = (nb[j] >= 0 && nb[j] < (int)z.building.size()) ? z.building[nb[j]] : 0;
            if (c && c < z.buildings.size() && z.buildings[c].kind == ZONE_KIND_STEAM && z.standing[nb[j]] > 0
                && std::find(out.begin(),out.end(),c) == out.end()){
                out.push_back(c);
            }
        }
    }
    std::sort(out.begin(),out.end());
}

int EconomyGroundKindCost(const ChasmWorld& w, int ground, int plot){
    if (ground == ZONE_GROUND_NONE || ground == ZONE_GROUND_ROAD){
        return 0;       //a road takes work, not wood (line_works_plan.md)
    }
    return (ground == ZONE_GROUND_WALL) ? ECONOMY_WALL_WOOD : EconomyGroundWood(w,plot);
}

int EconomyTerraceStone(const ChasmWorld& w, int plot, int top){
    float lowest, highest;
    ZonePlotGroundRange(w,plot,lowest,highest);
    float fill = std::max(0.0f,(float)top * ZONE_STOREY_HEIGHT - (lowest + highest) * 0.5f);
    return std::max(1,(int)std::ceil(w.picker->PlotArea(plot) * fill * ECONOMY_TERRACE_STONE));
}

int EconomyGroundBrought(const EconomyState& e, int plot){
    return (plot >= 0 && plot < (int)e.ground_site.size()) ? e.ground_site[plot] : 0;
}

//A site that is still one: a building that has not gone, or a garden, lot, road or terrace plot still to build.
bool Economy::SiteAlive(const ZoneState& z, uint32_t id) const{
    if (EconomyIsGroundSite(id)){
        int v = EconomyGroundSitePlot(id);
        return v >= 0 && v < (int)z.ground.size() && (ZoneTerraceSite(z,v) || (z.ground[v] != ZONE_GROUND_NONE
               && !z.ground_built[v] && z.storeys[v] == 0));
    }
    return id && id < z.buildings.size() && z.buildings[id].size > 0;
}

//What has been brought to a site, by good: a garden's, lot's or wall's plot takes wood only, a terrace's stone only.
std::array<int,GOOD_COUNT> Economy::SiteBrought(uint32_t id) const{
    std::array<int,GOOD_COUNT> brought{};
    if (EconomyIsGroundSite(id)){
        int v = EconomyGroundSitePlot(id);
        bool f_stone = v >= 0 && v < (int)ground_site_stone.size() && ground_site_stone[v];
        brought[f_stone ? GOOD_STONE : GOOD_WOOD] = EconomyGroundBrought(state,v);
    }else if (id < state.site.size()){
        brought = state.site[id];
    }
    return brought;
}

//What a site still takes to finish, by good - all of it, whatever has been brought.
std::array<int,GOOD_COUNT> Economy::SiteNeed(const ZoneState& z, uint32_t id) const{
    std::array<int,GOOD_COUNT> need{};
    if (EconomyIsGroundSite(id)){
        int v = EconomyGroundSitePlot(id);
        if (ZoneTerraceSite(z,v)){
            need[GOOD_STONE] = EconomyTerraceStone(*state.world,v,z.terrace[v]);
        }else{
            need[GOOD_WOOD] = SiteAlive(z,id) ? EconomyGroundCost(*state.world,z,v) : 0;
        }
        return need;
    }
    if (id >= plots_of.size()){
        return need;
    }
    //A field site: so much a cell still to build (fields_plan.md; EconomyCellCost).
    if (id < site_cells.size() && site_cells[id] > 0){
        for (int g = 0; g < GOOD_COUNT; g++){
            need[g] = site_cells[id] * EconomyCellCost(z.buildings[id].kind,g);
        }
        return need;
    }
    //A DOCK that stands (steam_plan.md step 5): its zeppelin's NEXT stage alone, its one good - so the builders bring
    //nothing for a later stage, and it rises stage by stage (the user's, 2026-10-24: rope brought while the hull waited
    //made both rise at once with the floatstone).
    if (ZeppelinSite(z,id)){
        int stage = state.zeppelin[id] + 1;
        need[EconomyZeppelinGood(stage)] = EconomyZeppelinCost(stage);
        return need;
    }
    //Its storeys still to build, and its arches' floors (arches_plan.md step 3) - each by its level.
    for (const std::vector<int>* list : {&plots_of[id],&arches_of[id]}){
        for (int v : *list){
            for (int g = 0; g < GOOD_COUNT; g++){
                need[g] += EconomyPlotLeftCost(*state.world,z,v,g);
            }
        }
    }
    return need;
}

/*
    What a site still wants brought, by good: what it needs, less what it has and what is on its way - a
    load for each carrier still going to fetch it (he takes less if less is wanted), what each carries for
    it. `except` is left out, so a carrier can ask what is left for himself.
*/
std::array<int,GOOD_COUNT> Economy::SiteLeft(const ZoneState& z, uint32_t id, const EconomyWorker* except) const{
    std::array<int,GOOD_COUNT> left = SiteNeed(z,id);
    std::array<int,GOOD_COUNT> brought = SiteBrought(id);
    std::array<int,GOOD_COUNT> coming = EconomyComing(state,id,except);
    for (int g = 0; g < GOOD_COUNT; g++){
        left[g] -= brought[g] + coming[g];
    }
    return left;
}

bool Economy::ZeppelinSite(const ZoneState& z, uint32_t id) const{
    return id && id < z.buildings.size() && z.buildings[id].kind == ZONE_KIND_DOCK && z.buildings[id].size > 0
           && id < site_cells.size() && site_cells[id] == 0 && id < cells_of.size() && !cells_of[id].empty()
           && id < dock_lie.size() && dock_lie[id].f_fits && id < state.zeppelin.size()
           && state.zeppelin[id] < ZEPPELIN_STAGES;
}

bool Economy::TowerWhole(const ZoneState& z, uint32_t id) const{
    return id && id < z.buildings.size() && z.buildings[id].kind == ZONE_KIND_TOWER && z.buildings[id].size > 0
           && id < stands.size() && id < site_storeys.size() && id < plots_of.size()
           && stands[id] && site_storeys[id] == 0 && !plots_of[id].empty();
}

/*
    What a guard tower that stands whole still wants brought to be FULL (defence_plan.md, step 7): its full stock
    less what it holds and what is on its way - the same reckoning as a site's (SiteLeft), with what it lacks for
    what it needs. Nothing for a tower still a site: its storeys' costs are the site's.
*/
std::array<int,GOOD_COUNT> Economy::TowerLeft(const ZoneState& z, uint32_t id, const EconomyWorker* except) const{
    std::array<int,GOOD_COUNT> left{};
    if (!TowerWhole(z,id)){
        return left;
    }
    left = EconomyTowerShort(state,*state.world,id,plots_of[id].front());
    std::array<int,GOOD_COUNT> coming = EconomyComing(state,id,except);
    for (int g = 0; g < GOOD_COUNT; g++){
        left[g] -= coming[g];
    }
    return left;
}

//A good at a building - or a crate - nobody is already on his way to take: a load for each carrier going there for it.
int Economy::GoodFree(uint32_t id, int good) const{
    const std::array<int,GOOD_COUNT>* s = SourceStock(id);
    int free = s ? (*s)[good] : 0;
    for (const EconomyWorker& o : state.workers){
        free -= SiteReservation(o,id,good);
    }
    return free;
}

/*
    A store's room left for a load: its room, less what it holds and every load carried to it now (WORKER_TO_STORE,
    WORKER_UNLOADING) - but `except`'s, who asks for himself. Whoever sets out for a store asks this, never the room
    alone: with what was on its way left out, every carrier saw the same last load of room and set out for it, the first
    filled it, and each of the rest put his load down beside it - a crate by a colony's one full store that grew for
    years, its food and water there for the taking while the farmers and carriers kept bringing more (a bug report,
    2026-10-24). Putting down, at the store, is still by the room as it stands: whoever gets there first.
*/
int Economy::StoreRoomLeft(uint32_t id, const EconomyWorker* except) const{
    if (id >= state.room.size() || id >= state.stock.size()){
        return 0;
    }
    int left = state.room[id] - CarryTotal(state.stock[id]);
    for (const EconomyWorker& o : state.workers){
        if (&o != except && o.store == id && o.carry > 0 && (o.state == WORKER_TO_STORE || o.state == WORKER_UNLOADING)){
            left -= o.carry;
        }
    }
    return left;
}

//Where goods are taken from building `id`: a woodcutter's on his pile, the rest at its plot nearest `to`; a
//crate's (ECONOMY_STORE_CRATE) on the plot it lies on.
int Economy::SourcePlot(uint32_t id, const vec2& to) const{
    if (EconomyIsCrateStore(id)){
        const EconomyCrate* c = CrateById(EconomyCrateOfStore(id));
        return c ? c->plot : -1;
    }
    if (id < pile_door.size() && pile_door[id] >= 0){
        return pile_door[id];
    }
    return NearestPlot(id,to);
}

/*
    A garden or lot is built from outside: its builder stands on a plot beside it with nothing built on it
    and no wall of its own round it, the lowest index - otherwise its fence would go up round him and shut
    him in. The plot itself only if every neighbour is closed.
*/
int Economy::GroundSiteApproach(const ZoneState& z, int v) const{
    const ChasmWorld& w = *state.world;
    int nb[8];
    int n = ZonePlotNeighbours(w,v,nb,8);
    int best = -1;
    for (int i = 0; i < n; i++){
        int o = nb[i];
        if (z.storeys[o] > 0 || ZoneGroundStands(z,o) || ZoneWallStands(z,o) || w.terrain->wet[o] || !w.terrain->SameGround(o,v)){
            continue;
        }
        if (best < 0 || o < best){
            best = o;
        }
    }
    return (best >= 0) ? best : v;
}

/*
    The plot of building `id` nearest a point, the lower index on a tie; -1 if it has none. A FIELD has no
    plots of its own - it lies on coarse cells - so it is reached at its middle vertex (home_plot, the centre
    of its first cell): its farmer, the hungry and its builders all go there. Without this its harvest went
    untouched - the farmer's way to plot -1 failed, and the hungry passed it by (found 2026-10-07).
*/
int Economy::NearestPlot(uint32_t id, const vec2& to, bool f_dry) const{
    if (id >= plots_of.size()){
        return -1;
    }
    if (plots_of[id].empty()){
        return (id < home_plot.size()) ? home_plot[id] : -1;
    }
    int plot = -1;
    float best = 1e30f;
    for (int v : plots_of[id]){
        if (f_dry && state.world->terrain->wet[v]){
            continue;       //a bridge's span: its wood is brought to its ends, on the banks
        }
        float d = (state.world->grid->fine.pos[v] - to).length();
        if (d < best){
            best = d;
            plot = v;
        }
    }
    return plot;
}

/*
    An idle person's errand: of every site that still wants a good and every building with that good to
    spare - a store, the camp's supplies, a house nobody lives in, a woodcutter's pile - or crate lying about
    with it (crates_plan.md), the pair with the
    shortest walk, by the straight line, from him to the good and on to the site (the lower ids, then the
    lower good, on a tie), in the player's order where the walks are close (below). The first of the nearest
    few he can walk to, and from whose goods the site can be walked to. Each good a site wants is an errand
    of its own (SITE COSTS PER GOOD), so a site wanting wood and water has them brought from wherever each
    is nearest.
*/
bool Economy::FindSiteWork(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    const ChasmWorld& w = *state.world;
    size_t n = std::min(z.buildings.size(),state.site.size());
    struct Wanting{
        uint32_t site;
        std::array<int,GOOD_COUNT> left;    //what it still wants of each good
    };
    struct Holding{
        uint32_t id;
        std::array<int,GOOD_COUNT> free;    //what it has of each good nobody is on his way to take
    };
    auto wants = [](const std::array<int,GOOD_COUNT>& a){
        return std::any_of(a.begin(),a.end(),[](int x){ return x > 0; });
    };
    std::vector<Wanting> sites;
    std::vector<Holding> sources;
    for (size_t id = 1; id < n; id++){
        const ZoneBuilding& b = z.buildings[id];
        if (b.size <= 0){
            continue;
        }
        //SETTLEMENTS: a site, and where its goods come from, only in his own settlement (Ours).
        if (!Ours(k,(uint32_t)id)){
            continue;
        }
        //A building's storeys still to stand, or a field's cells still a site (fields_plan.md) - or a dock's zeppelin.
        if (site_storeys[id] > 0 || site_cells[id] > 0 || ZeppelinSite(z,(uint32_t)id)){
            Wanting s{(uint32_t)id,SiteLeft(z,(uint32_t)id,&k)};
            if (wants(s.left)){
                sites.push_back(s);
            }
        }
        //A camp's supplies are the colony's; a house's stock only once nobody lives there (WHOSE IT IS, Economy.h).
        //A builders' hut's PROVISIONS too, made for the sites - never its wheat and beans (defence_plan.md); and a
        //ropewalk's ROPE or a sail loft's CANVAS, as a woodpile's wood - never their hemp (EconomyHempWork).
        bool f_hut = b.kind == ZONE_KIND_BUILDERS;
        const EconomyHempWork* work = EconomyHempWorkAt(b.kind);
        bool f_holds = b.kind == ZONE_KIND_STORE || b.kind == ZONE_KIND_CAMP || b.kind == ZONE_KIND_WOODCUTTER || f_hut
                       || work || (b.kind == ZONE_KIND_HOUSE && !PrivateStock(z,(uint32_t)id));
        if (f_holds && stands[id]){
            Holding h{(uint32_t)id,{}};
            for (int g = 0; g < GOOD_COUNT; g++){
                bool f_offered = f_hut ? g == GOOD_PROVISIONS : work ? g == work->made : true;
                h.free[g] = f_offered ? GoodFree((uint32_t)id,g) : 0;
            }
            if (wants(h.free)){
                sources.push_back(h);
            }
        }
    }
    //The crates (crates_plan.md): goods lying on the ground are a store's to anyone carrying for a site.
    for (const EconomyCrate& c : state.crates){
        Holding h{ECONOMY_STORE_CRATE | c.id,{}};
        for (int g = 0; g < GOOD_COUNT; g++){
            h.free[g] = GoodFree(h.id,g);
        }
        if (wants(h.free) && Ours(k,h.id)){
            sources.push_back(h);
        }
    }
    //The gardens, lots and walls still to build, after the buildings - so on a tie a building comes first.
    std::vector<int> trees;
    for (int v : ground_sites){
        uint32_t site = ECONOMY_SITE_GROUND | (uint32_t)v;
        //A wall's wood waits for its way to be cleared - the line work below (line_works_plan.md, step 3).
        if (z.ground[v] == ZONE_GROUND_WALL){
            RoadTrees(z,v,trees);
            if (!trees.empty()){
                continue;
            }
        }
        Wanting s{site,SiteLeft(z,site,&k)};
        if (wants(s.left) && Ours(k,site)){
            sites.push_back(s);
        }
    }
    struct Errand{
        float walk;
        uint32_t site, from;
        int from_plot;
        uint32_t key;       //its place in the placing order: a building's id, a road's or wall's line - 0 none
        int job;            //a piece of line work, its index in `jobs`; -1 for a carry
        int good;           //a carry's good
        bool operator<(const Errand& o) const{
            return (walk != o.walk) ? walk < o.walk : (site != o.site) ? site < o.site : (from != o.from) ? from < o.from
                 : good < o.good;
        }
    };
    std::vector<Errand> found;
    auto site_plot = [&](uint32_t s, const vec2& to){
        return EconomyIsGroundSite(s) ? GroundSiteApproach(z,EconomyGroundSitePlot(s)) : NearestPlot(s,to,true);
    };
    for (const Wanting& s : sites){
        for (const Holding& f : sources){
            int fp = -1;
            for (int g = 0; g < GOOD_COUNT; g++){
                if (s.left[g] <= 0 || f.free[g] <= 0){
                    continue;
                }
                if (fp < 0){
                    fp = SourcePlot(f.id,k.pos);
                }
                int sp = site_plot(s.site,w.grid->fine.pos[fp]);
                float walk = (w.grid->fine.pos[fp] - k.pos).length() + (w.grid->fine.pos[sp] - w.grid->fine.pos[fp]).length();
                uint32_t key = EconomyIsGroundSite(s.site) ? z.line[EconomyGroundSitePlot(s.site)] : s.site;
                found.push_back(Errand{walk,s.site,f.id,fp,key,-1,g});
            }
        }
    }
    //Line work - a tree in a road's or wall's way, a road plot to lay - in the same ranking, by the walk to it
    //and its line's place (Economy.h, THE QUEUE): no longer only what is left when there is no wood to carry.
    std::vector<LineJob> jobs;
    LineJobs(k,z,false,jobs);
    for (size_t j = 0; j < jobs.size(); j++){
        found.push_back(Errand{jobs[j].walk,ECONOMY_SITE_GROUND | (uint32_t)jobs[j].plot,(uint32_t)j,-1,
                               z.line[jobs[j].plot],(int)j,-1});
    }
    if (found.empty()){
        return false;
    }
    std::sort(found.begin(),found.end());
    /*
        The player's say (construction_plan.md, "Which site goes first"): errands to a site PUT FIRST before
        all others, however far; then, in each of the two, those within ECONOMY_QUEUE_SLACK of its shortest
        in placing order - a building's id or a line's, from one counter - so the queue orders only what is
        near enough for the walk not to matter, and a site clearly nearer still wins. A garden or lot has no
        place in the queue: it keeps its place by distance, and the rest round it are reordered in the
        places left. A line is never put first.
    */
    auto put_first = [&](const Errand& e){
        return e.job < 0 && !EconomyIsGroundSite(e.site) && z.buildings[e.site].f_priority;
    };
    //Before even those: a builder's own hut, while it is still to build - what he was taken on for
    //(builders_plan.md), since with everyone at work nobody else may come to build it.
    auto own_hut = [&](const Errand& e){
        return e.job < 0 && k.job == WORKER_JOB_BUILDER && e.site == k.building;
    };
    size_t n_own = std::stable_partition(found.begin(),found.end(),own_hut) - found.begin();
    size_t n_first = std::stable_partition(found.begin() + n_own,found.end(),put_first) - found.begin();
    auto queue = [&](size_t a, size_t b){
        std::vector<size_t> slots;
        std::vector<Errand> queued;
        for (size_t i = a; i < b && found[i].walk <= found[a].walk + ECONOMY_QUEUE_SLACK; i++){
            if (found[i].key){
                slots.push_back(i);
                queued.push_back(found[i]);
            }
        }
        std::sort(queued.begin(),queued.end(),[](const Errand& x, const Errand& y){
            return (x.key != y.key) ? x.key < y.key : (x.walk != y.walk) ? x.walk < y.walk
                 : (x.site != y.site) ? x.site < y.site : (x.from != y.from) ? x.from < y.from : x.good < y.good;
        });
        for (size_t i = 0; i < slots.size(); i++){
            found[slots[i]] = queued[i];
        }
    };
    queue(n_own,n_first);
    queue(n_first,found.size());
    for (size_t t = 0; t < found.size() && t < 4; t++){
        const Errand& e = found[t];
        if (e.job >= 0){
            if (StartLineJob(k,z,walkers,jobs[e.job],false)){
                return true;
            }
            continue;
        }
        vec2 at = SourcePoint(e.from,e.from_plot,k.pos);
        if (!CanWalk(z,walkers,at,site_plot(e.site,at))){
            continue;
        }
        if (RouteTo(k,z,walkers,e.from_plot,at)){
            k.site = e.site;
            k.store = e.from;
            k.prop = -1;
            k.carry_good = e.good;      //the kind he goes for, nothing carried yet
            k.state = WORKER_TO_FETCH;
            return true;
        }
    }
    return false;
}

void Economy::RaiseWall(const ZoneState& z, int v){
    if (!ZoneWallPlanned(z,v) || z.storeys[v] > 0 || std::count(ground_raises.begin(),ground_raises.end(),v)
        || state.ground_site[v] < ECONOMY_WALL_WOOD){
        return;
    }
    std::vector<int> trees;
    RoadTrees(z,v,trees);
    if (!trees.empty()){
        return;     //a neighbour drawn since brought a tree into its way: up once that is felled
    }
    state.ground_site[v] -= ECONOMY_WALL_WOOD;
    ground_raises.push_back(v);     //it stands: the app raises it this tick (ZONE_OP_GROUND_RAISE)
    state.version++;
}

/*
    A builder has finished with his load: every storey the site's goods now pay for stands - the lowest
    first, over the whole building, so it rises level by level; the lower plot index on a tie. Raised
    through the zones by the app, on this tick (Raises). What it took is used up.
*/
void Economy::Build(const ZoneState& z, uint32_t id){
    //A garden's wall or a lot's fence goes up at once, when all its wood is there - a palisade's too, with
    //nothing of its way standing (RaiseWall).
    if (EconomyIsGroundSite(id)){
        int v = EconomyGroundSitePlot(id);
        if (!SiteAlive(z,id) || std::count(ground_raises.begin(),ground_raises.end(),v)){
            return;
        }
        if (z.ground[v] == ZONE_GROUND_WALL && !ZoneTerraceSite(z,v)){
            RaiseWall(z,v);
            return;
        }
        //A tree still standing on it: up once a woodcutter has felled it (Felled, woodcutting_plan.md).
        if (SiteTreesStanding(z,id)){
            return;
        }
        //A terrace's plot rises when all its stone is there (terraces_plan.md stage 2); the app raises it as one.
        int cost = ZoneTerraceSite(z,v) ? EconomyTerraceStone(*state.world,v,z.terrace[v]) : EconomyGroundWood(*state.world,v);
        if (state.ground_site[v] >= cost){
            state.ground_site[v] -= cost;
            ground_raises.push_back(v);
            state.version++;
        }
        return;
    }
    if (id == 0 || id >= plots_of.size() || id >= state.site.size()){
        return;
    }
    //Nothing of it rises while a tree stands in its way: once a woodcutter has felled the last (Felled).
    if (SiteTreesStanding(z,id)){
        return;
    }
    //A field site goes down all at once, every cell of it, when all it takes is there (fields_plan.md).
    if (id < site_cells.size() && site_cells[id] > 0){
        bool f_raised = false;
        for (int c : cells_of[id]){
            f_raised = f_raised || std::count(field_raises.begin(),field_raises.end(),c) > 0;
        }
        std::array<int,GOOD_COUNT> cost;
        bool f_all = true;
        for (int g = 0; g < GOOD_COUNT; g++){
            cost[g] = site_cells[id] * EconomyCellCost(z.buildings[id].kind,g);
            f_all = f_all && state.site[id][g] >= cost[g];
        }
        if (!f_raised && f_all){
            for (int g = 0; g < GOOD_COUNT; g++){
                state.site[id][g] -= cost[g];
            }
            for (int c : cells_of[id]){
                if (!z.field_built[c]){
                    field_raises.push_back(c);
                }
            }
            state.version++;
        }
        return;
    }
    /*
        A DOCK's zeppelin (steam_plan.md step 5): its next stage once all of its one good is there - the only good the
        site asks for (SiteNeed), so the steam goes in last and it is never left half-filled for want of canvas. Told,
        every one. A later stage's good already there (set by hand, `set_stock`'s `site`) raises it in its turn.
    */
    if (ZeppelinSite(z,id)){
        while (state.zeppelin[id] < ZEPPELIN_STAGES){
            int stage = state.zeppelin[id] + 1;
            int good = EconomyZeppelinGood(stage);
            if (state.site[id][good] < EconomyZeppelinCost(stage)){
                break;
            }
            state.site[id][good] -= EconomyZeppelinCost(stage);
            state.zeppelin[id] = stage;
            state.version++;
            Event(EVENT_ZEPPELIN_STAGE,0,id,home_plot[id],dock_lie[id].centre,stage,GOOD_BIT(good));
        }
        return;
    }
    for (;;){
        int best = -1;
        int best_level = 0;
        float best_shore = 0.0f;
        bool f_bridge = z.buildings[id].kind == ZONE_KIND_BRIDGE;
        //Its plots and its arches' (arches_plan.md step 3): an arch's next floor is at the level over its passage
        //and what of it stands, so it goes up with the building's own storeys at that height.
        std::vector<int> all = plots_of[id];
        all.insert(all.end(),arches_of[id].begin(),arches_of[id].end());
        for (int v : all){
            bool f_arch = !z.building[v];
            int level = (f_arch ? 1 + z.ArchBuiltOf(v) : z.standing[v]) + (int)std::count(raises.begin(),raises.end(),v);
            if (level >= (f_arch ? z.ArchOf(v) : z.storeys[v])){
                continue;
            }
            //A bridge from its banks out over the water: the plot nearest dry land first.
            float shore = f_bridge ? state.world->terrain->WaterEdgeDistance(state.world->grid->fine.pos[v]) : 0.0f;
            if (best < 0 || level < best_level || (level == best_level && shore > best_shore)){
                best = v;
                best_level = level;
                best_shore = shore;
            }
        }
        if (best < 0){
            return;
        }
        //All it takes of every good, or it waits for the rest (SITE COSTS PER GOOD) - the storey over the level reached.
        std::array<int,GOOD_COUNT> cost;
        for (int g = 0; g < GOOD_COUNT; g++){
            cost[g] = EconomyStoreyCost(*state.world,z,best,g,best_level + 1);
            if (state.site[id][g] < cost[g]){
                return;
            }
        }
        for (int g = 0; g < GOOD_COUNT; g++){
            state.site[id][g] -= cost[g];
            if (StoreyKept(z.buildings[id].kind,g)){
                state.stock[id][g] += cost[g];
            }
        }
        raises.push_back(best);
        state.version++;
    }
}

//--- Road works (docs/line_works_plan.md) ------------------------------------------------------------

namespace {

//The verge a planned road's trees are felled from: the view's clearance (RoadNear hides what is nearer a
//laid road) and a little over, for the curve the drawn road takes inside a corner of its edges.
#define ECONOMY_ROAD_VERGE  (ROAD_PROP_CLEARANCE + 0.4f)
#define ECONOMY_WAY_MARGIN  (ECONOMY_ROAD_VERGE + 4.0f)     //a line plot's way reaches its neighbours' and a verge past them: within this of it

float SegmentDistance2D(const vec2& p, const vec2& a, const vec2& b){
    vec2 d = b - a;
    float len2 = d.dot(d);
    float t = (len2 > 1e-9f) ? std::max(0.0f,std::min(1.0f,(p - a).dot(d) / len2)) : 0.0f;
    return (p - (a + d * t)).length();
}

}

/*
    The standing trees in the way of road plot v: within ECONOMY_ROAD_VERGE of its edges toward the road
    plots beside it - planned or laid - and standing bridges, or of its vertex alone, and on its own
    ground (not up or down a cliff) - and not COVERED (EconomyPropCovered): a tree in a woodcutter's fenced
    lot beside a road, which the view no longer drew and nobody could walk in to fell, held three plots of
    road unlaid for good (seen 2026-10-08, chasm_roadwork.rec). The edges WHOLE, not halves to their midpoints: the neighbour's half is
    felled for it too, which costs nothing, and leaves no tree under a road drawn later from that side.
    A wall plot's the same, along its edges toward the wall plots and gates beside it - its palisade's line
    (RoadMesh.h, WALLS). In bucket and index order, so the same trees in the same order every run.
*/
bool EconomyPropCovered(const ZoneState& z, int plot, int coarse){
    //What is BUILT covers its trees; a site's stand until felled (woodcutting_plan.md) - a debug paint stands at once.
    if (plot >= 0 && plot < (int)z.storeys.size() && ((z.storeys[plot] > 0 && z.standing[plot] > 0)
                                                     || (ZoneEnclosesGround(z.ground[plot]) && z.ground_built[plot]))){
        return true;
    }
    if (ZoneCastleShape(z,plot) != ZONE_CASTLE_NONE){
        return true;    //under the debug castle's stone (ramparts_plan.md)
    }
    if (ZoneTerraceStands(z,plot)){
        return true;    //under a terrace (terraces_plan.md stage 2)
    }
    return coarse >= 0 && coarse < (int)z.field.size() && z.field[coarse] && z.field_built[coarse];
}

uint32_t EconomyTreeSite(const ZoneState& z, int plot, int coarse){
    if (plot >= 0 && plot < (int)z.storeys.size()){
        if (z.building[plot] && z.storeys[plot] > 0 && z.standing[plot] == 0){
            return z.building[plot];
        }
        if ((ZoneEnclosesGround(z.ground[plot]) && !z.ground_built[plot] && z.storeys[plot] == 0) || ZoneTerraceSite(z,plot)){
            return ECONOMY_SITE_GROUND | (uint32_t)plot;
        }
    }
    if (coarse >= 0 && coarse < (int)z.field.size() && z.field[coarse] && !z.field_built[coarse]){
        return z.field[coarse];
    }
    return 0;
}

//The trees standing on a plot or coarse cell - or, f_boulders, the boulders (stone_plan.md).
static int StandingOn(const ChasmWorld& w, const EconomyState& e, int plot, int coarse, bool f_boulders){
    if (!w.forest || !w.mesh || (plot < 0 && coarse < 0)){
        return 0;
    }
    //The chunks its quads are in - a prop is listed by the chunk it stands in.
    std::vector<int> quads;
    if (coarse >= 0){
        for (int k = 0; k < 4; k++){
            quads.push_back(coarse * 4 + k);
        }
    }else{
        for (int i = 0; i < w.picker->PlotQuadCount(plot); i++){
            quads.push_back(w.picker->PlotQuadCorner(plot,i) / 4);
        }
    }
    std::vector<int> chunks;
    for (int q : quads){
        int c = TerrainChunkOfQuad(*w.grid,*w.mesh,q);
        if (c >= 0 && c < (int)w.forest->chunk_props.size() && std::find(chunks.begin(),chunks.end(),c) == chunks.end()){
            chunks.push_back(c);
        }
    }
    int n = 0;
    for (int c : chunks){
        for (int i : w.forest->chunk_props[c]){
            const PropInstance& pr = w.forest->props[i];
            bool f_standing = (size_t)i >= e.prop_state.size() || e.prop_state[i] == PROP_STATE_STANDING;
            bool f_kind = f_boulders ? pr.kind == PROP_BOULDER : IsTree(pr.kind);
            if (f_kind && f_standing && ((coarse >= 0) ? pr.coarse == coarse : pr.plot == plot)){
                n++;
            }
        }
    }
    return n;
}

int EconomyTreesOn(const ChasmWorld& w, const EconomyState& e, int plot, int coarse){
    return StandingOn(w,e,plot,coarse,false);
}

int EconomyBouldersOn(const ChasmWorld& w, const EconomyState& e, int plot, int coarse){
    return StandingOn(w,e,plot,coarse,true);
}

void Economy::RoadTrees(const ZoneState& z, int v, std::vector<int>& out) const{
    out.clear();
    const ChasmWorld& w = *state.world;
    if (!w.forest || tree_nx == 0 || v < 0 || v >= (int)z.ground.size()){
        return;
    }
    const std::vector<vec2>& pos = w.grid->fine.pos;
    const vec2 c = pos[v];
    std::vector<vec2> ends;
    float reach = 0.0f;
    int nb[8];
    int n = ZonePlotNeighbours(w,v,nb,8);
    bool f_wall = z.ground[v] == ZONE_GROUND_WALL;
    for (int i = 0; i < n; i++){
        int u = nb[i];
        bool f_line = f_wall ? (z.ground[u] == ZONE_GROUND_WALL || z.gate[u])
                             : (z.ground[u] == ZONE_GROUND_ROAD || ZoneBridgeWalkable(z,u));
        if (f_line){
            ends.push_back(pos[u]);
            reach = std::max(reach,(pos[u] - c).length());
        }
    }
    if (ends.empty()){
        ends.push_back(c);
    }
    reach += ECONOMY_ROAD_VERGE;
    const std::vector<PropInstance>& props = w.forest->props;
    int x0 = std::max(0,(int)((c.x - reach - tree_origin.x) / tree_cell));
    int x1 = std::min(tree_nx - 1,(int)((c.x + reach - tree_origin.x) / tree_cell));
    int z0 = std::max(0,(int)((c.y - reach - tree_origin.y) / tree_cell));
    int z1 = std::min(tree_nz - 1,(int)((c.y + reach - tree_origin.y) / tree_cell));
    for (int bz = z0; bz <= z1; bz++){
        for (int bx = x0; bx <= x1; bx++){
            int b = bz * tree_nx + bx;
            for (int j = tree_start[b]; j < tree_start[b + 1]; j++){
                int i = tree_items[j];
                const PropInstance& pr = props[i];
                if (state.prop_state[i] != PROP_STATE_STANDING || pr.plot < 0 || !w.terrain->SameGround(pr.plot,v)
                    || EconomyPropCovered(z,pr.plot,pr.coarse)){
                    continue;
                }
                vec2 p(pr.pos.x,pr.pos.z);
                for (const vec2& e : ends){
                    if (SegmentDistance2D(p,c,e) < ECONOMY_ROAD_VERGE){
                        out.push_back(i);
                        break;
                    }
                }
            }
        }
    }
}

void Economy::TreesInTheWay(const EconomyWorker& k, const ZoneState& z, const vec2& at, float reach,
                            std::vector<int>& out) const{
    out.clear();
    const std::vector<vec2>& pos = state.world->grid->fine.pos;
    std::vector<int> trees;
    auto add = [&](int v){
        if ((pos[v] - at).length() > reach + ECONOMY_WAY_MARGIN || !Ours(k,ECONOMY_SITE_GROUND | (uint32_t)v)){
            return;
        }
        RoadTrees(z,v,trees);
        out.insert(out.end(),trees.begin(),trees.end());
    };
    for (int v : road_sites){
        add(v);
    }
    for (int v : wall_sites){
        add(v);
    }
    std::sort(out.begin(),out.end());
    out.erase(std::unique(out.begin(),out.end()),out.end());
}

void Economy::Felled(const ZoneState& z, int prop){
    const PropInstance& pr = state.world->forest->props[prop];
    vec2 at(pr.pos.x,pr.pos.z);
    const std::vector<vec2>& pos = state.world->grid->fine.pos;
    for (int v : wall_sites){
        if ((pos[v] - at).length() <= ECONOMY_WAY_MARGIN){
            RaiseWall(z,v);
        }
    }
    uint32_t site = EconomyTreeSite(z,pr.plot,pr.coarse);
    if (site){
        Build(z,site);
    }
}

bool Economy::SiteTreesStanding(const ZoneState& z, uint32_t id) const{
    const ChasmWorld& w = *state.world;
    if (!w.forest || tree_nx == 0){
        return false;
    }
    //Where to look: the site's plots not yet standing, or a field's cells not yet built - each round its middle.
    std::vector<vec2> spots;
    if (EconomyIsGroundSite(id)){
        spots.push_back(w.grid->fine.pos[EconomyGroundSitePlot(id)]);
    }else if (id < cells_of.size() && !cells_of[id].empty()){
        for (int c : cells_of[id]){
            if (!z.field_built[c]){
                vec2 mid(0.0f,0.0f);
                for (int k = 0; k < 4; k++){
                    mid += w.grid->FineQuadCentre(c * 4 + k) * 0.25f;
                }
                spots.push_back(mid);
            }
        }
    }else if (id < plots_of.size()){
        for (int v : plots_of[id]){
            if (z.standing[v] == 0){
                spots.push_back(w.grid->fine.pos[v]);
            }
        }
    }
    const float r = 6.0f;       //past a coarse cell's corner from its middle, and well past a plot's quarters
    for (const vec2& c : spots){
        int x0 = std::max(0,(int)((c.x - r - tree_origin.x) / tree_cell));
        int x1 = std::min(tree_nx - 1,(int)((c.x + r - tree_origin.x) / tree_cell));
        int z0 = std::max(0,(int)((c.y - r - tree_origin.y) / tree_cell));
        int z1 = std::min(tree_nz - 1,(int)((c.y + r - tree_origin.y) / tree_cell));
        for (int bz = z0; bz <= z1; bz++){
            for (int bx = x0; bx <= x1; bx++){
                int b = bz * tree_nx + bx;
                for (int j = tree_start[b]; j < tree_start[b + 1]; j++){
                    int i = tree_items[j];
                    const PropInstance& pr = w.forest->props[i];
                    if (state.prop_state[i] == PROP_STATE_STANDING && EconomyTreeSite(z,pr.plot,pr.coarse) == id){
                        return true;
                    }
                }
            }
        }
    }
    //...or a BOULDER (stone_plan.md), which waits for a stonecutter as a tree for a woodcutter.
    for (int i : rocks){
        const PropInstance& pr = w.forest->props[i];
        if (pr.kind == PROP_BOULDER && state.prop_state[i] == PROP_STATE_STANDING && EconomyTreeSite(z,pr.plot,pr.coarse) == id){
            return true;
        }
    }
    return false;
}

/*
    Line work (line_works_plan.md): of the plots of planned roads, the nearest him by the straight line (the
    lower index on a tie) - from home, any; on a stint, only within ECONOMY_ROAD_REACH of where he stands, so he
    works along the line rather than across the map. At each, the piece to do: the plot itself to lay, if
    nothing of its way still stands and nobody is laying it. ONLY A WOODCUTTER FELLS (woodcutting_plan.md):
    a plot with a tree in its way waits for one (WoodNear), and a wall's has nothing to lay - it waits for its
    wood (FindSiteWork). The first few pieces.
*/
void Economy::LineJobs(const EconomyWorker& k, const ZoneState& z, bool f_stint, std::vector<LineJob>& out) const{
    out.clear();
    if (road_sites.empty()){
        return;
    }
    const ChasmWorld& w = *state.world;
    const std::vector<vec2>& pos = w.grid->fine.pos;
    std::vector<int> taken_plots;
    for (const EconomyWorker& o : state.workers){
        if (&o != &k && ((o.state == WORKER_TO_ROAD && o.prop < 0) || o.state == WORKER_LAYING)){
            taken_plots.push_back(EconomyGroundSitePlot(o.site));
        }
    }
    std::vector<std::pair<float,int>> found;
    auto consider = [&](int v){
        if (std::count(ground_raises.begin(),ground_raises.end(),v)){
            return;         //laid or raised this tick; the zones have not caught up yet
        }
        if (!Ours(k,ECONOMY_SITE_GROUND | (uint32_t)v)){
            return;         //SETTLEMENTS: another settlement's road, or none's
        }
        float d = (pos[v] - k.pos).length();
        if (!f_stint || d <= ECONOMY_ROAD_REACH){
            found.push_back(std::make_pair(d,v));
        }
    };
    for (int v : road_sites){
        consider(v);
    }
    std::sort(found.begin(),found.end());
    std::vector<int> trees;
    for (size_t t = 0; t < found.size() && t < 12 && out.size() < 4; t++){
        int v = found[t].second;
        RoadTrees(z,v,trees);
        if (!trees.empty() || std::find(taken_plots.begin(),taken_plots.end(),v) != taken_plots.end()){
            continue;   //a tree still in its way, for a woodcutter, or being laid
        }
        out.push_back(LineJob{found[t].first,v,-1,v,pos[v]});
    }
}

bool Economy::StartLineJob(EconomyWorker& k, const ZoneState& z, Walkers& walkers, const LineJob& j, bool f_stint){
    if (!RouteTo(k,z,walkers,j.to_plot,j.to)){
        return false;
    }
    k.site = ECONOMY_SITE_GROUND | (uint32_t)j.plot;
    k.prop = j.tree;
    k.store = 0;
    k.state = WORKER_TO_ROAD;
    if (!f_stint){
        k.stint = 0;
    }
    return true;
}

//The line work alone - for a stint along the line, or when nothing else in the ranking could be walked to.
bool Economy::FindRoadWork(EconomyWorker& k, const ZoneState& z, Walkers& walkers, bool f_stint){
    std::vector<LineJob> jobs;
    LineJobs(k,z,f_stint,jobs);
    for (const LineJob& j : jobs){
        if (StartLineJob(k,z,walkers,j,f_stint)){
            return true;
        }
    }
    return false;
}

/*
    A piece of road work done (or found done by others): the next along the line while his trip lasts, home
    after - and home at once if he was given a workplace meanwhile (a builder stays on), as a carrier
    finishes his errand and stops, or once he needs food or water (GoHome sends him for it).
*/
void Economy::NextRoadWork(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    k.prop = -1;
    if ((k.job == WORKER_JOB_NONE || k.job == WORKER_JOB_BUILDER) && k.stint < ECONOMY_ROAD_STINT && !EconomyPressing(k)
        && FindRoadWork(k,z,walkers,true)){
        return;
    }
    k.site = 0;
    k.stint = 0;
    GoHome(k,z,walkers);
}

//--- Crates (crates_plan.md) -----------------------------------------------------------------------

const char* EconomyCrateOriginName(int origin){
    return (origin == CRATE_FOUND) ? "found" : (origin == CRATE_CAMP) ? "camp" : (origin == CRATE_RUBBLE) ? "rubble"
         : (origin == CRATE_BROKEN) ? "broken" : (origin == CRATE_SITE) ? "site" : "dropped";
}

bool EconomyCrateGround(const ChasmWorld& w, const ZoneState& z, int plot, const vec2& at){
    if (plot < 0 || plot >= (int)z.building.size() || plot >= (int)w.grid->fine.pos.size()){
        return false;
    }
    if (z.building[plot] || z.storeys[plot] > 0 || (z.ground[plot] != ZONE_GROUND_NONE && z.ground[plot] != ZONE_GROUND_ROAD)
        || w.terrain->wet[plot] || w.terrain->Mountain(plot)){
        return false;
    }
    //A field is painted on coarse cells, not plots: the one under where the crate lies.
    GridPick p = w.picker->Pick(at);
    return !(p.f_hit && p.coarse_quad >= 0 && p.coarse_quad < (int)z.field.size() && z.field[p.coarse_quad]);
}

const EconomyCrate* EconomyCrateOn(const EconomyState& e, int plot){
    for (const EconomyCrate& c : e.crates){
        if (c.plot == plot){
            return &c;
        }
    }
    return NULL;
}

const EconomyCrate* EconomyCrateInTheWay(const EconomyState& e, const ChasmWorld& w, int op, int index, int value){
    switch (op){
        case ZONE_OP_BUILD_PAINT:
        case ZONE_OP_BUILD_ADD:
            return EconomyCrateOn(e,index);
        case ZONE_OP_GROUND_PAINT:
            return (value != ZONE_GROUND_ROAD) ? EconomyCrateOn(e,index) : NULL;
        case ZONE_OP_TERRACE:
            return EconomyCrateOn(e,index);     //it would be buried (terraces_plan.md stage 2)
        case ZONE_OP_FIELD_PAINT:
        case ZONE_OP_RANGE_PAINT:
            //A cell holds a crate whose point lies in it (EconomyCrateGround asks the same).
            for (const EconomyCrate& c : e.crates){
                GridPick p = w.picker->Pick(c.pos);
                if (p.f_hit && p.coarse_quad == index){
                    return &c;
                }
            }
            return NULL;
        default:
            return NULL;     //a bridge's plots are wet, where no crate lies; the rest put nothing down
    }
}

void Economy::AddToCrate(int plot, const vec2& at, int good, int amount, int origin, uint32_t by){
    for (EconomyCrate& c : state.crates){
        if (c.plot == plot){
            c.stock[good] += amount;
            state.crates_version++;
            state.version++;
            return;
        }
    }
    EconomyCrate c;
    c.id = state.next_crate++;
    c.plot = plot;
    c.pos = at;
    c.stock[good] = amount;
    c.origin = origin;
    c.by = by;
    c.tick = state.tick;
    state.crates.push_back(c);      //ids rise, so the list stays in id order
    state.crates_version++;
    state.version++;
}

/*
    His load put down (CRATES). Standing at a store that takes it - unloading there, or just arrived - it goes
    in, as far as the store has room: that is where it was going. The rest is a crate: on his own plot if that is
    open ground, at his feet; else on the open plot nearest him within ECONOMY_CRATE_RINGS steps - stepping on
    only across ground people walk over (open ground, a camp among its tents, a standing bridge) and on his own
    level, so it never lands behind a wall, past a house or down a cliff from him - at the plot's point, the
    lower index on a tie. In the middle of the camp, its tents all round him, that is a few steps out (found in
    its first test: stepping only over open ground, a load put down there was lost). With no open ground that
    near, which a person standing anywhere he can walk to should never meet, the load is lost, as every load was
    before crates.
*/
void Economy::DropLoad(EconomyWorker& k, const ZoneState& z){
    const ChasmWorld& w = *state.world;
    int good = k.carry_good;
    int amount = k.carry;
    k.carry = 0;
    k.carry_good = -1;
    if (amount <= 0 || good < 0 || good >= GOOD_COUNT){
        return;
    }
    GridPick here = w.picker->Pick(k.pos);
    int start = here.f_hit ? here.plot : -1;
    if (start < 0 || start >= (int)z.building.size()){
        return;
    }
    uint32_t s = z.building[start];
    if (s && s < state.accepts.size() && (state.accepts[s] & GOOD_BIT(good))){
        int put = std::min(amount,std::max(0,state.room[s] - CarryTotal(state.stock[s])));
        state.stock[s][good] += put;
        amount -= put;
        state.version++;
    }
    if (amount <= 0){
        return;
    }
    //Where a crate already lies on his plot, the load goes into it, which stays where it is.
    int at = CratePlotNear(z,start,k.pos);
    if (at >= 0){
        AddToCrate(at,(at == start) ? k.pos : w.grid->fine.pos[at],good,amount,CRATE_DROPPED,k.id);
    }
}

//Where a crate goes down from `start`: there, if it is open ground, else the open plot nearest `at` within
//ECONOMY_CRATE_RINGS, stepping only over ground people walk (open ground, a camp, a standing bridge) on its level.
int Economy::CratePlotNear(const ZoneState& z, int start, const vec2& at) const{
    const ChasmWorld& w = *state.world;
    if (EconomyCrateGround(w,z,start,at)){
        return start;
    }
    std::vector<int> seen(1,start);
    std::vector<int> ring(1,start);
    std::vector<int> next;
    int best = -1;
    float best_d = 1e30f;
    for (int r = 0; r < ECONOMY_CRATE_RINGS && best < 0; r++){
        next.clear();
        for (int v : ring){
            int nb[8];
            int n = ZonePlotNeighbours(w,v,nb,8);
            for (int i = 0; i < n; i++){
                int o = nb[i];
                if (std::find(seen.begin(),seen.end(),o) != seen.end() || !w.terrain->SameGround(o,start)){
                    continue;
                }
                seen.push_back(o);
                bool f_open = EconomyCrateGround(w,z,o,w.grid->fine.pos[o]);
                if (f_open || z.KindOf(o) == ZONE_KIND_CAMP || ZoneBridgeWalkable(z,o)){
                    next.push_back(o);
                }
                if (!f_open){
                    continue;
                }
                float d = (w.grid->fine.pos[o] - at).length();
                if (d < best_d || (d == best_d && o < best)){
                    best_d = d;
                    best = o;
                }
            }
        }
        ring.swap(next);
    }
    return best;
}

//A crate whose ground has been built over - a debug placement, which the play rules refuse - is lost with it.
void Economy::LoseCoveredCrates(const ZoneState& z){
    const ChasmWorld& w = *state.world;
    size_t before = state.crates.size();
    state.crates.erase(std::remove_if(state.crates.begin(),state.crates.end(),[&](const EconomyCrate& c){
        return !EconomyCrateGround(w,z,c.plot,c.pos);
    }),state.crates.end());
    if (state.crates.size() != before){
        state.crates_version++;
        state.version++;
    }
}

/*
    RUBBLE (stone_plan.md): which plots get a pile and how much, by a hash of the seed and the plot - a world always
    has the same rubble, and plot order gives the ids. Only at the foot of a balcony's wall (the user's, 2026-10-15:
    none on the rim, so the first stone waits for a winch). A pile lies RUBBLE_TOWARD of the way to its plot's nearest
    higher corner, so it sits against the wall and still within its own plot's column. On a SEAM's foot (Deposits.h)
    every plot gets one, of floatstone: its crystal fallen, the way to the seam before there is a mine. Islands are
    left for the zeppelins.
*/
void Economy::PlaceRubble(const ZoneState& z){
    const ChasmWorld& w = *state.world;
    const Grid& g = *w.grid;
    const GridPicker& p = *w.picker;
    const Terrain& t = *w.terrain;
    const uint32_t seed = g.settings.seed;
    for (int v = 0; v < (int)g.fine.pos.size(); v++){
        if (t.level[v] != TERRAIN_BALCONY || g.fine.f_boundary[v]){
            continue;
        }
        int edge = -1;
        float edge_d = 1e30f;
        for (int i = 0; i < p.PlotQuadCount(v); i++){
            const GridQuad& q = g.fine.quads[p.PlotQuadCorner(v,i) / 4];
            for (int k = 0; k < 4; k++){
                int c = q.v[k];
                float d = (g.fine.pos[c] - g.fine.pos[v]).length();
                if (t.steps[c] < t.steps[v] && (d < edge_d || (d == edge_d && c < edge))){
                    edge_d = d;
                    edge = c;
                }
            }
        }
        bool f_seam = w.deposits && w.deposits->SeamAt(v) >= 0;
        if (edge < 0 || (!f_seam && (float)(Mix(seed ^ 0x72756231u,(uint32_t)v) % 10000u) >= RUBBLE_WALL_CHANCE * 10000.0f)){
            continue;
        }
        vec2 at = g.fine.pos[v] + (g.fine.pos[edge] - g.fine.pos[v]) * RUBBLE_TOWARD;
        if (!EconomyCrateGround(w,z,v,at)){
            continue;
        }
        int lo = f_seam ? RUBBLE_FLOATSTONE_MIN : RUBBLE_WALL_STONE_MIN;
        int hi = f_seam ? RUBBLE_FLOATSTONE_MAX : RUBBLE_WALL_STONE_MAX;
        int amount = lo + (int)(Mix(seed ^ (f_seam ? 0x72756233u : 0x72756232u),(uint32_t)v) % (uint32_t)(hi - lo + 1));
        AddToCrate(v,at,f_seam ? GOOD_FLOATSTONE : GOOD_STONE,amount,CRATE_RUBBLE,0);
    }
}

bool Economy::SpawnCrate(int plot, int good, int amount, const ZoneState& z, std::string* why){
    auto refuse = [&](const char* reason){
        if (why){
            *why = reason;
        }
        return false;
    };
    if (!state.world || z.world != state.world){
        return refuse("no colony");
    }
    if (good < 0 || good >= GOOD_COUNT || amount <= 0){
        return refuse("no goods");
    }
    const ChasmWorld& w = *state.world;
    if (plot < 0 || plot >= (int)w.grid->fine.pos.size()){
        return refuse("no such plot");
    }
    if (!EconomyCrateGround(w,z,plot,w.grid->fine.pos[plot])){
        return refuse("not open ground");
    }
    AddToCrate(plot,w.grid->fine.pos[plot],good,amount,CRATE_FOUND,0);
    return true;
}

bool Economy::SetPickUp(uint32_t crate, bool f_on, std::string* why){
    for (EconomyCrate& c : state.crates){
        if (c.id == crate){
            c.f_pick_up = f_on;
            state.version++;
            return true;
        }
    }
    if (why){
        *why = "no such crate";
    }
    return false;
}

const EconomyCrate* Economy::CrateById(uint32_t id) const{
    for (const EconomyCrate& c : state.crates){
        if (c.id == id){
            return &c;
        }
    }
    return NULL;
}

std::array<int,GOOD_COUNT>* Economy::SourceStock(uint32_t id){
    return const_cast<std::array<int,GOOD_COUNT>*>(static_cast<const Economy*>(this)->SourceStock(id));
}

const std::array<int,GOOD_COUNT>* Economy::SourceStock(uint32_t id) const{
    if (EconomyIsCrateStore(id)){
        const EconomyCrate* c = CrateById(EconomyCrateOfStore(id));
        return c ? &c->stock : NULL;
    }
    return (id && id < state.stock.size()) ? &state.stock[id] : NULL;
}

//A crate taken from: the view told, and - emptied - off the map (the user's: gone when empty).
void Economy::CrateTouched(uint32_t store){
    if (!EconomyIsCrateStore(store)){
        return;
    }
    uint32_t id = EconomyCrateOfStore(store);
    state.crates.erase(std::remove_if(state.crates.begin(),state.crates.end(),[&](const EconomyCrate& c){
        return c.id == id && CarryTotal(c.stock) <= 0;
    }),state.crates.end());
    state.crates_version++;
    state.version++;
}

//Beside it, on the side he comes from - not in it, as he would stand at the foot of a tree (GoForWood).
vec2 Economy::CrateStand(const EconomyCrate& c, const vec2& from) const{
    vec2 to = c.pos - from;
    float l = to.length();
    return (l > ECONOMY_CRATE_STAND * 2.0f) ? c.pos - to * (ECONOMY_CRATE_STAND / l) : c.pos;
}

vec2 Economy::SourcePoint(uint32_t id, int plot, const vec2& from) const{
    const EconomyCrate* c = EconomyIsCrateStore(id) ? CrateById(EconomyCrateOfStore(id)) : NULL;
    return c ? CrateStand(*c,from) : state.world->grid->fine.pos[plot];
}

/*
    THE CARETAKERS' LAST ERRAND (crates_plan.md, the user's): a crate's goods to a storehouse - only a builder's,
    and only once there is nothing to carry or build and no line work (TickWorker). Of the crates holding a good
    some store takes a load of - its whole load, with the room - and that he could walk on to that store from,
    the nearest by the straight line, the lower id on a tie: the first of the nearest few he can walk to. A
    crate another caretaker is already on his way to only while it holds more than that one's load.
    `f_picked` (PICK UP, stone_plan.md): only the crates the player put PICK UP, wherever they lie - the order is the
    player's, as a post is - though the store is one of his own settlement's still. Not the caretakers' alone: anyone
    without a job runs it too (the user's, 2026-10-18), and nothing here asks his job. And only within what he can walk
    without water (the user's, 2026-10-15): the walk to it and on to that store, by the routes he would take, no
    longer than ECONOMY_PICK_UP_WATER of a full water bar. A crate turned down for it is marked so for its card.
*/
bool Economy::MoveCrate(EconomyWorker& k, const ZoneState& z, Walkers& walkers, bool f_picked){
    const ChasmWorld& w = *state.world;
    //The nearest store that takes `amount` of `good` whole, by the straight line from `at` - its plot nearest
    //there - or -1.
    auto store_plot = [&](int good, int amount, const vec2& at){
        int best = -1;
        float best_d = 1e30f;
        for (size_t s = 1; s < state.accepts.size() && s < plots_of.size(); s++){
            if (!(state.accepts[s] & GOOD_BIT(good)) || !Ours(k,(uint32_t)s) || StoreRoomLeft((uint32_t)s,&k) < amount){
                continue;
            }
            for (int v : plots_of[s]){
                float d = (w.grid->fine.pos[v] - at).length();
                if (d < best_d){
                    best_d = d;
                    best = v;
                }
            }
        }
        return best;
    };
    std::vector<std::pair<float,uint32_t>> found;
    for (const EconomyCrate& c : state.crates){
        uint32_t id = ECONOMY_STORE_CRATE | c.id;
        if (f_picked ? !c.f_pick_up : !Ours(k,id)){
            continue;       //SETTLEMENTS: a crate in another's, or in none - unless it is picked up
        }
        int left = CarryTotal(c.stock);
        for (const EconomyWorker& o : state.workers){
            if (&o != &k && o.store == id && (o.state == WORKER_TO_CRATE || o.state == WORKER_LIFTING)){
                left -= ECONOMY_LOAD;
            }
        }
        if (left > 0){
            found.push_back(std::make_pair((c.pos - k.pos).length(),id));
        }
    }
    std::sort(found.begin(),found.end());
    const float reach = ECONOMY_PICK_UP_WATER * (float)NEEDS_WATER_FULL / (float)CALENDAR_TICKS_PER_SECOND;
    for (size_t t = 0; t < found.size() && t < 4; t++){
        EconomyCrate* c = NULL;     //not CrateById's: its mark is set here
        for (EconomyCrate& x : state.crates){
            c = ((ECONOMY_STORE_CRATE | x.id) == found[t].second) ? &x : c;
        }
        int good = -1;
        int store = -1;
        for (int g = 0; g < GOOD_COUNT && good < 0; g++){
            int sp = (c->stock[g] > 0) ? store_plot(g,std::min(ECONOMY_LOAD,c->stock[g]),c->pos) : -1;
            if (sp >= 0 && CanWalk(z,walkers,c->pos,sp)){
                good = g;
                store = sp;
            }
        }
        if (good < 0){
            continue;
        }
        if (f_picked){
            PickUpLook* seen = NULL;
            for (PickUpLook& l : pick_up_looks){
                seen = (l.crate == c->id && l.by == k.id) ? &l : seen;
            }
            bool f_far = false;
            if (seen && seen->zones == z.version && seen->store == store && seen->from.x == k.pos.x && seen->from.y == k.pos.y){
                f_far = seen->f_far;
            }else{
                EconomyWorker back;
                back.pos = c->pos;
                float there = WalkSeconds(k,z,walkers,c->plot,CrateStand(*c,k.pos));
                float on = WalkSeconds(back,z,walkers,store,state.world->grid->fine.pos[store]);
                f_far = there < 0.0f || on < 0.0f || there + on > reach;
                if (!seen){
                    //Room for a verdict on each of the nearest four for everyone who looks - the jobless too now - so
                    //a colony with a dozen idle does not empty it at every look and plan every route again.
                    if (pick_up_looks.size() >= 64 + 4 * state.workers.size()){
                        pick_up_looks.clear();
                    }
                    pick_up_looks.push_back(PickUpLook());
                    seen = &pick_up_looks.back();
                }
                seen->crate = c->id;
                seen->by = k.id;
                seen->zones = z.version;
                seen->store = store;
                seen->from = k.pos;
                seen->f_far = f_far;
            }
            if (c->f_too_far != f_far){
                c->f_too_far = f_far;
                state.version++;
            }
            if (f_far){
                continue;
            }
        }
        if (RouteTo(k,z,walkers,c->plot,CrateStand(*c,k.pos))){
            k.store = found[t].second;
            k.carry_good = good;        //the kind he goes for; nothing carried yet
            k.carry = 0;
            k.prop = -1;
            k.state = WORKER_TO_CRATE;
            return true;
        }
    }
    return false;
}

//--- The towers kept (docs/defence_plan.md, step 7) ------------------------------------------------

/*
    A caretaker's FIRST errand, from home (the user's: towers ahead of building): of every guard tower that stands
    whole and lacks water or provisions (TowerLeft - its full stock less what it holds and what is on its way) and
    every place with that good to spare - a store, the camp's supplies, a house nobody lives in, a builders' hut's
    provisions, a crate - the pair with the shortest walk, by the straight line from him to the good and on to the
    tower (the lower ids, then the lower good, on a tie). The first of the nearest few he can walk to, and from
    whose goods the tower can be walked to. He goes as for a site - `site` the tower, WORKER_TO_FETCH - and puts it
    in its stock at the end (WORKER_TO_SITE). Provisions nobody has are not here: they are made for the tower first
    (MakeProvisions, which counts it), and carried from the hut once made. False if there is nothing to take.
*/
bool Economy::KeepTowers(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    if (k.job != WORKER_JOB_BUILDER){
        return false;
    }
    const ChasmWorld& w = *state.world;
    const int goods[2] = {GOOD_WATER,GOOD_PROVISIONS};
    struct Short{
        uint32_t id;
        int plot;
        std::array<int,GOOD_COUNT> left;
    };
    size_t n = std::min(z.buildings.size(),state.stock.size());
    std::vector<Short> towers;
    for (size_t id = 1; id < n; id++){
        if (!TowerWhole(z,(uint32_t)id) || !Ours(k,(uint32_t)id)){
            continue;
        }
        Short t{(uint32_t)id,plots_of[id].front(),TowerLeft(z,(uint32_t)id,&k)};
        if (t.left[GOOD_WATER] > 0 || t.left[GOOD_PROVISIONS] > 0){
            towers.push_back(t);
        }
    }
    if (towers.empty()){
        return false;
    }
    struct Holding{
        uint32_t id;
        std::array<int,GOOD_COUNT> free;
    };
    std::vector<Holding> sources;
    for (size_t id = 1; id < n && id < stands.size(); id++){
        const ZoneBuilding& b = z.buildings[id];
        bool f_hut = b.kind == ZONE_KIND_BUILDERS;
        bool f_holds = b.kind == ZONE_KIND_STORE || b.kind == ZONE_KIND_CAMP || f_hut || b.kind == ZONE_KIND_WELL
                       || (b.kind == ZONE_KIND_HOUSE && !PrivateStock(z,(uint32_t)id));
        if (b.size <= 0 || !f_holds || !stands[id] || !Ours(k,(uint32_t)id)){
            continue;
        }
        Holding h{(uint32_t)id,{}};
        for (int g : goods){
            h.free[g] = (!f_hut || g == GOOD_PROVISIONS) ? GoodFree((uint32_t)id,g) : 0;
        }
        if (h.free[GOOD_WATER] > 0 || h.free[GOOD_PROVISIONS] > 0){
            sources.push_back(h);
        }
    }
    for (const EconomyCrate& c : state.crates){
        Holding h{ECONOMY_STORE_CRATE | c.id,{}};
        for (int g : goods){
            h.free[g] = GoodFree(h.id,g);
        }
        if ((h.free[GOOD_WATER] > 0 || h.free[GOOD_PROVISIONS] > 0) && Ours(k,h.id)){
            sources.push_back(h);
        }
    }
    struct Errand{
        float walk;
        uint32_t tower, from;
        int tower_plot, from_plot, good;
        bool operator<(const Errand& o) const{
            return (walk != o.walk) ? walk < o.walk : (tower != o.tower) ? tower < o.tower : (from != o.from) ? from < o.from
                 : good < o.good;
        }
    };
    std::vector<Errand> found;
    for (const Short& t : towers){
        for (const Holding& f : sources){
            int fp = -1;
            for (int g : goods){
                if (t.left[g] <= 0 || f.free[g] <= 0){
                    continue;
                }
                if (fp < 0){
                    fp = SourcePlot(f.id,k.pos);
                }
                if (fp < 0){
                    break;
                }
                float walk = (w.grid->fine.pos[fp] - k.pos).length() + (w.grid->fine.pos[t.plot] - w.grid->fine.pos[fp]).length();
                found.push_back(Errand{walk,t.id,f.id,t.plot,fp,g});
            }
        }
    }
    std::sort(found.begin(),found.end());
    for (size_t i = 0; i < found.size() && i < 4; i++){
        const Errand& e = found[i];
        vec2 at = SourcePoint(e.from,e.from_plot,k.pos);
        if (!CanWalk(z,walkers,at,e.tower_plot)){
            continue;
        }
        if (RouteTo(k,z,walkers,e.from_plot,at)){
            k.site = e.tower;
            k.store = e.from;
            k.prop = -1;
            k.carry_good = e.good;      //the kind he goes for, nothing carried yet
            k.state = WORKER_TO_FETCH;
            return true;
        }
    }
    return false;
}

//--- Provisions (docs/defence_plan.md, step 5) -------------------------------------------------------

/*
    How many provisions are still wanted beyond what is made: what the sites still lack of them (SiteLeft,
    less what is on its way), the towers that stand short of them (TowerLeft, step 7) and the level the colony
    keeps (STOCK KEPT), less what lies made where the sites' carriers take from (a store, the camp's supplies, a
    house nobody lives in, a builders' hut, a crate), what a builder carries from a hut to a store, and what is
    being made. Below 0: made and wanted by nobody. Wheat and beans at the huts are not counted - they are made
    into provisions only while this is above 0.
    SPARE is the made ones beyond what the sites and towers lack, less the stores' share of the level: what a hut
    holds past that goes to a store, so the kept ones lie where anyone posted takes from - never the sites' share.
    BY COLONY (settlements_plan.md, step 2): all of it in settlement `settlement` alone - its builders take from nowhere
    else - and its colony's level.
*/
int Economy::ProvisionsLacking(const ZoneState& z, uint32_t settlement, int* spare) const{
    int wanted = 0;
    int held = 0;
    size_t n = std::min(z.buildings.size(),state.site.size());
    for (size_t id = 1; id < n; id++){
        const ZoneBuilding& b = z.buildings[id];
        if (b.size <= 0 || EconomySettlementOf(state,(uint32_t)id) != settlement){
            continue;
        }
        if (site_storeys[id] > 0 || site_cells[id] > 0){
            wanted += std::max(0,SiteLeft(z,(uint32_t)id,NULL)[GOOD_PROVISIONS]);
        }else if (TowerWhole(z,(uint32_t)id)){
            wanted += std::max(0,TowerLeft(z,(uint32_t)id,NULL)[GOOD_PROVISIONS]);
        }
        bool f_holds = b.kind == ZONE_KIND_STORE || b.kind == ZONE_KIND_CAMP || b.kind == ZONE_KIND_BUILDERS
                       || (b.kind == ZONE_KIND_HOUSE && !PrivateStock(z,(uint32_t)id));
        if (f_holds && stands[id]){
            held += std::max(0,GoodFree((uint32_t)id,GOOD_PROVISIONS));
        }
    }
    for (const EconomyCrate& c : state.crates){
        if (SettlementOfPlace(ECONOMY_STORE_CRATE | c.id) == settlement){
            held += std::max(0,GoodFree(ECONOMY_STORE_CRATE | c.id,GOOD_PROVISIONS));
        }
    }
    for (const EconomyWorker& o : state.workers){
        //Not a ropemaker's rope or a sailmaker's canvas; a builder makes them at his hut.
        held += (o.state == WORKER_MAKING && !EconomyHempWorkOf(o.job) && EconomySettlementOf(state,o.building) == settlement) ? 1 : 0;
        held += (o.state == WORKER_TO_STORE && o.store && o.carry_good == GOOD_PROVISIONS && SettlementOfPlace(o.store) == settlement)
                ? o.carry : 0;
    }
    int level = EconomyKeptLevel(state,EconomyColonyNumber(state,settlement),GOOD_PROVISIONS);
    if (spare){
        int stored = level - KeptShort(z,GOOD_PROVISIONS,settlement);     //in the stores and on its way there, up to the level
        *spare = held - wanted - std::min(level,std::max(0,stored));
    }
    return wanted + level - held;
}

/*
    What settlement `settlement`'s stores still lack of made good `good`'s level - its colony's: the level, less what
    its stores and camp's supplies hold (EconomyStoredTotals, the top bar's count) and what anyone carries to one.
*/
int Economy::KeptShort(const ZoneState& z, int good, uint32_t settlement) const{
    int level = EconomyKeptLevel(state,EconomyColonyNumber(state,settlement),good);
    if (level <= 0){
        return 0;
    }
    int stored = EconomyStoredTotals(state,z,settlement)[good];
    for (const EconomyWorker& o : state.workers){
        stored += (o.state == WORKER_TO_STORE && o.store && o.carry_good == good && SettlementOfPlace(o.store) == settlement)
                  ? o.carry : 0;
    }
    return level - stored;
}

uint32_t Economy::SettlementOfPlace(uint32_t id) const{
    if (EconomyIsCrateStore(id)){
        const EconomyCrate* c = CrateById(id & ~ECONOMY_STORE_CRATE);
        return (c && c->plot >= 0 && c->plot < (int)state.settlement.size()) ? state.settlement[c->plot] : 0;
    }
    if (EconomyIsGroundSite(id)){
        int v = EconomyGroundSitePlot(id);
        return (v >= 0 && v < (int)state.settlement.size()) ? state.settlement[v] : 0;
    }
    return EconomySettlementOf(state,id);
}

std::array<int,GOOD_COUNT>& Economy::KeptOf(uint32_t colony){
    while (state.kept.size() <= colony){
        std::array<int,GOOD_COUNT> start{};
        for (int g = 0; g < GOOD_COUNT; g++){
            start[g] = EconomyKept(state,(uint32_t)state.kept.size(),g);      //the start's, for a colony with none
        }
        state.kept.push_back(start);
    }
    return state.kept[colony];
}

std::array<int,GOOD_COUNT>& Economy::OrderedOf(uint32_t colony){
    while (state.ordered.size() <= colony){
        state.ordered.push_back(std::array<int,GOOD_COUNT>{});
    }
    return state.ordered[colony];
}

bool Economy::SetKept(int good, int count, uint32_t colony){
    if (good < 0 || good >= GOOD_COUNT || !(GOODS_MADE & GOOD_BIT(good))){
        return false;
    }
    KeptOf(colony)[good] = std::max(0,count);
    state.version++;
    return true;
}

bool Economy::OrderMore(int good, int count, const ZoneState& z, uint32_t colony){
    if (good < 0 || good >= GOOD_COUNT || !(GOODS_MADE & GOOD_BIT(good)) || !state.world || z.world != state.world){
        return false;
    }
    if (count <= 0){
        OrderedOf(colony)[good] = 0;
    }else{
        int stored = EconomyStoredTotals(state,z,EconomyColonySettlement(state,colony))[good];
        int ordered = EconomyOrdered(state,colony,good);
        int from = (ordered > 0) ? ordered : std::max(EconomyKept(state,colony,good),stored);
        OrderedOf(colony)[good] = from + count;
    }
    state.version++;
    return true;
}

/*
    A builder's provisions errand, from home - before his building work (the user's order: towers, then
    provisions, then building, then crates). While some are wanted (ProvisionsLacking): to his hut to make
    them, with a wheat and a beans there. Else to his hut for a load of what lies there for a store: provisions
    past the sites' share (the kept ones), and with none wanted its wheat and beans - when a store takes it. Else,
    with too few on hand and on their way at every hut for what is wanted, for a load of the scarcer of the two
    at his own hut - from the nearest store, camp's supplies, house nobody lives in or crate with some to spare.
    False if none of that is to do, or he can get to none of it.
*/
bool Economy::MakeProvisions(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    const ChasmWorld& w = *state.world;
    uint32_t h = k.building;
    if (k.job != WORKER_JOB_BUILDER || !Workshop(z,stands,h)){
        return false;
    }
    const std::array<int,GOOD_COUNT>& hut = state.stock[h];
    uint32_t settlement = EconomySettlementOf(state,h);     //his hut's: what it makes them for (BY COLONY)
    int spare = 0;
    int lacking = ProvisionsLacking(z,settlement,&spare);
    auto to_hut = [&](){
        int plot = NearestPlot(h,k.pos);
        if (plot < 0 || !RouteTo(k,z,walkers,plot,w.grid->fine.pos[plot])){
            return false;
        }
        k.store = 0;
        k.site = 0;
        k.prop = -1;
        k.stint = 0;
        k.state = WORKER_TO_WORKSHOP;
        return true;
    };
    if (lacking > 0 && hut[GOOD_WHEAT] > 0 && hut[GOOD_BEANS] > 0){
        return to_hut();
    }
    //Something there for a store, and a store with room for a load of it (as AtWorkshop takes it).
    for (int g : {GOOD_PROVISIONS,GOOD_WHEAT,GOOD_BEANS}){
        int load = std::min(ECONOMY_LOAD,(g == GOOD_PROVISIONS) ? std::min(hut[g],spare) : (lacking <= 0) ? hut[g] : 0);
        for (size_t s = 1; load > 0 && s < state.accepts.size(); s++){
            if ((state.accepts[s] & GOOD_BIT(g)) && Ours(k,(uint32_t)s) && StoreRoomLeft((uint32_t)s,&k) >= load){
                return to_hut();
            }
        }
    }
    if (lacking <= 0){
        return false;
    }
    //What every hut of his settlement will make with what it has and what is on its way to it: its wheat and beans,
    //in pairs.
    auto coming = [&](uint32_t hh, int good){
        int n = 0;
        for (const EconomyWorker& o : state.workers){
            if (o.building != hh || o.carry_good != good){
                continue;
            }
            n += (o.state == WORKER_TO_INPUT || o.state == WORKER_TAKING_INPUT) ? o.stint
               : (o.state == WORKER_TO_WORKSHOP) ? o.carry : 0;
        }
        return n;
    };
    int will_make = 0;
    int mine[2] = {0,0};
    for (size_t hh = 1; hh < z.buildings.size() && hh < state.stock.size(); hh++){
        if (!Workshop(z,stands,(uint32_t)hh) || EconomySettlementOf(state,(uint32_t)hh) != settlement){
            continue;
        }
        int wheat = state.stock[hh][GOOD_WHEAT] + coming((uint32_t)hh,GOOD_WHEAT);
        int beans = state.stock[hh][GOOD_BEANS] + coming((uint32_t)hh,GOOD_BEANS);
        will_make += std::min(wheat,beans);
        if (hh == h){
            mine[0] = wheat;
            mine[1] = beans;
        }
    }
    int pairs = lacking - will_make;
    if (pairs <= 0){
        return false;
    }
    //Where wheat or beans can be had: a store, the camp's supplies, a house nobody lives in, a crate - with
    //some nobody else is on his way for. By the straight line from him.
    auto sources = [&](int good, std::vector<std::pair<float,uint32_t>>& out){
        out.clear();
        for (size_t id = 1; id < z.buildings.size() && id < stands.size(); id++){
            const ZoneBuilding& b = z.buildings[id];
            bool f_holds = b.kind == ZONE_KIND_STORE || b.kind == ZONE_KIND_CAMP
                           || (b.kind == ZONE_KIND_HOUSE && !PrivateStock(z,(uint32_t)id));
            int plot = (b.size > 0 && f_holds && stands[id] && GoodFree((uint32_t)id,good) > 0 && Ours(k,(uint32_t)id))
                       ? SourcePlot((uint32_t)id,k.pos) : -1;
            if (plot >= 0){
                out.push_back(std::make_pair((w.grid->fine.pos[plot] - k.pos).length(),(uint32_t)id));
            }
        }
        for (const EconomyCrate& c : state.crates){
            uint32_t id = ECONOMY_STORE_CRATE | c.id;
            if (GoodFree(id,good) > 0 && Ours(k,id)){
                out.push_back(std::make_pair((c.pos - k.pos).length(),id));
            }
        }
        std::sort(out.begin(),out.end());
    };
    /*
        The scarcer at his hut - wheat on a tie - as many as the pairs still wanted (each pair takes one more of
        it). Only as many as pair with what is there of the other, where none of the other is to be had: no
        wheat or no beans, no provisions (defence_plan.md), and nothing fetched to wait at the hut for its half.
    */
    int good = (mine[0] <= mine[1]) ? GOOD_WHEAT : GOOD_BEANS;
    int other = (good == GOOD_WHEAT) ? GOOD_BEANS : GOOD_WHEAT;
    int diff = std::abs(mine[0] - mine[1]);
    std::vector<std::pair<float,uint32_t>> found;
    sources(other,found);
    int amount = std::min(ECONOMY_LOAD,found.empty() ? std::min(diff,pairs) : pairs);
    if (amount <= 0){
        return false;
    }
    sources(good,found);
    for (size_t t = 0; t < found.size() && t < 4; t++){
        uint32_t id = found[t].second;
        int plot = SourcePlot(id,k.pos);
        if (plot >= 0 && RouteTo(k,z,walkers,plot,SourcePoint(id,plot,k.pos))){
            k.store = id;
            k.carry_good = good;        //the kind he goes for; nothing carried yet
            k.carry = 0;
            k.stint = std::min(amount,GoodFree(id,good));
            k.site = 0;
            k.prop = -1;
            k.state = WORKER_TO_INPUT;
            return true;
        }
    }
    return false;
}

/*
    At his builders' hut (WORKER_TO_WORKSHOP arrived, or a provision made): wheat or beans he brought go into
    its stock; then, while provisions are wanted and it has a wheat and a beans, he makes one - taking them at
    the start - up to a load a round; else a load of what lies there for a store - provisions past the sites'
    share, and with none wanted its wheat and beans - goes to one that takes it; else home. His hut gone, or no
    longer his: what he holds to a store.
*/
void Economy::AtWorkshop(EconomyWorker& k, const ZoneState& z, Walkers& walkers, float pace){
    uint32_t h = k.building;
    if (k.job != WORKER_JOB_BUILDER || !Workshop(z,stands,h)){
        if (k.carry > 0 && FindStore(k,z,walkers)){
            return;
        }
        if (k.carry > 0){
            DropLoad(k,z);      //none takes it: put down (crates_plan.md, step 5)
        }
        GoHome(k,z,walkers);
        return;
    }
    std::array<int,GOOD_COUNT>& hut = state.stock[h];
    if (k.carry > 0 && k.carry_good >= 0){
        hut[k.carry_good] += k.carry;
        k.carry = 0;
        k.carry_good = -1;
        k.stint = 0;
        state.version++;
    }
    int spare = 0;
    int lacking = ProvisionsLacking(z,EconomySettlementOf(state,h),&spare);
    if (lacking > 0 && hut[GOOD_WHEAT] > 0 && hut[GOOD_BEANS] > 0 && k.stint < ECONOMY_LOAD){
        hut[GOOD_WHEAT]--;
        hut[GOOD_BEANS]--;
        k.state = WORKER_MAKING;
        EconomyStartTimer(k,SecondsToTicks(ECONOMY_PROVISION_SECONDS / pace));
        state.version++;
        return;
    }
    for (int g : {GOOD_PROVISIONS,GOOD_WHEAT,GOOD_BEANS}){
        int n = (g == GOOD_PROVISIONS) ? std::min(hut[g],spare) : (lacking <= 0) ? hut[g] : 0;
        if (n <= 0){
            continue;
        }
        k.carry = std::min(ECONOMY_LOAD,n);
        k.carry_good = g;
        hut[g] -= k.carry;
        if (FindStore(k,z,walkers)){
            state.version++;
            return;
        }
        hut[g] += k.carry;      //no store takes it: it stays at the hut
        k.carry = 0;
        k.carry_good = -1;
    }
    GoHome(k,z,walkers);
}

/*
    A ropemaker's or a sailmaker's errand from home (EconomyHempWork; goods_plan.md, steam_plan.md step 4), after a load
    of what his workshop makes to a store (FetchFromWorkplace): with the hemp for one at it and room in its pile, there
    to make it; else, with room left, a load of hemp - as much as makes what the pile still has room for, less what
    lies there - from the nearest store, camp's supplies, house nobody lives in or crate with some to spare.
    The others of his workshop count first - a sail loft's several (WORKERS BY PLOTS): what they are making and are
    on their way to make takes room in the pile, those on their way the hemp lying there, and what they are bringing
    the hemp still wanted. A ropewalk's one ropemaker has none. No count of what is wanted, unlike provisions: the
    pile is the bound. False if neither is to do, or he can get to none of it.
*/
bool Economy::MakeOfHemp(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    const ChasmWorld& w = *state.world;
    uint32_t h = k.building;
    const EconomyHempWork* work = EconomyHempWorkOf(k.job);
    if (!work || EconomyHempPace(z,h) <= 0.0f || h >= state.stock.size()){
        return false;
    }
    int going = 0;
    int coming = 0;
    for (const EconomyWorker& o : state.workers){
        if (&o == &k || o.building != h || o.job != k.job){
            continue;
        }
        going += (o.state == WORKER_TO_WORKSHOP && o.carry <= 0) ? 1 : 0;
        coming += (o.state == WORKER_TO_INPUT || o.state == WORKER_TAKING_INPUT) ? o.stint
                : (o.state == WORKER_TO_WORKSHOP) ? std::max(0,o.carry) : 0;
    }
    const std::array<int,GOOD_COUNT>& own = state.stock[h];
    int room = work->pile - own[work->made] - OthersMaking(k) - going;
    if (room <= 0){
        return false;
    }
    if (own[GOOD_HEMP] - going * work->hemp >= work->hemp){
        int plot = NearestPlot(h,k.pos);
        if (plot < 0 || !RouteTo(k,z,walkers,plot,w.grid->fine.pos[plot])){
            return false;
        }
        k.store = 0;
        k.site = 0;
        k.prop = -1;
        k.stint = 0;
        k.state = WORKER_TO_WORKSHOP;
        return true;
    }
    int want = std::min(ECONOMY_LOAD,room * work->hemp - own[GOOD_HEMP] - coming);
    if (want <= 0){
        return false;
    }
    //Where hemp can be had, by the straight line from him - as a builder looks for wheat and beans (MakeProvisions).
    std::vector<std::pair<float,uint32_t>> found;
    for (size_t id = 1; id < z.buildings.size() && id < stands.size(); id++){
        const ZoneBuilding& b = z.buildings[id];
        bool f_holds = b.kind == ZONE_KIND_STORE || b.kind == ZONE_KIND_CAMP
                       || (b.kind == ZONE_KIND_HOUSE && !PrivateStock(z,(uint32_t)id));
        int plot = (b.size > 0 && f_holds && stands[id] && GoodFree((uint32_t)id,GOOD_HEMP) > 0 && Ours(k,(uint32_t)id))
                   ? SourcePlot((uint32_t)id,k.pos) : -1;
        if (plot >= 0){
            found.push_back(std::make_pair((w.grid->fine.pos[plot] - k.pos).length(),(uint32_t)id));
        }
    }
    for (const EconomyCrate& c : state.crates){
        uint32_t id = ECONOMY_STORE_CRATE | c.id;
        if (GoodFree(id,GOOD_HEMP) > 0 && Ours(k,id)){
            found.push_back(std::make_pair((c.pos - k.pos).length(),id));
        }
    }
    std::sort(found.begin(),found.end());
    for (size_t t = 0; t < found.size() && t < 4; t++){
        uint32_t id = found[t].second;
        int plot = SourcePlot(id,k.pos);
        if (plot >= 0 && RouteTo(k,z,walkers,plot,SourcePoint(id,plot,k.pos))){
            k.store = id;
            k.carry_good = GOOD_HEMP;   //the kind he goes for; nothing carried yet
            k.carry = 0;
            k.stint = std::min(want,GoodFree(id,GOOD_HEMP));
            k.site = 0;
            k.prop = -1;
            k.state = WORKER_TO_INPUT;
            return true;
        }
    }
    return false;
}

/*
    At his ropewalk or sail loft (EconomyHempWork; WORKER_TO_WORKSHOP arrived, or one made): the hemp he brought into its
    stock; then, with the hemp for one there and room in the pile past what the others are making, one made - the hemp
    taken at the start - at his pace and the workshop's (EconomyHempPace), up to a load a round; else home. The workshop
    gone, too short now, or no longer his: what he holds to a store.
*/
void Economy::AtHempWorkshop(EconomyWorker& k, const ZoneState& z, Walkers& walkers, float pace){
    uint32_t h = k.building;
    const EconomyHempWork* work = EconomyHempWorkOf(k.job);
    float speed = work ? EconomyHempPace(z,h) : 0.0f;
    if (speed <= 0.0f || h >= state.stock.size()){
        if (k.carry > 0 && FindStore(k,z,walkers)){
            return;
        }
        if (k.carry > 0){
            DropLoad(k,z);      //none takes it: put down (crates_plan.md, step 5)
        }
        GoHome(k,z,walkers);
        return;
    }
    std::array<int,GOOD_COUNT>& own = state.stock[h];
    if (k.carry > 0 && k.carry_good >= 0){
        own[k.carry_good] += k.carry;
        k.carry = 0;
        k.carry_good = -1;
        k.stint = 0;
        state.version++;
    }
    if (own[GOOD_HEMP] >= work->hemp && own[work->made] + OthersMaking(k) < work->pile && k.stint < ECONOMY_LOAD){
        own[GOOD_HEMP] -= work->hemp;
        k.carry_good = work->made;      //what he is making (WORKER_MAKING) - nothing carried
        k.state = WORKER_MAKING;
        EconomyStartTimer(k,SecondsToTicks(work->seconds / (pace * speed)));
        state.version++;
        return;
    }
    GoHome(k,z,walkers);
}

int Economy::OthersMaking(const EconomyWorker& k) const{
    int n = 0;
    for (const EconomyWorker& o : state.workers){
        n += (&o != &k && o.building == k.building && o.job == k.job && o.state == WORKER_MAKING) ? 1 : 0;
    }
    return n;
}

/*
    A condenser's hand from home (steam_plan.md step 3), after a load of its water to a store (FetchFromWorkplace): with no
    steam collector beside it, its steam run out and room for more water, a load of steam from the nearest store, camp's
    supplies, house nobody lives in or crate with some to spare - as a ropemaker fetches hemp (MakeOfHemp) - unless another
    of its hands is on his way with some. Beside a collector he brings none: it waits on the collector, which is how the
    player sees a pairing that is wrong. False if there is nothing to do, or none he can get to.
*/
bool Economy::FeedCondenser(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    const ChasmWorld& w = *state.world;
    uint32_t h = k.building;
    if (k.job != WORKER_JOB_CONDENSER || !h || h >= z.buildings.size() || h >= state.stock.size() || h >= plots_of.size()){
        return false;
    }
    std::vector<uint32_t> feeds;
    EconomyCondenserFeeds(w,z,plots_of[h],feeds);
    const std::array<int,GOOD_COUNT>& own = state.stock[h];
    int hold = (h < state.water_room.size()) ? state.water_room[h] : ECONOMY_LOAD;
    if (!feeds.empty() || own[GOOD_STEAM] > 0 || own[GOOD_WATER] >= hold){
        return false;
    }
    for (const EconomyWorker& o : state.workers){
        if (&o != &k && o.building == h && o.job == WORKER_JOB_CONDENSER
            && (o.state == WORKER_TO_INPUT || o.state == WORKER_TAKING_INPUT || o.state == WORKER_TO_WORKSHOP)){
            return false;
        }
    }
    std::vector<std::pair<float,uint32_t>> found;
    for (size_t id = 1; id < z.buildings.size() && id < stands.size(); id++){
        const ZoneBuilding& b = z.buildings[id];
        bool f_holds = b.kind == ZONE_KIND_STORE || b.kind == ZONE_KIND_CAMP
                       || (b.kind == ZONE_KIND_HOUSE && !PrivateStock(z,(uint32_t)id));
        int plot = (b.size > 0 && f_holds && stands[id] && GoodFree((uint32_t)id,GOOD_STEAM) > 0 && Ours(k,(uint32_t)id))
                   ? SourcePlot((uint32_t)id,k.pos) : -1;
        if (plot >= 0){
            found.push_back(std::make_pair((w.grid->fine.pos[plot] - k.pos).length(),(uint32_t)id));
        }
    }
    for (const EconomyCrate& c : state.crates){
        uint32_t id = ECONOMY_STORE_CRATE | c.id;
        if (GoodFree(id,GOOD_STEAM) > 0 && Ours(k,id)){
            found.push_back(std::make_pair((c.pos - k.pos).length(),id));
        }
    }
    std::sort(found.begin(),found.end());
    for (size_t t = 0; t < found.size() && t < 4; t++){
        uint32_t id = found[t].second;
        int plot = SourcePlot(id,k.pos);
        if (plot >= 0 && RouteTo(k,z,walkers,plot,SourcePoint(id,plot,k.pos))){
            k.store = id;
            k.carry_good = GOOD_STEAM;  //the kind he goes for; nothing carried yet
            k.carry = 0;
            k.stint = std::min(ECONOMY_LOAD,GoodFree(id,GOOD_STEAM));
            k.site = 0;
            k.prop = -1;
            k.state = WORKER_TO_INPUT;
            return true;
        }
    }
    return false;
}

/*
    At his condenser with the steam he fetched (WORKER_TO_WORKSHOP arrived): into its stock, to be condensed in the tick,
    and home. The condenser gone, or no longer his: what he holds to a store.
*/
void Economy::AtCondenser(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    uint32_t h = k.building;
    bool f_there = k.job == WORKER_JOB_CONDENSER && h && h < z.buildings.size() && h < state.stock.size()
                   && z.buildings[h].kind == ZONE_KIND_CONDENSER && z.buildings[h].size > 0;
    if (!f_there){
        if (k.carry > 0 && FindStore(k,z,walkers)){
            return;
        }
        if (k.carry > 0){
            DropLoad(k,z);      //none takes it: put down (crates_plan.md, step 5)
        }
        GoHome(k,z,walkers);
        return;
    }
    if (k.carry > 0 && k.carry_good >= 0){
        state.stock[h][k.carry_good] += k.carry;
        k.carry = 0;
        k.carry_good = -1;
        k.stint = 0;
        state.version++;
    }
    GoHome(k,z,walkers);
}

//--- Saves -----------------------------------------------------------------------------------------

EconomySaved Economy::Save() const{
    return EconomySaveState(state);
}

EconomySaved EconomySaveState(const EconomyState& state){
    EconomySaved s;
    for (size_t id = 1; id < state.stock.size(); id++){
        if (CarryTotal(state.stock[id]) > 0){
            s.stocks.push_back(std::make_pair((uint32_t)id,state.stock[id]));
        }
    }
    for (size_t id = 1; id < state.pantry.size(); id++){
        if (CarryTotal(state.pantry[id]) > 0){
            s.pantries.push_back(std::make_pair((uint32_t)id,state.pantry[id]));
        }
    }
    for (size_t i = 0; i < state.prop_state.size(); i++){
        if (state.prop_state[i] != PROP_STATE_STANDING){
            s.props.push_back(std::make_pair((int)i,(int)state.prop_state[i]));
        }
    }
    for (size_t d = 0; d < state.dug.size(); d++){
        if (state.dug[d] > 0){
            s.dug.push_back(std::make_pair((int)d,state.dug[d]));
        }
    }
    //FORAGING: the plants that are not ripe and full, as a new map's all are.
    if (state.world && state.world->forest){
        const std::vector<PropInstance>& props = state.world->forest->props;
        for (size_t i = 0; i < state.fruit.size() && i < props.size(); i++){
            int full = EconomyFruitFull(props[i]);
            int regrow = (i < state.regrow.size()) ? state.regrow[i] : 0;
            if (full > 0 && (state.fruit[i] != full || regrow != 0)){
                s.plants.push_back(std::array<int,3>{(int)i,(int)state.fruit[i],regrow});
            }
        }
    }
    s.workers = state.workers;
    s.fields = state.fields;
    for (size_t id = 1; id < state.site.size(); id++){
        if (std::any_of(state.site[id].begin(),state.site[id].end(),[](int x){ return x > 0; })){
            s.sites.push_back(std::make_pair((uint32_t)id,state.site[id]));
        }
    }
    for (size_t v = 0; v < state.ground_site.size(); v++){
        if (state.ground_site[v] > 0){
            s.ground_sites.push_back(std::make_pair((int)v,state.ground_site[v]));
        }
    }
    for (size_t id = 0; id < state.heat.size(); id++){
        if (state.heat[id] != HEAT_NONE){
            s.heats.push_back(std::make_pair((uint32_t)id,(int)state.heat[id]));
        }
    }
    s.crates = state.crates;
    s.next_crate = state.next_crate;
    for (size_t id = 1; id < state.guards.size(); id++){
        if (state.guards[id] >= 0){
            s.guards.push_back(std::make_pair((uint32_t)id,state.guards[id]));
        }
    }
    for (size_t id = 1; id < state.zeppelin.size(); id++){
        if (state.zeppelin[id] > 0){
            s.zeppelins.push_back(std::make_pair((uint32_t)id,state.zeppelin[id]));
        }
    }
    for (size_t id = 1; id < state.endless.size(); id++){
        if (state.endless[id]){
            s.endless.push_back((uint32_t)id);
        }
    }
    //STOCK KEPT: every made good's number, so a save keeps a 0 the start would make something else; orders running.
    //By colony from 1 - 0 is none - each one that has set any.
    for (size_t c = 1; c < state.kept.size(); c++){
        for (int g = 0; g < GOOD_COUNT; g++){
            if (GOODS_MADE & GOOD_BIT(g)){
                s.kept.push_back({(int)c,g,state.kept[c][g]});
            }
        }
    }
    for (size_t c = 1; c < state.ordered.size(); c++){
        for (int g = 0; g < GOOD_COUNT; g++){
            if (state.ordered[c][g] > 0){
                s.ordered.push_back({(int)c,g,state.ordered[c][g]});
            }
        }
    }
    //ITS COLONY NUMBER, by the homes carrying one, and the next to be given.
    for (size_t id = 1; id < state.colony.size(); id++){
        if (state.colony[id]){
            s.colonies.push_back(std::make_pair((uint32_t)id,state.colony[id]));
        }
    }
    s.next_colony = state.next_colony;
    s.next_person = state.next_person;
    s.dead = state.dead;
    s.zombies = state.zombies;
    s.harm = state.harm;
    s.fallen = state.fallen;
    s.next_zombie = state.next_zombie;
    s.arrows = state.arrows;
    s.history = state.history;
    s.f_history = true;
    return s;
}

/*
    Put back exactly as saved - the workers on their routes, not planned again (a re-plan could find
    another route of the same cost, and a replay has to be the same walk). Then what follows from the
    zones is worked out, with no stock shared out: the save's already is.
*/
void Economy::Restore(std::shared_ptr<const ChasmWorld> world, const EconomySaved& saved, const ZoneState& z,
                      Walkers& walkers, uint64_t calendar_tick){
    Reset(world);
    state.tick = calendar_tick;
    now = calendar_tick;
    if (!world || z.world != world){
        return;
    }
    state.stock.assign(z.buildings.size(),std::array<int,GOOD_COUNT>{});
    for (const auto& st : saved.stocks){
        if (st.first < state.stock.size()){
            state.stock[st.first] = st.second;
        }
    }
    state.pantry.assign(z.buildings.size(),std::array<int,GOOD_COUNT>{});
    for (const auto& st : saved.pantries){
        if (st.first < state.pantry.size()){
            state.pantry[st.first] = st.second;
        }
    }
    for (const auto& dg : saved.dug){
        if (dg.first >= 0 && dg.first < (int)state.dug.size()){
            state.dug[dg.first] = dg.second;
        }
    }
    for (const auto& pr : saved.props){
        if (pr.first >= 0 && pr.first < (int)state.prop_state.size()){
            state.prop_state[pr.first] = (uint8_t)pr.second;
        }
    }
    //FORAGING: the plants as saved - the rest ripe and full, as Reset left them. A save from before has none.
    for (const std::array<int,3>& pl : saved.plants){
        if (pl[0] >= 0 && pl[0] < (int)state.fruit.size() && EconomyFruitFull(world->forest->props[pl[0]]) > 0){
            state.fruit[pl[0]] = (uint8_t)std::max(0,std::min(255,pl[1]));
            state.regrow[pl[0]] = (uint8_t)std::max(0,std::min(255,pl[2]));
        }
    }
    state.fruit_version++;
    /*
        The people as saved. A save from before P4 has only stand-in workers, with no person ids: they
        are dropped, and the colony starts with its settlers at the camp, as a new one does.
    */
    bool f_people = !saved.workers.empty();
    for (const EconomyWorker& k : saved.workers){
        f_people = f_people && k.id != 0;
    }
    if (f_people){
        state.workers = saved.workers;
        state.next_person = saved.next_person;
        state.dead = saved.dead;
        //A save from before site costs per good: a carrier on his way to fetch named no good - it was wood.
        for (EconomyWorker& k : state.workers){
            if ((k.state == WORKER_TO_FETCH || k.state == WORKER_FETCHING) && k.carry_good < 0){
                k.carry_good = GOOD_WOOD;
            }
        }
    }
    state.fields = saved.fields;
    state.site.assign(z.buildings.size(),std::array<int,GOOD_COUNT>{});
    for (const auto& st : saved.sites){
        if (st.first < state.site.size()){
            state.site[st.first] = st.second;
        }
    }
    state.ground_site.assign(z.ground.size(),0);
    for (const auto& st : saved.ground_sites){
        if (st.first >= 0 && st.first < (int)state.ground_site.size()){
            state.ground_site[st.first] = st.second;
        }
    }
    //A saved colony has had its supplies - unless it was saved before its first tick, as a recording's start can be
    //when a new game begins: that first tick is still to come, with its supplies, rubble and history. Its calendar
    //is still at 0, and its journal has no line yet: the first tick writes the landing.
    f_new_colony = calendar_tick == 0 && saved.history.journal.empty();
    state.heat.assign(std::max<size_t>(z.buildings.size(),1),HEAT_NONE);
    for (const auto& h : saved.heats){
        if (h.first < state.heat.size()){
            state.heat[h.first] = (uint8_t)h.second;
        }
    }
    //GUARD COUNTS (step 8) as set. A save from before them, with towers, has none: its towers keep every place, as
    //they had (-2, which Derive reads).
    state.zeppelin.assign(z.buildings.size(),0);
    for (const auto& zp : saved.zeppelins){
        if (zp.first < state.zeppelin.size()){
            state.zeppelin[zp.first] = std::max(0,std::min(ZEPPELIN_STAGES,zp.second));
        }
    }
    state.guards.assign(z.buildings.size(),-1);
    for (const auto& gc : saved.guards){
        if (gc.first < state.guards.size()){
            state.guards[gc.first] = gc.second;
        }
    }
    for (size_t id = 1; saved.guards.empty() && id < z.buildings.size(); id++){
        if (z.buildings[id].kind == ZONE_KIND_TOWER && z.buildings[id].size > 0){
            state.guards[id] = -2;
        }
    }
    state.endless.assign(z.buildings.size(),0);
    for (uint32_t id : saved.endless){
        if (id < state.endless.size() && z.buildings[id].kind == ZONE_KIND_WELL){
            state.endless[id] = 1;
        }
    }
    //STOCK KEPT as set, by colony - a good the save does not name keeps the start's - and the orders running.
    for (const auto& kc : saved.kept){
        if (kc[0] >= 0 && kc[1] >= 0 && kc[1] < GOOD_COUNT && (GOODS_MADE & GOOD_BIT(kc[1]))){
            KeptOf((uint32_t)kc[0])[kc[1]] = std::max(0,kc[2]);
        }
    }
    for (const auto& oc : saved.ordered){
        if (oc[0] >= 0 && oc[1] >= 0 && oc[1] < GOOD_COUNT && (GOODS_MADE & GOOD_BIT(oc[1]))){
            OrderedOf((uint32_t)oc[0])[oc[1]] = std::max(0,oc[2]);
        }
    }
    //ITS COLONY NUMBER as the homes carried it: Derive's Settle, below, goes on from there. A save from before has
    //none, and the settlements standing are numbered afresh in the order founded - as COLONY N was then.
    state.colony.assign(z.buildings.size(),0);
    state.next_colony = std::max(1u,saved.next_colony);
    for (const auto& cc : saved.colonies){
        if (cc.first < state.colony.size() && cc.second > 0){
            state.colony[cc.first] = cc.second;
            state.next_colony = std::max(state.next_colony,cc.second + 1);
        }
    }
    //CRATES as they lay. A save from before them has none.
    state.crates = saved.crates;
    state.next_crate = saved.next_crate;
    for (const EconomyCrate& c : state.crates){
        state.next_crate = std::max(state.next_crate,c.id + 1);
    }
    state.crates_version++;
    //ZOMBIES on their routes, as the people are. A save from before them has none.
    state.zombies = saved.zombies;
    state.harm = saved.harm;
    state.fallen = saved.fallen;    //THE FALLEN lying where they fell, as long as they had left
    state.next_zombie = saved.next_zombie;
    for (const EconomyZombie& m : state.zombies){
        state.next_zombie = std::max(state.next_zombie,m.id + 1);
    }
    for (const EconomyFallen& f : state.fallen){
        state.next_zombie = f.f_person ? state.next_zombie : std::max(state.next_zombie,f.id + 1);
    }
    state.arrows = saved.arrows;    //ARROWS in the air and come down, as they were
    Derive(z,walkers,false);
    //A colony saved before its first tick (f_new_colony above): that tick follows the zones as a new colony's does - it
    //is where the supplies land (FollowZones) - so it runs as the first tick of the game it was saved from did.
    if (f_new_colony){
        state.zones_version = 0xFFFFFFFFu;
    }
    /*
        Land never settled (land_plan.md): a save from before land - its fields have no owner, its farmers work
        a field. The first tick settles it. Only such a save: a field with no owner is otherwise one nobody can
        reach, and settling a save of today's again would part a replay from the run it was cut from.
    */
    f_resettle = saved.f_before_land;
    for (const EconomyWorker& k : state.workers){
        f_resettle = f_resettle || (k.job == WORKER_JOB_FARMER
                                    && (k.building >= z.buildings.size() || z.buildings[k.building].kind == ZONE_KIND_FIELD));
    }
    //THE HISTORY as saved - today's row too, so a load mid-day counts on from where it was. A save from before it has
    //none: it begins here, with what the colony is now.
    if (saved.f_history){
        state.history = saved.history;
    }else{
        BeginHistory(calendar_tick);
    }
    state.felled_version++;
    state.version++;
}

//--- Questions -------------------------------------------------------------------------------------

GoodsMask EconomyAccepts(const EconomyState& e, uint32_t id){
    return (id < e.accepts.size()) ? e.accepts[id] : 0;
}

int EconomyStock(const EconomyState& e, uint32_t id, int good){
    return (id < e.stock.size() && good >= 0 && good < GOOD_COUNT) ? e.stock[id][good] : 0;
}

int EconomyStockTotal(const EconomyState& e, uint32_t id){
    return (id < e.stock.size()) ? CarryTotal(e.stock[id]) : 0;
}

bool EconomyPrivateStock(const EconomyState& e, const ZoneState& z, uint32_t id){
    return id && id < z.buildings.size() && z.buildings[id].kind == ZONE_KIND_HOUSE && Living(e.workers,id) > 0;
}

std::array<int,GOOD_COUNT> EconomyReserved(const EconomyState& e, uint32_t id){
    std::array<int,GOOD_COUNT> r{};
    for (const EconomyWorker& o : e.workers){
        if (o.store == id && o.carry_good >= 0 && o.carry_good < GOOD_COUNT){
            r[o.carry_good] += SiteReservation(o,id,o.carry_good) + HomeReservation(o,id,o.carry_good);
        }
    }
    return r;
}

std::array<int,GOOD_COUNT> EconomyStoredTotals(const EconomyState& e, const ZoneState& z, uint32_t settlement){
    std::array<int,GOOD_COUNT> t{};
    for (size_t id = 1; id < e.stock.size() && id < z.buildings.size(); id++){
        if (settlement != ECONOMY_EVERY_SETTLEMENT && EconomySettlementOf(e,(uint32_t)id) != settlement){
            continue;
        }
        //Stores, and the camp's supplies - what the colony has to hand.
        if (z.buildings[id].kind == ZONE_KIND_STORE || z.buildings[id].kind == ZONE_KIND_CAMP){
            for (int g = 0; g < GOOD_COUNT; g++){
                t[g] += e.stock[id][g];
            }
        }
    }
    return t;
}

int EconomyCropYield(int last, int crop){
    if (crop < 0 || crop >= ZONE_CROP_COUNT){
        return 0;
    }
    return (last < 0 || last >= ZONE_CROP_COUNT) ? 100 : crop_after[last][crop];
}

namespace {

/*
    What AUTO ranks crops by (fields_plan.md, "AUTO by food", the user's 2026-10-08): the health a crop
    gives per day it grows, sown after `last` - its yield per area x the rotation's percent x what a unit
    is worth eaten, over its growing days. By percent alone it never sowed beans, the most food there is:
    they tied with wheat or greens at 100% and lost on table order. Area and pace are the same for
    every crop, so they are left out.
*/
double CropFood(int last, int crop){
    return (double)crop_yield[crop] * (double)EconomyCropYield(last,crop)
           * (double)EconomyFoodWorth(GoodOfCrop(crop)) / (double)crop_days[crop];
}

//AUTO sows food only (goods_plan.md): hemp is grown where the player asks for it, never by default - not even
//as the one crop that still ripens before the frost, where its food of 0 would otherwise be the best there is.
bool CropIsFood(int crop){
    return GoodIsFood(GoodOfCrop(crop));
}

}

int EconomyAutoCrop(int last){
    int best = 0;
    for (int c = 1; c < ZONE_CROP_COUNT; c++){
        if (CropIsFood(c) && CropFood(last,c) > CropFood(last,best)){
            best = c;
        }
    }
    return best;
}

int EconomyFieldSowCrop(const EconomyField& f, const ZoneBuilding& b){
    if (f.last < 0){
        //Its first: the player's choice - AUTO too, his or the countdown's (issues_plan.md) - or nothing yet.
        return (b.crop < ZONE_CROP_COUNT) ? (int)b.crop : (b.crop == ZONE_CROP_AUTO) ? EconomyAutoCrop(-1) : -1;
    }
    return (b.next_crop < ZONE_CROP_COUNT) ? (int)b.next_crop : EconomyAutoCrop(f.last);
}

int EconomyGrowingTicksLeft(const ChasmWorld& w, const vec2& p, uint64_t tick){
    CalendarDate d = CalendarDateOf(tick);
    if (d.season == SEASON_WINTER || UnderSnow(w,p,tick)){
        return 0;
    }
    //This year's winter - and before it, the snow, where it comes early: looked for a quarter day at a time,
    //as the front only ever reaches a place over autumn's last days.
    const uint64_t year = (uint64_t)CALENDAR_SEASONS * CALENDAR_SEASON_TICKS;
    uint64_t end = (tick / year) * year + (uint64_t)SEASON_WINTER * CALENDAR_SEASON_TICKS;
    const uint64_t step = CALENDAR_DAY_TICKS / 4;
    for (uint64_t t = tick + step; t < end; t += step){
        if (UnderSnow(w,p,t)){
            end = t;
            break;
        }
    }
    return (int)(end - tick);
}

int EconomyGrowingSeasonTicks(const ChasmWorld& w, const vec2& p, uint64_t tick){
    const uint64_t year = (uint64_t)CALENDAR_SEASONS * CALENDAR_SEASON_TICKS;
    uint64_t t = (tick / year + 1) * year;      //next spring's first tick
    const uint64_t thaw = t + CALENDAR_SEASON_TICKS;
    while (t < thaw && UnderSnow(w,p,t)){
        t += CALENDAR_DAY_TICKS / 4;
    }
    return EconomyGrowingTicksLeft(w,p,t);
}

int EconomyRipenTicks(int crop, int grown, float pace){
    int per = (int)std::lround(pace * 100.0f);      //what a tick grows, as the fields' loop adds it
    if (crop < 0 || crop >= ZONE_CROP_COUNT || per <= 0){
        return 1 << 30;
    }
    int need = std::max(0,crop_days[crop] * CALENDAR_DAY_TICKS * 100 - grown);
    return (need + per - 1) / per;
}

int EconomyFieldSowNow(const EconomyField& f, const ZoneBuilding& b, float pace, int ticks_left){
    auto ripens = [&](int c){
        return EconomyRipenTicks(c,0,pace) <= ticks_left;
    };
    bool f_auto = (f.last >= 0) ? b.next_crop >= ZONE_CROP_COUNT : b.crop == ZONE_CROP_AUTO;
    if (f_auto){
        //AUTO: the most food of the food crops still in time, the earlier crop on a tie.
        int best = -1;
        for (int c = 0; c < ZONE_CROP_COUNT; c++){
            if (CropIsFood(c) && ripens(c) && (best < 0 || CropFood(f.last,c) > CropFood(f.last,best))){
                best = c;
            }
        }
        return best;
    }
    int crop = EconomyFieldSowCrop(f,b);
    return (crop >= 0 && ripens(crop)) ? crop : -1;
}

float EconomyFieldPace(const EconomyState& e, const ZoneState& z, uint32_t id){
    uint32_t farmer = (id < e.farmer_of.size()) ? e.farmer_of[id] : 0;
    for (const EconomyWorker& k : e.workers){
        if (farmer && k.id == farmer){
            //Posted: it waits (Tending). Crowded at home, slower (building_size_plan.md).
            return (k.post >= 0) ? 0.0f : EconomySkillFactor(k.skills[SKILL_HUSBANDRY]) * EconomyPersonCrowdedPace(e,z,k);
        }
    }
    return 0.0f;
}

const EconomyField* EconomyFieldOf(const EconomyState& e, uint32_t id){
    for (const EconomyField& f : e.fields){
        if (f.building == id){
            return &f;
        }
    }
    return NULL;
}

float EconomyFieldGrowth(const EconomyState& e, const ZoneState& z, uint32_t id){
    (void)z;
    for (const EconomyField& f : e.fields){
        if (f.building == id){
            if (!f.f_sown || f.crop < 0 || f.crop >= ZONE_CROP_COUNT){
                return 0.0f;    //empty
            }
            return std::min(1.0f,(float)f.grown / ((float)crop_days[f.crop] * CALENDAR_DAY_TICKS * 100.0f));
        }
    }
    return 0.0f;
}
