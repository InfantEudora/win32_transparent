#include "VoidBrief.h"

#include <algorithm>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VOID_SHORT_DAYS     4.0     //water or food for fewer days than this is a reason to speak
#define VOID_NEED_DAYS      2.0     //someone with less health than this is named, as Chasm's red rows (ISSUES_RED_DAYS)
#define VOID_MAX_OPTIONS    8       //the menu's length, "nothing today" included

namespace {

//--- reading Chasm's JSON without tripping JSON_NOEXCEPTION: a missing key or a wrong type is a default -----

const json& Get(const json& j, const char* key){
    static const json none;
    if (!j.is_object()){
        return none;
    }
    auto it = j.find(key);
    return it != j.end() ? *it : none;
}

double Num(const json& j, const char* key, double fallback = 0.0){
    const json& v = Get(j,key);
    return v.is_number() ? v.get<double>() : fallback;
}

int Int(const json& j, const char* key, int fallback = 0){
    return (int)std::lround(Num(j,key,fallback));
}

std::string Str(const json& j, const char* key, const std::string& fallback = std::string()){
    const json& v = Get(j,key);
    return v.is_string() ? v.get<std::string>() : fallback;
}

bool Flag(const json& j, const char* key){
    const json& v = Get(j,key);
    return v.is_boolean() && v.get<bool>();
}

const json& List(const json& j, const char* key){
    static const json empty = json::array();
    const json& v = Get(j,key);
    return v.is_array() ? v : empty;
}

//--- Chasm's rules, copied (see the header): only for a Chasm from before 2026-10-14 ------------------------

#define CHASM_DAYS_PER_SEASON   10          //Calendar.h
#define CHASM_WATER_WORTH_DAYS  2.0         //NEEDS_WATER_WORTH: a unit of water is two days of one adult's drinking

const char* SEASONS[4] = {"spring","summer","autumn","winter"};
const char* FOOD_CROPS[3] = {"wheat","greens","beans"};

//A crop's days to ripen at a pace of 1: chasm_economy's `crops`, or crop_days in Economy.cpp.
double CropDays(const json& economy, const std::string& crop){
    const json& c = Get(Get(economy,"crops"),crop.c_str());
    if (Get(c,"days").is_number()){
        return Num(c,"days");
    }
    if (crop == "wheat" || crop == "hemp"){ return 15; }
    if (crop == "greens"){ return 8; }
    if (crop == "beans"){ return 20; }
    return 0;
}

//EconomyFoodWorth, in days of one adult's eating
double FoodWorthDays(const std::string& good){
    if (good == "wheat" || good == "fruit"){ return 1.0; }
    if (good == "greens"){ return 0.5; }
    if (good == "beans"){ return 2.0; }
    if (good == "provisions"){ return 3.0; }
    return 0.0;
}

//EconomyShare: what someone eats and drinks, in adults
double Share(const std::string& stage){
    if (stage == "baby"){ return 0.5; }
    if (stage == "child"){ return 0.75; }
    return 1.0;
}

//--- words ------------------------------------------------------------------------------------------------

std::string Plural(int n, const std::string& one, const std::string& many){
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

//Whole days held, as Chasm's DaysText counts them: rounded down, "under a day" below one.
std::string DaysHeld(double days){
    int d = (int)floor(days);
    return d < 1 ? "under a day" : Plural(d,"day","days");
}

//A time ahead, rounded.
std::string InDays(double days){
    int d = (int)lround(days);
    return d < 1 ? "within a day" : "in " + Plural(d,"day","days");
}

std::string Upper(std::string s){
    if (!s.empty()){
        s[0] = (char)toupper((unsigned char)s[0]);
    }
    return s;
}

std::string Lower(std::string s){
    std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){ return (char)tolower(c); });
    return s;
}

//"a", "a and b", "a, b and c"
std::string Join(const std::vector<std::string>& items){
    std::string out;
    for (size_t i = 0;i < items.size();i++){
        out += (i == 0) ? "" : (i + 1 == items.size()) ? " and " : ", ";
        out += items[i];
    }
    return out;
}

std::string JobName(const std::string& job, int n){
    if (job == "water_carrier"){ return n == 1 ? "water carrier" : "water carriers"; }
    if (job == "picker"){ return n == 1 ? "fruit picker" : "fruit pickers"; }
    return n == 1 ? job : job + "s";
}

