#include "Economy.h"
#include "ChasmWorld.h"
#include "Walkers.h"
#include <algorithm>
#include <cmath>
#include <numeric>

/*
    SPACING (Economy.h; zombies_plan.md, "Step 1b"): the bodies' offsets from their routes, a tick at a time - a group
    on one path side by side, a crowd standing apart, two meeting on a road passing each other on the right.
*/

namespace {

//A body in the pass: where it stands, which way it walks (nothing while it stands), and what pushes it this tick.
struct Body{
    vec2 at;
    vec2 way;
    uint32_t id = 0;
    bool f_zombie = false;
    vec2 push;
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
    they meet. One walking his way ahead of him is left be until he is that near: a group on one path.
*/
void Lean(Body& b, const Body& other, float dist){
    if (Standing(b) || dist < 1e-4f){
        return;
    }
    float ahead = (other.at - b.at).dot(b.way) / dist;
    bool f_coming = Standing(other) || other.way.dot(b.way) < 0.0f;
    if (ahead <= 0.0f || (dist >= SPACING_RADIUS && !f_coming)){
        return;
    }
    float strength = SPACING_PUSH * SPACING_KEEP_RIGHT * (SPACING_AHEAD - dist) / SPACING_AHEAD;
    b.push = b.push + RightOf(b.way) * (strength * ahead);
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
    std::vector<vec2> anchors;          //...and where its route has it
    for (EconomyWorker& k : state.workers){
        if (EconomyIndoors(k)){
            k.off = vec2();
            continue;
        }
        if ((k.with && EconomyStageAt(k.born,now) == LIFE_BABY) || !k.route_layer.empty()){
            SettleBack(k.off,dt);
            continue;
        }
        Body b;
        b.at = EconomyBody(k);
        b.way = WayOf(k);
        b.id = k.id;
        bodies.push_back(b);
        offs.push_back(&k.off);
        anchors.push_back(k.pos);
    }
    for (EconomyZombie& m : state.zombies){
        Body b;
        b.at = EconomyBody(m);
        b.way = WayOf(m);
        b.id = m.id;
        b.f_zombie = true;
        bodies.push_back(b);
        offs.push_back(&m.off);
        anchors.push_back(m.pos);
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
