#include "ChasmStats.h"
#include "Calendar.h"
#include "ChasmWorld.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <stdio.h>
#include <string.h>

void ChasmStatsDays(const EconomyDay& row, float& food, float& water){
    //Everyone at their SHARE (Economy.h), as ChasmDaysHeld counts them: a baby half an adult, a child three quarters.
    int mouths = row.people[LIFE_BABY] * LIFE_SHARE_BABY + row.people[LIFE_CHILD] * LIFE_SHARE_CHILD
                 + (row.people[LIFE_YOUNG] + row.people[LIFE_ADULT] + row.people[LIFE_OLD]) * LIFE_SHARE_FULL;
    //Nobody to feed - a lost colony (demo_plan.md step 7) - has no days of anything: not the whole store for one.
    if (mouths == 0){
        food = 0.0f;
        water = 0.0f;
        return;
    }
    float people_days = (float)std::max(1,mouths) / LIFE_SHARE_FULL * CALENDAR_DAY_TICKS;
    int64_t food_ticks = 0;
    for (int g = 0; g < GOOD_COUNT; g++){
        food_ticks += (GOODS_FOOD & GOOD_BIT(g)) ? (int64_t)row.held[g] * EconomyFoodWorth(g) : 0;
    }
    food = (float)food_ticks / people_days;
    water = (float)((int64_t)row.held[GOOD_WATER] * NEEDS_WATER_WORTH) / people_days;
}

ChasmStatsTotals ChasmStatsSum(const EconomyHistory& h){
    ChasmStatsTotals t;
    for (const EconomyEvent& ev : h.journal){
        switch (ev.kind){
            case EVENT_BORN:
                t.born++;
                break;
            case EVENT_DIED:
                t.died++;
                if (ev.amount >= 0 && ev.amount < CHASM_STATS_CAUSES){
                    t.died_of[ev.amount]++;
                }
                break;
            case EVENT_HARVEST:
                t.harvests++;
                for (int g = 0; g < GOOD_COUNT; g++){
                    t.harvested[g] += (ev.what & GOOD_BIT(g)) ? ev.amount : 0;
                }
                break;
            case EVENT_BUILDING_FINISHED:
                t.finished++;
                if (ev.amount >= 0 && ev.amount < ZONE_KIND_COUNT){
                    t.finished_kind[ev.amount]++;
                }
                break;
            case EVENT_SETTLED:
                t.settled++;
                break;
            case EVENT_SEAM_SEEN:
                t.seams++;
                break;
        }
    }
    auto add = [&](const EconomyDay& d){
        t.felled += d.felled;
        t.broken += d.broken;
        t.storeys += d.storeys;
        t.zombies += d.zombies;
        for (int g = 0; g < GOOD_COUNT; g++){
            t.rotted[g] += d.rotted[g];
        }
    };
    for (const EconomyDay& d : h.days){
        add(d);
    }
    add(h.today);
    return t;
}

std::vector<ChasmStatsSeason> ChasmStatsSeasons(const EconomyHistory& h){
    std::vector<ChasmStatsSeason> out;
    int first = h.start.day / CALENDAR_DAYS_PER_SEASON;
    int last = std::max(first,h.today.day / CALENDAR_DAYS_PER_SEASON);
    for (int s = first; s <= last; s++){
        ChasmStatsSeason one;
        one.season = s;
        out.push_back(one);
    }
    //A date set back by hand puts a row or event before the first season: it is left out of the bars, not the totals.
    auto at = [&](int day) -> ChasmStatsSeason* {
        int s = day / CALENDAR_DAYS_PER_SEASON - first;
        return (s >= 0 && s < (int)out.size()) ? &out[s] : NULL;
    };
    for (const EconomyEvent& ev : h.journal){
        ChasmStatsSeason* s = at((int)(ev.tick / CALENDAR_DAY_TICKS));
        if (!s){
            continue;
        }
        s->born += (ev.kind == EVENT_BORN) ? 1 : 0;
        s->died += (ev.kind == EVENT_DIED) ? 1 : 0;
        s->harvested += (ev.kind == EVENT_HARVEST) ? ev.amount : 0;
    }
    for (const EconomyDay& d : h.days){
        if (ChasmStatsSeason* s = at(d.day)){
            s->felled += d.felled;
        }
    }
    if (ChasmStatsSeason* s = at(h.today.day)){
        s->felled += h.today.felled;
    }
    return out;
}

