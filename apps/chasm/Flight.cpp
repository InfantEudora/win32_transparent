#include "Economy.h"
#include "ChasmWorld.h"
#include <algorithm>
#include <cmath>
#include <queue>

/*
    FLIGHT (docs/flight_plan.md, step 1), the rules: a finished zeppelin's way over the open chasm, found once when it is
    sent, and flown by the tick - its pace, its turning and its climb all here, so a replay flies it the same. Economy.h,
    FLIGHT, says what it does; this is how. And its PASSENGERS (step 2): going aboard and stepping off, where each stands
    on its deck, and its cabin kept full. And the MAST AND GOODS (step 3): moored nose-in at a mast, its load list, and
    its cargo unloaded.
*/

//How it bobs on the air off its mooring (current tuning): the view's motion, but those on its deck stand on it, so
//their height has it too (EconomyZeppelinBob).
#define FLIGHT_BOB              0.12f   //how far it rises and sinks...
#define FLIGHT_BOB_SECONDS      7.0f    //...once in this long

namespace {

const float PI = 3.14159265f;

/*
    The places on its deck, along its nose from its middle and across, as modelled (blender_chasm_props.py: the deck
    level from amidships to where the bow rises, the cabin aft of it): ZEPPELIN_PASSENGERS two abreast inside the rail,
    then a child's or two up in the bow.
*/
const float deck_places[][2] = {{-0.45f,-0.42f},{-0.45f,0.42f},{0.25f,-0.42f},{0.25f,0.42f},{0.95f,-0.42f},{0.95f,0.42f},
                                {1.6f,-0.42f},{1.6f,0.42f},{2.2f,-0.3f},{2.2f,0.3f},{2.7f,0.0f}};
const int DECK_PLACES = (int)(sizeof(deck_places) / sizeof(deck_places[0]));

//The deck's height over the origin `along` from its middle: level, then sweeping up into the bow (zep_gondola_z).
float DeckAt(float along){
    float lift = powf(std::max(0.0f,(along - 1.6f) / 1.7f),1.5f);
    return ZEPPELIN_DECK + 0.35f * lift;
}

//Grown up: a young adult or older - one who takes a place aboard.
bool Grown(const EconomyWorker& k, uint64_t tick){
    int stage = EconomyStageAt(k.born,tick);
    return stage != LIFE_BABY && stage != LIFE_CHILD;
}

//He stands at `at`: a route of that one point, on the ground's layer - aboard, the deck's height is his deck_y.
void StandAt(EconomyWorker& k, const vec2& at){
    k.pos = at;
    k.route.assign(1,at);
    k.speed.clear();
    k.route_layer.clear();
    k.seg = 0;
    k.along = 0.0f;
    k.off = vec2();
}

//An angle brought into -pi..pi.
float Wrap(float a){
    while (a > PI){
        a -= 2.0f * PI;
    }
    while (a < -PI){
        a += 2.0f * PI;
    }
    return a;
}

vec2 Along(float heading){
    return vec2(cosf(heading),sinf(heading));
}

//The ground at `p`, on its plot's level - and over the map's edge, high: nothing flies off it.
float GroundAt(const ChasmWorld& w, const vec2& p){
    GridPick pick = w.picker->Pick(p);
    if (!pick.f_hit || pick.plot < 0){
        return 1e3f;
    }
    return w.terrain->GroundHeight(p,w.terrain->Height(pick.plot));
}

//Whether the way from `a` to `b` keeps over the open chasm, looked at every unit.
bool OverChasm(const ChasmWorld& w, const vec2& a, const vec2& b){
    int n = std::max(1,(int)ceilf((b - a).length()));
    for (int i = 0; i <= n; i++){
        if (!EconomyFlyable(w,a + (b - a) * ((float)i / (float)n))){
            return false;
        }
    }
    return true;
}

//The nearest point to `p` over the open chasm, within 40 units: where one off a dock gone, or left over land, goes first.
bool NearestFlyable(const ChasmWorld& w, const vec2& p, vec2& out){
    for (float r = 0.5f; r <= 40.0f; r += 0.5f){
        for (int k = 0; k < 36; k++){
            float a = (float)k * (2.0f * PI / 36.0f);
            vec2 q = p + Along(a) * r;
            if (EconomyFlyable(w,q)){
                out = q;
                return true;
            }
        }
    }
    return false;
}

/*
    Where a zeppelin leaving a dock goes first, from over its berth: the nearest point over the open chasm, and a little
    further out the same way, clear of the edge - and where one coming to moor makes for, to come in over the berth. Not
    along the dock's nose: a dock usually lies along the rim, its nose with it (ZoneDockFrame), and it is the edge beside
    it that is near - so it goes off sideways, high over the mast, and comes back so, turning onto the dock's lie over it.
*/
bool DockOut(const ChasmWorld& w, const ZoneDockLie& lie, vec2& out){
    vec2 p;
    if (!NearestFlyable(w,lie.centre,p)){
        return false;
    }
    vec2 way = p - lie.centre;
    float len = way.length();
    out = (len > 1e-3f) ? p + way * (2.0f / len) : p;
    if (!EconomyFlyable(w,out)){
        out = p;
    }
    return true;
}

/*
    Its WAY from `from` to `to` (both over the chasm, or `to` just inside it, alongside an edge): A* over the rift's
    raster's points that are flyable, eight ways round each, then pulled straight - each point on to the furthest it
    sees over the open chasm - so it flies straight across where it can and round the chasm's bends where it must. `to`
    last, `from` left out. False for no way: the chasm too narrow for it somewhere between.
*/
bool FindWay(const ChasmWorld& w, const vec2& from, const vec2& to, std::vector<vec2>& out){
    out.clear();
    if (OverChasm(w,from,to)){
        out.push_back(to);
        return true;
    }
    const ChasmLayout::Raster& r = w.grid->layout.rift;
    if (r.w < 2 || r.h < 2){
        return false;
    }
    float cx = (r.hi.x - r.lo.x) / (float)(r.w - 1);
    float cz = (r.hi.y - r.lo.y) / (float)(r.h - 1);
    auto point = [&](int n){
        return vec2(r.lo.x + (float)(n % r.w) * cx,r.lo.y + (float)(n / r.w) * cz);
    };
    auto flyable = [&](int i, int j){
        return i >= 0 && j >= 0 && i < r.w && j < r.h && r.d[(size_t)j * r.w + i] < -ZEPPELIN_CLEAR;
    };
    //Each end's nearest flyable point of the raster, within a few of them.
    auto nearest = [&](const vec2& p) -> int {
        int ci = (int)lroundf((p.x - r.lo.x) / cx);
        int cj = (int)lroundf((p.y - r.lo.y) / cz);
        int best = -1;
        float best_d = 1e30f;
        for (int j = cj - 6; j <= cj + 6; j++){
            for (int i = ci - 6; i <= ci + 6; i++){
                if (!flyable(i,j)){
                    continue;
                }
                float d = (point(j * r.w + i) - p).length();
                if (d < best_d){
                    best_d = d;
                    best = j * r.w + i;
                }
            }
        }
        return best;
    };
    int start = nearest(from);
    int goal = nearest(to);
    if (start < 0 || goal < 0){
        return false;
    }
    size_t n = (size_t)r.w * r.h;
    std::vector<float> cost(n,1e30f);
    std::vector<int> came(n,-1);
    std::vector<uint8_t> done(n,0);
    typedef std::pair<float,int> Open;
    std::priority_queue<Open,std::vector<Open>,std::greater<Open>> open;
    vec2 end = point(goal);
    cost[start] = 0.0f;
    open.push(Open((point(start) - end).length(),start));
    while (!open.empty()){
        int at = open.top().second;
        open.pop();
        if (done[at]){
            continue;
        }
        done[at] = 1;
        if (at == goal){
            break;
        }
        int ai = at % r.w;
        int aj = at / r.w;
        for (int dj = -1; dj <= 1; dj++){
            for (int di = -1; di <= 1; di++){
                if ((di == 0 && dj == 0) || !flyable(ai + di,aj + dj)){
                    continue;
                }
                int to_n = (aj + dj) * r.w + ai + di;
                float c = cost[at] + (point(to_n) - point(at)).length();
                if (c < cost[to_n]){
                    cost[to_n] = c;
                    came[to_n] = at;
                    open.push(Open(c + (point(to_n) - end).length(),to_n));
                }
            }
        }
    }
    if (!done[goal]){
        return false;
    }
    std::vector<vec2> points;
    for (int at = goal; at >= 0; at = came[at]){
        points.push_back(point(at));
    }
    std::reverse(points.begin(),points.end());
    points.push_back(to);
    vec2 anchor = from;
    size_t k = 0;
    while (k < points.size()){
        size_t far = k;
        while (far + 1 < points.size() && OverChasm(w,anchor,points[far + 1])){
            far++;
        }
        out.push_back(points[far]);
        anchor = points[far];
        k = far + 1;
    }
    return true;
}

/*
    Whether a zeppelin at `p`, its origin at `y` and its nose along `heading`, has nothing under it as high as itself:
    its gondola's keel amidships, its propellers off the stern quarters, the envelope's underside along the rest - as
    modelled (blender_chasm_props.py, "The zeppelin"), with a hand's breadth to spare.
*/
bool BodyClear(const ChasmWorld& w, const vec2& p, float y, float heading){
    struct Sample{
        float along;        //toward its nose from its middle
        float across;
        float bottom;       //its underside there, over its origin
    };
    static const Sample samples[] = {
        {0.0f,0.0f,0.45f},{2.8f,0.0f,0.45f},{-2.4f,0.0f,0.45f},{0.0f,1.0f,0.45f},{0.0f,-1.0f,0.45f},
        {2.4f,0.8f,0.45f},{2.4f,-0.8f,0.45f},{-2.2f,0.9f,0.45f},{-2.2f,-0.9f,0.45f},
        {-2.4f,1.95f,1.3f},{-2.4f,-1.95f,1.3f},
        {1.8f,2.1f,2.9f},{1.8f,-2.1f,2.9f},{-1.0f,2.1f,2.9f},{-1.0f,-2.1f,2.9f},
        {5.0f,0.0f,3.25f},{-5.0f,0.0f,4.0f},{6.8f,0.0f,4.6f},{-6.8f,0.0f,4.7f}};
    vec2 f = Along(heading);
    vec2 s(-f.y,f.x);
    for (const Sample& k : samples){
        if (GroundAt(w,p + f * k.along + s * k.across) > y + k.bottom - 0.3f){
            return false;
        }
    }
    return true;
}

//Toward `to` from `from` by up to `rate` a second, slowing for the last of it (ZEPPELIN_ACCEL) - one tick's worth.
float Approach(float from, float to, float rate, float dt){
    float left = to - from;
    float step = std::min(rate,0.1f + sqrtf(2.0f * ZEPPELIN_ACCEL * fabsf(left))) * dt;
    return (fabsf(left) <= step) ? to : from + ((left > 0.0f) ? step : -step);
}

/*
    One tick of a zeppelin's flight, leg by leg (Economy.h, FLIGHT_*). Along its way it steers for the point it is making
    for, slowing as it turns - to a standstill, turning about, for a point behind it - and for the stop at the end, from
    as far off as it takes to slow from its pace; over the last two units it slides straight onto its end as it turns, as
    an airship can, so it never circles a point it is too fast to turn onto.
*/
void FlyOn(EconomyZeppelin& f, float dt){
    switch (f.leg){
        case FLIGHT_RISE:
            f.speed = 0.0f;
            f.turn = 0.0f;
            f.y = Approach(f.y,f.height,ZEPPELIN_CLIMB,dt);
            if (f.y >= f.height){
                f.leg = FLIGHT_CRUISE;
            }
            return;
        case FLIGHT_CRUISE:{
            f.y = Approach(f.y,f.height,ZEPPELIN_CLIMB,dt);
            if (f.path.empty()){
                f.path.push_back(f.pos);
            }
            f.next = std::max(0,std::min((int)f.path.size() - 1,f.next));
            vec2 to = f.path[(size_t)f.next] - f.pos;
            float dist = to.length();
            while (f.next + 1 < (int)f.path.size() && dist < ZEPPELIN_REACH){
                f.next++;
                to = f.path[(size_t)f.next] - f.pos;
                dist = to.length();
            }
            bool f_last = f.next + 1 >= (int)f.path.size();
            float left = dist;
            for (size_t i = (size_t)f.next; i + 1 < f.path.size(); i++){
                left += (f.path[i + 1] - f.path[i]).length();
            }
            float diff = (dist > 1e-4f) ? Wrap(atan2f(to.y,to.x) - f.heading) : 0.0f;
            bool f_slide = f_last && dist < 2.0f;
            f.turn = f_slide ? 0.0f : std::max(-ZEPPELIN_TURN,std::min(ZEPPELIN_TURN,diff * 1.5f));
            f.heading = Wrap(f.heading + f.turn * dt);
            float want = ZEPPELIN_SPEED * (f_slide ? 1.0f : std::max(0.0f,cosf(diff)));
            want = std::min(want,sqrtf(2.0f * ZEPPELIN_ACCEL * left));
            f.speed = (f.speed < want) ? std::min(want,f.speed + ZEPPELIN_ACCEL * dt)
                                       : std::max(want,f.speed - 2.0f * ZEPPELIN_ACCEL * dt);
            float step = std::max(f.speed,0.05f) * dt;
            if (f_last && dist <= step){
                f.pos = f.path.back();
                f.speed = 0.0f;
                f.turn = 0.0f;
                //There: hovering once it is at its height too, or turning to set down or moor.
                if (f.bound != ZEPPELIN_HOVERING){
                    f.leg = FLIGHT_TURN;
                }else if (f.y == f.height){
                    f.doing = ZEPPELIN_HOVERING;
                    f.path.clear();
                    f.next = 0;
                }
                return;
            }
            f.pos = f.pos + (f_slide ? to * (1.0f / dist) : Along(f.heading)) * (f.speed * dt);
            return;
        }
        case FLIGHT_TURN:{
            f.speed = 0.0f;
            float diff = Wrap(f.end_heading - f.heading);
            float rate = std::min(ZEPPELIN_TURN,0.05f + sqrtf(0.5f * fabsf(diff)));
            if (fabsf(diff) <= rate * dt){
                f.heading = f.end_heading;
                f.turn = 0.0f;
                f.leg = FLIGHT_DESCEND;
            }else{
                f.turn = (diff > 0.0f) ? rate : -rate;
                f.heading = Wrap(f.heading + f.turn * dt);
            }
            return;
        }
        default:
            f.speed = 0.0f;
            f.turn = 0.0f;
            f.y = Approach(f.y,f.end_y,ZEPPELIN_CLIMB,dt);
            if (f.y == f.end_y){
                f.doing = f.bound;
                f.path.clear();
                f.next = 0;
            }
            return;
    }
}

}

