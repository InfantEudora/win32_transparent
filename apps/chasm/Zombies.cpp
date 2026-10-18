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

namespace {

int Ticks(float seconds){
    return std::max(1,(int)std::lround(seconds * CALENDAR_TICKS_PER_SECOND));
}

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
    if (state.zombies.empty()){
        return;
    }
    for (EconomyZombie& m : state.zombies){
        //Each on its own tick, by its id, so a wave's looks do not all fall on one; a lost one less often. Anything
        //built or taken down sends every one after a target looking again at once, as it re-plans the people's walks
        //(Replan): its way may be shut, or a shorter one open.
        uint64_t every = (uint64_t)ZOMBIE_LOOK_TICKS * (m.f_lost ? ZOMBIE_LOST_LOOKS : 1);
        bool f_replan = f_zones_moved && m.target != 0;
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
        if (!EconomyReachable(k,now)){
            continue;
        }
        near.push_back(std::make_pair((k.pos - m.pos).length(),i));
    }
    size_t tries = std::min(near.size(),(size_t)ZOMBIE_TRIES);
    std::partial_sort(near.begin(),near.begin() + tries,near.end());
    for (size_t t = 0; t < tries; t++){
        const EconomyWorker& k = state.workers[near[t].second];
        //Still after him, and its walk still ends by him: on as it was.
        if (k.id == m.target && !f_replan && !m.route.empty()
            && (m.route.back() - k.pos).length() <= ZOMBIE_REACH + ZOMBIE_REPLAN){
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
    if (EconomyStraightClear(w,z,m.pos,to,here.plot,there.plot)){
        route.push_back(to);
        speed.push_back(ECONOMY_WORKER_SPEED * ZOMBIE_PACE);
    }else{
        WalkPlace from;
        from.plot = here.plot;
        std::vector<WalkPlace> places;
        std::vector<float> edge_speed;
        if (!walkers.PlanRoute(z,from,there.plot,WALK_WHO_ZOMBIE,places,edge_speed)){
            return false;
        }
        //The path's first plot skipped when it lies behind it and the second is straight ahead (RouteTo's twitch).
        size_t first = 0;
        if (places.size() >= 2){
            vec2 p0 = w.grid->fine.pos[places[0].plot];
            vec2 p1 = w.grid->fine.pos[places[1].plot];
            if ((p0 - m.pos).dot(p1 - p0) < 0.0f && EconomyStraightClear(w,z,m.pos,p1,here.plot,places[1].plot)){
                first = 1;
            }
        }
        for (size_t i = first; i < places.size(); i++){
            vec2 pt = w.grid->fine.pos[places[i].plot];
            if ((pt - route.back()).length() > 1e-4f){
                speed.push_back(((i == 0) ? ECONOMY_WORKER_SPEED : edge_speed[i - 1]) * ZOMBIE_PACE);
                route.push_back(pt);
            }
        }
        if ((to - route.back()).length() > 1e-4f){
            speed.push_back(ECONOMY_WORKER_SPEED * ZOMBIE_PACE);
            route.push_back(to);
        }
    }
    float cut = ZOMBIE_REACH;
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
    platform, nothing reaches him, so a zombie at the door does not keep him from his meal.
*/
bool Economy::ZombieNear(const EconomyWorker& k, float within) const{
    if (state.zombies.empty() || !EconomyReachable(k,now)){
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
        if (i < 0 || !EconomyReachable(state.workers[i],now) || (EconomyBody(state.workers[i]) - EconomyBody(m)).length() > FIGHT_REACH){
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
        if (EconomyReachable(k,now) && EconomyCanOrder(k,now)){
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