ChasmStatsNow ChasmStatsNowOf(const EconomyState& e, const ZoneState& z, uint32_t colony){
    ChasmStatsNow n;
    EconomyDayLevels(e,e.tick,n.row);
    //BY COLONY: its own row of the levels - none, all nothing - and its own people, buildings and dead below.
    if (colony){
        EconomyDay mine;
        for (const EconomyDay& r : n.row.colonies){
            mine = (r.colony == colony) ? r : mine;
        }
        mine.day = n.row.day;
        n.row = mine;
    }
    auto ours = [&](uint32_t id){ return !colony || EconomyColonyNumber(e,EconomySettlementOf(e,id)) == colony; };
    ChasmStatsDays(n.row,n.food_days,n.water_days);
    std::vector<int> living(z.buildings.size(),0);
    for (const EconomyWorker& k : e.workers){
        if (colony && EconomyColonyNumber(e,EconomyPersonLand(e,k)) != colony){
            continue;
        }
        n.people++;
        n.expecting += k.due ? 1 : 0;
        n.in_camp += (k.house == 0 || (k.house < z.buildings.size() && z.buildings[k.house].kind == ZONE_KIND_CAMP)) ? 1 : 0;
        if (k.house < living.size()){
            living[k.house]++;
        }
    }
    //What stands and what is still to build, by building, in one pass over the plots and the cells.
    std::vector<uint8_t> stands(z.buildings.size(),0);
    std::vector<uint8_t> site(z.buildings.size(),0);
    std::vector<uint8_t> mine(z.buildings.size(),0);
    //Never a swamp hut (ZoneKindWild): the swamp's, whoever's land it stands on.
    for (size_t id = 1; id < z.buildings.size(); id++){
        mine[id] = (ours((uint32_t)id) && !ZoneKindWild(z.buildings[id].kind)) ? 1 : 0;
    }
    for (size_t v = 0; v < z.building.size(); v++){
        uint32_t id = z.building[v];
        if (!id || id >= z.buildings.size() || !mine[id]){
            continue;
        }
        stands[id] |= (z.standing[v] > 0) ? 1 : 0;
        site[id] |= (z.storeys[v] > z.standing[v]) ? 1 : 0;
        n.tents += (z.buildings[id].kind == ZONE_KIND_CAMP && z.standing[v] > 0) ? 1 : 0;
    }
    for (size_t c = 0; c < z.field.size(); c++){
        uint32_t id = z.field[c];
        if (!id || id >= z.buildings.size() || !mine[id]){
            continue;
        }
        stands[id] |= z.field_built[c] ? 1 : 0;
        site[id] |= z.field_built[c] ? 0 : 1;
    }
    for (size_t id = 1; id < z.buildings.size(); id++){
        int kind = z.buildings[id].kind;
        if (z.buildings[id].size <= 0 || kind <= ZONE_KIND_NONE || kind >= ZONE_KIND_COUNT || !mine[id]){
            continue;
        }
        if (site[id]){
            n.building[kind]++;
        }else if (stands[id]){
            n.standing[kind]++;
        }
        n.houses += (kind == ZONE_KIND_HOUSE && stands[id] && living[id] > 0) ? 1 : 0;
    }
    //The dead: every one the colony remembers - a colony's, the deaths its journal has, since its rows began.
    if (colony){
        for (const EconomyEvent& ev : e.history.journal){
            if (ev.kind == EVENT_DIED && ev.colony == colony){
                n.dead++;
                if (ev.amount >= 0 && ev.amount < CHASM_STATS_CAUSES){
                    n.dead_of[ev.amount]++;
                }
            }
        }
    }else{
        for (const EconomyDeath& d : e.dead){
            n.dead++;
            if (d.cause >= 0 && d.cause < CHASM_STATS_CAUSES){
                n.dead_of[d.cause]++;
            }
        }
    }
    for (const EconomySettlement& st : e.settlements){
        ChasmStatsSettlement one;
        one.id = st.id;
        one.colony = st.colony;
        one.beds = st.beds;
        for (const EconomyWorker& k : e.workers){
            one.people += (EconomyPersonSettlement(e,k) == st.id) ? 1 : 0;
        }
        n.beds += (!colony || st.colony == colony) ? st.beds : 0;
        n.settlements.push_back(one);
    }
    return n;
}

