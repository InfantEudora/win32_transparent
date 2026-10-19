#include "Economy.h"
#include "ChasmWorld.h"
#include "Walkers.h"
#include <algorithm>
#include <cmath>

/*
    ZOMBIES (docs/zombies_plan.md), the rules: dropped by the debug tools, each goes for the nearest person it can get
    to and walks after him. Step 1: it does nothing to him yet - biting is step 2 - so nothing here changes a person.
    The zombies are the economy's state, saved and hashed with it, and move after the people on every tick.
*/

vec2 EconomyZombieFacing(const EconomyZombie& m){
    //Along the leg it is on - or, at its walk's end, the last leg it walked, which points at whom it was after.
    int s = std::min(m.seg,(int)m.route.size() - 2);
    if (s >= 0){
        vec2 d = m.route[s + 1] - m.route[s];
        float l = d.length();
        if (l > 1e-4f){
            return d * (1.0f / l);
        }
    }
    return vec2(0.0f,1.0f);
}

float EconomyFightPace(const EconomyWorker& k){
    return 0.5f * (EconomySkillFactor(k.skills[SKILL_STRENGTH]) + EconomySkillFactor(k.skills[SKILL_INGENUITY]));
}

bool EconomyReachable(const EconomyWorker& k, uint64_t tick){
    return !EconomyIndoors(k) && !(k.with && EconomyStageAt(k.born,tick) == LIFE_BABY) && EconomyWorkerLayer(k) == WALK_GROUND;
}

bool Economy::CanGetAt(const EconomyWorker& k) const{
    if (EconomyReachable(k,now)){
        return true;
    }
    //Indoors in a BREACHED house (zombies_plan.md step 5, the user's): its door is down, and it is no cover.
    return EconomyIndoors(k) && k.house && EconomyBreached(state,k.house)
           && !(k.with && EconomyStageAt(k.born,now) == LIFE_BABY) && EconomyWorkerLayer(k) == WALK_GROUND;
}

//--- Walls break (Economy.h) ------------------------------------------------------------------------------------------

int EconomyHarmStrength(int what){
    float seconds = (what == HARM_WALL) ? ZOMBIE_BREAK_WALL : (what == HARM_GATE) ? ZOMBIE_BREAK_GATE : ZOMBIE_BREACH_DOOR;
    return std::max(1,(int)std::lround(seconds * CALENDAR_TICKS_PER_SECOND));
}

int EconomyHarmOf(const EconomyState& e, int what, uint32_t id){
    for (const EconomyHarm& h : e.harm){
        if (h.what == what && h.id == id){
            return h.ticks;
        }
    }
    return 0;
}

bool EconomyBreached(const EconomyState& e, uint32_t building){
    return EconomyHarmOf(e,HARM_DOOR,building) >= EconomyHarmStrength(HARM_DOOR);
}

bool EconomyGateBroken(const EconomyState& e, int plot){
    return plot >= 0 && EconomyHarmOf(e,HARM_GATE,(uint32_t)plot) >= EconomyHarmStrength(HARM_GATE);
}

namespace {

int Ticks(float seconds){
    return std::max(1,(int)std::lround(seconds * CALENDAR_TICKS_PER_SECOND));
}

//What is left of each thing in a zombie's way, for its walk (Walkers.h, WalkBreaks): seconds of one zombie at it.
struct ZombieBreaks : public WalkBreaks{
    const EconomyState& e;
    explicit ZombieBreaks(const EconomyState& state) : e(state){}
    float Left(int what, uint32_t id) const{
        int left = std::max(0,EconomyHarmStrength(what) - EconomyHarmOf(e,what,id));
        return (float)left / CALENDAR_TICKS_PER_SECOND;
    }
    float WallLeft(int plot) const override { return Left(HARM_WALL,(uint32_t)plot); }
    float GateLeft(int plot) const override { return Left(HARM_GATE,(uint32_t)plot); }
    float DoorLeft(uint32_t building) const override { return Left(HARM_DOOR,building); }
    float ClimbSeconds() const override { return ZOMBIE_CLIMB_SECONDS; }
};

//It stops where it is: a route of the one point it stands on.
void StopZombie(EconomyZombie& m){
    m.route.assign(1,m.pos);
    m.speed.clear();
    m.seg = 0;
    m.along = 0.0f;
}

//A tick of its walk - Economy::Walk's, with no load to carry.
void WalkZombie(EconomyZombie& m, float dt){
    float left = dt;
    while (m.seg < (int)m.route.size() - 1 && left > 0.0f){
        vec2 a = m.route[m.seg];
        vec2 b = m.route[m.seg + 1];
        float len = (b - a).length();
        float v = std::max(0.05f,m.speed[m.seg]);
        float need = (len - m.along) / v;
        if (need <= left){
            left -= need;
            m.seg++;
            m.along = 0.0f;
            m.pos = b;
        }else{
            m.along += v * left;
            left = 0.0f;
            m.pos = a + (b - a) * (m.along / std::max(1e-4f,len));
        }
    }
}

}

