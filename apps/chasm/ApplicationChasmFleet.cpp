#include "ApplicationChasm.h"
#include <math.h>
#include <algorithm>

/*
    THE ZEPPELINS FLYING, DRAWN (docs/flight_plan.md, step 1): every finished zeppelin where the rules have it - its body,
    zeppelin_body (the finished stage with its propellers' booms bare), and a zeppelin_propeller turning on each boom - as
    objects of their own rather than instance sets, so the selected one is outlined (UploadSelection). Over the rules'
    place, a view's own motion: off its mooring it bobs on the air and leans into its turns, and its propellers turn with
    its pace, idling while it holds still in the air. On the simulation's clock, so a pause holds it still.

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
//Its motion off its mooring (current tuning, view only).
#define FLEET_BOB           0.12f   //how far it rises and sinks on the air...
#define FLEET_BOB_SECONDS   7.0f    //...once in this long
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
            continue;
        }
        if (fleet_id[i] != f->id){
            fleet_id[i] = f->id;
            fleet_spin[i] = 0.0f;
        }
        bool f_air = f->doing != ZEPPELIN_MOORED;
        float bob = f_air ? FLEET_BOB * sinf((float)now * (2.0f * FLEET_PI / FLEET_BOB_SECONDS) + (float)f->id * 1.7f) : 0.0f;
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
    }
}

//PHYSICS THREAD, from UpdatePlayPick: the right click with zeppelin `id` selected.
void ApplicationChasm::SendZeppelinTo(uint32_t id, int2 px, const GridPick& hover){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    std::shared_ptr<const ZoneState> z = GetZones();
    if (!w || !z || z->world != w){
        return;
    }
    //A dock under the cursor, by its mast or its cells...
    uint32_t dock = 0;
    PlayPick on = PlayPickUnder(px,hover);
    if (on.kind == PLAY_PICK_BUILDING && on.id < z->buildings.size() && z->buildings[on.id].kind == ZONE_KIND_DOCK){
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