EconomyHistory ChasmColonyHistory(const EconomyHistory& h, uint32_t colony){
    EconomyHistory c;
    auto row_of = [&](const EconomyDay& d, EconomyDay& out){
        for (const EconomyDay& r : d.colonies){
            if (r.colony == colony){
                out = r;
                out.day = d.day;
                return true;
            }
        }
        return false;
    };
    EconomyDay r;
    bool f_started = row_of(h.start,r);
    if (f_started){
        c.start = r;
        c.from = h.from;
    }
    for (const EconomyDay& d : h.days){
        if (!row_of(d,r)){
            continue;
        }
        //Founded on that day, or kept by colony only since: its first row's levels where it begins, its counts not.
        if (!f_started){
            c.start = r;
            c.start.felled = c.start.broken = c.start.storeys = c.start.zombies = 0;
            c.start.rotted.fill(0);
            c.from = (uint64_t)std::max(0,d.day) * CALENDAR_DAY_TICKS;
            f_started = true;
        }
        c.days.push_back(r);
    }
    if (!row_of(h.today,c.today)){
        c.today = EconomyDay();
    }
    c.today.day = h.today.day;
    if (!f_started){
        c.start.day = h.today.day;
        c.from = (uint64_t)std::max(0,h.today.day) * CALENDAR_DAY_TICKS;
    }
    for (const EconomyEvent& ev : h.journal){
        if (ev.colony == colony){
            c.journal.push_back(ev);
        }
    }
    return c;
}

std::vector<uint32_t> ChasmStatsColonies(const EconomyState& e){
    std::vector<uint32_t> out;
    auto add = [&](uint32_t colony){
        if (colony && std::find(out.begin(),out.end(),colony) == out.end()){
            out.push_back(colony);
        }
    };
    for (const EconomySettlement& st : e.settlements){
        add(st.colony);
    }
    const EconomyHistory& h = e.history;
    for (const EconomyDay* d : {&h.start,&h.today}){
        for (const EconomyDay& r : d->colonies){
            add(r.colony);
        }
    }
    for (const EconomyDay& d : h.days){
        for (const EconomyDay& r : d.colonies){
            add(r.colony);
        }
    }
    std::sort(out.begin(),out.end());
    return out;
}

