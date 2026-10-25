#include "ApplicationChasm.h"
#include "MeshBuild.h"
#include "Palette.h"
#include <math.h>
#include <algorithm>

/*
    THE ZEPPELINS FLYING, DRAWN (docs/flight_plan.md, step 1): every finished zeppelin where the rules have it - its body,
    zeppelin_body (the finished stage with its propellers' booms bare), and a zeppelin_propeller turning on each boom - as
    objects of their own rather than instance sets, so the selected one is outlined (UploadSelection). Over the rules'
    place, a view's own motion: off its mooring it bobs on the air and leans into its turns, and its propellers turn with
    its pace, idling while it holds still in the air. On the simulation's clock, so a pause holds it still - the bob by the
    rules' tick (EconomyZeppelinBob), since those on its deck stand on it. Down - moored or set down - a GANGWAY from its
    deck to its landing (step 2), what its passengers go aboard and off by.

    And what a click on one needs: its shape, to pick it by (ChasmZeppelinAlongRay), and a right click with one selected,
    turned into where it is sent (SendZeppelinTo).
*/

#define FLEET_BODY          (ZEPPELIN_STAGES + 3)   //in dock_asset_names
#define FLEET_PROPELLER     (ZEPPELIN_STAGES + 4)
//A propeller's hub on the body, in the models' frame - zep_prop_hub (blender_chasm_props.py): out to starboard, up, and
//aft, the nose being -Z. Port's is across from it.
#define FLEET_HUB_X         1.95f
#define FLEET_HUB_Y         2.0f
#define FLEET_HUB_Z         2.4f
//The gangway (step 2): off the deck's side amidships, forward of the cabin, out to the first ground within a step of
//the deck - the edge it is set down by, or the dock's ground under it - a plank between two rope rails. Moored nose-in
//at a mast (step 3), off its bow instead, out to the lip beside the mast.
#define FLEET_GANGWAY_ALONG 0.6f    //along its nose from its middle, where it leaves the deck...
#define FLEET_GANGWAY_SIDE  0.95f   //...at the rail
#define FLEET_GANGWAY_BOW   2.6f    //...or at a mast, this far along, where the deck sweeps up into the bow...
#define FLEET_GANGWAY_BOW_SIDE 0.4f //...this far toward its landing's side, clear of the mast on its axis...
#define FLEET_GANGWAY_BOW_RISE 0.16f //...and the deck this much higher there (zep_gondola_z)
#define FLEET_GANGWAY_DROP  1.6f    //ground this far under the deck at most: a step down from it, or the dock's
#define FLEET_GANGWAY_MOST  6.0f    //...looked for this far out
#define FLEET_GANGWAY_HALF  0.22f   //the plank's half width...
#define FLEET_GANGWAY_RAIL  0.45f   //...and its rails' height over it
//Its motion off its mooring (current tuning, view only).
#define FLEET_LEAN          0.3f    //its roll, radians, for every radian a second it turns...
#define FLEET_LEAN_MOST     0.12f   //...up to this
#define FLEET_PROP_TURNS    2.0f    //propeller turns a second at its full pace...
#define FLEET_PROP_IDLE     0.5f    //...and holding still in the air
//Its body to pick it by, in the models' frame (blender_chasm_props.py, "The zeppelin"): the envelope round its axis...
#define FLEET_PICK_HALF     7.0f    //half ZEP_LENGTH
#define FLEET_PICK_RADIUS   2.2f    //ZEP_RADIUS
#define FLEET_PICK_AXIS     5.0f    //ZEP_AXIS_Z
//...and the gondola under it, back to front along its nose, up from its keel, and across.
static const float fleet_gondola_lo[3] = {-2.6f,0.45f,-1.05f};
static const float fleet_gondola_hi[3] = {3.3f,2.4f,1.05f};

static const float FLEET_PI = 3.14159265f;

