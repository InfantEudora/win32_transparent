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

vec2 EconomyZombieLooks(const EconomyState& e, const EconomyZombie& m){
    //Stood by whom it is after - pushed round him by the others (SPACING) - it faces him, not the way it came. At
    //something it hits it faces that: its last leg's way, which ends at it (walls break, step 4).
    if (m.target && m.seg >= (int)m.route.size() - 1 && !m.hit){
        for (const EconomyWorker& k : e.workers){
            vec2 to = EconomyBody(k) - EconomyBody(m);
            if (k.id == m.target && to.length() > 1e-3f){
                return to * (1.0f / to.length());
            }
        }
    }
    return EconomyZombieFacing(m);
}

vec2 EconomyWorkerLooks(const EconomyState& e, const EconomyWorker& k){
    //Aboard a zeppelin (flight_plan.md step 2): out over its rail, the side he stands on.
    if (k.state == WORKER_ABOARD){
        if (const EconomyZeppelin* f = EconomyZeppelinById(e,k.zeppelin)){
            vec2 nose(cosf(f->heading),sinf(f->heading));
            vec2 side(-nose.y,nose.x);
            return ((k.pos - f->pos).dot(side) >= 0.0f) ? side : side * -1.0f;
        }
    }
    //Shooting at a zombie (step 3): he faces it, and the Shoot clip turns him side-on by itself.
    if (k.aim){
        for (const EconomyZombie& m : e.zombies){
            vec2 to = EconomyBody(m) - EconomyBody(k);
            if (m.id == k.aim && to.length() > 1e-3f){
                return to * (1.0f / to.length());
            }
        }
    }
    //Fighting one by hand (step 2, his blow counting down): he faces the nearest in reach, the one he strikes at - and
    //so falls back from a bite from it (step 6).
    if (k.swing > 0){
        float nearest = FIGHT_REACH;
        vec2 to_it;
        for (const EconomyZombie& m : e.zombies){
            vec2 to = EconomyBody(m) - EconomyBody(k);
            float d = to.length();
            if (d <= nearest && d > 1e-3f){
                nearest = d;
                to_it = to * (1.0f / d);
            }
        }
        if (to_it.dot(to_it) > 0.5f){
            return to_it;
        }
    }
    vec2 facing = EconomyWorkerFacing(k);
    return (facing.dot(facing) < 1e-8f) ? vec2(0.0f,1.0f) : facing;
}

//--- The fallen (Economy.h) -------------------------------------------------------------------------------------------

namespace {

uint64_t SecondsTicks(float seconds){
    return (uint64_t)std::max(1L,std::lround(seconds * CALENDAR_TICKS_PER_SECOND));
}

}

uint64_t EconomyFallenTicks(const EconomyFallen& f){
    if (!f.f_person){
        return (uint64_t)FALLEN_ZOMBIE_DAYS * CALENDAR_DAY_TICKS;
    }
    return SecondsTicks(FALLEN_PERSON_LIE_SECONDS + FALLEN_PERSON_SINK_SECONDS + FALLEN_PERSON_ANGEL_SECONDS);
}

float EconomyFallenSinking(const EconomyFallen& f, uint64_t now){
    uint64_t age = (now > f.tick) ? now - f.tick : 0;
    uint64_t from = f.f_person ? SecondsTicks(FALLEN_PERSON_LIE_SECONDS)
                               : (uint64_t)(FALLEN_ZOMBIE_DAYS - FALLEN_ZOMBIE_SINK_DAYS) * CALENDAR_DAY_TICKS;
    uint64_t over = f.f_person ? SecondsTicks(FALLEN_PERSON_SINK_SECONDS) : (uint64_t)FALLEN_ZOMBIE_SINK_DAYS * CALENDAR_DAY_TICKS;
    if (age <= from){
        return 0.0f;
    }
    return std::min(1.0f,(float)(age - from) / (float)over);
}

float EconomyFallenRising(const EconomyFallen& f, uint64_t now){
    if (!f.f_person){
        return -1.0f;
    }
    uint64_t age = (now > f.tick) ? now - f.tick : 0;
    //Indoors there is no body to sink: the angel rises from the house once he would have.
    uint64_t from = SecondsTicks(FALLEN_PERSON_LIE_SECONDS + FALLEN_PERSON_SINK_SECONDS);
    if (age < from){
        return -1.0f;
    }
    return std::min(1.0f,(float)(age - from) / (float)SecondsTicks(FALLEN_PERSON_ANGEL_SECONDS));
}