std::vector<std::string> ChasmStatsCheck(const EconomyState& e){
    std::vector<std::string> out;
    const EconomyHistory& h = e.history;
    char buf[160];
    std::map<int,int> born_on;
    std::map<int,int> died_on;
    std::map<int,int> put_down_on;      //people the debug tools put down: neither born nor settled, but here
    int born = 0;
    int died = 0;
    for (const EconomyEvent& ev : h.journal){
        int day = (int)(ev.tick / CALENDAR_DAY_TICKS);
        if (ev.kind == EVENT_BORN){
            born++;
            born_on[day]++;
        }else if (ev.kind == EVENT_DIED){
            died++;
            died_on[day]++;
        }else if (ev.kind == EVENT_PUT_DOWN){
            put_down_on[day]++;
        }
    }
    //The dead since it began, and everyone born in the colony since: a settler's own parents never came, and the
    //children the settlers brought were born before it.
    int dead_since = 0;
    int born_since = 0;
    for (const EconomyDeath& d : e.dead){
        dead_since += (d.tick >= h.from) ? 1 : 0;
        born_since += (d.mother && d.born >= (int64_t)h.from) ? 1 : 0;
    }
    for (const EconomyWorker& k : e.workers){
        born_since += (k.mother && k.born >= (int64_t)h.from) ? 1 : 0;
    }
    if (died != dead_since){
        snprintf(buf,sizeof(buf),"deaths: the journal has %i, the colony's dead since it began %i",died,dead_since);
        out.push_back(buf);
    }
    if (born != born_since){
        snprintf(buf,sizeof(buf),"births: the journal has %i, the people born in the colony since it began %i",born,born_since);
        out.push_back(buf);
    }
    int felled = 0;
    int broken = 0;
    EconomyForestCleared(e,felled,broken);
    ChasmStatsTotals t = ChasmStatsSum(h);
    if (h.felled_from + t.felled != felled){
        snprintf(buf,sizeof(buf),"trees: %i down when it began and %i felled since, but %i are down",h.felled_from,t.felled,felled);
        out.push_back(buf);
    }
    if (h.broken_from + t.broken != broken){
        snprintf(buf,sizeof(buf),"rocks: %i broken when it began and %i since, but %i are broken",h.broken_from,t.broken,broken);
        out.push_back(buf);
    }
    int kills = 0;
    for (const EconomyWorker& k : e.workers){
        kills += EconomyKills(k.kills);
    }
    for (const EconomyDeath& d : e.dead){
        kills += EconomyKills(d.kills);
    }
    if (h.kills_from + t.zombies != kills){
        snprintf(buf,sizeof(buf),"zombies: %i kills put down with people and %i killed, but the living and the dead have %i",
                 h.kills_from,t.zombies,kills);
        out.push_back(buf);
    }
    //Each row on from the one before - the start's first - and the colony now on from the last.
    auto heads = [](const EconomyDay& d){
        int n = 0;
        for (int p : d.people){
            n += p;
        }
        return n;
    };
    auto follows = [&](const EconomyDay& before, bool f_start, const EconomyDay& after, const char* what){
        bool f_next = f_start ? (after.day == before.day) : (after.day == before.day + 1);
        if (!f_next){
            return;     //the date was set over a gap: the days between were never lived
        }
        int expected = heads(before) + born_on[after.day] + put_down_on[after.day] - died_on[after.day];
        if (heads(after) != expected){
            snprintf(buf,sizeof(buf),"%s day %i: %i people, but the day before's %i, %i born, %i put down and %i dead make %i",
                     what,after.day,heads(after),heads(before),born_on[after.day],put_down_on[after.day],died_on[after.day],
                     expected);
            out.push_back(buf);
        }
    };
    const EconomyDay* before = &h.start;
    bool f_start = true;
    for (const EconomyDay& d : h.days){
        follows(*before,f_start,d,"row");
        before = &d;
        f_start = false;
    }
    EconomyDay now = h.today;
    EconomyDayLevels(e,e.tick,now);
    follows(*before,f_start,now,"today,");
    /*
        BY COLONY (settlements_plan.md, step 4): each row's colonies and nobody's add up to it - its people and goods,
        where it has levels, and its counts. A row from before has none to add.
    */
    auto adds_up = [&](const EconomyDay& d, bool f_levels, const char* what){
        if (d.colonies.empty()){
            return;
        }
        EconomyDay sum;
        for (const EconomyDay& r : d.colonies){
            for (int s = 0; s < ECONOMY_STAGES; s++){
                sum.people[s] += r.people[s];
            }
            for (int g = 0; g < GOOD_COUNT; g++){
                sum.held[g] += r.held[g];
            }
            sum.felled += r.felled;
            sum.broken += r.broken;
            sum.storeys += r.storeys;
            sum.zombies += r.zombies;
            for (int g = 0; g < GOOD_COUNT; g++){
                sum.rotted[g] += r.rotted[g];
            }
        }
        bool f_counts = sum.felled == d.felled && sum.broken == d.broken && sum.storeys == d.storeys && sum.zombies == d.zombies
                        && sum.rotted == d.rotted;
        bool f_level = !f_levels || (sum.people == d.people && sum.held == d.held);
        if (!f_counts || !f_level){
            auto total = [](const EconomyDay& x){
                int people = 0;
                int held = 0;
                for (int p : x.people){
                    people += p;
                }
                for (int g : x.held){
                    held += g;
                }
                return std::to_string(people) + " people, " + std::to_string(held) + " goods, "
                       + std::to_string(x.felled + x.broken + x.storeys + x.zombies) + " counted";
            };
            snprintf(buf,sizeof(buf),"%s day %i: its colonies' rows add up to %s - the whole's are %s",what,d.day,
                     total(sum).c_str(),total(d).c_str());
            out.push_back(buf);
        }
    };
    adds_up(h.start,true,"start,");
    for (const EconomyDay& d : h.days){
        adds_up(d,true,"row");
    }
    adds_up(now,true,"today,");
    //The journal in the order made.
    for (size_t i = 1; i < h.journal.size(); i++){
        if (h.journal[i].tick < h.journal[i - 1].tick){
            snprintf(buf,sizeof(buf),"journal: line %i (tick %llu) is before the line above it",(int)i,
                     (unsigned long long)h.journal[i].tick);
            out.push_back(buf);
            break;
        }
    }
    return out;
}

namespace {

std::string PersonName(const EconomyState& e, uint32_t id){
    for (const EconomyWorker& k : e.workers){
        if (k.id == id){
            return EconomyPersonName(k);
        }
    }
    for (const EconomyDeath& d : e.dead){
        if (d.id == id){
            return EconomyDeathName(d);
        }
    }
    return "someone";
}

std::string WithArticle(const char* noun){
    bool f_vowel = noun[0] && strchr("aeiouAEIOU",noun[0]) != NULL;
    return std::string(f_vowel ? "an " : "a ") + noun;
}

std::string Capitalised(std::string s){
    if (!s.empty()){
        s[0] = (char)toupper((unsigned char)s[0]);
    }
    return s;
}

}