uint32_t ChasmZeppelinAlongRay(const EconomyState& e, const ray& r, float* along){
    uint32_t best = 0;
    float best_t = INFINITY;
    for (const EconomyZeppelin& f : e.fleet){
        //Into its frame: along its nose, up, and across.
        vec2 n(cosf(f.heading),sinf(f.heading));
        vec3 o = r.origin - vec3(f.pos.x,f.y,f.pos.y);
        vec3 lo(o.x * n.x + o.z * n.y,o.y,o.z * n.x - o.x * n.y);
        vec3 ld(r.direction.x * n.x + r.direction.z * n.y,r.direction.y,r.direction.z * n.x - r.direction.x * n.y);
        //The envelope, scaled to a sphere of 1: the nearer of its two meetings, ahead.
        vec3 p(lo.x / FLEET_PICK_HALF,(lo.y - FLEET_PICK_AXIS) / FLEET_PICK_RADIUS,lo.z / FLEET_PICK_RADIUS);
        vec3 d(ld.x / FLEET_PICK_HALF,ld.y / FLEET_PICK_RADIUS,ld.z / FLEET_PICK_RADIUS);
        float a = d.dot(d);
        float b = 2.0f * p.dot(d);
        float c = p.dot(p) - 1.0f;
        float disc = b * b - 4.0f * a * c;
        if (a > 1e-12f && disc >= 0.0f){
            float t = (-b - sqrtf(disc)) / (2.0f * a);
            if (t > 0.0f && t < best_t){
                best_t = t;
                best = f.id;
            }
        }
        //The gondola, a box: where the ray is inside all three of its slabs at once.
        const float oo[3] = {lo.x,lo.y,lo.z};
        const float dd[3] = {ld.x,ld.y,ld.z};
        float t0 = 0.0f;
        float t1 = INFINITY;
        bool f_hit = true;
        for (int k = 0; k < 3 && f_hit; k++){
            if (fabsf(dd[k]) < 1e-9f){
                f_hit = oo[k] >= fleet_gondola_lo[k] && oo[k] <= fleet_gondola_hi[k];
                continue;
            }
            float ta = (fleet_gondola_lo[k] - oo[k]) / dd[k];
            float tb = (fleet_gondola_hi[k] - oo[k]) / dd[k];
            t0 = std::max(t0,std::min(ta,tb));
            t1 = std::min(t1,std::max(ta,tb));
            f_hit = t0 <= t1;
        }
        if (f_hit && t0 > 0.0f && t0 < best_t){
            best_t = t0;
            best = f.id;
        }
    }
    if (along){
        *along = best ? best_t * r.direction.length() : INFINITY;
    }
    return best;
}

//The ground at `p` on its plot's level, and whether there is any: off the map there is none.
static bool FleetGround(const ChasmWorld& w, const vec2& p, float& ground){
    GridPick pick = w.picker->Pick(p);
    if (!pick.f_hit || pick.plot < 0){
        return false;
    }
    ground = w.terrain->GroundHeight(p,w.terrain->Height(pick.plot));
    return true;
}

/*
    The GANGWAY's ends (step 2): off its deck's rail on its landing's side, out to the first ground within
    FLEET_GANGWAY_DROP under the deck - straight out to the side, else straight for the landing - and a little onto it,
    far enough that the plank slopes down rather than drops. False with no ground in reach.
*/
static bool GangwayEnds(const ChasmWorld& w, const EconomyZeppelin& f, int landing, vec3& a, vec3& b){
    vec2 nose(cosf(f.heading),sinf(f.heading));
    vec2 side(-nose.y,nose.x);
    vec2 to = w.grid->fine.pos[landing] - f.pos;
    vec2 out = (to.dot(side) >= 0.0f) ? side : side * -1.0f;
    float deck = f.y + ZEPPELIN_DECK;
    vec2 foot = f.pos + nose * FLEET_GANGWAY_ALONG + out * FLEET_GANGWAY_SIDE;
    bool f_bow = f.doing == ZEPPELIN_MOORED && f.plot >= 0;     //at a mast, nose-in (step 3)
    if (f_bow){
        foot = f.pos + nose * FLEET_GANGWAY_BOW + out * FLEET_GANGWAY_BOW_SIDE;
        deck += FLEET_GANGWAY_BOW_RISE;
        out = nose;
    }
    a = vec3(foot.x,deck,foot.y);
    vec2 toward = w.grid->fine.pos[landing] - foot;
    vec2 ways[2] = {out,(toward.length() > 1e-3f) ? toward * (1.0f / toward.length()) : out};
    for (const vec2& way : ways){
        for (float s = 0.3f; s <= FLEET_GANGWAY_MOST; s += 0.15f){
            float ground;
            if (!FleetGround(w,foot + way * s,ground) || ground < deck - FLEET_GANGWAY_DROP){
                continue;
            }
            vec2 q = foot + way * std::max(s + 0.25f,1.2f);
            if (!FleetGround(w,q,ground)){
                continue;
            }
            b = vec3(q.x,ground,q.y);
            return true;
        }
    }
    return false;
}

