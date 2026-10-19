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
/*
    WANDERERS (the user's, 2026-10-17: "a few birds fly around in small flocks and single"): ones, twos and threes that
    are mostly in the air - a short sit, then a long flight to trees far off, circling longer and as often flying on as
    coming down. Half are homed round the colony, so there are some where the player looks.
*/
#define BIRD_WANDERERS          8
#define BIRD_WANDER_ALONE       0.4f    //the share that are one bird; the rest two or three
#define BIRD_WANDER_COLONY      4       //of them, homed within BIRD_WANDER_ROAM of the colony's middle
#define BIRD_WANDER_ROAM        70.0f   //how far from home they settle...
#define BIRD_WANDER_AWAY        25.0f   //...at least this far from where they last sat
#define BIRD_WANDER_SIT_MIN_S   8.0f
#define BIRD_WANDER_SIT_MAX_S   25.0f
#define BIRD_WANDER_CIRCLE_MAX_S 12.0f  //circling from BIRD_CIRCLE_S to this
#define BIRD_WANDER_FLY_ON      0.5f    //the chance that, done circling, they fly on rather than come down
#define BIRD_WANDER_LIFT        4.0f    //up to this much higher than the flocks fly
//Travelling, the flock's way turns rather than snaps, and wanders a little off the straight line - more for a wanderer.
#define BIRD_TURN_RATE          1.0f    //radians a second
#define BIRD_WOBBLE             0.25f   //radians
#define BIRD_WANDER_WOBBLE      0.6f
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
/*
    CALLS (birds_plan.md step 3): each flock on a clock of its own - now and then sitting, more often in the air - and
    scared up, the sentinel at once and a second bird answering a moment later, so the two bouts overlap. A call is a
    bout of caws or a chatter (the user's takes are runs, not single caws), so these are gaps between bouts.
*/
#define BIRD_CALL_SETTLED_MIN_S 15.0f
#define BIRD_CALL_SETTLED_MAX_S 45.0f
#define BIRD_CALL_FLYING_MIN_S  5.0f
#define BIRD_CALL_FLYING_MAX_S  14.0f
#define BIRD_CALL_ANSWER_MIN_S  0.4f
#define BIRD_CALL_ANSWER_MAX_S  1.0f
#define BIRD_CALL_TAKEOFF       0.5f    //the chance that a flock moving on of its own accord calls as it goes

/*
    SEASONS (step 4): crows gather in autumn and winter, so a flock grows to its own size times the season's GATHER
    (BirdWorld::gather; the wanderers stay ones, twos and threes). Birds JOIN a flock in the air, a few at a time
    flying in from off its edge, and LEAVE it one at a time when the gathering is over - a season turning is seen as
    birds coming and going, never as a flock that has changed between two frames.
*/
#define BIRD_GATHER_AUTUMN      1.5f    //spring and summer are 1
#define BIRD_GATHER_WINTER      2.0f
#define BIRD_JOIN_FROM          35.0f   //how far off joining birds start...
#define BIRD_JOIN_GROUP         3       //...up to this many together
#define BIRD_JOIN_GAP_MIN_S     3.0f    //between one arrival or departure and the next
#define BIRD_JOIN_GAP_MAX_S     10.0f
#define BIRD_LEAVE_FAR          40.0f   //a leaving bird is gone this far from its flock

/*
    CARRION (zombies_plan.md step 6, the user's 2026-10-19): crows come down on the dead. A body lying still
    (BirdWorld::carrion) near a flock moving on wins over the trees more often than not; the flock comes down on the
    ground round it and a few on it, and pecks there. It goes up as anyone comes near, as from a tree, and moves on once
    the body is no longer lying still. Not the wanderers.
*/
#define BIRD_CARRION_REACH      40.0f   //from the flock, the furthest body it comes down on
#define BIRD_CARRION_TAKE       0.7f    //the chance a flock moving on takes a body in reach over the trees
#define BIRD_CARRION_RING       0.6f    //its perches on the ground round the body's middle, about this far off...
#define BIRD_CARRION_ON         0.16f   //...and a few on it, this high over the ground
#define BIRD_CARRION_PERCHES    9       //...in all
#define BIRD_PECK_S             0.4f    //a peck: the head down and up again
#define BIRD_PECK_SHARE         0.55f   //of a feeding bird's fidgets
#define BIRD_PECK_DOWN          0.9f    //radians, nose down, at the bottom of a peck

//Whose calls a flock has: the table's rows pick the takes by it.
#define BIRD_VOICE_CROW         0
#define BIRD_VOICE_MAGPIE       1       //the wanderers', for now (the user's, 2026-10-18) - before magpies are a kind of their own

