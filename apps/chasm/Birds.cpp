#include "Birds.h"

#include <math.h>
#include <algorithm>

/*
    BIRDS (Birds.h, docs/birds_plan.md). How a flock moves:

      SETTLED   every bird on its perch, fidgeting - a head turned, a turn on the spot, a hop to a free perch.
                Restless after a while, it TRAVELS to another roost near its home; scared, it SCATTERS.
      TRAVEL    the anchor flies to the next roost at the cruising height, clear of the tallest tree, coming down
                no faster than BIRD_SINK_RATE so a dip in the ground is not followed into it; then CIRCLE.
      CIRCLE    round the roost once or so, lower each time - the birds come down to their perches from that.
      SCATTER   every bird away from the scare on a line of its own, beating hard; then they flock again and
                TRAVEL to a roost clear of where they were scared, which they keep away from for BIRD_WARY_S.

    In the air, a flock GATHERS to the season's size: a few birds at a time fly in to join it, or one leaves.

    A bird in flight steers for its place round the anchor - a slot that drifts slowly - with a turn rate rather than
    a spring, so it banks round rather than snapping; birds too close push apart. Its wings beat in bursts and glide
    between them unless it is climbing, slow or scared. A bird comes down to its perch from above and flares before
    it folds.
*/

namespace {

uint32_t Mix(uint32_t x){
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float Clamp(float v, float lo, float hi){
    return v < lo ? lo : (v > hi ? hi : v);
}

//The way round from a to b, -pi to pi.
float AngleTo(float a, float b){
    float d = fmodf(b - a + 3.14159265f,6.2831853f);
    if (d < 0.0f){
        d += 6.2831853f;
    }
    return d - 3.14159265f;
}

vec2 Flat(const vec3& v){
    return vec2(v.x,v.z);
}

float Mid(float lo, float hi, float r){
    return lo + (hi - lo) * r;
}

}

void BirdSky::Reset(uint32_t s){
    seed = Mix(s ^ 0xB12D5u);
    next_id = 1;
    next_bird = 1;
    flocks.clear();
    calls.clear();
}

//A number in [0,1) for this flock's next choice: the sky's seed, the flock and its count of draws - never RRandom.
float BirdSky::Draw(BirdFlock& f, uint32_t salt){
    uint32_t h = Mix(seed ^ Mix(f.id * 0x9E3779B9u ^ Mix(f.draws++ * 0x85EBCA6Bu + salt)));
    return (float)(h >> 8) * (1.0f / 16777216.0f);
}

int BirdSky::Count() const{
    int n = 0;
    for (const BirdFlock& f : flocks){
        n += (int)f.birds.size();
    }
    return n;
}

void BirdSky::TakeCalls(std::vector<BirdCall>& out){
    out.insert(out.end(),calls.begin(),calls.end());
    calls.clear();
}

float BirdSky::SitTime(BirdFlock& f){
    return (f.role == BIRD_ROLE_WANDER) ? Mid(BIRD_WANDER_SIT_MIN_S,BIRD_WANDER_SIT_MAX_S,Draw(f,1))
                                        : Mid(BIRD_RESTLESS_MIN_S,BIRD_RESTLESS_MAX_S,Draw(f,1));
}

//One bird of the flock calling, where it is: the app hears it or not (TakeCalls).
void BirdSky::Call(BirdFlock& f, const Bird& b){
    calls.push_back(BirdCall{BIRD_CALL_CAW,b.pos,b.id,f.voice});
    f.calls++;
}

/*
    A wanderer calls at the sitting rate in the air too: it is one to three birds and nearly always flying, and at a
    flock's flying rate the two over the colony chattered every five seconds, drowning the crows (seen 2026-10-18).
*/
float BirdSky::CallGap(BirdFlock& f){
    const bool f_seldom = (f.state == FLOCK_SETTLED) || (f.role == BIRD_ROLE_WANDER);
    return f_seldom ? Mid(BIRD_CALL_SETTLED_MIN_S,BIRD_CALL_SETTLED_MAX_S,Draw(f,8))
                    : Mid(BIRD_CALL_FLYING_MIN_S,BIRD_CALL_FLYING_MAX_S,Draw(f,8));
}

void BirdSky::MakeBird(BirdFlock& f, Bird& b, int index){
    b.id = next_bird++;
    b.rate = Mid(0.88f,1.12f,Draw(f,b.id));
    //A ring round the anchor, wider for the later birds - a winter's flock is twice as many - and the golden angle,
    //so no two line up.
    float a = (float)index * 2.3999632f + Draw(f,b.id) * 0.5f;
    float d = Mid(0.8f,2.4f,Draw(f,b.id)) * sqrtf(std::max(1.0f,(float)(index + 1) / 10.0f));
    b.slot = vec2(sinf(a),cosf(a)) * d;
    b.yaw = Draw(f,b.id) * 6.2831853f;
    b.idle = Draw(f,b.id) * 4.0f;
}

int BirdSky::Wanted(const BirdWorld& w, const BirdFlock& f) const{
    if (f.role == BIRD_ROLE_WANDER){
        return f.size;
    }
    return std::max(1,(int)((float)f.size * w.gather + 0.5f));
}

bool BirdSky::AddFlock(const BirdWorld& w, const vec2& home, int count, int role){
    BirdFlock f;
    f.id = next_id++;
    f.home = home;
    f.role = role;
    f.voice = (role == BIRD_ROLE_WANDER) ? BIRD_VOICE_MAGPIE : BIRD_VOICE_CROW;
    f.lift = (role == BIRD_ROLE_WANDER) ? Draw(f,5) * BIRD_WANDER_LIFT : 0.0f;
    BirdRoost r;
    if (!w.roost || !w.roost(home,BIRD_ROAM * 0.5f,std::vector<BirdScare>(),Mix(seed ^ f.id),r) || r.perches.empty()){
        return false;
    }
    //Made at the season's size, so a load in winter starts with winter's flocks rather than growing into them.
    f.size = std::max(1,count);
    const int n = Wanted(w,f);
    for (int i = 0; i < n; i++){
        Bird b;
        MakeBird(f,b,i);
        f.birds.push_back(b);
    }
    f.roost = r;
    Settle(f);
    for (Bird& b : f.birds){
        b.state = BIRD_PERCHED;
        b.pos = b.perch.at;
        b.fold = 1.0f;
    }
    f.anchor = r.at + vec3(0.0f,2.0f,0.0f);
    f.state = FLOCK_SETTLED;
    f.timer = SitTime(f);
    //Anywhere in a sit's gap, so the map's flocks made together do not all call together.
    f.call = Draw(f,8) * BIRD_CALL_SETTLED_MAX_S;
    flocks.push_back(f);
    return true;
}

void BirdSky::AddFlockInAir(const BirdWorld& w, const vec2& home, int count, int role){
    BirdFlock f;
    f.id = next_id++;
    f.home = home;
    f.role = role;
    f.voice = (role == BIRD_ROLE_WANDER) ? BIRD_VOICE_MAGPIE : BIRD_VOICE_CROW;
    f.lift = (role == BIRD_ROLE_WANDER) ? Draw(f,5) * BIRD_WANDER_LIFT : 0.0f;
    f.size = std::max(1,count);
    float ground = w.ground ? w.ground(home) : 0.0f;
    f.anchor = vec3(home.x,ground + w.tree_top + BIRD_CRUISE_CLEAR + f.lift,home.y);
    f.heading = Draw(f,4) * 6.2831853f;
    const vec3 way(sinf(f.heading),0.0f,cosf(f.heading));
    const int n = Wanted(w,f);
    for (int i = 0; i < n; i++){
        Bird b;
        MakeBird(f,b,i);
        b.state = BIRD_FLYING;
        b.fold = 0.0f;
        b.pos = f.anchor + vec3(b.slot.x,0.0f,b.slot.y);
        b.vel = way * BIRD_CRUISE_SPEED;
        b.yaw = f.heading;
        f.birds.push_back(b);
    }
    //On its way already: a roost if there is one, else a point in the sky.
    f.roost.at = f.anchor;
    f.state = FLOCK_TRAVEL;
    NextRoost(w,f,std::vector<BirdScare>());
    f.call = Draw(f,8) * BIRD_CALL_SETTLED_MAX_S;
    flocks.push_back(f);
}

//Each bird its perch in the roost; more birds than perches double up a little apart. One leaving is not given one.
void BirdSky::Settle(BirdFlock& f){
    const std::vector<BirdPerch>& p = f.roost.perches;
    if (p.empty()){
        return;
    }
    size_t i = 0;
    for (Bird& b : f.birds){
        if (b.f_leaving){
            continue;
        }
        b.perch = p[i % p.size()];
        float lap = (float)(i++ / p.size());
        if (lap > 0.0f){
            float a = Draw(f,b.id) * 6.2831853f;
            b.perch.at += vec3(sinf(a),0.0f,cosf(a)) * (0.18f * lap);
        }
    }
}

//The flock goes: each perched bird after its own delay, up to `stagger` seconds; any on its way down goes at once.
void BirdSky::TakeOff(BirdFlock& f, float stagger){
    for (Bird& b : f.birds){
        if (b.state == BIRD_PERCHED){
            b.delay = stagger * Draw(f,b.id);
        }else if (b.state == BIRD_LANDING){
            b.state = BIRD_FLYING;
        }
    }
}

void BirdSky::Scare(const BirdWorld& w, BirdFlock& f, const BirdScare& s){
    const bool f_was_settled = (f.state == FLOCK_SETTLED);
    //Nearest first: the order they go in. One already leaving goes on as it was.
    std::vector<std::pair<float,size_t>> order;
    vec3 middle;
    for (size_t i = 0; i < f.birds.size(); i++){
        if (!f.birds[i].f_leaving){
            order.push_back(std::make_pair((Flat(f.birds[i].pos) - s.at).length(),i));
            middle += f.birds[i].pos;
        }
    }
    std::sort(order.begin(),order.end());
    const float n = (float)std::max<size_t>(1,order.size() - 1);
    /*
        ONE WAY OUT for the flock - away from the scare, from its middle - each bird off it by a little and pulled a
        little more by its own way from the scare. Every bird straight away from the scare scattered them like a burst
        shell when the scare was among them, which no flock does: they go up together and leave together.
    */
    middle = middle / (float)std::max<size_t>(1,order.size());
    vec2 out = Flat(middle) - s.at;
    float way = (out.length() > 0.3f) ? atan2f(out.x,out.y) : Draw(f,4) * 6.2831853f;
    for (size_t k = 0; k < order.size(); k++){
        Bird& b = f.birds[order[k].second];
        bool f_near = order[k].first <= s.reach;
        //In reach, by rank over the stagger; beyond it, a beat after the last of those.
        b.delay = f_near ? BIRD_STAGGER_S * (float)k / n : BIRD_STAGGER_S + Draw(f,b.id) * 0.4f;
        if (b.state == BIRD_LANDING){
            b.state = BIRD_FLYING;
        }
        //The flock's way, bent a little toward its own way from the scare, and a little by its own draw.
        vec2 away = Flat(b.pos) - s.at;
        float own = (away.length() > 0.01f) ? atan2f(away.x,away.y) : way;
        float a = way + 0.3f * AngleTo(way,own) + (Draw(f,b.id) - 0.5f) * 0.7f;
        b.flee = vec2(sinf(a),cosf(a));
    }
    f.flushes++;
    if (!order.empty()){
        //The sentinel calls first; a flock that was sitting goes up with a burst of wings.
        const Bird& first = f.birds[order[0].second];
        Call(f,first);
        if (f_was_settled){
            calls.push_back(BirdCall{BIRD_CALL_FLUSH,middle,f.birds[0].id,f.voice});
        }
    }
    f.state = FLOCK_SCATTER;
    f.timer = Mid(BIRD_SCATTER_MIN_S,BIRD_SCATTER_MAX_S,Draw(f,2));
    //Another answers the sentinel a moment later - one bird alone has no one to, and calls again in the air.
    f.call = (order.size() >= 2) ? Mid(BIRD_CALL_ANSWER_MIN_S,BIRD_CALL_ANSWER_MAX_S,Draw(f,8)) : CallGap(f);
    f.wary = BirdScare{s.at,s.reach * BIRD_WARY_REACH};
    f.wary_left = BIRD_WARY_S;
    (void)w;
}

void BirdSky::Flush(const BirdWorld& w, const BirdScare& s){
    for (BirdFlock& f : flocks){
        for (const Bird& b : f.birds){
            if (!b.f_leaving && (Flat(b.pos) - s.at).length() <= s.reach){
                Scare(w,f,s);
                break;
            }
        }
    }
}

//Somewhere new near home, clear of what is scaring birds now and of where this flock was scared; the anchor flies there.
void BirdSky::NextRoost(const BirdWorld& w, BirdFlock& f, const std::vector<BirdScare>& scares){
    std::vector<BirdScare> avoid = scares;
    if (f.wary_left > 0.0f){
        avoid.push_back(f.wary);
    }
    //Restless - or a wanderer flying on - it moves on rather than settling where it sat; a wanderer goes far.
    const bool f_wander = (f.role == BIRD_ROLE_WANDER);
    if (f.roost.tree >= 0 && (f.state == FLOCK_SETTLED || f.state == FLOCK_CIRCLE)){
        avoid.push_back(BirdScare{Flat(f.roost.at),f_wander ? BIRD_WANDER_AWAY : 6.0f});
    }
    const float roam = f_wander ? BIRD_WANDER_ROAM : BIRD_ROAM;
    BirdRoost r;
    uint32_t draw = Mix(seed ^ f.id ^ (f.draws++ * 0x27D4EB2Du));
    bool f_found = false;
    //A colony flock follows the axe: near one of the trees being felled, which becomes its home.
    if (f.role == BIRD_ROLE_COLONY && !w.lures.empty()){
        const vec2& lure = w.lures[draw % (uint32_t)w.lures.size()];
        if (w.roost && w.roost(lure,BIRD_LURE_REACH,avoid,draw,r) && !r.perches.empty()){
            f.roost = r;
            f.home = lure;
            f_found = true;
        }
    }
    if (!f_found && w.roost && w.roost(f.home,roam,avoid,draw,r) && !r.perches.empty()){
        f.roost = r;
        f_found = true;
    }else if (!f_found && w.roost && w.roost(Flat(f.anchor),roam * 2.0f,avoid,draw,r) && !r.perches.empty()){
        //Its woods felled: a new home in the nearest trees left.
        f.roost = r;
        f.home = Flat(r.at);
        f_found = true;
    }
    //No trees to be had at all (the title's diorama, captured with none): a point in the sky near home, at a tree's
    //height, to circle and fly on from - it never comes down there (StepFlock, FLOCK_CIRCLE).
    if (!f_found && w.sky_reach > 0.0f){
        float a = Draw(f,14) * 6.2831853f;
        vec2 p = f.home + vec2(sinf(a),cosf(a)) * (Mid(0.3f,1.0f,Draw(f,15)) * w.sky_reach);
        f.roost = BirdRoost();
        f.roost.at = vec3(p.x,(w.ground ? w.ground(p) : 0.0f) + w.tree_top,p.y);
    }
    //None clear in the world: back over the old roost, to try again from there. Setting off, the way it is already
    //going, if any.
    if (f.state != FLOCK_TRAVEL){
        vec2 v = Flat(f.anchor_vel);
        vec2 to = Flat(f.roost.at) - Flat(f.anchor);
        f.heading = (v.length() > 0.5f) ? atan2f(v.x,v.y) : atan2f(to.x,to.y);
    }
    f.state = FLOCK_TRAVEL;
}

void BirdSky::Step(const BirdWorld& w, float dt, const std::vector<BirdScare>& scares){
    if (dt <= 0.0f){
        return;
    }
    for (BirdFlock& f : flocks){
        StepFlock(w,f,dt,scares);
    }
}

void BirdSky::StepFlock(const BirdWorld& w, BirdFlock& f, float dt, const std::vector<BirdScare>& scares){
    f.clock += dt;
    f.wary_left = std::max(0.0f,f.wary_left - dt);
    const vec3 was = f.anchor;
    auto clear_of = [&](const vec2& p){
        for (const BirdScare& s : scares){
            if ((p - s.at).length() <= s.reach){
                return false;
            }
        }
        return true;
    };

    if (f.state == FLOCK_SETTLED){
        //Scared: by anyone near a bird that is down or coming down, or by its tree going.
        bool f_scared = false;
        for (const BirdScare& s : scares){
            for (const Bird& b : f.birds){
                if (!b.f_leaving && (Flat(b.pos) - s.at).length() <= s.reach){
                    Scare(w,f,s);
                    f_scared = true;
                    break;
                }
            }
            if (f_scared){
                break;
            }
        }
        if (!f_scared && w.standing){
            for (const Bird& b : f.birds){
                if (!b.f_leaving && b.perch.tree >= 0 && !w.standing(b.perch.tree)){
                    Scare(w,f,BirdScare{Flat(b.perch.at),BIRD_REACH_FELLED});
                    f_scared = true;
                    break;
                }
            }
        }
        if (!f_scared){
            f.timer -= dt;
            if (f.timer <= 0.0f){
                NextRoost(w,f,scares);
                TakeOff(f,1.2f);
                if (Draw(f,3) < BIRD_CALL_TAKEOFF && !f.birds.empty()){
                    Call(f,f.birds[0]);
                }
                //Off: the next call is a flight's gap away, not what was left of the sit's.
                f.call = CallGap(f);
            }
        }
    }else if (f.state == FLOCK_SCATTER){
        //The anchor goes with them, so that when they flock again it is where they are.
        vec3 middle;
        int staying = 0;
        for (const Bird& b : f.birds){
            if (!b.f_leaving){
                middle += b.pos;
                staying++;
            }
        }
        if (staying > 0){
            f.anchor = middle / (float)staying;
        }
        f.timer -= dt;
        if (f.timer <= 0.0f){
            NextRoost(w,f,scares);
        }
    }else if (f.state == FLOCK_TRAVEL){
        //A roost someone has come under since: another - looked for once a second, not every step, while none is clear.
        f.timer -= dt;
        if (f.timer <= 0.0f
            && (!clear_of(Flat(f.roost.at)) || (w.standing && f.roost.tree >= 0 && !w.standing(f.roost.tree)))){
            NextRoost(w,f,scares);
            f.timer = 1.0f;
        }
        vec2 to = Flat(f.roost.at) - Flat(f.anchor);
        float d = to.length();
        if (d <= BIRD_CIRCLE_RADIUS * 1.2f){
            f.state = FLOCK_CIRCLE;
            f.circle = atan2f(-to.x,-to.y);
            f.timer = (f.role == BIRD_ROLE_WANDER) ? Mid(BIRD_CIRCLE_S,BIRD_WANDER_CIRCLE_MAX_S,Draw(f,6)) : BIRD_CIRCLE_S;
        }else{
            /*
                The way turns toward the roost at BIRD_TURN_RATE, wandering off the straight line by a slow swing - so a
                flight is a curve. Near the roost the swing goes and it turns three times as hard, so it can never
                circle short of the roost on a turn too wide to reach it.
            */
            float way = atan2f(to.x,to.y);
            float rate = BIRD_TURN_RATE;
            if (d > 2.5f * BIRD_CIRCLE_RADIUS){
                float wobble = (f.role == BIRD_ROLE_WANDER) ? BIRD_WANDER_WOBBLE : BIRD_WOBBLE;
                way += wobble * sinf(f.clock * 0.31f + (float)f.id * 1.7f);
            }else{
                rate *= 3.0f;
            }
            f.heading += Clamp(AngleTo(f.heading,way),-rate * dt,rate * dt);
            float step = std::min(d,BIRD_CRUISE_SPEED * dt);
            f.anchor.x += sinf(f.heading) * step;
            f.anchor.z += cosf(f.heading) * step;
        }
        //Over the tallest tree: up at once, down slowly.
        vec2 ahead = Flat(f.anchor) + vec2(sinf(f.heading),cosf(f.heading)) * std::min(d,8.0f);
        float ground = w.ground ? std::max(w.ground(Flat(f.anchor)),w.ground(ahead)) : 0.0f;
        float want = ground + w.tree_top + BIRD_CRUISE_CLEAR + f.lift;
        f.anchor.y = (want > f.anchor.y) ? want : std::max(want,f.anchor.y - BIRD_SINK_RATE * dt);
    }else if (f.state == FLOCK_CIRCLE){
        f.circle += BIRD_CRUISE_SPEED / BIRD_CIRCLE_RADIUS * dt;
        f.anchor.x = f.roost.at.x + sinf(f.circle) * BIRD_CIRCLE_RADIUS;
        f.anchor.z = f.roost.at.z + cosf(f.circle) * BIRD_CIRCLE_RADIUS;
        //Circling its way round: heading along the circle, so flying on leaves on the tangent, not a turn.
        f.heading = f.circle + 1.5707963f;
        float want = f.roost.at.y + 2.5f + f.lift;
        f.anchor.y = std::max(want,f.anchor.y - BIRD_SINK_RATE * dt);
        f.timer -= dt;
        if (f.timer <= 0.0f){
            const bool f_fly_on = (f.role == BIRD_ROLE_WANDER) && Draw(f,7) < BIRD_WANDER_FLY_ON;
            if (!f_fly_on && !f.roost.perches.empty() && clear_of(Flat(f.roost.at))
                && (!w.standing || f.roost.tree < 0 || w.standing(f.roost.tree))){
                Settle(f);
                for (Bird& b : f.birds){
                    b.state = b.f_leaving ? b.state : BIRD_LANDING;
                }
                f.state = FLOCK_SETTLED;
                f.timer = SitTime(f);
            }else{
                NextRoost(w,f,scares);
            }
        }
    }
    //Now and then one of them calls, whatever the flock is doing - more often in the air (CallGap).
    f.call -= dt;
    if (f.call <= 0.0f && !f.birds.empty()){
        Call(f,f.birds[(size_t)(Draw(f,9) * (float)f.birds.size()) % f.birds.size()]);
        f.call = CallGap(f);
    }
    Gather(w,f,dt);
    f.anchor_vel = (f.anchor - was) / dt;
    if (w.ground){
        f.floor = w.ground(Flat(f.anchor)) + 0.5f;
    }
    for (Bird& b : f.birds){
        StepBird(w,f,b,dt);
        StepWings(f,b,dt);
    }
    //Gone: a leaving bird far enough off that no one sees it go.
    f.birds.erase(std::remove_if(f.birds.begin(),f.birds.end(),[&](const Bird& b){
        return b.f_leaving && (Flat(b.pos) - Flat(f.anchor)).length() > BIRD_LEAVE_FAR;
    }),f.birds.end());
}

void BirdSky::Gather(const BirdWorld& w, BirdFlock& f, float dt){
    f.join -= dt;
    //Only in the air: a bird joining a flock in its tree would have to find a perch among them, and one leaving would
    //go up alone and be taken for a scare.
    if (f.join > 0.0f || (f.state != FLOCK_TRAVEL && f.state != FLOCK_CIRCLE)){
        return;
    }
    int staying = 0;
    for (const Bird& b : f.birds){
        staying += b.f_leaving ? 0 : 1;
    }
    const int want = Wanted(w,f);
    if (staying == want){
        return;
    }
    f.join = Mid(BIRD_JOIN_GAP_MIN_S,BIRD_JOIN_GAP_MAX_S,Draw(f,10));
    if (staying < want){
        //A few together, from one way off the flock's edge, flying in at the cruise.
        int n = std::min(want - staying,1 + (int)(Draw(f,11) * (float)BIRD_JOIN_GROUP));
        float a = Draw(f,12) * 6.2831853f;
        vec3 from = f.anchor + vec3(sinf(a) * BIRD_JOIN_FROM,1.5f,cosf(a) * BIRD_JOIN_FROM);
        vec3 in = (f.anchor - from).normalize();
        for (int k = 0; k < n; k++){
            Bird b;
            MakeBird(f,b,staying + k);
            b.state = BIRD_FLYING;
            b.fold = 0.0f;
            b.pos = from + vec3(-in.z,0.0f,in.x) * ((float)k - 0.5f * (float)(n - 1)) * 0.9f;
            b.vel = in * BIRD_CRUISE_SPEED;
            b.yaw = atan2f(in.x,in.z);
            f.birds.push_back(b);
        }
        return;
    }
    //One leaves - the last to come - off the way the flock is not going, so it is not taken for a straggler.
    for (auto it = f.birds.rbegin(); it != f.birds.rend(); ++it){
        if (!it->f_leaving){
            float a = f.heading + 3.14159265f + (Draw(f,13) - 0.5f) * 2.0f;
            it->f_leaving = true;
            it->flee = vec2(sinf(a),cosf(a));
            if (it->state == BIRD_LANDING){
                it->state = BIRD_FLYING;
            }
            break;
        }
    }
}

void BirdSky::StepBird(const BirdWorld& w, BirdFlock& f, Bird& b, float dt){
    (void)w;
    if (b.state == BIRD_PERCHED){
        b.vel = vec3();
        b.bank = 0.0f;
        if (f.state != FLOCK_SETTLED || b.f_leaving){
            b.delay -= dt;
            if (b.delay <= 0.0f){
                //Up and off the way it faces, beating from the first stroke.
                b.state = BIRD_FLYING;
                b.vel = vec3(sinf(b.yaw),2.5f,cosf(b.yaw));
                b.glide = 0.0f;
                b.beats = 6;
                b.look = 0.0f;
            }
            return;
        }
        //Fidgeting: mostly a look round, now and then a turn on the spot or a hop to a free perch.
        b.idle -= dt;
        if (b.idle <= 0.0f){
            b.idle = Mid(1.5f,6.0f,Draw(f,b.id));
            float r = Draw(f,b.id);
            if (r < 0.6f){
                b.look = (Draw(f,b.id) - 0.5f) * 1.6f;
            }else if (r < 0.85f){
                b.yaw += (Draw(f,b.id) - 0.5f) * 2.4f;
                b.look = 0.0f;
            }else if (!f.roost.perches.empty()){
                const BirdPerch& p = f.roost.perches[(size_t)(Draw(f,b.id) * (float)f.roost.perches.size())
                                                     % f.roost.perches.size()];
                bool f_free = true;
                for (const Bird& o : f.birds){
                    f_free = f_free && (o.id == b.id || (o.perch.at - p.at).length() > 0.15f);
                }
                if (f_free && (p.at - b.perch.at).length() > 0.15f){
                    b.perch = p;
                    b.state = BIRD_LANDING;
                    b.vel = vec3(0.0f,1.5f,0.0f);
                }
            }
        }
        b.pos = b.perch.at;
        return;
    }

    vec3 want_vel;
    float agility = 2.5f;
    if (b.f_leaving){
        //Off on its own way at the cruise, climbing a little, until it is far enough off to be gone.
        want_vel = vec3(b.flee.x,0.2f,b.flee.y) * BIRD_CRUISE_SPEED;
        if (b.state == BIRD_LANDING){
            b.state = BIRD_FLYING;
        }
    }else if (b.state == BIRD_LANDING){
        //Down to it from above: aim over the perch by a share of the way still to go, slowing as it nears.
        vec3 to = b.perch.at - b.pos;
        float d = to.length();
        if (d < 0.12f){
            b.state = BIRD_PERCHED;
            b.pos = b.perch.at;
            b.vel = vec3();
            b.idle = Mid(0.5f,3.0f,Draw(f,b.id));
            return;
        }
        float dh = Flat(to).length();
        vec3 aim = b.perch.at + vec3(0.0f,std::min(2.0f,dh * 0.6f),0.0f);
        vec3 dir = aim - b.pos;
        float speed = std::min(BIRD_CRUISE_SPEED,0.8f + d * 1.5f);
        want_vel = dir.normalize() * speed;
        agility = 4.0f;
    }else if (f.state == FLOCK_SCATTER){
        //Climbing hard at first, then out level.
        float climb = (b.vel.y > 2.0f || f.timer < 1.0f) ? 0.15f : 0.6f;
        want_vel = vec3(b.flee.x,climb,b.flee.y) * BIRD_FLUSH_SPEED;
        if (b.delay > 0.0f){
            b.delay = 0.0f;
        }
    }else{
        //Its place round the anchor, drifting slowly so the flock never sets into a shape.
        float t = f.clock * 0.45f + (float)(b.id % 64u) * 1.7f;
        vec3 place = f.anchor + vec3(b.slot.x + sinf(t) * 0.6f,cosf(t * 1.3f) * 0.5f,b.slot.y + cosf(t * 0.8f) * 0.6f);
        want_vel = (place - b.pos) * 1.2f + f.anchor_vel;
        float s = want_vel.length();
        if (s > BIRD_FLUSH_SPEED){
            want_vel = want_vel * (BIRD_FLUSH_SPEED / s);
        }
    }
    //Too close to another: apart.
    for (const Bird& o : f.birds){
        if (o.id == b.id || o.state == BIRD_PERCHED){
            continue;
        }
        vec3 off = b.pos - o.pos;
        float d = off.length();
        if (d > 0.001f && d < 0.6f){
            want_vel += off * ((0.6f - d) * 6.0f / d);
        }
    }
    b.vel += (want_vel - b.vel) * std::min(1.0f,agility * dt);
    b.pos += b.vel * dt;
    if (b.state == BIRD_FLYING){
        b.pos.y = std::max(b.pos.y,f.floor);
    }
    //Facing its way over the ground, banking into a turn.
    float hs = Flat(b.vel).length();
    float target_bank = 0.0f;
    if (hs > 0.2f){
        float yaw = atan2f(b.vel.x,b.vel.z);
        float rate = AngleTo(b.yaw,yaw) / dt;
        b.yaw = yaw;
        target_bank = Clamp(-rate * 0.25f,-0.8f,0.8f);
    }
    b.bank += (target_bank - b.bank) * std::min(1.0f,4.0f * dt);
}

void BirdSky::StepWings(BirdFlock& f, Bird& b, float dt){
    const bool f_down = (b.state == BIRD_PERCHED);
    float fold_to = f_down ? 1.0f : 0.0f;
    b.fold += Clamp(fold_to - b.fold,-6.0f * dt,6.0f * dt);
    float amp_to = 0.0f;
    float rate = BIRD_FLAP_HZ * b.rate;
    if (!f_down){
        float speed = b.vel.length();
        bool f_must = (f.state == FLOCK_SCATTER) || b.vel.y > 0.4f || speed < 0.7f * BIRD_CRUISE_SPEED;
        if (f.state == FLOCK_SCATTER){
            rate *= BIRD_FLUSH_FLAP;
        }
        if (f_must){
            b.glide = 0.0f;
            b.beats = std::max(b.beats,1);
            amp_to = 1.0f;
        }else if (b.glide > 0.0f){
            b.glide -= dt;
            if (b.glide <= 0.0f){
                b.beats = 3 + (int)(Draw(f,b.id) * 4.0f);
            }
        }else{
            amp_to = 1.0f;
        }
    }
    //A beat counted at the top of each stroke; a burst spent, a glide.
    float was = b.phase;
    b.phase += 6.2831853f * rate * dt;
    if (b.phase >= 6.2831853f){
        b.phase = fmodf(b.phase,6.2831853f);
    }
    if (!f_down && b.phase < was && b.glide <= 0.0f && --b.beats <= 0){
        bool f_must = (f.state == FLOCK_SCATTER) || b.vel.y > 0.4f || b.vel.length() < 0.7f * BIRD_CRUISE_SPEED;
        if (!f_must){
            b.glide = Mid(0.5f,1.7f,Draw(f,b.id));
        }else{
            b.beats = 1;
        }
    }
    b.amp += (amp_to - b.amp) * std::min(1.0f,6.0f * dt);
}

void BirdSky::Poses(std::vector<BirdPose>& out) const{
    out.clear();
    for (const BirdFlock& f : flocks){
        for (const Bird& b : f.birds){
            BirdPose p;
            p.pos = b.pos;
            float pitch = 0.0f;
            if (b.state != BIRD_PERCHED){
                float speed = b.vel.length();
                pitch = (speed > 0.01f) ? Clamp(b.vel.y / speed,-0.6f,0.6f) * 0.5f : 0.0f;
            }
            //Nose up is a turn about +X the negative way: +Z toward +Y.
            p.turn = quat(vec3(0.0f,1.0f,0.0f),b.yaw + b.look) * quat(vec3(1.0f,0.0f,0.0f),-pitch)
                   * quat(vec3(0.0f,0.0f,1.0f),b.bank);
            float spread = 1.0f - b.fold;
            float s = sinf(b.phase);
            p.up = b.amp * std::max(0.0f,s) * spread;
            p.down = b.amp * std::max(0.0f,-s) * spread;
            p.folded = b.fold;
            if (b.state == BIRD_PERCHED){
                p.tree = b.perch.tree;
                p.ride = 1.0f;
            }
            //The flare: wings up and held over the last of the way down.
            if (b.state == BIRD_LANDING){
                float d = (b.perch.at - b.pos).length();
                if (d < 1.0f){
                    p.up = std::max(p.up,0.8f * (1.0f - d) * spread);
                    p.down *= d;
                    p.tree = b.perch.tree;
                    p.ride = 1.0f - d;
                }
            }
            out.push_back(p);
        }
    }
}
