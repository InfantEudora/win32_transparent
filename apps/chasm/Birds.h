#ifndef _CHASM_BIRDS_H_
#define _CHASM_BIRDS_H_

#include <stdint.h>
#include <functional>
#include <vector>
#include "type_vec2.h"
#include "type_vec3.h"
#include "type_quat.h"

/*
    BIRDS (docs/birds_plan.md): black flocks that perch in the trees and fly up when someone comes near them.

    VIEW, NOT STATE - the first thing in Chasm that moves and is not simulation. Never saved, hashed or recorded, and
    never drawing from the shared RRandom: every choice is a hash of the sky's seed, the flock, the bird and a count, so
    nothing a flock does can shift the rules' stream. The rules are read - where people are, which trees stand - and
    never written. A load starts the flocks again from their homes.

    Engine-free: the flocks, their birds and the step that moves them. Where a flock can perch and how high the ground
    is are asked of the app through BirdWorld, so this file knows nothing of the forest, the terrain or the economy;
    what is drawn comes out as BirdPose, one per bird, and the moments a sound belongs to as BirdCall.

    A FLOCK is the unit that decides: it settles in a ROOST (a tree and its neighbours, a perch for each bird),
    grows restless and moves on, or is SCARED off it. Its birds only follow - each flies to its own place round the
    flock's ANCHOR, a point that travels to the next roost and circles it before they come down.
*/

//The shape keys of chasm_props.glb's `bird`, by morph index: the order the props script made them in.
#define BIRD_KEY_UP             0       //top of the wingbeat
#define BIRD_KEY_DOWN           1       //bottom of it
#define BIRD_KEY_FOLDED         2       //perched, wings along the body
//The basis is the wings spread level: the glide.

/*
    CURRENT TUNING (birds_plan.md) - figures to be judged in game, not rules. Distances in world units (a villager is
    drawn 0.86 tall), times in seconds of the GAME's clock: a pause holds the birds, 3x flies them three times as fast.
*/
#define BIRD_FLOCK_MIN          4
#define BIRD_FLOCK_MAX          10
#define BIRD_FLOCKS             12      //on a whole map, homed in its forests
#define BIRD_HOME_SPACING       40.0f   //...at least this far apart
/*
    ...but the first few in the woods round the colony, where its people walk and its woodcutters fell: crows live
    beside people, and a flock homed only in the forests' deep cores is never near anyone to be put up.
*/
#define BIRD_COLONY_FLOCKS      3
#define BIRD_COLONY_MIN         12.0f   //from the colony's middle - not over the houses...
#define BIRD_COLONY_MAX         45.0f   //...and not out of its reach
#define BIRD_COLONY_SPACING     20.0f
/*
    ...and those FOLLOW THE AXE: moving on, a colony flock settles near a tree a woodcutter is at (BirdWorld::lures) if
    there is one - clear of the chopping and of where it was last scared, so it is the NEXT tree, or him walking past,
    that puts it up. The user's moment (birds_plan.md): birds flying out of the trees being cut.
*/
#define BIRD_LURE_REACH         16.0f
#define BIRD_ROAM               30.0f   //how far from its home a flock settles
#define BIRD_CRUISE_SPEED       5.0f
#define BIRD_FLUSH_SPEED        8.0f
#define BIRD_CRUISE_CLEAR       3.0f    //flying height over the tallest tree
#define BIRD_SINK_RATE          1.5f    //the most a travelling flock comes down a second: it climbs at once
#define BIRD_FLAP_HZ            3.5f
#define BIRD_FLUSH_FLAP         1.8f    //...times this while scattering
#define BIRD_REACH_WALK         4.0f    //how near someone walking puts a settled flock up
#define BIRD_REACH_CHOP         8.0f    //...an axe at a tree
#define BIRD_REACH_FELLED       12.0f   //...a tree coming down
#define BIRD_WARY_S             20.0f   //how long a scared flock keeps away from where it was scared...
#define BIRD_WARY_REACH         2.0f    //...by this many times the scare's reach
#define BIRD_RESTLESS_MIN_S     30.0f   //a settled flock moves on after a while, so the sky has life with no one near
#define BIRD_RESTLESS_MAX_S     90.0f
#define BIRD_SCATTER_MIN_S      1.5f    //scared: flying apart, before they flock again
#define BIRD_SCATTER_MAX_S      3.0f
#define BIRD_CIRCLE_RADIUS      5.0f    //over the next roost before coming down
#define BIRD_CIRCLE_S           4.0f
#define BIRD_STAGGER_S          0.4f    //scared: the nearest bird first, the last this much later

//What the app knows and the birds ask.
struct BirdPerch{
    vec3 at;                    //where its feet go
    int tree = -1;              //the prop it is on, ForestData::props
};
struct BirdRoost{
    int tree = -1;              //the tree settled on...
    vec3 at;                    //...its top
    std::vector<BirdPerch> perches;     //its perches and its neighbours': where the flock comes down
};
//Something that puts a flock up: a point, and how near it reaches.
struct BirdScare{
    vec2 at;
    float reach = 0.0f;
};
struct BirdWorld{
    std::function<float(const vec2&)> ground;       //the ground's height at a point
    //A roost within `reach` of `centre` clear of every `avoid`, `draw` choosing among those that are; false: none.
    std::function<bool(const vec2& centre, float reach, const std::vector<BirdScare>& avoid, uint32_t draw,
                       BirdRoost& out)> roost;
    std::function<bool(int tree)> standing;         //still standing and drawn: a felled tree puts its birds up
    float tree_top = 6.0f;                          //the tallest tree, which a travelling flock clears
    std::vector<vec2> lures;                        //trees woodcutters are at: where a colony flock goes
};