const char* ChasmKindLabel(int kind, bool f_plural){
    switch (kind){
        case ZONE_KIND_HOUSE:       return f_plural ? "houses" : "house";
        case ZONE_KIND_STORE:       return f_plural ? "stores" : "store";
        case ZONE_KIND_WOODCUTTER:  return f_plural ? "woodcutters' huts" : "woodcutter's hut";
        case ZONE_KIND_WATER:       return f_plural ? "water collectors" : "water collector";
        case ZONE_KIND_FIELD:       return f_plural ? "fields" : "field";
        case ZONE_KIND_WINCH:       return f_plural ? "winches" : "winch";
        case ZONE_KIND_CAMP:        return f_plural ? "camps" : "camp";
        case ZONE_KIND_BRIDGE:      return f_plural ? "bridges" : "bridge";
        case ZONE_KIND_BUILDERS:    return f_plural ? "builders' huts" : "builders' hut";
        case ZONE_KIND_RANGE:       return f_plural ? "archery ranges" : "archery range";
        case ZONE_KIND_TOWER:       return f_plural ? "guard towers" : "guard tower";
        case ZONE_KIND_ROPEWALK:    return f_plural ? "ropewalks" : "ropewalk";
        case ZONE_KIND_STONECUTTER: return f_plural ? "stonecutters' huts" : "stonecutter's hut";
        case ZONE_KIND_MINE:        return f_plural ? "mines" : "mine";
        case ZONE_KIND_WELL:        return f_plural ? "wells" : "well";
        case ZONE_KIND_STEAM:       return f_plural ? "steam collectors" : "steam collector";
        case ZONE_KIND_CONDENSER:   return f_plural ? "condensers" : "condenser";
        case ZONE_KIND_SAIL_LOFT:   return f_plural ? "sail lofts" : "sail loft";
        case ZONE_KIND_DOCK:        return f_plural ? "zeppelin docks" : "zeppelin dock";
        case ZONE_KIND_MAST:        return f_plural ? "mooring masts" : "mooring mast";
        case ZONE_KIND_HUT:         return f_plural ? "swamp huts" : "swamp hut";
        default:                    return f_plural ? "buildings" : "building";
    }
}

const char* ChasmGoalTitle(int goal){
    switch (goal){
        case CHASM_GOAL_WINTER:     return "through the winter, nobody lost";
        case CHASM_GOAL_CHASM:      return "reach the chasm";
        default:                    return "a goal";
    }
}