const char* EconomyDropRefusal(const ChasmWorld& w, const ZoneState& z, int plot){
    int kind = z.KindOf(plot);
    if (z.ground[plot] == ZONE_GROUND_WALL || ZoneGateShut(z,plot)
        || (kind != ZONE_KIND_NONE && kind != ZONE_KIND_CAMP && !ZoneBridgeWalkable(z,plot))){
        return "something stands there";
    }
    if (w.terrain->wet[plot] && !ZoneBridgeWalkable(z,plot)){
        return "on the water";
    }
    if (w.terrain->Mountain(plot)){
        return "on the mountain";
    }
    return NULL;
}

bool Economy::SpawnZombies(int plot, int count, const ZoneState& z, std::string* why){
    auto refuse = [&](const char* text){
        if (why){
            *why = text;
        }
        return false;
    };
    if (!state.world || z.world != state.world){
        return refuse("no world");
    }
    if (count <= 0){
        //Every one off the map. Ids go on from where they were: never reused.
        if (!state.zombies.empty()){
            state.zombies.clear();
            state.version++;
        }
        return true;
    }
    const ChasmWorld& w = *state.world;
    if (plot < 0 || plot >= (int)z.ground.size() || plot >= (int)w.grid->fine.pos.size()){
        return refuse("no such plot");
    }
    //Where a person may be sent (Order): open ground, a road, a camp or a bridge - and not the mountain, which nothing walks.
    if (const char* bad = EconomyDropRefusal(w,z,plot)){
        return refuse(bad);
    }
    //Out from the plot's point along the golden angle's spiral, so no two stand on one spot.
    vec2 at = w.grid->fine.pos[plot];
    int n = std::min(count,(int)ZOMBIE_SPAWN_MOST);
    for (int i = 0; i < n; i++){
        float r = ZOMBIE_SPAWN_SPREAD * std::sqrt((float)i);
        float a = 2.3999632f * (float)i;
        EconomyZombie m;
        m.id = state.next_zombie++;
        m.pos = at + vec2(std::cos(a),std::sin(a)) * r;
        StopZombie(m);
        state.zombies.push_back(m);
    }
    state.version++;
    return true;
}

void Economy::TickZombies(const ZoneState& z, Walkers& walkers, uint64_t tick, float dt, bool f_zones_moved){
    if (f_zones_moved){
        ForgetHarm(z);
    }
    if (state.zombies.empty()){
        return;
    }
    //Something broken or breached last tick: every way through it changed, so every zombie looks again.
    bool f_broke = f_zombies_replan;
    f_zombies_replan = false;
    for (EconomyZombie& m : state.zombies){
        //Each on its own tick, by its id, so a wave's looks do not all fall on one; a lost one less often. Anything
        //built or taken down sends every one after a target looking again at once, as it re-plans the people's walks
        //(Replan): its way may be shut, or a shorter one open.
        uint64_t every = (uint64_t)ZOMBIE_LOOK_TICKS * (m.f_lost ? ZOMBIE_LOST_LOOKS : 1);
        bool f_replan = (f_zones_moved && m.target != 0) || f_broke;
        if ((tick + m.id) % every == 0 || f_replan){
            ZombieLook(m,z,walkers,f_replan);
        }
        WalkZombie(m,dt);
    }
    state.version++;    //they move every tick - the view redraws them
}