const EconomyZeppelin* EconomyZeppelinById(const EconomyState& e, uint32_t id){
    for (const EconomyZeppelin& f : e.fleet){
        if (f.id == id){
            return &f;
        }
    }
    return NULL;
}

const EconomyZeppelin* EconomyZeppelinAtDock(const EconomyState& e, uint32_t dock, bool f_bound){
    for (const EconomyZeppelin& f : e.fleet){
        if (dock && f.dock == dock && (f.doing == ZEPPELIN_MOORED
                                       || (f_bound && f.doing == ZEPPELIN_FLYING && f.bound == ZEPPELIN_MOORED))){
            return &f;
        }
    }
    return NULL;
}

bool EconomyFlyable(const ChasmWorld& w, const vec2& p){
    return w.grid && ChasmRiftDistance(w.grid->layout,p) < -ZEPPELIN_CLEAR;
}

int EconomyZeppelinLanding(const ChasmWorld& w, const EconomyZeppelin& f){
    if (f.doing == ZEPPELIN_SET_DOWN){
        return f.plot;
    }
    //Moored at a mast (step 3), the plot beside it it was given as it was sent; on a dock, the dock's under it.
    if (f.doing == ZEPPELIN_MOORED && f.plot >= 0){
        return f.plot;
    }
    if (f.doing == ZEPPELIN_MOORED){
        GridPick p = w.picker->Pick(f.pos);
        return p.f_hit ? p.plot : -1;
    }
    return -1;
}