#define BIRD_PERCHED    0
#define BIRD_FLYING     1
#define BIRD_LANDING    2       //on its way down to its perch

#define FLOCK_SETTLED   0       //in its roost, every bird perched or on its way down
#define FLOCK_TRAVEL    1       //the anchor on its way to the next roost
#define FLOCK_CIRCLE    2       //...and round it, before they come down
#define FLOCK_SCATTER   3       //scared: every bird for itself, away from the scare

struct Bird{
    uint32_t id = 0;
    int state = BIRD_PERCHED;
    vec3 pos;
    vec3 vel;
    float yaw = 0.0f;           //the way it faces, radians about +Y from +Z (the villagers' rule)
    float bank = 0.0f;
    float phase = 0.0f;         //the wingbeat, radians
    float rate = 1.0f;          //its own share of BIRD_FLAP_HZ, so a flock never beats in step
    float amp = 0.0f;           //how hard it beats, 0 the glide
    float fold = 1.0f;          //1 folded
    float glide = 0.0f;         //seconds of glide left; beating while 0...
    int beats = 0;              //...for this many beats more
    float delay = 0.0f;         //seconds before it goes, when the flock goes
    vec2 slot;                  //its place round the anchor
    vec2 flee;                  //scattering: its way out
    BirdPerch perch;            //where it sits or is going to sit
    float idle = 0.0f;          //perched: seconds to its next fidget
    float look = 0.0f;          //...and the way its head is turned off its yaw
};

struct BirdFlock{
    uint32_t id = 0;
    int state = FLOCK_SETTLED;
    bool f_colony = false;      //homed by the colony: follows the axe (BIRD_LURE_REACH)
    vec2 home;                  //the forest it keeps to
    vec3 anchor;                //where the flock is: a point in the air the birds fly round
    vec3 anchor_vel;
    BirdRoost roost;            //where it is settled, or going
    float timer = 0.0f;         //seconds left of what it is doing
    float clock = 0.0f;         //seconds since it was made: the slow drift of the birds' places round the anchor
    float floor = 0.0f;         //the ground under the anchor: no bird in flight goes below it
    float circle = 0.0f;        //circling: the angle round the roost
    BirdScare wary;             //where it was last scared...
    float wary_left = 0.0f;     //...and how long it keeps away
    uint32_t draws = 0;         //its count for the hash: each draw a new one
    uint32_t flushes = 0;       //times scared up, for the tool
    std::vector<Bird> birds;
};

//What is drawn of a bird.
struct BirdPose{
    vec3 pos;
    quat turn;
    float up = 0.0f;            //the shape keys' factors
    float down = 0.0f;
    float folded = 0.0f;
};

//A moment a sound belongs to (birds_plan.md step 3): a flock going up, a bird calling.
#define BIRD_CALL_FLUSH 0       //a flock scared up, at its middle - the wing burst
#define BIRD_CALL_CAW   1       //one bird calling
struct BirdCall{
    int kind = BIRD_CALL_CAW;
    vec3 at;
    uint32_t bird = 0;          //who: a sound's instance, for its draws
};

class BirdSky{
public:
    void Reset(uint32_t seed);
    //A flock of `count` settled in a roost near `home` - false where there is none.
    bool AddFlock(const BirdWorld& w, const vec2& home, int count, bool f_colony = false);
    //`dt` game seconds. `scares` are this step's: whoever is walking, chopping, or a tree just down.
    void Step(const BirdWorld& w, float dt, const std::vector<BirdScare>& scares);
    //Every flock in reach of `s` scared up now, whatever it is doing (the tool's `flush`).
    void Flush(const BirdWorld& w, const BirdScare& s);
    void Poses(std::vector<BirdPose>& out) const;
    const std::vector<BirdFlock>& Flocks() const { return flocks; }
    int Count() const;
    //The calls since the last Take, oldest first; taking empties it.
    void TakeCalls(std::vector<BirdCall>& out);

private:
    uint32_t seed = 0;
    uint32_t next_id = 1;
    std::vector<BirdFlock> flocks;
    std::vector<BirdCall> calls;
    float Draw(BirdFlock& f, uint32_t salt);
    void Settle(BirdFlock& f);
    void TakeOff(BirdFlock& f, float stagger);
    void Scare(const BirdWorld& w, BirdFlock& f, const BirdScare& s);
    void NextRoost(const BirdWorld& w, BirdFlock& f, const std::vector<BirdScare>& scares);
    void StepFlock(const BirdWorld& w, BirdFlock& f, float dt, const std::vector<BirdScare>& scares);
    void StepBird(const BirdWorld& w, BirdFlock& f, Bird& b, float dt);
    void StepWings(BirdFlock& f, Bird& b, float dt);
};

#endif
