#include "ApplicationChasm.h"
#include "MeshBuild.h"
#include "Debug.h"
#include <math.h>
#include <algorithm>

static Debugger* debug = new Debugger("ChasmRange",DEBUG_ALL);

/*
    THE ARCHERY RANGES, DRAWN (docs/range_plan.md): on every built range cell a TARGET at its north side, a
    row of HAY BALES behind it as the backstop along the fence and one against the fence at its side - all inside
    the cell's fence, by ZoneRangeCellAt's north, the rule the trainees stand by too - and ARROWS: in the air as a
    trainee looses them, then in the target or,
    missed, in the ground behind it. Each kind is one instance set of chasm_props.glb's prop, or of a stand-in
    box built here until that prop is modelled: a name the GLB has is used as soon as it has it, nothing else
    changes. The ground under them is the zone mesh's (CropMesh).

    View only: the props rebuilt when the zones change, the arrows from the trainees' stint clocks (ShotClockOf).
*/

const char* range_asset_names[RANGE_PROPS] = {"range_target","hay_bale","arrow","arrow_full"};

#define RANGE_BALE_SPREAD   0.28f   //the backstop's bales, this much of the cell's width apart along its row
#define RANGE_BALE_IN       0.34f   //a bale's middle, in from the fence it stands against: half its depth and a gap
#define RANGE_BALE_ROOM     0.30f   //...and at least this far in from any of the fence: about half its length
#define RANGE_TARGET_GAP    0.50f   //the target, at least this far in front of the backstop's row
#define RANGE_TARGET_ROOM   0.36f   //...and this far in from the fence: half its width and a little
#define RANGE_BOSS_HEIGHT   0.74f   //the middle of a target's face, above its base (`range_target`)
#define RANGE_BOSS_FRONT    0.14f   //...and its face, this far south of the target's origin (between its legs, behind)
#define RANGE_FACE_HALF_W   0.30f   //an arrow this near its middle across, and
#define RANGE_FACE_HALF_H   0.28f   //...up and down, sticks in the straw; any further out MISSES
/*
    WHERE A SHOT GOES (ShotOf): off the target's middle by a normal scatter each way, one standard deviation
    RANGE_SPREAD_NOVICE with no archery and RANGE_SPREAD_MASTER at full, straight between. Worked out against the
    face: a beginner misses about two shots in five, the average trainee at his cap (50%) about one in eleven, a
    master next to none.
*/
#define RANGE_SPREAD_NOVICE 0.24f
#define RANGE_SPREAD_MASTER 0.05f
#define RANGE_ARROW_SPEED   8.0f    //world units a second in the air: an arrow's own would cross a range in a frame
#define RANGE_MISS_ON       1.3f    //a miss comes down at most this far behind the target, of the way to the backstop
#define RANGE_SHOT_SECONDS  4.0f    //a shot, when the props file has no Shoot clip to time it

