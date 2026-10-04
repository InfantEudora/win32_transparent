#ifndef _ARCHER_CREATURES_H_
#define _ARCHER_CREATURES_H_

#include <stdint.h>
#include <vector>
#include "Spline.h"
#include "Vine.h"

/*
    Creatures that follow paths through the level - docs/creature_plan.md, sections 1-3. Engine-free,
    like Vine and Wind: stage_test builds every kind of path and walks crawlers along them with
    nothing else linked.

    --- A PATH ------------------------------------------------------------------------------------
    A core Spline through `points`, and beside each point the way a crawler's BACK faces there -
    `ups`, out of the surface it walks on. Between points the up is blended by distance along the
    curve and squared to the tangent, so a body is always along the path with its belly on the
    surface. Three sources, which differ only in where the points and ups come from:

      HAND      a point list and one up, as a level declares it - the default for anything whose
                route is design rather than dressing
      VINE      a vine's own curve (DeclareVines, BuildVineSpline) with the crawler held off its
                bark and turning round it as it climbs - a helix round the trunk, the up pointing
                out from the trunk's axis
      SURFACE   the creeper's walk (GrowVine) against a field, with a crawler's numbers: up a
                face, over a lip, across, and down - the up the field's normal at each point

    --- A CRAWLER ---------------------------------------------------------------------------------
    A small spider: a distance `s` along one path, a direction and a speed. AMBIENT - view-only,
    seeded, nothing in the rules reads it, and its objects are visual-only, so a replay's state
    never sees one. What makes it read as a spider is that it does NOT glide: it darts, stops,
    sometimes turns back, and twitches while stopped. So it lives as RUNS and PAUSES - a run is a
    burst at its speed for a while, a pause a hold with now and then a twitch, a step of a couple
    of centimetres one way and back - and the legs cycle off the DISTANCE it has covered
    (`travelled`), so the feet never slide and a stopped spider's legs stop.

    Ticks, never seconds: the step is ARCHER_DT. Every random draw is the crawler's own xorshift,
    seeded from its index and the swarm's seed, so a reseed lays the same spiders on the same paths.
*/

enum CreaturePathKind{
    CREATURE_PATH_HAND = 0,
    CREATURE_PATH_VINE,
    CREATURE_PATH_SURFACE,
    CREATURE_PATH_KIND_COUNT
};
const char* CreaturePathKindName(int kind);

struct CreaturePath{
    int   kind = CREATURE_PATH_HAND;
    std::vector<vec3> points;
    std::vector<vec3> ups;          //one per point, unit: the way a crawler's back faces there
    //A closed loop, walked round and round; open, a crawler turns back at either end.
    bool  f_loop = false;
    //Off the surface or the curve - round a vine, the orbit's radius over the bark. For the record.
    float offset = 0.0f;

    //Fits the spline and each point's distance along it. False with fewer than two points.
    bool  Build();
    bool  IsBuilt() const { return spline.IsBuilt(); }
    float Length() const { return spline.GetLength(); }
    //Where a crawler at distance s is, the way along the path (unit) and its up (unit, square to it).
    void  At(float s, vec3& out_pos, vec3& out_tangent, vec3& out_up) const;

    Spline spline;
    std::vector<float> point_s;     //each point's distance along the spline
};

//A hand-declared path: `points`, with `up` everywhere. A loop closes back onto the first point.
bool  CreaturePathFromPoints(const std::vector<vec3>& points, const vec3& up, bool f_loop, CreaturePath& out);
/*
    Round a vine, between s0 and s1 along its curve (clamped to it): held `clearance` off the bark
    (the trunk's radius there, VineRadiusAt, plus it), turning once every `pitch` units it climbs,
    starting `phase` radians round from the curve's frame normal. Sampled every `step`.
*/
bool  CreaturePathAroundVine(const VinePath& vine, const VineParams& params, float clearance, float pitch,
                             float phase, float s0, float s1, CreaturePath& out, float step = 0.10f);
/*
    Along a surface: the creeper's walk from `anchor` on a face whose outward normal is `normal`,
    `length` long, keeping `gap` off the surface, with the walk's wander hashed on `seed`. Its depth
    is the anchor's: the creeper does not wander into the screen. False if it could not walk.
*/
bool  CreaturePathOverSurface(const VineField& field, const vec3& anchor, const vec3& normal, int seed,
                              float length, float gap, CreaturePath& out);

struct CrawlerParams{
    float speed_min = 0.35f;        //units/s while running, at the swarm's scale
    float speed_max = 0.90f;
    int   run_min = 20;             //ticks a run lasts
    int   run_max = 110;
    int   pause_min = 15;           //and a pause
    int   pause_max = 150;
    float turn_chance = 0.25f;      //that a run starts the other way
    float twitch_chance = 0.03f;    //per tick of a pause, that it twitches
    int   twitch_ticks = 6;         //out and back
    float twitch_size = 0.03f;      //how far a twitch steps, units
    //Distance a full leg cycle covers. Set from the spider's size by the app; the feet are judged
    //by eye against the ground they cross.
    float stride = 0.20f;
};

struct Crawler{
    int   path = -1;
    float s = 0.0f;
    float dir = 1.0f;               //+1 toward the path's end, -1 toward its start
    float speed = 0.5f;             //this run's
    float travelled = 0.0f;         //every unit it has moved, either way - what the legs cycle off
    int   run_ticks = 0;            //left in this run; 0 while paused
    int   pause_ticks = 0;          //left in this pause
    int   twitch = 0;               //ticks into a twitch, 0 none
    float twitch_dir = 1.0f;
    uint32_t rng = 1;
};

class CrawlerSwarm{
public:
    std::vector<CreaturePath> paths;
    std::vector<Crawler> crawlers;
    CrawlerParams params;

    //`count` crawlers on path `path`, spread along it at hashed distances, half each way.
    void  Seed(int path, int count, uint32_t seed);
    void  Clear() { crawlers.clear(); }
    //One tick, every crawler.
    void  Step();
    //Crawler i's place: position, the way it faces (unit, along the path the way it is going) and
    //its back (unit, square to that).
    void  Pose(int i, vec3& out_pos, vec3& out_forward, vec3& out_up) const;
    //How far through its leg cycle crawler i is, 0..1, off the distance it has covered.
    float LegPhase(int i) const;
private:
    void  StepOne(Crawler& c);
    float Rand01(Crawler& c);
};

#endif