bool Economy::ZombieLook(EconomyZombie& m, const ZoneState& z, Walkers& walkers, bool f_replan){
    //Whom it may go for: whom it can get at (EconomyReachable), nearest first by the straight line; on a tie the lower id, as
    //the people are kept in id order.
    std::vector<std::pair<float,size_t>> near;
    for (size_t i = 0; i < state.workers.size(); i++){
        const EconomyWorker& k = state.workers[i];
        if (!CanGetAt(k)){
            continue;
        }
        near.push_back(std::make_pair((k.pos - m.pos).length(),i));
    }
    size_t tries = std::min(near.size(),(size_t)ZOMBIE_TRIES);
    std::partial_sort(near.begin(),near.begin() + tries,near.end());
    for (size_t t = 0; t < tries; t++){
        const EconomyWorker& k = state.workers[near[t].second];
        //Still after him, and its walk still ends by him - or at what it is breaking on its way to him: on as it was.
        if (k.id == m.target && !f_replan && !m.route.empty()
            && ((m.route.back() - k.pos).length() <= ZOMBIE_REACH + ZOMBIE_REPLAN || m.hit)){
            m.f_lost = false;
            return true;
        }
        if (ZombieRouteTo(m,z,walkers,k.pos)){
            m.target = k.id;
            m.f_lost = false;
            return true;
        }
    }
    //Nobody out, or nobody of the nearest it can get to: it stands where it is.
    m.f_lost = tries > 0;
    m.target = 0;
    m.hit = 0;
    m.hit_id = 0;
    StopZombie(m);
    return false;
}