namespace {

//A box from (x0,y0,z0) to (x1,y1,z1), each face its own colour or `column`.
void Box(std::vector<vertex>& out, vec3 a, vec3 b, int column, int front = -1){
    vec3 c[8];
    for (int i = 0; i < 8; i++){
        c[i] = vec3((i & 1) ? b.x : a.x,(i & 4) ? b.y : a.y,(i & 2) ? b.z : a.z);
    }
    MeshQuad(out,c[4],c[5],c[7],c[6],vec3(0,1,0),column);
    MeshQuad(out,c[0],c[1],c[3],c[2],vec3(0,-1,0),column);
    MeshQuad(out,c[0],c[1],c[5],c[4],vec3(0,0,-1),column);
    MeshQuad(out,c[2],c[3],c[7],c[6],vec3(0,0,1),front >= 0 ? front : column);
    MeshQuad(out,c[0],c[2],c[6],c[4],vec3(-1,0,0),column);
    MeshQuad(out,c[1],c[3],c[7],c[5],vec3(1,0,0),column);
}

/*
    The stand-ins, each with its origin at the centre of its base and its front to +z (south: toward the
    shooters and the camera). A target: a straw butt on two legs, a red square for its bull; a bale: a
    block of straw; an arrow: a shaft out of the target toward its archer, a pale fletching at the end.
*/
void StandInMesh(int kind, std::vector<vertex>& out){
    switch (kind){
        case 0:
            Box(out,vec3(-0.25f,0.0f,-0.03f),vec3(-0.19f,0.62f,0.03f),PAL_TIMBER);
            Box(out,vec3(0.19f,0.0f,-0.03f),vec3(0.25f,0.62f,0.03f),PAL_TIMBER);
            Box(out,vec3(-0.32f,0.44f,RANGE_BOSS_FRONT - 0.12f),vec3(0.32f,1.04f,RANGE_BOSS_FRONT),PAL_FIELD);
            Box(out,vec3(-0.12f,0.63f,RANGE_BOSS_FRONT),vec3(0.12f,0.85f,RANGE_BOSS_FRONT + 0.012f),PAL_ACCENT);
            break;
        case 1:
            Box(out,vec3(-0.28f,0.0f,-0.18f),vec3(0.28f,0.34f,0.18f),PAL_FIELD);
            Box(out,vec3(-0.12f,0.0f,-0.185f),vec3(-0.08f,0.345f,0.185f),PAL_TIMBER);
            Box(out,vec3(0.08f,0.0f,-0.185f),vec3(0.12f,0.345f,0.185f),PAL_TIMBER);
            break;
        case 2:
            //Centred on its origin, as the modelled one is: its point at -z, in the target.
            Box(out,vec3(-0.012f,-0.012f,-RANGE_ARROW_HALF),vec3(0.012f,0.012f,RANGE_ARROW_HALF),PAL_BARK_DARK);
            Box(out,vec3(-0.03f,-0.004f,RANGE_ARROW_HALF - 0.07f),vec3(0.03f,0.004f,RANGE_ARROW_HALF),PAL_WALL);
            break;
        default:
            //A whole arrow, in a hand or in the air: its nock at the origin, its point along +z - the shaft, the
            //fletching crossed at the nock end, the head dark.
            Box(out,vec3(-0.010f,-0.010f,0.0f),vec3(0.010f,0.010f,RANGE_ARROW_FULL),PAL_TIMBER);
            Box(out,vec3(-0.028f,-0.004f,0.01f),vec3(0.028f,0.004f,0.09f),PAL_WALL);
            Box(out,vec3(-0.004f,-0.028f,0.01f),vec3(0.004f,0.028f,0.09f),PAL_WALL);
            Box(out,vec3(-0.016f,-0.016f,RANGE_ARROW_FULL - 0.04f),vec3(0.016f,0.016f,RANGE_ARROW_FULL),PAL_BARK_DARK);
            break;
    }
}

//One shot: where it leaves the bow, where it crosses the target's face, where it ends, how long it flies.
struct RangeShot{
    vec3 from;
    vec3 via;               //in the face, for a hit: where it sticks
    vec3 to;                //a miss: in the ground behind
    float seconds = 0.0f;
    bool f_hit = false;
};

/*
    Where a shot from `from` at the target standing at `base`, turned `yaw`, goes - its scatter (RANGE_SPREAD_*) drawn
    from `hash`, so it is the same shot every frame and every replay. In the face it sticks; outside it a MISS flies
    on, level, and comes down in the ground behind the target, short of the backstop `back` behind it - the steeper
    the higher it passed. A shot so low it would pass under the ground at the face comes down short of the target.
*/
RangeShot ShotOf(const vec3& from, const vec3& base, float yaw, float back, float skill, uint32_t hash){
    float spread = RANGE_SPREAD_NOVICE + (RANGE_SPREAD_MASTER - RANGE_SPREAD_NOVICE) * skill;
    float u1 = ((float)(hash & 0xFFFFu) + 0.5f) / 65536.0f;
    float u2 = (float)(hash >> 16) / 65536.0f;
    float r = spread * std::sqrt(-2.0f * std::log(u1));
    float dx = r * std::cos(6.2831853f * u2);
    float dy = r * std::sin(6.2831853f * u2);
    float cs = std::cos(yaw), sn = std::sin(yaw);
    RangeShot s;
    s.from = from;
    s.via = vec3(base.x + dx * cs + RANGE_BOSS_FRONT * sn,base.y + RANGE_BOSS_HEIGHT + dy,
                 base.z - dx * sn + RANGE_BOSS_FRONT * cs);
    s.f_hit = fabsf(dx) <= RANGE_FACE_HALF_W && fabsf(dy) <= RANGE_FACE_HALF_H;
    s.to = s.via;
    if (s.via.y < base.y && from.y > base.y){
        s.via = from + (s.via - from) * ((from.y - base.y) / (from.y - s.via.y));
        s.to = s.via;
    }else if (!s.f_hit){
        vec3 on = s.via - from;
        on.y = 0.0f;
        float len = on.length();
        if (len > 1e-4f){
            //Down at about 35 degrees, or steeper where the backstop comes first.
            float drop = std::max(0.0f,s.via.y - base.y);
            float ahead = std::min(drop * 1.43f,back * RANGE_MISS_ON);
            s.to = s.via + on * (ahead / len);
            s.to.y = base.y;
        }
    }
    s.seconds = ((s.via - s.from).length() + (s.to - s.via).length()) / RANGE_ARROW_SPEED;
    return s;
}

/*
    Where a shot's point is `t` seconds after it left the bow, and the way it flies: from the bow to the face rising
    a little on the way (RANGE_ARROW_ARC), then a miss straight on to the ground.
*/
void FlightAt(const RangeShot& s, float t, vec3& point, vec3& dir){
    float d1 = (s.via - s.from).length();
    float d2 = (s.to - s.via).length();
    float at = t * RANGE_ARROW_SPEED;
    if (at < d1 && d1 > 1e-4f){
        float f = at / d1;
        float lift = RANGE_ARROW_ARC * d1;
        point = s.from + (s.via - s.from) * f + vec3(0.0f,lift * 4.0f * f * (1.0f - f),0.0f);
        dir = (s.via - s.from) * (1.0f / d1) + vec3(0.0f,RANGE_ARROW_ARC * 4.0f * (1.0f - 2.0f * f),0.0f);
    }else{
        float f = d2 > 1e-4f ? std::min(1.0f,(at - d1) / d2) : 1.0f;
        point = s.via + (s.to - s.via) * f;
        dir = d2 > 1e-4f ? s.to - s.via : s.via - s.from + vec3(0.0f,-4.0f * RANGE_ARROW_ARC * d1,0.0f);
    }
    dir.normalize();
}

/*
    A RANGE CELL'S FENCE (the user's, 2026-10-18): its props stand inside it and along it. They were put at fixed
    reaches north and east of the cell's middle, and on a cell turned to the grid's slant those reached past the
    fence. `fence` is the coarse cell's outline as segments, as GridPicker::CoarseOutline gives it - the line its
    fence is drawn on (BoundaryMesh).
*/
//Where a ray from `from` along `dir` first meets the fence, and which way that piece of it runs. False: it never does.
bool FenceAhead(const std::vector<vec2>& fence, const vec2& from, const vec2& dir, vec2& hit, vec2& along){
    float best = 1e30f;
    for (size_t i = 0; i + 1 < fence.size(); i += 2){
        vec2 a = fence[i];
        vec2 e = fence[i + 1] - a;
        float den = dir.x * e.y - dir.y * e.x;
        if (std::fabs(den) < 1e-9f){
            continue;
        }
        vec2 r = a - from;
        float t = (r.x * e.y - r.y * e.x) / den;      //along the ray
        float u = (r.x * dir.y - r.y * dir.x) / den;  //along the piece
        if (t > 0.0f && u >= 0.0f && u <= 1.0f && t < best){
            best = t;
            hit = from + dir * t;
            along = e * (1.0f / std::max(1e-6f,e.length()));
        }
    }
    return best < 1e30f;
}
//How far p is inside the fence: from the nearest piece of it, negative outside.
float InsideFence(const std::vector<vec2>& fence, const vec2& p){
    float d = 1e30f;
    bool f_in = false;
    for (size_t i = 0; i + 1 < fence.size(); i += 2){
        vec2 a = fence[i];
        vec2 b = fence[i + 1];
        vec2 e = b - a;
        float len2 = e.dot(e);
        float t = (len2 > 1e-12f) ? std::max(0.0f,std::min(1.0f,(p - a).dot(e) / len2)) : 0.0f;
        d = std::min(d,(p - (a + e * t)).length());
        if ((a.y > p.y) != (b.y > p.y) && p.x < a.x + (p.y - a.y) * e.x / e.y){
            f_in = !f_in;
        }
    }
    return f_in ? d : -d;
}
//`at` moved toward `toward` until it is `room` inside the fence, a quarter of the way at a time. False: it never is.
bool KeepInside(const std::vector<vec2>& fence, vec2& at, const vec2& toward, float room){
    for (int i = 0; i < 8 && InsideFence(fence,at) < room; i++){
        at = at + (toward - at) * 0.25f;
    }
    return InsideFence(fence,at) >= room;
}
//A prop against the piece of fence `hit` is on, which runs `along`: its middle `in` from it, toward `middle`.
vec2 AgainstFence(const vec2& hit, const vec2& along, const vec2& middle, float in){
    vec2 n(-along.y,along.x);
    if (n.dot(middle - hit) < 0.0f){
        n = n * -1.0f;
    }
    return hit + n * in;
}
//The turn about up that lays a prop's +x along `along` (x, z).
float YawAlong(const vec2& along){
    return atan2f(-along.y,along.x);
}

}

