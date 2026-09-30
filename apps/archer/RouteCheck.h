#ifndef _ARCHER_ROUTECHECK_H_
#define _ARCHER_ROUTECHECK_H_

#include "Stage.h"

#include <functional>
#include <string>
#include <vector>

/*
    ROUTE CHECKS: proving a way through the level is passable, by playing it against the rules.

    docs/bridge_crumble_plan.md section 6. It started as the solver that found the pad-to-canopy
    recording (2026-09-27), and it is here so that every crossing the level is designed around -
    the spring plants, and later the stepping stones, the chase, each bridge and each detour - is
    held to it by `make rules`. Retune a spring or move a block, and the test says at once whether
    the way is still open and how much timing it leaves.

    A ROUTE is a start and a list of LEGS. A leg is one hop: some ticks of waiting (standing, or
    walking one way), a jump press, the jump held and a direction held for some ticks of the
    flight, until she is standing again - and a GOAL for where that has to be. Each leg is SEARCHED:
    every wait in its range against every air-hold, and what it keeps is the widest RUN of
    consecutive waits that all reach the goal, pressed at the middle of that run. The run's length
    is the leg's TIMING WINDOW - how many ticks a player can be early or late and still make it.

    Legs are chosen with a little look-ahead: the best few candidates of each are tried against
    the rest of the route, because the leg with the widest window can leave her somewhere the
    next leg cannot be made from.

    EVERYTHING IS DRIVEN FROM A KEY TIMELINE - which keys are down each tick - and a tick's
    ArcherInput is derived from it as ApplicationArcher::GatherInput does, jump_pressed only on the
    tick the key goes down. So a solved route is exactly what a recording of it replays: the
    timeline can be written out as one (see core/InputRecording.h) and it plays the same.

    Engine-free, like everything `make rules` builds.
*/

//The keys a route uses, down or not on one tick.
struct RouteKeys{
    bool left = false;
    bool right = false;
    bool jump = false;
    bool aim_down = false;      //the arrow key: a stomp when held through a fall onto a spring
};

//Tick `t` of a timeline as the game would see it.
ArcherInput RouteInput(const std::vector<RouteKeys>& timeline, size_t t);

struct RouteLeg{
    std::string name;
    //Where the hop has to end - checked on the tick she is standing again.
    std::function<bool(const Stage&)> goal;
    /*
        Optional: among the hops that reach the goal, prefer the higher score over the wider
        window. For a leg whose job is to set up the next - a pumping bounce, scored by how deep it
        will sink the pad. A scored leg's window is still measured, but it is not a timing check.
    */
    std::function<float(const Stage&)> score;
    //Held while waiting: 0 stands, +1 walks right, -1 left.
    int   walk = 0;
    //The flight's direction, held from the press for `air` ticks: +1 right, -1 left.
    int   dir = 1;
    int   wait_min = 1;         //at least 1, so the jump key is up the tick before the press
    int   wait_max = 45;
    int   air_min = 0;
    int   air_max = 60;
    int   air_step = 4;
    int   hold = 60;            //ticks the jump is held from the press - 60 is the whole flight
    bool  f_try_stomp = false;  //also try the aim-down arrow held from the apex to the landing
    /*
        Pull up if she catches a ledge: the jump let go for a tick and pressed again, which is what
        TickHang climbs on. For a leg that goes up a face too high to jump onto - a detour out of a
        pit - where the hop ends standing on top only after the hang and the climb.
    */
    bool  f_climb = false;
    bool  f_timed = true;       //its window counts against the route's minimum
};

struct RouteLegResult{
    std::string name;
    int   wait = 0;             //the chosen press, in ticks after the leg began
    int   air = 0;
    bool  f_stomp = false;
    int   window = 0;
    bool  f_timed = true;
    float score = 0.0f;
    int   ticks = 0;            //how long the leg took, landing included
};

struct RouteResult{
    bool  f_passable = false;
    std::string failed_leg;     //the first leg nothing reached, when it is not passable
    std::vector<RouteLegResult> legs;
    std::vector<RouteKeys> timeline;
    int   narrowest_window = 0; //over the timed legs
};

/*
    Solves `legs` from `start` (a Stage as it would stand at the start of a recording: level
    built, archer placed). `look_ahead` is how many of each leg's best candidates are tried before
    giving up on the leg before it.
*/
RouteResult SolveRoute(const Stage& start, const std::vector<RouteLeg>& legs, int look_ahead = 6);

//Plays `timeline` from `start` and returns where it ends - the check that a solved route is what
//a replay of it does.
Stage PlayRoute(const Stage& start, const std::vector<RouteKeys>& timeline);

#endif
