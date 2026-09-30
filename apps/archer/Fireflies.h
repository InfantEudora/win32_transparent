#ifndef _ARCHER_FIREFLIES_H_
#define _ARCHER_FIREFLIES_H_

#include "Wind.h"

#include <stdint.h>
#include <vector>

/*
    Fireflies - docs/wind_plan.md step 6. A few dozen glowing specks that live where the plants are
    thickest, drift in slow loops, flash in the rhythm real ones do, and light the ground around
    them through a small group of point lights.

    Engine-free, like Wind, Leaves and Streaks: the app hands in the HOMES (from the foliage
    scatter - where the plants are, weighted by how shaded they grew), draws each firefly as a
    glow, and places its point lights where LightGroup says.

    --- HOW ONE MOVES -----------------------------------------------------------------------------
    It belongs to a home and steers toward a point that loops around it - a slow Lissajous figure,
    a unit or so across and a little above the plants - so its flight is lazy and never leaves.
    The wind pushes it a little (`wind_follow`), the blocks push it out, and when its home drifts
    out of the view it fades away and comes back belonging to a home in the view.

    --- HOW ONE FLASHES ---------------------------------------------------------------------------
    A flash clock runs round a period of about five seconds. At the top of it comes a burst of one
    to three quick flashes - a fast rise and a slower glow-down, the J-shaped flash of a Photinus -
    and between bursts only a faint ember. The periods are all close to one another on purpose:
    when a fly flashes, others near it nudge their clocks forward (`sync`), which is the pulse
    coupling that makes real swarms fall into step. Over half a minute or so a cluster starts to
    flash together - never perfectly, because the periods differ.

    --- DETERMINISTIC -----------------------------------------------------------------------------
    Every random draw hashes the firefly's index and its spawn count. Time is in ticks.
*/

//Somewhere a firefly may live - a plant, from Foliage.
struct FireflyHome{
    float x = 0.0f, y = 0.0f, z = 0.0f;     //y is the surface it stands on
    float weight = 1.0f;                    //how likely a firefly is to live here
};

struct FireflyParams{
    int   count        = 40;
    float pad          = 0.5f;      //homes this fraction beyond the view count as in view
    float roam_x       = 1.3f;      //the loop around the home, half-widths
    float roam_y       = 0.5f;
    float roam_z       = 0.8f;
    float height_min   = 0.5f;      //the loop's centre above the home's surface
    float height_max   = 1.8f;
    float steer        = 1.8f;      //how hard it heads for its loop point, 1/s
    float wind_follow  = 0.15f;     //share of the wind it drifts with
    float period       = 5.0f;      //seconds between bursts, on average
    float period_jitter= 0.35f;     //+- seconds
    float sync         = 0.35f;     //how hard a flash pulls neighbours' clocks forward, 0..1
    float sync_radius  = 3.5f;
    float ember        = 0.08f;     //glow between flashes
};

struct Firefly{
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float vx = 0.0f, vy = 0.0f, vz = 0.0f;
    int   home = -1;
    float clock = 0.0f;             //seconds into its flash period
    float period = 5.0f;
    int   burst = 1;                //flashes per burst
    float loop[6] = {};             //the Lissajous: three rates, three phases
    float height = 1.0f;            //the loop's centre above the home
    float fade = 0.0f;              //0..1 - in on arrival, out when its home leaves the view
    bool  f_leaving = false;
    float brightness = 0.0f;        //the glow right now, 0..1 (ember included)
    uint32_t spawns = 0;
};

//One corner of a firefly's glow quad, as the app's shader wants it.
struct FireflyVertex{
    float x, y, z;
    float u, v;         //-1..1 across the quad
    float brightness;   //the fly's glow, 0..1
};

//One of the group's point lights, as LightGroup places it.
struct FireflyLight{
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float intensity = 0.0f;         //summed brightness of the flies it stands for
};

class FireflySwarm{
public:
    //The places fireflies live. Replacing them sends every firefly to a new one.
    void SetHomes(const std::vector<FireflyHome>& homes);
    //One tick. The view is what the camera sees.
    void Step(const WindField& wind, int64_t tick, float x0, float y0, float x1, float y1);
    /*
        The light group: `n` lights for the fireflies in the view, one per vertical band of it, each
        at the brightness-weighted centre of the flies in its band, with their summed brightness.
        A band with no fly lit has intensity 0 - the app smooths and dims them from there.
    */
    void LightGroup(int n, float x0, float y0, float x1, float y1, std::vector<FireflyLight>& out) const;

    /*
        Each lit firefly as a quad facing the eye, two triangles, `out` cleared first. The quad is
        `size` across at full brightness and a third of that at the ember, since the halo is what
        grows when a fly flashes.
    */
    void BuildGlows(float eye_x, float eye_y, float eye_z, float size, std::vector<FireflyVertex>& out) const;

    //Brightness from a flash clock, for the tests and anyone tuning the pattern.
    float Glow(float clock, int burst) const;

    std::vector<Firefly> flies;
    FireflyParams params;

private:
    std::vector<FireflyHome> homes;
    //Picks a home in the region by weight and places fly i near it, faded out.
    bool Spawn(int i, float x0, float y0, float x1, float y1);
    bool InRegion(int home, float x0, float y0, float x1, float y1) const;
};

#endif
