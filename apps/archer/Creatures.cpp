#include "Creatures.h"

#include <math.h>

const char* CreaturePathKindName(int kind){
    static const char* names[CREATURE_PATH_KIND_COUNT] = { "hand", "vine", "surface" };
    return (kind >= 0 && kind < CREATURE_PATH_KIND_COUNT) ? names[kind] : "?";
}

//--- Paths ----------------------------------------------------------------------------------------

bool CreaturePath::Build(){
    if (points.size() < 2 || ups.size() != points.size()){
        return false;
    }
    spline.points = points;
    //A loop closes onto its first point, so the last segment brings it round.
    if (f_loop){
        spline.points.push_back(points[0]);
    }
    if (!spline.Build()){
        return false;
    }
    //The curve passes through point i at parameter i.
    point_s.resize(spline.points.size());
    for (size_t i = 0; i < spline.points.size(); i++){
        point_s[i] = spline.DistanceAtParam((float)i);
    }
    return true;
}

void CreaturePath::At(float s, vec3& out_pos, vec3& out_tangent, vec3& out_up) const{
    const float len = spline.GetLength();
    if (f_loop && len > 0.0f){
        s = fmodf(s,len);
        if (s < 0.0f){ s += len; }
    }else{
        s = fminf(fmaxf(s,0.0f),len);
    }
    out_pos = spline.PositionAt(s);
    out_tangent = spline.TangentAt(s);
    //The ups either side of s, blended by distance; a loop's closing point is its first point's.
    size_t n = point_s.size();
    size_t i = 0;
    while (i + 2 < n && point_s[i + 1] < s){
        i++;
    }
    const vec3& ua = ups[i % ups.size()];
    const vec3& ub = ups[(i + 1) % ups.size()];
    float span = (n > 1) ? (point_s[i + 1] - point_s[i]) : 0.0f;
    float t = (span > 1e-6f) ? fminf(fmaxf((s - point_s[i]) / span,0.0f),1.0f) : 0.0f;
    vec3 up = ua + (ub - ua) * t;
    //Square to the tangent, so the body lies along the path with its belly on the surface.
    up = up - out_tangent * up.dot(out_tangent);
    if (up.length() < 1e-4f){
        up = spline.FrameAt(s).normal;
    }
    up.normalize();
    out_up = up;
}

bool CreaturePathFromPoints(const std::vector<vec3>& points, const vec3& up, bool f_loop, CreaturePath& out){
    out = CreaturePath();
    out.kind = CREATURE_PATH_HAND;
    out.points = points;
    vec3 u = up;
    u.normalize();
    out.ups.assign(points.size(),u);
    out.f_loop = f_loop;
    return out.Build();
}

bool CreaturePathAroundVine(const VinePath& vine, const VineParams& params, float clearance, float pitch,
                            float phase, float s0, float s1, CreaturePath& out, float step){
    out = CreaturePath();
    out.kind = CREATURE_PATH_VINE;
    out.offset = clearance;
    Spline trunk;
    if (!BuildVineSpline(vine,trunk) || pitch <= 0.0f || step <= 0.0f){
        return false;
    }
    const float len = trunk.GetLength();
    s0 = fminf(fmaxf(s0,0.0f),len);
    s1 = fminf(fmaxf(s1,0.0f),len);
    if (s1 - s0 < 2.0f * step){
        return false;
    }
    /*
        Round the trunk by its own rotation-minimising frame, so the turn is the helix's and not
        the frame's - a Frenet frame would flip the crawler to the far side at every inflection.
    */
    for (float s = s0; s <= s1 + 1e-4f; s += step){
        SplineFrame f = trunk.FrameAt(s);
        float r = VineRadiusAt(trunk,vine,params,s) + clearance;
        float a = phase + 6.28318531f * (s - s0) / pitch;
        vec3 d = f.side * cosf(a) + f.normal * sinf(a);
        d.normalize();
        out.points.push_back(f.position + d * r);
        out.ups.push_back(d);
    }
    return out.Build();
}

bool CreaturePathOverSurface(const VineField& field, const vec3& anchor, const vec3& normal, int seed,
                             float length, float gap, CreaturePath& out){
    out = CreaturePath();
    out.kind = CREATURE_PATH_SURFACE;
    out.offset = gap;
    /*
        The creeper with a crawler's numbers: the length given, no branches, the hug kept `gap` off
        the face (the walk keeps the trunk's radius off it, and a crawler is its trunk), and no
        clearance of its own - a creeper beds a hair in, and a spider walks on top.
    */
    VineSpecies sp = VineSpeciesFor(VINE_SPECIES_CREEPER);
    sp.length_min = length;
    sp.length_max = length;
    sp.branch_min = 0;
    sp.branch_max = 0;
    sp.thickness = 1.0f;
    sp.thickness_jitter = 0.0f;
    sp.clearance = 0.0f;
    /*
        And it HUGS harder and climbs less than a vine. With the creeper's own numbers the walk
        came up over the hill's lip 0.4 above the top before settling onto it - a vine's leaves
        hide that; a spider would be walking on air. Points closer, so the corner is followed.
    */
    sp.hug = 40.0f;
    sp.climb = 1.0f;
    sp.step = 0.04f;
    sp.point_spacing = 0.12f;
    VineParams vp;
    vp.tile_scale = 1.0f;
    vp.tile_radius = gap;
    VineGrowth g;
    if (!GrowVine(sp,vp,anchor,normal,seed,field,g) || g.strands.empty()){
        return false;
    }
    const std::vector<vec3>& walk = g.strands[0].path.points;
    vec3 last = normal;
    last.normalize();
    /*
        From the second point: the first is inside the rock, where a vine's trunk roots. Each one
        SNAPPED onto the surface at `gap`, along the field's normal - the walk rounds a sharp corner
        wide (0.23 off a box's lip at its widest), and a vine's leaves hide that where a spider's
        legs would be standing on air.
    */
    for (size_t i = 1; i < walk.size(); i++){
        vec3 q = walk[i];
        vec3 n = last;
        for (int k = 0; k < 4; k++){
            vec3 g = field.Normal(q);
            if (g.length() < 1e-4f){
                break;
            }
            g.normalize();
            n = g;
            q = q - n * (field.Distance(q) - gap);
        }
        out.points.push_back(q);
        out.ups.push_back(n);
        last = n;
    }
    return out.Build();
}