/*
    Which way he falls: struck along the way he faced - from behind - forward, against it backward; with no blow, forward,
    a collapse. He keeps facing that way (the user's): the clip carries him down along it.
*/
void Economy::ZombieFalls(const EconomyZombie& m, const vec2& struck){
    EconomyFallen f;
    f.id = m.id;
    f.pos = EconomyBody(m);
    f.facing = EconomyZombieLooks(state,m);
    f.f_back = struck.dot(f.facing) < 0.0f;
    f.layer = WALK_GROUND;
    f.tick = now;
    state.fallen.push_back(f);
    state.version++;
}

void Economy::PersonFalls(const EconomyWorker& k, const vec2* struck){
    EconomyFallen f;
    f.id = k.id;
    f.f_person = true;
    f.pos = EconomyBody(k);
    f.facing = EconomyWorkerLooks(state,k);
    f.f_back = struck && struck->dot(f.facing) < 0.0f;
    f.f_indoors = EconomyIndoors(k);
    f.house = f.f_indoors ? k.house : 0;
    f.layer = (int8_t)EconomyWorkerLayer(k);
    f.tick = now;
    state.fallen.push_back(f);
    state.version++;
}

/*
    KILLS (ranks_plan.md): counted on whoever's hit felled it, or on his record among the dead when his arrow landed
    after him, and told where it fell - for the day's count (THE HISTORY) and the sound to come. Told only once counted,
    so the kills and the count agree (ChasmStatsCheck). A kill that takes the living to a new RANK is told as well; the
    dead's rank is read from their kills all the same, but nobody rises after his death.
*/
void Economy::ZombieKilled(const EconomyZombie& m, uint32_t by, int how){
    if (how < 0 || how >= KILL_HOWS){
        return;
    }
    std::array<int,KILL_HOWS>* kills = NULL;
    const EconomyWorker* living = NULL;
    for (EconomyWorker& k : state.workers){
        if (k.id == by){
            kills = &k.kills;
            living = &k;
        }
    }
    for (EconomyDeath& d : state.dead){
        kills = (!kills && d.id == by) ? &d.kills : kills;
    }
    if (!kills){
        return;
    }
    int rank = EconomyRank(*kills);
    (*kills)[how]++;
    Event(EVENT_ZOMBIE_FELLED,by,0,-1,EconomyBody(m),how);
    if (living && EconomyRank(*kills) > rank){
        Event(EVENT_RANKED,by,0,-1,EconomyBody(*living),EconomyRank(*kills));
    }
}

void Economy::TickFallen(){
    size_t before = state.fallen.size();
    state.fallen.erase(std::remove_if(state.fallen.begin(),state.fallen.end(),[&](const EconomyFallen& f){
        return now >= f.tick + EconomyFallenTicks(f);
    }),state.fallen.end());
    if (state.fallen.size() != before){
        state.version++;
    }
}

float EconomyFightPace(const EconomyWorker& k){
    return 0.5f * (EconomySkillFactor(k.skills[SKILL_STRENGTH]) + EconomySkillFactor(k.skills[SKILL_INGENUITY]));
}

