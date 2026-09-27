#include "RouteCheck.h"

#include <algorithm>

ArcherInput RouteInput(const std::vector<RouteKeys>& timeline, size_t t){
    ArcherInput in;
    const RouteKeys& k = timeline[t];
    bool prev_jump = (t > 0) ? timeline[t - 1].jump : false;
    float move = 0.0f;
    if (k.left){  move -= 1.0f; }
    if (k.right){ move += 1.0f; }
    in.move_axis = move;
    in.aim_axis = k.aim_down ? -1.0f : 0.0f;
    in.f_jump_down = k.jump;
    in.f_jump_pressed = k.jump && !prev_jump;
    return in;
}

static void PlayFrom(Stage& s, const std::vector<RouteKeys>& tl, size_t from){
    for (size_t t = from; t < tl.size(); t++){
        StageEvents e;
        s.Tick(RouteInput(tl,t),e);
    }
}

Stage PlayRoute(const Stage& start, const std::vector<RouteKeys>& timeline){
    Stage s = start;
    PlayFrom(s,timeline,0);
    return s;
}

static RouteKeys Held(int dir){
    RouteKeys k;
    k.left = (dir < 0);
    k.right = (dir > 0);
    return k;
}

/*
    One hop, appended to `tl`, from `s` - the state after `tl` so far. `wait` ticks of the leg's
    walk, then the press, the jump held `leg.hold` ticks and the direction `air` ticks, and the
    aim-down arrow from the apex on if `stomp`. True once she is standing again, with `out` that
    state; false if she never left the ground or never came back down.
*/
static bool DoHop(Stage s, std::vector<RouteKeys>& tl, const RouteLeg& leg, int wait, int air, bool stomp,
                  Stage& out, int max_ticks = 240){
    size_t start = tl.size();
    for (int i = 0; i < wait; i++){
        tl.push_back(Held(leg.walk));
    }
    PlayFrom(s,tl,start);
    //The press needs the key UP on the tick before it.
    if (!tl.empty() && tl.back().jump){
        return false;
    }
    bool f_left = false;
    bool f_falling = false;
    //f_climb: 0 not hanging yet, 1 caught - let go this tick, 2 press now, 3 pressed.
    int climb = 0;
    for (int i = 0; i < max_ticks; i++){
        RouteKeys k = (i < air) ? Held(leg.dir) : RouteKeys();
        k.jump = (i < leg.hold);
        k.aim_down = stomp && f_falling;
        if (leg.f_climb){
            if (climb == 0 && s.mode == MODE_HANG){
                climb = 1;
            }
            if (climb == 1){
                k.jump = false;
                climb = 2;
            }else if (climb == 2){
                k.jump = true;
                climb = 3;
            }
        }
        tl.push_back(k);
        StageEvents e;
        s.Tick(RouteInput(tl,tl.size() - 1),e);
        if (!s.f_on_ground){ f_left = true; }
        if (s.vel.y < 0.0f){ f_falling = true; }
        if (f_left && s.f_on_ground){
            out = s;
            return true;
        }
    }
    return false;
}

