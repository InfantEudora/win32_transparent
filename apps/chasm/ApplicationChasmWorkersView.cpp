#include "ApplicationChasm.h"
#include "MeshBuild.h"
#include "Palette.h"
#include "AssetManager.h"
#include "Debug.h"

#include <math.h>

static Debugger* debug = new Debugger("ChasmPeople",DEBUG_ALL);

/*
    THE PEOPLE, DRAWN (docs/people_plan.md). Each person is the user's figure from chasm_props.glb - a man
    or a woman, by the person's sex - with what he carries on him while he carries it, by the GOOD: wood
    as a log along the right shoulder, food as a sack on the back, water as a pail in the right hand. (By
    the good, not the job: an idle carrier taking wood to a site carries a log, not a pail.) A woodcutter
    at work, and a builder, bob as they swing.

    View only, like the walkers: one object per person, reused, placed every frame from the published
    EconomyState. The state moves on ticks, so a person steps at the tick rate.
*/

//ApplicationChasmRoads.cpp: the walker's figure, origin at the feet, facing +z - the stand-in when the
//props file has no figure.
void BuildWalkerFigure(std::vector<vertex>& out, int tunic_column);

const char* person_asset_names[PERSON_FIGURES] = {"adult_male","adult_female","kid_male","kid_female"};

#define WORKER_LOADS        4       //nothing, a log, a sack, a pail
#define LOAD_NONE           0
#define LOAD_LOG            1
#define LOAD_SACK           2
#define LOAD_PAIL           3
#define PERSON_MODEL_SCALE  0.46f   //the figures are modelled 1.87 tall (a man); drawn the walker's 0.86
#define WORKER_CHOP_RATE    1.6f    //a working woodcutter's swings a second
#define WORKER_CHOP_BOB     0.07f   //and how far each lifts him

namespace {

//An axis-aligned box from lo to hi, flat-shaded.
void Box(std::vector<vertex>& out, const vec3& lo, const vec3& hi, int column){
    vec3 c[8];
    for (int i = 0; i < 8; i++){
        c[i] = vec3((i & 1) ? hi.x : lo.x,(i & 2) ? hi.y : lo.y,(i & 4) ? hi.z : lo.z);
    }
    MeshQuad(out,c[0],c[2],c[6],c[4],vec3(-1,0,0),column);
    MeshQuad(out,c[1],c[3],c[7],c[5],vec3(1,0,0),column);
    MeshQuad(out,c[0],c[1],c[5],c[4],vec3(0,-1,0),column);
    MeshQuad(out,c[2],c[3],c[7],c[6],vec3(0,1,0),column);
    MeshQuad(out,c[0],c[1],c[3],c[2],vec3(0,0,-1),column);
    MeshQuad(out,c[4],c[5],c[7],c[6],vec3(0,0,1),column);
}

//The load a good makes. Fitted to the figures at PERSON_MODEL_SCALE: shoulder about 0.67 up, the back
//0.10 behind the middle, the right hand about 0.23 out and 0.37 up.
int LoadOf(int good){
    switch (good){
        case GOOD_WOOD:     return LOAD_LOG;
        case GOOD_WATER:    return LOAD_PAIL;
        case -1:            return LOAD_NONE;
        default:            return LOAD_SACK;     //the crops
    }
}

void AddLoad(std::vector<vertex>& out, int load){
    switch (load){
        case LOAD_LOG:
            Box(out,vec3(0.08f,0.64f,-0.34f),vec3(0.20f,0.75f,0.30f),PAL_BARK);
            break;
        case LOAD_SACK:
            Box(out,vec3(-0.13f,0.32f,-0.27f),vec3(0.13f,0.62f,-0.09f),PAL_STONE_LIGHT);
            break;
        case LOAD_PAIL:{
            Box(out,vec3(0.19f,0.12f,-0.07f),vec3(0.33f,0.30f,0.07f),PAL_TIMBER);
            const float y = 0.302f;
            MeshQuad(out,vec3(0.21f,y,-0.05f),vec3(0.31f,y,-0.05f),vec3(0.31f,y,0.05f),vec3(0.21f,y,0.05f),
                     vec3(0,1,0),PAL_WATER);
            break;
        }
        default:
            break;
    }
}

}