//...and the gangway itself, from a to b: a plank, a post at its foot each side, and a rope rail each side.
static void BuildGangway(const vec3& a, const vec3& b, std::vector<vertex>& out){
    const vec3 up(0.0f,1.0f,0.0f);
    vec3 along = b - a;
    vec3 across(-along.z,0.0f,along.x);
    if (across.length() < 1e-5f){
        return;
    }
    across.normalize();
    vec3 half = across * FLEET_GANGWAY_HALF;
    vec3 under(0.0f,-0.05f,0.0f);
    MeshQuad(out,a - half,a + half,b + half,b - half,up,PAL_TIMBER);
    MeshQuad(out,a - half + under,a + half + under,b + half + under,b - half + under,up * -1.0f,PAL_TIMBER);
    MeshQuad(out,a + half,b + half,b + half + under,a + half + under,across,PAL_TIMBER);
    MeshQuad(out,a - half,b - half,b - half + under,a - half + under,across * -1.0f,PAL_TIMBER);
    for (float s : {-1.0f,1.0f}){
        vec3 edge = across * (s * (FLEET_GANGWAY_HALF + 0.03f));
        MeshPost(out,vec2(b.x + edge.x,b.z + edge.z),0.03f,b.y - 0.05f,b.y + FLEET_GANGWAY_RAIL,PAL_TIMBER);
        vec3 r0 = a + edge + up * FLEET_GANGWAY_RAIL;
        vec3 r1 = b + edge + up * FLEET_GANGWAY_RAIL;
        vec3 thick(0.0f,0.035f,0.0f);
        for (float face : {-1.0f,1.0f}){       //faced both ways, so it reads from either side
            MeshQuad(out,r0,r1,r1 - thick,r0 - thick,across * (s * face),PAL_ROPE,PAL_CROPS);
        }
    }
}

//RENDER THREAD, from PreRender.
void ApplicationChasm::UploadFleet(){
    //A chasm_props.glb from before flight has no body of its own: the finished stage, its propellers fixed on it.
    Mesh* body = dock_mesh[FLEET_BODY] ? dock_mesh[FLEET_BODY] : dock_mesh[ZEPPELIN_STAGES - 1];
    Mesh* prop = dock_mesh[FLEET_BODY] ? dock_mesh[FLEET_PROPELLER] : NULL;
    if (!body){
        return;         //none drawn - BuildDockScene says so in the log
    }
    if (!fleet_body[0]){
        world_scene->AtTickBoundary([&](){
            for (int i = 0; i < FLEET_SHOWN; i++){
                for (int k = 0; k < 3; k++){
                    Mesh* mesh = (k == 0) ? body : prop;
                    Object* o = new Object();
                    o->name = "Zeppelin " + std::to_string(i) + ((k == 0) ? " body" : (k == 1) ? " port propeller"
                                                                                                  : " starboard propeller");
                    if (mesh){
                        mesh->Retain();     //each object's own hold on it, or a SetMesh would free it under the rest
                    }
                    o->SetMesh(mesh ? mesh : new Mesh());
                    o->SetMaterialSlot(0,palette_material);
                    o->SetVisualOnly(true);
                    o->SetPickability(false);
                    o->SetVisibility(false);
                    world_scene->AddObject(o);
                    if (k == 0){
                        fleet_body[i] = o;
                    }else{
                        fleet_prop[i][k - 1] = o;
                    }
                }
                Object* g = new Object();
                g->name = "Zeppelin " + std::to_string(i) + " gangway";
                g->SetMesh(new Mesh());
                g->SetMaterialSlot(0,palette_material);
                g->SetVisualOnly(true);
                g->SetPickability(false);
                g->SetVisibility(false);
                world_scene->AddObject(g);
                fleet_gangway[i] = g;
            }
        });
        if (!fleet_body[0]){
            return;
        }
    }
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    std::shared_ptr<const EconomyState> e = GetEconomy();
    bool f_ours = w && e && e->world == w;
    double now = SimSeconds();
    float dt = (float)std::max(0.0,std::min(0.25,now - fleet_clock));
    fleet_clock = now;
    const vec3 up(0.0f,1.0f,0.0f);
    const vec3 length_way(0.0f,0.0f,1.0f);
    for (int i = 0; i < FLEET_SHOWN; i++){
        const EconomyZeppelin* f = (f_ours && i < (int)e->fleet.size()) ? &e->fleet[(size_t)i] : NULL;
        if (!f){
            fleet_id[i] = 0;
            fleet_body[i]->SetVisibility(false);
            for (Object* p : fleet_prop[i]){
                p->SetVisibility(false);
            }
            fleet_gangway[i]->SetVisibility(false);
            fleet_gangway_key[i] = 0;
            continue;
        }
        if (fleet_id[i] != f->id){
            fleet_id[i] = f->id;
            fleet_spin[i] = 0.0f;
        }
        bool f_air = f->doing != ZEPPELIN_MOORED;
        float bob = EconomyZeppelinBob(*f,e->tick);     //the rules' tick: those on its deck have it too
        float lean = f_air ? std::max(-FLEET_LEAN_MOST,std::min(FLEET_LEAN_MOST,f->turn * FLEET_LEAN)) : 0.0f;
        //The models' nose is -Z: turned so it points along the rules' heading, then leaned about its own length.
        quat q = quat(up,atan2f(-cosf(f->heading),-sinf(f->heading))) * quat(length_way,lean);
        vec3 at(f->pos.x,f->y + bob,f->pos.y);
        fleet_body[i]->SetPosition(at,false);
        fleet_body[i]->SetRotation(q,false);
        fleet_body[i]->SetVisibility(true);
        float turns = f_air ? FLEET_PROP_IDLE + FLEET_PROP_TURNS * (f->speed / ZEPPELIN_SPEED) : 0.0f;
        fleet_spin[i] = fmodf(fleet_spin[i] + dt * turns * 2.0f * FLEET_PI,2.0f * FLEET_PI);
        for (int k = 0; k < 2; k++){
            Object* p = fleet_prop[i][k];
            if (!prop){
                p->SetVisibility(false);
                continue;
            }
            //Port and starboard each the other's mirror, in their turning too, as twin screws are.
            float side = (k == 0) ? -1.0f : 1.0f;
            p->SetPosition(at + q * vec3(side * FLEET_HUB_X,FLEET_HUB_Y,FLEET_HUB_Z),false);
            p->SetRotation(q * quat(length_way,side * fleet_spin[i]),false);
            p->SetVisibility(true);
        }
        //THE GANGWAY while it is down (step 2), rebuilt when where it lies changes - by the centimetre.
        int landing = EconomyZeppelinLanding(*w,*f);
        vec3 ga, gb;
        Object* g = fleet_gangway[i];
        if (landing < 0 || !GangwayEnds(*w,*f,landing,ga,gb)){
            g->SetVisibility(false);
            fleet_gangway_key[i] = 0;
            continue;
        }
        uint64_t key = 1469598103934665603ull;
        for (float v : {ga.x,ga.y,ga.z,gb.x,gb.y,gb.z}){
            key = (key ^ (uint64_t)(int64_t)lroundf(v * 100.0f)) * 1099511628211ull;
        }
        if (key != fleet_gangway_key[i]){
            fleet_gangway_key[i] = key;
            std::vector<vertex> verts;
            BuildGangway(ga,gb,verts);
            if (!verts.empty()){
                g->GetMesh()->SetMeshData(verts.data(),(int)verts.size());
            }
        }
        g->SetVisibility(true);
    }
}