std::string KindName(const std::string& kind){
    if (kind == "woodcutter"){ return "woodcutter's hut"; }
    if (kind == "water"){ return "water collector"; }
    if (kind == "builders"){ return "builders' hut"; }
    if (kind == "tower"){ return "guard tower"; }
    return kind;
}

//The days in a row's value - "WATER 3 DAYS", "FOOD UNDER A DAY" - or -1 without any.
double RowDays(const std::string& value){
    if (value.find("UNDER A DAY") != std::string::npos){
        return 0.5;
    }
    for (size_t i = 0;i < value.size();i++){
        if (isdigit((unsigned char)value[i])){
            return atof(value.c_str() + i);
        }
    }
    return -1.0;
}

const json* FindIssue(const json& issues, const char* kind){
    for (const json& i:List(issues,"issues")){
        if (Str(i,"kind") == kind){
            return &i;
        }
    }
    return NULL;
}

std::string WorkerName(const json& economy, int id){
    for (const json& w:List(economy,"workers")){
        if (Int(w,"id") == id){
            return Str(w,"name","someone");
        }
    }
    return "someone";
}

//What a site still waits for, by good; empty when it has it all.
std::vector<std::pair<std::string,int>> Lacking(const json& site){
    std::vector<std::pair<std::string,int>> out;
    const json& needed = Get(site,"needed");
    const json& brought = Get(site,"brought");
    if (needed.is_object()){
        for (auto it = needed.begin();it != needed.end();++it){
            int left = (it.value().is_number() ? it.value().get<int>() : 0) - Int(brought,it.key().c_str());
            if (left > 0){
                out.push_back({it.key(),left});
            }
        }
    }
    return out;
}

struct Option{
    int rank;           //the menu is ordered by it: what cannot wait first
    std::string text;
};
enum { RANK_DANGER, RANK_COUNTDOWN, RANK_SHORT, RANK_STUCK, RANK_NEXT, RANK_FIELD, RANK_SITE };

}

