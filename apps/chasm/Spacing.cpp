#include "Economy.h"
#include "ChasmWorld.h"
#include "Walkers.h"
#include <algorithm>
#include <cmath>
#include <numeric>

/*
    SPACING (Economy.h; zombies_plan.md, "Step 1b"): the bodies' offsets from their routes, a tick at a time - a group
    on one path side by side, a crowd standing apart, two meeting on a road passing each other on the right - and
    (step 1c) the pace each may walk at behind those ahead of it, so a crowd on one path queues.
*/

namespace {

//A body in the pass: where it stands, which way it walks (nothing while it stands) and how fast, what pushes it this
//tick, and the pace those ahead of it let it walk at.
struct Body{
    vec2 at;
    vec2 way;
    float speed = 0.0f;         //its leg's, unslowed
    uint32_t id = 0;
    bool f_zombie = false;
    vec2 push;
    float crowd = 1.0f;
};

//The way a walker goes: along the leg he is on, while he has one left; nothing standing.
template<typename T> vec2 WayOf(const T& k){
    if (k.seg + 1 < (int)k.route.size()){
        vec2 d = k.route[k.seg + 1] - k.route[k.seg];
        float l = d.length();
        if (l > 1e-4f){
            return d * (1.0f / l);
        }
    }
    return vec2();
}

//His right, going `way` - seen from above, with x east and z south: north's right is east.
vec2 RightOf(const vec2& way){
    return vec2(-way.y,way.x);
}

//Two bodies on one spot: a way apart from a hash of the two ids - the lower one way, the higher the other.
vec2 Apart(uint32_t self, uint32_t other){
    uint32_t lo = std::min(self,other);
    uint32_t hi = std::max(self,other);
    uint32_t h = (lo * 0x9E3779B1u) ^ (hi * 0x85EBCA77u);
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    float a = (float)(h % 6283u) / 1000.0f;
    vec2 u(std::cos(a),std::sin(a));
    return (self == lo) ? u : vec2() - u;
}

//Body `b` pushed away from one at `other`, `n` the way from it to `b`, with `strength` (world units a second).
bool Standing(const Body& b){
    return b.way.x == 0.0f && b.way.y == 0.0f;
}

//Body `b` pushed away from another, `n` the way from it to `b`, with `strength` (world units a second): one walking
//only aside, never back - but out of a person's room, any way (f_any_way).
void Shove(Body& b, const vec2& n, float strength, bool f_any_way = false){
    if (Standing(b) || f_any_way){
        b.push = b.push + n * strength;
        return;
    }
    vec2 right = RightOf(b.way);
    b.push = b.push + right * (n.dot(right) * strength);
}

/*
    KEEP TO THE RIGHT: walker `b` leans to his right for `other`, `dist` away, the straighter ahead of him the more - near
    enough to push, or within SPACING_AHEAD when it comes at him or stands in his way, so the two begin to pass before
    they meet. One walking his way ahead of him is left be until he is that near - a group on one path - unless he walks
    slower (SPACING_OVERTAKE): then he is passed, as one standing is, rather than followed all the way (FOLLOWING).
*/
void Lean(Body& b, const Body& other, float dist){
    if (Standing(b) || dist < 1e-4f){
        return;
    }
    float ahead = (other.at - b.at).dot(b.way) / dist;
    bool f_coming = Standing(other) || other.way.dot(b.way) < 0.0f;
    bool f_slower = !f_coming && other.speed < b.speed * SPACING_OVERTAKE;
    if (ahead <= 0.0f || (dist >= SPACING_RADIUS && !f_coming && !f_slower)){
        return;
    }
    float strength = SPACING_PUSH * SPACING_KEEP_RIGHT * (SPACING_AHEAD - dist) / SPACING_AHEAD;
    b.push = b.push + RightOf(b.way) * (strength * ahead);
}

/*
    FOLLOWING (Economy.h): walker `b` slows behind `other`, walking his way just ahead of him and in his line - all but
    stopped at SPACING_FOLLOW_CLOSEST, slower than anyone he could follow, so he drops back - rather than walking into
    him. One standing or coming at him he does not wait behind: he leans round those (Lean), so nobody waits behind
    anybody for good. And only behind one walking AWAY from him: two whose ways meet each have the other ahead, and
    slowed each other to a crawl side by side (found 2026-10-20) - they are pushed apart across their ways instead.
*/
void Follow(Body& b, const Body& other){
    if (Standing(b)){
        return;
    }
    vec2 d = other.at - b.at;
    float ahead = d.dot(b.way);
    float aside = std::fabs(d.dot(RightOf(b.way)));
    //One standing in his way, near: he slows while he leans round it, to SPACING_PASS_SLOWEST, so the pushes have the
    //time to part them - walking a crowd's queue to his place, he slipped through those stood in it (found 2026-10-20).
    if (Standing(other)){
        if (ahead > aside && ahead < SPACING_RADIUS && aside < SPACING_FOLLOW_WIDE){
            b.crowd = std::min(b.crowd,std::max(SPACING_PASS_SLOWEST,ahead / SPACING_RADIUS));
        }
        return;
    }
    if (other.way.dot(b.way) < SPACING_FOLLOW_SAME || d.dot(other.way) <= 0.0f){
        return;
    }
    if (ahead <= aside || ahead >= SPACING_FOLLOW || aside >= SPACING_FOLLOW_WIDE){
        return;
    }
    float pace = (ahead - SPACING_FOLLOW_CLOSEST) / (SPACING_FOLLOW - SPACING_FOLLOW_CLOSEST);
    b.crowd = std::min(b.crowd,std::max(SPACING_FOLLOW_SLOWEST,pace));
}

//Taking no part: back toward the route, and on it once near enough.
void SettleBack(vec2& off, float dt){
    off = off * std::max(0.0f,1.0f - SPACING_SETTLE * dt);
    if (off.length() < 1e-3f){
        off = vec2();
    }
}

}

