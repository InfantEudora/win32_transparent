#include <math.h>

#include "Streaks.h"

/*
    Like Wind.cpp, nothing in here includes an engine header - it links into streaks_test.exe with
    no window and no GPU.
*/

//The chance per tick that a dead streak TRIES to come back. Low on purpose: with a try every tick
//every streak was back within a few ticks wherever it was, and the gust bias - which only weighs
//each try - made no visible difference. At this rate a streak waits about its own lifetime in
//calm air and a third of that under a gust, so a gust front reads as a flurry.
#define STREAK_SPAWN_RATE       0.02f
//A streak is at full strength at this many times the mean wind, so calm air draws them at about
//60% and a gust at 100% - the gust shows in their brightness as well as their number.
#define STREAK_FULL_SPEED       1.6f

enum{ CH_TRY = 0, CH_X, CH_Y, CH_Z, CH_LIFE };

static float StreakHash01(uint32_t streak, uint32_t spawn, int channel){
    uint32_t h = streak * 2654435761u + spawn * 2246822519u + (uint32_t)channel * 3266489917u;
    h = (h ^ (h >> 15)) * 2246822519u;
    h = (h ^ (h >> 13)) * 3266489917u;
    h = h ^ (h >> 16);
    return (float)(h & 0xFFFFFF) / (float)0x1000000;
}