bool ApplicationChasm::ShotClockOf(const EconomyWorker& k, uint64_t tick, ShotClock& out) const{
    if (k.state != WORKER_TRAINING){
        return false;
    }
    const int stint = std::max(1,(int)std::lround(ECONOMY_TRAIN_SECONDS * CALENDAR_TICKS_PER_SECOND));     //Economy's SecondsToTicks
    int elapsed = std::max(0,std::min(stint,stint - k.timer));
    float clip = (f_worker_shoot_clip && worker_shoot_seconds > 0.0f) ? worker_shoot_seconds : RANGE_SHOT_SECONDS;
    out.shots = std::max(1,(int)std::lround(ECONOMY_TRAIN_SECONDS / clip));
    out.cycle = ECONOMY_TRAIN_SECONDS / (float)out.shots;
    out.elapsed = (float)elapsed / (float)CALENDAR_TICKS_PER_SECOND;
    out.start = tick >= (uint64_t)elapsed ? tick - (uint64_t)elapsed : 0;
    return true;
}

//RENDER THREAD, from LoadProps: each prop's mesh - the GLB's where it has one, else the stand-in.
void ApplicationChasm::BuildRangeScene(){
    for (int k = 0; k < RANGE_PROPS; k++){
        Mesh* mesh = assetmanager->GetMeshFromAsset(range_asset_names[k]);
        if (!mesh){
            std::vector<vertex> verts;
            StandInMesh(k,verts);
            mesh = new Mesh();
            mesh->SetMeshData(verts.data(),(int)verts.size());
            debug->Info("'%s' not in chasm_props.glb - a stand-in is drawn (range_plan.md)\n",range_asset_names[k]);
        }
        mesh->Retain();     //held for good: each set is given it, and SetMesh releases what it replaces
        UseGroundShader(mesh);
        range_mesh[k] = mesh;
    }
}

