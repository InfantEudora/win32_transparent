#include "ApplicationChasm.h"
#include "MeshBuild.h"
#include "Debug.h"
#include <math.h>
#include <algorithm>

static Debugger* debug = new Debugger("ChasmRange",DEBUG_ALL);

/*
    THE ARCHERY RANGES, DRAWN (docs/range_plan.md): on every built range cell a TARGET at its north side, a
    row of HAY BALES behind it as the backstop and one at its side - where ZoneRangeCellAt puts them, the
    rule the trainees stand by too - and ARROWS in the targets while someone trains. Each kind is one instance
    set of chasm_props.glb's prop, or of a stand-in box built here until that prop is modelled: a name the GLB
    has is used as soon as it has it, nothing else changes. The ground under them is the zone mesh's (CropMesh).

    View only, rebuilt when the zones change.
*/

const char* range_asset_names[RANGE_PROPS] = {"range_target","hay_bale","arrow"};

#define RANGE_BALE_BACK     0.44f   //the backstop's bales, this much of the cell north of its middle
#define RANGE_BALE_SPREAD   0.28f   //...and this far apart along the row
#define RANGE_BALE_SIDE     0.36f   //the loose bale, this far east of the middle
#define RANGE_ARROW_HALF    0.16f   //half an arrow's length: chasm_props.glb's `arrow` is centred on its origin
#define RANGE_BOSS_HEIGHT   0.74f   //the middle of a target's face, above its base (`range_target`)
#define RANGE_BOSS_RADIUS   0.30f   //...and how far out from it an arrow may stick
#define RANGE_BOSS_FRONT    0.14f   //...and its face, this far south of the target's origin (between its legs, behind)
#define RANGE_ARROWS_MOST   5       //arrows in a target, by the end of a stint

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
        default:
            //Centred on its origin, as the modelled one is: its point at -z, in the target.
            Box(out,vec3(-0.012f,-0.012f,-RANGE_ARROW_HALF),vec3(0.012f,0.012f,RANGE_ARROW_HALF),PAL_BARK_DARK);
            Box(out,vec3(-0.03f,-0.004f,RANGE_ARROW_HALF - 0.07f),vec3(0.03f,0.004f,RANGE_ARROW_HALF),PAL_WALL);
            break;
    }
}

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
        THE ARROWS: in the target of the cell each trainee stands in while he TRAINS, more as his stint goes on -
        one, then up to RANGE_ARROWS_MOST. Where each sticks is a hash of the cell and its number, so an arrow
        stays where it hit. From the people's state every frame, the set rebuilt only when what it shows changes.
    */
    std::shared_ptr<const EconomyState> e = GetEconomy();
    std::vector<std::pair<int,int>> shooting;       //target index, arrows
    uint64_t key = 1469598103934665603ull;
    if (e && e->world == w && !range_target_cell.empty()){
        const int stint = std::max(1,(int)std::lround(ECONOMY_TRAIN_SECONDS * CALENDAR_TICKS_PER_SECOND));     //Economy's SecondsToTicks
        for (const EconomyWorker& k : e->workers){
            if (k.state != WORKER_TRAINING){
                continue;
            }
            GridPick at = w->picker->Pick(k.pos);
            auto it = at.f_hit ? std::find(range_target_cell.begin(),range_target_cell.end(),at.coarse_quad)
                               : range_target_cell.end();
            if (it == range_target_cell.end()){
                continue;
            }
            float done = 1.0f - std::max(0,k.timer) / (float)stint;
            int arrows = 1 + (int)(done * (RANGE_ARROWS_MOST - 1));
            shooting.push_back(std::make_pair((int)(it - range_target_cell.begin()),arrows));
            key = (key ^ (uint64_t)(it - range_target_cell.begin())) * 1099511628211ull;
            key = (key ^ (uint64_t)arrows) * 1099511628211ull;
        }
    }
    if (key == range_arrows_key){
        return;
    }
    range_arrows_key = key;
    std::vector<fmat4> arrows;
    for (const std::pair<int,int>& s : shooting){
        const vec3& base = range_target_pos[s.first];
        float yaw = range_target_yaw[s.first];
        float cs = std::cos(yaw), sn = std::sin(yaw);
        for (int i = 0; i < s.second; i++){
            uint32_t h = MeshHash((uint32_t)range_target_cell[s.first],(uint32_t)i,0x6172726fu);
            float r = RANGE_BOSS_RADIUS * std::sqrt((float)(h % 1000u) / 1000.0f);
            float a = (float)((h >> 10) % 6283u) / 1000.0f;
            //On the face, a little in: the tip in the straw, the shaft and fletching out toward the line.
            vec3 local(r * std::cos(a),RANGE_BOSS_HEIGHT + r * std::sin(a),RANGE_BOSS_FRONT + RANGE_ARROW_HALF * 0.7f);
            vec3 p(base.x + local.x * cs + local.z * sn,base.y + local.y,base.z - local.x * sn + local.z * cs);
            float tilt = ((float)((h >> 20) % 1000u) / 1000.0f - 0.5f) * 0.3f;
            arrows.push_back(Object::ComposeTransformScale(p,quat(up,yaw + tilt),vec3(1.0f,1.0f,1.0f)));
        }
    }
    bool f_any = !arrows.empty();
    range_set[2]->SetInstances(std::move(arrows));
    range_set[2]->SetVisibility(f_any);
}

//RENDER THREAD: the targets and bales of every built range cell, where ZoneRangeCellAt puts them; the targets kept
//for the arrows.
void ApplicationChasm::BuildRangeProps(const ChasmWorld& cw, const ZoneState* z){
    const ChasmWorld* w = &cw;
    range_target_cell.clear();
    range_target_pos.clear();
    range_target_yaw.clear();
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
        range_target_cell.push_back((int)c);
        range_target_pos.push_back(place(0,ZoneRangeTarget(r),r.middle,wobble));
        range_target_yaw.push_back(wobble);
        vec2 back = r.centre + r.dir * (r.size * RANGE_BALE_BACK);
        for (int b = -1; b <= 1; b++){
            place(1,back + east * (r.size * RANGE_BALE_SPREAD * (float)b),r.middle,wobble * (float)(b + 2));
        }
        place(1,r.centre + east * (r.size * RANGE_BALE_SIDE),r.middle,1.5708f + wobble);
    }
    for (int k = 0; k < 2; k++){       //the arrows are UploadRange's
        bool f_any = !sets[k].empty();
        range_set[k]->SetInstances(std::move(sets[k]));
        range_set[k]->SetVisibility(f_any);
    }
}