static float Smooth(float e0, float e1, float x){
    float t = fminf(fmaxf((x - e0) / (e1 - e0),0.0f),1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void StreakSwarm::Spawn(const WindField& wind, int i, int64_t tick, float x0, float y0, float x1, float y1){
    Streak& s = streaks[i];
    s.spawns++;
    uint32_t n = s.spawns;
    const WindParams& wp = wind.Params();
    float gust_norm = 0.0f;
    float x = x0 + (x1 - x0) * StreakHash01((uint32_t)i,n,CH_X);
    float y = y0 + (y1 - y0) * StreakHash01((uint32_t)i,n,CH_Y);
    if (wp.gust_strength > 0.0f){
        gust_norm = fminf(fmaxf((wind.GustFactor(x,tick) - 1.0f) / wp.gust_strength,0.0f),1.0f);
    }
    float chance = STREAK_SPAWN_RATE * ((1.0f - params.gust_bias) + params.gust_bias * gust_norm);
    if (StreakHash01((uint32_t)i,n,CH_TRY) >= chance){
        return;
    }
    if (wind.IsBuilt() && (wind.Distance(x,y) < 0.3f)){
        return;
    }
    s.f_alive = true;
    s.x = x;
    s.y = y;
    s.z = params.z_min + (params.z_max - params.z_min) * StreakHash01((uint32_t)i,n,CH_Z);
    s.age = 0;
    int span = (params.life_max > params.life_min) ? params.life_max - params.life_min : 0;
    s.life = params.life_min + (int)(span * StreakHash01((uint32_t)i,n,CH_LIFE));
    s.strength = 0.0f;
    s.trail.clear();
    s.trail.push_back(x);
    s.trail.push_back(y);
}

void StreakSwarm::Step(const WindField& wind, int64_t tick, float x0, float y0, float x1, float y1){
    if ((int)streaks.size() != params.count){
        streaks.resize(params.count > 0 ? params.count : 0);
    }
    float pw = (x1 - x0) * params.pad, ph = (y1 - y0) * params.pad;
    x0 -= pw;
    x1 += pw;
    y0 -= ph;
    y1 += ph;
    const float dt = ARCHER_DT;
    const float full = STREAK_FULL_SPEED * fmaxf(fabsf(wind.Params().speed),0.05f);
    const int keep = 2 * (params.points > 2 ? params.points : 2);
    const int every = (params.sample_ticks > 0) ? params.sample_ticks : 1;

    for (int i = 0; i < (int)streaks.size(); i++){
        Streak& s = streaks[i];
        if (!s.f_alive){
            Spawn(wind,i,tick,x0,y0,x1,y1);
            continue;
        }
        s.age++;
        //Midpoint rule: a tracer is only as good as its integrator, and in an eddy plain Euler
        //spirals outward.
        WindVec a = wind.Velocity(s.x,s.y,tick);
        WindVec b = wind.Velocity(s.x + 0.5f * dt * a.x,s.y + 0.5f * dt * a.y,tick);
        s.x += dt * b.x;
        s.y += dt * b.y;
        float target = fminf(sqrtf(b.x * b.x + b.y * b.y) / full,1.0f);
        s.strength += (target - s.strength) * 0.1f;
        if ((s.age % every) == 0){
            s.trail.push_back(s.x);
            s.trail.push_back(s.y);
            if ((int)s.trail.size() > keep){
                s.trail.erase(s.trail.begin(),s.trail.begin() + 2);
            }
        }
        bool f_gone = (s.x < x0 - 2.0f) || (s.x > x1 + 2.0f) || (s.y < y0 - 2.0f) || (s.y > y1 + 2.0f);
        bool f_buried = wind.IsBuilt() && (wind.Distance(s.x,s.y) < 0.0f);
        if ((s.age >= s.life) || f_gone || f_buried){
            s.f_alive = false;
        }
    }
}

void StreakSwarm::BuildRibbons(float eye_x, float eye_y, float eye_z, std::vector<StreakVertex>& out) const {
    out.clear();
    std::vector<float> pts;
    for (const Streak& s : streaks){
        if (!s.f_alive || (s.trail.size() < 4)){
            continue;
        }
        //Fades in over the first fifth of its life and out over the last third.
        float f = (float)s.age / (float)(s.life > 0 ? s.life : 1);
        float env = Smooth(0.0f,0.2f,f) * (1.0f - Smooth(0.7f,1.0f,f)) * s.strength * params.alpha;
        if (env < 0.003f){
            continue;
        }
        pts = s.trail;
        pts.push_back(s.x);
        pts.push_back(s.y);
        int n = (int)pts.size() / 2;
        StreakVertex prev_l = {}, prev_r = {};
        for (int k = 0; k < n; k++){
            float px = pts[2 * k], py = pts[2 * k + 1];
            int ka = (k > 0) ? k - 1 : k, kb = (k < n - 1) ? k + 1 : k;
            float tx = pts[2 * kb] - pts[2 * ka], ty = pts[2 * kb + 1] - pts[2 * ka + 1];
            float tl = sqrtf(tx * tx + ty * ty);
            if (tl < 1e-6f){
                tx = 1.0f;
                ty = 0.0f;
            }else{
                tx /= tl;
                ty /= tl;
            }
            //Across the path AND the line of sight: cross(tangent, to_eye).
            float vx = eye_x - px, vy = eye_y - py, vz = eye_z - s.z;
            float sx = ty * vz, sy = -tx * vz, sz = tx * vy - ty * vx;
            float sl = sqrtf(sx * sx + sy * sy + sz * sz);
            if (sl < 1e-6f){
                sx = -ty;
                sy = tx;
                sz = 0.0f;
            }else{
                sx /= sl;
                sy /= sl;
                sz /= sl;
            }
            float u = (float)k / (float)(n - 1);
            //Thin at both ends, fullest just ahead of the middle; brightest toward the head, gone
            //at the tail, softened at the very tip.
            float half = 0.5f * params.width * (0.25f + 0.75f * sinf(3.14159265f * fminf(u * 1.1f,1.0f)));
            float a = env * Smooth(0.0f,0.55f,u) * (1.0f - Smooth(0.92f,1.0f,u));
            StreakVertex l = { px + sx * half,py + sy * half,s.z + sz * half,u, 1.0f,a };
            StreakVertex r = { px - sx * half,py - sy * half,s.z - sz * half,u,-1.0f,a };
            if (k > 0){
                out.push_back(prev_l);
                out.push_back(prev_r);
                out.push_back(l);
                out.push_back(prev_r);
                out.push_back(r);
                out.push_back(l);
            }
            prev_l = l;
            prev_r = r;
        }
    }
}
