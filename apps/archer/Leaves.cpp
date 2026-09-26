#include <math.h>

#include "Leaves.h"

/*
    Like Wind.cpp, nothing in here includes an engine header - it links into leaves_test.exe with
    no window and no GPU.
*/

//A fraction of new leaves come down from above the view rather than in from the side - leaves
//falling out of the canopy, which is what fills the air on a calm day.
#define LEAF_FROM_ABOVE     0.25f
//Seconds a leaf takes to shrink away at the end of its rest.
#define LEAF_FADE_SECONDS   0.6f
//Where a resting leaf feels the wind: half a unit up, not at its own height. The field's first
//cell above a surface is blended with the zero inside it, so right at the ground even a gale
//reads as a breeze - which kept half the leaves lying still in a 6.0 wind.
#define LEAF_FEEL_HEIGHT    0.5f
//A leaf touching a top only settles if the wind there is under this fraction of lift_speed. In
//the lee of a step the eddy runs back along the ground at up to 4 units/s; leaves that settled
//the moment they touched down there dropped out of the eddy they were supposed to be caught in.
#define LEAF_SETTLE_WIND    0.6f
//Leaves more than a couple of units outside the VIEW (in the padding) step every this many ticks,
//that many ticks at a time. Nobody is looking; they only have to be roughly where the air would
//have taken them when the camera zooms out onto them.
#define LEAF_FAR_STRIDE     3
#define LEAF_NEAR_BORDER    2.0f

//The channels a spawn draws from, so no two decisions share a number.
enum{ CH_X = 0, CH_Y, CH_Z, CH_SIZE, CH_TINT, CH_AXIS_X, CH_AXIS_Y, CH_AXIS_Z, CH_ANGLE, CH_PHASE,
      CH_YAW, CH_TILT, CH_ABOVE, CH_TRY };

static float LeafHash01(uint32_t leaf, uint32_t spawn, int channel){
    uint32_t h = leaf * 374761393u + spawn * 668265263u + (uint32_t)channel * 2246822519u;
    h = (h ^ (h >> 15)) * 2246822519u;
    h = (h ^ (h >> 13)) * 3266489917u;
    h = h ^ (h >> 16);
    return (float)(h & 0xFFFFFF) / (float)0x1000000;
}

void LeafSwarm::Reset(const WindField& wind, float x0, float y0, float x1, float y1){
    leaves.assign(params.count > 0 ? params.count : 0,Leaf());
    for (int i = 0; i < (int)leaves.size(); i++){
        Spawn(wind,i,true,x0,y0,x1,y1);
    }
    f_started = true;
}

/*
    `edge`: <0 in from the left, >0 in from the right, 0 scattered anywhere in the view. A few of
    the side spawns come from above instead (LEAF_FROM_ABOVE).
*/
void LeafSwarm::Spawn(const WindField& wind, int i, bool f_scatter, float x0, float y0, float x1, float y1){
    Leaf& l = leaves[i];
    int edge = f_scatter ? 0 : ((l.x < 0.5f * (x0 + x1)) ? 1 : -1);
    //Remembered BEFORE the count moves, so the side it left by picks the side it comes back on.
    l.spawns++;
    uint32_t s = l.spawns;
    auto h = [&](int ch){ return LeafHash01((uint32_t)i,s,ch); };

    l.size = params.size_min + (params.size_max - params.size_min) * h(CH_SIZE);
    l.tint = (params.tints > 0) ? (int)(h(CH_TINT) * params.tints) % params.tints : 0;
    l.z = params.z_min + (params.z_max - params.z_min) * h(CH_Z);
    //Tumbling mostly in the screen's plane reads best from the side; a little out of it.
    float ax = h(CH_AXIS_X) - 0.5f, ay = h(CH_AXIS_Y) - 0.5f, az = 0.6f + h(CH_AXIS_Z);
    float al = sqrtf(ax * ax + ay * ay + az * az);
    l.axis[0] = ax / al;
    l.axis[1] = ay / al;
    l.axis[2] = az / al;
    l.angle = 6.2831853f * h(CH_ANGLE);
    l.phase = 6.2831853f * h(CH_PHASE);
    l.yaw = 6.2831853f * h(CH_YAW);
    l.tilt = 0.25f + 0.25f * h(CH_TILT);
    l.state = LEAF_FLYING;
    l.ticks = 0;
    //Scattered leaves can land in plain view (a zoom out, the first fill), so they fade in; one
    //coming in past the edge is out of sight already.
    l.fade = f_scatter ? 0.0f : 1.0f;

    bool f_above = !f_scatter && (h(CH_ABOVE) < LEAF_FROM_ABOVE);
    float speed = wind.Params().speed;
    float x = 0.0f, y = 0.0f;
    for (int t = 0; t < 8; t++){
        float u = LeafHash01((uint32_t)i,s,CH_X + 16 * (t + 1));
        float v = LeafHash01((uint32_t)i,s,CH_Y + 16 * (t + 1));
        if (f_scatter){
            x = x0 + (x1 - x0) * u;
            y = y0 + (y1 - y0) * v;
        }else if (f_above){
            x = x0 + (x1 - x0) * u;
            y = y1 + params.margin * 0.3f * v;
        }else{
            x = (edge < 0) ? x0 - params.margin * u : x1 + params.margin * u;
            y = y0 + (y1 - y0) * v;
        }
        if (!wind.IsBuilt() || (wind.Distance(x,y) > 0.3f)){
            break;
        }
        //Every try inside a block: the last one goes to the top of the view, which is air on any
        //level worth having leaves on.
        if (t == 7){
            y = y1;
        }
    }
    l.x = x;
    l.y = y;
    l.vx = speed;
    l.vy = -params.fall_speed;
}