//What the app knows and the birds ask.
struct BirdPerch{
    vec3 at;                    //where its feet go
    int tree = -1;              //the prop it is on, ForestData::props
};
struct BirdRoost{
    int tree = -1;              //the tree settled on...
    vec3 at;                    //...its top
    std::vector<BirdPerch> perches;     //its perches and its neighbours': where the flock comes down
    uint32_t carrion = 0;       //CARRION: the body it is down on instead, 0 a tree's (its perches on the ground)
};
//A body lying still, for the crows (CARRION above).
struct BirdCarrion{
    uint32_t id = 0;            //the app's, never 0
    vec2 at;                    //its middle
    vec2 along;                 //...and the way it lies, unit, feet to head
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
    std::vector<BirdCarrion> carrion;               //the dead lying still: where a flock may come down instead
    float gather = 1.0f;                            //the season's: a flock's size by its own (SEASONS above)
    /*
        With no roost to be had - over the title's diorama, captured with no perches - a flock flies on to a point in
        the air within this of its home, and so passes over and on. 0, the world's: it waits over its last roost.
    */
    float sky_reach = 0.0f;
};

#define BIRD_PERCHED    0
#define BIRD_FLYING     1
#define BIRD_LANDING    2       //on its way down to its perch

#define BIRD_ROLE_FOREST    0   //a flock homed in a forest's core
#define BIRD_ROLE_COLONY    1   //...round the colony, following the axe (BIRD_LURE_REACH)
#define BIRD_ROLE_WANDER    2   //one to three birds, mostly in the air (BIRD_WANDERERS)

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
    float peck = 0.0f;          //...feeding on carrion: seconds of a peck left
    bool f_leaving = false;     //flying off along `flee`, no longer the flock's: gone at BIRD_LEAVE_FAR
};

struct BirdFlock{
    uint32_t id = 0;
    int state = FLOCK_SETTLED;
    int role = BIRD_ROLE_FOREST;
    int voice = BIRD_VOICE_CROW;
    float heading = 0.0f;       //travelling: the anchor's way, radians about +Y from +Z
    float lift = 0.0f;          //how much higher than BIRD_CRUISE_CLEAR it flies: a wanderer's own
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
    float call = 0.0f;          //seconds to its next call
    int size = 1;               //its own count, at a gather of 1
    float join = 0.0f;          //seconds to the next bird joining or leaving
    uint32_t draws = 0;         //its count for the hash: each draw a new one
    uint32_t flushes = 0;       //times scared up, for the tool
    uint32_t calls = 0;         //...and calls made
    std::vector<Bird> birds;
};

//What is drawn of a bird.
struct BirdPose{
    vec3 pos;
    quat turn;
    float up = 0.0f;            //the shape keys' factors
    float down = 0.0f;
    float folded = 0.0f;
    //The tree it sits on or is landing in, and how far it moves with it as the tree sways in the wind (weather_plan.md):
    //1 perched, coming up to it over the last unit of a landing.
    int tree = -1;
    float ride = 0.0f;
};

//A moment a sound belongs to (birds_plan.md step 3): a flock going up, a bird calling.
#define BIRD_CALL_FLUSH 0       //a flock scared up, at its middle - the wing burst
#define BIRD_CALL_CAW   1       //one bird calling
struct BirdCall{
    int kind = BIRD_CALL_CAW;
    vec3 at;
    uint32_t bird = 0;          //who: a sound's instance, for its draws
    int voice = BIRD_VOICE_CROW;
};

class BirdSky{
public:
    void Reset(uint32_t seed);
    //A flock of `count` (by the season's gather) settled in a roost near `home` - false where there is none.
    bool AddFlock(const BirdWorld& w, const vec2& home, int count, int role = BIRD_ROLE_FOREST);
    //...or in the air over it, on its way to a roost or, with none, a point in the sky (BirdWorld::sky_reach).
    void AddFlockInAir(const BirdWorld& w, const vec2& home, int count, int role = BIRD_ROLE_FOREST);
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
    uint32_t next_bird = 1;     //birds join and leave, so their ids are the sky's, not a place in a flock
    std::vector<BirdFlock> flocks;
    std::vector<BirdCall> calls;
    float Draw(BirdFlock& f, uint32_t salt);
    void MakeBird(BirdFlock& f, Bird& b, int index);   //its own wingbeat, place round the anchor and way of facing
    int Wanted(const BirdWorld& w, const BirdFlock& f) const;  //how many it should be now, by the season
    void Gather(const BirdWorld& w, BirdFlock& f, float dt);  //in the air: a bird or a few joining, or one leaving
    float SitTime(BirdFlock& f);    //how long it stays settled this time: a wanderer's sits are short
    void Call(BirdFlock& f, const Bird& b);
    float CallGap(BirdFlock& f);    //to its next call, sitting or flying as it is now
    void Settle(BirdFlock& f);
    void TakeOff(BirdFlock& f, float stagger);
    void Scare(const BirdWorld& w, BirdFlock& f, const BirdScare& s);
    void NextRoost(const BirdWorld& w, BirdFlock& f, const std::vector<BirdScare>& scares);
    void StepFlock(const BirdWorld& w, BirdFlock& f, float dt, const std::vector<BirdScare>& scares);
    void StepBird(const BirdWorld& w, BirdFlock& f, Bird& b, float dt);
    void StepWings(BirdFlock& f, Bird& b, float dt);
};

#endif