/*
    RENDER THREAD, at Init once the props are loaded (LoadProps): a mesh per sex and load - the figure's
    vertices copied and scaled, the load added. Without a figure in the props file, the walker's.
*/
void ApplicationChasm::BuildWorkerScene(){
    std::vector<vertex> verts;
    for (int sex = 0; sex < 2; sex++){
        Mesh* figure = assetmanager->GetMeshFromAsset(person_asset_names[sex == 0 ? PERSON_ADULT_MALE : PERSON_ADULT_FEMALE]);
        if (!figure || figure->GetVertices().empty()){
            debug->Warn("Person figure '%s' not in chasm_props.glb - the walker's figure stands in\n",
                        person_asset_names[sex == 0 ? PERSON_ADULT_MALE : PERSON_ADULT_FEMALE]);
        }
        for (int load = 0; load < WORKER_LOADS; load++){
            if (figure && !figure->GetVertices().empty()){
                verts = figure->GetVertices();
                for (vertex& v : verts){
                    v.pos = v.pos * PERSON_MODEL_SCALE;
                }
            }else{
                BuildWalkerFigure(verts,PAL_WALL);
            }
            AddLoad(verts,load);
            Mesh* mesh = new Mesh();
            mesh->SetMeshData(verts.data(),(int)verts.size());
            /*
                Held for good. Object::SetMesh releases the mesh an object had, and the last release
                deletes it - so without this, the moment every worker carrying nothing had picked
                something up, the empty-handed figure was freed, and the next one to put his load
                down was drawn from a deleted mesh (GL errors, then a crash).
            */
            mesh->Retain();
            worker_meshes[sex * WORKER_LOADS + load] = mesh;
        }
    }
}

/*
    RENDER THREAD. One object per worker, made when there are more workers than objects and hidden
    when there are fewer. An object's mesh follows its worker's job and load; a change of mesh joins
    the scene's walk like a new object does, under its lock, and is rare (a load picked up or put
    down), so the frames between pay nothing for it.
*/
void ApplicationChasm::UploadWorkers(){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    std::shared_ptr<const EconomyState> e = GetEconomy();
    size_t n = (w && e && e->world == w) ? e->workers.size() : 0;
    if (worker_objects.size() < n){
        main_scene->AtTickBoundary([&](){
            while (worker_objects.size() < n){
                Object* o = new Object();
                o->name = "Worker " + std::to_string(worker_objects.size());
                o->SetMesh(worker_meshes[0]);
                o->SetMaterialSlot(0,palette_material);
                o->SetPickability(false);
                o->SetVisualOnly(true);
                o->SetVisibility(false);
                main_scene->AddObject(o);
                worker_objects.push_back(o);
                worker_object_mesh.push_back(0);
            }
        });
    }
    std::vector<int> want(n,0);
    bool f_swap = false;
    for (size_t i = 0; i < n; i++){
        const EconomyWorker& k = e->workers[i];
        want[i] = (k.f_female ? WORKER_LOADS : 0) + (k.carry > 0 ? LoadOf(k.carry_good) : LOAD_NONE);
        f_swap = f_swap || want[i] != worker_object_mesh[i];
    }
    if (f_swap){
        main_scene->AtTickBoundary([&](){
            for (size_t i = 0; i < n; i++){
                if (want[i] != worker_object_mesh[i]){
                    worker_objects[i]->SetMesh(worker_meshes[want[i]]);
                    worker_object_mesh[i] = want[i];
                }
            }
        });
    }
    float seconds = (float)SimSeconds();
    for (size_t i = 0; i < worker_objects.size(); i++){
        Object* o = worker_objects[i];
        if (i >= n){
            o->SetVisibility(false);
            continue;
        }
        const EconomyWorker& k = e->workers[i];
        //At home, he is indoors (P4): not drawn.
        if (EconomyIndoors(k)){
            o->SetVisibility(false);
            continue;
        }
        //On the ground of the plot he stands on.
        GridPick pick = w->picker->Pick(k.pos);
        float y = pick.f_hit ? w->terrain->GroundHeight(k.pos,w->terrain->Height(pick.plot)) : 0.0f;
        if ((k.job == WORKER_JOB_WOODCUTTER && k.state == WORKER_WORKING) || k.state == WORKER_BUILDING){
            //Each worker on his own beat, so two at one tree (or one site) do not swing as one.
            float phase = seconds * WORKER_CHOP_RATE + (float)i * 0.37f;
            y += WORKER_CHOP_BOB * fabsf(sinf(phase * 3.1415927f));
        }
        vec2 facing = EconomyWorkerFacing(k);
        if (facing.dot(facing) < 1e-8f){
            facing = vec2(0.0f,1.0f);
        }
        o->SetPosition(vec3(k.pos.x,y,k.pos.y),false);
        o->SetRotation(quat(vec3(0.0f,1.0f,0.0f),atan2f(facing.x,facing.y)),false);
        o->SetVisibility(true);
    }
}