int EconomyZeppelinAboard(const EconomyState& e, uint32_t id, bool f_grown){
    int n = 0;
    for (const EconomyWorker& k : e.workers){
        n += (k.state == WORKER_ABOARD && k.zeppelin == id && (!f_grown || Grown(k,e.tick))) ? 1 : 0;
    }
    return n;
}

vec2 EconomyDeckPlace(const EconomyState& e, const EconomyZeppelin& f, const EconomyWorker& k, float* y){
    int rank = 0;
    for (const EconomyWorker& o : e.workers){
        rank += (o.state == WORKER_ABOARD && o.zeppelin == f.id && o.id < k.id) ? 1 : 0;
    }
    const float* place = deck_places[rank % DECK_PLACES];
    vec2 nose = Along(f.heading);
    vec2 side(-nose.y,nose.x);
    if (y){
        *y = f.y + DeckAt(place[0]) + EconomyZeppelinBob(f,e.tick);
    }
    return f.pos + nose * place[0] + side * place[1];
}

float EconomyZeppelinBob(const EconomyZeppelin& f, uint64_t tick){
    //Held still on its lines, or alongside an edge with its gangway down - and easing out of it as it comes down there,
    //and into it as it rises off.
    float amount = 1.0f;
    if (f.doing == ZEPPELIN_MOORED || f.doing == ZEPPELIN_SET_DOWN){
        amount = 0.0f;
    }else if (f.doing == ZEPPELIN_FLYING && f.leg == FLIGHT_DESCEND){
        amount = std::min(1.0f,fabsf(f.y - f.end_y) / 1.5f);
    }else if (f.doing == ZEPPELIN_FLYING && f.leg == FLIGHT_RISE){
        amount = std::max(0.0f,1.0f - fabsf(f.height - f.y) / 3.0f);
    }
    if (amount <= 0.0f){
        return 0.0f;
    }
    float seconds = (float)(tick % (uint64_t)(FLIGHT_BOB_SECONDS * CALENDAR_TICKS_PER_SECOND)) / (float)CALENDAR_TICKS_PER_SECOND;
    return amount * FLIGHT_BOB * sinf(seconds * (2.0f * PI / FLIGHT_BOB_SECONDS) + (float)f.id * 1.7f);
}