/*
    Its way to `to`, as a person's (RouteTo): straight where the line is clear, otherwise the grid's A* from the plot
    it is on, as WALK_WHO_ZOMBIE, with a straight step at either end - at ZOMBIE_PACE of a person's speed on each
    leg. Then the end taken back ZOMBIE_REACH along it, so it stops an arm's length short of him.
*/
bool Economy::ZombieRouteTo(EconomyZombie& m, const ZoneState& z, Walkers& walkers, const vec2& to){
    const ChasmWorld& w = *state.world;
    GridPick here = w.picker->Pick(m.pos);
    GridPick there = w.picker->Pick(to);
    if (!here.f_hit || !there.f_hit || here.plot < 0 || there.plot < 0){
        return false;
    }
    std::vector<vec2> route;
    std::vector<float> speed;
    route.push_back(m.pos);
    uint8_t hit = 0;
    uint32_t hit_id = 0;
    //What stops it (WALLS BREAK, below): a standing palisade plot, a shut gate, a building's door not yet breached.
    auto stops = [&](int v){
        return ZoneWallStands(z,v) || ZoneGateShut(z,v)
               || (ZoneKindHasDoor(z.KindOf(v)) && z.standing[v] > 0 && !EconomyBreached(state,z.building[v]));
    };
    /*
        Stood IN what it is breaking - a palisade's or a gate's plot, which it hits from close to its stakes - its way is
        planned from the plot it came to it from, the nearest of that plot's neighbours that stops nothing: a walk may
        always leave the plot it starts on, so planned from there it would walk through the wall for nothing. From the
        plot beside it, it comes to the same wall again and hits on - or, its target gone back out, walks off.
    */
    int start = here.plot;
    if (stops(start)){
        int nb[8];
        int n = ZonePlotNeighbours(w,start,nb,8);
        float best = 1e30f;
        for (int i = 0; i < n; i++){
            float d = (w.grid->fine.pos[nb[i]] - m.pos).length();
            if (!stops(nb[i]) && d < best){
                best = d;
                start = nb[i];
            }
        }
    }
    if (start == here.plot && EconomyStraightClear(w,z,m.pos,to,here.plot,there.plot)){
        route.push_back(to);
        speed.push_back(ECONOMY_WORKER_SPEED * ZOMBIE_PACE);
    }else{
        WalkPlace from;
        from.plot = start;
        std::vector<WalkPlace> places;
        std::vector<float> edge_speed;
        ZombieBreaks breaks(state);
        if (!walkers.PlanRoute(z,from,there.plot,WALK_WHO_ZOMBIE,places,edge_speed,-1,&breaks)){
            return false;
        }
        /*
            WALLS BREAK: the first thing on the way it has to break - a standing palisade plot, a shut gate, a house it
            goes in at - ends its walk there, to hit it: at a palisade or a gate ZOMBIE_HIT_STAND off its plot's point,
            at a house's wall half the edge out and a little more. Broken, everyone looks again, and walks on through.
        */
        vec2 stand;
        size_t end = places.size();
        for (size_t i = 1; i < places.size() && !hit; i++){
            int a = places[i - 1].plot;
            int b = places[i].plot;
            if (ZoneWallStands(z,b)){
                hit = HARM_WALL;
                hit_id = (uint32_t)b;
            }else if (ZoneGateShut(z,b)){
                hit = HARM_GATE;
                hit_id = (uint32_t)b;
            }else if (stops(b) && z.building[b] != z.building[a]){
                hit = HARM_DOOR;
                hit_id = z.building[b];
            }
            if (hit){
                end = i;
                vec2 pa = w.grid->fine.pos[a];
                vec2 pb = w.grid->fine.pos[b];
                float l = std::max(1e-3f,(pa - pb).length());
                float r = (hit == HARM_DOOR) ? 0.5f * l + ZOMBIE_DOOR_STAND : ZOMBIE_HIT_STAND;
                stand = pb + (pa - pb) * std::min(1.0f,r / l);
            }
        }
        places.resize(end);
        //The path's first plot skipped when it lies behind it and the second is straight ahead (RouteTo's twitch).
        size_t first = 0;
        if (places.size() >= 2){
            vec2 p0 = w.grid->fine.pos[places[0].plot];
            vec2 p1 = w.grid->fine.pos[places[1].plot];
            if ((p0 - m.pos).dot(p1 - p0) < 0.0f && EconomyStraightClear(w,z,m.pos,p1,here.plot,places[1].plot)){
                first = 1;
            }
        }
        //Next to what it hits already: straight up to it, not back to its own plot's point first.
        if (hit && places.size() == 1){
            first = 1;
        }
        for (size_t i = first; i < places.size(); i++){
            vec2 pt = w.grid->fine.pos[places[i].plot];
            if ((pt - route.back()).length() > 1e-4f){
                float v = ((i == 0) ? ECONOMY_WORKER_SPEED : edge_speed[i - 1]) * ZOMBIE_PACE;
                //Over a garden's wall or a lot's fence: climbed, that leg ZOMBIE_CLIMB_SECONDS longer.
                if (i > 0 && ZoneBoundaryBetween(z,places[i - 1].plot,places[i].plot) != ZONE_BOUNDARY_NONE
                    && !ZoneGateBetween(w,z,places[i - 1].plot,places[i].plot)){
                    float len = (pt - route.back()).length();
                    v = len / (len / v + ZOMBIE_CLIMB_SECONDS);
                }
                speed.push_back(v);
                route.push_back(pt);
            }
        }
        vec2 last = hit ? stand : to;
        if ((last - route.back()).length() > 1e-4f){
            speed.push_back(ECONOMY_WORKER_SPEED * ZOMBIE_PACE);
            route.push_back(last);
        }
    }
    m.hit = hit;
    m.hit_id = hit_id;
    float cut = hit ? 0.0f : ZOMBIE_REACH;      //at what it hits, right up to it
    while (route.size() >= 2 && cut > 0.0f){
        vec2 a = route[route.size() - 2];
        vec2& b = route.back();
        float len = (b - a).length();
        if (len > cut){
            b = b + (a - b) * (cut / len);
            cut = 0.0f;
        }else{
            cut -= len;
            route.pop_back();
            speed.pop_back();
        }
    }
    m.route.swap(route);
    m.speed.swap(speed);
    m.seg = 0;
    m.along = 0.0f;
    return true;
}

/*
    MEALS (Economy.h): a zombie's body within `within` of his - for someone it can get at. Indoors, or up on a tower's
    platform, nothing reaches him, so a zombie at the door does not keep him from his meal - unless the door is down.
*/
bool Economy::ZombieNear(const EconomyWorker& k, float within) const{
    if (state.zombies.empty() || !CanGetAt(k)){
        return false;
    }
    vec2 at = EconomyBody(k);
    for (const EconomyZombie& m : state.zombies){
        if ((EconomyBody(m) - at).length() <= within){
            return true;
        }
    }
    return false;
}

