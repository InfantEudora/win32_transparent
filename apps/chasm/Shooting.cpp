#include "Economy.h"
#include "ChasmWorld.h"
#include "Walkers.h"
#include "BuildingMesh.h"
#include <algorithm>
#include <cmath>

/*
    ARROWS (docs/zombies_plan.md, step 3), the rules: who shoots, at which zombie, and every arrow's flight - all of it
    the economy's state, saved and hashed, so a replay looses the same arrows and they come down where they did. Where an
    arrow goes is a hash of the tick, the archer and the zombie, never the shared RRandom (the open rrand issue).
*/

namespace {

int Ticks(float seconds){
    return std::max(1,(int)std::lround(seconds * CALENDAR_TICKS_PER_SECOND));
}

//The ground at `at`, on the level of the plot it is on: 0 off the grid.
float GroundAt(const ChasmWorld& w, const vec2& at){
    GridPick p = w.picker->Pick(at);
    return (p.f_hit && p.plot >= 0) ? w.terrain->GroundHeight(at,w.terrain->Height(p.plot)) : 0.0f;
}

//What he stands on: the ground under his body, or a tower's platform - the layer his route has him on.
float StandGround(const ChasmWorld& w, const ZoneState& z, const EconomyWorker& k){
    int layer = EconomyWorkerLayer(k);
    if (layer == WALK_GROUND){
        return GroundAt(w,EconomyBody(k));
    }
    GridPick p = w.picker->Pick(k.pos);
    return (p.f_hit && p.plot >= 0) ? BuildingLayerHeight(w,z,p.plot,layer) : GroundAt(w,k.pos);
}

/*
    Who shoots: someone grown with a bow (EconomyCarriesBow) - anyone trained at the range, whatever his job, a trainee,
    or a guard, the tower's archer - out of doors and STANDING: on his post, a tower's platform among them, or on the
    range's line. So a trained farmer ordered out is a posted archer. One walking walks on.
*/
bool Shooter(const EconomyWorker& k, uint64_t tick){
    bool f_bow = EconomyCarriesBow(k);
    bool f_stands = (k.state == WORKER_ON_POST || k.state == WORKER_TRAINING) && k.seg + 1 >= (int)k.route.size();
    return f_bow && f_stands && !EconomyIndoors(k) && EconomyCanOrder(k,tick);
}

/*
    Where on zombie `m` an arrow from `from` goes in: `at` across and up from its chest, across being square to the way
    the arrow comes - so a hit follows it as it walks, and stays where it went in.
*/
vec3 Into(const ChasmWorld& w, const EconomyZombie& m, const vec3& from, const vec2& at){
    vec2 body = EconomyBody(m);
    vec2 way = body - vec2(from.x,from.z);
    float l = way.length();
    vec2 across = (l > 1e-4f) ? vec2(-way.y,way.x) * (1.0f / l) : vec2(1.0f,0.0f);
    return vec3(body.x + across.x * at.x,GroundAt(w,body) + ARROW_CHEST + at.y,body.y + across.y * at.x);
}

//A shot's own number: the tick, the archer and the zombie, well mixed.
uint32_t ShotHash(uint64_t tick, uint32_t by, uint32_t zombie){
    uint32_t h = (uint32_t)tick * 0x8DA6B343u ^ (uint32_t)(tick >> 32) * 0x2C1B3C6Du ^ by * 0xD8163841u ^ zombie * 0xCB1AB31Fu;
    h ^= h >> 13;
    h *= 0x5BD1E995u;
    h ^= h >> 15;
    return h;
}

//A turn's release, ARROW_RELEASE into it: the clip's beat, on a whole tick within it.
int ReleaseOf(int turn){
    return std::max(1,std::min(turn,(int)std::lround(ARROW_RELEASE * (float)turn)));
}

}

/*
    THE PACE (the user's, 2026-10-19): the more archery, the quicker his turn, and each turn off that by up to
    ARROW_SHOT_JITTER either way - so two archers who see a zombie on the same tick do not loose on the same tick, nor fall
    back into step after. The hash is a shot's, for zombie 0, which no zombie is.
*/
int EconomyShotTicks(const EconomyWorker& k, uint64_t tick){
    float skill = std::max(0.0f,std::min(1.0f,(float)k.archery / (float)ARCHERY_FULL));
    float seconds = ARROW_SHOT_SECONDS + (ARROW_SHOT_MASTER - ARROW_SHOT_SECONDS) * skill;
    float u = (float)(ShotHash(tick,k.id,0) >> 8) / 16777216.0f;
    return Ticks(seconds * (1.0f + ARROW_SHOT_JITTER * (2.0f * u - 1.0f)));
}