//--- Crawlers -------------------------------------------------------------------------------------

float CrawlerSwarm::Rand01(Crawler& c){
    uint32_t x = c.rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    c.rng = x;
    return (float)(x >> 8) / 16777216.0f;
}

void CrawlerSwarm::Seed(int path, int count, uint32_t seed){
    if (path < 0 || path >= (int)paths.size() || !paths[path].IsBuilt()){
        return;
    }
    const float len = paths[path].Length();
    for (int k = 0; k < count; k++){
        Crawler c;
        c.path = path;
        c.rng = (seed * 2654435761u) ^ ((uint32_t)(crawlers.size() + 1) * 0x9E3779B9u);
        if (c.rng == 0){ c.rng = 1; }
        //A few draws first, so neighbouring seeds part company before anything is decided.
        for (int w = 0; w < 4; w++){ Rand01(c); }
        //Spread evenly, then jittered, so a path's crawlers never start in a clump.
        c.s = len * (((float)k + 0.2f + 0.6f * Rand01(c)) / (float)count);
        c.dir = (Rand01(c) < 0.5f) ? -1.0f : 1.0f;
        c.speed = params.speed_min + (params.speed_max - params.speed_min) * Rand01(c);
        if (Rand01(c) < 0.5f){
            c.run_ticks = params.run_min + (int)(Rand01(c) * (float)(params.run_max - params.run_min));
        }else{
            c.pause_ticks = params.pause_min + (int)(Rand01(c) * (float)(params.pause_max - params.pause_min));
        }
        crawlers.push_back(c);
    }
}

void CrawlerSwarm::Step(){
    for (Crawler& c : crawlers){
        StepOne(c);
    }
}

void CrawlerSwarm::StepOne(Crawler& c){
    if (c.path < 0 || c.path >= (int)paths.size()){
        return;
    }
    const CreaturePath& p = paths[c.path];
    const float len = p.Length();
    if (len <= 0.0f){
        return;
    }
    //Every move goes through here: the legs count it, and an open path's ends turn it back.
    auto move = [&](float ds){
        const float from = c.s;
        c.s += ds;
        if (p.f_loop){
            c.travelled += fabsf(ds);
            c.s = fmodf(c.s,len);
            if (c.s < 0.0f){ c.s += len; }
            return;
        }
        //Only what it really covered: held at an end, its legs do not walk on the spot.
        c.s = fminf(fmaxf(c.s,0.0f),len);
        c.travelled += fabsf(c.s - from);
        if (from + ds < 0.0f || from + ds > len){
            //At the end: stop there, and the next run goes back the way it came.
            if (c.run_ticks > 0){
                c.dir = (c.s <= 0.0f) ? 1.0f : -1.0f;
                c.run_ticks = 0;
                c.pause_ticks = params.pause_min + (int)(Rand01(c) * (float)(params.pause_max - params.pause_min));
            }
        }
    };

    if (c.run_ticks > 0){
        move(c.dir * c.speed * ARCHER_DT);
        if (c.run_ticks > 0 && --c.run_ticks == 0){
            c.pause_ticks = params.pause_min + (int)(Rand01(c) * (float)(params.pause_max - params.pause_min));
        }
        return;
    }
    //Paused: now and then a twitch, out and straight back, so it reads as alive.
    if (c.twitch > 0){
        const int half = params.twitch_ticks / 2;
        const float per = params.twitch_size / (float)((half > 0) ? half : 1);
        move(c.twitch_dir * ((c.twitch <= half) ? per : -per));
        if (++c.twitch > 2 * half){
            c.twitch = 0;
        }
    }else if (Rand01(c) < params.twitch_chance){
        c.twitch = 1;
        c.twitch_dir = (Rand01(c) < 0.5f) ? -1.0f : 1.0f;
    }
    if (c.pause_ticks > 0){
        c.pause_ticks--;
    }
    if (c.pause_ticks == 0 && c.twitch == 0){
        c.speed = params.speed_min + (params.speed_max - params.speed_min) * Rand01(c);
        c.run_ticks = params.run_min + (int)(Rand01(c) * (float)(params.run_max - params.run_min));
        if (Rand01(c) < params.turn_chance){
            c.dir = -c.dir;
        }
    }
}

void CrawlerSwarm::Pose(int i, vec3& out_pos, vec3& out_forward, vec3& out_up) const{
    const Crawler& c = crawlers[i];
    vec3 tangent;
    paths[c.path].At(c.s,out_pos,tangent,out_up);
    out_forward = tangent * c.dir;
}

float CrawlerSwarm::LegPhase(int i) const{
    const Crawler& c = crawlers[i];
    float stride = (params.stride > 1e-4f) ? params.stride : 1e-4f;
    float t = c.travelled / stride;
    return t - floorf(t);
}