/*
    A FIGHT (Economy.h), a tick of it: every zombie with whom it is after within FIGHT_REACH counts down to its next
    bite, and every grown-up with a zombie within that reach counts down to his next blow at the nearest. A count starts
    a whole interval off as they come within reach, and starts again whenever they part. All the bites and blows are
    found first and dealt after, so nobody strikes first; a person bitten to death still lands the blow he had coming.
    Then the zombies at 0 fall, and whom a bite took the last of are named for Needs (bitten_dead).
*/
void Economy::Fights(){
    //None left - the last fell, or all were cleared: nobody fights on.
    if (state.zombies.empty()){
        for (EconomyWorker& k : state.workers){
            k.swing = 0;
        }
        return;
    }
    std::vector<int> index_of(state.next_person,-1);
    for (size_t i = 0; i < state.workers.size(); i++){
        if (state.workers[i].id < index_of.size()){
            index_of[state.workers[i].id] = (int)i;
        }
    }
    std::vector<size_t> bites;                          //by worker index, one for each bite
    std::vector<int> blows(state.zombies.size(),0);     //by zombie, the hit points it loses
    for (EconomyZombie& m : state.zombies){
        int i = (m.target < index_of.size()) ? index_of[m.target] : -1;
        if (i < 0 || !CanGetAt(state.workers[i]) || (EconomyBody(state.workers[i]) - EconomyBody(m)).length() > FIGHT_REACH){
            m.bite = 0;
            continue;
        }
        if (m.bite <= 0){
            m.bite = Ticks(ZOMBIE_BITE_SECONDS);
        }else if (--m.bite == 0){
            m.bite = Ticks(ZOMBIE_BITE_SECONDS);
            bites.push_back((size_t)i);
        }
    }
    for (EconomyWorker& k : state.workers){
        //A child is bitten, but does not fight back.
        int near = -1;
        float nearest = FIGHT_REACH;
        if (CanGetAt(k) && EconomyCanOrder(k,now)){
            vec2 at = EconomyBody(k);
            for (size_t j = 0; j < state.zombies.size(); j++){
                float d = (EconomyBody(state.zombies[j]) - at).length();
                if (d <= FIGHT_REACH && (near < 0 || d < nearest)){
                    near = (int)j;
                    nearest = d;
                }
            }
        }
        if (near < 0){
            k.swing = 0;
            continue;
        }
        float pace = EconomyFightPace(k);
        if (k.swing <= 0){
            k.swing = Ticks(FIGHT_SWING_SECONDS / pace);
        }else if (--k.swing == 0){
            k.swing = Ticks(FIGHT_SWING_SECONDS / pace);
            blows[near] += (int)std::lround(FIGHT_HIT * pace);
        }
    }
    for (size_t i : bites){
        EconomyWorker& k = state.workers[i];
        k.health -= ZOMBIE_BITE;
        if (k.health <= 0 && std::find(bitten_dead.begin(),bitten_dead.end(),k.id) == bitten_dead.end()){
            k.health = 0;
            bitten_dead.push_back(k.id);
        }
    }
    for (size_t j = 0; j < state.zombies.size(); j++){
        state.zombies[j].health -= blows[j];
    }
    state.zombies.erase(std::remove_if(state.zombies.begin(),state.zombies.end(),[](const EconomyZombie& m){
        return m.health <= 0;
    }),state.zombies.end());
}

