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
                 + (row.people[LIFE_YOUNG] + row.people[LIFE_ADULT]) * LIFE_SHARE_FULL;
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

ChasmStatsNow ChasmStatsNowOf(const EconomyState& e, const ZoneState& z){
    ChasmStatsNow n;
    EconomyDayLevels(e,e.tick,n.row);
    ChasmStatsDays(n.row,n.food_days,n.water_days);
    n.people = (int)e.workers.size();
    std::vector<int> living(z.buildings.size(),0);
    for (const EconomyWorker& k : e.workers){
        n.expecting += k.due ? 1 : 0;
        n.in_camp += (k.house == 0 || (k.house < z.buildings.size() && z.buildings[k.house].kind == ZONE_KIND_CAMP)) ? 1 : 0;
        if (k.house < living.size()){
            living[k.house]++;
        }
    }
    //What stands and what is still to build, by building, in one pass over the plots and the cells.
    std::vector<uint8_t> stands(z.buildings.size(),0);
    std::vector<uint8_t> site(z.buildings.size(),0);
    for (size_t v = 0; v < z.building.size(); v++){
        uint32_t id = z.building[v];
        if (!id || id >= z.buildings.size()){
            continue;
        }
        stands[id] |= (z.standing[v] > 0) ? 1 : 0;
        site[id] |= (z.storeys[v] > z.standing[v]) ? 1 : 0;
        n.tents += (z.buildings[id].kind == ZONE_KIND_CAMP && z.standing[v] > 0) ? 1 : 0;
    }
    for (size_t c = 0; c < z.field.size(); c++){
        uint32_t id = z.field[c];
        if (!id || id >= z.buildings.size()){
            continue;
        }
        stands[id] |= z.field_built[c] ? 1 : 0;
        site[id] |= z.field_built[c] ? 0 : 1;
    }
    for (size_t id = 1; id < z.buildings.size(); id++){
        int kind = z.buildings[id].kind;
        if (z.buildings[id].size <= 0 || kind <= ZONE_KIND_NONE || kind >= ZONE_KIND_COUNT){
            continue;
        }
        if (site[id]){
            n.building[kind]++;
        }else if (stands[id]){
            n.standing[kind]++;
        }
        n.houses += (kind == ZONE_KIND_HOUSE && stands[id] && living[id] > 0) ? 1 : 0;
    }
    for (const EconomyDeath& d : e.dead){
        n.dead++;
        if (d.cause >= 0 && d.cause < CHASM_STATS_CAUSES){
            n.dead_of[d.cause]++;
        }
    }
    for (const EconomySettlement& st : e.settlements){
        ChasmStatsSettlement one;
        one.id = st.id;
        one.beds = st.beds;
        for (const EconomyWorker& k : e.workers){
            one.people += (EconomyPersonSettlement(e,k) == st.id) ? 1 : 0;
        }
        n.beds += st.beds;
        n.settlements.push_back(one);
    }
    return n;
}

std::vector<std::string> ChasmStatsCheck(const EconomyState& e){
    std::vector<std::string> out;
    const EconomyHistory& h = e.history;
    char buf[160];
    std::map<int,int> born_on;
    std::map<int,int> died_on;
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
        int expected = heads(before) + born_on[after.day] - died_on[after.day];
        if (heads(after) != expected){
            snprintf(buf,sizeof(buf),"%s day %i: %i people, but the day before's %i, %i born and %i dead make %i",what,after.day,
                     heads(after),heads(before),born_on[after.day],died_on[after.day],expected);
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
        default:                    return f_plural ? "buildings" : "building";
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
        case EVENT_DIED:
            return (ev.amount == NEEDS_DIED_ZOMBIE) ? who + " was killed by a zombie"
                                                    : who + " died of " + EconomyDeathCause(ev.amount);
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
        case EVENT_TREE_FELLED:
            return who + " felled a tree";
        case EVENT_ROCK_BROKEN:
            return who + " broke a rock";
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