std::array<int,GOOD_COUNT> EconomyCabinShort(const EconomyZeppelin& f){
    std::array<int,GOOD_COUNT> left{};
    left[GOOD_WATER] = std::max(0,ZEPPELIN_CABIN_WATER - f.cabin[GOOD_WATER]);
    left[GOOD_PROVISIONS] = std::max(0,ZEPPELIN_CABIN_PROVISIONS - f.cabin[GOOD_PROVISIONS]);
    return left;
}

std::array<int,GOOD_COUNT> EconomyLoadShort(const EconomyZeppelin& f){
    std::array<int,GOOD_COUNT> left{};
    for (int g = 0; g < GOOD_COUNT; g++){
        left[g] = std::max(0,f.load[g] - f.cargo[g]);
    }
    return left;
}

int EconomyGoodsTotal(const std::array<int,GOOD_COUNT>& goods){
    int n = 0;
    for (int x : goods){
        n += std::max(0,x);
    }
    return n;
}

bool EconomyMastBerth(const ChasmWorld& w, const ZoneState& z, int plot, vec2& at, float& y, float& heading, int& landing,
                      const char** why){
    auto refuse = [why](const char* reason){
        if (why){
            *why = reason;
        }
        return false;
    };
    ZoneMastPlace m;
    if (!ZoneMastAt(w,plot,m)){
        return refuse("no drop beside the mast");
    }
    at = vec2(m.cup.x,m.cup.z) + m.dir * ZEPPELIN_NOSE;
    y = m.cup.y - ZEPPELIN_NOSE_Y;
    heading = atan2f(-m.dir.y,-m.dir.x);
    if (!BodyClear(w,at,y,heading)){
        return refuse("no room for a zeppelin at the mast - something is under where it would hang");
    }
    //Its landing: a plot beside the mast with nothing on it, on the mast's ground, that people can stand on.
    const Terrain& t = *w.terrain;
    int nb[8];
    int n = ZonePlotNeighbours(w,plot,nb,8);
    landing = -1;
    float best = 1e30f;
    for (int i = 0; i < n; i++){
        int o = nb[i];
        if (o < 0 || o >= (int)z.ground.size() || !t.SameGround(o,plot) || t.wet[o] || t.Mountain(o)
            || z.KindOf(o) != ZONE_KIND_NONE || z.ground[o] == ZONE_GROUND_WALL || ZoneGateShut(z,o)){
            continue;
        }
        float d = (w.grid->fine.pos[o] - vec2(m.cup.x,m.cup.z)).length();
        if (d < best || (d == best && o < landing)){
            best = d;
            landing = o;
        }
    }
    if (landing < 0){
        return refuse("nowhere beside the mast to step off onto");
    }
    return true;
}