//PHYSICS THREAD, from UpdatePlayPick: the right click with zeppelin `id` selected.
void ApplicationChasm::SendZeppelinTo(uint32_t id, int2 px, const GridPick& hover){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    std::shared_ptr<const ZoneState> z = GetZones();
    if (!w || !z || z->world != w){
        return;
    }
    //A dock under the cursor, by its mast or its cells - or a mooring mast (step 3)...
    uint32_t dock = 0;
    PlayPick on = PlayPickUnder(px,hover);
    if (on.kind == PLAY_PICK_BUILDING && on.id < z->buildings.size() && (z->buildings[on.id].kind == ZONE_KIND_DOCK
                                                                          || z->buildings[on.id].kind == ZONE_KIND_MAST)){
        dock = on.id;
    }
    if (!dock && hover.f_hit && hover.coarse_quad >= 0 && hover.coarse_quad < (int)z->field.size()){
        uint32_t b = z->field[hover.coarse_quad];
        dock = (b && b < z->buildings.size() && z->buildings[b].kind == ZONE_KIND_DOCK) ? b : 0;
    }
    //...else the plot under it, if it is land, to set down by...
    int plot = (hover.f_hit && hover.plot >= 0 && hover.plot < (int)w->terrain->level.size()
                && w->terrain->level[hover.plot] != TERRAIN_FLOOR) ? hover.plot : -1;
    //...else where the ray through it is at the zeppelin's height: over the chasm, where it seems to point.
    vec2 at = hover.at;
    ray r = main_scene->camera->GetPixelRay(px);
    if (r.direction.y < -1e-4f){
        vec3 p = r.origin + r.direction * ((ZEPPELIN_CRUISE - r.origin.y) / r.direction.y);
        at = vec2(p.x,p.z);
    }
    SubmitZeppelin(id,at,plot,dock);
}