//RENDER THREAD, from PreRender: every built range cell's props, when the zones change.
void ApplicationChasm::UploadRange(){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    std::shared_ptr<const ZoneState> z = GetZones();
    if (!w || !range_mesh[0]){
        return;
    }
    if (!range_set[0]){
        main_scene->AtTickBoundary([&](){
            for (int k = 0; k < RANGE_PROPS; k++){
                Object* o = new Object();
                o->name = std::string("Range ") + range_asset_names[k];
                o->SetMesh(range_mesh[k]);
                o->SetMaterialSlot(0,palette_material);
                o->SetVisualOnly(true);
                o->SetPickability(false);
                o->SetInstances(std::vector<fmat4>());
                o->SetVisibility(false);
                main_scene->AddObject(o);
                range_set[k] = o;
            }
        });
        if (!range_set[0]){
            return;
        }
    }
    bool f_ours = z && z->world == w;
    uint32_t version = f_ours ? z->version : 0xFFFFFFFEu;
    const vec3 up(0.0f,1.0f,0.0f);
    if (w != range_built_world || version != range_built_zones){
        BuildRangeProps(*w,f_ours ? z.get() : NULL);
        range_built_world = w;
        range_built_zones = version;
        range_arrows_key = ~0ull;
    }
    /*
        THE ARROWS (range_plan.md "Shooting"): each trainee's shots this stint, by his stint's clock - shot n leaves
        the bow at SHOOT_RELEASE of its turn of the clip, flies, and then stays where it went: in his target's face,
        or missed into the ground behind it. Where each goes is a hash of him, his stint and its number (ShotOf), so
        an arrow stays where it hit. The stuck ones are rebuilt only when what they show changes; the ones in the
        air, and those in the archers' hands (UploadWorkers), every frame there are any.
    */
    std::shared_ptr<const EconomyState> e = GetEconomy();
    std::vector<fmat4> stuck;
    std::vector<fmat4> flying = worker_hand_arrows;
    uint64_t key = 1469598103934665603ull;
    auto mix = [&](uint64_t v){ key = (key ^ v) * 1099511628211ull; };
    if (e && e->world == w && !range_target_cell.empty()){
        for (const EconomyWorker& k : e->workers){
            ShotClock clock;
            if (!ShotClockOf(k,e->tick,clock)){
                continue;
            }
            GridPick at = w->picker->Pick(k.pos);
            auto it = at.f_hit ? std::find(range_target_cell.begin(),range_target_cell.end(),at.coarse_quad)
                               : range_target_cell.end();
            if (it == range_target_cell.end()){
                continue;
            }
            int t = (int)(it - range_target_cell.begin());
            //His bow at the release: the clip's, measured on his figure (BuildWorkerScene), where he stands and faces.
            vec2 facing = EconomyWorkerFacing(k);
            vec2 body = EconomyBody(k);     //SPACING: where he stands, off his place on the line
            vec3 feet(body.x,WorkerHeight(*w,z.get(),k),body.y);
            vec3 from = feet + quat(up,atan2f(facing.x,facing.y)) * worker_release_at[PersonFigure(k,e->tick)];
            float skill = std::max(0.0f,std::min(1.0f,(float)k.archery / (float)ARCHERY_FULL));
            uint32_t seed = MeshHash(k.id,(uint32_t)clock.start,(uint32_t)(clock.start >> 32));
            int landed = 0;
            for (int n = 0; n < clock.shots; n++){
                float flown = clock.elapsed - ((float)n + SHOOT_RELEASE) * clock.cycle;
                if (flown < 0.0f){
                    break;
                }
                uint32_t h = MeshHash(seed,(uint32_t)n,0x73686f74u);
                RangeShot s = ShotOf(from,range_target_pos[t],range_target_yaw[t],range_target_back[t],skill,h);
                vec3 point, dir;
                FlightAt(s,std::min(flown,s.seconds),point,dir);
                if (fabsf(dir.y) > 0.999f){
                    continue;
                }
                if (flown < s.seconds){
                    //Shooting at a zombie (zombies_plan.md, step 3), his stint waits: not one hung in the air meanwhile.
                    if (k.draw > 0){
                        continue;
                    }
                    flying.push_back(Object::ComposeTransformScale(point - dir * RANGE_ARROW_FULL,TurnZTo(dir,up),
                                                                   vec3(1.0f,1.0f,1.0f)));
                    continue;
                }
                //Stuck: the point a little in, along the way it came, turned a touch either way as arrows sit.
                uint32_t j = MeshHash(h);
                vec3 lean = dir + vec3(((float)(j & 0xFFu) / 255.0f - 0.5f) * 0.12f,
                                       ((float)((j >> 8) & 0xFFu) / 255.0f - 0.5f) * 0.12f,0.0f);
                lean.normalize();
                stuck.push_back(Object::ComposeTransformScale(point - lean * (RANGE_ARROW_HALF * 0.7f),
                                                              TurnZTo(lean * -1.0f,up),vec3(1.0f,1.0f,1.0f)));
                landed++;
            }
            mix((uint64_t)t);
            mix(((uint64_t)k.id << 32) ^ clock.start);
            mix((uint64_t)landed);
        }
    }
    if (key != range_arrows_key){
        range_arrows_key = key;
        bool f_any = !stuck.empty();
        range_set[2]->SetInstances(std::move(stuck));
        range_set[2]->SetVisibility(f_any);
    }
    if (!flying.empty() || f_range_flying){
        f_range_flying = !flying.empty();
        range_set[3]->SetInstances(std::move(flying));
        range_set[3]->SetVisibility(f_range_flying);
    }
}