bool EconomySetDownSpot(const ChasmWorld& w, int plot, float heading, vec2& at, float& y, float& end_heading,
                        const char** why){
    auto refuse = [why](const char* reason){
        if (why){
            *why = reason;
        }
        return false;
    };
    const Grid& g = *w.grid;
    const Terrain& t = *w.terrain;
    if (plot < 0 || plot >= (int)g.fine.pos.size()){
        return refuse("no such plot");
    }
    switch (t.level[plot]){
        case TERRAIN_FLOOR:     return refuse("on the chasm floor - a zeppelin hovers over it, or sets down by an edge");
        case TERRAIN_BARE:      return refuse("bare rock - too small a ledge to step off onto");
        default:                break;
    }
    if (t.wet[plot]){
        return refuse("in the water");
    }
    if (t.Mountain(plot)){
        return refuse("on the mountain");
    }
    vec2 c = g.fine.pos[plot];
    y = t.ground[plot] + ZEPPELIN_SET_DOWN_STEP - ZEPPELIN_DECK;
    //Ring by ring outward from the plot, so it comes as near as it can: side-on, the way it turns less to.
    for (float r = 2.0f; r <= ZEPPELIN_SET_DOWN_REACH; r += 0.5f){
        for (int k = 0; k < 36; k++){
            float a = (float)k * (2.0f * PI / 36.0f);
            vec2 p = c + Along(a) * r;
            if (ChasmRiftDistance(g.layout,p) >= 0.0f){
                continue;
            }
            float h = Wrap(a + PI * 0.5f);
            if (fabsf(Wrap(h - heading)) > PI * 0.5f){
                h = Wrap(h + PI);
            }
            if (BodyClear(w,p,y,h)){
                at = p;
                end_heading = h;
                return true;
            }
        }
    }
    return refuse("not at an edge - a zeppelin sets down beside the chasm's edge");
}

const char* EconomyEdgeName(const ChasmWorld& w, int plot){
    if (plot < 0 || plot >= (int)w.terrain->level.size()){
        return "the edge";
    }
    switch (w.terrain->level[plot]){
        case TERRAIN_PLATEAU:   return "the rim";
        case TERRAIN_BALCONY:   return "a balcony";
        case TERRAIN_ISLAND:    return "an island";
        default:                return "the edge";
    }
}

bool Economy::DockBerthFree(const ZoneState& z, uint32_t dock, const EconomyZeppelin& f, std::string* why) const{
    auto refuse = [why](const char* reason){
        if (why){
            *why = reason;
        }
        return false;
    };
    if (dock == 0 || dock >= z.buildings.size() || z.buildings[dock].kind != ZONE_KIND_DOCK || z.buildings[dock].size <= 0){
        return refuse("not a dock");
    }
    if (dock >= site_cells.size() || site_cells[dock] > 0){
        return refuse("the dock is still being built");
    }
    if (dock >= dock_lie.size() || !dock_lie[dock].f_fits){
        return refuse("the dock is too small for a zeppelin");
    }
    if (EconomyZeppelinStage(state,dock) < ZEPPELIN_STAGES){
        return refuse("a zeppelin is being built on that dock");
    }
    for (const EconomyZeppelin& o : state.fleet){
        if (o.id != f.id && o.dock == dock && (o.doing == ZEPPELIN_MOORED
                                               || (o.doing == ZEPPELIN_FLYING && o.bound == ZEPPELIN_MOORED))){
            return refuse("another zeppelin is moored there");
        }
    }
    vec2 out;
    if (!DockOut(*state.world,dock_lie[dock],out)){
        return refuse("the dock is nowhere near the open chasm");
    }
    return true;
}

bool Economy::MastBerthFree(const ZoneState& z, uint32_t mast, const EconomyZeppelin& f, std::string* why) const{
    auto refuse = [why](const char* reason){
        if (why){
            *why = reason;
        }
        return false;
    };
    if (mast == 0 || mast >= z.buildings.size() || z.buildings[mast].kind != ZONE_KIND_MAST || z.buildings[mast].size <= 0
        || mast >= plots_of.size() || plots_of[mast].empty()){
        return refuse("not a mast");
    }
    if (mast >= site_storeys.size() || site_storeys[mast] > 0 || mast >= stands.size() || !stands[mast]){
        return refuse("the mast is still being built");
    }
    vec2 at;
    float y, heading;
    int landing;
    const char* reason = "no room at the mast";
    if (!EconomyMastBerth(*state.world,z,plots_of[mast].front(),at,y,heading,landing,&reason)){
        return refuse(reason);
    }
    for (const EconomyZeppelin& o : state.fleet){
        if (o.id != f.id && o.dock == mast && (o.doing == ZEPPELIN_MOORED
                                               || (o.doing == ZEPPELIN_FLYING && o.bound == ZEPPELIN_MOORED))){
            return refuse("another zeppelin is moored there");
        }
    }
    return true;
}