int Economy::ShotTarget(const EconomyWorker& k, const ZoneState& z, const std::vector<int>& coming, float& eye_ground) const{
    const ChasmWorld& w = *state.world;
    eye_ground = StandGround(w,z,k);
    vec2 at = EconomyBody(k);
    int best = -1;
    float nearest = 0.0f;
    for (size_t j = 0; j < state.zombies.size(); j++){
        const EconomyZombie& m = state.zombies[j];
        if (m.health - coming[j] <= 0){
            continue;
        }
        vec2 body = EconomyBody(m);
        float d = (body - at).length();
        if (best >= 0 && d >= nearest){
            continue;
        }
        //As far as he sees it, over the ground between (Exploration.h, SIGHT): a higher post sees, and shoots, further.
        float ground = GroundAt(w,body);
        if (d > SightReach(eye_ground,ground) || (w.sight && !w.sight->Clear(at,eye_ground,body,ground + SIGHT_TARGET))){
            continue;
        }
        best = (int)j;
        nearest = d;
    }
    return best;
}

/*
    HELD BY ZOMBIES (step 3b, the user's): a zombie he could shoot at, or one he is fighting. One his arrows already have
    counts too - else, the moment every zombie in sight was doomed by arrows in the air, he would be free to walk off
    into the next.
*/
bool Economy::ZombiesInRange(const EconomyWorker& k, const ZoneState& z) const{
    if (state.zombies.empty()){
        return false;
    }
    if (ZombieNear(k,FIGHT_REACH)){
        return true;
    }
    if (!Shooter(k,now)){
        return false;
    }
    std::vector<int> none(state.zombies.size(),0);
    float eye_ground = 0.0f;
    return ShotTarget(k,z,none,eye_ground) >= 0;
}

/*
    A DRINK BETWEEN SHOTS (step 3b, the user's): a meal waits while he shoots, and held on his post by the zombies he
    shoots at, a guard went dry through a long wave with water on his back. So between two turns of the Shoot clip he
    drinks, when a whole unit fits, from where he would at any other time (Needs): his tower's water, or with none
    there his pack's. Water alone - a drink is a second, and a unit puts back half the bar; food is slower, worth less
    a unit, and hunger kills slower, so a meal still waits for the wave to pass. Never with a zombie near enough to
    fight (MEALS), when he would rather keep shooting.
*/
bool Economy::DrinkBetweenShots(EconomyWorker& k, const ZoneState& z){
    if (EconomyMealOn(k)){
        return k.meal_good == GOOD_WATER;
    }
    std::array<int,GOOD_COUNT>* from = &k.pack;
    if (uint32_t tower = EconomyOnTower(state,z,k)){
        if (state.stock[tower][GOOD_WATER] > 0){
            from = &state.stock[tower];
        }
    }
    //Water alone offered, so what Consume begins is a drink.
    std::array<int,GOOD_COUNT> water{};
    water[GOOD_WATER] = (*from)[GOOD_WATER];
    if (!Consume(k,water)){
        return false;
    }
    (*from)[GOOD_WATER] = water[GOOD_WATER];
    return true;
}