std::string ChasmJournalText(const EconomyState& e, const EconomyEvent& ev){
    std::string who = PersonName(e,ev.person);
    std::string good;
    for (int g = 0; g < GOOD_COUNT; g++){
        good = (ev.what & GOOD_BIT(g)) ? GoodName(g) : good;
    }
    char buf[160];
    switch (ev.kind){
        case EVENT_BORN:
            return who + " was born";
        case EVENT_PUT_DOWN:
            return who + " was put down (debug)";
        case EVENT_DIED:
            //...his rank before his name, as the dead keep it (ranks_plan.md): "Expert Jelle De Wit died of thirst". Not
            //his kills - the page's column would cut the cause off; the pop-up and the death line have room for them.
            //His AGE last (the user's, 2026-10-24), where a narrow column cuts first: "... died of old age at 43".
            buf[0] = 0;
            for (const EconomyDeath& d : e.dead){
                if (d.id != ev.person){
                    continue;
                }
                int rank = EconomyRank(d.kills);
                who = (rank > RANK_NONE) ? std::string(EconomyRankName(rank)) + " " + who : who;
                int age = EconomyAgeAt(d.born,d.tick);
                if (age > 0){
                    snprintf(buf,sizeof(buf)," at %i",age);
                }else{
                    snprintf(buf,sizeof(buf)," as a baby");
                }
            }
            return ((ev.amount == NEEDS_DIED_ZOMBIE) ? who + " was killed by a zombie"
                                                     : who + " died of " + EconomyDeathCause(ev.amount)) + buf;
        case EVENT_RANKED:
            return who + " rose to " + EconomyRankName(ev.amount);
        case EVENT_ZEPPELIN_STAGE:
            //steam_plan.md step 5: each of its stages, the last its finishing.
            return (ev.amount >= ZEPPELIN_STAGES) ? std::string("The zeppelin was finished")
                                                  : std::string("The zeppelin's ") + EconomyZeppelinStageName(ev.amount) + " was built";
        case EVENT_HARVEST:
            snprintf(buf,sizeof(buf),"Harvested %i %s",ev.amount,good.c_str());
            return buf;
        case EVENT_BUILDING_FINISHED:
            return Capitalised(WithArticle(ChasmKindLabel(ev.amount,false))) + " was finished";
        case EVENT_SETTLED:
            if (!ev.person){
                snprintf(buf,sizeof(buf),"%i settlers made camp",ev.amount);
                return buf;
            }
            return who + " founded a settlement";
        case EVENT_SEAM_SEEN:
            return who + " saw floatstone in the chasm's wall";
        case EVENT_DISCOVERED:{
            //Named by the world's own list of areas (Areas.h), whose index the event carries.
            const AreaData* a = e.world ? e.world->areas.get() : nullptr;
            int kind = (a && ev.amount >= 0 && ev.amount < (int)a->areas.size()) ? a->areas[(size_t)ev.amount].kind : -1;
            return who + " found " + AreaKindTold(kind);
        }
        case EVENT_FROST_TOOK:
            return std::string("The frost took a field of ") + ZoneCropName(ev.amount);
        case EVENT_GREW_UP:
            return who + " grew up";
        case EVENT_GREW_OLD:
            return who + " grew old";
        case EVENT_EXPECTING:
            return who + " was expecting a child";
        case EVENT_BOULDER_FOUND:
            return who + " found a boulder";
        case EVENT_BALCONY_REACHED:
            return who + " reached a balcony";
        //Short enough for the journal's column: the goals panel says them in full.
        case EVENT_GOAL_MET:
            return std::string("Goal met: ") + (ev.amount == CHASM_GOAL_WINTER ? "through the winter" : ChasmGoalTitle(ev.amount));
        case EVENT_GOAL_FAILED:
            return std::string("Goal failed: ") + (ev.amount == CHASM_GOAL_WINTER ? "through the winter" : ChasmGoalTitle(ev.amount));
        case EVENT_DEMO_COMPLETE:
            snprintf(buf,sizeof(buf),"Demo complete, %i of %i goals",ev.amount,CHASM_GOALS);
            return buf;
        case EVENT_COLONY_LOST:
            return "The colony was lost";
        case EVENT_WALL_BROKEN:
            return "Zombies broke the wall";
        case EVENT_GATE_BROKEN:
            return "Zombies broke a gate";
        case EVENT_BREACHED:
            return std::string("Zombies broke into a ") + ChasmKindLabel(ev.amount,false);
        case EVENT_TREE_FELLED:
            return who + " felled a tree";
        case EVENT_ROCK_BROKEN:
            return who + " broke a rock";
        case EVENT_ZOMBIE_FELLED:
            return who + ((ev.amount == KILL_HAND) ? " killed a zombie by hand" : " shot a zombie down");
        case EVENT_STOREY_RAISED:
            return "A storey of " + WithArticle(ChasmKindLabel(ev.amount,false)) + " went up";
        case EVENT_NOTHING_TO_TAKE:
            return "No food or water for " + who + " to take";
        case EVENT_RAN_OUT:
            return who + " ran out of " + good;
        case EVENT_NO_TENT:
            return "No tent to spare for " + who + " to take";
        default:
            return EconomyEventName(ev.kind);
    }
}

std::string ChasmStatsDate(uint64_t tick){
    CalendarDate d = CalendarDateOf(tick);
    std::string season = CalendarSeasonName(d.season);
    std::transform(season.begin(),season.end(),season.begin(),::toupper);
    char buf[48];
    snprintf(buf,sizeof(buf),"Y%i %s %i",d.year,season.c_str(),d.day);
    return buf;
}

std::string ChasmStatsSeasonName(int season){
    std::string name = CalendarSeasonName(((season % CALENDAR_SEASONS) + CALENDAR_SEASONS) % CALENDAR_SEASONS);
    std::transform(name.begin(),name.end(),name.begin(),::toupper);
    char buf[48];
    snprintf(buf,sizeof(buf),"Y%i %s",season / CALENDAR_SEASONS + 1,name.c_str());
    return buf;
}