//The dock's last stage stood (Build): its zeppelin, a craft of its own now, moored on its berth as it was built.
void Economy::ZeppelinFinished(uint32_t dock){
    if (dock >= dock_lie.size()){
        return;
    }
    const ZoneDockLie& lie = dock_lie[dock];
    EconomyZeppelin f;
    f.id = state.next_zeppelin++;
    f.built_at = dock;
    f.doing = ZEPPELIN_MOORED;
    f.dock = dock;
    f.pos = lie.centre;
    f.y = lie.ground;
    f.heading = atan2f(lie.nose.y,lie.nose.x);
    state.fleet.push_back(f);
    state.version++;
}

/*
    SENT ABOARD (step 2): an order to its landing - spread round it with a group - and the zeppelin named, to go aboard
    when he stands there with it (BoardIfThere). Its places are for grown-ups: those aboard and on their way.
*/
bool Economy::Board(uint32_t person, uint32_t zeppelin, bool f_spread, const ZoneState& z, Walkers& walkers, std::string* why){
    auto refuse = [why](const char* reason){
        if (why){
            *why = reason;
        }
        return false;
    };
    if (!state.world || z.world != state.world){
        return refuse("no colony");
    }
    const EconomyZeppelin* f = EconomyZeppelinById(state,zeppelin);
    if (!f){
        return refuse("no such zeppelin");
    }
    EconomyWorker* k = NULL;
    for (EconomyWorker& o : state.workers){
        k = (o.id == person) ? &o : k;
    }
    if (!k){
        return refuse("nobody by that id");
    }
    if (k->state == WORKER_ABOARD){
        return refuse((k->zeppelin == zeppelin) ? "already aboard" : "aboard another zeppelin");
    }
    int landing = EconomyZeppelinLanding(*state.world,*f);
    if (landing < 0){
        return refuse("the zeppelin is in the air - moor it or set it down first");
    }
    int taken = 0;
    for (const EconomyWorker& o : state.workers){
        taken += (o.id != person && o.zeppelin == zeppelin && Grown(o,now)) ? 1 : 0;
    }
    if (taken >= ZEPPELIN_PASSENGERS){
        return refuse("the zeppelin is full");
    }
    if (!Order(person,landing,z,walkers,why,-1,f_spread,NULL)){
        return false;
    }
    k->zeppelin = zeppelin;     //after the order, which lets go of any zeppelin he was sent aboard before
    state.version++;
    return true;
}

bool Economy::BoardIfThere(EconomyWorker& k, const ZoneState& z){
    (void)z;
    EconomyZeppelin* f = NULL;
    for (EconomyZeppelin& x : state.fleet){
        f = (x.id == k.zeppelin) ? &x : f;
    }
    if (!f){
        k.zeppelin = 0;
        return false;
    }
    //In the air, or down somewhere else: he waits on his post for it.
    const ChasmWorld& w = *state.world;
    int landing = EconomyZeppelinLanding(w,*f);
    if (landing < 0 || k.post < 0 || (w.grid->fine.pos[k.post] - w.grid->fine.pos[landing]).length() > ZEPPELIN_BOARD_REACH){
        return false;
    }
    //Full meanwhile - those sent aboard after him came first: he stays on his post.
    if (Grown(k,now) && EconomyZeppelinAboard(state,f->id,true) >= ZEPPELIN_PASSENGERS){
        k.zeppelin = 0;
        state.version++;
        return false;
    }
    k.state = WORKER_ABOARD;
    EconomyStartTimer(k,0);
    StandAt(k,EconomyDeckPlace(state,*f,k,&k.deck_y));
    state.version++;
    return true;
}

bool Economy::StepOff(EconomyWorker& k){
    EconomyZeppelin* f = NULL;
    for (EconomyZeppelin& x : state.fleet){
        f = (x.id == k.zeppelin) ? &x : f;
    }
    int landing = f ? EconomyZeppelinLanding(*state.world,*f) : -1;
    if (f && landing < 0){
        return false;
    }
    //Onto its landing, his pack topped up from its cabin - with no zeppelin (none is ever lost yet), where he stands.
    if (landing >= 0){
        StandAt(k,state.world->grid->fine.pos[landing]);
        FillPack(k,f->cabin);
    }
    k.state = WORKER_ON_POST;
    k.zeppelin = 0;
    k.deck_y = 0.0f;
    state.version++;
    return true;
}

int Economy::CabinOf(uint32_t dock) const{
    for (size_t i = 0; dock && i < state.fleet.size(); i++){
        if (state.fleet[i].doing == ZEPPELIN_MOORED && state.fleet[i].dock == dock){
            return (int)i;
        }
    }
    return -1;
}