VoidBrief VoidBuildBrief(const VoidState& s, const VoidBrief* last){
    VoidBrief b;
    b.at = std::chrono::steady_clock::now();
    const json& t = s.time;
    const json& e = s.economy;
    if (!Str(t,"error").empty() || !Str(e,"error").empty()){
        b.error = "Chasm says: " + (Str(t,"error").empty() ? Str(e,"error") : Str(t,"error"));
        return b;
    }
    if (!Get(t,"season").is_string() || !Get(e,"workers").is_array()){
        b.error = "the replies are not chasm_time and chasm_economy's - is it Chasm on that port?";
        return b;
    }
    b.f_read = true;
    b.tick = (uint64_t)std::max(0.0,Num(t,"tick"));
    /*
        A load or a new map is nothing to compare with: the calendar went back, or further on than Chasm
        can run in the time between the reads (50 ticks a second at its top speed of 10), or fewer have
        died than had.
    */
    if (last && last->f_read){
        double seconds = std::chrono::duration<double>(b.at - last->at).count();
        uint64_t reach = (uint64_t)((seconds + 1.0) * 50.0 * 10.0);
        if (last->tick > b.tick || b.tick - last->tick > reach || (int)List(e,"dead").size() < last->deaths){
            last = NULL;
        }
    }
    std::vector<std::string> sections;
    std::vector<Option> options;

    //--- TODAY ---
    int year = Int(t,"year",1);
    int day = Int(t,"day",1);
    std::string season = Str(t,"season");
    int si = 0;
    for (int i = 0;i < 4;i++){
        si = (season == SEASONS[i]) ? i : si;
    }
    double to_next = CHASM_DAYS_PER_SEASON + 1 - day - Num(t,"day_fraction");     //the next season starts on day 11
    double to_winter = (si < 3) ? (2 - si) * CHASM_DAYS_PER_SEASON + to_next : 0.0;
    //Growing weather left where the colony is: Chasm's at the camp, where the snow may come before winter does.
    double growing_left = Get(e,"camp_growing_days_left").is_number() ? Num(e,"camp_growing_days_left") : to_winter;
    b.date = "Year " + std::to_string(year) + ", " + season + ", day " + std::to_string(day);
    std::string today = "TODAY: " + b.date + " of " + std::to_string(CHASM_DAYS_PER_SEASON) + ". "
                      + Upper(SEASONS[(si + 1) % 4]) + " " + InDays(to_next) + ".";
    if (si == 2){
        today += " Whatever is still in a field on winter's first day is lost to the frost.";
    }
    //The snow comes down from the north, which is -z: the camp is under it once the front is south of it.
    const json& camp = Get(e,"camp");
    if (camp.is_object() && Num(camp,"z") < Num(t,"snow_front_z",-1e9)){
        today += " The snow lies on the colony; every home under it burns firewood at dawn.";
    }
    sections.push_back(today);

    //--- PEOPLE ---
    const json& workers = List(e,"workers");
    const json& people = Get(e,"people");
    int total = Int(people,"total",(int)workers.size());
    for (const json& w:workers){
        b.people.push_back(Int(w,"id"));
    }
    const json* houses_row = FindIssue(s.issues,"build_houses");
    int in_camp = Get(people,"in_camp").is_number() ? Int(people,"in_camp")
                : houses_row ? std::max(0,(int)RowDays(Str(*houses_row,"value"))) : 0;
    std::string line = "PEOPLE: " + std::to_string(total);
    if (total == 0){
        line += ". Nobody is left.";
    }else if (in_camp >= total){
        line += total == 1 ? ", sleeping in the camp." : ", all sleeping in the camp.";
    }else if (in_camp > 0){
        line += ": " + std::to_string(total - in_camp) + " housed, " + std::to_string(in_camp) + " sleeping in the camp.";
    }else{
        line += total == 1 ? ", housed." : ", all housed.";
    }
    int homeless = Int(s.buildings,"homeless");     //no home at all, not even the camp
    if (homeless > 0){
        line += " " + Plural(homeless,"has","have") + " no home.";
    }
    const json& jobs = Get(e,"jobs");
    std::vector<std::string> working, empty_places;
    int n_working = 0;
    if (jobs.is_object()){
        for (auto it = jobs.begin();it != jobs.end();++it){
            int n = Int(it.value(),"working");
            int places = Int(it.value(),"places");
            if (n > 0){
                working.push_back(Plural(n,JobName(it.key(),1),JobName(it.key(),2)));
                n_working += n;
            }
            if (places > n){
                std::string job = JobName(it.key(),places - n);
                empty_places.push_back(places - n > 1 ? std::to_string(places - n) + " places for " + job
                                                      : std::string("a place for ") + (strchr("aeiou",job[0]) ? "an " : "a ") + job);
            }
        }
    }
    if (total > 0){
        line += n_working ? " " + std::to_string(n_working) + " working: " + Join(working) + "." : " Nobody is working.";
    }
    std::vector<std::string> idle;
    for (const json& w:workers){
        std::string stage = Str(w,"stage");
        if (Str(w,"job") == "none" && stage != "child" && stage != "baby"){
            idle.push_back(Str(w,"name"));
        }
    }
    if (idle.size() == 1){
        line += " " + idle[0] + " has no work.";
    }else if (idle.size() > 1){
        line += " " + (idle.size() <= 3 ? Join(idle) : std::to_string(idle.size())) + " have no work.";
    }
    int children = Int(people,"children");
    int babies = Int(people,"babies");
    if (children || babies){
        std::vector<std::string> young;
        if (children){ young.push_back(Plural(children,"child","children")); }
        if (babies){ young.push_back(Plural(babies,"baby","babies")); }
        line += " " + Upper(Join(young)) + ".";
    }
    std::vector<std::string> expecting;
    for (const json& w:workers){
        if (Get(w,"due_in_days").is_number()){
            expecting.push_back(Str(w,"name") + " " + InDays(Num(w,"due_in_days")));
        }
    }
    if (!expecting.empty()){
        line += " Expecting: " + Join(expecting) + ".";
    }
    std::vector<std::string> in_need;
    for (const json& w:workers){
        std::string name = Str(w,"name");
        if (Flag(w,"dry")){
            in_need.push_back(name + " has had no water and is losing health");
        }else if (Num(w,"health_days",99.0) < VOID_NEED_DAYS){
            in_need.push_back(name + " is weak, " + DaysHeld(Num(w,"health_days")) + " of health left");
        }
        if (Flag(w,"cold")){
            in_need.push_back(name + " is cold: no fire at home today");
        }
    }
    if (!in_need.empty()){
        line += " " + Upper(Join(in_need)) + ".";
        for (const std::string& n:in_need){
            b.reasons.push_back(n);
        }
    }
    const json& dead = List(e,"dead");
    b.deaths = (int)dead.size();
    if (!dead.empty()){
        int thirst = 0, hunger = 0, cold = 0, other = 0;
        for (const json& d:dead){
            std::string cause = Str(d,"cause");
            (cause == "thirst" ? thirst : cause == "hunger" ? hunger : cause == "cold" ? cold : other)++;
        }
        std::vector<std::string> causes;
        if (thirst){ causes.push_back(std::to_string(thirst) + " of thirst"); }
        if (hunger){ causes.push_back(std::to_string(hunger) + " of hunger"); }
        if (cold){ causes.push_back(std::to_string(cold) + " of cold"); }
        if (other){ causes.push_back(std::to_string(other) + " otherwise"); }
        const json& newest = dead.back();
        double ago = Num(t,"days_since_start") + Num(t,"day_fraction") - Num(newest,"day");
        line += " " + Plural((int)dead.size(),"has","have") + " died, " + Join(causes) + "; the last was "
              + Str(newest,"name") + ", " + (ago < 1.0 ? "today" : Plural((int)floor(ago),"day","days") + " ago") + ".";
    }
    sections.push_back(line);

    //--- STORES ---
    const json& stored = Get(e,"stored");
    const json& held = Get(e,"held");
    double water_days = Num(held,"water_days");
    double food_days = Num(held,"food_days");
    if (!Get(held,"water_days").is_number()){
        /*
            A Chasm from before `held`: what the stores and the camp hold, as `stored` counts it, at the copied
            shares and worths. Chasm's own rows count the houses' pantries too, so where a row gives the days,
            its figure is the one used.
        */
        double mouths = 0.0;
        for (const json& w:workers){
            mouths += Share(Str(w,"stage"));
        }
        mouths = std::max(1.0,mouths);
        double food_units_days = 0.0;
        if (stored.is_object()){
            for (auto it = stored.begin();it != stored.end();++it){
                if (it.value().is_number()){
                    food_units_days += it.value().get<double>() * FoodWorthDays(it.key());
                }
            }
        }
        water_days = Int(stored,"water") * CHASM_WATER_WORTH_DAYS / mouths;
        food_days = food_units_days / mouths;
        const json* water_row = FindIssue(s.issues,"build_water");
        const json* food_row = FindIssue(s.issues,"build_field");
        if (water_row && RowDays(Str(*water_row,"value")) >= 0.0){
            water_days = RowDays(Str(*water_row,"value"));
        }
        if (food_row && RowDays(Str(*food_row,"value")) >= 0.0){
            food_days = RowDays(Str(*food_row,"value"));
        }
    }
    line = "STORES: water for " + DaysHeld(water_days) + ", food for " + DaysHeld(food_days) + ". Wood "
         + std::to_string(Int(stored,"wood")) + ".";
    /*
        Firewood, from autumn on or under the snow: how many dawns the homes the snow reaches can burn, from
        their woodsheds and with the stores'. Chasm counts it whatever the season, for the winter ahead.
    */
    const json& firewood = Get(e,"firewood");
    bool f_snow = today.find("The snow lies") != std::string::npos;
    if (Int(firewood,"burn_per_dawn") > 0 && (si >= 2 || f_snow)){
        int homes = Int(firewood,"homes");
        line += " Firewood: " + Plural(homes,"home","homes") + (f_snow ? " under the snow " : " the snow will reach ")
              + (f_snow ? (homes == 1 ? "burns " : "burn ") : "will burn ")
              + std::to_string(Int(firewood,"burn_per_dawn")) + " wood a dawn, enough for "
              + Plural((int)floor(Num(firewood,"dawns_with_stores")),"dawn","dawns") + " with the stores.";
        if (f_snow && Num(firewood,"dawns_with_stores") < VOID_SHORT_DAYS){
            b.reasons.push_back("firewood for " + Plural((int)floor(Num(firewood,"dawns_with_stores")),"dawn","dawns"));
        }
        if (Int(firewood,"cold_today") > 0){
            line += " " + Plural(Int(firewood,"cold_today"),"home was","homes were") + " cold at dawn.";
            b.reasons.push_back(Plural(Int(firewood,"cold_today"),"home","homes") + " cold at dawn");
        }
    }
    const json& kept = Get(e,"kept");
    for (const char* good:{"provisions","rope"}){
        int held = Int(stored,good);
        int keep = Int(Get(kept,good),"kept");
        int ordered = Int(Get(kept,good),"ordered");
        if (held == 0 && keep == 0 && ordered == 0){
            line += std::string(" No ") + good + ".";
            continue;
        }
        line += " " + Upper(good) + " " + std::to_string(held);
        if (keep){ line += ", " + std::to_string(keep) + " kept"; }
        if (ordered){ line += ", " + std::to_string(ordered) + " being made"; }
        line += ".";
    }
    sections.push_back(line);
    if (water_days < VOID_SHORT_DAYS){
        b.reasons.push_back("water for " + DaysHeld(water_days));
    }
    if (food_days < VOID_SHORT_DAYS){
        b.reasons.push_back("food for " + DaysHeld(food_days));
    }

    //--- FIELDS ---
    const json& fields = List(e,"fields");
    line = "FIELDS:";
    if (fields.empty()){
        line += " none.";
    }
    for (const json& f:fields){
        int id = Int(f,"id");
        std::string crop = Str(f,"crop");
        std::string last_crop = Str(f,"last");
        std::string next = Str(f,"sows_next");
        const json& frost = Get(f,"frost");
        double pace = Num(frost,"pace");
        line += "\nField " + std::to_string(id) + ": ";
        if (Flag(f,"sown")){
            line += crop;
            if (pace <= 0.0){
                line += ", not growing: nobody farms it.";
            }else{
                double ripe_in = CropDays(e,crop) * (1.0 - Num(f,"growth")) / pace;
                bool f_in_time = !Get(frost,"ripens_in_time").is_boolean() || Flag(frost,"ripens_in_time");
                line += ", ripe " + InDays(ripe_in) + (f_in_time ? ", before the frost." : ", after the frost: it will be lost.");
            }
            continue;
        }
        bool f_season_over = si == 3 || Num(frost,"growing_days_left") <= 0.0;
        if (next == "none"){
            line += f_season_over ? "bare for the winter, no crop chosen" : "nothing sown yet, no crop chosen";
        }else{
            line += f_season_over ? "bare for the winter; sows " + next + " in spring" : "nothing sown yet; sows " + next;
        }
        int yield = Int(Get(f,"next_yields"),next.c_str(),100);
        if (next != "none" && last_crop != "none" && yield != 100){
            line += ". " + Upper(next) + " after " + last_crop + " gives " + std::to_string(yield) + " of a full 100";
        }
        line += Flag(f,"fallow") ? "; nobody farms it." : ".";
    }
    sections.push_back(line);

    //--- WORK ---
    std::vector<std::string> work;
    for (const json& building:List(s.buildings,"buildings")){
        if (Flag(building,"stopped")){
            work.push_back("A " + KindName(Str(building,"kind")) + " is stopped.");
        }
    }
    if (!empty_places.empty()){
        work.push_back(Upper(Join(empty_places)) + (empty_places.size() == 1 && empty_places[0].rfind("a ",0) == 0 ? " stands" : " stand") + " empty.");
    }
    const json& sites = List(e,"sites");
    for (const json& site:sites){
        std::vector<std::pair<std::string,int>> lack = Lacking(site);
        std::string kind = KindName(Str(site,"kind"));
        if (lack.empty()){
            work.push_back("A " + kind + " is being built.");
            continue;
        }
        std::vector<std::string> wants, short_of;
        for (auto& l:lack){
            wants.push_back(std::to_string(l.second) + " " + l.first);
            if (Int(stored,l.first.c_str()) < l.second){
                short_of.push_back(l.first + " (" + std::to_string(Int(stored,l.first.c_str())) + ")");
            }
        }
        std::string w = "A " + kind + " waits for " + Join(wants);
        w += List(site,"carriers").empty() ? "; nobody carries to it" : "";
        w += short_of.empty() ? "; the stores hold enough." : "; the stores are short of " + Join(short_of) + ".";
        work.push_back(w);
    }
    std::string work_line = "WORK:";
    for (const std::string& w:work){
        work_line += " " + w;
    }
    sections.push_back(work.empty() ? "WORK: every place is filled, nothing is being built." : work_line);

    //--- OUT ---
    std::vector<std::string> out;
    for (const json& w:workers){
        const json& post = Get(w,"post");
        const json& pack = Get(w,"pack");
        bool f_ordered = Flag(post,"ordered");
        if (!f_ordered && !pack.is_object() && !Get(w,"ice_home_days").is_number() && !Get(w,"tent").is_string()){
            continue;
        }
        std::string o = Str(w,"name");
        if (f_ordered){
            o += Flag(post,"there") ? ", at the post you gave" : ", on the way to the post you gave";
        }
        if (Str(w,"forage") == "fruit"){ o += ", picking fruit by hand"; }
        if (Str(w,"forage") == "water"){ o += ", drawing water by hand"; }
        if (pack.is_object()){
            o += ", pack: food for " + DaysHeld(Num(pack,"food_days")) + ", " + Plural(Int(pack,"water"),"water","water");
            if (Num(pack,"food_days") < 1.0 && f_ordered){
                b.reasons.push_back(Str(w,"name") + "'s pack has food for under a day");
            }
        }
        if (Str(w,"tent") == "carried"){ o += ", carrying a tent"; }
        if (Get(w,"ice_home_days").is_number()){
            double ice = Num(w,"ice_home_days");
            o += "; the only way home is over the ice, which thaws " + InDays(ice);
            if (ice < 3.0){
                b.reasons.push_back(Str(w,"name") + " must cross the ice home " + InDays(ice));
            }
        }
        out.push_back(o);
    }
    std::string out_line = "OUT:";
    for (const std::string& o:out){
        out_line += " " + o + ".";
    }
    sections.push_back(out.empty() ? "OUT: nobody." : out_line);
    int ripe = 0, ripe_abandoned = 0;
    for (const json& set:List(e,"settlements")){
        int r = Int(Get(set,"plants"),"ripe");
        ripe += r;
        ripe_abandoned += Int(set,"people") == 0 ? r : 0;
    }
    if (ripe > 0){
        sections.push_back("FRUIT: " + std::to_string(ripe) + " hang ripe in reach"
                           + (ripe_abandoned ? ", " + std::to_string(ripe_abandoned) + " of them where nobody lives now." : "."));
    }

    //--- THREATS ---
    const json& zombies = List(s.zombies,"zombies");
    if (zombies.empty()){
        sections.push_back("THREATS: none seen.");
    }else{
        std::vector<std::string> targets;
        for (const json& z:zombies){
            std::string name = Str(z,"target_name");
            if (!name.empty() && std::find(targets.begin(),targets.end(),name) == targets.end()){
                targets.push_back(name);
            }
        }
        sections.push_back("THREATS: " + Plural((int)zombies.size(),"zombie is","zombies are") + " about"
                           + (targets.empty() ? std::string(".") : ", after " + Join(targets) + "."));
        b.reasons.push_back(Plural((int)zombies.size(),"zombie","zombies") + " about");
    }

    //--- NEEDS ATTENTION, as the game shows it ---
    std::vector<std::string> rows;
    for (const json& i:List(s.issues,"issues")){
        std::string row = Str(i,"text");
        std::string value = Str(i,"value");
        rows.push_back(row + (value.empty() ? "" : " (" + value + ")"));
        std::string tier = Str(i,"tier");
        if (tier == "danger" || tier == "countdown" || tier == "stuck"){
            b.reasons.push_back(row);
        }
    }
    sections.push_back("NEEDS ATTENTION: " + (rows.empty() ? std::string("nothing.") : Join(rows) + "."));

    //--- SINCE LAST READ ---
    if (last && last->f_read){
        std::vector<std::string> since;
        for (size_t d = (size_t)std::max(0,last->deaths);d < dead.size();d++){
            std::string what = Str(dead[d],"name") + " died of " + Str(dead[d],"cause");
            since.push_back(what);
            b.reasons.push_back(what);
        }
        for (const json& w:workers){
            if (std::find(last->people.begin(),last->people.end(),Int(w,"id")) == last->people.end()){
                since.push_back(Str(w,"name") + " was born");
            }
        }
        const json& notices = Get(e,"notices");
        b.notices_made = Int(notices,"made");
        int fresh = std::min(b.notices_made - last->notices_made,(int)List(notices,"last").size());
        const json& kept_notices = List(notices,"last");
        for (int k = (int)kept_notices.size() - std::max(0,fresh);k < (int)kept_notices.size();k++){
            const json& n = kept_notices[k];
            std::vector<std::string> what;
            for (const json& g:List(n,"what")){
                what.push_back(g.is_string() ? g.get<std::string>() : "");
            }
            std::string kind = Str(n,"kind");
            std::string who = WorkerName(e,Int(n,"person"));
            std::string text = kind == "no_tent" ? who + " found no tent to take"
                             : kind == "nothing_to_take" ? who + " found no " + Join(what) + " to take for the pack"
                             : who + "'s pack ran out of " + Join(what);
            since.push_back(text);
            b.reasons.push_back(text);
        }
        if (!since.empty()){
            sections.push_back("SINCE LAST READ: " + Upper(Join(since)) + ".");
        }
    }else{
        b.notices_made = Int(Get(e,"notices"),"made");
    }

    //--- WHAT YOU CAN ADVISE ---
    auto rank_of = [](const std::string& tier){
        return tier == "danger" ? RANK_DANGER : tier == "countdown" ? RANK_COUNTDOWN : tier == "stuck" ? RANK_STUCK : RANK_NEXT;
    };
    const json* rope_row = FindIssue(s.issues,"site_needs_rope");
    bool f_hemp_wanted = rope_row && Str(*rope_row,"value") == "GROW HEMP";
    for (const json& i:List(s.issues,"issues")){
        std::string kind = Str(i,"kind");
        std::string value = Str(i,"value");
        int rank = rank_of(Str(i,"tier"));
        std::string where = Lower(Str(i,"text"));
        if (kind == "choose_crop"){
            //A crop for each food that still ripens on that field, and AUTO, which takes the most food of them.
            int id = Int(i,"building");
            const json* field = NULL;
            for (const json& f:fields){
                field = Int(f,"id") == id ? &f : field;
            }
            double pace = field ? Num(Get(*field,"frost"),"pace",1.0) : 1.0;
            double left = field ? Num(Get(*field,"frost"),"growing_days_left") : growing_left;
            std::string name = "Field " + std::to_string(id);
            int ripening = 0;
            for (const char* crop:FOOD_CROPS){
                double days = CropDays(e,crop) / std::max(0.1,pace);
                if (days <= left){
                    options.push_back({rank,name + ": start with " + crop + ". Ripe " + InDays(days) + ", before the frost."});
                    ripening++;
                }
            }
            if (f_hemp_wanted && CropDays(e,"hemp") / std::max(0.1,pace) <= left){
                options.push_back({rank,name + ": start with hemp, for rope. Ripe " + InDays(CropDays(e,"hemp") / std::max(0.1,pace)) + "."});
            }
            options.push_back({rank,name + (ripening ? ": leave it to AUTO, which sows the most food that still ripens."
                                                     : ": leave it to AUTO. Nothing sown now ripens before the frost.")});
        }else if (kind == "build_water"){
            options.push_back({rank,"Build a water collector. Water lasts " + DaysHeld(water_days) + "."});
        }else if (kind == "build_field"){
            std::string o = "Build a field. ";
            std::string greens = "Greens ripen in " + Plural((int)lround(CropDays(e,"greens")),"day","days");
            if (si == 3){
                o += "Nothing grows until spring, " + InDays(to_next) + ".";
            }else if (CropDays(e,"greens") <= growing_left){
                o += greens + ", wheat and beans take longer. Food lasts " + DaysHeld(food_days) + ".";
            }else{
                o += greens + ", after the frost: nothing would be harvested before spring.";
            }
            options.push_back({rank,o});
        }else if (kind == "build_woodcutter"){
            options.push_back({rank,"Build a woodcutter's hut. Wood " + std::to_string(Int(stored,"wood")) + "."});
        }else if (kind == "build_houses"){
            options.push_back({rank,"Build houses. " + Plural(in_camp,"sleeps","sleep") + " in the camp; "
                                    + (f_snow ? "under the snow they are cold." : "it is " + season + ", nobody is cold.")});
        }else if (kind == "build_store"){
            options.push_back({rank,"Build a store. What is made has nowhere to be kept but the camp and the houses."});
        }else if (kind == "nobody_at"){
            options.push_back({rank,"Free someone for " + where.substr(std::min(where.size(),(size_t)10)) + ": nobody works there."});
        }else if (kind == "no_room"){
            options.push_back({rank,"Give the woodcutter's hut room for its wood: a lot beside it, or a bigger hut."});
        }else if (kind == "ropewalk_short"){
            options.push_back({rank,"Make the ropewalk longer: it is too short to work."});
        }else if (kind == "house_shut_in"){
            options.push_back({rank,"Open a way into the house that is shut in" + (value.empty() ? std::string(".") : ": " + Lower(value) + " cannot get out.")});
        }else if (kind == "house_crowded"){
            options.push_back({rank,"Give the crowded house a storey or a plot more: " + Lower(value) + "."});
        }else if (kind == "site_needs_rope"){
            options.push_back({rank,f_hemp_wanted ? "Sow hemp on a field: a site needs rope, and none is made."
                                                  : "Build a ropewalk: a site needs rope, and none is made."});
        }else if (kind == "trees_unreached"){
            options.push_back({rank,"Build a woodcutter's hut near the trees in the way: " + Lower(value) + " wait for them to be felled."});
        }else if (kind == "tower_unkept"){
            options.push_back({rank,"Put a builder to keeping the guard tower: it runs down."});
        }else{
            options.push_back({rank,Upper(where) + "."});
        }
    }
    //Fruit, when food is short: the one food that needs no field and no time.
    if (food_days < VOID_SHORT_DAYS && ripe > 0){
        options.push_back({RANK_SHORT,(idle.size() == 1 ? "Send " + idle[0] : std::string("Send someone")) + " to pick fruit. "
                                      + std::to_string(ripe) + " hang ripe" + (ripe_abandoned ? ", " + std::to_string(ripe_abandoned) + " of them where nobody lives now." : ".")});
    }
    //A field about to sow a crop that grows badly after its last, and the best crop instead.
    for (const json& f:fields){
        std::string next = Str(f,"sows_next");
        const json& yields = Get(f,"next_yields");
        int yield = Int(yields,next.c_str(),100);
        if (Flag(f,"sown") || next == "none" || next == "hemp" || yield >= 100){
            continue;
        }
        std::string best;
        int best_yield = yield;
        for (const char* crop:FOOD_CROPS){
            if (Int(yields,crop) > best_yield){
                best = crop;
                best_yield = Int(yields,crop);
            }
        }
        if (!best.empty()){
            options.push_back({RANK_FIELD,"Field " + std::to_string(Int(f,"id")) + ": sow " + best + " next instead of " + next + ". "
                                          + Upper(next) + " after " + Str(f,"last") + " gives " + std::to_string(yield) + "; "
                                          + best + " gives " + std::to_string(best_yield) + "."});
        }
    }
    //Which site first, only with two or more waiting - and saying whether anything it waits for is short.
    int waiting = 0;
    for (const json& site:sites){
        waiting += Lacking(site).empty() ? 0 : 1;
    }
    for (const json& site:sites){
        std::vector<std::pair<std::string,int>> lack = Lacking(site);
        if (waiting < 2 || lack.empty() || Flag(site,"priority")){
            continue;
        }
        std::vector<std::string> wants, short_of;
        for (auto& l:lack){
            wants.push_back(std::to_string(l.second) + " " + l.first);
            if (Int(stored,l.first.c_str()) < l.second){
                short_of.push_back(l.first);
            }
        }
        options.push_back({RANK_SITE,"Build the " + KindName(Str(site,"kind")) + " first. It waits for " + Join(wants) + "; "
                                     + (short_of.empty() ? "nothing it needs is short." : "the stores are short of " + Join(short_of) + ".")});
    }
    std::stable_sort(options.begin(),options.end(),[](const Option& a, const Option& o){ return a.rank < o.rank; });
    if ((int)options.size() > VOID_MAX_OPTIONS - 1){
        options.resize(VOID_MAX_OPTIONS - 1);
    }
    options.push_back({RANK_SITE,"Nothing today."});
    std::string menu = "WHAT YOU CAN ADVISE:";
    for (size_t i = 0;i < options.size();i++){
        menu += "\n" + std::string(1,(char)('A' + i)) + ". " + options[i].text;
        b.options.push_back(options[i].text);
    }
    sections.push_back(menu);

    for (const std::string& section:sections){
        b.text += (b.text.empty() ? "" : "\n\n") + section;
    }
    return b;
}