namespace {

struct Candidate{
    int   wait = 0;
    int   air = 0;
    bool  f_stomp = false;
    int   window = 0;
    float score = 0.0f;
};

//Every way this leg can be made from `s`, best first.
std::vector<Candidate> Candidates(const Stage& s, const RouteLeg& leg, int& budget){
    std::vector<Candidate> out;
    const int wmin = std::max(leg.wait_min,1);
    const int step = std::max(leg.air_step,1);
    for (int stomp = 0; stomp < (leg.f_try_stomp ? 2 : 1); stomp++){
        for (int air = leg.air_min; air <= leg.air_max; air += step){
            //Which waits reach the goal, and what each scores.
            std::vector<char> ok;
            std::vector<float> score;
            for (int w = wmin; w <= leg.wait_max; w++){
                std::vector<RouteKeys> tl;
                Stage land;
                budget--;
                bool f = DoHop(s,tl,leg,w,air,stomp != 0,land) && leg.goal(land);
                ok.push_back(f ? 1 : 0);
                score.push_back((f && leg.score) ? leg.score(land) : 0.0f);
            }
            //The runs of consecutive successes: one candidate per run, pressed at its middle - or,
            //for a scored leg, one per success, so the best score is not averaged away.
            for (size_t i = 0; i < ok.size(); ){
                if (!ok[i]){ i++; continue; }
                size_t j = i;
                while (j < ok.size() && ok[j]){ j++; }
                int run = (int)(j - i);
                if (leg.score){
                    for (size_t k = i; k < j; k++){
                        Candidate c;
                        c.wait = wmin + (int)k; c.air = air; c.f_stomp = (stomp != 0);
                        c.window = run; c.score = score[k];
                        out.push_back(c);
                    }
                }else{
                    Candidate c;
                    c.wait = wmin + (int)(i + j - 1) / 2; c.air = air; c.f_stomp = (stomp != 0);
                    c.window = run;
                    out.push_back(c);
                }
                i = j;
            }
            if (budget <= 0){
                break;
            }
        }
    }
    std::stable_sort(out.begin(),out.end(),[&](const Candidate& a, const Candidate& b){
        if (leg.score && a.score != b.score){ return a.score > b.score; }
        return a.window > b.window;
    });
    return out;
}

/*
    Leg `index` onward from `s`, whose timeline so far is `tl`. Depth-first over each leg's best
    `look_ahead` candidates; the first full route found is the answer, and since candidates come
    best first it is the best at every leg that could be made best.
*/
bool Solve(const Stage& s, const std::vector<RouteLeg>& legs, size_t index, int look_ahead,
           std::vector<RouteKeys>& tl, std::vector<RouteLegResult>& done, size_t& deepest, int& budget){
    if (index == legs.size()){
        return true;
    }
    //The furthest leg any attempt got to: when the route fails, that is the one worth naming.
    deepest = std::max(deepest,index);
    const RouteLeg& leg = legs[index];
    std::vector<Candidate> cands = Candidates(s,leg,budget);
    for (int c = 0; c < (int)cands.size() && c < look_ahead && budget > 0; c++){
        std::vector<RouteKeys> t2 = tl;
        size_t before = t2.size();
        Stage land;
        if (!DoHop(s,t2,leg,cands[c].wait,cands[c].air,cands[c].f_stomp,land)){
            continue;
        }
        RouteLegResult r;
        r.name = leg.name;
        r.wait = cands[c].wait;
        r.air = cands[c].air;
        r.f_stomp = cands[c].f_stomp;
        r.window = cands[c].window;
        r.f_timed = leg.f_timed && !leg.score;
        r.score = cands[c].score;
        r.ticks = (int)(t2.size() - before);
        done.push_back(r);
        if (Solve(land,legs,index + 1,look_ahead,t2,done,deepest,budget)){
            tl = t2;
            return true;
        }
        done.pop_back();
    }
    return false;
}

}

RouteResult SolveRoute(const Stage& start, const std::vector<RouteLeg>& legs, int look_ahead){
    RouteResult result;
    std::vector<RouteKeys> tl;
    std::vector<RouteLegResult> done;
    size_t deepest = 0;
    //A cap on hops tried, so a route that has become impossible fails in seconds rather than
    //trying every combination of every leg's candidates.
    int budget = 60000;
    result.f_passable = Solve(start,legs,0,look_ahead,tl,done,deepest,budget);
    if (result.f_passable){
        result.legs = done;
        result.timeline = tl;
        result.narrowest_window = 1 << 30;
        for (const RouteLegResult& r : done){
            if (r.f_timed && r.window < result.narrowest_window){
                result.narrowest_window = r.window;
            }
        }
        if (result.narrowest_window == (1 << 30)){
            result.narrowest_window = 0;
        }
    }else{
        std::string failed = (deepest < legs.size()) ? legs[deepest].name : std::string("?");
        result.failed_leg = (budget <= 0) ? failed + " (search budget spent)" : failed;
    }
    return result;
}