std::array<int,GOOD_COUNT> Economy::CabinLeft(uint32_t dock, const EconomyWorker* except) const{
    int i = CabinOf(dock);
    if (i < 0){
        return std::array<int,GOOD_COUNT>{};
    }
    //Its cabin and its load list together (step 3): what is brought goes into the cabin first, then the cargo.
    std::array<int,GOOD_COUNT> left = EconomyCabinShort(state.fleet[(size_t)i]);
    std::array<int,GOOD_COUNT> load = EconomyLoadShort(state.fleet[(size_t)i]);
    std::array<int,GOOD_COUNT> coming = EconomyComing(state,dock,except);
    for (int g = 0; g < GOOD_COUNT; g++){
        left[g] += load[g] - coming[g];
    }
    return left;
}

bool Economy::SendZeppelin(uint32_t id, const vec2& at, int plot, uint32_t dock, const ZoneState& z, std::string* why){
    auto refuse = [why](const std::string& reason){
        if (why){
            *why = reason;
        }
        return false;
    };
    if (!state.world || !state.world->grid || z.world != state.world){
        return refuse("no map");
    }
    const ChasmWorld& w = *state.world;
    EconomyZeppelin* f = NULL;
    for (EconomyZeppelin& x : state.fleet){
        f = (x.id == id) ? &x : f;
    }
    if (!f){
        return refuse("no such zeppelin");
    }
    //Where its way over the open chasm ends, and what comes after it: onto a dock, the berth over the dock's ground.
    int bound;
    vec2 end;
    std::vector<vec2> tail;
    float end_heading = f->heading;
    float end_y = f->y;
    float height = ZEPPELIN_CRUISE;
    int landing = -1;
    bool f_mast = dock && dock < z.buildings.size() && z.buildings[dock].kind == ZONE_KIND_MAST;
    if (dock){
        if (f->doing == ZEPPELIN_MOORED && f->dock == dock){
            return refuse("it is moored there already");
        }
        std::string reason;
        if (f_mast ? !MastBerthFree(z,dock,*f,&reason) : !DockBerthFree(z,dock,*f,&reason)){
            return refuse(reason);
        }
        if (f_mast){
            //A MAST (step 3): its berth hangs out over the drop, so its way ends there, turned nose-in and down onto it.
            const char* why_not = NULL;
            EconomyMastBerth(w,z,plots_of[dock].front(),end,end_y,end_heading,landing,&why_not);
        }else{
            const ZoneDockLie& lie = dock_lie[dock];
            DockOut(w,lie,end);
            tail.push_back(lie.centre);
            end_heading = atan2f(lie.nose.y,lie.nose.x);
            end_y = lie.ground;
            height = std::max(height,lie.ground + ZEPPELIN_MAST_CLEAR);
        }
        bound = ZEPPELIN_MOORED;
    }else if (plot >= 0 && plot < (int)w.terrain->level.size() && w.terrain->level[plot] != TERRAIN_FLOOR){
        const char* reason = "not at an edge";
        if (!EconomySetDownSpot(w,plot,f->heading,end,end_y,end_heading,&reason)){
            return refuse(reason);
        }
        bound = ZEPPELIN_SET_DOWN;
    }else{
        if (!EconomyFlyable(w,at)){
            return refuse("a zeppelin flies only over the open chasm");
        }
        end = at;
        bound = ZEPPELIN_HOVERING;
    }
    /*
        Where it sets out from: where it is, over the open chasm - or, on a dock or coming onto one, out from over its
        berth to the chasm first (DockOut); anywhere else over land (its dock erased from under it), the nearest point
        over the chasm.
    */
    std::vector<vec2> way;
    vec2 from = f->pos;
    uint32_t on = (f->doing == ZEPPELIN_MOORED || (f->doing == ZEPPELIN_FLYING && f->bound == ZEPPELIN_MOORED)) ? f->dock : 0;
    on = (on && on < z.buildings.size() && z.buildings[on].kind == ZONE_KIND_DOCK) ? on : 0;     //a mast's berth is over the drop
    if (on && on < dock_lie.size()){
        height = std::max(height,dock_lie[on].ground + ZEPPELIN_MAST_CLEAR);
    }
    if (f->doing == ZEPPELIN_FLYING){
        height = std::max(height,f->height);
    }
    if (!EconomyFlyable(w,from)){
        vec2 out;
        if (!(on && on < dock_lie.size() && DockOut(w,dock_lie[on],out)) && !NearestFlyable(w,from,out)){
            return refuse("it is nowhere near the open chasm");
        }
        way.push_back(out);
        from = out;
    }
    std::vector<vec2> over;
    if (!FindWay(w,from,end,over)){
        return refuse("no way there over the open chasm");
    }
    way.insert(way.end(),over.begin(),over.end());
    way.insert(way.end(),tail.begin(),tail.end());
    f->doing = ZEPPELIN_FLYING;
    f->bound = bound;
    f->dock = (bound == ZEPPELIN_MOORED) ? dock : 0;
    f->plot = (bound == ZEPPELIN_SET_DOWN) ? plot : landing;      //a mast's landing (step 3), -1 for the rest
    f->path = way;
    f->next = 0;
    f->height = height;
    f->end_heading = end_heading;
    f->end_y = end_y;
    f->leg = (f->y < height) ? FLIGHT_RISE : FLIGHT_CRUISE;
    state.version++;
    return true;
}