//RENDER THREAD: the targets and bales of every built range cell, inside its fence and north by ZoneRangeCellAt; the
//targets kept for the arrows.
void ApplicationChasm::BuildRangeProps(const ChasmWorld& cw, const ZoneState* z){
    const ChasmWorld* w = &cw;
    range_target_cell.clear();
    range_target_pos.clear();
    range_target_yaw.clear();
    range_target_back.clear();
    std::vector<fmat4> sets[RANGE_PROPS];
    const vec3 up(0.0f,1.0f,0.0f);
    bool f_ours = z != NULL;
    auto place = [&](int kind, const vec2& at, int level_plot, float yaw){
        float ground = w->terrain->GroundHeight(at,w->terrain->Height(level_plot));
        sets[kind].push_back(Object::ComposeTransformScale(vec3(at.x,ground,at.y),quat(up,yaw),vec3(1.0f,1.0f,1.0f)));
        return vec3(at.x,ground,at.y);
    };
    for (size_t c = 0; f_ours && c < z->field.size(); c++){
        uint32_t id = z->field[c];
        if (!id || id >= z->buildings.size() || z->buildings[id].kind != ZONE_KIND_RANGE || !z->field_built[c]){
            continue;
        }
        ZoneRangeCell r = ZoneRangeCellAt(*w,(int)c);
        if (r.middle < 0){
            continue;
        }
        vec2 east(-r.dir.y,r.dir.x);
        //A turn of a few degrees each, by the cell, so a row of ranges is not stamped out of one mould.
        uint32_t hash = MeshHash((uint32_t)c,0x72616e67u);
        float wobble = ((float)(hash % 1000u) / 1000.0f - 0.5f) * 0.12f;
        std::vector<vec2> fence;
        w->picker->CoarseOutline((int)c,fence);
        /*
            THE BACKSTOP, along the fence the range shoots at: a bale where the way north from the middle meets the
            fence, and one either side of it along that piece - each against whatever of the fence is behind it, so
            on a cell turned to the slant the row turns with it - all kept clear of the fence's other sides. A bale
            with no room is left out.
        */
        vec2 hit, along;
        if (!FenceAhead(fence,r.centre,r.dir,hit,along)){
            continue;
        }
        if (along.dot(east) < 0.0f){
            along = along * -1.0f;
        }
        vec2 back = AgainstFence(hit,along,r.centre,RANGE_BALE_IN);
        vec2 out = (hit - back) * (1.0f / RANGE_BALE_IN);      //square out through that piece
        KeepInside(fence,back,r.centre,RANGE_BALE_ROOM);
        //The target where the rule has it, unless that is too near the backstop - a cell shallow to the north.
        float reach = std::min(r.size * ZONE_RANGE_TARGET,(back - r.centre).dot(r.dir) - RANGE_TARGET_GAP);
        vec2 target = r.centre + r.dir * std::max(0.0f,reach);
        KeepInside(fence,target,r.centre,RANGE_TARGET_ROOM);
        range_target_cell.push_back((int)c);
        range_target_pos.push_back(place(0,target,r.middle,wobble));
        range_target_yaw.push_back(wobble);
        range_target_back.push_back(std::max(0.2f,(back - target).dot(r.dir)));
        for (int b = -1; b <= 1; b++){
            vec2 at = back;
            vec2 run = along;
            if (b != 0){
                vec2 h;
                if (!FenceAhead(fence,r.centre + along * (r.size * RANGE_BALE_SPREAD * (float)b),out,h,run)){
                    continue;
                }
                at = AgainstFence(h,run,r.centre,RANGE_BALE_IN);
                if (!KeepInside(fence,at,back,RANGE_BALE_ROOM)){
                    continue;
                }
            }
            place(1,at,r.middle,YawAlong(run) + wobble * (float)(b + 2));
        }
        //The loose bale, its long side against the fence east of the middle.
        vec2 side, side_run;
        if (FenceAhead(fence,r.centre,east,side,side_run)){
            vec2 at = AgainstFence(side,side_run,r.centre,RANGE_BALE_IN);
            if (KeepInside(fence,at,r.centre,RANGE_BALE_ROOM)){
                place(1,at,r.middle,YawAlong(side_run) + wobble);
            }
        }
    }
    for (int k = 0; k < 2; k++){       //the arrows are UploadRange's
        bool f_any = !sets[k].empty();
        range_set[k]->SetInstances(std::move(sets[k]));
        range_set[k]->SetVisibility(f_any);
    }
}
