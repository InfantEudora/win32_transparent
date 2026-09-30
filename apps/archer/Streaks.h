#ifndef _ARCHER_STREAKS_H_
#define _ARCHER_STREAKS_H_

#include "Wind.h"

#include <stdint.h>
#include <vector>

/*
    Wind streaks - docs/wind_plan.md step 5. Subtle but visible: a few dozen thin pale ribbons, each
    the recent path of a speck of air, drawn over the scene and fading in and out. They follow the
    field exactly (a tracer has no inertia), so over open ground they run straight and in the lee
    of a step they curl, which is the one place the eye is told there is an eddy.

    Engine-free, like Wind and Leaves. StreakSwarm moves the tracers and turns them into ribbon
    triangles - positions, a 0..1 coordinate along and -1..1 across, and an alpha per vertex -
    which the app copies into a mesh and draws through its own shader.

    --- WHERE THEY APPEAR -------------------------------------------------------------------------
    A dead streak is reborn at a random spot in the view, but a spot is only taken with a chance
    that rises with the gust passing over it (`gust_bias`), so a gust front arrives as a flurry of
    streaks - which is the warning a balance mechanic will want later. Still air draws nothing: a
    streak's alpha follows the local wind speed against the mean, and one in dead air fades out.

    --- DETERMINISTIC -----------------------------------------------------------------------------
    Every random draw hashes the streak's index and its spawn count. Durations are ticks.
*/

struct StreakParams{
    int   count        = 30;        //alive or waiting, in the view
    int   points       = 32;        //trail samples - the ribbon's resolution
    int   sample_ticks = 2;         //ticks between samples, so a trail is points*sample_ticks long
    int   life_min     = 120;       //ticks a streak lives, drawn between these
    int   life_max     = 240;
    float width        = 0.07f;     //at its widest, world units
    float alpha        = 0.30f;     //at its most opaque
    float z_min        = -1.2f;     //depth - some behind her, some in front
    float z_max        = 1.8f;
    float gust_bias    = 0.7f;      //0: spawn anywhere; 1: only where a gust is passing
    float pad          = 0.1f;      //spawn area: the view grown by this fraction
};

struct Streak{
    bool     f_alive = false;
    float    x = 0.0f, y = 0.0f, z = 0.0f;   //the head
    int      age = 0, life = 0;             //ticks
    float    strength = 0.0f;               //local speed against the mean, smoothed, 0..1
    uint32_t spawns = 0;
    std::vector<float> trail;               //x,y pairs, oldest first, at most `points` of them
};

//One ribbon vertex, as the app's shader wants it.
struct StreakVertex{
    float x, y, z;
    float u;            //0 at the tail, 1 at the head
    float v;            //-1..1 across
    float alpha;
};

class StreakSwarm{
public:
    //One tick: move the living, age them, respawn the dead. The view is what the camera sees.
    void Step(const WindField& wind, int64_t tick, float x0, float y0, float x1, float y1);
    /*
        The ribbons as triangles, three StreakVertex per triangle, `out` cleared first. `eye` is
        the camera position: each ribbon is widened across its path AND the line to the eye, so it
        faces the camera whether the side camera or the orbit one is looking.
    */
    void BuildRibbons(float eye_x, float eye_y, float eye_z, std::vector<StreakVertex>& out) const;

    std::vector<Streak> streaks;
    StreakParams params;

private:
    void Spawn(const WindField& wind, int i, int64_t tick, float x0, float y0, float x1, float y1);
};

#endif