/*
    A tick of ARROWS (Economy.h). The arrows first: one in the air flies on, a hit following its zombie, and comes down
    after its flight - a hit into its zombie, taking ARROW_HIT, a miss into the ground. A hit whose zombie fell before it
    got there comes down where it stood. The zombies the arrows felled go - fallen, the way the arrow flew - and what
    was stuck in them with them.
    Then the archers: each counts on through his turn of the Shoot clip while he has someone to shoot at, and looses at
    its release at whom he aims at then. With nobody left before the release his bow comes down; after it, the turn
    plays out. All in id order, and an arrow loosed counts at once against the next archer's choice.
*/
void Economy::Shoot(const ZoneState& z){
    if (state.zombies.empty() && state.arrows.empty()){
        for (EconomyWorker& k : state.workers){
            k.draw = 0;
            k.draw_turn = 0;
            k.aim = 0;
        }
        return;
    }
    const ChasmWorld& w = *state.world;
    //The zombies are in id order, as they were dropped.
    auto index_of = [&](uint32_t id){
        auto it = std::lower_bound(state.zombies.begin(),state.zombies.end(),id,
                                   [](const EconomyZombie& m, uint32_t v){ return m.id < v; });
        return (it != state.zombies.end() && it->id == id) ? (int)(it - state.zombies.begin()) : -1;
    };
    for (EconomyArrow& a : state.arrows){
        if (!EconomyArrowFlying(a)){
            if (!a.f_hit){
                a.lie--;
            }
            continue;
        }
        int j = index_of(a.zombie);
        if (a.f_hit && j >= 0){
            a.to = Into(w,state.zombies[j],a.from,a.at);
        }else if (a.f_hit){
            a.f_hit = false;
            a.to.y = GroundAt(w,vec2(a.to.x,a.to.z));
        }
        if (++a.flown >= a.ticks){
            if (a.f_hit){
                EconomyZombie& m = state.zombies[j];
                bool f_alive = m.health > 0;
                m.health -= ARROW_HIT;
                //THE FALLEN (zombies_plan.md, step 6): the arrow that fells it pushes it the way it flew.
                if (f_alive && m.health <= 0){
                    ZombieFalls(m,vec2(a.to.x - a.from.x,a.to.z - a.from.z));
                }
            }else{
                a.lie = Ticks(ARROW_LIE_SECONDS);
            }
        }
    }
    state.zombies.erase(std::remove_if(state.zombies.begin(),state.zombies.end(),[](const EconomyZombie& m){
        return m.health <= 0;
    }),state.zombies.end());
    state.arrows.erase(std::remove_if(state.arrows.begin(),state.arrows.end(),[&](const EconomyArrow& a){
        return !EconomyArrowFlying(a) && (a.f_hit ? index_of(a.zombie) < 0 : a.lie <= 0);
    }),state.arrows.end());

    //What the arrows in the air will do to each zombie: one they will fell is shot at no more.
    std::vector<int> coming(state.zombies.size(),0);
    for (const EconomyArrow& a : state.arrows){
        int j = index_of(a.zombie);
        if (EconomyArrowFlying(a) && a.f_hit && j >= 0){
            coming[(size_t)j] += ARROW_HIT;
        }
    }
    for (EconomyWorker& k : state.workers){
        //One within his reach is fought by hand (A FIGHT), not shot at.
        if (state.zombies.empty() || !Shooter(k,now) || ZombieNear(k,FIGHT_REACH)){
            k.draw = 0;
            k.draw_turn = 0;
            k.aim = 0;
            continue;
        }
        float eye_ground = 0.0f;
        int j = ShotTarget(k,z,coming,eye_ground);
        if (k.draw >= k.draw_turn){
            k.draw = 0;
            k.draw_turn = 0;
        }
        if (j < 0 && k.draw < ReleaseOf(k.draw_turn)){
            k.draw = 0;
            k.draw_turn = 0;
            k.aim = 0;
            continue;
        }
        //Between his turns, a drink first if one fits (step 3b): the bow down for it, and the next turn after.
        if (k.draw == 0 && DrinkBetweenShots(k,z)){
            k.aim = 0;
            continue;
        }
        //A turn begun: its length fixed now (THE PACE), so its release is one tick whatever his archery does meanwhile.
        if (k.draw == 0){
            k.draw_turn = EconomyShotTicks(k,now);
        }
        k.draw++;
        if (j < 0){
            continue;
        }
        const EconomyZombie& m = state.zombies[(size_t)j];
        k.aim = m.id;
        if (k.draw != ReleaseOf(k.draw_turn)){
            continue;
        }
        /*
            The arrow leaves the bow: off his aim by a normal scatter each way, wider the further it flies and the less
            archery he has - inside the zombie's body it hits. A miss passes it there and comes down past it, as far on
            as it was high (ARROW_MISS_ON), or short of it if it was so low it would go into the ground first.
        */
        vec2 body = EconomyBody(k);
        EconomyArrow a;
        a.by = k.id;
        a.zombie = m.id;
        a.from = vec3(body.x,eye_ground + ARROW_BOW,body.y);
        float d = (Into(w,m,a.from,vec2()) - a.from).length();
        float skill = std::max(0.0f,std::min(1.0f,(float)k.archery / (float)ARCHERY_FULL));
        float spread = (ARROW_SPREAD_NOVICE + (ARROW_SPREAD_MASTER - ARROW_SPREAD_NOVICE) * skill) * d;
        uint32_t h = ShotHash(now,k.id,m.id);
        float u1 = ((float)(h & 0xFFFFu) + 0.5f) / 65536.0f;
        float u2 = (float)(h >> 16) / 65536.0f;
        float r = spread * std::sqrt(-2.0f * std::log(u1));
        a.at = vec2(r * std::cos(6.2831853f * u2),r * std::sin(6.2831853f * u2));
        a.f_hit = std::fabs(a.at.x) <= ARROW_BODY_HALF_WIDTH && std::fabs(a.at.y) <= ARROW_BODY_HALF_HEIGHT;
        vec3 pass = Into(w,m,a.from,a.at);
        if (a.f_hit){
            a.to = pass;
            coming[(size_t)j] += ARROW_HIT;
        }else{
            float ground = GroundAt(w,EconomyBody(m));
            vec2 down;
            if (pass.y > ground){
                vec2 way(pass.x - a.from.x,pass.z - a.from.z);
                float l = std::max(1e-4f,way.length());
                down = vec2(pass.x,pass.z) + way * ((pass.y - ground) * ARROW_MISS_ON / l);
            }else{
                vec3 p = a.from + (pass - a.from) * ((a.from.y - ground) / std::max(1e-4f,a.from.y - pass.y));
                down = vec2(p.x,p.z);
            }
            a.to = vec3(down.x,GroundAt(w,down),down.y);
        }
        a.ticks = Ticks((a.to - a.from).length() / ARROW_SPEED);
        state.arrows.push_back(a);
    }
    state.version++;
}