void Economy::TickFleet(const ZoneState& z, float dt){
    for (EconomyZeppelin& f : state.fleet){
        bool f_dock = f.dock && f.dock < z.buildings.size() && (z.buildings[f.dock].kind == ZONE_KIND_DOCK
                                                                || z.buildings[f.dock].kind == ZONE_KIND_MAST)
                      && z.buildings[f.dock].size > 0;
        //Its dock erased from under it, it hovers where it is - and sent on from there, it goes out to the chasm first.
        if (f.doing == ZEPPELIN_MOORED && !f_dock){
            f.doing = ZEPPELIN_HOVERING;
            f.dock = 0;
            f.plot = -1;
            state.version++;
        }
        //...and erased before it came, it hovers at the end of its way over the chasm instead, short of the berth.
        if (f.doing == ZEPPELIN_FLYING && f.bound == ZEPPELIN_MOORED && !f_dock){
            f.bound = ZEPPELIN_HOVERING;
            f.dock = 0;
            f.plot = -1;
            if (f.leg == FLIGHT_CRUISE && f.next + 1 < (int)f.path.size()){
                f.path.pop_back();
            }else{
                f.leg = FLIGHT_CRUISE;
                f.height = f.y;
                f.path.assign(1,f.pos);
                f.next = 0;
            }
        }
        if (f.doing != ZEPPELIN_FLYING){
            f.speed = 0.0f;
            f.turn = 0.0f;
            continue;
        }
        FlyOn(f,dt);
        state.version++;
    }
    //Its PASSENGERS where it is now (step 2), each in his place on its deck - after it moved, so they never lag it.
    for (EconomyWorker& k : state.workers){
        if (k.state != WORKER_ABOARD){
            continue;
        }
        const EconomyZeppelin* f = EconomyZeppelinById(state,k.zeppelin);
        if (!f){
            StepOff(k);         //none is lost yet: where he stands
            continue;
        }
        StandAt(k,EconomyDeckPlace(state,*f,k,&k.deck_y));
    }
}

bool Economy::SetLoad(uint32_t zeppelin, int good, int amount, std::string* why){
    auto refuse = [why](const char* reason){
        if (why){
            *why = reason;
        }
        return false;
    };
    EconomyZeppelin* f = NULL;
    for (EconomyZeppelin& x : state.fleet){
        f = (x.id == zeppelin) ? &x : f;
    }
    if (!f){
        return refuse("no such zeppelin");
    }
    if (good < 0 || good >= GOOD_COUNT){
        return refuse("no such good");
    }
    //What the rest of its list leaves of ZEPPELIN_CARGO.
    int others = EconomyGoodsTotal(f->load) - f->load[good];
    amount = std::max(0,std::min(amount,ZEPPELIN_CARGO - others));
    if (f->load[good] == amount){
        return refuse("no change");
    }
    f->load[good] = amount;
    state.version++;
    return true;
}

/*
    UNLOADED (step 3): moored, into its mast's or dock's store up to its room - the room as it stands, since nobody
    carries to it - and the rest a crate on its landing, or the nearest open plot to it (CratePlotNear); set down by an
    edge, all of it a crate there. A good at a time, in good order, so the same goes in every time.
*/
bool Economy::Unload(uint32_t zeppelin, const ZoneState& z, std::string* why){
    auto refuse = [why](const char* reason){
        if (why){
            *why = reason;
        }
        return false;
    };
    if (!state.world || z.world != state.world){
        return refuse("no colony");
    }
    const ChasmWorld& w = *state.world;
    EconomyZeppelin* f = NULL;
    for (EconomyZeppelin& x : state.fleet){
        f = (x.id == zeppelin) ? &x : f;
    }
    if (!f){
        return refuse("no such zeppelin");
    }
    if (EconomyGoodsTotal(f->cargo) == 0){
        return refuse("nothing aboard to unload");
    }
    int landing = EconomyZeppelinLanding(w,*f);
    if (landing < 0){
        return refuse("the zeppelin is in the air - moor it or set it down first");
    }
    uint32_t store = (f->doing == ZEPPELIN_MOORED && f->dock < state.room.size() && f->dock < state.stock.size()) ? f->dock : 0;
    int crate = -1;
    for (int g = 0; g < GOOD_COUNT; g++){
        int amount = f->cargo[g];
        if (amount <= 0){
            continue;
        }
        if (store){
            int put = std::min(amount,std::max(0,state.room[store] - EconomyGoodsTotal(state.stock[store])));
            state.stock[store][g] += put;
            amount -= put;
        }
        if (amount > 0){
            crate = (crate >= 0) ? crate : CratePlotNear(z,landing,w.grid->fine.pos[landing]);
            if (crate >= 0){
                AddToCrate(crate,w.grid->fine.pos[crate],g,amount,CRATE_CARGO,0);
            }
        }
        f->cargo[g] = 0;
    }
    state.version++;
    return true;
}