int LeafSwarm::CountFor(float x0, float y0, float x1, float y1) const {
    float n = params.density * fmaxf(x1 - x0,0.0f) * fmaxf(y1 - y0,0.0f);
    return (int)fminf(fmaxf(n,0.0f),(float)(params.max_count > 0 ? params.max_count : 0));
}

void LeafSwarm::Step(const WindField& wind, int64_t tick, float x0, float y0, float x1, float y1){
    //What the camera sees, kept for the far-leaf stride; the leaves live in it grown by `pad`.
    const float vx0 = x0 - LEAF_NEAR_BORDER, vx1 = x1 + LEAF_NEAR_BORDER;
    const float vy0 = y0 - LEAF_NEAR_BORDER, vy1 = y1 + LEAF_NEAR_BORDER;
    const float pw = (x1 - x0) * params.pad, ph = (y1 - y0) * params.pad;
    x0 -= pw;
    x1 += pw;
    y0 -= ph;
    y1 += ph;
    if (params.density > 0.0f){
        params.count = CountFor(x0,y0,x1,y1);
    }
    if (!f_started){
        Reset(wind,x0,y0,x1,y1);
    }else if ((int)leaves.size() != params.count){
        //Grown or shrunk - a zoom. Everyone already here stays put; the new ones are scattered
        //over the whole region and fade in.
        int old = (int)leaves.size();
        leaves.resize(params.count > 0 ? params.count : 0);
        for (int i = old; i < (int)leaves.size(); i++){
            Spawn(wind,i,true,x0,y0,x1,y1);
        }
    }
    const float t = (float)fmod((double)tick * ARCHER_DT,100000.0);
    const float w = x1 - x0;
    const float m = params.margin;
    const float r = params.radius;

    for (int i = 0; i < (int)leaves.size(); i++){
        Leaf& l = leaves[i];
        bool f_near = (l.x > vx0) && (l.x < vx1) && (l.y > vy0) && (l.y < vy1);
        int stride = f_near ? 1 : LEAF_FAR_STRIDE;
        //Staggered by index, so a third of the far leaves move on each tick rather than all at once.
        if ((stride > 1) && (((tick + i) % stride) != 0)){
            continue;
        }
        const float dt = ARCHER_DT * stride;
        const float k = fminf(dt / fmaxf(params.drag_time,dt),1.0f);
        l.ticks += stride;
        if ((l.state != LEAF_FADING) && (l.fade < 1.0f)){
            l.fade = fminf(l.fade + dt / LEAF_FADE_SECONDS,1.0f);
        }

        //Far away: the camera jumped (a restart, a teleport). Refill the view where it is now.
        if ((l.x < x0 - w) || (l.x > x1 + w) || (l.y < y0 - w) || (l.y > y1 + w)){
            Spawn(wind,i,true,x0,y0,x1,y1);
            continue;
        }
        /*
            Left the view: back in on the OTHER side. Whichever carried it out - the wind, or the
            camera running the other way - the side it left by is the side air is leaving the view
            on, so the opposite side is where fresh air, and fresh leaves, come in. Spawning upwind
            instead piles every leaf at the back edge while she runs downwind of them.
        */
        if ((l.x < x0 - m) || (l.x > x1 + m) || (l.y < y0 - m) || (l.y > y1 + 2.0f * m)){
            Spawn(wind,i,false,x0,y0,x1,y1);
            continue;
        }

        if (l.state == LEAF_FADING){
            l.fade -= dt / LEAF_FADE_SECONDS;
            if (l.fade <= 0.0f){
                //Back as a new leaf coming in from upwind.
                l.x = (wind.Params().speed >= 0.0f) ? x1 : x0;
                Spawn(wind,i,false,x0,y0,x1,y1);
            }
            continue;
        }

        if (l.state == LEAF_RESTING){
            WindVec a = wind.Velocity(l.x,l.y + LEAF_FEEL_HEIGHT,tick);
            float sp = sqrtf(a.x * a.x + a.y * a.y);
            if (sp > params.lift_speed){
                l.state = LEAF_FLYING;
                l.ticks = 0;
                l.vx = 0.5f * a.x;
                l.vy = 0.8f + 0.2f * (sp - params.lift_speed);
                l.y += 0.02f;
                continue;
            }
            if (sp > 0.4f * params.lift_speed){
                l.x += params.skitter * a.x * dt;
                //Skittered off the edge of what it lay on: it falls.
                if (wind.IsBuilt() && (wind.Distance(l.x,l.y - r - 0.05f) > 0.15f)){
                    l.state = LEAF_FLYING;
                    l.ticks = 0;
                    l.vx = params.skitter * a.x;
                    l.vy = 0.0f;
                    continue;
                }
            }
            if (l.ticks > params.rest_ticks){
                l.state = LEAF_FADING;
                l.ticks = 0;
            }
            continue;
        }

        //--- Flying ---
        WindVec a = wind.Velocity(l.x,l.y,tick);
        float fx = params.flutter * sinf(2.2f * t + l.phase);
        float fy = 0.5f * params.flutter * sinf(4.4f * t + 1.7f * l.phase);
        float tx = a.x + fx;
        float ty = a.y - params.fall_speed + fy;
        float rx = tx - l.vx, ry = ty - l.vy;
        l.vx += rx * k;
        l.vy += ry * k;
        l.x += l.vx * dt;
        l.y += l.vy * dt;
        //It tumbles harder the harder the air pushes it, and a little just for moving.
        float spin = 2.0f + 1.5f * sqrtf(rx * rx + ry * ry) + 0.8f * sqrtf(l.vx * l.vx + l.vy * l.vy);
        l.angle = fmodf(l.angle + spin * dt,6.2831853f);

        if (!wind.IsBuilt()){
            continue;
        }
        float d = wind.Distance(l.x,l.y);
        if (d < r){
            WindVec n = wind.DistanceGradient(l.x,l.y);
            float nl = sqrtf(n.x * n.x + n.y * n.y);
            if (nl < 1e-4f){
                n.x = 0.0f;
                n.y = 1.0f;
                nl = 1.0f;
            }
            n.x /= nl;
            n.y /= nl;
            l.x += n.x * (r - d);
            l.y += n.y * (r - d);
            float vn = l.vx * n.x + l.vy * n.y;
            if (vn < 0.0f){
                l.vx -= vn * n.x;
                l.vy -= vn * n.y;
            }
            //Something facing up, in air calm enough to lie in: it has landed. In a stronger wind
            //it slides on along the surface instead, with what is left of its velocity.
            WindVec a2 = wind.Velocity(l.x,l.y + LEAF_FEEL_HEIGHT,tick);
            float calm = sqrtf(a2.x * a2.x + a2.y * a2.y);
            if ((n.y > 0.6f) && (calm < LEAF_SETTLE_WIND * params.lift_speed)){
                l.state = LEAF_RESTING;
                l.ticks = 0;
                l.vx = 0.0f;
                l.vy = 0.0f;
            }
        }
    }
}