bool EconomyReachable(const EconomyWorker& k, uint64_t tick){
    //Aboard a zeppelin (flight_plan.md step 2): up on its deck, out of every zombie's reach.
    return !EconomyIndoors(k) && k.state != WORKER_ABOARD && !(k.with && EconomyStageAt(k.born,tick) == LIFE_BABY)
           && EconomyWorkerLayer(k) == WALK_GROUND;
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

//A tick of its walk - Economy::Walk's, with no load to carry, and slowed behind one ahead of it (SPACING, following).
void WalkZombie(EconomyZombie& m, float dt){
    float left = dt;
    while (m.seg < (int)m.route.size() - 1 && left > 0.0f){
        vec2 a = m.route[m.seg];
        vec2 b = m.route[m.seg + 1];
        float len = (b - a).length();
        float v = std::max(0.05f,m.speed[m.seg] * m.crowd);
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
        //Every one off the map, and the fallen ones with them (step 6) - the people's dead are left. Ids go on from where
        //they were: never reused.
        size_t lying = state.fallen.size();
        state.fallen.erase(std::remove_if(state.fallen.begin(),state.fallen.end(),[](const EconomyFallen& f){
            return !f.f_person;
        }),state.fallen.end());
        if (!state.zombies.empty() || state.fallen.size() != lying){
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
    //Out from the plot's point along the golden angle's spiral, so no two stand on one spot - the first half a turn
    //out too, or it and the second would start inside each other.
    vec2 at = w.grid->fine.pos[plot];
    int n = std::min(count,(int)ZOMBIE_SPAWN_MOST);
    for (int i = 0; i < n; i++){
        float r = ZOMBIE_SPAWN_SPREAD * std::sqrt((float)i + 0.5f);
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

//--- The first wave (Economy.h; swamp_huts_plan.md step 2) -----------------------------------------------------------

void Economy::CallWave(uint64_t met){
    if (state.wave.due){
        return;
    }
    //Dawn is the day's first tick. The swamp is frozen in winter (gameplay_plan.md): a dawn then waits for spring's first.
    uint64_t due = (met / CALENDAR_DAY_TICKS + WAVE_AFTER_DAWNS) * (uint64_t)CALENDAR_DAY_TICKS;
    if (CalendarDateOf(due).season == SEASON_WINTER){
        due = (due / CALENDAR_YEAR_TICKS + 1) * (uint64_t)CALENDAR_YEAR_TICKS;
    }
    state.wave.due = std::max(due,(uint64_t)1);
    state.version++;
}

void Economy::WaveSeen(const vec2& at){
    if (!state.wave.f_open || state.wave.f_seen){
        return;
    }
    state.wave.f_seen = true;
    Event(EVENT_WAVE_SEEN,0,0,-1,at);
    state.version++;
}

void Economy::TickWave(const ZoneState& z){
    EconomyWave& wave = state.wave;
    if (!wave.due || !state.world || z.world != state.world || now < wave.due){
        return;
    }
    const ChasmWorld& w = *state.world;
    //Its fallen struck off: the ones it let out that stand no more - shot, struck down, or taken off by the debug tools.
    size_t standing = wave.zombies.size();
    wave.zombies.erase(std::remove_if(wave.zombies.begin(),wave.zombies.end(),[&](uint32_t id){
        return std::none_of(state.zombies.begin(),state.zombies.end(),[id](const EconomyZombie& m){ return m.id == id; });
    }),wave.zombies.end());
    if (wave.zombies.size() != standing){
        state.version++;
    }
    const uint64_t trickle = (uint64_t)Ticks(HUT_TRICKLE_SECONDS);
    if (!wave.f_open){
        //The doors open: every hut there is, by its id, each on its own beat.
        wave.f_open = true;
        for (size_t v = 0; v < z.building.size(); v++){
            uint32_t id = z.building[v];
            if (id && id < z.buildings.size() && z.buildings[id].kind == ZONE_KIND_HUT){
                EconomyWaveHut h;
                h.hut = id;
                h.door = ZoneHutDoor(w,(int)v);
                h.left = WAVE_FIRST_PER_HUT;
                wave.huts.push_back(h);
            }
        }
        std::sort(wave.huts.begin(),wave.huts.end(),[](const EconomyWaveHut& a, const EconomyWaveHut& b){
            return a.hut < b.hut;
        });
        for (size_t i = 0; i < wave.huts.size(); i++){
            wave.huts[i].next = now + (uint64_t)Ticks(HUT_BEAT_SECONDS) * i;
        }
        if (EconomyWaveLeft(wave) > 0){
            Event(EVENT_WAVE,0,wave.huts[0].hut,wave.huts[0].door,vec2(),EconomyWaveLeft(wave));
        }
        state.version++;
    }
    for (EconomyWaveHut& h : wave.huts){
        if (h.left <= 0 || now < h.next){
            continue;
        }
        h.next = now + trickle;
        //Out at its door - or, with something standing there now, the nearest open plot round the hut; with none, a
        //trickle later.
        int hut = -1;
        for (size_t v = 0; v < z.building.size() && hut < 0; v++){
            hut = (z.building[v] == h.hut) ? (int)v : -1;
        }
        std::vector<int> ways;
        if (h.door >= 0){
            ways.push_back(h.door);
        }
        if (hut >= 0){
            int nb[8];
            int k = ZonePlotNeighbours(w,hut,nb,8);
            ways.insert(ways.end(),nb,nb + k);
        }
        for (int plot : ways){
            if (plot >= 0 && plot < (int)z.ground.size() && !EconomyDropRefusal(w,z,plot) && SpawnZombies(plot,1,z,NULL)){
                wave.zombies.push_back(state.next_zombie - 1);
                h.left--;
                break;
            }
        }
        //...and a hut taken down meanwhile lets out no more.
        h.left = (hut < 0) ? 0 : h.left;
        state.version++;
    }
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
    }
    //A CROWD KEEPS ITS ROOM: each its place, from whom they are all after and where they all stand - then they walk.
    ZombiePlaces(z);
    for (EconomyZombie& m : state.zombies){
        WalkZombie(m,dt);
    }
    state.version++;    //they move every tick - the view redraws them
}

namespace {

//A CROWD KEEPS ITS ROOM: how far out ring `ring` of the places round a person is - the first where it bites from.
float RingRadius(int ring){
    return ZOMBIE_REACH + ZOMBIE_SLOT_GAP * (float)std::max(0,ring);
}

}

bool Economy::ZombieLook(EconomyZombie& m, const ZoneState& z, Walkers& walkers, bool f_replan){
    /*
        Whom it may go for: whom it can get at (EconomyReachable), by the straight line - and by the crowd there (A CROWD
        KEEPS ITS ROOM): ZOMBIE_CROWDED for every zombie after him nearer him than it is, the queue it would stand at the
        back of, and its own target ZOMBIE_KEEP_TARGET nearer. The best few tried until one can be walked to; on a tie the
        lower id, as the people are kept in id order.
    */
    std::vector<std::pair<float,size_t>> near;
    for (size_t i = 0; i < state.workers.size(); i++){
        const EconomyWorker& k = state.workers[i];
        if (!CanGetAt(k)){
            continue;
        }
        float d = (k.pos - m.pos).length();
        int ahead = 0;
        for (const EconomyZombie& o : state.zombies){
            if (&o != &m && o.target == k.id){
                float od = (k.pos - o.pos).length();
                ahead += (od < d || (od == d && o.id < m.id)) ? 1 : 0;
            }
        }
        float score = d + ZOMBIE_CROWDED * (float)ahead - ((k.id == m.target) ? ZOMBIE_KEEP_TARGET : 0.0f);
        near.push_back(std::make_pair(score,i));
    }
    size_t tries = std::min(near.size(),(size_t)ZOMBIE_TRIES);
    std::partial_sort(near.begin(),near.begin() + tries,near.end());
    for (size_t t = 0; t < tries; t++){
        const EconomyWorker& k = state.workers[near[t].second];
        //Still after him, and its walk still ends by him - at its place round him, which follows him (ZombiePlaces) - or
        //at what it is breaking on its way to him: on as it was.
        if (k.id == m.target && !f_replan && !m.route.empty()
            && ((m.route.back() - k.pos).length() <= RingRadius(m.ring) + ZOMBIE_REPLAN || m.hit)){
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
    vec2 stand;
    vec2 hit_from;
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
                hit_from = pa;
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
    m.aim = hit ? stand : vec2();
    m.from = hit ? hit_from : vec2();
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

namespace {

//A place (A CROWD KEEPS ITS ROOM): where, its ring or row, whether it is to be had, and which of its group took it.
struct Place{
    vec2 at;
    int ring = 0;
    int ok = -1;            //-1 not yet asked
    int taken = -1;
};

//...a zombie and a place, what the one costs the other.
struct PlaceCost{
    float cost;
    uint32_t id;
    int place;
    int member;
};

//The places of ring `ring` round `centre`, evenly round it - every other ring half a place on, so that none lines up
//with the ring inside it.
void RingPlaces(const vec2& centre, int ring, std::vector<Place>& out){
    float r = RingRadius(ring);
    int n = std::max(1,(int)(6.2831853f * r / ZOMBIE_SLOT_GAP));
    for (int i = 0; i < n; i++){
        float a = 6.2831853f * ((float)i + ((ring & 1) ? 0.5f : 0.0f)) / (float)n;
        Place p;
        p.at = centre + vec2(std::cos(a),std::sin(a)) * r;
        p.ring = ring;
        out.push_back(p);
    }
}

//...and of row `row` at what it breaks: across `back` - from it toward where they came from - and `row` places further
//back than where the first stands, ZOMBIE_HITTERS in the first and one more every row behind, so that the rows stagger.
void RowPlaces(const vec2& aim, const vec2& back, int row, std::vector<Place>& out){
    vec2 across(-back.y,back.x);
    int n = ZOMBIE_HITTERS + row;
    for (int i = 0; i < n; i++){
        Place p;
        p.at = aim + back * (ZOMBIE_SLOT_GAP * (float)row) + across * (ZOMBIE_SLOT_GAP * ((float)i - 0.5f * (float)(n - 1)));
        p.ring = row;
        out.push_back(p);
    }
}

/*
    Its walk ended at its place `at`, wherever its route lets it go straight there: its last leg's end moved - that leg
    from where it is now, if it is on it, so it is moved nowhere - and the turn before that cut where it can go straight
    on from the leg before, not round by his plot's point beyond him. Stood, it walks there once it is ZOMBIE_SLOT_SLACK
    off. `end_plot`, a plot the walk may end in though it stops a walker: the palisade it hits.
*/
void EndAt(EconomyZombie& m, const vec2& at, const ChasmWorld& w, const ZoneState& z, int end_plot){
    int last = (int)m.route.size() - 1;
    if (last < 0 || (m.route[last] - at).length() < 1e-4f){
        return;
    }
    auto clear = [&](const vec2& a){
        GridPick p = w.picker->Pick(a);
        return p.f_hit && p.plot >= 0 && EconomyStraightClear(w,z,a,at,p.plot,end_plot);
    };
    if (m.seg >= last){
        if ((at - m.pos).length() >= ZOMBIE_SLOT_SLACK && clear(m.pos)){
            m.route.assign(1,m.pos);
            m.route.push_back(at);
            m.speed.assign(1,ECONOMY_WORKER_SPEED * ZOMBIE_PACE);
            m.seg = 0;
            m.along = 0.0f;
        }
        return;
    }
    if (m.seg >= last - 2){
        m.route[m.seg] = m.pos;
        m.along = 0.0f;
    }
    if (last >= 2 && m.seg <= last - 2 && clear(m.route[last - 2])){
        m.route.erase(m.route.begin() + (last - 1));
        m.speed.erase(m.speed.begin() + (last - 1));
        m.route[last - 1] = at;
        return;
    }
    if (clear(m.route[last - 1])){
        m.route[last] = at;
    }
}

}

/*
    A CROWD KEEPS ITS ROOM (Economy.h), a tick of it: every zombie a place of its own, handed out afresh from where they
    all stand now, so a crowd reshapes as its people move and its zombies fall. Round whom it is after, rings of them,
    from where he stands; at what it breaks, rows across the way its lowest id came. Each target's places go to its
    zombies cheapest first - the way there, an inner ring before an outer, the one held kept - and are had only where a
    walk could take it from him (or from the plot it hits from), on open ground, nearer nobody else than its reach, and
    not on a place another target's zombies took this tick: people first, then what is broken, each in id order. With
    none, a zombie keeps the end its walk was planned to.
*/
void Economy::ZombiePlaces(const ZoneState& z){
    const ChasmWorld& w = *state.world;
    std::vector<int> was(state.zombies.size());
    for (size_t i = 0; i < state.zombies.size(); i++){
        was[i] = state.zombies[i].ring;
        state.zombies[i].ring = -1;
    }
    //Whom each is after or what it breaks, [HARM_* << 32 | id, its index] - people (0) first, everyone in id order.
    std::vector<std::pair<uint64_t,size_t>> groups;
    for (size_t i = 0; i < state.zombies.size(); i++){
        const EconomyZombie& m = state.zombies[i];
        if (m.hit){
            groups.emplace_back(((uint64_t)m.hit << 32) | m.hit_id,i);
        }else if (m.target){
            groups.emplace_back((uint64_t)m.target,i);
        }
    }
    if (groups.empty()){
        return;
    }
    std::sort(groups.begin(),groups.end());
    //Where everyone out of doors on the ground stands, whom no place may be on.
    std::vector<std::pair<uint32_t,vec2>> people;
    for (const EconomyWorker& k : state.workers){
        if (EconomyReachable(k,now)){
            people.emplace_back(k.id,k.pos);
        }
    }
    std::vector<vec2> taken;        //this tick's places, every target's
    std::vector<Place> places;
    std::vector<PlaceCost> costs;
    std::vector<size_t> members;
    for (size_t g = 0; g < groups.size(); ){
        uint64_t key = groups[g].first;
        members.clear();
        for (; g < groups.size() && groups[g].first == key; g++){
            members.push_back(groups[g].second);
        }
        //What it is laid out round, and what a walk to a place is tested from.
        bool f_rows = (key >> 32) != 0;
        uint32_t person = f_rows ? 0 : (uint32_t)key;
        int end_plot = -1;
        vec2 base;
        vec2 aim;
        vec2 back;
        if (!f_rows){
            const EconomyWorker* k = NULL;
            for (const EconomyWorker& o : state.workers){
                k = (o.id == person) ? &o : k;
            }
            if (!k || !CanGetAt(*k)){
                continue;
            }
            base = k->pos;
        }else{
            const EconomyZombie& f = state.zombies[members[0]];
            back = f.from - f.aim;
            if (back.length() < 1e-3f){
                back = vec2() - EconomyZombieFacing(f);
            }
            if (back.length() < 1e-3f){
                continue;
            }
            back = back * (1.0f / back.length());
            aim = f.aim;
            base = f.from;
            int what = (int)(key >> 32);
            end_plot = (what == HARM_WALL || what == HARM_GATE) ? (int)(uint32_t)key : -1;
        }
        GridPick here = w.picker->Pick(base);
        if (!here.f_hit || here.plot < 0){
            continue;
        }
        auto open = [&](const Place& p){
            for (const std::pair<uint32_t,vec2>& o : people){
                if (o.first != person && (o.second - p.at).length() < ZOMBIE_REACH){
                    return false;
                }
            }
            for (const vec2& t : taken){
                if ((t - p.at).length() < 0.9f * ZOMBIE_SLOT_GAP){
                    return false;
                }
            }
            return EconomyStraightClear(w,z,base,p.at,here.plot,end_plot);
        };
        places.clear();
        std::vector<int> got(members.size(),-1);
        size_t left = members.size();
        size_t first = 0;           //the first ring's places: as many as are kept to spare to choose from
        int rings = 0;
        while (left > 0 && rings < ZOMBIE_SLOT_RINGS){
            //Rings added until there are places enough to choose from - the first ring's worth over - then handed out.
            size_t free = 0;
            do{
                if (f_rows){
                    RowPlaces(aim,back,rings,places);
                }else{
                    RingPlaces(base,rings,places);
                }
                first = (rings == 0) ? places.size() : first;
                rings++;
                free = 0;
                for (const Place& p : places){
                    free += (p.taken < 0 && p.ok != 0) ? 1 : 0;
                }
            }while (rings < ZOMBIE_SLOT_RINGS && free < left + first);
            costs.clear();
            for (size_t a = 0; a < members.size(); a++){
                if (got[a] >= 0){
                    continue;
                }
                const EconomyZombie& m = state.zombies[members[a]];
                for (size_t b = 0; b < places.size(); b++){
                    const Place& p = places[b];
                    if (p.taken >= 0 || p.ok == 0){
                        continue;
                    }
                    bool f_held = was[members[a]] == p.ring && (m.route.back() - p.at).length() < 0.5f * ZOMBIE_SLOT_GAP;
                    float cost = (m.pos - p.at).length() + ZOMBIE_RING_COST * (float)p.ring - (f_held ? ZOMBIE_SLOT_KEEP : 0.0f);
                    costs.push_back(PlaceCost{cost,m.id,(int)b,(int)a});
                }
            }
            std::sort(costs.begin(),costs.end(),[](const PlaceCost& x, const PlaceCost& y){
                if (x.cost != y.cost){
                    return x.cost < y.cost;
                }
                return (x.id != y.id) ? x.id < y.id : x.place < y.place;
            });
            for (const PlaceCost& c : costs){
                Place& p = places[c.place];
                if (got[c.member] >= 0 || p.taken >= 0){
                    continue;
                }
                if (p.ok < 0){
                    p.ok = open(p) ? 1 : 0;
                }
                if (p.ok == 0){
                    continue;
                }
                p.taken = c.member;
                got[c.member] = c.place;
                left--;
            }
        }
        for (size_t a = 0; a < members.size(); a++){
            if (got[a] < 0){
                continue;
            }
            EconomyZombie& m = state.zombies[members[a]];
            const Place& p = places[got[a]];
            m.ring = (int8_t)p.ring;
            EndAt(m,p.at,w,z,end_plot);
            taken.push_back(p.at);
        }
    }
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
    Then the zombies at 0 fall (THE FALLEN, by the last blow, which is its striker's kill - KILLS), and whom a bite
    took the last of are named for Needs (bitten_dead, with the way that bite pushed him).
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
    std::vector<std::pair<size_t,vec2>> bites;          //by worker index, one for each bite, and where it came from
    std::vector<int> blows(state.zombies.size(),0);     //by zombie, the hit points it loses...
    std::vector<vec2> blown(state.zombies.size());      //...and the way the last blow pushed it (THE FALLEN)...
    std::vector<uint32_t> striker(state.zombies.size(),0);  //...and who struck that blow: the kill, if it falls (KILLS)
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
            bites.emplace_back((size_t)i,EconomyBody(m));
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
        //His RANK makes his blows come sooner, no harder (RANKS).
        float swing = FIGHT_SWING_SECONDS / (pace * EconomyRankPace(EconomyRank(k.kills)));
        if (k.swing <= 0){
            k.swing = Ticks(swing);
        }else if (--k.swing == 0){
            k.swing = Ticks(swing);
            blows[near] += (int)std::lround(FIGHT_HIT * pace);
            blown[near] = EconomyBody(state.zombies[near]) - EconomyBody(k);
            striker[near] = k.id;
        }
    }
    for (const std::pair<size_t,vec2>& b : bites){
        EconomyWorker& k = state.workers[b.first];
        k.health -= ZOMBIE_BITE;
        if (k.health <= 0 && std::find(bitten_dead.begin(),bitten_dead.end(),k.id) == bitten_dead.end()){
            k.health = 0;
            bitten_dead.push_back(k.id);
            bitten_struck.emplace_back(k.id,EconomyBody(k) - b.second);
        }
    }
    for (size_t j = 0; j < state.zombies.size(); j++){
        EconomyZombie& m = state.zombies[j];
        bool f_alive = m.health > 0;
        m.health -= blows[j];
        if (f_alive && m.health <= 0){
            ZombieFalls(m,blown[j]);
            ZombieKilled(m,striker[j],KILL_HAND);
        }
    }
    state.zombies.erase(std::remove_if(state.zombies.begin(),state.zombies.end(),[](const EconomyZombie& m){
        return m.health <= 0;
    }),state.zombies.end());
}

/*
    WALLS BREAK (Economy.h), a tick of it: every zombie at the end of its walk with something to break hits it - in id
    order, the first ZOMBIE_HITTERS at one thing and the rest standing by - a tick of its strength each. What reaches
    its strength breaks: a palisade plot goes back to a site (ground_breaks), BROKEN; a gate's door hangs open
    (gate_breaks), BROKEN; a house is BREACHED - each keeping its harm at its strength to say so. Each is told, and
    every zombie looks again on the next tick, when the zones have changed. One whose thing no longer stops it - broken
    by others, opened, taken down - looks again too.
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

/*
    Harm to what is no longer there to harm - a palisade plot taken down or unbuilt, a gate's road taken up, a house gone
    - and a BROKEN palisade plot's once it stands again: built anew, it is whole (zombies_plan.md, step 4d; until 4c
    makes that the builders', the line works build it as any wall site).
*/
void Economy::ForgetHarm(const ZoneState& z){
    size_t before = state.harm.size();
    state.harm.erase(std::remove_if(state.harm.begin(),state.harm.end(),[&](const EconomyHarm& h){
        if (h.what == HARM_WALL){
            if (!(h.id < z.ground.size() && z.ground[h.id] == ZONE_GROUND_WALL)){
                return true;
            }
            bool f_stands = ZoneWallStands(z,(int)h.id);
            return (h.ticks >= EconomyHarmStrength(HARM_WALL)) ? f_stands : !f_stands;
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
