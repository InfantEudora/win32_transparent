#include <math.h>

#include "Fireflies.h"

/*
    Like Wind.cpp, nothing in here includes an engine header - it links into fireflies_test.exe
    with no window and no GPU.
*/

//The flash, in seconds: a fast rise and a slower glow-down, flashes in a burst this far apart.
#define FLY_FLASH_RISE      0.07f
#define FLY_FLASH_DECAY     0.22f
#define FLY_FLASH_GAP       0.40f
//Seconds to fade in on arrival and out when its home leaves the view.
#define FLY_FADE_SECONDS    1.2f
//How close a fly may come to a block before it is pushed back out.
#define FLY_CLEARANCE       0.2f

enum{ CH_HOME = 0, CH_RATE_A, CH_RATE_B, CH_RATE_C, CH_PHASE_A, CH_PHASE_B, CH_PHASE_C, CH_HEIGHT,
      CH_PERIOD, CH_BURST, CH_CLOCK };

static float FlyHash01(uint32_t fly, uint32_t spawn, int channel){
    uint32_t h = fly * 2246822519u + spawn * 3266489917u + (uint32_t)channel * 668265263u + 0x9E3779B9u;
    h = (h ^ (h >> 15)) * 2246822519u;
    h = (h ^ (h >> 13)) * 3266489917u;
    h = h ^ (h >> 16);
    return (float)(h & 0xFFFFFF) / (float)0x1000000;
}

void FireflySwarm::SetHomes(const std::vector<FireflyHome>& new_homes){
    homes = new_homes;
    for (Firefly& f : flies){
        f.home = -1;
    }
}

//The brightest flash at the moment, not their sum: summed and capped, the second flash of a burst
//rose on the first one's tail into a plateau and a burst of three read as one long flash.
float FireflySwarm::Glow(float clock, int burst) const {
    float peak = 0.0f;
    for (int k = 0; k < burst; k++){
        float d = clock - k * FLY_FLASH_GAP;
        if (d < 0.0f){
            continue;
        }
        peak = fmaxf(peak,(d < FLY_FLASH_RISE) ? d / FLY_FLASH_RISE : expf(-(d - FLY_FLASH_RISE) / FLY_FLASH_DECAY));
    }
    return params.ember + (1.0f - params.ember) * peak;
}

bool FireflySwarm::InRegion(int h, float x0, float y0, float x1, float y1) const {
    const FireflyHome& o = homes[h];
    return (o.x >= x0) && (o.x <= x1) && (o.y >= y0) && (o.y <= y1);
}

bool FireflySwarm::Spawn(int i, float x0, float y0, float x1, float y1){
    Firefly& f = flies[i];
    f.spawns++;
    f.home = -1;
    f.fade = 0.0f;
    f.f_leaving = false;
    uint32_t s = f.spawns;
    auto h = [&](int ch){ return FlyHash01((uint32_t)i,s,ch); };

    //A home in the region, by weight.
    float total = 0.0f;
    for (int k = 0; k < (int)homes.size(); k++){
        if (InRegion(k,x0,y0,x1,y1)){
            total += homes[k].weight;
        }
    }
    if (total <= 0.0f){
        return false;
    }
    float pick = h(CH_HOME) * total;
    for (int k = 0; k < (int)homes.size(); k++){
        if (!InRegion(k,x0,y0,x1,y1)){
            continue;
        }
        pick -= homes[k].weight;
        if (pick <= 0.0f){
            f.home = k;
            break;
        }
    }
    if (f.home < 0){
        return false;
    }
    //Slow loops, each fly its own: a full turn every 7 to 20 seconds on each axis.
    f.loop[0] = 0.3f + 0.6f * h(CH_RATE_A);
    f.loop[1] = 0.3f + 0.6f * h(CH_RATE_B);
    f.loop[2] = 0.3f + 0.6f * h(CH_RATE_C);
    f.loop[3] = 6.2831853f * h(CH_PHASE_A);
    f.loop[4] = 6.2831853f * h(CH_PHASE_B);
    f.loop[5] = 6.2831853f * h(CH_PHASE_C);
    f.height = params.height_min + (params.height_max - params.height_min) * h(CH_HEIGHT);
    f.period = fmaxf(params.period + params.period_jitter * (2.0f * h(CH_PERIOD) - 1.0f),1.0f);
    f.burst = 1 + (int)(h(CH_BURST) * 2.999f);
    f.clock = f.period * h(CH_CLOCK);
    const FireflyHome& o = homes[f.home];
    f.x = o.x;
    f.y = o.y + f.height;
    f.z = o.z;
    f.vx = f.vy = f.vz = 0.0f;
    return true;
}