/*
    WALLS BREAK (Economy.h), a tick of it: every zombie at the end of its walk with something to break hits it - in id
    order, the first ZOMBIE_HITTERS at one thing and the rest standing by - a tick of its strength each. What reaches
    its strength breaks: a palisade plot goes back to a site (ground_breaks), and its harm with it; a gate's door hangs
    open (gate_breaks), BROKEN; a house is BREACHED. Each is told, and every zombie looks again on the next tick, when
    the zones have changed. One whose thing no longer stops it - broken by others, opened, taken down - looks again too.
*/
void Economy::ZombiesHit(const ZoneState& z){
    if (state.zombies.empty()){
        return;
    }
    const ChasmWorld& w = *state.world;
    auto stops = [&](int what, uint32_t id){
        if (what == HARM_WALL){
            return id < z.ground.size() && ZoneWallStands(z,(int)id);
        }
        if (what == HARM_GATE){
            return id < z.gate.size() && ZoneGateShut(z,(int)id);
        }
        return id < z.buildings.size() && ZoneKindHasDoor(z.buildings[id].kind) && z.buildings[id].size > 0
               && !EconomyBreached(state,id);
    };
    typedef std::pair<int,uint32_t> Thing;
    std::vector<std::pair<Thing,int>> at;       //this tick's hitters, by thing
    std::vector<Thing> broke;
    for (EconomyZombie& m : state.zombies){
        if (!m.hit){
            continue;
        }
        if (!stops(m.hit,m.hit_id)){
            m.hit = 0;
            m.hit_id = 0;
            f_zombies_replan = true;
            continue;
        }
        if (!EconomyZombieHitting(m)){
            continue;
        }
        Thing thing(m.hit,m.hit_id);
        auto it = std::find_if(at.begin(),at.end(),[&](const std::pair<Thing,int>& a){ return a.first == thing; });
        if (it == at.end()){
            at.push_back(std::make_pair(thing,0));
            it = at.end() - 1;
        }
        if (it->second >= ZOMBIE_HITTERS){
            continue;
        }
        it->second++;
        //Its harm, kept in what and id order, made as it is first hit.
        auto h = std::lower_bound(state.harm.begin(),state.harm.end(),thing,[](const EconomyHarm& a, const Thing& b){
            return (a.what != b.first) ? a.what < b.first : a.id < b.second;
        });
        if (h == state.harm.end() || h->what != thing.first || h->id != thing.second){
            EconomyHarm fresh;
            fresh.what = (uint8_t)thing.first;
            fresh.id = thing.second;
            h = state.harm.insert(h,fresh);
        }
        int strength = EconomyHarmStrength(thing.first);
        if (h->ticks >= strength){
            continue;
        }
        if (++h->ticks == strength){
            broke.push_back(thing);
        }
        state.version++;
    }
    for (const Thing& b : broke){
        if (b.first == HARM_WALL){
            ground_breaks.push_back((int)b.second);
            state.harm.erase(std::remove_if(state.harm.begin(),state.harm.end(),[&](const EconomyHarm& h){
                return h.what == HARM_WALL && h.id == b.second;
            }),state.harm.end());
            Event(EVENT_WALL_BROKEN,0,0,(int)b.second,w.grid->fine.pos[b.second]);
        }else if (b.first == HARM_GATE){
            gate_breaks.push_back((int)b.second);
            Event(EVENT_GATE_BROKEN,0,0,(int)b.second,w.grid->fine.pos[b.second]);
        }else{
            int plot = (b.second < home_plot.size()) ? home_plot[b.second] : -1;
            int kind = (b.second < z.buildings.size()) ? z.buildings[b.second].kind : 0;
            Event(EVENT_BREACHED,0,b.second,plot,(plot >= 0) ? w.grid->fine.pos[plot] : vec2(),kind);
        }
        for (EconomyZombie& m : state.zombies){
            if (m.hit == b.first && m.hit_id == b.second){
                m.hit = 0;
                m.hit_id = 0;
            }
        }
        f_zombies_replan = true;
    }
}

//Harm to what is no longer there to harm - a palisade plot taken down or unbuilt, a gate's road taken up, a house gone.
void Economy::ForgetHarm(const ZoneState& z){
    size_t before = state.harm.size();
    state.harm.erase(std::remove_if(state.harm.begin(),state.harm.end(),[&](const EconomyHarm& h){
        if (h.what == HARM_WALL){
            return !(h.id < z.ground.size() && ZoneWallStands(z,(int)h.id));
        }
        if (h.what == HARM_GATE){
            return !(h.id < z.gate.size() && z.gate[h.id] != ZONE_GATE_NONE);
        }
        return !(h.id < z.buildings.size() && ZoneKindHasDoor(z.buildings[h.id].kind) && z.buildings[h.id].size > 0);
    }),state.harm.end());
    if (state.harm.size() != before){
        state.version++;
    }
}