void Economy::SpaceOut(const ZoneState& z, float dt){
    const ChasmWorld& w = *state.world;
    std::vector<Body> bodies;
    std::vector<vec2*> offs;            //per body, the offset it moves...
    std::vector<vec2> anchors;          //...and where its route has it...
    std::vector<float*> crowds;         //...and the pace it walks its next tick at
    for (EconomyWorker& k : state.workers){
        if (EconomyIndoors(k)){
            k.off = vec2();
            k.crowd = 1.0f;
            continue;
        }
        if ((k.with && EconomyStageAt(k.born,now) == LIFE_BABY) || !k.route_layer.empty()){
            SettleBack(k.off,dt);
            k.crowd = 1.0f;
            continue;
        }
        Body b;
        b.at = EconomyBody(k);
        b.way = WayOf(k);
        b.speed = Standing(b) ? 0.0f : k.speed[k.seg] * EconomyCarryPace(k);
        b.id = k.id;
        bodies.push_back(b);
        offs.push_back(&k.off);
        anchors.push_back(k.pos);
        crowds.push_back(&k.crowd);
    }
    for (EconomyZombie& m : state.zombies){
        Body b;
        b.at = EconomyBody(m);
        b.way = WayOf(m);
        b.speed = Standing(b) ? 0.0f : m.speed[m.seg];
        b.id = m.id;
        b.f_zombie = true;
        bodies.push_back(b);
        offs.push_back(&m.off);
        anchors.push_back(m.pos);
        crowds.push_back(&m.crowd);
    }
    /*
        Every pair near enough to matter - SPACING_AHEAD, the furthest anything here reaches - found by a sweep along x:
        the bodies in x order, each against those after it until they are too far east. Ties in x by their place in the
        list, so the pairs come in the same order every time, and each push is summed from where the bodies stood before
        this pass moved any.
    */
    std::vector<int> order(bodies.size());
    std::iota(order.begin(),order.end(),0);
    std::sort(order.begin(),order.end(),[&](int a, int b){
        return (bodies[a].at.x != bodies[b].at.x) ? bodies[a].at.x < bodies[b].at.x : a < b;
    });
    for (size_t i = 0; i < order.size(); i++){
        Body& a = bodies[order[i]];
        for (size_t j = i + 1; j < order.size(); j++){
            Body& b = bodies[order[j]];
            if (b.at.x - a.at.x >= SPACING_AHEAD){
                break;
            }
            vec2 d = a.at - b.at;
            float dist = d.length();
            if (dist >= SPACING_AHEAD){
                continue;
            }
            vec2 n = (dist > 1e-4f) ? d * (1.0f / dist) : Apart(a.id,b.id);
            if (a.f_zombie == b.f_zombie){
                Lean(a,b,dist);
                Lean(b,a,dist);
                Follow(a,b);
                Follow(b,a);
                if (dist < SPACING_RADIUS){
                    float strength = SPACING_PUSH * (SPACING_RADIUS - dist) / SPACING_RADIUS;
                    Shove(a,n,strength);
                    Shove(b,vec2() - n,strength);
                }
            }else if (dist < SPACING_BODY){
                //A person and a zombie: only the zombie moves, and only out of his room.
                float strength = SPACING_PUSH * (SPACING_BODY - dist) / SPACING_BODY;
                if (a.f_zombie){
                    Shove(a,n,strength,true);
                }else{
                    Shove(b,vec2() - n,strength,true);
                }
            }
        }
    }
    //Each offset moved, drawn back toward its route, held to SPACING_MOST - and never where its route could not take it.
    for (size_t i = 0; i < bodies.size(); i++){
        *crowds[i] = bodies[i].crowd;
        vec2 was = *offs[i];
        vec2 off = (was + bodies[i].push * dt) * std::max(0.0f,1.0f - SPACING_RETURN * dt);
        float l = off.length();
        if (l > SPACING_MOST){
            off = off * (SPACING_MOST / l);
        }else if (l < 1e-3f){
            off = vec2();
        }
        if (off.x == 0.0f && off.y == 0.0f){
            *offs[i] = off;
            continue;
        }
        GridPick here = w.picker->Pick(anchors[i]);
        auto clear = [&](const vec2& o){
            return (o.x == 0.0f && o.y == 0.0f) || EconomyStraightClear(w,z,anchors[i],anchors[i] + o,here.plot,-1);
        };
        if (!here.f_hit || here.plot < 0){
            off = vec2();
        }else if (!clear(off)){
            //Against a wall, the one it had if its route still allows it - else halfway, else none.
            off = clear(was) ? was : clear(off * 0.5f) ? off * 0.5f : vec2();
        }
        *offs[i] = off;
    }
}