void FireflySwarm::Step(const WindField& wind, int64_t tick, float x0, float y0, float x1, float y1){
    const float dt = ARCHER_DT;
    const float t = (float)fmod((double)tick * dt,100000.0);
    float pw = (x1 - x0) * params.pad, ph = (y1 - y0) * params.pad;
    x0 -= pw;
    x1 += pw;
    y0 -= ph;
    y1 += ph;
    if ((int)flies.size() != params.count){
        flies.resize(params.count > 0 ? params.count : 0);
    }
    std::vector<int> fired;

    for (int i = 0; i < (int)flies.size(); i++){
        Firefly& f = flies[i];
        if ((f.home < 0) || (f.home >= (int)homes.size())){
            if (!Spawn(i,x0,y0,x1,y1)){
                f.brightness = 0.0f;
                continue;
            }
        }
        if (!f.f_leaving && !InRegion(f.home,x0,y0,x1,y1)){
            f.f_leaving = true;
        }
        if (f.f_leaving){
            f.fade -= dt / FLY_FADE_SECONDS;
            if (f.fade <= 0.0f){
                if (!Spawn(i,x0,y0,x1,y1)){
                    f.brightness = 0.0f;
                    continue;
                }
            }
        }else{
            f.fade = fminf(f.fade + dt / FLY_FADE_SECONDS,1.0f);
        }

        //Steer for the point on its loop, drift with a little of the wind.
        const FireflyHome& o = homes[f.home];
        float tx = o.x + params.roam_x * sinf(f.loop[0] * t + f.loop[3]);
        float ty = o.y + f.height + params.roam_y * sinf(f.loop[1] * t + f.loop[4]);
        float tz = o.z + params.roam_z * sinf(f.loop[2] * t + f.loop[5]);
        WindVec w = wind.Velocity(f.x,f.y,tick);
        float dvx = (tx - f.x) * params.steer + params.wind_follow * w.x;
        float dvy = (ty - f.y) * params.steer + params.wind_follow * w.y;
        float dvz = (tz - f.z) * params.steer;
        float k = fminf(3.0f * dt,1.0f);
        f.vx += (dvx - f.vx) * k;
        f.vy += (dvy - f.vy) * k;
        f.vz += (dvz - f.vz) * k;
        f.x += f.vx * dt;
        f.y += f.vy * dt;
        f.z += f.vz * dt;
        if (wind.IsBuilt()){
            float d = wind.Distance(f.x,f.y);
            if (d < FLY_CLEARANCE){
                WindVec n = wind.DistanceGradient(f.x,f.y);
                float nl = sqrtf(n.x * n.x + n.y * n.y);
                if (nl > 1e-4f){
                    n.x /= nl;
                    n.y /= nl;
                    f.x += n.x * (FLY_CLEARANCE - d);
                    f.y += n.y * (FLY_CLEARANCE - d);
                    float vn = f.vx * n.x + f.vy * n.y;
                    if (vn < 0.0f){
                        f.vx -= vn * n.x;
                        f.vy -= vn * n.y;
                    }
                }
            }
        }

        f.clock += dt;
        if (f.clock >= f.period){
            f.clock -= f.period;
            fired.push_back(i);
        }
        f.brightness = Glow(f.clock,f.burst) * f.fade;
    }

    /*
        Pulse coupling. A fly that has just started a burst pulls the clocks of those near it
        FORWARD - only those in the second half of their period, so a fly is never yanked back
        into a burst it has only just finished - by a share of what they have left. The ones close
        to flashing anyway fire with it; the rest get a little closer each time. That is enough for
        clusters to fall into step, and the spread in periods keeps it from ever being perfect.
    */
    if ((params.sync > 0.0f) && !fired.empty()){
        float r2 = params.sync_radius * params.sync_radius;
        for (int a : fired){
            const Firefly& fa = flies[a];
            for (int b = 0; b < (int)flies.size(); b++){
                Firefly& fb = flies[b];
                if ((b == a) || (fb.home < 0) || (fb.clock < 0.5f * fb.period)){
                    continue;
                }
                float dx = fb.x - fa.x, dy = fb.y - fa.y;
                if (dx * dx + dy * dy > r2){
                    continue;
                }
                fb.clock += params.sync * 0.3f * (fb.period - fb.clock);
            }
        }
    }
}

void FireflySwarm::BuildGlows(float eye_x, float eye_y, float eye_z, float size, std::vector<FireflyVertex>& out) const {
    out.clear();
    for (const Firefly& f : flies){
        if ((f.home < 0) || (f.brightness <= 0.001f)){
            continue;
        }
        //Facing the eye: right is across the line of sight and world up, up completes the frame.
        float vx = eye_x - f.x, vy = eye_y - f.y, vz = eye_z - f.z;
        float vl = sqrtf(vx * vx + vy * vy + vz * vz);
        if (vl < 1e-4f){
            continue;
        }
        vx /= vl;
        vy /= vl;
        vz /= vl;
        float rx = vz, ry = 0.0f, rz = -vx;         //cross((0,1,0), view)
        float rl = sqrtf(rx * rx + rz * rz);
        if (rl < 1e-4f){
            rx = 1.0f;
            rz = 0.0f;
        }else{
            rx /= rl;
            rz /= rl;
        }
        float ux = vy * rz - vz * ry, uy = vz * rx - vx * rz, uz = vx * ry - vy * rx;   //cross(view, right)
        float half = 0.5f * size * (0.33f + 0.67f * f.brightness);
        FireflyVertex c[4];
        const float su[4] = {-1.0f, 1.0f, 1.0f,-1.0f};
        const float sv[4] = {-1.0f,-1.0f, 1.0f, 1.0f};
        for (int k = 0; k < 4; k++){
            c[k].x = f.x + half * (su[k] * rx + sv[k] * ux);
            c[k].y = f.y + half * (su[k] * ry + sv[k] * uy);
            c[k].z = f.z + half * (su[k] * rz + sv[k] * uz);
            c[k].u = su[k];
            c[k].v = sv[k];
            c[k].brightness = f.brightness;
        }
        out.push_back(c[0]);
        out.push_back(c[1]);
        out.push_back(c[2]);
        out.push_back(c[0]);
        out.push_back(c[2]);
        out.push_back(c[3]);
    }
}

void FireflySwarm::LightGroup(int n, float x0, float y0, float x1, float y1, std::vector<FireflyLight>& out) const {
    out.assign(n > 0 ? n : 0,FireflyLight());
    if (n <= 0){
        return;
    }
    std::vector<float> wx(n,0.0f), wy(n,0.0f), wz(n,0.0f);
    float span = fmaxf(x1 - x0,1e-3f);
    for (const Firefly& f : flies){
        if ((f.home < 0) || (f.brightness <= 0.0f) || (f.x < x0) || (f.x > x1) || (f.y < y0) || (f.y > y1)){
            continue;
        }
        int b = (int)((f.x - x0) / span * n);
        b = (b < 0) ? 0 : ((b >= n) ? n - 1 : b);
        out[b].intensity += f.brightness;
        wx[b] += f.brightness * f.x;
        wy[b] += f.brightness * f.y;
        wz[b] += f.brightness * f.z;
    }
    for (int b = 0; b < n; b++){
        if (out[b].intensity > 0.0f){
            out[b].x = wx[b] / out[b].intensity;
            out[b].y = wy[b] / out[b].intensity;
            out[b].z = wz[b] / out[b].intensity;
        }else{
            out[b].x = x0 + span * (b + 0.5f) / n;
            out[b].y = 0.5f * (y0 + y1);
        }
    }
}
